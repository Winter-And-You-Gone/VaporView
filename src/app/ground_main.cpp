#include <QAbstractAnimation>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCommandLineParser>
#include <QComboBox>
#include <QDebug>
#include <QDateTime>
#include <QEasingCurve>
#include <QEvent>
#include <QEventLoop>
#include <QMainWindow>
#include <QMessageBox>
#include <QObject>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>
#include <QIcon>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QSettings>
#include <QStackedWidget>
#include <QTimer>
#include <QVariant>
#include "shared/theme/AppTheme.h"
#include "LogService.h"
#include "TelemetryCodec.h"
#include "app/LifecycleBreadcrumb.h"
#include "app/StartupSplash.h"
#include "ground/devices/RemoteSkyController.h"
#include "ground/main/MainWindow.h"
#include "SkyRuntime.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string_view>

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

void configureSettingsDirectoryFromEnvironment()
{
    const QString settingsDirectory = qEnvironmentVariable("VAPORVIEW_SETTINGS_DIR").trimmed();
    if (settingsDirectory.isEmpty())
    {
        return;
    }

    QDir directory(settingsDirectory);
    directory.mkpath(QStringLiteral("."));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.absolutePath());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, directory.absolutePath());
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

constexpr qint64 kMinimumSplashVisibleMs = 600;
constexpr int kWindowTransitionDurationMs = 200;

void fadeInMainWindow(MainWindow& window)
{
    window.setWindowOpacity(0.0);
    window.show();

    auto *mainWindowFade = new QPropertyAnimation(&window, "windowOpacity", &window);
    mainWindowFade->setDuration(kWindowTransitionDurationMs);
    mainWindowFade->setStartValue(0.0);
    mainWindowFade->setEndValue(1.0);
    mainWindowFade->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(mainWindowFade, &QPropertyAnimation::finished, &window, [&window]() {
        window.setWindowOpacity(1.0);
        window.raise();
        window.activateWindow();
    });

    mainWindowFade->start(QAbstractAnimation::DeleteWhenStopped);
}

void showMainWindow(MainWindow& window, VaporView::StartupSplash *splash)
{
    if (splash)
    {
        QObject::connect(splash, &VaporView::StartupSplash::fadeOutFinished,
                         &window, [splash, &window]() {
                             splash->deleteLater();
                             fadeInMainWindow(window);
                         });
        splash->fadeOutAndClose(kWindowTransitionDurationMs);
        return;
    }

    fadeInMainWindow(window);
}

void startAi8RemoteE2e(QApplication& app, MainWindow& window, const QString& outputPath)
{
    auto *pollTimer = new QTimer(&window);
    pollTimer->setInterval(50);
    auto *step = new int(0);
    auto *startedMs = new qint64(QDateTime::currentMSecsSinceEpoch());
    auto finish = [&app, outputPath, pollTimer, step, startedMs](bool success, const QString& detail) {
        QFile output(outputPath);
        if (output.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            output.write(success ? QByteArrayLiteral("PASS\n") : QByteArrayLiteral("FAIL\n"));
            output.write(detail.toUtf8());
            output.write("\n");
        }
        pollTimer->stop();
        delete step;
        delete startedMs;
        QTimer::singleShot(0, &app, [&app, success]() { app.exit(success ? 0 : 1); });
    };
    QObject::connect(pollTimer, &QTimer::timeout, &window,
                     [&app, &window, pollTimer, step, startedMs, finish]() mutable {
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - *startedMs;
        if (elapsed > 30000)
        {
            const auto *readButton = window.findChild<QPushButton *>(
                QStringLiteral("ai8ReadParametersButton"));
            const auto *writeButton = window.findChild<QPushButton *>(
                QStringLiteral("ai8WriteParametersButton"));
            const auto *status = window.findChild<QLabel *>(QStringLiteral("ai8ProtocolStatus"));
            finish(false,
                   QStringLiteral("AI-8 remote UI E2E timed out at step %1; read=%2; write=%3; status=%4")
                       .arg(*step)
                       .arg(readButton && readButton->isEnabled())
                       .arg(writeButton && writeButton->isEnabled())
                       .arg(status ? status->text() : QStringLiteral("<missing>")));
            return;
        }
        if (*step == 0)
        {
            if (!QMetaObject::invokeMethod(&window, "onConnectClicked", Qt::DirectConnection))
            {
                finish(false, QStringLiteral("MainWindow connect action is unavailable"));
                return;
            }
            *step = 1;
            return;
        }
        auto *panel = window.findChild<QWidget *>(QStringLiteral("ai8TemperatureControllerPanel"));
        auto *readButton = window.findChild<QPushButton *>(QStringLiteral("ai8ReadParametersButton"));
        auto *writeButton = window.findChild<QPushButton *>(QStringLiteral("ai8WriteParametersButton"));
        auto *status = window.findChild<QLabel *>(QStringLiteral("ai8ProtocolStatus"));
        if (!panel || !readButton || !writeButton || !status)
        {
            finish(false, QStringLiteral("AI-8 parameter page controls are unavailable"));
            return;
        }
        const QString statusText = status->text();
        if (*step == 1 && readButton->isEnabled())
        {
            readButton->click();
            *step = 2;
            return;
        }
        if (*step == 2 && (statusText.contains(QStringLiteral("读取完成")) ||
                           statusText.contains(QStringLiteral("Parameters were read"))))
        {
            auto *setpoint = panel->findChild<QDoubleSpinBox *>(QStringLiteral("ai8SetpointSpin"));
            if (!setpoint || std::fabs(setpoint->value() - 25.0) > 0.001)
            {
                finish(false, QStringLiteral("AI-8 remote read did not load the simulated default"));
                return;
            }
            setpoint->setValue(36.5);
            if (!writeButton->isEnabled())
            {
                finish(false, QStringLiteral("AI-8 write action stayed disabled after read"));
                return;
            }
            writeButton->click();
            *step = 3;
            return;
        }
        if (*step == 3 && (statusText.contains(QStringLiteral("回读确认")) ||
                           statusText.contains(QStringLiteral("confirmed by read-back"))))
        {
            auto *setpoint = panel->findChild<QDoubleSpinBox *>(QStringLiteral("ai8SetpointSpin"));
            if (!setpoint || std::fabs(setpoint->value() - 36.5) > 0.001)
            {
                finish(false, QStringLiteral("AI-8 remote write was not confirmed in the shared UI"));
                return;
            }
            finish(true, QStringLiteral("AI-8 remote read/write/read-back passed"));
        }
    });
    QTimer::singleShot(1200, pollTimer, [pollTimer]() { pollTimer->start(); });
}

struct RemoteDeviceE2eState
{
    int step = 0;
    qint64 startedMs = QDateTime::currentMSecsSinceEpoch();
    quint32 pendingDeviceRequest = 0;
    quint16 pendingCommandSequence = 0;
    bool deviceConfigAutoLoaded = false;
    VaporView::TelemetryStatus lastStatus;
    VaporView::TemperatureControllerData lastTemperatureController;
    QHash<quint32, VaporView::DeviceOperationResponse> deviceResponses;
    QHash<quint16, VaporView::CommandAck> commandAcks;
};

void startRemoteDeviceE2e(QApplication& app, MainWindow& window, const QString& outputPath)
{
    using VaporView::Ground::Devices::RemoteSkyController;

    auto *controllerObject = window.property("remoteSkyController").value<QObject *>();
    auto *controller = qobject_cast<RemoteSkyController *>(controllerObject);
    auto *pollTimer = new QTimer(&window);
    pollTimer->setInterval(50);
    auto state = std::make_shared<RemoteDeviceE2eState>();
    auto finish = [&app, outputPath, pollTimer, state](bool success, const QString& detail) {
        QFile output(outputPath);
        if (output.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            output.write(success ? QByteArrayLiteral("PASS\n") : QByteArrayLiteral("FAIL\n"));
            output.write(detail.toUtf8());
            output.write("\n");
        }
        pollTimer->stop();
        QTimer::singleShot(0, &app, [&app, success]() { app.exit(success ? 0 : 1); });
    };
    if (!controller)
    {
        finish(false, QStringLiteral("RemoteSkyController test handle is unavailable"));
        return;
    }

    QObject::connect(controller, &RemoteSkyController::deviceOperationResponseReceived,
                     &window, [state](const VaporView::DeviceOperationResponse& response) {
        state->deviceResponses.insert(response.request_id, response);
    });
    QObject::connect(controller, &RemoteSkyController::commandAckReceived,
                     &window, [state](const VaporView::CommandAck& ack) {
        state->commandAcks.insert(ack.command_seq, ack);
    });
    QObject::connect(controller, &RemoteSkyController::statusUpdated,
                     &window, [state](const VaporView::TelemetryStatus& status) {
        state->lastStatus = status;
    });
    QObject::connect(controller, &RemoteSkyController::temperatureControllerStatusUpdated,
                     &window, [state](const VaporView::TemperatureControllerData& data) {
        state->lastTemperatureController = data;
    });

    auto requireDeviceResponse =
        [state, finish](quint32 requestId, const char *label) -> bool {
            if (!state->deviceResponses.contains(requestId))
            {
                return false;
            }
            const VaporView::DeviceOperationResponse response =
                state->deviceResponses.take(requestId);
            if (response.error_code != VaporView::CommandErrorCode::Ok)
            {
                finish(false, QStringLiteral("%1 failed with %2")
                                .arg(QString::fromLatin1(label),
                                     VaporView::commandErrorCodeText(response.error_code)));
                return false;
            }
            return true;
        };
    auto requireCommandAck =
        [state, finish](quint16 sequence, const char *label) -> bool {
            if (!state->commandAcks.contains(sequence))
            {
                return false;
            }
            const VaporView::CommandAck ack = state->commandAcks.take(sequence);
            if (ack.error_code != VaporView::CommandErrorCode::Ok || ack.result != 0)
            {
                finish(false, QStringLiteral("%1 failed with %2")
                                .arg(QString::fromLatin1(label),
                                     VaporView::commandErrorCodeText(ack.error_code)));
                return false;
            }
            return true;
        };

    QObject::connect(pollTimer, &QTimer::timeout, &window,
                     [&window,
                      controller,
                      state,
                      finish,
                      requireDeviceResponse,
                      requireCommandAck]() mutable {
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - state->startedMs;
        if (elapsed > 45000)
        {
            finish(false, QStringLiteral("Remote device UI E2E timed out at step %1; open=%2; support=%3; config_auto_loaded=%4; pending_request=%5; pending_seq=%6; ack_seen=%7; temp_valid=%8; temp_target=%9; rtcm_bytes=%10")
                            .arg(state->step)
                            .arg(controller->isOpen())
                            .arg(static_cast<int>(controller->deviceOperationSupport()))
                            .arg(state->deviceConfigAutoLoaded)
                            .arg(state->pendingDeviceRequest)
                            .arg(state->pendingCommandSequence)
                            .arg(state->commandAcks.contains(state->pendingCommandSequence))
                            .arg(state->lastTemperatureController.valid)
                            .arg(state->lastTemperatureController.channels[0].target_temperature_c)
                            .arg(state->lastStatus.rtcm_correction_bytes_received));
            return;
        }

        if (state->step == 0)
        {
            auto *pageStack = window.findChild<QStackedWidget *>(QStringLiteral("mainPageStack"));
            auto *deviceConfigPage = window.findChild<QWidget *>(QStringLiteral("deviceConfigPage"));
            auto *statusLabel = window.findChild<QLabel *>(QStringLiteral("deviceRemoteSkyConfigStatus"));
            auto *epsilonPort = window.findChild<QComboBox *>(QStringLiteral("deviceEpsilonPortCombo"));
            auto *epsilonBaud = window.findChild<QComboBox *>(QStringLiteral("deviceEpsilonBaudCombo"));
            auto *ai8Port = window.findChild<QComboBox *>(QStringLiteral("deviceAi8TemperaturePortCombo"));
            auto *ai8Baud = window.findChild<QComboBox *>(QStringLiteral("deviceAi8TemperatureBaudCombo"));
            auto *ai8Rate = window.findChild<QComboBox *>(QStringLiteral("deviceAi8TemperatureRateCombo"));
            if (!pageStack || !deviceConfigPage || !statusLabel ||
                !epsilonPort || !epsilonBaud ||
                !ai8Port || !ai8Baud || !ai8Rate)
            {
                finish(false, QStringLiteral("Remote Device Config widgets are unavailable"));
                return;
            }
            pageStack->setCurrentWidget(deviceConfigPage);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            if (epsilonPort->isEnabled() || epsilonBaud->isEnabled() ||
                ai8Port->isEnabled() || ai8Baud->isEnabled() || ai8Rate->isEnabled())
            {
                finish(false, QStringLiteral("Remote Device Config fields were editable before SkyConfig was loaded"));
                return;
            }
            if (statusLabel->text().trimmed().isEmpty())
            {
                finish(false, QStringLiteral("Remote Device Config did not explain the unloaded SkyConfig state"));
                return;
            }
            if (!QMetaObject::invokeMethod(&window, "onConnectClicked", Qt::DirectConnection))
            {
                finish(false, QStringLiteral("MainWindow connect action is unavailable"));
                return;
            }
            state->step = 1;
            return;
        }

        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (state->step == 1)
        {
            auto *statusLabel = window.findChild<QLabel *>(QStringLiteral("deviceRemoteSkyConfigStatus"));
            auto *epsilonPort = window.findChild<QComboBox *>(QStringLiteral("deviceEpsilonPortCombo"));
            auto *epsilonBaud = window.findChild<QComboBox *>(QStringLiteral("deviceEpsilonBaudCombo"));
            auto *ai8Port = window.findChild<QComboBox *>(QStringLiteral("deviceAi8TemperaturePortCombo"));
            auto *ai8Baud = window.findChild<QComboBox *>(QStringLiteral("deviceAi8TemperatureBaudCombo"));
            auto *ai8Rate = window.findChild<QComboBox *>(QStringLiteral("deviceAi8TemperatureRateCombo"));
            const bool configAutoLoaded =
                statusLabel &&
                statusLabel->property("status").toString() == QStringLiteral("success") &&
                epsilonPort && epsilonPort->isEnabled() &&
                epsilonBaud && epsilonBaud->isEnabled() &&
                ai8Port && ai8Port->isEnabled() &&
                ai8Baud && ai8Baud->isEnabled() &&
                ai8Rate && ai8Rate->isEnabled() &&
                !epsilonPort->currentText().trimmed().isEmpty() &&
                !ai8Port->currentText().trimmed().isEmpty();
            state->deviceConfigAutoLoaded = state->deviceConfigAutoLoaded || configAutoLoaded;
            if (!controller->isOpen() ||
                !configAutoLoaded ||
                !controller->statusFresh(nowMs) ||
                controller->deviceState(VaporView::SkyDeviceId::Epsilon) != VaporView::DeviceState::Connected ||
                controller->deviceState(VaporView::SkyDeviceId::TemperatureController) != VaporView::DeviceState::Connected ||
                controller->deviceState(VaporView::SkyDeviceId::Ai8TemperatureController) != VaporView::DeviceState::Connected)
            {
                return;
            }
            state->step = 2;
        }

        if (state->step == 2)
        {
            VaporView::EpsilonPacketRatesOperation operation;
            operation.output_rate_hz = 100;
            operation.callback_rate_hz = 250;
            operation.packet_rates = {{0x40, 250}, {0x50, 100}, {0x5C, 10}};
            operation.packet_rate_signature = QStringLiteral("40=250;50=100;5C=10");
            state->pendingDeviceRequest = controller->configureEpsilonPacketRates(operation);
            if (state->pendingDeviceRequest == 0)
            {
                finish(false, QStringLiteral("EPSILON packet-rate request was not sent"));
                return;
            }
            state->step = 3;
            return;
        }
        if (state->step == 3)
        {
            if (!requireDeviceResponse(state->pendingDeviceRequest, "EPSILON packet-rate operation"))
            {
                return;
            }
            VaporView::EpsilonMainAntennaLeverArmOperation operation;
            operation.x_m = 1.25;
            operation.y_m = -0.5;
            operation.z_m = 0.75;
            state->pendingDeviceRequest = controller->configureEpsilonMainAntennaLeverArm(operation);
            if (state->pendingDeviceRequest == 0)
            {
                finish(false, QStringLiteral("EPSILON lever-arm request was not sent"));
                return;
            }
            state->step = 4;
            return;
        }
        if (state->step == 4)
        {
            if (!requireDeviceResponse(state->pendingDeviceRequest, "EPSILON lever-arm operation"))
            {
                return;
            }
            VaporView::EpsilonRtcmInputOperation operation;
            operation.device_port_index = 3;
            operation.forward_port = QStringLiteral("/dev/ui-e2e-rtcm");
            operation.forward_baud = 230400;
            state->pendingDeviceRequest = controller->configureEpsilonRtcmInput(operation);
            if (state->pendingDeviceRequest == 0)
            {
                finish(false, QStringLiteral("EPSILON RTCM request was not sent"));
                return;
            }
            state->step = 5;
            return;
        }
        if (state->step == 5)
        {
            if (!requireDeviceResponse(state->pendingDeviceRequest, "EPSILON RTCM operation"))
            {
                return;
            }
            const QByteArray rtcmBytes = QByteArray::fromHex("D30000123456");
            if (!controller->sendRtcmCorrectionData(rtcmBytes))
            {
                finish(false, QStringLiteral("RTCM correction uplink was rejected"));
                return;
            }
            state->step = 6;
            return;
        }
        if (state->step == 6)
        {
            if (state->lastStatus.rtcm_correction_bytes_received < 6 ||
                state->lastStatus.rtcm_correction_chunks_received == 0 ||
                state->lastStatus.rtcm_correction_last_receive_time_us == 0)
            {
                return;
            }
            VaporView::TemperatureControllerCommand command;
            command.channel = 1;
            command.target_temperature_c = 33.0;
            state->pendingCommandSequence = controller->sendCommand(
                VaporView::CommandId::SetTemperatureTarget,
                VaporView::TelemetryCodec::serializeTemperatureControllerCommand(command));
            if (state->pendingCommandSequence == 0)
            {
                finish(false, QStringLiteral("RD105 target request was not sent"));
                return;
            }
            state->step = 7;
            return;
        }
        if (state->step == 7)
        {
            const bool targetReadBack =
                state->lastTemperatureController.valid &&
                std::fabs(state->lastTemperatureController.channels[0].target_temperature_c - 33.0) <= 0.001;
            if (state->commandAcks.contains(state->pendingCommandSequence) &&
                !requireCommandAck(state->pendingCommandSequence, "RD105 target command"))
            {
                return;
            }
            if (!targetReadBack)
            {
                return;
            }
            VaporView::TemperatureControllerCommand command;
            command.channel = 1;
            command.kp = 1200;
            command.ki = 120;
            command.kd = 12;
            state->pendingCommandSequence = controller->sendCommand(
                VaporView::CommandId::SetTemperaturePid,
                VaporView::TelemetryCodec::serializeTemperatureControllerCommand(command));
            if (state->pendingCommandSequence == 0)
            {
                finish(false, QStringLiteral("RD105 PID request was not sent"));
                return;
            }
            state->step = 8;
            return;
        }
        if (state->step == 8)
        {
            const bool pidReadBack =
                state->lastTemperatureController.valid &&
                state->lastTemperatureController.channels[0].kp == 1200 &&
                state->lastTemperatureController.channels[0].ki == 120 &&
                state->lastTemperatureController.channels[0].kd == 12;
            if (state->commandAcks.contains(state->pendingCommandSequence) &&
                !requireCommandAck(state->pendingCommandSequence, "RD105 PID command"))
            {
                return;
            }
            if (!pidReadBack)
            {
                return;
            }
            VaporView::Ai8TemperatureControllerProtocol::Selection selection;
            selection.channel = 3;
            selection.inputGroup = 2;
            selection.outputGroup = 2;
            state->pendingDeviceRequest = controller->readAi8Page(
                VaporView::Ai8TemperatureControllerProtocol::Page::Channel, selection);
            if (state->pendingDeviceRequest == 0)
            {
                finish(false, QStringLiteral("AI-8 read request was not sent"));
                return;
            }
            state->step = 9;
            return;
        }
        if (state->step == 9)
        {
            if (!state->deviceResponses.contains(state->pendingDeviceRequest))
            {
                return;
            }
            const VaporView::DeviceOperationResponse response =
                state->deviceResponses.take(state->pendingDeviceRequest);
            VaporView::Ai8TemperatureControllerProtocol::PageData page;
            if (response.error_code != VaporView::CommandErrorCode::Ok ||
                !VaporView::TelemetryCodec::parseAi8PageData(response.payload, page) ||
                std::fabs(page.channel.setpointC - 25.0) > 0.001)
            {
                finish(false, QStringLiteral("AI-8 remote read did not return simulation defaults"));
                return;
            }
            finish(true, QStringLiteral("Remote EPSILON, RTCM, RD105, and AI-8 E2E passed"));
        }
    });
    QTimer::singleShot(1200, pollTimer, [pollTimer]() { pollTimer->start(); });
}

int runApplication(int argc, char *argv[])
{
    QApplication app(argc, argv);
    configureSettingsDirectoryFromEnvironment();
    VaporView::writeLifecycleBreadcrumb("qapplication_constructed");
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, []() {
        VaporView::writeLifecycleBreadcrumb("about_to_quit");
    });

    const auto requestProcessExit = [](int exitCode, std::string_view reasonCode) {
        VaporView::writeLifecycleBreadcrumb("process_exit_requested", exitCode, reasonCode);
        return exitCode;
    };
    const auto runEventLoop = [&app]() {
        VaporView::writeLifecycleBreadcrumb("app_exec_enter");
        const int exitCode = app.exec();
        VaporView::writeLifecycleBreadcrumb("app_exec_returned", exitCode);
        return exitCode;
    };

    WheelValueChangeFilter wheelValueChangeFilter;
    app.installEventFilter(&wheelValueChangeFilter);

    app.setApplicationName("VaporView");
    app.setApplicationVersion("1.0.23");
    app.setOrganizationName("VaporView");
    VaporView::LogService logService(QStringLiteral("VaporView"));
    logService.installQtMessageHandler();
    VaporView::writeLifecycleBreadcrumb("logging_initialized");
    QObject::connect(&logService, &VaporView::LogService::writerFailureReported, &app,
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
    qRegisterMetaType<VaporView::TemperatureControllerData>("VaporView::TemperatureControllerData");
    qRegisterMetaType<VaporView::Ai8TemperatureControllerProtocol::LiveData>(
        "VaporView::Ai8TemperatureControllerProtocol::LiveData");
    qRegisterMetaType<VaporView::DeviceOperationResponse>("VaporView::DeviceOperationResponse");
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
    QCommandLineOption ai8RemoteE2eOption(QStringLiteral("ai8-remote-e2e-output"),
                                           QStringLiteral("Run the AI-8 remote UI E2E and write a result file"),
                                           QStringLiteral("path"));
    QCommandLineOption remoteDeviceE2eOption(QStringLiteral("remote-device-e2e-output"),
                                             QStringLiteral("Run the remote device E2E and write a result file"),
                                             QStringLiteral("path"));
    parser.addOptions({modeOption, sourceOption, telemetryPortOption, telemetryBaudOption,
                       telemetryTransportOption, telemetryHostOption, telemetryTcpPortOption,
                       skyConfigOption,
                       skySimulateOption, skyWaveHostOption, skyWavePortOption,
                       ai8RemoteE2eOption, remoteDeviceE2eOption});
    parser.process(app);

    if (parser.value(modeOption).compare(QStringLiteral("sky"), Qt::CaseInsensitive) == 0)
    {
        VaporView::SkyRuntimeOptions options;
        VaporView::TelemetryTransportType transport = VaporView::TelemetryTransportType::Tcp;
        if (!VaporView::parseTelemetryTransport(parser.value(telemetryTransportOption), transport))
        {
            logService.publish(VaporView::LogLevel::Critical,
                               QStringLiteral("App"),
                               QStringLiteral("startup.arguments"),
                               QStringLiteral("启动参数无效：遥测传输方式只能是 tcp 或 serial。"),
                               {{QStringLiteral("event"), QStringLiteral("startup_argument_invalid")},
                                {QStringLiteral("error_code"), QStringLiteral("INVALID_TELEMETRY_TRANSPORT")},
                                {QStringLiteral("argument"), QStringLiteral("--telemetry-transport")},
                                {QStringLiteral("value"), parser.value(telemetryTransportOption)}});
            return requestProcessExit(2, "invalid_telemetry_transport");
        }
        if (!parser.isSet(telemetryTransportOption) && parser.isSet(telemetryPortOption))
        {
            transport = VaporView::TelemetryTransportType::Serial;
            logService.publish(VaporView::LogLevel::Info,
                               QStringLiteral("App"),
                               QStringLiteral("startup.arguments"),
                               QStringLiteral("已按兼容规则选择串口遥测。"),
                               {{QStringLiteral("event"), QStringLiteral("startup_argument_compatibility_mode")},
                                {QStringLiteral("argument"), QStringLiteral("--telemetry-port")},
                                {QStringLiteral("selected_transport"), QStringLiteral("serial")}});
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
            logService.publish(VaporView::LogLevel::Critical,
                               QStringLiteral("App"),
                               QStringLiteral("startup.arguments"),
                               QStringLiteral("启动参数无效：串口遥测必须提供串口名称。"),
                               {{QStringLiteral("event"), QStringLiteral("startup_argument_missing")},
                                {QStringLiteral("error_code"), QStringLiteral("MISSING_TELEMETRY_PORT")},
                                {QStringLiteral("argument"), QStringLiteral("--telemetry-port")},
                                {QStringLiteral("transport"), QStringLiteral("serial")}});
            return requestProcessExit(2, "missing_telemetry_port");
        }
        if (options.telemetry_transport == VaporView::TelemetryTransportType::Tcp &&
            (options.telemetry_tcp_port <= 0 || options.telemetry_tcp_port > 65535))
        {
            logService.publish(VaporView::LogLevel::Critical,
                               QStringLiteral("App"),
                               QStringLiteral("startup.arguments"),
                               QStringLiteral("启动参数无效：TCP 遥测端口必须在 1 到 65535 之间。"),
                               {{QStringLiteral("event"), QStringLiteral("startup_argument_invalid")},
                                {QStringLiteral("error_code"), QStringLiteral("INVALID_TELEMETRY_TCP_PORT")},
                                {QStringLiteral("argument"), QStringLiteral("--telemetry-tcp-port")},
                                {QStringLiteral("value"), options.telemetry_tcp_port}});
            return requestProcessExit(2, "invalid_telemetry_tcp_port");
        }
        logService.publish(VaporView::LogLevel::Info,
                           QStringLiteral("App"),
                           QStringLiteral("startup.lifecycle"),
                           QStringLiteral("天空端后台模式已启动。"),
                           {{QStringLiteral("event"), QStringLiteral("sky_background_mode_started")},
                            {QStringLiteral("transport"), VaporView::telemetryTransportName(options.telemetry_transport)},
                            {QStringLiteral("split_core_executable"), QStringLiteral("VaporViewSkyCore.exe")},
                            {QStringLiteral("split_tui_executable"), QStringLiteral("VaporViewSkyTui.exe")}});
        VaporView::SkyRuntime runtime(options);
        if (!runtime.start())
        {
            return requestProcessExit(1, "sky_runtime_start_failed");
        }
        return runEventLoop();
    }

    auto *startupSplash = new VaporView::StartupSplash;
    QPointer<VaporView::StartupSplash> startupSplashGuard(startupSplash);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [startupSplashGuard]() {
        if (startupSplashGuard)
        {
            delete startupSplashGuard.data();
        }
    });
    startupSplash->showCentered();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

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
    VaporView::writeLifecycleBreadcrumb("main_window_created");
    mainWindow.setWindowTitle("VaporView");
    if (!app.windowIcon().isNull())
    {
        mainWindow.setWindowIcon(app.windowIcon());
    }

    QTimer::singleShot(0, &mainWindow, [&mainWindow, startupSplashGuard]() {
        QTimer::singleShot(0, &mainWindow, [&mainWindow, startupSplashGuard]() {
            const qint64 elapsedMs = startupSplashGuard
                ? startupSplashGuard->visibleElapsedMilliseconds()
                : kMinimumSplashVisibleMs;
            const int remainingMs = static_cast<int>(
                std::max<qint64>(0, kMinimumSplashVisibleMs - elapsedMs));
            QTimer::singleShot(remainingMs, &mainWindow, [&mainWindow, startupSplashGuard]() {
                showMainWindow(mainWindow, startupSplashGuard.data());
            });
        });
    });

    if (parser.isSet(ai8RemoteE2eOption))
    {
        startAi8RemoteE2e(app, mainWindow, parser.value(ai8RemoteE2eOption));
    }
    if (parser.isSet(remoteDeviceE2eOption))
    {
        startRemoteDeviceE2e(app, mainWindow, parser.value(remoteDeviceE2eOption));
    }

    return runEventLoop();
}

}  // namespace

int main(int argc, char *argv[])
{
    VaporView::writeLifecycleBreadcrumb("process_entry");
    const int exitCode = runApplication(argc, argv);
    VaporView::writeLifecycleBreadcrumb("normal_process_exit", exitCode);
    return exitCode;
}
