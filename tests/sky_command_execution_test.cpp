#include "SkyRuntime.h"
#include "SkyLocalIpcServer.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTcpSocket>
#include <QPointer>
#include <cstdlib>
#include <iostream>

static void require(bool value, const char *message)
{
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    VaporView::SkyRuntimeOptions options;
    options.simulate_data = true;
    options.telemetry_tcp_port = 0;
    options.config_path = directory.filePath(QStringLiteral("sky.json"));
    auto config = VaporView::SkyConfig::defaults();
    config.temperature_controller.enabled = true;
    require(config.saveToFile(options.config_path), "save isolated enabled temperature fixture");
    VaporView::SkyRuntime runtime(options);
    require(runtime.start(), "start isolated simulated Core");
    runtime.connectDevice(VaporView::SkyDeviceId::Epsilon);
    VaporView::EpsilonPacketRatesOperation rates;
    rates.output_rate_hz = 100;
    rates.callback_rate_hz = 20;
    rates.packet_rates = {{0x5C, 10}};
    VaporView::DeviceOperationRequest operation;
    operation.request_id = 77;
    operation.device_id = VaporView::SkyDeviceId::Epsilon;
    operation.operation = VaporView::DeviceOperation::ConfigureEpsilonPacketRates;
    operation.payload = VaporView::TelemetryCodec::serializeEpsilonPacketRatesOperation(rates);
    VaporView::CommandMessage command;
    command.command_id = VaporView::CommandId::DeviceOperation;
    command.command_seq = 13;
    command.payload = VaporView::TelemetryCodec::serializeDeviceOperationRequest(operation);
    int completions = 0;
    auto completed = [&](const VaporView::SkyCommandResult& result) {
        require(result.ack.error_code == VaporView::CommandErrorCode::Ok, "configuration completed successfully");
        require(result.device_operation_response.request_id == 77, "result retains logical request identity");
        ++completions;
    };
    for (int repeat = 0; repeat < 4; ++repeat) runtime.submitCommand(command, completed);
    require(completions == 0, "device transaction never completes synchronously on the Core loop");
    auto competing = command;
    competing.command_seq++;
    runtime.submitCommand(competing, [&](const VaporView::SkyCommandResult& result) {
        require(result.ack.error_code == VaporView::CommandErrorCode::DeviceOperationBusy,
                "competing device transaction is rejected while collector is owned by worker");
    });
    VaporView::CommandMessage status;
    status.command_id = VaporView::CommandId::RequestStatus;
    bool statusCompleted = false;
    runtime.submitCommand(status, [&](const VaporView::SkyCommandResult& result) {
        statusCompleted = result.ack.error_code == VaporView::CommandErrorCode::Ok;
    });
    require(statusCompleted, "Core serves status while transaction is in flight");
    QElapsedTimer deadline;
    deadline.start();
    while (completions != 4 && deadline.elapsed() < 3000) QCoreApplication::processEvents();
    require(completions == 4, "duplicate requests share one completion");
    runtime.submitCommand(command, completed);
    require(completions == 5, "completed retry immediately reuses cached result");
    operation.request_id = 78;
    operation.operation = VaporView::DeviceOperation::ReadEpsilonSettings;
    operation.payload = VaporView::TelemetryCodec::serializeEpsilonSettingsRead(VaporView::EpsilonSettingsGroup::Installation);
    command.command_seq = 15;
    command.payload = VaporView::TelemetryCodec::serializeDeviceOperationRequest(operation);
    bool settingsCompleted = false;
    runtime.submitCommand(command, [&](const VaporView::SkyCommandResult& result) {
        require(result.ack.error_code == VaporView::CommandErrorCode::UnknownCommand,
                "simulation cannot report fabricated hardware parameter values");
        settingsCompleted = true;
    });
    VaporView::CommandMessage startRecording;
    startRecording.command_id = VaporView::CommandId::StartRecording;
    require(runtime.executeCommand(startRecording).ack.error_code == VaporView::CommandErrorCode::DeviceOperationBusy,
            "recording cannot start while a settings transaction owns the collector");
    QString recordingError;
    require(!runtime.startRecording(&recordingError), "direct recording API observes settings ownership");
    deadline.restart();
    while (!settingsCompleted && deadline.elapsed() < 3000) QCoreApplication::processEvents();
    require(settingsCompleted, "unsupported settings transaction releases collector ownership");

    operation.request_id = 79;
    operation.operation = VaporView::DeviceOperation::CalibrateEpsilonLevel;
    operation.payload = VaporView::TelemetryCodec::serializeEpsilonMaintenanceAction(VaporView::EpsilonMaintenanceAction::Level);
    command.command_seq = 16;
    command.payload = VaporView::TelemetryCodec::serializeDeviceOperationRequest(operation);
    const auto calibrationResult = runtime.executeCommand(command);
    require(calibrationResult.ack.error_code == VaporView::CommandErrorCode::UnknownCommand,
            "Core rejects calibration without a completed maintenance workflow");
    command.command_seq = 17;
    operation.request_id = 80;
    command.payload = VaporView::TelemetryCodec::serializeDeviceOperationRequest(operation);
    bool maintenanceCompleted = false;
    runtime.submitCommand(command, [&](const VaporView::SkyCommandResult& result) {
        require(result.ack.error_code == VaporView::CommandErrorCode::UnknownCommand,
                "asynchronous simulation also refuses fabricated calibration ACK");
        maintenanceCompleted = true;
    });
    require(runtime.executeCommand(startRecording).ack.error_code == VaporView::CommandErrorCode::DeviceOperationBusy,
            "recording cannot start while maintenance owns the collector");
    deadline.restart();
    while (!maintenanceCompleted && deadline.elapsed() < 3000) QCoreApplication::processEvents();
    require(maintenanceCompleted, "unsupported maintenance releases collector ownership");
    operation.operation = VaporView::DeviceOperation::CalibrateEpsilonMagnetic3D;
    operation.payload = VaporView::TelemetryCodec::serializeEpsilonMaintenanceAction(VaporView::EpsilonMaintenanceAction::Magnetic3D);
    operation.request_id = 81;
    command.command_seq = 18;
    command.payload = VaporView::TelemetryCodec::serializeDeviceOperationRequest(operation);
    bool magneticCompleted = false;
    runtime.submitCommand(command, [&](const VaporView::SkyCommandResult& result) {
        require(result.ack.error_code == VaporView::CommandErrorCode::UnknownCommand,
                "simulation cannot invent magnetic calibration completion");
        VaporView::EpsilonMaintenanceResult maintenance;
        require(VaporView::TelemetryCodec::parseEpsilonMaintenanceResult(result.device_operation_response.payload, maintenance) &&
                maintenance.action == VaporView::EpsilonMaintenanceAction::Magnetic3D,
                "unsupported magnetic result matches requested action");
        magneticCompleted = true;
    });
    VaporView::DeviceOperationRequest cancel;
    cancel.request_id = 82;
    cancel.device_id = VaporView::SkyDeviceId::Epsilon;
    cancel.operation = VaporView::DeviceOperation::CancelEpsilonMagneticCalibration;
    cancel.payload = QByteArray::fromHex("51000000");
    auto cancelCommand = command;
    cancelCommand.command_seq = 19;
    cancelCommand.payload = VaporView::TelemetryCodec::serializeDeviceOperationRequest(cancel);
    bool cancelAccepted = false;
    runtime.submitCommand(cancelCommand, [&](const VaporView::SkyCommandResult& result) {
        cancelAccepted = result.ack.error_code == VaporView::CommandErrorCode::Ok;
    });
    require(cancelAccepted && !magneticCompleted, "cancel is accepted before busy check without claiming calibration completion");
    deadline.restart();
    while (!magneticCompleted && deadline.elapsed() < 3000) QCoreApplication::processEvents();
    require(magneticCompleted, "magnetic worker final result releases ownership after cancel");

    VaporView::CommandMessage enable;
    enable.command_id = VaporView::CommandId::EnableWaveformStreaming;
    enable.command_seq = 100;
    auto apply = [&](const VaporView::CommandMessage& request, const QByteArray& client = "retry-client") {
        VaporView::CommandErrorCode error = VaporView::CommandErrorCode::InternalError;
        runtime.submitCommand(request, [&](const VaporView::SkyCommandResult& result) {
            error = result.ack.error_code;
        }, client);
        return error;
    };
    require(apply(enable) == VaporView::CommandErrorCode::Ok && runtime.waveformStreamingEnabled(),
            "ordinary mutating command executes once");
    auto disable = enable;
    disable.command_id = VaporView::CommandId::DisableWaveformStreaming;
    disable.command_seq = 101;
    require(apply(disable) == VaporView::CommandErrorCode::Ok && !runtime.waveformStreamingEnabled(),
            "new identity executes a later command");
    require(apply(enable) == VaporView::CommandErrorCode::Ok && !runtime.waveformStreamingEnabled(),
            "lost ACK retry reuses result without repeating the earlier side effect");
    disable.command_seq = enable.command_seq;
    require(apply(disable) == VaporView::CommandErrorCode::InvalidPayload,
            "one command identity cannot be reused for a different payload");
    require(apply(enable, "other-client") == VaporView::CommandErrorCode::Ok && runtime.waveformStreamingEnabled(),
            "independent clients do not share command identities");

    runtime.connectDevice(VaporView::SkyDeviceId::TemperatureController);
    VaporView::TemperatureControllerCommand temperature;
    temperature.channel = 1;
    temperature.target_temperature_c = 20;
    VaporView::CommandMessage temperatureCommand;
    temperatureCommand.command_id = VaporView::CommandId::SetTemperatureTarget;
    temperatureCommand.command_seq = 110;
    temperatureCommand.payload = VaporView::TelemetryCodec::serializeTemperatureControllerCommand(temperature);
    bool temperatureCompleted = false;
    runtime.submitCommand(temperatureCommand, [&](const VaporView::SkyCommandResult& result) {
        require(result.ack.error_code == VaporView::CommandErrorCode::Ok, "temperature transaction completes");
        temperatureCompleted = true;
    });
    require(!temperatureCompleted, "temperature control yields the Core event loop");
    statusCompleted = false;
    runtime.submitCommand(status, [&](const VaporView::SkyCommandResult& result) {
        statusCompleted = result.ack.error_code == VaporView::CommandErrorCode::Ok;
    });
    require(statusCompleted, "status remains available during temperature control");
    deadline.restart();
    while (!temperatureCompleted && deadline.elapsed() < 3000) QCoreApplication::processEvents();
    require(temperatureCompleted, "temperature completion returns on the Core event loop");

    VaporView::SkyLocalIpcServer ipc(&runtime);
    require(ipc.listen(QStringLiteral("127.0.0.1"), 0), "listen isolated IPC server");
    QTcpSocket slowClient;
    slowClient.setReadBufferSize(1);
    slowClient.connectToHost(QStringLiteral("127.0.0.1"), ipc.serverPort());
    deadline.restart();
    while (ipc.findChildren<QTcpSocket*>().isEmpty() && deadline.elapsed() < 3000)
        QCoreApplication::processEvents();
    const auto peers = ipc.findChildren<QTcpSocket*>();
    require(peers.size() == 1, "slow IPC client is accepted");
    QPointer<QTcpSocket> acceptedPeer = peers.front();
    VaporView::DownsampledWaveform waveform;
    waveform.samples.fill(1.0f, 8192);
    waveform.original_point_count = waveform.downsampled_point_count = waveform.samples.size();
    const QByteArray waveformPayload = VaporView::TelemetryCodec::serializeDownsampledWaveform(waveform);
    for (int frame = 0; frame < 512; ++frame)
    {
        runtime.telemetryFrameReady(VaporView::MsgType::WaveformDownsampled, waveformPayload);
        require(acceptedPeer && acceptedPeer->bytesToWrite() <= 4 * 1024 * 1024,
                "IPC burst cannot grow the pending write queue past its limit");
    }
    deadline.restart();
    while (acceptedPeer && acceptedPeer->state() != QAbstractSocket::UnconnectedState && deadline.elapsed() < 3000)
        QCoreApplication::processEvents();
    require(!acceptedPeer || acceptedPeer->state() == QAbstractSocket::UnconnectedState,
            "slow IPC client is disconnected outside the broadcast traversal");
    require(runtime.isRunning(), "slow IPC peer cannot stop the Core");
    statusCompleted = false;
    runtime.submitCommand(status, [&](const VaporView::SkyCommandResult& result) {
        statusCompleted = result.ack.error_code == VaporView::CommandErrorCode::Ok;
    });
    require(statusCompleted, "Core remains responsive after IPC backpressure");
    ipc.close();
    runtime.stop();
    QCoreApplication::processEvents();
    require(runtime.start(), "restart Core with a new command lifetime");
    runtime.setWaveformStreamingEnabled(false);
    require(apply(enable) == VaporView::CommandErrorCode::Ok && runtime.waveformStreamingEnabled(),
            "same identity after Core restart cannot reuse an old cached result");
    runtime.stop();
}
