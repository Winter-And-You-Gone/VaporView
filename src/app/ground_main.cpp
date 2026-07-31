#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCommandLineParser>
#include <QComboBox>
#include <QDebug>
#include <QEvent>
#include <QMainWindow>
#include <QMessageBox>
#include <QObject>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>
#include <QIcon>
#include <QDir>
#include <QFileInfo>
#include <QPalette>
#include <QSettings>
#include "shared/theme/AppTheme.h"
#include "LogService.h"
#include "ground/main/MainWindow.h"
#include "SkyRuntime.h"

namespace
{
class WheelValueChangeFilter final : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() != QEvent::Wheel)
        {
            return QObject::eventFilter(object, event);
        }

        QWidget *widget = qobject_cast<QWidget *>(object);
        while (widget)
        {
            if (auto *combo = qobject_cast<QComboBox *>(widget))
            {
                if (!combo->view()->isVisible())
                {
                    event->ignore();
                    return true;
                }
                break;
            }

            if (qobject_cast<QAbstractSpinBox *>(widget))
            {
                event->ignore();
                return true;
            }

            widget = widget->parentWidget();
        }

        return QObject::eventFilter(object, event);
    }
};

class UserIssueMessageFilter final : public QObject
{
public:
    explicit UserIssueMessageFilter(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() != QEvent::Show)
        {
            return QObject::eventFilter(object, event);
        }
        auto *messageBox = qobject_cast<QMessageBox *>(object);
        if (!messageBox)
        {
            return QObject::eventFilter(object, event);
        }

        VaporView::LogLevel level = VaporView::LogLevel::Info;
        switch (messageBox->icon())
        {
        case QMessageBox::Critical:
            level = VaporView::LogLevel::Critical;
            break;
        case QMessageBox::Warning:
            level = VaporView::LogLevel::Warning;
            break;
        case QMessageBox::Information:
            level = VaporView::LogLevel::Info;
            break;
        case QMessageBox::Question:
            return QObject::eventFilter(object, event);
        case QMessageBox::NoIcon:
            break;
        }
        VaporView::reportUserIssue(level,
                                   QStringLiteral("UI"),
                                   QStringLiteral("messagebox"),
                                   messageBox->text(),
                                   {{QStringLiteral("title"), messageBox->windowTitle()},
                                    {QStringLiteral("informative_text"), messageBox->informativeText()},
                                    {QStringLiteral("source_class"), QString::fromLatin1(messageBox->metaObject()->className())}});
        return QObject::eventFilter(object, event);
    }

};

bool startupDarkThemeEnabled()
{
    const QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
    return settings.value(QStringLiteral("dark_theme_enabled"), false).toBool();
}

void applyStartupTheme(QApplication& app, bool darkThemeEnabled)
{
    if (!darkThemeEnabled)
    {
        return;
    }

    app.setPalette(VaporView::appThemePalette(true, app.palette()));
    app.setStyleSheet(VaporView::startupAppThemeStyleSheet(true));
}

void showMainWindow(MainWindow& window)
{
    window.show();
}
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    WheelValueChangeFilter wheelValueChangeFilter;
    app.installEventFilter(&wheelValueChangeFilter);

    app.setApplicationName("VaporView");
    app.setApplicationVersion("1.0.11");
    app.setOrganizationName("VaporView");
    VaporView::LogService logService(QStringLiteral("VaporView"));
    logService.installQtMessageHandler();
    QObject::connect(&logService, &VaporView::LogService::diagnosticFailure, &app,
                     [](const QString& message) {
                         static bool shown = false;
                         if (shown)
                         {
                             return;
                         }
                         shown = true;
                         QMessageBox::warning(nullptr,
                                               QStringLiteral("日志受限"),
                                               QStringLiteral("软件诊断日志无法写入，已回退到调试输出。\n%1").arg(message));
                     });
    UserIssueMessageFilter userIssueMessageFilter(&app);
    app.installEventFilter(&userIssueMessageFilter);
    const bool startupDarkTheme = startupDarkThemeEnabled();
    applyStartupTheme(app, startupDarkTheme);

    qRegisterMetaType<VaporView::TelemetryBasic>("VaporView::TelemetryBasic");
    qRegisterMetaType<VaporView::DownsampledWaveform>("VaporView::DownsampledWaveform");
    qRegisterMetaType<VaporView::WaveformFeature>("VaporView::WaveformFeature");
    qRegisterMetaType<VaporView::DeviceStatusItem>("VaporView::DeviceStatusItem");
    qRegisterMetaType<VaporView::TelemetryStatus>("VaporView::TelemetryStatus");
    qRegisterMetaType<VaporView::CommandAck>("VaporView::CommandAck");
    qRegisterMetaType<VaporView::CommandId>("VaporView::CommandId");
    qRegisterMetaType<VaporView::SkyDeviceId>("VaporView::SkyDeviceId");

    QCommandLineParser parser;
    parser.setApplicationDescription("VaporView");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption modeOption(QStringLiteral("mode"), QStringLiteral("Run mode: ground or sky"), QStringLiteral("mode"), QStringLiteral("ground"));
    QCommandLineOption sourceOption(QStringLiteral("source"), QStringLiteral("Ground source: local or remote"), QStringLiteral("source"), QStringLiteral("local"));
    QCommandLineOption telemetryPortOption(QStringLiteral("telemetry-port"), QStringLiteral("Telemetry serial port"), QStringLiteral("port"));
    QCommandLineOption telemetryBaudOption(QStringLiteral("telemetry-baud"), QStringLiteral("Telemetry serial baud"), QStringLiteral("baud"), QStringLiteral("921600"));
    QCommandLineOption telemetryTransportOption(QStringLiteral("telemetry-transport"), QStringLiteral("Telemetry transport: tcp or serial"), QStringLiteral("transport"), QStringLiteral("tcp"));
    QCommandLineOption telemetryHostOption(QStringLiteral("telemetry-host"), QStringLiteral("Telemetry TCP listen host"), QStringLiteral("host"), QStringLiteral("0.0.0.0"));
    QCommandLineOption telemetryTcpPortOption(QStringLiteral("telemetry-tcp-port"), QStringLiteral("Telemetry TCP listen port"), QStringLiteral("port"), QStringLiteral("39100"));
    QCommandLineOption skyConfigOption(QStringLiteral("sky-config"), QStringLiteral("Sky config JSON path"), QStringLiteral("path"));
    QCommandLineOption skySimulateOption(QStringLiteral("sky-simulate-data"), QStringLiteral("Generate simulated sky data"));
    QCommandLineOption skyWaveHostOption(QStringLiteral("sky-wave-host"), QStringLiteral("Sky TCP wave host"), QStringLiteral("host"), QStringLiteral("127.0.0.1"));
    QCommandLineOption skyWavePortOption(QStringLiteral("sky-wave-port"), QStringLiteral("Sky TCP wave port"), QStringLiteral("port"), QStringLiteral("8888"));
    parser.addOptions({modeOption, sourceOption, telemetryPortOption, telemetryBaudOption,
                       telemetryTransportOption, telemetryHostOption, telemetryTcpPortOption,
                       skyConfigOption,
                       skySimulateOption, skyWaveHostOption, skyWavePortOption});
    parser.process(app);

    if (parser.value(modeOption).compare(QStringLiteral("sky"), Qt::CaseInsensitive) == 0)
    {
        VaporView::SkyRuntimeOptions options;
        VaporView::TelemetryTransportType transport = VaporView::TelemetryTransportType::Tcp;
        if (!VaporView::parseTelemetryTransport(parser.value(telemetryTransportOption), transport))
        {
            qCritical() << "--telemetry-transport must be tcp or serial";
            return 2;
        }
        if (!parser.isSet(telemetryTransportOption) && parser.isSet(telemetryPortOption))
        {
            transport = VaporView::TelemetryTransportType::Serial;
            qInfo().noquote() << "Compatibility: --telemetry-port without --telemetry-transport selects serial telemetry";
        }
        options.telemetry_transport = transport;
        options.telemetry_host = parser.value(telemetryHostOption);
        options.telemetry_tcp_port = parser.value(telemetryTcpPortOption).toInt();
        options.telemetry_port = parser.value(telemetryPortOption);
        options.telemetry_baud = parser.value(telemetryBaudOption).toInt();
        options.config_path = parser.value(skyConfigOption);
        options.simulate_data = parser.isSet(skySimulateOption);
        options.wave_host = parser.value(skyWaveHostOption);
        options.wave_port = parser.value(skyWavePortOption).toInt();
        if (options.telemetry_transport == VaporView::TelemetryTransportType::Serial &&
            options.telemetry_port.trimmed().isEmpty())
        {
            qCritical() << "--telemetry-port is required for serial telemetry in sky mode";
            return 2;
        }
        if (options.telemetry_transport == VaporView::TelemetryTransportType::Tcp &&
            (options.telemetry_tcp_port <= 0 || options.telemetry_tcp_port > 65535))
        {
            qCritical() << "--telemetry-tcp-port must be 1-65535";
            return 2;
        }
        qInfo().noquote() << "Running sky in background mode. For split sky mode use VaporViewSkyCore.exe and VaporViewSkyTui.exe.";
        VaporView::SkyRuntime runtime(options);
        if (!runtime.start())
        {
            return 1;
        }
        return app.exec();
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString logoFileName = startupDarkTheme
        ? QStringLiteral("VaporViewLOGO_rgb217_119_87.svg")
        : QStringLiteral("VaporViewLOGO_black.svg");
    const QString logoRelativePath = QStringLiteral("resources/VaproViewLOGO/%1").arg(logoFileName);
    const QStringList iconCandidates = {
        QDir(appDir).filePath(logoRelativePath),
        QDir(appDir).filePath(QStringLiteral("../") + logoRelativePath),
        QDir(appDir).filePath(QStringLiteral("../../") + logoRelativePath)
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
    showMainWindow(mainWindow);

    return app.exec();
}

