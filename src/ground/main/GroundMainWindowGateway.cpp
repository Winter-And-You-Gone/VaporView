#include "ground/main/GroundMainWindowImplementation.h"
#include "ground/main/GatewaySettingsDialog.h"

#include <QSystemTrayIcon>

void MainWindow::initializeGateway()
{
    state_->gateway_settings_ = VaporView::Ground::Main::loadGatewaySettings();
    state_->gateway_service_ = std::make_unique<VaporView::Ground::Gateway::GatewayService>();
    state_->gateway_service_->setObjectName(QStringLiteral("gatewayService"));
    state_->gateway_service_->attach(state_->remote_sky_controller_->telemetryService());
    connect(state_->gateway_service_.get(), &VaporView::Ground::Gateway::GatewayService::statusChanged,
        this, [this](const QString& text) {
            if (state_->gateway_tray_)
                state_->gateway_tray_->setToolTip(QStringLiteral("VaporView\n") + text.left(110));
        });
    configureGatewayRuntime();
}

void MainWindow::configureGatewayRuntime()
{
    if (!state_->gateway_service_) return;
    auto config = state_->gateway_settings_.connection;
    if (isUiTestMode()) config.enabled = false;
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
    if (isUiTestMode()) return;
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
