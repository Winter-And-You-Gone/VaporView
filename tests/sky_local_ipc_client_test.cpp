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
    bool logReceived = false;
    bool dashboardDeviceDataReceived = false;
    bool localStructuredLogReceived = false;
    QObject::connect(&client, &VaporView::SkyLocalIpcClient::connectedChanged, [&](bool value) {
        connected = value;
    });
    QObject::connect(&client, &VaporView::SkyLocalIpcClient::logRecordGenerated,
                     [&](const VaporView::LogRecord& record) {
                         if (record.source == QStringLiteral("SkyTui") &&
                             record.level == VaporView::LogLevel::Info &&
                             record.category == QStringLiteral("ipc.connection") &&
                             record.message == QStringLiteral("SkyCore IPC 已连接。") &&
                             record.fields.value(QStringLiteral("ui_visibility")).toString() ==
                                 QStringLiteral("details") &&
                             record.fields.value(QStringLiteral("event")).toString() ==
                                 QStringLiteral("sky_ipc_connected"))
                         {
                             localStructuredLogReceived = true;
                         }
                     });
    QObject::connect(&client, &VaporView::SkyLocalIpcClient::ackReceived, [&](const VaporView::CommandAck& ack) {
        ackReceived = ack.command_id == VaporView::CommandId::RequestStatus &&
                      ack.error_code == VaporView::CommandErrorCode::Ok;
    });
    QObject::connect(&client, &VaporView::SkyLocalIpcClient::statusReceived, [&](const VaporView::TelemetryStatus& status) {
        statusReceived = status.session_name == QStringLiteral("ipc-test");
    });
    QObject::connect(&client, &VaporView::SkyLocalIpcClient::logRecordReceived, [&](const VaporView::LogRecord& record) {
        logReceived = record.level == VaporView::LogLevel::Warning &&
                      record.source == QStringLiteral("SkyCore") &&
                      record.category == QStringLiteral("ipc") &&
                      record.message == QStringLiteral("IPC log test") &&
                      record.fields.value(QStringLiteral("event")).toString() ==
                          QStringLiteral("sky_ipc_test_log");
    });
    QObject::connect(&client, &VaporView::SkyLocalIpcClient::dashboardUpdated, [&]() {
        const VaporView::SkyDashboardSnapshot dashboard = client.dashboardSnapshot();
        dashboardDeviceDataReceived =
            dashboard.temperature_controller.valid &&
            std::abs(dashboard.temperature_controller.channels[0].target_temperature_c - 31.0) < 0.001 &&
            dashboard.ai8_temperature_controller.valid &&
            std::abs(dashboard.ai8_temperature_controller.measuredC[7] - 27.5) < 0.001;
    });

    client.connectToCore(QStringLiteral("127.0.0.1"), server.serverPort());
    require(waitUntil([&]() {
        return connected && server.hasPendingConnections() && localStructuredLogReceived;
    }), "client connects and reports structured local log");

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
    VaporView::LogRecord logRecord;
    logRecord.timestamp_us = nowUs();
    logRecord.level = VaporView::LogLevel::Warning;
    logRecord.source = QStringLiteral("SkyCore");
    logRecord.category = QStringLiteral("ipc");
    logRecord.message = QStringLiteral("IPC log test");
    logRecord.fields.insert(QStringLiteral("event"), QStringLiteral("sky_ipc_test_log"));
    socket->write(encoder.encodeFrame(VaporView::MsgType::LogEvent,
                                      VaporView::TelemetryCodec::serializeLogRecord(logRecord),
                                      3,
                                      nowUs()));
    VaporView::TemperatureControllerData rd105;
    rd105.valid = true;
    rd105.channels[0].target_temperature_c = 31.0;
    rd105.channels[0].measured_temperature_c = 29.5;
    socket->write(encoder.encodeFrame(VaporView::MsgType::TemperatureControllerStatus,
                                      VaporView::TelemetryCodec::serializeTemperatureControllerStatus(rd105),
                                      4,
                                      nowUs()));
    VaporView::Ai8TemperatureControllerProtocol::LiveData ai8;
    ai8.valid = true;
    ai8.controlStatesValid = true;
    for (int index = 0; index < VaporView::Ai8TemperatureControllerProtocol::kChannelCount; ++index)
    {
        ai8.measuredC[static_cast<size_t>(index)] = 20.5 + index;
    }
    socket->write(encoder.encodeFrame(VaporView::MsgType::Ai8TemperatureControllerStatus,
                                      VaporView::TelemetryCodec::serializeAi8TemperatureControllerStatus(ai8),
                                      5,
                                      nowUs()));
    socket->flush();

    require(waitUntil([&]() { return ackReceived && statusReceived && logReceived && dashboardDeviceDataReceived; }),
            "client parses ack, status, log and temperature dashboard frames");

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

    client.setAutoReconnectEnabled(false);
    client.disconnectFromCore();
    require(waitUntil([&]() { return !connected; }), "client closes after reconnect");

    std::cout << "sky_local_ipc_client_test passed\n";
    return 0;
}
