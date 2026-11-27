#include "test_settings_sandbox.h"
#include "ground/wave/TcpWavePanel.h"
#include "ground/wave/WaterProfileAccumulator.h"
#include <cmath>
#include "shared/theme/AppTheme.h"
#include <QMouseEvent>
#include "ground/widgets/SegmentedSwitchButton.h"
#include "LogService.h"
#include "shared/theme/SingleLevelPopupMenu.h"
#include "shared/theme/SingleLevelPopupComboBox.h"

#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QGroupBox>
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
#include <QToolButton>
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
    panel.findChild<QAction *>("tcpWaveDisplayPeakTrendMenuAction")->trigger();
    for (quint32 cycle=1;cycle<=4;++cycle)
        panel.setFpgaSecondHarmonicPeak(0x30,cycle,quint64(cycle)*100000000,cycle%2?10:30,false);
    panel.resize(1500,900);panel.show();QApplication::processEvents();
    auto* mode=panel.findChild<VaporView::Ground::Widgets::SegmentedSwitchButton*>("tcpWavePeakPlotModeSwitch");
    auto* plot=panel.findChild<QWidget*>("tcpWavePeakTrendPlot");
    require(mode&&mode->switchChecked()&&mode->leftSegmentText()==QStringLiteral("折线")&&mode->rightSegmentText()==QStringLiteral("散点"),
            "home trend capsule shows the current scatter mode");
    auto *secondMode = panel.findChild<VaporView::Ground::Widgets::SegmentedSwitchButton *>("tcpWavePeakPlotModeSwitch1");
    auto *secondPlot = panel.findChild<QWidget *>("tcpWavePeakTrendPlot1");
    require(secondMode && secondPlot && secondMode->switchChecked(), "second lane has its own scatter switch");
    for (quint32 cycle = 1; cycle <= 4; ++cycle)
        panel.setFpgaSecondHarmonicPeak(0x31, cycle, cycle, cycle % 2 ? 12 : 21, false);
    const auto secondScatter = secondPlot->grab().toImage();
    secondMode->click();
    QApplication::processEvents();
    require(!secondMode->switchChecked() && mode->switchChecked() && secondPlot->grab().toImage() != secondScatter,
            "second lane changes to a line plot independently");
    const auto values=panel.testPeakValues();
    auto *secondClear = secondPlot->parentWidget()->findChild<QPushButton *>("compactTcpButton");
    require(secondClear, "second lane has a clear button");
    secondClear->click();
    require(panel.testPeakValues(1).isEmpty() && panel.testPeakValues() == values, "clearing the second trend preserves the first");
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
    panel.findChild<QAction *>("tcpWaveDisplayHarmonicMenuAction")->trigger();
    panel.setFpgaWaveform(0x30,{{1,1,1,1,1}},{"H2/max(H1)"},"normalized",5);
    QApplication::processEvents();
    auto *plot=panel.findChild<QWidget*>("tcpWaveHarmonicPlot");
    const auto connected=plot->grab().toImage();
    const double invalid=std::numeric_limits<double>::quiet_NaN();
    panel.setFpgaWaveform(0x30,{{1,1,invalid,1,1}},{"H2/max(H1)"},"normalized",5);
    const auto gap=plot->grab().toImage();
    const QRect center(connected.width()/2-3,0,6,connected.height());
    require(connected.copy(center)!=gap.copy(center),"invalid normalized samples visibly break the waveform instead of joining across the gap");
    panel.setFpgaWaveform(0x30,{{invalid,invalid,invalid,invalid,invalid}},{"H2/max(H1)"},"normalized",5);
    require(plot->grab().toImage()!=gap,"all-invalid normalized waveforms render the empty state rather than a numeric trace");
    panel.setFpgaWaveform(0x30,{{invalid,1,1,1,1}},{"H2/max(H1)"},"normalized",5);
    const QRect right(connected.width()*3/4,0,6,connected.height());
    require(plot->grab().toImage().copy(right)==connected.copy(right),"an invalid first reference does not suppress the remaining finite waveform");
}

void testFpgaPeakHeaderStaysStable()
{
    TcpWavePanel panel; panel.setFpgaMode(true); panel.resize(2200,900); panel.show();
    panel.findChild<QAction *>("tcpWaveDisplayShowAllMenuAction")->trigger();
    auto *info=panel.findChild<QLabel*>("fpgaMonitorPeakInfo");
    auto *valueLabel=panel.findChild<QLabel*>("fpgaMonitorPeakValue");
    auto *mode=panel.findChild<QWidget*>("tcpWavePeakPlotModeSwitch");
    require(info&&valueLabel&&mode,"FPGA peak header controls exist");
    auto *titleBar=mode->parentWidget();
    auto *statusBar=info->parentWidget();
    require(titleBar&&titleBar->objectName()==QStringLiteral("sectionTitleBar")&&statusBar&&statusBar!=titleBar,
        "mode and clear controls stay in the peak trend title bar while peak readout lives below it");
    auto *titleLabel=titleBar->findChild<QLabel*>("sectionTitleLabel");
    QPushButton *clear=nullptr;
    for(auto *button:titleBar->findChildren<QPushButton*>("compactTcpButton"))
        if(!button->isHidden())clear=button;
    require(titleLabel&&clear,"FPGA peak title and clear button live in the title bar");
    quint32 cycle=0;
    const auto baseFont=panel.font();
    for(double scale:{1.0,1.3}) for(bool english:{false,true}) {
        auto font=baseFont; font.setPointSizeF(baseFont.pointSizeF()*scale); panel.setFont(font);
        panel.setEnglish(english); QApplication::processEvents();
        ++cycle; panel.setFpgaSecondHarmonicPeak(0x30,cycle,cycle,1.0,false); QApplication::processEvents();
        const auto titleRect=titleLabel->geometry(),modeRect=mode->geometry(),clearRect=clear->geometry();
        const auto infoRect=info->geometry(),valueRect=valueLabel->geometry();
        const auto captionImage=info->grab().toImage();
        require(modeRect.left()>titleRect.right()&&clearRect.left()>modeRect.right()
            &&clearRect.right()==titleBar->width()-titleBar->layout()->contentsMargins().right()-1,
            "mode switch and clear button are right-aligned inside the peak trend title bar");
        require(info->alignment().testFlag(Qt::AlignRight)&&infoRect.width()>0
            &&valueRect.left()>infoRect.right()
            &&valueRect.right()==statusBar->width()-statusBar->layout()->contentsMargins().right()-1,
            "latest FPGA peak occupies the right edge of the status bar below the title bar");
        require(valueLabel->minimumWidth()==valueLabel->maximumWidth()
            &&valueLabel->fontMetrics().horizontalAdvance("1111111")==valueLabel->fontMetrics().horizontalAdvance("8888888"),
            "latest peak reserves a fixed width with equal-width digits");
        for(double value:{1.0,0.2281666,-0.0001000001,-1.234567e-20,1.234567e30,
                          double(std::numeric_limits<float>::max()),-double(std::numeric_limits<float>::denorm_min()),
                          std::numeric_limits<double>::quiet_NaN()}) {
            ++cycle;panel.setFpgaSecondHarmonicPeak(0x30,cycle,cycle,value,false);QApplication::processEvents();
            require(mode->geometry()==modeRect&&clear->geometry()==clearRect&&info->geometry()==infoRect&&valueLabel->geometry()==valueRect,
                "short, fractional, scientific and invalid peaks never move the header buttons");
            require(info->grab().toImage()==captionImage,"changing peak values never move the rendered latest caption");
            require(valueLabel->fontMetrics().horizontalAdvance(valueLabel->text())<=valueLabel->contentsRect().width(),
                "latest peak text fits the remaining title space");
        }
        const auto renderPeak=[&](double value) {
            ++cycle; panel.setFpgaSecondHarmonicPeak(0x30,cycle,cycle,value,false);
            QApplication::processEvents();
            return valueLabel->grab().toImage();
        };
        const QImage fractional=renderPeak(0.2336055);
        const QColor background=fractional.pixelColor(0,0);
        int firstInk=fractional.width();
        for(int x=0;x<fractional.width()&&firstInk==fractional.width();++x)
            for(int y=0;y<fractional.height();++y)
                if(fractional.pixelColor(x,y)!=background) {firstInk=x;break;}
        require(firstInk<fractional.width(),"latest peak paints its numeric text");
        const qreal dpr=fractional.devicePixelRatio();
        const int decimalX=firstInk+qRound(valueLabel->fontMetrics().horizontalAdvance("0")*dpr);
        const QRect prefix(0,0,decimalX,fractional.height());
        for(double value:{0.2,0.23,0.2336})
            require(renderPeak(value).copy(prefix)==fractional.copy(prefix),
                "shorter fractional parts never move the leading zero or decimal anchor");
        const QImage shortInteger=renderPeak(0.25);
        const QRect suffix(decimalX,0,shortInteger.width()-decimalX,shortInteger.height());
        for(double value:{10.25,-10.25,12345.25})
            require(renderPeak(value).copy(suffix)==shortInteger.copy(suffix),
                "integer digit counts and negative signs never move the decimal or fractional digits");
    }
    panel.setFont(baseFont);
    panel.setEnglish(false);
    panel.setFpgaMode(false);
    QApplication::processEvents();
    require(mode->parentWidget()==titleBar&&clear->parentWidget()==titleBar
        &&clear->geometry().left()>mode->geometry().right()
        &&clear->geometry().right()==titleBar->width()-titleBar->layout()->contentsMargins().right()-1,
        "direct mode also keeps mode switch and clear button right-aligned in the peak trend title bar");
    panel.setFpgaMode(true);
    panel.setEnglish(true);
    panel.resize(700,900);
    QApplication::processEvents();
    require(mode->geometry().top()>=titleLabel->geometry().bottom()
        &&clear->geometry().left()>mode->geometry().right()
        &&clear->geometry().right()==titleBar->width()-titleBar->layout()->contentsMargins().right()-1
        &&titleLabel->width()>=titleLabel->fontMetrics().horizontalAdvance(titleLabel->text()),
        "narrow peak trend title bar wraps controls onto a right-aligned second row without clipping the title");
    panel.resize(1500,900);
    QApplication::processEvents();
    require(mode->geometry().left()>titleLabel->geometry().right()
        &&clear->geometry().left()>mode->geometry().right()
        &&clear->geometry().right()==titleBar->width()-titleBar->layout()->contentsMargins().right()-1,
        "widening the peak trend title bar restores single-row right-aligned controls");
}

}  // namespace

void testFpgaWaveDisplaySelection()
{
    TcpWavePanel panel;
    panel.resize(1500, 900);
    panel.show();
    QApplication::processEvents();
    auto *button = panel.findChild<QToolButton *>(QStringLiteral("tcpWaveDisplayButton"));
    auto *menu = panel.findChild<VaporView::SingleLevelPopupMenu *>(QStringLiteral("waveDisplayMenu"));
    require(button && menu, "wave display menu and title-bar entry exist");
    const QStringList actions = {QStringLiteral("tcpWaveDisplayRawMenuAction"),
                                 QStringLiteral("fpgaWaveDisplayH2MenuAction"),
                                 QStringLiteral("tcpWaveDisplayHarmonicMenuAction"),
                                 QStringLiteral("tcpWaveDisplayPeakTrendMenuAction")};
    const QList<QWidget *> plots = {panel.findChild<QWidget *>("tcpWaveRawPlot"),
                                    panel.findChild<QWidget *>("fpgaWaveH2Plot"),
                                    panel.findChild<QWidget *>("tcpWaveHarmonicPlot"),
                                    panel.findChild<QWidget *>("tcpWavePeakTrendPlot"),
                                    panel.findChild<QWidget *>("waterConcentrationPlot"),
                                    panel.findChild<QWidget *>("waterProfilePlot"),
                                    panel.findChild<QWidget *>("waterConcentrationPlot1"),
                                    panel.findChild<QWidget *>("waterProfilePlot1")};
    const auto clickRow = [&](const QString &name) {
        button->click();
        require(waitUntil([&] { return menu->isVisible(); }), "wave display menu opens in either transport mode");
        auto *row = menu->findChild<VaporView::SingleLevelPopupMenuRow *>(name);
        require(row && row->isVisible(), "requested display row is available in the active mode");
        row->click();
        menu->hide();
        QApplication::processEvents();
    };
    require(!panel.findChild<QComboBox *>("fpgaMonitorChannelCombo"), "FPGA channels have no selector");
    const auto checkPlots = [&](int mask) {
        for (int index = 0; index < 4; ++index) {
            auto *right = panel.findChild<QWidget *>(plots[index]->objectName() + QStringLiteral("1"));
            const bool shown = panel.isFpgaMode() && (mask & (1 << index));
            require(right && right->isVisible() == shown, "second FPGA channel follows every display selection");
            if (shown) {
                const auto a = plots[index]->parentWidget()->geometry(), b = right->parentWidget()->geometry();
                require(a.top() == b.top() && a.right() < b.left() && std::abs(a.width() - b.width()) <= 1,
                        "each selected harmonic or peak occupies matching cells on the same row");
            }
        }
        for (int index = 0; index < plots.size(); ++index)
            require(plots[index] && plots[index]->isVisible() == bool(mask & (1 << index)),
                    "selected waveform subset matches actual plot visibility");
        require(panel.hasVisibleWaveDisplay() == (mask != 0), "empty selection reports no visible plots");
        require(panel.usesExpandedPanelHeight() == (panel.isFpgaMode() ? mask != 0 : bool(mask && (mask & (mask - 1)))),
                "expanded height follows the number of selected graphs");
    };
    clickRow(actions[0]); // Remember a different selection for direct mode.
    panel.setFpgaMode(true);
    QApplication::processEvents();
    require(button->isVisible(), "FPGA keeps the wave display settings button visible");
    checkPlots(240);
    const auto equalRow = [&](int left, int right) {
        const auto *a = plots[left]->parentWidget();
        const auto *b = plots[right]->parentWidget();
        return a->geometry().top() == b->geometry().top() && a->geometry().right() < b->geometry().left() &&
               std::abs(a->width() - b->width()) <= 1;
    };
    require(equalRow(4, 6) && equalRow(5, 7), "default water plots align in two equal-width rows");
    const QString defaultView = QStringLiteral("fpgaWaveDisplayDefaultMenuAction");
    const QString showAll = QStringLiteral("tcpWaveDisplayShowAllMenuAction");
    clickRow(showAll);
    checkPlots(255);
    require(equalRow(4, 6) && equalRow(5, 7), "both water channels stay aligned in the all-graphs view");
    const int allHeight = panel.preferredPanelHeight();
    int acquisitionRequests = 0;
    QObject::connect(&panel, &TcpWavePanel::fpgaAcquisitionRequested, &panel,
                     [&](bool, bool) { ++acquisitionRequests; });
    for (int mask = 0; mask < 16; ++mask) {
        if (mask > 0) clickRow(showAll); // Restore all, then deselect all to reset the subset.
        clickRow(showAll);
        for (int index = 0; index < actions.size(); ++index)
            if (mask & (1 << index)) clickRow(actions[index]);
        for (int index = 0; index < actions.size(); ++index) {
            auto *row = menu->findChild<VaporView::SingleLevelPopupMenuRow *>(actions[index]);
            const bool checked = mask & (1 << index);
            require(row && row->isChecked() == checked && row->property("hasCheckIcon").toBool() == checked &&
                    row->checkLabel()->pixmap().isNull() == !checked,
                    "only selected curves display a check icon");
        }
        checkPlots(mask);
        if (!mask) require(panel.preferredPanelHeight() < 80, "hiding all FPGA graphs collapses to the title bar");
        if (mask == 2) {
            const int singleHeight = panel.preferredPanelHeight();
            require(singleHeight < allHeight, "H2 alone releases the unused peak-trend height");
            panel.setCompactLayout(true);
            require(panel.preferredPanelHeight() == singleHeight, "hidden FPGA plots reserve no compact-layout height");
            panel.setCompactLayout(false);
        }
    }
    clickRow(showAll);
    checkPlots(255);
    clickRow(showAll);
    checkPlots(0);
    panel.setFpgaWaveform(0x30, {{10, 20}, {40, 50}, {4, 2.5}}, {"H1", "H2", "H2/max(H1)"}, "complete", 2);
    panel.setFpgaSecondHarmonicPeak(0x30, 1, 100, 4, false);
    require(panel.testPeakValues() == QVector<float>({4}), "hidden trend continues collecting cycle peaks");
    clickRow(actions[2]);
    clickRow(actions[3]);
    checkPlots(12);
    require(panel.testHarmonicSamples() == QVector<float>({4, 2.5}), "reopening plots retains the latest normalized samples");
    panel.setEnglish(true);
    auto *normalizedRow = menu->findChild<VaporView::SingleLevelPopupMenuRow *>(actions[2]);
    require(normalizedRow->text() == QStringLiteral("Show Normalized Second Harmonic"), "FPGA display labels follow the language");
    checkPlots(12);
    panel.setFpgaMode(false);
    QApplication::processEvents();
    checkPlots(1);
    require(!menu->findChild<VaporView::SingleLevelPopupMenuRow *>(actions[1])->defaultAction()->isVisible(),
            "direct mode hides the FPGA-only H2 menu item");
    panel.setFpgaMode(true);
    QApplication::processEvents();
    checkPlots(12);
    clickRow(defaultView);
    checkPlots(240);
    auto *calLabel = panel.findChild<QLabel *>("waterCalibrationLabel");
    auto *calCombo = dynamic_cast<VaporView::SingleLevelPopupComboBox *>(panel.findChild<QComboBox *>("waterCalibrationComboBox"));
    require(calLabel && calCombo, "water concentration card provides a labeled calibration dropdown");
    for (auto *button : panel.findChildren<QPushButton *>())
        require(!button->property("waterCalibrationButton").toBool(), "calibration uses the dropdown only");
    panel.setEnglish(false);
    panel.setWaterCalibrationFiles({"/tmp/a.vcal", "/tmp/b.vcal"}, "/tmp/a.vcal");
    panel.setWaterCalibration("cal-a", "a.vcal", "ppmv", false, {});
    require(calLabel->text() == QStringLiteral("定标文件") && calCombo->count() == 2 && calCombo->isEnabled() &&
            calCombo->currentText() == "a.vcal" && calCombo->currentData() == "/tmp/a.vcal", "selected calibration is displayed");
    QString selectedPath;
    const auto connection = QObject::connect(&panel, &TcpWavePanel::waterCalibrationFileSelected,
                                             [&selectedPath](const QString &path) { selectedPath = path; });
    calCombo->showPopup();
    require(calCombo->popupMenu()->actions().size() == 2, "popup lists both files");
    calCombo->popupMenu()->actions().at(1)->trigger();
    calCombo->hidePopup();
    QObject::disconnect(connection);
    require(selectedPath == "/tmp/b.vcal" && calCombo->currentText() == "b.vcal", "selection emits the full path");
    panel.setWaterCalibration("cal-b", "b.vcal", "ppmv", false, {});
    panel.setWaterCalibration("cal-b", "b.vcal", "ppmv", true, {});
    require(!calCombo->isEnabled(), "lock-only changes disable selection");
    panel.setWaterCalibration({}, {}, {}, false, {});
    require(calCombo->isEnabled() && calCombo->currentText() == QStringLiteral("未选择"), "Chinese placeholder");
    panel.setEnglish(true);
    require(calLabel->text() == "Calibration File" && calCombo->currentText() == "Not selected", "English label and placeholder");
    panel.setWaterCalibrationFiles({});
    require(!calCombo->isEnabled(), "empty file list disables selection");
    panel.setEnglish(false);
    panel.setWaterCalibration("cal-a", "sample.vcal", "ppmv", false, {});

    // Verify sub-card title bar height and top inset stay identical across compact/default and wide/maximized sizes.
    const auto collectHeaderMetrics = [&panel]() {
        QList<std::pair<int, int>> metrics;
        for (auto *box : panel.findChildren<QGroupBox *>(QStringLiteral("sensorGroupBox"))) {
            if (!box->isVisibleTo(&panel)) continue;
            auto *bar = box->findChild<QWidget *>(QStringLiteral("sectionTitleBar"), Qt::FindDirectChildrenOnly);
            if (!bar) continue;
            metrics.append(std::make_pair(bar->geometry().top(), bar->height()));
        }
        return metrics;
    };
    panel.setCompactLayout(true);
    panel.resize(1200, 560);
    QApplication::processEvents();
    const auto compactHeaders = collectHeaderMetrics();
    panel.setCompactLayout(false);
    panel.resize(1860, 460);
    QApplication::processEvents();
    const auto wideHeaders = collectHeaderMetrics();
    require(!compactHeaders.isEmpty() && compactHeaders == wideHeaders,
            "sub-card title bar height and top position stay invariant between default and maximized sizes");
    for (const auto &entry : compactHeaders) {
        require(entry.first <= 4 && entry.second == compactHeaders.first().second,
                "every sub-card title bar stays pinned to the card top with uniform height");
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    panel.appendWaterSample(0, "one", now, 12.5, 30.0);
    panel.appendWaterSample(0, "one", now, 12.5, 30.0);
    panel.appendWaterSample(0, "two", now + 1, 13.0, std::numeric_limits<double>::quiet_NaN());
    require(plots[4]->property("sampleCount").toInt() == 2 && plots[5]->property("sampleCount").toInt() == 1,
            "water charts deduplicate features and require a height for the profile");
    panel.appendWaterSample(0, "two", now + 1, 13.0, 35.0);
    require(plots[4]->property("sampleCount").toInt() == 2 && plots[5]->property("sampleCount").toInt() == 2,
            "a timely later height completes the profile point without duplicating concentration");
    panel.appendWaterSample(1, "other", now, 4.0, 60.0);
    require(plots[4]->property("sampleCount").toInt() == 2 && plots[5]->property("sampleCount").toInt() == 2 &&
            plots[6]->property("sampleCount").toInt() == 1 && plots[7]->property("sampleCount").toInt() == 1,
            "both water channels display independent samples");
    auto *profile = plots[5];
    auto *summary = panel.findChild<QLabel *>("waterProfileSummary");
    auto *profileCard = profile->parentWidget();
    auto *resolution = dynamic_cast<VaporView::SingleLevelPopupComboBox *>(profileCard->findChild<QComboBox *>("waterProfileResolution0"));
    require(resolution && resolution->currentData().toInt() == 5 && resolution->count() == 6, "profile defaults to five meters");
    auto *resolutionMenu = resolution->popupMenu();
    QPushButton *clearProfile = nullptr;
    for (auto *candidate : profileCard->findChildren<QPushButton *>())
        if (candidate->text() == QStringLiteral("清空")) clearProfile = candidate;
    require(summary && resolutionMenu && clearProfile, "profile controls available");
    QPushButton *clearOther = nullptr;
    for (auto *button : plots[7]->parentWidget()->findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("清空")) clearOther = button;
    require(clearOther, "second channel has a profile clear button");
    clearOther->click();
    panel.appendWaterSample(1, "other", now, 4.0, 60.0);
    require(plots[6]->property("sampleCount").toInt() == 1 && plots[7]->property("layerCount").toInt() == 0,
            "manual clear preserves concentration and excludes republished current reading");
    require(profile->property("layerCount").toInt() == 2, "manual clear is channel-local");
    const auto chooseResolution = [&](int meters) {
        resolution->showPopup();
        require(waitUntil([&] { return resolutionMenu->isVisible(); }), "resolution popup opens");
        for (auto *action : resolutionMenu->actions()) if (action->text() == QStringLiteral("%1 m").arg(meters)) {
            action->trigger();
            resolutionMenu->hide();
            require(resolution->currentData().toInt() == meters, "resolution selection updates the combo");
            return;
        }
        require(false, "resolution option exists");
    };
    chooseResolution(10);
    require(profile->property("layerCount").toInt() == 1 && profile->property("sampleCount").toInt() == 2,
            "coarser resolution combines historical layers");
    chooseResolution(5);
    for (int i = 0; i < 1501; ++i) panel.appendWaterSample(0, QString::number(i), now + 2 + i, 13, 50);
    require(plots[4]->property("sampleCount").toInt() == 1000 && profile->property("layerCount").toInt() == 3 &&
            profile->property("sampleCount").toInt() == 1503, "1000-frame trend cannot evict profile layers");
    panel.clearWaterSamples(); // The same path used on disconnect.
    require(plots[4]->property("sampleCount").toInt() == 0 && profile->property("sampleCount").toInt() == 1503,
            "disconnect clears only the time series");
    panel.hide(); panel.show();
    require(profile->property("layerCount").toInt() == 3, "reopening retains the profile");

    // Two vertically aligned layers separated by three empty bins. Inspect the actual
    // plotted pixels and hover the lower node, without depending on private plot geometry.
    clearProfile->click();
    panel.appendWaterSample(0, "lower", now + 2000, 10, 20);
    panel.appendWaterSample(0, "upper", now + 2001, 10, 40);
    panel.setEnglish(true);
    QApplication::processEvents();
    const auto image = profile->grab().toImage();
    const QColor blue = VaporView::appThemeColor(VaporView::AppThemeColor::PlotSeriesWaveBlue, image.pixelColor(0, 0).lightness() < 128);
    int top = image.height(), bottom = -1;
    QPoint lower;
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
        if (image.pixelColor(x, y) != blue) continue;
        top = std::min(top, y);
        if (y > bottom) { bottom = y; lower = QPoint(x, y); }
    }
    require(bottom - top > 12, "both layer nodes are rendered");
    // Match blended antialiased blue too: a thin connecting line need not contain a solid pixel.
    for (int x = lower.x() - 4; x <= lower.x() + 4; ++x) {
        auto pixel = image.pixelColor(x, (top + bottom) / 2);
        require(pixel.blue() - pixel.red() < 20, "no profile line across empty bins");
    }
    QPointF local = QPointF(lower) / image.devicePixelRatio();
    QMouseEvent move(QEvent::MouseMove, local, profile->mapToGlobal(local), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(profile, &move);
    require(profile->toolTip().contains(QStringLiteral("20–25 m")) && profile->toolTip().contains("22.5 m") &&
            profile->toolTip().contains("Samples: 1") && profile->toolTip().contains(QStringLiteral("Sample SD: —")),
            "hover explains the actual bin and singleton uncertainty");
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(profile, &leave);
    require(profile->toolTip().startsWith("X:"), "leaving a node restores the axes tooltip");
    clearProfile->click();
    resolution->setCurrentIndex(resolution->findData(0));
    require(resolution->currentText() == "Unbinned", "English unbinned label");
    panel.setEnglish(false);
    require(resolution->currentText() == QStringLiteral("不分箱格"), "Chinese unbinned label");
    panel.setEnglish(true);
    for (int i = 0; i < 3; ++i) panel.appendWaterSample(0, QStringLiteral("sub-%1").arg(i), now + 3000 + i, 10, 10.1 + 0.4 * i);
    require(profile->property("layerCount").toInt() == 3, "submeter points remain distinct");
    for (int meters : {1, 5, 50}) {
        resolution->setCurrentIndex(resolution->findData(meters));
        require(profile->property("layerCount").toInt() == 1 && profile->property("sampleCount").toInt() == 3, "binned submeter samples combine");
    }
    resolution->setCurrentIndex(resolution->findData(0));
    for (int i = 0; i < 200; ++i)
        panel.appendWaterSample(0, QStringLiteral("near-%1").arg(i), now + 3100 + i, 20.01 + i * 0.00004, 30.01 + i * 0.00004);
    require(profile->property("layerCount").toInt() == 4 && profile->property("sampleCount").toInt() == 203,
            "near-identical samples occupy one fine spatial cell");
    for (int i = 0; i < 200; ++i)
        panel.appendWaterSample(0, QStringLiteral("dense-%1").arg(i), now + 3400 + i, 15, 100 + i * 0.06);
    panel.appendWaterSample(0, "high", now + 3700, 15, 2000);
    const auto scatter = profile->grab().toImage();
    const int rendered = profile->property("renderedPointCount").toInt();
    require(profile->property("layerCount").toInt() == 205 && rendered >= 4 && rendered < 205, "scatter culls visually overlapping points");
    bool hovered = false;
    for (int y = 0; !hovered && y < scatter.height(); ++y) for (int x = 0; !hovered && x < scatter.width(); ++x) {
        if (scatter.pixelColor(x, y) != blue) continue;
        const QPointF pos = QPointF(x, y) / scatter.devicePixelRatio();
        QMouseEvent hover(QEvent::MouseMove, pos, profile->mapToGlobal(pos), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(profile, &hover);
        hovered = profile->toolTip().startsWith("Height: ") && profile->toolTip().contains("Concentration: ") &&
                  profile->toolTip().contains("Merged samples: 200");
    }
    require(hovered, "hover reports the retained scatter point and merged count");
    clearProfile->click();
    require(profile->property("layerCount").toInt() == 0 && profile->property("sampleCount").toInt() == 0, "clear resets scatter data");
    resolution->setCurrentIndex(resolution->findData(5));
    require(profile->property("layerCount").toInt() == 0, "clear resets binned data");
    panel.setWaterCalibration("cal-b", "other.vcal", "ppmv", false, {});
    require(plots[4]->property("sampleCount").toInt() == 0 && plots[5]->property("sampleCount").toInt() == 0,
            "calibration changes clear prior measurements");
    require(acquisitionRequests == 0, "display selections do not send acquisition commands");
}

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

    if (app.arguments().contains(QStringLiteral("--fpga-display-only"))) {
        testFpgaWaveDisplaySelection();
        return 0;
    }
    WaterProfileAccumulator accumulator;
    for (int i = 0; i <= WaterProfileAccumulator::maxUnbinnedPoints; ++i)
        accumulator.append(0, QString::number(i), i, i, 10.1 + (i % 3) * 0.4);
    const auto raw = accumulator.rawPoints(0);
    quint64 represented = 0;
    for (const auto &point : raw) represented += point.sampleCount;
    const int n = WaterProfileAccumulator::maxUnbinnedPoints + 1;
    require(raw.size() <= WaterProfileAccumulator::maxUnbinnedPoints && represented == quint64(n) &&
            accumulator.rawSampleCount(0) == represented, "adaptive compaction preserves every accepted sample");
    for (int meters : {1, 5, 50}) {
        const auto layers = accumulator.layers(0, meters);
        require(layers.size() == 1 && layers[0].sampleCount == represented && layers[0].meanConcentration == (n - 1) / 2.0 &&
                std::abs(layers[0].stdDev - std::sqrt(double(n) * (n + 1) / 12)) < 1e-8, "compaction preserves exact binned moments");
    }
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
    testFpgaWaveDisplaySelection();
    {
        TcpWavePanel panel;panel.setConnectionEndpoint("127.0.0.9",9901);panel.setFpgaMode(true);
        panel.setFpgaWaveform(0x20,{{1,2,3}},{"ADC"},"ADC complete",3);
        panel.setFpgaWaveform(0x30,{{10,20,30},{40,50,60},{4,2.5,2}},{"H1","H2","H2/max(H1)"},"DLIA complete",3);
        auto *raw=panel.findChild<QWidget*>("tcpWaveRawPlot");
        auto *dlia=panel.findChild<QWidget*>("tcpWaveHarmonicPlot");
        auto *h2=panel.findChild<QWidget*>("fpgaWaveH2Plot");
        auto *peaks=panel.findChild<QWidget*>("tcpWavePeakTrendPlot");
        panel.resize(1500,900);panel.show();QApplication::processEvents();
        // This waveform test exercises all four traces. The default shows the four water cards.
        panel.findChild<VaporView::SingleLevelPopupMenuRow *>("tcpWaveDisplayShowAllMenuAction")->click();
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
        require(panel.testFirstHarmonicSamples()==QVector<float>({20,30,10})
            &&panel.testSecondHarmonicSamples()==QVector<float>({50,60,40})
            &&h2->property("sampleCount").toInt()==3&&dlia->property("sampleCount").toInt()==3,
            "FPGA mode renders H1 and H2 from the same DLIA cycle instead of ADC RAW");
        panel.setFpgaWaveform(0x20,{{99,98}},{"ADC"},"ADC only",2);
        require(panel.testFirstHarmonicSamples()==QVector<float>({20,30,10}),"ADC updates cannot replace the H1 graph");
        require(!raw->property("xAxisTimeMode").toBool()&&panel.testRawXAxisLabel()=="2",
            "FPGA x axis uses sample position rather than invented wall-clock time");
        require(!panel.findChild<QComboBox*>("fpgaMonitorChannelCombo"), "both FPGA channels are displayed without a selector");
        require(!panel.findChild<QComboBox*>("fpgaMonitorComponentCombo")&&panel.testHarmonicSamples()==QVector<float>({2.5,2,4}),
            "FPGA displays the validated normalized samples rather than raw H2 amplitudes");
        panel.setFpgaSecondHarmonicPeak(0x30,1,100,4,false);
        panel.setFpgaSecondHarmonicPeak(0x30,1,100,4,false);
        panel.setEnglish(true);panel.setEnglish(false);
        require(panel.testPeakValues()==QVector<float>({4}),"duplicate cycles and language refresh do not add peaks");
        auto *otherH1 = panel.findChild<QWidget *>("tcpWaveRawPlot1");
        auto *otherH2 = panel.findChild<QWidget *>("fpgaWaveH2Plot1");
        auto *otherNorm = panel.findChild<QWidget *>("tcpWaveHarmonicPlot1");
        auto *otherTrend = panel.findChild<QWidget *>("tcpWavePeakTrendPlot1");
        require(otherH1 && otherH2 && otherNorm && otherTrend, "home has a second FPGA lane");
        require(otherH1->property("sampleCount").toInt()==0&&otherH2->property("sampleCount").toInt()==0&&otherNorm->property("sampleCount").toInt()==0&&otherTrend->property("sampleCount").toInt()==0,
            "the second lane starts empty while the first retains its data");
        panel.setFpgaWaveform(0x31,{{1,2},{3,4},{5,6},{7,8},{std::hypot(5,7)/std::hypot(1,3),std::hypot(6,8)/std::hypot(2,4)}},
            {"I1","Q1","I2","Q2","H2/max(H1)"},"IQ complete",2);
        require(panel.testHarmonicSamples(1).size()==2&&std::abs(panel.testHarmonicSamples(1).first()-std::hypot(5,7)/std::hypot(1,3))<0.0001,
            "IQ-only format displays the normalized harmonic magnitudes");
        require(panel.testFirstHarmonicSamples(1).size()==2&&panel.testSecondHarmonicSamples(1).size()==2
            &&std::abs(panel.testFirstHarmonicSamples(1).first()-std::hypot(1,3))<0.0001
            &&std::abs(panel.testSecondHarmonicSamples(1).first()-std::hypot(5,7))<0.0001,
            "IQ-only H1 and H2 graphs display magnitudes of their respective quadrature pairs");
        panel.setFpgaWaveform(0x31,{{1,2,3,4},{8,4,1,2},{2,1,0.25,0.5}},
            {"H1","H2","H2/max(H1)"},"complete",4);
        require(panel.testFirstHarmonicSamples(1)==QVector<float>({3,4,1,2})
            &&panel.testSecondHarmonicSamples(1)==QVector<float>({1,2,8,4})
            &&panel.testHarmonicSamples(1)==QVector<float>({0.25,0.5,2,1}),
            "channel 1 centers a boundary H2 peak and shifts both companion traces identically");
        panel.setFpgaSecondHarmonicPeak(0x31,2,200,10,true);
        require(panel.findChild<QLabel*>("fpgaMonitorPeakInfo1")->text()=="预览峰值 · 最新："
            &&panel.findChild<QLabel*>("fpgaMonitorPeakValue1")->text()=="10","bounded previews show the latest peak and retain the preview label");
        panel.setFpgaWaveform(0x31,{{},{},{},{},{}},{"I1","Q1","I2","Q2","H2/max(H1)"},"incomplete",2);
        require(otherH1->property("sampleCount").toInt()==0&&otherH2->property("sampleCount").toInt()==0
            &&otherNorm->property("sampleCount").toInt()==0,"invalid FPGA cycle clears all three harmonic waveforms");
        panel.setFpgaSecondHarmonicPeak(0x31,3,300,std::numeric_limits<double>::quiet_NaN(),false);
        require(panel.testPeakValues(1).size()==2&&std::isnan(panel.testPeakValues(1).last()),"invalid cycles leave a gap in the trend");
        require(panel.findChild<QLabel*>("fpgaMonitorPeakValue1")->text()==QStringLiteral("—"), "invalid latest peaks never retain the previous numeric result");
        require(panel.testPeakValues()==QVector<float>({4}),"the first lane still shows its own peak history");
        QMetaObject::invokeMethod(&panel,"onClearPeakPlotClicked");
        panel.setEnglish(true);panel.setEnglish(false);
        panel.setFpgaSecondHarmonicPeak(0x30,1,100,4,false);
        require(panel.testPeakValues().isEmpty(),"cleared peaks are not replayed by display refresh or a duplicate cycle");
        require(panel.testPeakValues(1).size()==2,"clearing one channel preserves the other channel's history");
        panel.setRemoteSkyMode(true);
        require(panel.testHarmonicSamples(1).isEmpty()&&panel.testPeakValues(1).isEmpty(),"changing data hosts discards the previous FPGA waveforms and trends");
        panel.setRemoteSkyMode(false);
        panel.injectRemoteRawSignalFrame(1,{99,99});panel.testFlushLiveDisplay();
        require(raw->property("sampleCount").toInt()==0,"TCP data cannot overwrite FPGA data");
        panel.toggleConnection();require(!panel.isConnecting(),"FPGA monitor cannot open a TCP socket");
        panel.setUiTestMode(true);
        panel.setFpgaWaveform(0x21,{{1,2,3}},{"ADC"},"Simulated RAW",3);
        panel.setFpgaSecondHarmonicPeak(0x31,10,1000,23,false);
        panel.setUiTestMode(false);
        require(otherH1->property("sampleCount").toInt()==0&&otherTrend->property("sampleCount").toInt()==0,
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
