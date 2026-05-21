#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QMainWindow>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>
#include <QIcon>
#include <QDir>
#include <QFileInfo>
#include <QPalette>
#include <QSettings>
#include "MainWindow.h"
#include "SkyRuntime.h"

namespace
{
QPalette startupDarkPalette(const QPalette& base)
{
    QPalette palette = base;
    palette.setColor(QPalette::Window, QColor("#101418"));
    palette.setColor(QPalette::WindowText, QColor("#d8dee9"));
    palette.setColor(QPalette::Base, QColor("#10151b"));
    palette.setColor(QPalette::AlternateBase, QColor("#162638"));
    palette.setColor(QPalette::Text, QColor("#e5e7eb"));
    palette.setColor(QPalette::Button, QColor("#151a20"));
    palette.setColor(QPalette::ButtonText, QColor("#e5e7eb"));
    palette.setColor(QPalette::BrightText, QColor("#ffffff"));
    palette.setColor(QPalette::Light, QColor("#475569"));
    palette.setColor(QPalette::Midlight, QColor("#334155"));
    palette.setColor(QPalette::Mid, QColor("#2c3440"));
    palette.setColor(QPalette::Dark, QColor("#111827"));
    palette.setColor(QPalette::Shadow, QColor("#05070a"));
    palette.setColor(QPalette::Highlight, QColor("#245b8f"));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    palette.setColor(QPalette::ToolTipBase, QColor("#1b222b"));
    palette.setColor(QPalette::ToolTipText, QColor("#e5e7eb"));
    palette.setColor(QPalette::Link, QColor("#7db7ff"));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#64748b"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#64748b"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#94a3b8"));
    return palette;
}

QString startupDarkStyleSheet()
{
    return QStringLiteral(
        "QWidget, QMainWindow { background-color: #101418; color: #d8dee9; }"
        "QMenuBar, QToolBar, QStatusBar { background-color: #151a20; color: #d8dee9; }"
    );
}

void applyStartupTheme(QApplication& app)
{
    const QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
    if (!settings.value(QStringLiteral("dark_theme_enabled"), false).toBool())
    {
        return;
    }

    app.setPalette(startupDarkPalette(app.palette()));
    app.setStyleSheet(startupDarkStyleSheet());
}
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("VaporView");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("VaporView");
    applyStartupTheme(app);

    qRegisterMetaType<VaporView::TelemetryBasic>("VaporView::TelemetryBasic");
    qRegisterMetaType<VaporView::DownsampledWaveform>("VaporView::DownsampledWaveform");
    qRegisterMetaType<VaporView::WaveformFeature>("VaporView::WaveformFeature");
    qRegisterMetaType<VaporView::TelemetryStatus>("VaporView::TelemetryStatus");
    qRegisterMetaType<VaporView::CommandAck>("VaporView::CommandAck");
    qRegisterMetaType<VaporView::SkyDeviceId>("VaporView::SkyDeviceId");

    QCommandLineParser parser;
    parser.setApplicationDescription("VaporView");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption modeOption(QStringLiteral("mode"), QStringLiteral("Run mode: ground or sky"), QStringLiteral("mode"), QStringLiteral("ground"));
    QCommandLineOption sourceOption(QStringLiteral("source"), QStringLiteral("Ground source: local or remote"), QStringLiteral("source"), QStringLiteral("local"));
    QCommandLineOption telemetryPortOption(QStringLiteral("telemetry-port"), QStringLiteral("Telemetry serial port"), QStringLiteral("port"));
    QCommandLineOption telemetryBaudOption(QStringLiteral("telemetry-baud"), QStringLiteral("Telemetry serial baud"), QStringLiteral("baud"), QStringLiteral("921600"));
    QCommandLineOption skyConfigOption(QStringLiteral("sky-config"), QStringLiteral("Sky config JSON path"), QStringLiteral("path"));
    QCommandLineOption skySimulateOption(QStringLiteral("sky-simulate-data"), QStringLiteral("Generate simulated sky data"));
    QCommandLineOption skyWaveHostOption(QStringLiteral("sky-wave-host"), QStringLiteral("Sky TCP wave host"), QStringLiteral("host"), QStringLiteral("127.0.0.1"));
    QCommandLineOption skyWavePortOption(QStringLiteral("sky-wave-port"), QStringLiteral("Sky TCP wave port"), QStringLiteral("port"), QStringLiteral("8888"));
    parser.addOptions({modeOption, sourceOption, telemetryPortOption, telemetryBaudOption, skyConfigOption,
                       skySimulateOption, skyWaveHostOption, skyWavePortOption});
    parser.process(app);

    if (parser.value(modeOption).compare(QStringLiteral("sky"), Qt::CaseInsensitive) == 0)
    {
        VaporView::SkyRuntimeOptions options;
        options.telemetry_port = parser.value(telemetryPortOption);
        options.telemetry_baud = parser.value(telemetryBaudOption).toInt();
        options.config_path = parser.value(skyConfigOption);
        options.simulate_data = parser.isSet(skySimulateOption);
        options.wave_host = parser.value(skyWaveHostOption);
        options.wave_port = parser.value(skyWavePortOption).toInt();
        if (options.telemetry_port.trimmed().isEmpty())
        {
            qCritical() << "--telemetry-port is required in sky mode";
            return 2;
        }
        qInfo().noquote() << "Running sky in background mode. For interactive TUI use VaporViewSky.exe.";
        VaporView::SkyRuntime runtime(options);
        QObject::connect(&runtime, &VaporView::SkyRuntime::logMessage, [](const QString& message) {
            qInfo().noquote() << message;
        });
        if (!runtime.start())
        {
            return 1;
        }
        return app.exec();
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList iconCandidates = {
        QDir(appDir).filePath("resources/app.ico"),
        QDir(appDir).filePath("../resources/app.ico"),
        QDir(appDir).filePath("../../resources/app.ico")
    };

    QIcon windowIcon;
    for (const QString& path : iconCandidates)
    {
        if (QFileInfo::exists(path))
        {
            windowIcon = QIcon(path);
            if (!windowIcon.isNull())
            {
                break;
            }
        }
    }
    if (!windowIcon.isNull())
    {
        app.setWindowIcon(windowIcon);
    }

    MainWindow mainWindow;
    mainWindow.setWindowTitle("VaporView");
    if (!app.windowIcon().isNull())
    {
        mainWindow.setWindowIcon(app.windowIcon());
    }
    mainWindow.show();

    return app.exec();
}

