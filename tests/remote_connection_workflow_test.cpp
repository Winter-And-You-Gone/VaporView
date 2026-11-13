#include "test_settings_sandbox.h"
#include "ground/main/MainWindow.h"
#include "ground/main/GroundMainWindowState.h"
#include "ground/devices/RemoteSkyController.h"
#include "ground/calibration/CalibrationService.h"
#include "TcpTelemetryLink.h"
#include "FpgaTelemetry.h"
#include "FpgaExtensions.h"
#include "BaudRateComboSupport.h"
#include <QApplication>
#include <QAction>
#include <QFrame>
#include <QLabel>
#include <QPointer>
#include <QToolButton>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QLineEdit>
#include <QJsonDocument>
#include <QProcess>
#include <QTcpServer>
#include <QThread>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <stdexcept>

struct MainWindowRtcmTestAccess {
    static void refresh(MainWindow& window) { window.updateRemoteTelemetrySummaryLabel(); }
    static auto& state(MainWindow& window) { return *window.state_; }
    static bool valid(MainWindow& window, VaporView::SkyDeviceId id) { return window.remoteDeviceDataValid(id, 3000); }
    static QString invalid(MainWindow& window, VaporView::SkyDeviceId id) { return window.remoteDeviceInvalidText(id, 3000); }
};
static void require(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
template<class Predicate> static bool waitUntil(Predicate predicate, int timeout = 5000) {
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (predicate()) return true;
        QThread::msleep(1);
    }
    return predicate();
}
static void workflow(bool fpga)
{
    std::cout << "Workflow fpga=" << fpga << std::endl;
    using namespace VaporView;
    QTemporaryDir logsDirectory;
    LogService logs("RemoteWorkflow", nullptr, logsDirectory.path(), logsDirectory.path());
    QVector<LogRecord> results;
    TcpTelemetryLink server; require(server.listen("127.0.0.1", 0), "fake Sky listens");
    MainWindow window(nullptr, std::make_shared<VaporView::Ground::Calibration::CalibrationService>(logsDirectory.path()));
    QMetaObject::invokeMethod(&window, "onDataSourceModeChanged", Q_ARG(int, 1));
    auto &state = MainWindowRtcmTestAccess::state(window);
    state.remote_sky_link_config_.transport = TelemetryTransportType::Tcp;
    state.remote_sky_link_config_.tcpHost = "127.0.0.1";
    state.remote_sky_link_config_.tcpPort = server.localPort();
    state.fpga_home_disabled_sources_[1] = {0x42, 0x45, 0x46};
    SkyConfig config = SkyConfig::defaults();
    config.sensor_transport_path = fpga ? SensorTransportPath::FpgaRelay : SensorTransportPath::DirectDevices;
    QJsonObject registers;
    for (quint32 control : {0x6004u, 0x6304u, 0x6404u, 0x6804u, 0x4004u, 0x4104u, 0x5004u, 0x5104u})
        registers[QString::number(control)] = 0;
    QJsonObject status{{"version", 1}, {"connected", false}, {"ready", false}, {"busy", false},
        {"detail", "test board"}, {"archived_records", "0"},
        {"extensions", int(FpgaExtensions::Rd105)}, {"configuration", config.fpga.configuration.toJson()}};
    TelemetryCodec encoder, decoder; quint16 sequence = 1;
    int bulk = 0, operations = 0, failures = 0, disconnects = 0;
    const auto send = [&](MsgType type, const QByteArray &payload) {
        require(server.writeBytes(encoder.encodeFrame(type, payload, sequence++, 1)) > 0, "Sky frame delivered");
    };
    const auto publishStatus = [&] {
        status["registers"] = registers;
        QJsonObject parsed;
        require(FpgaRemote::parseStatus(FpgaRemote::encodeStatus(status), parsed), "valid fake board status");
        send(MsgType::FpgaStatus, FpgaRemote::encodeStatus(status));
        TelemetryStatus telemetry;
        send(MsgType::TelemetryStatus, TelemetryCodec::serializeTelemetryStatus(telemetry));
    };
    QObject::connect(&logs, &LogService::recordPublished, &window, [&](const LogRecord &record) {
        if (record.fields.value("event") == "ground_device_operation_response") results.append(record);
    });
    QObject::connect(&server, &TelemetryLink::bytesReceived, &window, [&](const QByteArray &bytes) {
        for (const auto &frame : decoder.feedBytes(bytes)) {
            CommandMessage command;
            if (frame.type != MsgType::Command || !TelemetryCodec::parseCommand(frame.payload, command)) continue;
            CommandAck ack; ack.command_id = command.command_id; ack.command_seq = command.command_seq;
            send(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(ack));
            if (command.command_id == CommandId::RequestStatus) publishStatus();
            if (command.command_id == CommandId::GetSkyConfig)
                send(MsgType::SkyConfig, QJsonDocument(config.toJson()).toJson(QJsonDocument::Compact));
            if (command.command_id == CommandId::ConnectAllDevices) ++bulk;
            if (command.command_id == CommandId::DisconnectAllDevices) ++disconnects;
            if (command.command_id != CommandId::DeviceOperation) continue;
            DeviceOperationRequest request; QJsonObject control;
            require(TelemetryCodec::parseDeviceOperationRequest(command.payload, request) &&
                FpgaRemote::parseControl(request.payload, control), "FPGA request parses");
            ++operations;
            const auto op = control.value("op").toString();
            DeviceOperationResponse response;
            response.request_id = request.request_id; response.device_id = request.device_id; response.operation = request.operation;
            if (op == "connect") { status["connected"] = true; status["ready"] = true; }
            else if (op == "disconnect") { ++disconnects; status["connected"] = false; status["ready"] = false; }
            else if (op == "sensor") {
                const auto source = control.value("source").toInt();
                require(source != 0x42 && source != 0x45 && source != 0x46, "unchecked devices are excluded");
                if (source == 0x40) {
                    ++failures; response.error_code = CommandErrorCode::ConfigApplyFailed;
                    response.error_message = "test sensor port busy";
                } else registers[QString::number(0x6004 + (source - 0x40) * 0x100)] = 1;
            } else if (op == "acquisition") {
                for (quint32 address : {0x4004u, 0x4104u, 0x5004u, 0x5104u}) registers[QString::number(address)] = 1;
            } else require(false, "bulk connect never starts recording, laser or DAC");
            status["registers"] = registers;
            response.payload = FpgaRemote::encodeStatus(status);
            send(MsgType::DeviceOperationResponse, TelemetryCodec::serializeDeviceOperationResponse(response));
        }
    });
    auto *link = window.findChild<QAction*>("telemetryLinkAction");
    require(link && link->isVisible() && link->isEnabled() && link->text().contains("打开"), "remote link action is available before connection");
    const auto openIcon = link->icon().cacheKey();
    require(!state.connect_btn_->isEnabled(), "device connect waits for an open telemetry link");
    link->trigger();
    require(waitUntil([&] { return state.remote_config_session_.loaded && state.remote_sky_online_; }), "link action completes handshake and configuration read");
    require(bulk == 0 && operations == 0, "opening the telemetry link never starts devices");
    require(link->text().contains("断开") && link->icon().cacheKey() != openIcon, "open link changes title and icon");
    require(state.connect_btn_->isEnabled() && state.disconnect_btn_->isEnabled(), "remote device actions use remote link state");
    window.resize(1600, 1000); window.show();
    QElapsedTimer initialLayout; initialLayout.start();
    waitUntil([&] { return initialLayout.elapsed() > 200; });
    QToolButton *linkButton = nullptr, *refreshButton = nullptr;
    for (auto *button : window.findChildren<QToolButton*>("titleBarButton")) {
        if (button->accessibleName() == link->text()) linkButton = button;
        if (button->toolTip() == state.refresh_ports_btn_->toolTip()) refreshButton = button;
    }
    require(linkButton && refreshButton && linkButton->isVisible()
        && linkButton->geometry().right() < refreshButton->geometry().left(), "link toggle precedes Refresh in the title bar");
    MainWindowRtcmTestAccess::refresh(window);
    QList<QPointer<QFrame>> pills;
    for (auto *pill : window.findChildren<QFrame*>("homeTelemetrySummaryPill")) pills.append(pill);
    require(!pills.isEmpty(), "summary fields exist before device connection");
    state.connect_btn_->trigger();
    const bool completed = waitUntil([&] {
        for (const auto& pill : pills) require(pill, "connecting devices never rebuilds or clears summary fields");
        for (auto *label : state.data_telemetry_summary_card_->findChildren<QLabel*>("homeTelemetrySummaryNameLabel"))
            require(label->isVisible(), "summary labels remain visible during connection");
        return fpga ? operations >= 6 && state.fpga_home_connect_queue_.isEmpty()
        && !state.fpga_session_.requestId : bulk == 1; });
    if (!completed) std::cerr << "bulk=" << bulk << " operations=" << operations << " pending=" << state.remote_connect_devices_pending_
        << " online=" << state.remote_sky_online_ << " loaded=" << state.remote_config_session_.loaded
        << " generation=" << state.remote_config_session_.loadedGeneration << '/' << window.testRemoteSkyController()->linkGeneration()
        << " reading=" << state.remote_config_session_.reading << " queue=" << state.fpga_home_connect_queue_.size() << std::endl;
    require(completed, "one title click completes configured connection workflow");
    if (fpga) {
        require(failures == 1 && bulk == 0, "partial FPGA failure continues remaining devices without legacy bulk command");
        require(waitUntil([&] { return std::any_of(results.begin(), results.end(), [](const LogRecord &r) {
            return r.message.contains("失败") && r.message.contains("sensor") && r.message.contains("10") && r.message.contains("port busy");
        }); }), "visible result identifies operation, error code and remote reason");
        publishStatus();
        for (quint16 source : {0x43, 0x44, 0x48}) {
            FpgaSensor::Reading reading; reading.source = source; reading.validity = {true, true, true, true};
            reading.temperatureC = 24.; reading.pressurePa = 101300.; reading.humidityPct = 40.;
            send(MsgType::FpgaSensor, FpgaRemote::encodeSensor(reading, 1));
        }
        require(waitUntil([&] { return MainWindowRtcmTestAccess::valid(window, SkyDeviceId::Bmp390)
            && MainWindowRtcmTestAccess::valid(window, SkyDeviceId::Sht45)
            && MainWindowRtcmTestAccess::valid(window, SkyDeviceId::TemperatureController); }), "per sensor FPGA freshness agrees with details");
        require(MainWindowRtcmTestAccess::invalid(window, SkyDeviceId::Hmp).contains("FPGA"), "unsupported HMP is explicit");
        FpgaSensor::Reading invalid; invalid.source = 0x43;
        send(MsgType::FpgaSensor, FpgaRemote::encodeSensor(invalid, 2));
        require(waitUntil([&] { return !MainWindowRtcmTestAccess::valid(window, SkyDeviceId::Bmp390); }), "invalid reading clears summary freshness");
    }
    const int before = operations + bulk;
    for (int i = 0; i < 5; ++i) publishStatus();
    QElapsedTimer settle; settle.start();
    waitUntil([&] { return settle.elapsed() > 250; });
    require(operations + bulk == before, "repeated status does not duplicate startup");
    for (int i = 0; i < 12; ++i) {
        send(MsgType::TelemetryBasic, TelemetryCodec::serializeBasicTelemetry(TelemetryBasic{}));
        publishStatus();
        QCoreApplication::processEvents();
        MainWindowRtcmTestAccess::refresh(window);
        for (const auto& pill : pills) require(pill, "changing rates and data availability preserve existing summary widgets");
    }
    for (auto *label : state.data_telemetry_summary_card_->findChildren<QLabel*>("homeTelemetrySummaryNameLabel"))
        require(label->isVisible(), "frequency, link rate and device fields remain visible while connected");
    link->trigger();
    require(waitUntil([&] { return !state.remote_sky_online_; }), "link toggle clears handshake state");
    require(disconnects == 0 && link->text().contains("打开"), "closing link preserves remote device acquisition");
    link->trigger();
    require(waitUntil([&] { return state.remote_config_session_.loadedGeneration == window.testRemoteSkyController()->linkGeneration()
        && state.remote_sky_online_; }), "reopening reads configuration for the new link");
    state.connect_btn_->trigger();
    require(waitUntil([&] { return fpga ? operations == before + 1 && !state.fpga_session_.requestId
        && state.fpga_home_connect_queue_.isEmpty() : bulk == 2; }),
        "explicit reconnect uses fresh config and preserves already running FPGA modules");
    require(state.disconnect_btn_->isEnabled(), "title disconnect remains usable with remote devices connected");
    state.disconnect_btn_->trigger();
    require(waitUntil([&] { return disconnects == 1 && (!fpga || !state.fpga_session_.requestId); }), "title disconnect sends a device disconnect operation");
    require(window.testRemoteSkyController()->isOpen(), "device disconnect keeps the telemetry link open");
    link->trigger(); window.close(); server.close();
}
static void simulatedSkyWorkflow()
{
    using namespace VaporView;
    QTemporaryDir directory;
    require(directory.isValid(), "temporary Sky configuration");
    LogService logs("RemoteSimulatorWorkflow", nullptr, directory.filePath("logs"), directory.filePath("logs"));
    QTcpServer reservation;
    require(reservation.listen(QHostAddress::LocalHost, 0), "reserve simulator port");
    const auto port = reservation.serverPort(); reservation.close();
    struct Child {
        QProcess process;
        ~Child() {
            if (process.state() == QProcess::NotRunning) return;
            process.write("quit\n"); process.closeWriteChannel();
            if (!process.waitForFinished(5000)) { process.kill(); process.waitForFinished(3000); }
        }
    } child;
    const auto executable = QCoreApplication::applicationDirPath() + "/VaporViewSkyCore"
#ifdef Q_OS_WIN
        ".exe"
#endif
        ;
    child.process.setProcessChannelMode(QProcess::MergedChannels);
    child.process.start(executable, {"--fpga-simulate-data", "--sky-config", directory.filePath("sky.json"),
        "--telemetry-transport", "tcp", "--telemetry-host", "127.0.0.1",
        "--telemetry-tcp-port", QString::number(port), "--ipc-port", "0"});
    require(child.process.waitForStarted(5000), "real SkyCore simulator starts");
    require(waitUntil([&] { return child.process.bytesAvailable() > 0; }), "SkyCore creates startup output");
    MainWindow window(nullptr, std::make_shared<VaporView::Ground::Calibration::CalibrationService>(directory.path()));
    QMetaObject::invokeMethod(&window, "onDataSourceModeChanged", Q_ARG(int, 1));
    auto &state = MainWindowRtcmTestAccess::state(window);
    state.remote_sky_link_config_.transport = TelemetryTransportType::Tcp;
    state.remote_sky_link_config_.tcpHost = "127.0.0.1";
    state.remote_sky_link_config_.tcpPort = port;
    state.fpga_home_disabled_sources_[1].clear();
    window.show();
    QMetaObject::invokeMethod(&window, "onTelemetryLinkClicked");
    require(waitUntil([&] { return state.remote_config_session_.loaded && state.remote_sky_online_; }), "simulator telemetry handshake");
    QElapsedTimer initialLayout; initialLayout.start();
    waitUntil([&] { return initialLayout.elapsed() >= 500; });
    QElapsedTimer connectClock; connectClock.start();
    QVector<qint64> connectDelays; qint64 lastConnectPulse = 0; QTimer connectPulse;
    connectPulse.setTimerType(Qt::PreciseTimer); connectPulse.setInterval(10);
    QObject::connect(&connectPulse, &QTimer::timeout, &window, [&] {
        const auto now = connectClock.elapsed(); connectDelays.append(now - lastConnectPulse); lastConnectPulse = now;

    }); connectPulse.start();
    QMetaObject::invokeMethod(&window, "onConnectClicked");
    const auto ready = [&] {
        for (const auto device : {SkyDeviceId::Ptb, SkyDeviceId::Epsilon, SkyDeviceId::Hmp, SkyDeviceId::Lidar,
                 SkyDeviceId::Ai8TemperatureController, SkyDeviceId::Bmp390, SkyDeviceId::Sht45,
                 SkyDeviceId::TemperatureController, SkyDeviceId::WaveTcp})
            if (!MainWindowRtcmTestAccess::valid(window, device)) return false;
        return state.fpga_home_connect_queue_.isEmpty() && !state.fpga_session_.requestId
            && window.testRemoteSkyController()->waveformPacketRate(4) > 0;
    };
    const bool connected = waitUntil(ready, 20000);
    if (!connected) {
        for (auto device : {SkyDeviceId::Ptb, SkyDeviceId::Epsilon, SkyDeviceId::Hmp, SkyDeviceId::Lidar,
            SkyDeviceId::Ai8TemperatureController, SkyDeviceId::Bmp390, SkyDeviceId::Sht45, SkyDeviceId::TemperatureController, SkyDeviceId::WaveTcp})
            std::cerr << "DEVICE " << int(device) << " valid=" << MainWindowRtcmTestAccess::valid(window,device)
                << " reason=" << MainWindowRtcmTestAccess::invalid(window,device).toStdString() << std::endl;
        std::cerr << "queue=" << state.fpga_home_connect_queue_.size() << " request=" << state.fpga_session_.requestId
            << " wave=" << window.testRemoteSkyController()->waveformPacketRate(4) << std::endl;
        std::cerr << child.process.readAll().constData() << std::endl;
    }
    require(connected, "one title click connects real SkyCore simulated sensors, summaries and waveform rate");
    require(state.fpga_session_.remoteStatus.value("simulation").toBool(), "Sky identifies simulation");
    connectPulse.stop();
    require(!connectDelays.isEmpty(), "connection heartbeat samples");
    std::sort(connectDelays.begin(), connectDelays.end());
    std::cout << "Connecting: p95Ms=" << connectDelays[(connectDelays.size()-1)*95/100]
        << " maxMs=" << connectDelays.last() << std::endl;
    require(connectDelays[(connectDelays.size()-1)*95/100] < 100 && connectDelays.last() < 250,
        "GUI stays responsive throughout connect-all animations");
    require(waitUntil([&] { return state.current_hmp_.valid && state.current_hmp_.temperature > 22.0
        && state.current_hmp_.temperature < 24.0 && state.current_hmp_.humidity > 44.0
        && state.current_hmp_.humidity < 46.0; }), "simulator environmental values reach ground UI");
    const auto initialTemperature = state.current_hmp_.temperature, initialHumidity = state.current_hmp_.humidity;
    require(waitUntil([&] { return std::abs(state.current_hmp_.temperature - initialTemperature) > 0.001
        && std::abs(state.current_hmp_.humidity - initialHumidity) > 0.001; }), "environmental values fluctuate");
    require(waitUntil([&] { const auto cycles = state.fpga_session_.remoteStatus.value("acquisition_cycles").toObject();
        return cycles.value("count").toString().toULongLong() > 0 && cycles.value("window_ms").toDouble() > 0
            && cycles.value("rate_hz").toDouble() > 0; }), "Sky reports actual acquisition cycle count and window");
    const auto registers = state.fpga_session_.remoteStatus.value("registers").toObject();
    // WMS supplies acquisition cycle labels; physical DAC outputs remain independent.
    for (const quint32 address : {0x3004u, 0x3104u})
        require(registers.value(QString::number(address)).toInt() == 0, "title connection leaves DAC off");
    require(state.remote_status_.recording_state == 0, "title connection leaves recording off");
    // Observe GUI timer delivery while the real telemetry and display updates continue.
    QElapsedTimer settle; settle.start(); waitUntil([&] { return settle.elapsed() >= 500; });
    QElapsedTimer responsiveness; responsiveness.start();
    QVector<qint64> delays; qint64 previous = 0; QTimer pulse;
    pulse.setTimerType(Qt::PreciseTimer); pulse.setInterval(10);
    QObject::connect(&pulse, &QTimer::timeout, &window, [&] {
        const auto now = responsiveness.elapsed(); delays.append(now - previous); previous = now;
    }); pulse.start();
    waitUntil([&] { return responsiveness.elapsed() >= 2500; }, 4000); pulse.stop();
    require(!delays.isEmpty(), "live remote GUI heartbeat samples");
    std::sort(delays.begin(), delays.end());
    const auto p95 = delays[(delays.size() - 1) * 95 / 100];
    std::cout << "Remote simulator: heartbeatSamples=" << delays.size() << " p95Ms=" << p95
        << " maxMs=" << delays.last() << std::endl;
    require(p95 < 100 && delays.last() < 250, "display event loop stays responsive during live simulated telemetry");
    state.disconnect_btn_->trigger();
    require(state.home_device_disconnecting_.contains(SkyDeviceId::Epsilon)
        && state.home_device_action_spinner_timer_->isActive(), "bulk disconnect starts capsule animation");
    require(waitUntil([&] { return !state.fpga_session_.requestId
        && !state.fpga_session_.remoteStatus.value("connected").toBool(); }), "bulk disconnect completes");
    require(waitUntil([&] { return state.home_device_disconnecting_.isEmpty(); }), "disconnect animation settles");
    window.testRemoteSkyController()->close(); window.close();
    child.process.write("quit\n"); child.process.closeWriteChannel();
    require(child.process.waitForFinished(8000) && child.process.exitStatus() == QProcess::NormalExit
        && child.process.exitCode() == 0, "SkyCore exits normally");
}
int main(int argc, char **argv)
{
    require(VaporViewTest::SettingsSandbox::initialized, "isolated application settings");
    qputenv("QT_STYLE_OVERRIDE", "Fusion");
    QApplication app(argc, argv);
    try {
    workflow(false); workflow(true);
    if (!app.arguments().contains("--title-actions-only")) simulatedSkyWorkflow();
    QComboBox baud;
    const auto capabilities = VaporView::hostSerialLinkBaudCapabilities();
    VaporView::configureSerialBaudRateCombo(&baud, capabilities, "115200");
    baud.lineEdit()->setSelection(1, 2);
    for (int i = 0; i < 100; ++i) VaporView::configureSerialBaudRateCombo(&baud, capabilities, "115200");
    require(baud.lineEdit()->selectedText() == "15", "unchanged refresh preserves editing and avoids model reset");
    std::cout << "Remote connection workflow passed\n";
    } catch (const std::exception &error) { std::cerr << "FAIL: " << error.what() << std::endl; return 1; }
}
