#include "ground/main/GroundMainWindowImplementation.h"
#include "ground/main/GatewaySettingsDialog.h"
#include "ground/calibration/CalibrationService.h"

#include <QSystemTrayIcon>
#include <QDeadlineTimer>

void MainWindow::initializeGateway()
{
    state_->gateway_settings_ = VaporView::Ground::Main::loadGatewaySettings();
    state_->gateway_service_ = std::make_unique<VaporView::Ground::Gateway::GatewayService>();
    state_->gateway_service_->setObjectName(QStringLiteral("gatewayService"));
    state_->gateway_service_->attach(state_->remote_sky_controller_->telemetryService());
    state_->gateway_service_->setListenerCommandPreparation(
        [this](VaporView::CommandId command, QByteArray& payload, QString& detail) {
            return prepareGatewayListenerCommand(command, payload, detail);
        });
    connect(state_->gateway_service_.get(),
        &VaporView::Ground::Gateway::GatewayService::listenerStartNotSent,
        this, [this] { state_->calibration_service_->finish(); });
    connect(state_->gateway_service_.get(), &VaporView::Ground::Gateway::GatewayService::statusChanged,
        this, [this](const QString& text) {
            if (state_->gateway_tray_)
                state_->gateway_tray_->setToolTip(QStringLiteral("VaporView\n") + text.left(110));
        });
    struct Control { QString key; QLabel *label; QToolButton *button; };
    const auto controls = [this] {
        return QList<Control>{
            {"epsilon", state_->home_epsilon_status_lbl_, state_->home_epsilon_action_btn_},
            {"ptb", state_->home_ptb_status_lbl_, state_->home_ptb_action_btn_},
            {"hc2a", state_->home_hc2a_status_lbl_, state_->home_hc2a_action_btn_},
            {"lidar", state_->home_lidar_status_lbl_, state_->home_lidar_action_btn_},
            {isFpgaRelayPath() ? "tec-in-situ" : "temperature", state_->home_temperature_status_lbl_, state_->home_temperature_action_btn_},
            {"tec_remote", state_->home_tec_remote_lbl_, state_->home_tec_remote_btn_},
            {"wave", state_->home_wave_status_lbl_, state_->home_wave_action_btn_},
            {"ai8_temperature", state_->home_ai8_temperature_status_lbl_, state_->home_ai8_temperature_action_btn_},
            {"bmp390", state_->home_bmp390_status_lbl_, state_->home_bmp390_action_btn_},
            {"sht45", state_->home_sht45_status_lbl_, state_->home_sht45_action_btn_},
            {"motion", state_->home_motion_status_lbl_, state_->home_motion_action_btn_}
        };
    };
    const auto ready = [this](const Control& control) {
        return !isUiTestMode() && !state_->virtual_fpga_port_ && control.button->isEnabled() &&
            state_->remote_recording_state_ != 1 && state_->remote_recording_state_ != 2 &&
            !state_->recording_service_->status().sessionOpen &&
            (control.key == "motion" || !isRemoteSkyMode() || state_->remote_sky_controller_->isOpen());
    };
    auto *refresh = new QTimer(state_->gateway_service_.get());
    connect(refresh, &QTimer::timeout, this, [this, controls, ready] {
        if (!state_->gateway_service_->listenerPort()) return;
        QJsonArray rows;
        for (const auto& control : controls()) {
            if (!control.label || !control.button || control.label->isHidden()) continue;
            rows.append(QJsonObject{{"key", control.key}, {"name", control.label->text()},
                {"state", control.label->property(control.key == "motion" ? "state" : "connectionState").toString()},
                {"connected", control.label->property("connected").toBool()}, {"enabled", ready(control)},
                {"detail", control.label->accessibleDescription()}});
        }
        state_->gateway_service_->updateDesktopControls(rows);
    });
    refresh->start(500);
    state_->gateway_service_->setDesktopAction([this, controls, ready](const QString& key, bool connectRequested, auto complete) {
        for (const auto& control : controls()) {
            if (control.key != key || !control.label || !control.button || control.label->isHidden()) continue;
            if (!ready(control)) { complete("device-unavailable"); return; }
            if (control.label->property("connected").toBool() == connectRequested) { complete("state-confirmed"); return; }
            control.button->click();
            auto *poll = new QTimer(state_->gateway_service_.get());
            const bool remote = isRemoteSkyMode() && key != "motion";
            const auto link = state_->remote_sky_controller_->telemetryService()->linkGeneration();
            const QDeadlineTimer deadline(15000);
            connect(poll, &QTimer::timeout, this, [this, poll, control, connectRequested, remote, link, deadline, complete] {
                QString outcome;
                if (control.label->isHidden() || (remote && (!state_->remote_sky_controller_->isOpen() ||
                    state_->remote_sky_controller_->telemetryService()->linkGeneration() != link))) outcome = "disconnected";
                else if (control.button->isEnabled() && control.label->property("connected").toBool() == connectRequested)
                    outcome = "state-confirmed";
                else if (deadline.hasExpired()) outcome = "device-state-timeout";
                else return;
                poll->stop();
                poll->deleteLater();
                complete(outcome);
            });
            poll->start(200);
            return;
        }
        complete("device-unavailable");
    });
    configureGatewayRuntime();
}

void MainWindow::configureGatewayRuntime()
{
    if (!state_->gateway_service_) return;
    auto config = state_->gateway_settings_.connection;
    if (isUiTestMode() || state_->virtual_fpga_port_)
    {
        config.enabled = false;
        config.listener.enabled = false;
    }
    state_->gateway_service_->configure(config);
    updateGatewayTray();
}

void MainWindow::updateGatewayTray()
{
    if (!state_->gateway_tray_ && state_->gateway_settings_.closeToTray &&
        !isUiTestMode() && QSystemTrayIcon::isSystemTrayAvailable())
    {
        QIcon icon = windowIcon();
        if (icon.isNull()) icon = style()->standardIcon(QStyle::SP_ComputerIcon);
        state_->gateway_tray_ = new QSystemTrayIcon(icon, this);
        state_->gateway_tray_->setObjectName(QStringLiteral("gatewaySystemTray"));
        auto *menu = new QMenu(this);
        menu->setObjectName(QStringLiteral("gatewayTrayMenu"));
        state_->gateway_show_action_ = menu->addAction(QString());
        connect(state_->gateway_show_action_, &QAction::triggered, this, [this] {
            showNormal();
            raise();
            activateWindow();
        });
        menu->addSeparator();
        menu->addAction(state_->exit_action_);
        state_->gateway_tray_->setContextMenu(menu);
        connect(state_->gateway_tray_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                    state_->gateway_show_action_->trigger();
            });
    }
    if (state_->gateway_show_action_)
        state_->gateway_show_action_->setText(state_->is_english_
            ? QStringLiteral("Show VaporView") : QStringLiteral("显示 VaporView"));
    if (state_->gateway_tray_)
    {
        state_->gateway_tray_->setToolTip(QStringLiteral("VaporView\n") +
            (state_->gateway_service_ ? state_->gateway_service_->statusText().left(110) : QString()));
        state_->gateway_tray_->setVisible(state_->gateway_settings_.closeToTray &&
            !isUiTestMode() && !state_->gateway_exit_requested_ && QSystemTrayIcon::isSystemTrayAvailable());
    }
}

void MainWindow::showGatewaySettings()
{
    if (isUiTestMode() || state_->virtual_fpga_port_) return;
    VaporView::Ground::Main::GatewaySettingsDialog dialog(
        state_->gateway_settings_, state_->gateway_service_.get(), state_->is_english_, this);
    connect(&dialog, &VaporView::Ground::Main::GatewaySettingsDialog::sessionSyncRequested,
        this, [this, &dialog](const QString& directory) {
            const auto current = state_->recording_service_->status();
            const auto selected = QFileInfo(directory).canonicalFilePath();
            const auto active = QFileInfo(current.sessionDirectory).canonicalFilePath();
#ifdef Q_OS_WIN
            constexpr auto sensitivity = Qt::CaseInsensitive;
#else
            constexpr auto sensitivity = Qt::CaseSensitive;
#endif
            const auto selectedPrefix = selected.endsWith(QLatin1Char('/')) ? selected : selected + QLatin1Char('/');
            const auto activePrefix = active.endsWith(QLatin1Char('/')) ? active : active + QLatin1Char('/');
            if (current.sessionOpen && !active.isEmpty() &&
                (selected.compare(active, sensitivity) == 0 || active.startsWith(selectedPrefix, sensitivity) ||
                 selected.startsWith(activePrefix, sensitivity)))
            {
                QMessageBox::warning(&dialog, dialog.windowTitle(), state_->is_english_
                    ? QStringLiteral("Stop recording before syncing this Session.")
                    : QStringLiteral("请先停止记录，再同步该 Session。"));
                return;
            }
            state_->gateway_service_->enqueueSession(directory);
        });
    connect(state_->gateway_service_.get(), &VaporView::Ground::Gateway::GatewayService::sessionSyncFinished,
        &dialog, [&dialog](const QString&, bool success, const QString& error) {
            if (!success)
                QMessageBox::warning(&dialog, dialog.windowTitle(), error);
        });
    if (dialog.exec() != QDialog::Accepted) return;
    state_->gateway_settings_ = dialog.settings();
    VaporView::Ground::Main::saveGatewaySettings(state_->gateway_settings_);
    configureGatewayRuntime();
}

void MainWindow::exitApplication()
{
    state_->gateway_exit_requested_ = true;
    if (state_->gateway_tray_) state_->gateway_tray_->hide();
    if (close()) qApp->quit();
}
