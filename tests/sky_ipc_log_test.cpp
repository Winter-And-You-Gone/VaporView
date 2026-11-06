#include "LogService.h"
#include "SkyLocalIpcClient.h"
#include "SkyLocalIpcServer.h"
#include "SkyRuntime.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QProcess>
#include <QTemporaryDir>
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
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--shutdown-empty-session-child")))
    {
        QTemporaryDir childLogs;
        require(childLogs.isValid(), "shutdown child log directory");
        VaporView::LogService logService(QStringLiteral("SkyIpcShutdownTest"), &app, childLogs.path());
        VaporView::SkyRuntimeOptions options;
        VaporView::SkyRuntime runtime(options);
        VaporView::SkyLocalIpcServer server(&runtime);
        VaporView::SkyLocalIpcClient client;
        bool connected = false, gotAck = false, gotLog = false;
        QString correlation;
        QObject::connect(&client, &VaporView::SkyLocalIpcClient::connectedChanged,
                         [&](bool value) { connected = value; });
        QObject::connect(&client, &VaporView::SkyLocalIpcClient::logRecordGenerated,
                         [&](const VaporView::LogRecord& record) {
            if (record.fields.value(QStringLiteral("event")) == QStringLiteral("sky_ipc_command_started"))
                correlation = record.correlation_id;
        });
        QObject::connect(&client, &VaporView::SkyLocalIpcClient::ackReceived,
                         [&](const VaporView::CommandAck& ack) {
            if (ack.command_id == VaporView::CommandId::ShutdownCore) {
                require(!correlation.isEmpty() && ack.correlation_id == correlation && ack.session_id.isEmpty(),
                        "shutdown ACK carries correlation and no inactive session");
                gotAck = true;
            }
        });
        QObject::connect(&server, &VaporView::SkyLocalIpcServer::logRecordGenerated,
                         [&](const VaporView::LogRecord& record) {
            if (record.fields.value(QStringLiteral("event")) == QStringLiteral("sky_ipc_shutdown_requested")) {
                require(!correlation.isEmpty() && record.correlation_id == correlation && record.session_id.isEmpty(),
                        "shutdown server log carries correlation and no inactive session");
                gotLog = true;
            }
        });
        require(server.listen(QStringLiteral("127.0.0.1"), 0), "shutdown child server listens");
        client.connectToCore(QStringLiteral("127.0.0.1"), server.serverPort());
        require(waitUntil([&] { return connected; }), "shutdown child client connects");
        require(client.requestCoreShutdown() != 0, "shutdown child command is sent");
        require(waitUntil([&] { return gotAck && gotLog; }), "shutdown child receives ACK and log");
        return 0;
    }
    QTemporaryDir logDirectory;
    require(logDirectory.isValid(), "temporary log directory");

    VaporView::LogService logService(QStringLiteral("SkyIpcLogTest"),
                                     &app,
                                     logDirectory.path());
    VaporView::SkyRuntimeOptions options;
    VaporView::SkyRuntime runtime(options);
    require(runtime.activeSessionId().isEmpty(), "runtime without recording has no active session");
    VaporView::SkyLocalIpcServer server(&runtime);
    bool serverLogRecordObserved = false;
    QString serverListenMessage;
    quint16 serverListenPort = 0;
    QObject::connect(&server, &VaporView::SkyLocalIpcServer::logRecordGenerated,
                     [&](const VaporView::LogRecord& record) {
                         serverLogRecordObserved =
                             record.level == VaporView::LogLevel::Info &&
                             record.source == QStringLiteral("SkyCore") &&
                             record.category == QStringLiteral("ipc") &&
                             record.fields.value(QStringLiteral("event")).toString() ==
                                 QStringLiteral("sky_ipc_listening");
                         if (serverLogRecordObserved)
                         {
                             serverListenMessage = record.message;
                             serverListenPort =
                                 static_cast<quint16>(record.fields.value(QStringLiteral("port")).toUInt());
                         }
                     });
    require(server.listen(QStringLiteral("127.0.0.1"), 0), "IPC server listens");
    require(server.serverPort() != 0, "IPC server exposes assigned port");
    require(serverLogRecordObserved, "IPC server exposes structured log records");
    require(serverListenMessage ==
                QStringLiteral("本地 IPC 服务已开始监听：127.0.0.1:%1。").arg(server.serverPort()),
            "IPC server startup log shows the listen endpoint");
    require(serverListenPort == server.serverPort(),
            "IPC server startup log exposes the assigned port");

    VaporView::SkyLocalIpcClient client;
    bool connected = false;
    int matchingRecords = 0;
    QObject::connect(&client, &VaporView::SkyLocalIpcClient::connectedChanged,
                     [&](bool value) { connected = value; });
    QObject::connect(&client, &VaporView::SkyLocalIpcClient::logRecordReceived,
                     [&](const VaporView::LogRecord& record) {
                         if (record.source == QStringLiteral("SkyCore") &&
                             record.level == VaporView::LogLevel::Warning &&
                             record.category == QStringLiteral("integration") &&
                             record.message == QStringLiteral("SkyCore IPC 结构化日志测试。") &&
                             record.fields.value(QStringLiteral("event")).toString() ==
                                 QStringLiteral("sky_ipc_structured_log_test") &&
                             record.fields.value(QStringLiteral("failure_reason")).toString() ==
                                 QStringLiteral("test"))
                         {
                             ++matchingRecords;
                         }
                     });

    client.connectToCore(QStringLiteral("127.0.0.1"), server.serverPort());
    require(waitUntil([&]() { return connected; }), "SkyTui client connects to SkyCore server");

    logService.publish(VaporView::LogLevel::Warning,
                       QStringLiteral("SkyCore"),
                       QStringLiteral("integration"),
                       QStringLiteral("SkyCore IPC 结构化日志测试。"),
                       {{QStringLiteral("event"), QStringLiteral("sky_ipc_structured_log_test")},
                        {QStringLiteral("failure_reason"), QStringLiteral("test")}});
    require(waitUntil([&]() { return matchingRecords == 1; }),
            "structured SkyCore log reaches SkyTui client exactly once");

    client.disconnectFromCore();
    server.close();

    // Closing the server synchronously while handling the first frame must
    // prevent later frames from the same read from reaching the runtime.
    {
        VaporView::SkyLocalIpcServer reentrantServer(&runtime);
        require(reentrantServer.listen(QStringLiteral("127.0.0.1"), 0), "reentrant server listens");
        QTcpSocket peer;
        peer.connectToHost(QHostAddress::LocalHost, reentrantServer.serverPort());
        require(waitUntil([&] { return peer.state() == QAbstractSocket::ConnectedState; }),
                "reentrant client connects");
        int firstReceived = 0, secondReceived = 0;
        const auto connection = QObject::connect(&logService, &VaporView::LogService::recordPublished,
            &app, [&](const VaporView::LogRecord& record) {
                if (record.fields.value(QStringLiteral("event")) != QStringLiteral("sky_command_received")) return;
                if (record.correlation_id == QStringLiteral("reentrant-first")) {
                    ++firstReceived;
                    reentrantServer.close();
                }
                if (record.correlation_id == QStringLiteral("reentrant-second")) ++secondReceived;
            }, Qt::DirectConnection);
        VaporView::CommandMessage first;
        first.command_id = VaporView::CommandId::RequestStatus;
        first.command_seq = 40;
        first.correlation_id = QStringLiteral("reentrant-first");
        auto second = first;
        second.command_seq = 41;
        second.correlation_id = QStringLiteral("reentrant-second");
        VaporView::TelemetryCodec encoder;
        const auto batch = encoder.encodeFrame(VaporView::MsgType::Command,
            VaporView::TelemetryCodec::serializeCommand(first), 1, 1) +
            encoder.encodeFrame(VaporView::MsgType::Command,
            VaporView::TelemetryCodec::serializeCommand(second), 2, 1);
        require(peer.write(batch) == batch.size(), "reentrant command batch queued");
        require(waitUntil([&] { return firstReceived == 1; }), "first batched command received");
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        require(secondReceived == 0 && !reentrantServer.isListening(),
                "closed connection does not dispatch remaining batched commands");
        QObject::disconnect(connection);
    }

    // An IPC server without a runtime rejects commands but must still echo
    // their operation context, rather than losing it on the early return.
    VaporView::SkyLocalIpcServer unavailableServer(nullptr);
    require(unavailableServer.listen(QStringLiteral("127.0.0.1"), 0), "unavailable server listens");
    QString sentId;
    QString ackId;
    bool unavailableConnected = false;
    VaporView::SkyLocalIpcClient unavailableClient;
    QObject::connect(&unavailableClient, &VaporView::SkyLocalIpcClient::connectedChanged,
                     [&](bool value) { unavailableConnected = value; });
    QObject::connect(&unavailableClient, &VaporView::SkyLocalIpcClient::logRecordGenerated,
                     [&](const VaporView::LogRecord& record) {
                         if (record.fields.value(QStringLiteral("event")) == QStringLiteral("sky_ipc_command_started"))
                             sentId = record.correlation_id;
                     });
    QObject::connect(&unavailableClient, &VaporView::SkyLocalIpcClient::ackReceived,
                     [&](const VaporView::CommandAck& ack) {
                         if (ack.command_id == VaporView::CommandId::ConnectDevice &&
                             ack.error_code == VaporView::CommandErrorCode::InternalError)
                             ackId = ack.correlation_id;
                     });
    unavailableClient.connectToCore(QStringLiteral("127.0.0.1"), unavailableServer.serverPort());
    require(waitUntil([&]() { return unavailableConnected; }), "client connects to unavailable runtime");
    require(unavailableClient.connectDevice(VaporView::SkyDeviceId::Bmp390) != 0, "device command sent");
    require(waitUntil([&]() { return !ackId.isEmpty(); }), "error ack preserves operation identity");
    require(ackId == sentId, "error ack matches original command identity");
    QElapsedTimer statusTimer;
    statusTimer.start();
    require(waitUntil([&]() { return statusTimer.elapsed() >= 550; }),
            "status timer remains safe without a runtime");
    unavailableClient.disconnectFromCore();
    unavailableServer.close();
    QProcess shutdownChild;
    shutdownChild.setProgram(QCoreApplication::applicationFilePath());
    shutdownChild.setArguments({QStringLiteral("--shutdown-empty-session-child")});
    shutdownChild.setProcessChannelMode(QProcess::MergedChannels);
    shutdownChild.start();
    require(shutdownChild.waitForStarted(5000), "shutdown child starts");
    require(shutdownChild.waitForFinished(10000), "shutdown child finishes");
    if (shutdownChild.exitStatus() != QProcess::NormalExit || shutdownChild.exitCode() != 0)
        std::cerr << shutdownChild.readAllStandardOutput().constData() << '\n';
    require(shutdownChild.exitStatus() == QProcess::NormalExit && shutdownChild.exitCode() == 0,
            "shutdown child validates inactive session without exiting the test process");
    return 0;
}
