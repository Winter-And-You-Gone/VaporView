#include "SkyRuntime.h"
#include "SkyTuiController.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("VaporViewSky");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("VaporView");

    qRegisterMetaType<VaporView::TelemetryBasic>("VaporView::TelemetryBasic");
    qRegisterMetaType<VaporView::DownsampledWaveform>("VaporView::DownsampledWaveform");
    qRegisterMetaType<VaporView::WaveformFeature>("VaporView::WaveformFeature");
    qRegisterMetaType<VaporView::TelemetryStatus>("VaporView::TelemetryStatus");
    qRegisterMetaType<VaporView::CommandAck>("VaporView::CommandAck");
    qRegisterMetaType<VaporView::SkyDeviceId>("VaporView::SkyDeviceId");

    QCommandLineParser parser;
    parser.setApplicationDescription("VaporView Sky TUI");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption telemetryPortOption(QStringLiteral("telemetry-port"), QStringLiteral("Telemetry serial port"), QStringLiteral("port"));
    QCommandLineOption telemetryBaudOption(QStringLiteral("telemetry-baud"), QStringLiteral("Telemetry serial baud"), QStringLiteral("baud"), QStringLiteral("921600"));
    QCommandLineOption skyConfigOption(QStringLiteral("sky-config"), QStringLiteral("Sky config JSON path"), QStringLiteral("path"));
    QCommandLineOption skySimulateOption(QStringLiteral("sky-simulate-data"), QStringLiteral("Generate simulated sky data"));
    QCommandLineOption skyWaveHostOption(QStringLiteral("sky-wave-host"), QStringLiteral("Sky TCP wave host"), QStringLiteral("host"), QStringLiteral("127.0.0.1"));
    QCommandLineOption skyWavePortOption(QStringLiteral("sky-wave-port"), QStringLiteral("Sky TCP wave port"), QStringLiteral("port"), QStringLiteral("8888"));
    parser.addOptions({telemetryPortOption, telemetryBaudOption, skyConfigOption,
                       skySimulateOption, skyWaveHostOption, skyWavePortOption});
    parser.process(app);

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
    VaporView::SkyTuiController tui(&runtime, options);
    QObject::connect(&runtime, &VaporView::SkyRuntime::logMessage,
                     &tui, &VaporView::SkyTuiController::appendLog);

    if (!runtime.start())
    {
        return 1;
    }

    tui.start();
    return app.exec();
}
