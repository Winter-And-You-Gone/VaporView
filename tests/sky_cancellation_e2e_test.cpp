#include "SkyRuntime.h"
#include "SkyLocalIpcServer.h"
#include "LogService.h"
#include "ground/devices/RemoteSkyController.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <iostream>
#include <stdexcept>

using namespace VaporView;
using VaporView::Ground::Devices::RemoteSkyController;

namespace {
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }

template<class Predicate> bool waitFor(Predicate predicate, int timeout = 8000)
{
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (predicate()) return true;
        QThread::msleep(1);
    }
    return predicate();
}

quint16 freePort()
{
    QTcpServer server;
    check(server.listen(QHostAddress::LocalHost, 0), "reserve localhost port");
    return server.serverPort();
}

int childMain(QCoreApplication& app, const QStringList& args)
{
    check(args.size() == 5, "child arguments");
    const QString directory = args.at(4);
    SkyConfig config = SkyConfig::defaults();
    config.sensor_transport_path = SensorTransportPath::DirectDevices;
    config.epsilon.enabled = true;
    config.epsilon.port = QStringLiteral("TEST_ONLY_NO_SERIAL");
    config.epsilon_rtcm.enabled = false;
    config.ptb.enabled = false;
    config.hc2a.enabled = false;
    config.bmp390.enabled = false;
    config.sht45.enabled = false;
    config.lidar.enabled = false;
    config.temperature_controller.enabled = false;
    config.ai8_temperature_controller.enabled = false;
    config.fpga.enabled = false;
    config.wave_tcp.enabled = false;
    const QString configPath = directory + QStringLiteral("/sky.json");
    check(config.saveToFile(configPath), "save simulated config");
    LogService logs(QStringLiteral("cancel-test-sky"), nullptr, directory, directory);
    SkyRuntimeOptions options;
    options.simulate_data = true;
    options.config_path = configPath;
    options.telemetry_host = QStringLiteral("127.0.0.1");
    options.telemetry_tcp_port = args.at(2).toUShort();
    SkyRuntime runtime(options);
    auto exits = std::make_shared<int>(0);
    auto restores = std::make_shared<int>(0);
    auto saves = std::make_shared<int>(0);
    check(runtime.setEpsilonMaintenanceExchangeForTesting(
        [exits, restores, saves](const std::string& command, int, const EpsilonMaintenanceWriteTrace& written) {
            if (written) written();
            if (command.find("#fparam get ") == 0) {
                const auto key = command.substr(12, command.size() - 14);
                return key + "=1\r\n";
            }
            if (command.find("#fparam set ") == 0 && command.find(" 1\r\n") != std::string::npos)
                ++*restores;
            if (command == "#fsave\r\n") ++*saves;
            if (command == "#fdeconfig\r\n" && ++*exits == 2) {
                std::cout << "FAKE_RESTORE " << *restores << ' ' << *saves << ' ' << *exits << std::endl;
            }
            if (command == "#fmagcal2d\r\n") return std::string("Now: 10 percent\r\n");
            if (command.empty()) { QThread::msleep(50); return std::string(); }
            return std::string("*#OK\r\n");
        }), "enable test-only simulated exchange");
    SkyLocalIpcServer ipc(&runtime);
    check(ipc.listen(QStringLiteral("127.0.0.1"), args.at(3).toUShort()), "child IPC listen");
    check(runtime.start(), "child runtime starts");
    QTimer stopTimer;
    QObject::connect(&stopTimer, &QTimer::timeout, &app, [&app, directory] {
        if (QFileInfo::exists(directory + QStringLiteral("/done"))) app.quit();
    });
    stopTimer.start(50);
    QTimer::singleShot(30000, &app, &QCoreApplication::quit);
    std::cout << "READY" << std::endl;
    const int result = app.exec();
    runtime.stop();
    return result;
}

struct ChildGuard {
    QProcess process;
    ~ChildGuard() {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(5000);
        }
    }
};

int parentMain()
{
    QTemporaryDir directory;
    check(directory.isValid(), "isolated test directory");
    const quint16 telemetryPort = freePort();
    quint16 ipcPort = freePort();
    while (ipcPort == telemetryPort) ipcPort = freePort();
    ChildGuard child;
    child.process.setProcessChannelMode(QProcess::MergedChannels);
    const QString executable = directory.filePath(QFileInfo(QCoreApplication::applicationFilePath()).fileName());
    check(QFile::copy(QCoreApplication::applicationFilePath(), executable), "copy isolated test executable");
    auto environment = QProcessEnvironment::systemEnvironment();
#ifdef Q_OS_WIN
    const QChar pathSeparator = QLatin1Char(';');
#else
    const QChar pathSeparator = QLatin1Char(':');
#endif
    environment.insert(QStringLiteral("PATH"), QCoreApplication::applicationDirPath() + pathSeparator + environment.value(QStringLiteral("PATH")));
    environment.insert(QStringLiteral("LOCALAPPDATA"), directory.path());
    environment.insert(QStringLiteral("VAPORVIEW_SETTINGS_DIR"), directory.path());
    child.process.setProcessEnvironment(environment);
    child.process.start(executable,
        {QStringLiteral("--child"), QString::number(telemetryPort), QString::number(ipcPort), directory.path()});
    check(child.process.waitForStarted(5000), "test child starts");
    QByteArray childOutput;
    check(waitFor([&] { childOutput += child.process.readAll(); return childOutput.contains("READY"); }), "test child ready");
    const qint64 childPid = child.process.processId();

    QVector<LogRecord> groundLogs, skyLogs;
    LogService logs(QStringLiteral("cancel-test-ground"), nullptr, directory.path(), directory.path());
    QObject::connect(&logs, &LogService::recordPublished, &logs, [&](const LogRecord& record) { groundLogs.push_back(record); });
    QTcpSocket observer;
    TelemetryCodec decoder;
    QObject::connect(&observer, &QTcpSocket::readyRead, &observer, [&] {
        for (const auto& frame : decoder.feedBytes(observer.readAll())) {
            LogRecord record;
            if (frame.type == MsgType::LogEvent && TelemetryCodec::parseLogRecord(frame.payload, record)) skyLogs.push_back(record);
        }
    });
    observer.connectToHost(QHostAddress::LocalHost, ipcPort);
    check(waitFor([&] { return observer.state() == QAbstractSocket::ConnectedState; }), "observer connected");
    RemoteSkyController ground;
    QHash<quint32, DeviceOperationResponse> responses;
    QHash<quint16, CommandAck> acks;
    bool configurationReceived = false;
    QObject::connect(&ground, &RemoteSkyController::deviceOperationResponseReceived,
        [&](const DeviceOperationResponse& response) { responses.insert(response.request_id, response); });
    QObject::connect(&ground, &RemoteSkyController::commandAckReceived,
        [&](const CommandAck& ack) { acks.insert(ack.command_seq, ack); });
    QObject::connect(ground.telemetryService(), &GroundTelemetryService::skyConfigReceived,
        [&](const QJsonObject&) { configurationReceived = true; });
    check(ground.openTcp(QStringLiteral("127.0.0.1"), telemetryPort), "Ground connects to test runtime");
    check(waitFor([&] { return ground.isOpen(); }), "Ground transport open");
    const quint32 operation = ground.calibrateEpsilon(EpsilonMaintenanceAction::Magnetic2D);
    check(operation != 0, "magnetic operation submitted");
    check(waitFor([&] {
        if (!responses.contains(operation)) return false;
        EpsilonMaintenanceResult value;
        return TelemetryCodec::parseEpsilonMaintenanceResult(responses.value(operation).payload, value) &&
               value.status == EpsilonMaintenanceStatus::Running;
    }), "actual maintenance worker reports running");
    const QString operationId = responses.value(operation).correlation_id;
    check(!operationId.isEmpty(), "running result retains correlation");
    const quint16 query = ground.sendCommand(CommandId::GetSkyConfig);
    check(query && waitFor([&] { return acks.contains(query) && configurationReceived; }) &&
          acks.value(query).error_code == CommandErrorCode::Ok,
          "independent configuration query succeeds during maintenance");
    QString recordingId;
    const quint16 recording = ground.sendCommand(CommandId::StopRecording, {}, &recordingId);
    const quint32 cancellation = ground.cancelEpsilonMagneticCalibration(operation);
    check(recording != 0 && !recordingId.isEmpty() && recordingId != operationId && cancellation != 0,
          "recording and cancellation are submitted while maintenance is running");
    check(waitFor([&] { return acks.contains(recording) && responses.contains(cancellation); }),
          "concurrent recording and cancellation responses arrive");
    const auto recordingAck = acks.value(recording);
    check(recordingAck.error_code == CommandErrorCode::RecordingNotStarted &&
          recordingAck.correlation_id == recordingId && recordingAck.session_id.isEmpty(),
          "independent stop reports no active recording with its own identity");
    const auto cancelResult = responses.value(cancellation);
    check(cancelResult.error_code == CommandErrorCode::Ok && !cancelResult.correlation_id.isEmpty() &&
          cancelResult.correlation_id != operationId && cancelResult.correlation_id != recordingId,
          "cancel has separate operation identity");
    check(waitFor([&] {
        EpsilonMaintenanceResult value;
        return responses.contains(operation) && TelemetryCodec::parseEpsilonMaintenanceResult(responses.value(operation).payload, value) &&
               value.status == EpsilonMaintenanceStatus::Cancelled;
    }), "actual maintenance worker stops after cancellation");
    check(responses.value(operation).correlation_id == operationId && responses.value(operation).session_id.isEmpty(),
          "cancelled result keeps the original identity and no session");
    check(waitFor([&] {
        bool sent = false, acknowledged = false, received = false, completed = false;
        for (const auto& record : groundLogs) if (record.correlation_id == recordingId) {
            const QString event = record.fields.value(QStringLiteral("event")).toString();
            sent |= event == QStringLiteral("ground_command_send");
            acknowledged |= event == QStringLiteral("ground_command_ack") && record.session_id == recordingAck.session_id;
        }
        for (const auto& record : skyLogs) if (record.correlation_id == recordingId) {
            const QString event = record.fields.value(QStringLiteral("event")).toString();
            received |= event == QStringLiteral("sky_command_received");
            completed |= event == QStringLiteral("sky_command_completed") && record.session_id == recordingAck.session_id;
        }
        return sent && acknowledged && received && completed;
    }), "concurrent recording stop retains its distinct identity across Ground and SkyCore");
    check(waitFor([&] { childOutput += child.process.readAll(); return childOutput.contains("FAKE_RESTORE 3 1 2"); }),
          "fake transport observed all AID restores, save and exit");
    const quint16 after = ground.sendCommand(CommandId::SetTelemetryRate, TelemetryCodec::serializeRatePayload(3));
    check(after && waitFor([&] { return acks.contains(after); }) && acks.value(after).error_code == CommandErrorCode::Ok,
          "subsequent independent operation succeeds after worker exits");
    quint64 startedThread = 0, finishedThread = 0;
    check(waitFor([&] {
        for (const auto& record : skyLogs) if (record.correlation_id == operationId) {
            const QString event = record.fields.value(QStringLiteral("event")).toString();
            if (event == QStringLiteral("sky_device_worker_started") && record.process_id == quint64(childPid)) startedThread = record.thread_id;
            if (event == QStringLiteral("sky_device_worker_finished") && record.process_id == quint64(childPid)) finishedThread = record.thread_id;
        }
        return startedThread && finishedThread;
    }) && startedThread == finishedThread, "worker lifecycle logs keep original ID in the same child worker thread");
    std::cout << "CANCEL_E2E operation=" << operationId.toStdString() << " cancel="
              << cancelResult.correlation_id.toStdString() << " concurrent_stop_recording=" << recordingId.toStdString()
              << " session=" << recordingAck.session_id.toStdString() << " child_pid=" << childPid << std::endl;
    ground.close();
    observer.disconnectFromHost();
    QFile done(directory.filePath(QStringLiteral("done")));
    check(done.open(QIODevice::WriteOnly), "signal child shutdown");
    done.close();
    check(child.process.waitForFinished(7000) && child.process.exitCode() == 0, "test child exits normally");
    return 0;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        return args.size() > 1 && args.at(1) == QStringLiteral("--child") ? childMain(app, args) : parentMain();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << std::endl;
        return 1;
    }
}
