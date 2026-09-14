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
}
