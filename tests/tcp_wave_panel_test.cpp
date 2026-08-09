#include "ground/wave/TcpWavePanel.h"
#include "LogService.h"

#include <QApplication>
#include <QByteArray>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QVector>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

template <typename Predicate>
bool waitUntil(Predicate predicate, int timeoutMs = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (predicate())
        {
            return true;
        }
    }
    return predicate();
}

QByteArray encodeValues(const std::vector<float>& values, VaporView::TcpFloatEncoding encoding)
{
    QByteArray payload;
    payload.reserve(static_cast<int>(values.size() * sizeof(float)));
    for (float value : values)
    {
        quint32 bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        const char little[4] = {
            static_cast<char>(bits & 0xFFu),
            static_cast<char>((bits >> 8) & 0xFFu),
            static_cast<char>((bits >> 16) & 0xFFu),
            static_cast<char>((bits >> 24) & 0xFFu),
        };
        const char big[4] = {little[3], little[2], little[1], little[0]};
        const char wordSwapped[4] = {little[0], little[1], little[3], little[2]};
        switch (encoding)
        {
        case VaporView::TcpFloatEncoding::LittleEndian:
            payload.append(little, sizeof(little));
            break;
        case VaporView::TcpFloatEncoding::BigEndian:
            payload.append(big, sizeof(big));
            break;
        case VaporView::TcpFloatEncoding::WordSwappedLittleEndian:
            payload.append(wordSwapped, sizeof(wordSwapped));
            break;
        case VaporView::TcpFloatEncoding::Unknown:
        default:
            payload.append(little, sizeof(little));
            break;
        }
    }
    return payload;
}

std::vector<float> sawtoothRawValues(int count)
{
    std::vector<float> values;
    values.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        const int phase = i % 80;
        values.push_back(static_cast<float>((static_cast<double>(phase) / 79.0) * 2.0 - 1.0));
    }
    return values;
}

std::vector<float> smoothHarmonicValues(int count)
{
    std::vector<float> values;
    values.reserve(static_cast<size_t>(count));
    const double center = count * 0.45;
    const double width = count * 0.08;
    for (int i = 0; i < count; ++i)
    {
        const double x = static_cast<double>(i) / std::max(1, count - 1);
        const double distance = (static_cast<double>(i) - center) / width;
        const double peak = std::exp(-distance * distance);
        values.push_back(static_cast<float>(0.08 * std::sin(2.0 * 3.14159265358979323846 * x) + peak));
    }
    return values;
}

QByteArray lengthPrefixedFrame(const QByteArray& payload)
{
    QByteArray frame(4, '\0');
    qToLittleEndian<qint32>(payload.size(), reinterpret_cast<uchar*>(frame.data()));
    frame.append(payload);
    return frame;
}

const VaporView::LogRecord *findRecord(const std::vector<VaporView::LogRecord>& records,
                                       const QString& event)
{
    for (const VaporView::LogRecord& record : records)
    {
        if (record.fields.value(QStringLiteral("event")).toString() == event)
        {
            return &record;
        }
    }
    return nullptr;
}

void requireTcpLog(const VaporView::LogRecord *record,
                   VaporView::LogLevel level,
                   const QString& event,
                   const QString& visibility)
{
    require(record != nullptr, "expected structured TCP wave log record");
    require(record->level == level, "TCP wave log level");
    require(record->source == QStringLiteral("Ground"), "TCP wave log source");
    require(record->category == QStringLiteral("telemetry.wave.tcp"), "TCP wave log category");
    require(record->fields.value(QStringLiteral("event")).toString() == event, "TCP wave log event");
    require(record->fields.value(QStringLiteral("ui_visibility")).toString() == visibility,
            "TCP wave log ui visibility");
    const bool containsChineseText =
        std::any_of(record->message.cbegin(), record->message.cend(), [](QChar ch) {
            return ch.unicode() > 0x7f;
        });
    require(!record->message.isEmpty() && containsChineseText,
            "TCP wave log message is Chinese");
}

void testInvalidPortPublishesStructuredWarning()
{
    QTemporaryDir logDir;
    require(logDir.isValid(), "invalid-port log directory");
    VaporView::LogService service(QStringLiteral("TcpWavePanelInvalidPortTest"), nullptr, logDir.path());
    std::vector<VaporView::LogRecord> records;
    QObject::connect(&service, &VaporView::LogService::recordPublished,
                     &service,
                     [&](const VaporView::LogRecord& record) { records.push_back(record); },
                     Qt::DirectConnection);

    TcpWavePanel panel;
    panel.testSetConnectionEndpoint(QStringLiteral("127.0.0.1"), QStringLiteral("70000"));
    panel.toggleConnection();

    const VaporView::LogRecord *record =
        findRecord(records, QStringLiteral("tcp_wave_connection_rejected_invalid_port"));
    requireTcpLog(record,
                  VaporView::LogLevel::Warning,
                  QStringLiteral("tcp_wave_connection_rejected_invalid_port"),
                  QStringLiteral("attention"));
    require(record->fields.value(QStringLiteral("reason_code")).toString() == QStringLiteral("INVALID_PORT"),
            "invalid port reason_code");
    require(record->fields.value(QStringLiteral("input_value")).toString() == QStringLiteral("70000"),
            "invalid port preserves input value");
}

void testSocketLifecyclePublishesStructuredLogs()
{
    QTemporaryDir logDir;
    require(logDir.isValid(), "socket log directory");
    VaporView::LogService service(QStringLiteral("TcpWavePanelSocketTest"), nullptr, logDir.path());
    std::vector<VaporView::LogRecord> records;
    QObject::connect(&service, &VaporView::LogService::recordPublished,
                     &service,
                     [&](const VaporView::LogRecord& record) { records.push_back(record); },
                     Qt::DirectConnection);

    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost, 0), "test server listen");
    TcpWavePanel panel;
    panel.testSetConnectionEndpoint(QStringLiteral("127.0.0.1"), QString::number(server.serverPort()));
    panel.toggleConnection();
    require(waitUntil([&]() { return server.hasPendingConnections(); }), "test server accepts TCP wave client");
    QTcpSocket *accepted = server.nextPendingConnection();
    require(accepted != nullptr, "accepted socket");
    require(waitUntil([&]() {
        return findRecord(records, QStringLiteral("tcp_wave_connected")) != nullptr;
    }), "connect success log observed");

    requireTcpLog(findRecord(records, QStringLiteral("tcp_wave_connection_started")),
                  VaporView::LogLevel::Info,
                  QStringLiteral("tcp_wave_connection_started"),
                  QStringLiteral("details"));
    requireTcpLog(findRecord(records, QStringLiteral("tcp_wave_connected")),
                  VaporView::LogLevel::Info,
                  QStringLiteral("tcp_wave_connected"),
                  QStringLiteral("details"));

    QByteArray backlogBytes(5 * 1024 * 1024, '\0');
    qToLittleEndian<qint32>(8 * 1024 * 1024,
                            reinterpret_cast<uchar*>(backlogBytes.data()));
    panel.testFeedReadyReadBytes(backlogBytes);
    require(findRecord(records, QStringLiteral("tcp_wave_receive_backlog")) != nullptr,
            "receive backlog warning log observed");
    const VaporView::LogRecord *backlog =
        findRecord(records, QStringLiteral("tcp_wave_receive_backlog"));
    requireTcpLog(backlog,
                  VaporView::LogLevel::Warning,
                  QStringLiteral("tcp_wave_receive_backlog"),
                  QStringLiteral("attention"));
    require(backlog->fields.value(QStringLiteral("ui_dedupe_key")).toString() ==
                QStringLiteral("tcp_wave:receive_backlog"),
            "receive backlog has stable ui_dedupe_key");

    accepted->disconnectFromHost();
    require(waitUntil([&]() {
        return findRecord(records, QStringLiteral("tcp_wave_disconnected_unexpectedly")) != nullptr;
    }, 5000), "unexpected disconnect log observed");
    const VaporView::LogRecord *disconnect =
        findRecord(records, QStringLiteral("tcp_wave_disconnected_unexpectedly"));
    requireTcpLog(disconnect,
                  VaporView::LogLevel::Warning,
                  QStringLiteral("tcp_wave_disconnected_unexpectedly"),
                  QStringLiteral("attention"));
    require(disconnect->fields.value(QStringLiteral("reason_code")).toString() ==
                QStringLiteral("REMOTE_DISCONNECTED"),
            "unexpected disconnect reason_code");
}

void testSocketErrorPublishesStructuredError()
{
    QTemporaryDir logDir;
    require(logDir.isValid(), "socket-error log directory");
    VaporView::LogService service(QStringLiteral("TcpWavePanelSocketErrorTest"), nullptr, logDir.path());
    std::vector<VaporView::LogRecord> records;
    QObject::connect(&service, &VaporView::LogService::recordPublished,
                     &service,
                     [&](const VaporView::LogRecord& record) { records.push_back(record); },
                     Qt::DirectConnection);

    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost, 0), "reserve socket error port");
    const quint16 port = server.serverPort();
    server.close();

    TcpWavePanel panel;
    panel.testSetConnectionEndpoint(QStringLiteral("127.0.0.1"), QString::number(port));
    panel.toggleConnection();
    require(waitUntil([&]() {
        return findRecord(records, QStringLiteral("tcp_wave_socket_error")) != nullptr;
    }, 5000), "socket error log observed");
    const VaporView::LogRecord *record = findRecord(records, QStringLiteral("tcp_wave_socket_error"));
    requireTcpLog(record,
                  VaporView::LogLevel::Error,
                  QStringLiteral("tcp_wave_socket_error"),
                  QStringLiteral("attention"));
    require(record->fields.value(QStringLiteral("error_code")).toString() == QStringLiteral("SOCKET_ERROR"),
            "socket error_code");
    require(record->fields.contains(QStringLiteral("system_error")), "socket error preserves system_error");
    require(record->fields.contains(QStringLiteral("socket_error_code")), "socket error preserves socket_error_code");
}

void testInvalidStreamAndPayloadCorrectionPublishStructuredLogs()
{
    QTemporaryDir logDir;
    require(logDir.isValid(), "parser log directory");
    VaporView::LogService service(QStringLiteral("TcpWavePanelParserTest"), nullptr, logDir.path());
    std::vector<VaporView::LogRecord> records;
    QObject::connect(&service, &VaporView::LogService::recordPublished,
                     &service,
                     [&](const VaporView::LogRecord& record) { records.push_back(record); },
                     Qt::DirectConnection);

    TcpWavePanel panel;
    const QByteArray invalidChunk(8192, static_cast<char>(0x7f));
    for (int i = 0; i < 1024; ++i)
    {
        panel.testFeedSocketBytes(invalidChunk);
    }
    const VaporView::LogRecord *resync = findRecord(records, QStringLiteral("tcp_wave_resynchronized"));
    requireTcpLog(resync,
                  VaporView::LogLevel::Debug,
                  QStringLiteral("tcp_wave_resynchronized"),
                  QStringLiteral("hidden"));
    require(resync->fields.value(QStringLiteral("ui_dedupe_key")).toString() ==
                QStringLiteral("tcp_wave:resynchronized"),
            "resync has stable ui_dedupe_key");
    const VaporView::LogRecord *invalid =
        findRecord(records, QStringLiteral("tcp_wave_invalid_stream_disconnected"));
    requireTcpLog(invalid,
                  VaporView::LogLevel::Error,
                  QStringLiteral("tcp_wave_invalid_stream_disconnected"),
                  QStringLiteral("attention"));
    require(invalid->fields.value(QStringLiteral("error_code")).toString() ==
                QStringLiteral("INVALID_WAVE_STREAM"),
            "invalid stream error_code");
    require(panel.testBufferedByteCount() <= 3, "invalid TCP stream backlog should stay bounded");

    records.clear();
    const QByteArray rawPayload =
        encodeValues(sawtoothRawValues(2000), VaporView::TcpFloatEncoding::LittleEndian);
    const QByteArray harmonicPayload =
        encodeValues(smoothHarmonicValues(1800), VaporView::TcpFloatEncoding::LittleEndian);
    panel.testFeedSocketBytes(lengthPrefixedFrame(harmonicPayload) + lengthPrefixedFrame(rawPayload));
    const VaporView::LogRecord *correction =
        findRecord(records, QStringLiteral("tcp_wave_payload_order_corrected"));
    requireTcpLog(correction,
                  VaporView::LogLevel::Info,
                  QStringLiteral("tcp_wave_payload_order_corrected"),
                  QStringLiteral("details"));
    require(correction->fields.value(QStringLiteral("confidence")).toDouble() >= 10.0,
            "payload correction preserves confidence");
}

void testInvalidTcpStreamDoesNotGrowBacklog()
{
    TcpWavePanel panel;
    panel.setEnglish(true);

    const QByteArray invalidChunk(8192, static_cast<char>(0x7f));
    for (int i = 0; i < 1024; ++i)
    {
        panel.testFeedSocketBytes(invalidChunk);
    }

    require(panel.testBufferedByteCount() <= 3, "invalid TCP stream backlog should stay bounded");
}

void testWavePlotXAxisLabelsDefaultToChinese()
{
    TcpWavePanel panel;

    const QVector<float> samples(512, 0.25f);
    panel.injectRemoteRawSignalFrame(1, samples);
    panel.injectRemoteSecondHarmonicFrame(2, samples);
    for (int frame = 0; frame < 11; ++frame)
    {
        VaporView::WaveformFeature feature;
        feature.host_time_us = static_cast<quint64>(frame + 1) * 1000ULL;
        feature.peak = 0.5f + static_cast<float>(frame) * 0.01f;
        feature.rms = 0.1f;
        feature.quality_flags = 0;
        panel.injectRemoteWaveformFeature(feature);
    }
    panel.testFlushLiveDisplay();

    require(panel.testRawXAxisLabel() == QStringLiteral("512 点"),
            "raw waveform x-axis defaults samples to Chinese");
    require(panel.testHarmonicXAxisLabel() == QStringLiteral("512 点"),
            "harmonic waveform x-axis defaults samples to Chinese");
    require(panel.testPeakXAxisStartLabel() == QStringLiteral("1"),
            "peak trend x-axis shows the visible range start");
    require(panel.testPeakXAxisLabel() == QStringLiteral("显示范围1-11/缓存11点"),
            "peak trend x-axis centers a Chinese visible range and cache label");
    require(panel.testPeakXAxisEndLabel() == QStringLiteral("11"),
            "peak trend x-axis shows the visible range end");
    require(panel.testWavePlotBottomMarginExtra() >= 8 &&
                panel.testPeakPlotBottomMarginExtra() >= 8,
            "waveform plots reserve extra bottom margin for full x-axis labels");

    panel.setEnglish(true);
    require(panel.testRawXAxisLabel() == QStringLiteral("512 samples"),
            "raw waveform x-axis still supports English samples");
    require(panel.testPeakXAxisLabel() == QStringLiteral("Visible range 1-11 / cache 11 pts"),
            "peak trend x-axis still supports English range text");
}

}  // namespace

int main(int argc, char **argv)
{
    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), "temporary settings directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("VaporViewTcpWavePanelTest"));
    app.setApplicationName(QStringLiteral("tcp_wave_panel_test"));

    testInvalidPortPublishesStructuredWarning();
    testSocketLifecyclePublishesStructuredLogs();
    testSocketErrorPublishesStructuredError();
    testInvalidStreamAndPayloadCorrectionPublishStructuredLogs();
    testInvalidTcpStreamDoesNotGrowBacklog();
    testWavePlotXAxisLabelsDefaultToChinese();
    std::cout << "tcp_wave_panel_test passed\n";
    return 0;
}
