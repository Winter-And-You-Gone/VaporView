#include "SkyLocalIpcServer.h"
#include "SkyRuntime.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QMetaObject>
#include <QTextStream>
#include <csignal>

namespace
{
QCoreApplication *g_app = nullptr;

void handleProcessSignal(int)
{
    if (g_app)
    {
        QMetaObject::invokeMethod(g_app, []() {
            QCoreApplication::quit();
        }, Qt::QueuedConnection);
    }
}

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
    g_app = &app;
    std::signal(SIGINT, handleProcessSignal);
    std::signal(SIGTERM, handleProcessSignal);

    app.setApplicationName("VaporViewSkyCore");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("VaporView");
    registerTelemetryMetaTypes();

    QCommandLineParser parser;
    parser.setApplicationDescription("VaporView SkyCore");

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
    QCommandLineOption profileOption(QStringLiteral("profile"), QStringLiteral("Runtime profile: flight or debug"), QStringLiteral("profile"), QStringLiteral("debug"));
    QCommandLineOption noIpcOption(QStringLiteral("no-ipc"), QStringLiteral("Disable local IPC server"));
    parser.addOptions({helpOption, versionOption, telemetryPortOption, telemetryBaudOption, skyConfigOption, skySimulateOption,
                       skyWaveHostOption, skyWavePortOption, ipcHostOption, ipcPortOption,
                       profileOption, noIpcOption});
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

    VaporView::SkyRuntimeOptions options;
    options.telemetry_port = parser.value(telemetryPortOption);
    options.telemetry_baud = parser.value(telemetryBaudOption).toInt();
    options.config_path = parser.value(skyConfigOption);
    options.simulate_data = parser.isSet(skySimulateOption);
    options.wave_host = parser.value(skyWaveHostOption);
    options.wave_port = parser.value(skyWavePortOption).toInt();

    if (options.telemetry_port.trimmed().isEmpty())
    {
        QTextStream(stderr) << "--telemetry-port is required\n";
        return 2;
    }

    VaporView::SkyRuntime runtime(options);
    QObject::connect(&runtime, &VaporView::SkyRuntime::logMessage, [](const QString& message) {
        QTextStream(stdout) << message << "\n";
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&runtime]() {
        runtime.stop();
    });

    VaporView::SkyLocalIpcServer ipcServer(&runtime);
    QObject::connect(&ipcServer, &VaporView::SkyLocalIpcServer::logMessage, [](const QString& message) {
        QTextStream(stdout) << message << "\n";
    });

    if (!parser.isSet(noIpcOption))
    {
        if (!ipcServer.listen(parser.value(ipcHostOption), static_cast<quint16>(parser.value(ipcPortOption).toUShort())))
        {
            return 4;
        }
    }

    QTextStream(stdout) << "SkyCore profile: " << parser.value(profileOption) << "\n";
    if (!runtime.start())
    {
        return 3;
    }

    return app.exec();
}
