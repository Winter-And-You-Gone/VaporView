#include "SkyLocalIpcClient.h"
#include "SkyLocalIpcServer.h"
#include "SkyRuntime.h"
#include "SkyStartupScreen.h"
#include "SkyTuiApp.h"
#include "SkyTuiOptions.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

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

}  // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("VaporViewSky");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("VaporView");
    registerTelemetryMetaTypes();

    QCommandLineParser parser;
    parser.setApplicationDescription("VaporView Sky compatibility TUI");

    QCommandLineOption helpOption(QStringList{QStringLiteral("h"), QStringLiteral("help")}, QStringLiteral("Displays help on commandline options."));
    QCommandLineOption versionOption(QStringLiteral("version"), QStringLiteral("Displays version information."));
    QCommandLineOption telemetryPortOption(QStringLiteral("telemetry-port"), QStringLiteral("Telemetry serial port"), QStringLiteral("port"));
    QCommandLineOption telemetryBaudOption(QStringLiteral("telemetry-baud"), QStringLiteral("Telemetry serial baud"), QStringLiteral("baud"), QStringLiteral("921600"));
    QCommandLineOption skyConfigOption(QStringLiteral("sky-config"), QStringLiteral("Sky config JSON path"), QStringLiteral("path"));
    QCommandLineOption skySimulateOption(QStringLiteral("sky-simulate-data"), QStringLiteral("Generate simulated sky data"));
    QCommandLineOption skyWaveHostOption(QStringLiteral("sky-wave-host"), QStringLiteral("Sky TCP wave host"), QStringLiteral("host"), QStringLiteral("127.0.0.1"));
    QCommandLineOption skyWavePortOption(QStringLiteral("sky-wave-port"), QStringLiteral("Sky TCP wave port"), QStringLiteral("port"), QStringLiteral("8888"));
    QCommandLineOption ipcHostOption(QStringLiteral("ipc-host"), QStringLiteral("Local IPC host"), QStringLiteral("host"), QStringLiteral("127.0.0.1"));
    QCommandLineOption ipcPortOption(QStringLiteral("ipc-port"), QStringLiteral("Local IPC port"), QStringLiteral("port"), QStringLiteral("39001"));
    parser.addOptions({helpOption, versionOption, telemetryPortOption, telemetryBaudOption, skyConfigOption, skySimulateOption,
                       skyWaveHostOption, skyWavePortOption, ipcHostOption, ipcPortOption});
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

    VaporView::SkyRuntimeOptions runtimeOptions;
    runtimeOptions.telemetry_port = parser.value(telemetryPortOption);
    runtimeOptions.telemetry_baud = parser.value(telemetryBaudOption).toInt();
    runtimeOptions.config_path = parser.value(skyConfigOption);
    runtimeOptions.simulate_data = parser.isSet(skySimulateOption);
    runtimeOptions.wave_host = parser.value(skyWaveHostOption);
    runtimeOptions.wave_port = parser.value(skyWavePortOption).toInt();

    if (runtimeOptions.telemetry_port.trimmed().isEmpty())
    {
        QTextStream(stderr) << "--telemetry-port is required\n";
        return 2;
    }

    if (VaporView::showSkyStartupScreen() == VaporView::SkyStartupDecision::Exit)
    {
        return 0;
    }

    VaporView::SkyRuntime runtime(runtimeOptions);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&runtime]() {
        runtime.stop();
    });

    VaporView::SkyLocalIpcServer ipcServer(&runtime);
    const QString ipcHost = parser.value(ipcHostOption);
    const auto ipcPort = static_cast<quint16>(parser.value(ipcPortOption).toUShort());
    if (!ipcServer.listen(ipcHost, ipcPort))
    {
        return 4;
    }

    VaporView::SkyTuiOptions tuiOptions;
    tuiOptions.ipc_host = ipcHost;
    tuiOptions.ipc_port = ipcPort;
    tuiOptions.quit_leaves_core = false;

    VaporView::SkyLocalIpcClient client;
    client.setAutoReconnectEnabled(true);
    VaporView::SkyTuiApp tui(&client, tuiOptions);
    QObject::connect(&runtime, &VaporView::SkyRuntime::logMessage, &tui, &VaporView::SkyTuiApp::appendLog);
    QObject::connect(&ipcServer, &VaporView::SkyLocalIpcServer::logMessage, &tui, &VaporView::SkyTuiApp::appendLog);

    tui.start();
    if (!runtime.start())
    {
        tui.appendLog(QStringLiteral("天空端启动失败：数传串口未打开。请检查端口是否存在、是否被占用，COM10 及以上可尝试 \\\\.\\COMxx。"));
        tui.appendLog(QStringLiteral("TUI 将保持打开，可输入 /status 查看状态，或输入 quit 退出。"));
    }
    client.connectToCore(tuiOptions.ipc_host, tuiOptions.ipc_port);

    return app.exec();
}
