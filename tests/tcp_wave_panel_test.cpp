#include "test_settings_sandbox.h"
#include "ground/wave/TcpWavePanel.h"
#include "ground/widgets/SegmentedSwitchButton.h"
#include "LogService.h"

#include <QApplication>
#include <QComboBox>
#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QLayout>
#include <QPushButton>
#include <QPixmap>
#include <QSettings>
#include <QStringList>
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
#include <limits>
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

bool isClockTimeLabel(const QString& label)
{
    return label.size() == 8 &&
        label.at(2) == QLatin1Char(':') &&
        label.at(5) == QLatin1Char(':') &&
        label.at(0).isDigit() && label.at(1).isDigit() &&
        label.at(3).isDigit() && label.at(4).isDigit() &&
        label.at(6).isDigit() && label.at(7).isDigit();
}

void requireTimeXAxis(QWidget *plot, const char *message)
{
    require(plot != nullptr, message);
    require(plot->property("xAxisTimeMode").toBool(), message);
    require(plot->property("xAxisTimeLabelFormat").toString() == QStringLiteral("hh:mm:ss"), message);
    const QStringList labels = plot->property("xAxisTickLabels").toStringList();
    require(labels.size() >= 2, message);
    for (const QString& label : labels)
    {
        require(isClockTimeLabel(label), message);
    }
}

void testWavePlotXAxisLabelsUseClockTime()
{
    TcpWavePanel panel;

    const QVector<float> samples(512, 0.25f);
    constexpr quint64 baseTimestampUs = 1'700'000'000'000'000ULL;
    panel.injectRemoteRawSignalFrame(baseTimestampUs, samples);
    panel.injectRemoteSecondHarmonicFrame(baseTimestampUs + 500'000ULL, samples);
    for (int frame = 0; frame < 11; ++frame)
    {
        VaporView::WaveformFeature feature;
        feature.host_time_us = baseTimestampUs + static_cast<quint64>(frame) * 1'000'000ULL;
        feature.peak = 0.5f + static_cast<float>(frame) * 0.01f;
        feature.rms = 0.1f;
        feature.quality_flags = 0;
        panel.injectRemoteWaveformFeature(feature);
    }
    panel.testFlushLiveDisplay();

    requireTimeXAxis(panel.findChild<QWidget *>(QStringLiteral("tcpWaveRawPlot")),
                     "raw waveform x-axis uses clock time");
    requireTimeXAxis(panel.findChild<QWidget *>(QStringLiteral("tcpWaveHarmonicPlot")),
                     "harmonic waveform x-axis uses clock time");
    requireTimeXAxis(panel.findChild<QWidget *>(QStringLiteral("tcpWavePeakTrendPlot")),
                     "peak trend x-axis uses clock time");
    require(isClockTimeLabel(panel.testRawXAxisLabel()),
            "raw waveform x-axis right label is hh:mm:ss");
    require(isClockTimeLabel(panel.testHarmonicXAxisLabel()),
            "harmonic waveform x-axis right label is hh:mm:ss");
    require(isClockTimeLabel(panel.testPeakXAxisStartLabel()) &&
                isClockTimeLabel(panel.testPeakXAxisEndLabel()),
            "peak trend x-axis endpoints are hh:mm:ss");
    require(panel.testRawXAxisLabel() ==
                QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(baseTimestampUs / 1000ULL))
                    .toLocalTime().toString(QStringLiteral("hh:mm:ss")),
            "raw waveform x-axis is anchored to the frame timestamp");
    require(panel.testPeakXAxisEndLabel() ==
                QDateTime::fromMSecsSinceEpoch(static_cast<qint64>((baseTimestampUs + 10'000'000ULL) / 1000ULL))
                    .toLocalTime().toString(QStringLiteral("hh:mm:ss")),
            "peak trend x-axis is anchored to the newest feature timestamp");
    require(panel.testWavePlotBottomMarginExtra() >= 8 &&
                panel.testPeakPlotBottomMarginExtra() >= 8,
            "waveform plots reserve extra bottom margin for full x-axis labels");

    panel.setEnglish(true);
    require(isClockTimeLabel(panel.testRawXAxisLabel()) &&
                isClockTimeLabel(panel.testHarmonicXAxisLabel()) &&
                isClockTimeLabel(panel.testPeakXAxisEndLabel()),
            "English mode keeps clock time x-axis labels");
}

bool hasLabelText(QWidget *root, const QString& text)
{
    if (!root)
    {
        return false;
    }
    for (QLabel *label : root->findChildren<QLabel *>())
    {
        if (label && label->text() == text)
        {
            return true;
        }
    }
    return false;
}

bool hasButtonText(QWidget *root, const QString& text)
{
    if (!root)
    {
        return false;
    }
    for (QPushButton *button : root->findChildren<QPushButton *>())
    {
        if (button && button->text() == text)
        {
            return true;
        }
    }
    return false;
}

bool endpointEditorsEnabled(QWidget *root, bool expectedEnabled)
{
    const QList<QLineEdit *> editors = root ? root->findChildren<QLineEdit *>() : QList<QLineEdit *>();
    if (editors.size() < 2)
    {
        return false;
    }
    for (QLineEdit *editor : editors)
    {
        if (!editor || editor->isEnabled() != expectedEnabled)
        {
            return false;
        }
    }
    return true;
}

void testRemoteSkyModeKeepsLocalEndpointText()
{
    TcpWavePanel panel;
    panel.setEnglish(false);
    require(hasLabelText(&panel, QStringLiteral("TCP主机:")) &&
                hasLabelText(&panel, QStringLiteral("端口:")),
            "local TCP wave labels describe the local TCP endpoint");
    require(hasButtonText(&panel, QStringLiteral("连接")),
            "local TCP wave connect button uses the shared text");

    panel.setRemoteSkyMode(true);
    require(hasLabelText(&panel, QStringLiteral("TCP主机:")) &&
                hasLabelText(&panel, QStringLiteral("端口:")),
            "remote TCP wave mode keeps the shared endpoint labels");
    require(!hasLabelText(&panel, QStringLiteral("天空端波形主机:")) &&
                !hasLabelText(&panel, QStringLiteral("天空端波形端口:")),
            "remote TCP wave mode does not expose separate Sky endpoint wording");
    require(hasButtonText(&panel, QStringLiteral("连接")),
            "remote TCP wave mode keeps the shared connect text");
    require(endpointEditorsEnabled(&panel, true),
            "remote TCP wave disconnected mode keeps endpoint editors usable");

    panel.setRemoteWaveTcpState(VaporView::DeviceState::Connected);
    require(hasButtonText(&panel, QStringLiteral("断开")),
            "remote TCP wave connected state keeps the shared disconnect text");
    require(endpointEditorsEnabled(&panel, false),
            "remote TCP wave connected state locks endpoint editors like local mode");

    panel.setRemoteWaveTcpState(VaporView::DeviceState::Disconnected);
    require(endpointEditorsEnabled(&panel, true),
            "remote TCP wave disconnected state restores endpoint editing");

    panel.setEnglish(true);
    require(hasLabelText(&panel, QStringLiteral("TCP Host:")) &&
                hasLabelText(&panel, QStringLiteral("Port:")),
            "remote TCP wave mode keeps the shared English endpoint labels");
    require(!hasLabelText(&panel, QStringLiteral("Sky Wave Host:")) &&
                !hasLabelText(&panel, QStringLiteral("Sky Wave Port:")),
            "remote TCP wave mode does not expose separate English Sky endpoint wording");
    require(hasButtonText(&panel, QStringLiteral("Connect")),
            "remote TCP wave disconnected state keeps the shared English connect text");

    panel.setRemoteSkyMode(false);
    require(hasLabelText(&panel, QStringLiteral("TCP Host:")) &&
                hasLabelText(&panel, QStringLiteral("Port:")),
            "local TCP wave labels are restored when leaving remote mode");
}

void testPeakPlotModeSwitch()
{
    TcpWavePanel panel;
    panel.setFpgaMode(true);
    for (quint32 cycle=1;cycle<=4;++cycle)
        panel.setFpgaSecondHarmonicPeak(0x30,cycle,quint64(cycle)*100000000,cycle%2?10:30,false);
    panel.resize(1500,900);panel.show();QApplication::processEvents();
    auto* mode=panel.findChild<VaporView::Ground::Widgets::SegmentedSwitchButton*>("tcpWavePeakPlotModeSwitch");
    auto* plot=panel.findChild<QWidget*>("tcpWavePeakTrendPlot");
    require(mode&&mode->switchChecked()&&mode->leftSegmentText()==QStringLiteral("折线")&&mode->rightSegmentText()==QStringLiteral("散点"),
            "home trend capsule shows the current scatter mode");
    const auto values=panel.testPeakValues();
    const auto scatter=plot->grab().toImage();
    mode->click();QApplication::processEvents();
    require(!mode->switchChecked()&&mode->accessibleName().endsWith(QStringLiteral("折线"))&&plot->grab().toImage()!=scatter,
            "home capsule selects a visibly different polyline rendering");
    require(waitUntil([&]{return !mode->switchAnimationRunning();}),"home capsule settles on the selected segment");
    panel.setEnglish(true);
    require(!mode->switchChecked()&&mode->leftSegmentText()=="Line"&&mode->rightSegmentText()=="Scatter",
            "translation preserves the selected plot mode");
    panel.setEnglish(false);
    mode->click();QApplication::processEvents();
    require(mode->switchChecked()&&plot->grab().toImage()==scatter&&panel.testPeakValues()==values,
            "switching back restores scatter rendering without changing peak history");
    require(waitUntil([&]{return !mode->switchAnimationRunning();}),"home capsule restores its selected segment");
}

void testNormalizedWaveformGapsRender()
{
    TcpWavePanel panel;panel.setFpgaMode(true);panel.resize(1500,900);panel.show();
    panel.setFpgaWaveform(0x30,{{1,1,1,1,1}},{"H2/H1"},"normalized",5);
    QApplication::processEvents();
    auto *plot=panel.findChild<QWidget*>("tcpWaveHarmonicPlot");
    const auto connected=plot->grab().toImage();
    const double invalid=std::numeric_limits<double>::quiet_NaN();
    panel.setFpgaWaveform(0x30,{{1,1,invalid,1,1}},{"H2/H1"},"normalized",5);
    const auto gap=plot->grab().toImage();
    const QRect center(connected.width()/2-3,0,6,connected.height());
    require(connected.copy(center)!=gap.copy(center),"invalid normalized samples visibly break the waveform instead of joining across the gap");
    panel.setFpgaWaveform(0x30,{{invalid,invalid,invalid,invalid,invalid}},{"H2/H1"},"normalized",5);
    require(plot->grab().toImage()!=gap,"all-invalid normalized waveforms render the empty state rather than a numeric trace");
    panel.setFpgaWaveform(0x30,{{invalid,1,1,1,1}},{"H2/H1"},"normalized",5);
    const QRect right(connected.width()*3/4,0,6,connected.height());
    require(plot->grab().toImage().copy(right)==connected.copy(right),"an invalid first reference does not suppress the remaining finite waveform");
}

void testFpgaPeakHeaderStaysStable()
{
    TcpWavePanel panel; panel.setFpgaMode(true); panel.resize(1200,900); panel.show();
    auto *info=panel.findChild<QLabel*>("fpgaMonitorPeakInfo");
    auto *mode=panel.findChild<QWidget*>("tcpWavePeakPlotModeSwitch");
    require(info&&mode,"FPGA peak header controls exist");
    auto *header=info->parentWidget();
    QPushButton *clear=nullptr;
    for(auto *button:header->findChildren<QPushButton*>("compactTcpButton"))
        if(!button->isHidden())clear=button;
    require(clear,"FPGA peak clear button exists");
    quint32 cycle=0;
    for(bool english:{false,true}) {
        panel.setEnglish(english); QApplication::processEvents();
        const auto modeRect=mode->geometry(),clearRect=clear->geometry(),infoRect=info->geometry();
        require(info->alignment().testFlag(Qt::AlignRight)&&infoRect.left()>clearRect.right()
            &&infoRect.right()==header->width()-header->layout()->contentsMargins().right()-1,
            "latest FPGA peak occupies the right edge after both buttons");
        for(double value:{1.0,0.2281666,1.234567e-20,1.234567e30,std::numeric_limits<double>::quiet_NaN()}) {
            ++cycle;panel.setFpgaSecondHarmonicPeak(0x30,cycle,cycle,value,false);QApplication::processEvents();
            require(mode->geometry()==modeRect&&clear->geometry()==clearRect&&info->geometry()==infoRect,
                "short, fractional, scientific and invalid peaks never move the header buttons");
            require(info->fontMetrics().horizontalAdvance(info->text())<=info->width(),
                "latest peak text fits the remaining title space");
        }
    }
}

}  // namespace

int main(int argc, char **argv)
{
    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), "temporary settings directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir.path());

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("VaporViewTcpWavePanelTest"));
    app.setApplicationName(QStringLiteral("tcp_wave_panel_test"));

    testInvalidPortPublishesStructuredWarning();
    testSocketLifecyclePublishesStructuredLogs();
    testSocketErrorPublishesStructuredError();
    testInvalidStreamAndPayloadCorrectionPublishStructuredLogs();
    testInvalidTcpStreamDoesNotGrowBacklog();
    testWavePlotXAxisLabelsUseClockTime();
    {
        TcpWavePanel panel;
        panel.setRemoteSkyMode(true);
        panel.setUiTestMode(true);
        panel.setRemoteWaveTcpState(VaporView::DeviceState::Disconnected);
        const QVector<float> samples{0.1f, 0.8f, 0.2f};
        VaporView::WaveformFeature feature;
        feature.host_time_us = 1'700'000'000'000'000ULL;
        feature.peak = 0.8f;
        feature.quality_flags = 0;
        panel.injectRemoteRawSignalFrame(feature.host_time_us, samples);
        panel.injectRemoteSecondHarmonicFrame(feature.host_time_us, samples);
        panel.injectRemoteWaveformFeature(feature);
        panel.testFlushLiveDisplay();
        require(isClockTimeLabel(panel.testRawXAxisLabel()) &&
                    isClockTimeLabel(panel.testHarmonicXAxisLabel()) &&
                    isClockTimeLabel(panel.testPeakXAxisLabel()),
                "all three UI-test plots receive data without a real remote connection");
        panel.setUiTestConnected(false);
        const QString previousTime = panel.testRawXAxisLabel();
        panel.injectRemoteRawSignalFrame(feature.host_time_us + 10'000'000ULL, samples);
        panel.testFlushLiveDisplay();
        require(panel.testRawXAxisLabel() == previousTime,
                "simulated disconnect stops incoming waveform frames");
        panel.setUiTestMode(false);
        require(!panel.isConnected(),
                "leaving UI test mode returns the panel to disconnected state");
    }
    testRemoteSkyModeKeepsLocalEndpointText();
    testPeakPlotModeSwitch();
    testNormalizedWaveformGapsRender();
    testFpgaPeakHeaderStaysStable();
    {
        TcpWavePanel panel;panel.setConnectionEndpoint("127.0.0.9",9901);panel.setFpgaMode(true);
        panel.setFpgaWaveform(0x20,{{1,2,3}},{"ADC"},"ADC complete",3);
        panel.setFpgaWaveform(0x30,{{10,20,30},{40,50,60},{4,2.5,2}},{"H1","H2","H2/H1"},"DLIA complete",3);
        auto *raw=panel.findChild<QWidget*>("tcpWaveRawPlot");
        auto *dlia=panel.findChild<QWidget*>("tcpWaveHarmonicPlot");
        auto *h2=panel.findChild<QWidget*>("fpgaWaveH2Plot");
        auto *peaks=panel.findChild<QWidget*>("tcpWavePeakTrendPlot");
        panel.resize(1500,900);panel.show();QApplication::processEvents();
        require(raw->isVisible()&&h2->isVisible()&&dlia->isVisible()&&peaks->isVisible(),"FPGA monitor displays all four graphs");
        auto *rawButton=panel.findChild<QPushButton*>("fpgaMonitorRawButton");
        auto *harmonicButton=panel.findChild<QPushButton*>("fpgaMonitorHarmonicButton");
        require(rawButton&&harmonicButton&&rawButton->isVisible()&&harmonicButton->isVisible()
            &&!rawButton->isEnabled()&&!harmonicButton->isEnabled(),"relay title buttons wait for hardware readiness");
        panel.setFpgaAcquisitionState(true,false,false,false);QApplication::processEvents();
        require(rawButton->isEnabled()&&harmonicButton->isEnabled()
            &&!rawButton->geometry().intersects(harmonicButton->geometry()),"two ready controls fit the title bar without overlap");
        bool requestedRaw=false,requestedEnabled=false;int requestedCount=0;
        QObject::connect(&panel,&TcpWavePanel::fpgaAcquisitionRequested,&panel,[&](bool raw,bool enabled){
            requestedRaw=raw;requestedEnabled=enabled;++requestedCount;
        });
        rawButton->click();require(requestedRaw&&requestedEnabled&&requestedCount==1,"raw control requests stream start");
        require(rawButton->text().contains(QStringLiteral("开始")),"click does not invent confirmed acquisition state");
        panel.setFpgaAcquisitionState(true,true,false,false);rawButton->click();harmonicButton->click();
        require(requestedCount==1,"busy relay controls cannot duplicate requests");
        panel.setFpgaAcquisitionState(true,false,true,true);
        harmonicButton->click();require(!requestedRaw&&!requestedEnabled&&requestedCount==2,"harmonic control stops shared acquisition");
        panel.setEnglish(true);require(rawButton->text().contains("Stop")&&harmonicButton->text().contains("Stop"),"acquisition buttons translate with readback state");
        panel.setEnglish(false);
        QStringList titles;
        for(auto *label:panel.findChildren<QLabel*>("sectionTitleLabel"))titles.append(label->text());
        require(titles.contains("波形监控")&&titles.contains("一次谐波H1")&&titles.contains("二次谐波H2")
            &&!titles.contains("原始信号")&&titles.contains("归一化二次谐波")
            &&titles.contains("归一化二次谐波峰值趋势"),"FPGA monitor names both normalized graphs explicitly");
        require(panel.testFirstHarmonicSamples()==QVector<float>({10,20,30})
            &&panel.testSecondHarmonicSamples()==QVector<float>({40,50,60})
            &&h2->property("sampleCount").toInt()==3&&dlia->property("sampleCount").toInt()==3,
            "FPGA mode renders H1 and H2 from the same DLIA cycle instead of ADC RAW");
        panel.setFpgaWaveform(0x20,{{99,98}},{"ADC"},"ADC only",2);
        require(panel.testFirstHarmonicSamples()==QVector<float>({10,20,30}),"ADC updates cannot replace the H1 graph");
        require(!raw->property("xAxisTimeMode").toBool()&&panel.testRawXAxisLabel()=="2",
            "FPGA x axis uses sample position rather than invented wall-clock time");
        auto *channel=panel.findChild<QComboBox*>("fpgaMonitorChannelCombo");
        require(!panel.findChild<QComboBox*>("fpgaMonitorComponentCombo")&&panel.testHarmonicSamples()==QVector<float>({4,2.5,2}),
            "FPGA displays the validated normalized samples rather than raw H2 amplitudes");
        panel.setFpgaSecondHarmonicPeak(0x30,1,100,4,false);
        panel.setFpgaSecondHarmonicPeak(0x30,1,100,4,false);
        panel.setEnglish(true);panel.setEnglish(false);
        require(panel.testPeakValues()==QVector<float>({4}),"duplicate cycles and language refresh do not add peaks");
        channel->setCurrentIndex(1);
        require(raw->property("sampleCount").toInt()==0&&h2->property("sampleCount").toInt()==0&&dlia->property("sampleCount").toInt()==0&&peaks->property("sampleCount").toInt()==0,
            "channel switch shows only that channel's waveforms and peak history");
        panel.setFpgaWaveform(0x31,{{1,2},{3,4},{5,6},{7,8},{std::hypot(5,7)/std::hypot(1,3),std::hypot(6,8)/std::hypot(2,4)}},
            {"I1","Q1","I2","Q2","H2/H1"},"IQ complete",2);
        require(panel.testHarmonicSamples().size()==2&&std::abs(panel.testHarmonicSamples().first()-std::hypot(5,7)/std::hypot(1,3))<0.0001,
            "IQ-only format displays the normalized harmonic magnitudes");
        require(panel.testFirstHarmonicSamples().size()==2&&panel.testSecondHarmonicSamples().size()==2
            &&std::abs(panel.testFirstHarmonicSamples().first()-std::hypot(1,3))<0.0001
            &&std::abs(panel.testSecondHarmonicSamples().first()-std::hypot(5,7))<0.0001,
            "IQ-only H1 and H2 graphs display magnitudes of their respective quadrature pairs");
        panel.setFpgaSecondHarmonicPeak(0x31,2,200,10,true);
        require(panel.findChild<QLabel*>("fpgaMonitorPeakInfo")->text()=="预览峰值 · 最新：10","bounded previews show the latest peak and retain the preview label");
        panel.setFpgaWaveform(0x31,{{},{},{},{},{}},{"I1","Q1","I2","Q2","H2/H1"},"incomplete",2);
        require(raw->property("sampleCount").toInt()==0&&h2->property("sampleCount").toInt()==0
            &&dlia->property("sampleCount").toInt()==0,"invalid FPGA cycle clears all three harmonic waveforms");
        panel.setFpgaSecondHarmonicPeak(0x31,3,300,std::numeric_limits<double>::quiet_NaN(),false);
        require(panel.testPeakValues().size()==2&&std::isnan(panel.testPeakValues().last()),"invalid cycles leave a gap in the trend");
        require(panel.findChild<QLabel*>("fpgaMonitorPeakInfo")->text().endsWith(QStringLiteral("最新：—")), "invalid latest peaks never retain the previous numeric result");
        channel->setCurrentIndex(0);
        require(panel.testPeakValues()==QVector<float>({4}),"switching back restores the independent channel trend");
        QMetaObject::invokeMethod(&panel,"onClearPeakPlotClicked");
        panel.setEnglish(true);panel.setEnglish(false);
        panel.setFpgaSecondHarmonicPeak(0x30,1,100,4,false);
        require(panel.testPeakValues().isEmpty(),"cleared peaks are not replayed by display refresh or a duplicate cycle");
        channel->setCurrentIndex(1);
        require(panel.testPeakValues().size()==2,"clearing one channel preserves the other channel's history");
        panel.setRemoteSkyMode(true);
        require(panel.testHarmonicSamples().isEmpty()&&panel.testPeakValues().isEmpty(),"changing data hosts discards the previous FPGA waveforms and trends");
        panel.setRemoteSkyMode(false);
        panel.injectRemoteRawSignalFrame(1,{99,99});panel.testFlushLiveDisplay();
        require(raw->property("sampleCount").toInt()==0,"TCP data cannot overwrite FPGA data");
        panel.toggleConnection();require(!panel.isConnecting(),"FPGA monitor cannot open a TCP socket");
        panel.setUiTestMode(true);
        panel.setFpgaWaveform(0x21,{{1,2,3}},{"ADC"},"Simulated RAW",3);
        panel.setFpgaSecondHarmonicPeak(0x31,10,1000,23,false);
        panel.setUiTestMode(false);
        require(raw->property("sampleCount").toInt()==0&&peaks->property("sampleCount").toInt()==0,
            "leaving UI test mode discards simulated FPGA waveforms and peaks");
        panel.setFpgaMode(false);
        require(!h2->isVisible()&&panel.testSecondHarmonicSamples().isEmpty(),"direct mode hides and clears the FPGA H2 graph");
        require(!rawButton->isVisible()&&!harmonicButton->isVisible(),"direct mode hides FPGA acquisition controls");
        require(panel.host()=="127.0.0.9"&&panel.port()==9901&&raw->property("xAxisTimeMode").toBool(),
            "Direct restores the TCP endpoint and clock axis");
        panel.injectRemoteSecondHarmonicFrame(1000,{40,50,60});panel.testFlushLiveDisplay();
        require(panel.testHarmonicSamples()==QVector<float>({40,50,60}),"Direct TCP harmonic data is not normalized again");
    }
    std::cout << "tcp_wave_panel_test passed\n";
    return 0;
}
