#include "TelemetryCodec.h"
#include "ground/devices/Rd105DeviceSession.h"
#include "ground/devices/RemoteSkyController.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <utility>

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

quint64 nowUs()
{
    return static_cast<quint64>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

bool waitForResult(QCoreApplication& app,
                   const QVector<VaporView::Ground::Devices::Rd105SessionResult>& results,
                   int timeoutMs = 1500)
{
    QElapsedTimer timer;
    timer.start();
    while (results.isEmpty() && timer.elapsed() < timeoutMs)
    {
        app.processEvents(QEventLoop::AllEvents, 20);
    }
    return !results.isEmpty();
}

VaporView::TemperatureControllerCommand targetCommand(double celsius = 42.5)
{
    VaporView::TemperatureControllerCommand command;
    command.channel = 2;
    command.target_temperature_c = celsius;
    return command;
}

VaporView::TemperatureControllerCommand baudCommand(quint16 baudIndex)
{
    VaporView::TemperatureControllerCommand command;
    command.rs485_baud_index = baudIndex;
    return command;
}

VaporView::TemperatureControllerCommand addressCommand(quint16 address)
{
    VaporView::TemperatureControllerCommand command;
    command.device_address = address;
    return command;
}

VaporView::TemperatureControllerCommand sensorConfigCommand()
{
    VaporView::TemperatureControllerCommand command;
    command.channel = 2;
    command.sensor_model = 1;
    command.ntc_r0 = 100000;
    command.ntc_b = 395000;
    return command;
}

VaporView::TemperatureControllerData confirmedData(double celsius)
{
    VaporView::TemperatureControllerData data;
    data.valid = true;
    data.timestamp = std::chrono::steady_clock::now();
    data.channels[1].target_temperature_c = celsius;
    return data;
}

bool isRd105TestCommand(VaporView::CommandId command)
{
    return command >= VaporView::CommandId::SetTemperatureTarget &&
           command <= VaporView::CommandId::SetTemperatureStartupDelay;
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace VaporView;
    using namespace VaporView::Ground::Devices;

    Rd105DeviceSession::LocalAdapter adapter;
    adapter.sendCommand = [](CommandId command, const TemperatureControllerCommand& payload) {
        require(command == CommandId::SetTemperatureTarget,
                "local RD105 adapter receives command id");
        require(payload.channel == 2 && payload.target_temperature_c > 40.0,
                "local RD105 adapter receives structured payload");
        LocalTemperatureCommandResult result;
        result.status = LocalTemperatureCommandStatus::Confirmed;
        result.latestData = confirmedData(payload.target_temperature_c);
        return result;
    };

    Rd105DeviceSession session(std::move(adapter), nullptr);
    session.setLocalAvailable(true, QStringLiteral("simulated local RD105"));
    QVector<Rd105SessionResult> results;
    QObject::connect(&session, &Rd105DeviceSession::operationFinished,
                     [&results](const Rd105SessionResult& result) {
                         results.push_back(result);
                     });

    const quint64 localId = session.sendCommand(CommandId::SetTemperatureTarget, targetCommand());
    require(localId != 0 && waitForResult(app, results) &&
                results.back().request_id == localId &&
                results.back().backend == Rd105Backend::Local &&
                results.back().success() &&
                results.back().has_latest_data,
            "local RD105 command completes with read-back data");
    results.clear();

    session.setLocalAvailable(false);
    const quint64 disconnectedId =
        session.sendCommand(CommandId::SetTemperatureTarget, targetCommand());
    require(disconnectedId != 0 && waitForResult(app, results) &&
                results.back().outcome == Rd105OperationOutcome::Disconnected,
            "local RD105 command reports disconnected when unavailable");
    results.clear();

    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost),
            "RD105 fake Sky server listens on localhost");
    QTcpSocket *serverSocket = nullptr;
    TelemetryCodec inboundCodec;
    TelemetryCodec outboundCodec;
    bool acknowledgeCommands = true;
    quint16 outboundSeq = 1;
    QObject::connect(&server, &QTcpServer::newConnection, [&]() {
        serverSocket = server.nextPendingConnection();
        QObject::connect(serverSocket, &QTcpSocket::readyRead, [&]() {
            const QVector<TelemetryFrame> frames =
                inboundCodec.feedBytes(serverSocket->readAll());
            for (const TelemetryFrame& frame : frames)
            {
                if (frame.type != MsgType::Command)
                {
                    continue;
                }
                CommandMessage message;
                require(TelemetryCodec::parseCommand(frame.payload, message),
                        "fake Sky parses RD105 command");
                if (!isRd105TestCommand(message.command_id))
                {
                    continue;
                }
                TemperatureControllerCommand parsed;
                require(TelemetryCodec::parseTemperatureControllerCommand(
                            message.payload, parsed),
                        "fake Sky parses RD105 payload");
                if (message.command_id == CommandId::SetTemperatureTarget ||
                    message.command_id == CommandId::SetTemperatureSensorConfig)
                {
                    require(parsed.channel == 2,
                            "remote RD105 command preserves channel when required");
                }
                if (!acknowledgeCommands)
                {
                    continue;
                }
                CommandAck ack;
                ack.command_id = message.command_id;
                ack.command_seq = message.command_seq;
                ack.result = 0;
                ack.error_code = CommandErrorCode::Ok;
                const QByteArray ackFrame = outboundCodec.encodeFrame(
                    MsgType::CommandAck,
                    TelemetryCodec::serializeCommandAck(ack),
                    outboundSeq++,
                    nowUs());
                serverSocket->write(ackFrame);
                serverSocket->flush();
            }
        });
    });

    RemoteSkyController remoteController;
    Rd105DeviceSession remoteSession({}, &remoteController);
    remoteSession.setBackend(Rd105Backend::Remote);
    remoteSession.setRemoteAvailable(true, QStringLiteral("simulated remote RD105"));
    QVector<Rd105SessionResult> remoteResults;
    QObject::connect(&remoteSession, &Rd105DeviceSession::operationFinished,
                     [&remoteResults](const Rd105SessionResult& result) {
                         remoteResults.push_back(result);
                     });
    require(remoteController.openTcp(QStringLiteral("127.0.0.1"), server.serverPort()),
            "RD105 controller opens fake Sky TCP link");
    QElapsedTimer linkTimer;
    linkTimer.start();
    while ((!remoteController.isOpen() || serverSocket == nullptr) &&
           linkTimer.elapsed() < 3000)
    {
        app.processEvents(QEventLoop::AllEvents, 20);
    }
    require(remoteController.isOpen() && serverSocket != nullptr,
            "RD105 fake Sky TCP link is established");

    const quint64 remoteId =
        remoteSession.sendCommand(CommandId::SetTemperatureTarget, targetCommand(43.0));
    require(remoteId != 0 && waitForResult(app, remoteResults, 3000) &&
                remoteResults.back().request_id == remoteId &&
                remoteResults.back().backend == Rd105Backend::Remote &&
                remoteResults.back().success(),
            "remote RD105 command completes on ACK");
    remoteResults.clear();

    const std::pair<CommandId, TemperatureControllerCommand> configCommands[] = {
        {CommandId::SetTemperatureRs485Baud, baudCommand(4)},
        {CommandId::SetTemperatureDeviceAddress, addressCommand(7)},
        {CommandId::SetTemperatureSensorConfig, sensorConfigCommand()},
        {CommandId::RestoreTemperatureFactoryDefaults, TemperatureControllerCommand{}},
    };
    for (const auto& [command, payload] : configCommands)
    {
        const quint64 configId = remoteSession.sendCommand(command, payload);
        require(configId != 0 && waitForResult(app, remoteResults, 3000) &&
                    remoteResults.back().request_id == configId &&
                    remoteResults.back().command == command &&
                    remoteResults.back().backend == Rd105Backend::Remote &&
                    remoteResults.back().success(),
                "remote RD105 configuration command completes on ACK");
        remoteResults.clear();
    }

    acknowledgeCommands = false;
    const quint64 timeoutId =
        remoteSession.sendCommand(CommandId::SetTemperatureTarget, targetCommand(44.0));
    require(timeoutId != 0 && waitForResult(app, remoteResults, 5000) &&
                remoteResults.back().request_id == timeoutId &&
                remoteResults.back().outcome == Rd105OperationOutcome::Timeout,
            "remote RD105 command times out without ACK");
    remoteResults.clear();

    acknowledgeCommands = false;
    const quint64 staleId =
        remoteSession.sendCommand(CommandId::SetTemperatureTarget, targetCommand(45.0));
    remoteSession.setBackend(Rd105Backend::Local);
    require(staleId != 0 && waitForResult(app, remoteResults) &&
                remoteResults.back().request_id == staleId &&
                remoteResults.back().outcome == Rd105OperationOutcome::Disconnected,
            "RD105 backend switch completes the active remote command as disconnected");
    const int resultCountAfterSwitch = remoteResults.size();
    QTimer::singleShot(50, [&]() {
        if (!serverSocket)
        {
            return;
        }
        CommandAck ack;
        ack.command_id = CommandId::SetTemperatureTarget;
        ack.command_seq = 999;
        ack.result = 0;
        ack.error_code = CommandErrorCode::Ok;
        serverSocket->write(outboundCodec.encodeFrame(
            MsgType::CommandAck,
            TelemetryCodec::serializeCommandAck(ack),
            outboundSeq++,
            nowUs()));
        serverSocket->flush();
    });
    QElapsedTimer staleTimer;
    staleTimer.start();
    while (staleTimer.elapsed() < 150)
    {
        app.processEvents(QEventLoop::AllEvents, 20);
    }
    require(remoteResults.size() == resultCountAfterSwitch,
            "late RD105 ACK does not mutate a new backend generation");

    remoteController.close();
    server.close();
    if (serverSocket)
    {
        serverSocket->deleteLater();
    }

    std::cout << "rd105_device_session_test passed\n";
    return 0;
}
