#include "SkyLocalIpcClient.h"
#include "LogService.h"
#include "SkyStartupScreen.h"
#include "SkyTuiApp.h"
#include "SkyTuiOptions.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>

namespace
{
void registerTelemetryMetaTypes()
{
    qRegisterMetaType<VaporView::TelemetryBasic>("VaporView::TelemetryBasic");
    qRegisterMetaType<VaporView::DownsampledWaveform>("VaporView::DownsampledWaveform");
    qRegisterMetaType<VaporView::WaveformFeature>("VaporView::WaveformFeature");
    qRegisterMetaType<VaporView::DeviceStatusItem>("VaporView::DeviceStatusItem");
    qRegisterMetaType<VaporView::TelemetryStatus>("VaporView::TelemetryStatus");
    qRegisterMetaType<VaporView::CommandAck>("VaporView::CommandAck");
    qRegisterMetaType<VaporView::CommandId>("VaporView::CommandId");
    qRegisterMetaType<VaporView::SkyDeviceId>("VaporView::SkyDeviceId");
}

void applyConnectValue(const QString& value, VaporView::SkyTuiOptions& options)
{
    const int colon = value.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0 || colon == value.size() - 1)
    {
        return;
    }
    options.ipc_host = value.left(colon);
    options.ipc_port = value.mid(colon + 1).toInt();
}

}  // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("VaporViewSkyTui");
    app.setApplicationVersion("1.0.22");
    app.setOrganizationName("VaporView");
    VaporView::LogService logService(QStringLiteral("VaporViewSkyTui"));
    logService.installQtMessageHandler();
    registerTelemetryMetaTypes();

    QCommandLineParser parser;
    parser.setApplicationDescription("VaporView Sky TUI client");

    QCommandLineOption helpOption(QStringList{QStringLiteral("h"), QStringLiteral("help")}, QStringLiteral("Displays help on commandline options."));
    QCommandLineOption versionOption(QStringLiteral("version"), QStringLiteral("Displays version information."));
    QCommandLineOption connectOption(QStringLiteral("connect"), QStringLiteral("Connect to SkyCore host:port"), QStringLiteral("host:port"));
    QCommandLineOption ipcHostOption(QStringLiteral("ipc-host"), QStringLiteral("Local IPC host"), QStringLiteral("host"), QStringLiteral("127.0.0.1"));
    QCommandLineOption ipcPortOption(QStringLiteral("ipc-port"), QStringLiteral("Local IPC port"), QStringLiteral("port"), QStringLiteral("39001"));
    QCommandLineOption autoLaunchCoreOption(QStringLiteral("auto-launch-core"), QStringLiteral("Reserved: auto-launch SkyCore before connecting"));
    QCommandLineOption corePathOption(QStringLiteral("core-path"), QStringLiteral("SkyCore executable path"), QStringLiteral("path"));
    QCommandLineOption shutdownCoreOption(QStringLiteral("shutdown-core"), QStringLiteral("Send a local IPC request to stop SkyCore and exit"));
    parser.addOptions({helpOption, versionOption, connectOption, ipcHostOption, ipcPortOption, autoLaunchCoreOption, corePathOption, shutdownCoreOption});
    parser.process(app);

    if (parser.isSet(helpOption))
    {
        QTextStream(stdout) << parser.helpText();
        return 0;
    }
    if (parser.isSet(versionOption))
    {
        QTextStream(stdout) << app.applicationName() << " " << app.applicationVersion() << "\n";
        return 0;
    }

    VaporView::SkyTuiOptions options;
    options.ipc_host = parser.value(ipcHostOption);
    options.ipc_port = parser.value(ipcPortOption).toInt();
    options.auto_launch_core = parser.isSet(autoLaunchCoreOption);
    options.core_path = parser.value(corePathOption);
    if (parser.isSet(connectOption))
    {
        applyConnectValue(parser.value(connectOption), options);
    }

    if (parser.isSet(shutdownCoreOption))
    {
        VaporView::SkyLocalIpcClient client;
        QObject::connect(&client, &VaporView::SkyLocalIpcClient::logRecordGenerated,
                         &logService, [&logService](const VaporView::LogRecord& record) {
            logService.publish(record);
            QTextStream(stderr) << record.message << "\n";
        });
        QObject::connect(&client, &VaporView::SkyLocalIpcClient::connectedChanged, &app, [&client](bool connected) {
            if (connected)
            {
                QTextStream(stdout) << "正在发送 SkyCore 停止请求。\n";
                client.requestCoreShutdown();
            }
        });
        QObject::connect(&client, &VaporView::SkyLocalIpcClient::ackReceived, &app, [&app](const VaporView::CommandAck& ack) {
            if (ack.command_id == VaporView::CommandId::ShutdownCore)
            {
                QTextStream(stdout) << (ack.error_code == VaporView::CommandErrorCode::Ok
                    ? QStringLiteral("SkyCore 已接受停止请求。\n")
                    : QStringLiteral("SkyCore 已拒绝停止请求。\n"));
                QTimer::singleShot(300, &app, &QCoreApplication::quit);
            }
        });
        QTimer::singleShot(5000, &app, [&app]() {
            QTextStream(stderr) << "等待 SkyCore 停止确认超时。\n";
            app.exit(2);
        });
        client.connectToCore(options.ipc_host, static_cast<quint16>(options.ipc_port));
        return app.exec();
    }

    if (VaporView::showSkyStartupScreen() == VaporView::SkyStartupDecision::Exit)
    {
        return 0;
    }

    VaporView::SkyLocalIpcClient client;
    client.setAutoReconnectEnabled(true);
    VaporView::SkyTuiApp tui(&client, options);
    tui.start();
    if (options.auto_launch_core)
    {
        tui.appendLog(QStringLiteral("--auto-launch-core 已保留但尚未启用；请先手动启动 VaporViewSkyCore。"));
    }
    client.connectToCore(options.ipc_host, static_cast<quint16>(options.ipc_port));

    return app.exec();
}
