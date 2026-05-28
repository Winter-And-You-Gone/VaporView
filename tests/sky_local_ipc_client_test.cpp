#include "SkyLocalIpcClient.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <cstdlib>
#include <iostream>

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

quint64 nowUs()
{
    return static_cast<quint64>(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) * 1000ULL;
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost, 0), "listen localhost");

    VaporView::SkyLocalIpcClient client;
    client.setAutoReconnectEnabled(true, 50);
    bool connected = false;
    bool ackReceived = false;
    bool statusReceived = false;
    QObject::connect(&client, &VaporView::SkyLocalIpcClient::connectedChanged, [&](bool value) {
        connected = value;
    });
    QObject::connect(&client, &VaporView::SkyLocalIpcClient::ackReceived, [&](const VaporView::CommandAck& ack) {
        ackReceived = ack.command_id == VaporView::CommandId::RequestStatus &&
                      ack.error_code == VaporView::CommandErrorCode::Ok;
    });
    QObject::connect(&client, &VaporView::SkyLocalIpcClient::statusReceived, [&](const VaporView::TelemetryStatus& status) {
        statusReceived = status.session_name == QStringLiteral("ipc-test");
    });

    client.connectToCore(QStringLiteral("127.0.0.1"), server.serverPort());
    require(waitUntil([&]() { return connected && server.hasPendingConnections(); }), "client connects");

    QTcpSocket *socket = server.nextPendingConnection();
    require(socket != nullptr, "accept client");

    VaporView::TelemetryCodec decoder;
    QVector<VaporView::TelemetryFrame> receivedFrames;
    require(waitUntil([&]() {
        const QByteArray bytes = socket->readAll();
        if (!bytes.isEmpty())
        {
            receivedFrames += decoder.feedBytes(bytes);
        }
        return !receivedFrames.isEmpty();
    }), "receive command frame");

    VaporView::CommandMessage request;
    bool sawRequestStatus = false;
    for (const VaporView::TelemetryFrame& frame : receivedFrames)
    {
        if (frame.type == VaporView::MsgType::Command &&
            VaporView::TelemetryCodec::parseCommand(frame.payload, request) &&
            request.command_id == VaporView::CommandId::RequestStatus)
        {
            sawRequestStatus = true;
            break;
        }
    }
    require(sawRequestStatus, "client sends RequestStatus command");

    VaporView::TelemetryCodec encoder;
    VaporView::CommandAck ack;
    ack.command_id = VaporView::CommandId::RequestStatus;
    ack.command_seq = request.command_seq;
    ack.error_code = VaporView::CommandErrorCode::Ok;
    socket->write(encoder.encodeFrame(VaporView::MsgType::CommandAck,
                                      VaporView::TelemetryCodec::serializeCommandAck(ack),
                                      1,
                                      nowUs()));

    VaporView::TelemetryStatus status;
    status.session_name = QStringLiteral("ipc-test");
    status.recording_state = 1;
    socket->write(encoder.encodeFrame(VaporView::MsgType::TelemetryStatus,
                                      VaporView::TelemetryCodec::serializeTelemetryStatus(status),
                                      2,
                                      nowUs()));
    socket->flush();

    require(waitUntil([&]() { return ackReceived && statusReceived; }), "client parses ack and status");

    const quint16 shutdownSeq = client.requestCoreShutdown();
    require(shutdownSeq != 0, "client sends shutdown request");
    QVector<VaporView::TelemetryFrame> shutdownFrames;
    require(waitUntil([&]() {
        const QByteArray bytes = socket->readAll();
        if (!bytes.isEmpty())
        {
            shutdownFrames += decoder.feedBytes(bytes);
        }
        for (const VaporView::TelemetryFrame& frame : shutdownFrames)
        {
            VaporView::CommandMessage command;
            if (frame.type == VaporView::MsgType::Command &&
                VaporView::TelemetryCodec::parseCommand(frame.payload, command) &&
                command.command_id == VaporView::CommandId::ShutdownCore &&
                command.command_seq == shutdownSeq)
            {
                return true;
            }
        }
        return false;
    }), "client sends ShutdownCore command");

    socket->disconnectFromHost();
    require(waitUntil([&]() { return !connected; }), "client observes disconnected socket");
    require(waitUntil([&]() { return connected && server.hasPendingConnections(); }), "client reconnects after disconnect");
    QTcpSocket *reconnectedSocket = server.nextPendingConnection();
    require(reconnectedSocket != nullptr, "accept reconnected client");

    std::cout << "sky_local_ipc_client_test passed\n";
    return 0;
}
