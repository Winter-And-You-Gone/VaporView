#include "LogService.h"
#include "SkyLocalIpcClient.h"
#include "SkyLocalIpcServer.h"
#include "SkyRuntime.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QTemporaryDir>

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
    QTemporaryDir logDirectory;
    require(logDirectory.isValid(), "temporary log directory");

    VaporView::LogService logService(QStringLiteral("SkyIpcLogTest"),
                                     &app,
                                     logDirectory.path());
    VaporView::SkyRuntimeOptions options;
    VaporView::SkyRuntime runtime(options);
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
    return 0;
}
