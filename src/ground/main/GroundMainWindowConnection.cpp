#include "ground/main/GroundMainWindowImplementation.h"
#include "ground/devices/DeviceRatePolicy.h"
#include "ground/widgets/SerialPortComboSupport.h"

void MainWindow::updateConnectionStatus(bool connected)
{
    if (isUiTestMode())
    {
        Q_UNUSED(connected);
        const std::array devices{VaporView::SkyDeviceId::Epsilon, VaporView::SkyDeviceId::Ptb,
                                 VaporView::SkyDeviceId::Hmp, VaporView::SkyDeviceId::Lidar,
                                 VaporView::SkyDeviceId::TemperatureController,
                                 VaporView::SkyDeviceId::Ai8TemperatureController,
                                 VaporView::SkyDeviceId::WaveTcp};
        const bool anyConnected = std::any_of(devices.cbegin(), devices.cend(),
            [this](VaporView::SkyDeviceId device) {
                return state_->ui_test_model_->deviceState(device) == VaporView::DeviceState::Connected;
            });
        state_->connect_btn_->setEnabled(!anyConnected && !state_->ui_test_connection_in_progress_);
        state_->cancel_connect_btn_->setEnabled(state_->ui_test_connection_in_progress_);
        state_->disconnect_btn_->setEnabled(anyConnected && !state_->ui_test_connection_in_progress_);
        state_->refresh_ports_btn_->setEnabled(!state_->ui_test_connection_in_progress_);
        if (state_->auto_detect_ports_btn_)
        {
            state_->auto_detect_ports_btn_->setEnabled(!state_->ui_test_connection_in_progress_);
            state_->auto_detect_ports_btn_->setText(state_->is_english_ ? QStringLiteral("Auto Detect Ports")
                                                                         : QStringLiteral("自动识别串口"));
        }
        for (QAction *action : {state_->epsilon_reconfigure_action_, state_->epsilon_rtcm_port_action_,
                                state_->epsilon_packet_rates_action_, state_->rtk_config_action_})
        {
            if (action) action->setEnabled(true);
        }
        updateRecordingActionStates();
        updateHomeDeviceStatusCapsules();
        updateTemperatureTitleButtonsState();
        updateDeviceConfigState();
        return;
    }
    const bool localWaveformConnected = !isRemoteSkyMode() && state_->tcp_wave_panel_ && state_->tcp_wave_panel_->isConnected();
    const bool localWaveformConnecting = !isRemoteSkyMode() && state_->tcp_wave_panel_ && state_->tcp_wave_panel_->isConnecting();
    connected = connected || localWaveformConnected;
    state_->is_connected_ = connected;
    const bool uiBusy = state_->connection_attempt_in_progress_ || state_->port_detection_in_progress_ || state_->epsilon_reconfigure_in_progress_ || localWaveformConnecting;
    const bool inputsEnabled = !connected && !uiBusy;

    state_->connect_btn_->setEnabled(inputsEnabled);
    state_->cancel_connect_btn_->setEnabled(state_->connection_attempt_in_progress_);
    state_->disconnect_btn_->setEnabled(connected && !state_->connection_attempt_in_progress_ && !state_->epsilon_reconfigure_in_progress_);
    state_->refresh_ports_btn_->setEnabled(inputsEnabled);
    if (state_->epsilon_reconfigure_action_)
    {
        state_->epsilon_reconfigure_action_->setEnabled(!uiBusy);
    }
    if (state_->epsilon_rtcm_port_action_)
    {
        state_->epsilon_rtcm_port_action_->setEnabled(!uiBusy);
    }
    if (state_->epsilon_packet_rates_action_)
    {
        state_->epsilon_packet_rates_action_->setEnabled(!uiBusy);
    }
    if (state_->auto_detect_ports_btn_)
    {
        state_->auto_detect_ports_btn_->setEnabled(!connected && !state_->connection_attempt_in_progress_ && !state_->epsilon_reconfigure_in_progress_);
        state_->auto_detect_ports_btn_->setText(state_->port_detection_in_progress_
            ? (state_->is_english_ ? "Cancel Auto Detect" : "取消自动识别")
            : (state_->is_english_ ? "Auto Detect Ports" : "自动识别串口"));
        state_->auto_detect_ports_btn_->setToolTip(state_->port_detection_in_progress_
            ? (state_->is_english_ ? "Stop the current serial-port detection task." : "停止当前串口自动识别任务。")
            : (state_->is_english_ ? "Probe available serial ports and automatically assign detected devices."
                           : "扫描可用串口，并将识别出的设备自动填入对应端口。"));
    }

    if (state_->epsilon_port_combo_) state_->epsilon_port_combo_->setEnabled(inputsEnabled);
    if (state_->ptb_port_combo_) state_->ptb_port_combo_->setEnabled(inputsEnabled);
    if (state_->hmp_port_combo_) state_->hmp_port_combo_->setEnabled(inputsEnabled);
    if (state_->lidar_port_combo_) state_->lidar_port_combo_->setEnabled(inputsEnabled);
    if (state_->temperature_port_combo_) state_->temperature_port_combo_->setEnabled(inputsEnabled);
    updateTemperatureControllerTitleText();
    if (state_->epsilon_baud_combo_) state_->epsilon_baud_combo_->setEnabled(inputsEnabled);
    if (state_->ptb_baud_combo_) state_->ptb_baud_combo_->setEnabled(inputsEnabled);
    if (state_->hmp_baud_combo_) state_->hmp_baud_combo_->setEnabled(inputsEnabled);
    if (state_->lidar_baud_combo_) state_->lidar_baud_combo_->setEnabled(inputsEnabled);
    if (state_->temperature_baud_combo_) state_->temperature_baud_combo_->setEnabled(inputsEnabled);
    for (QPushButton* button : {state_->imu_apply_btn_, state_->imu_hi91_btn_, state_->imu_hi92_btn_, state_->imu_baud_115200_btn_, state_->imu_baud_921600_btn_,
                                state_->imu_rate_100_btn_, state_->imu_rate_200_btn_, state_->imu_rate_500_btn_, state_->imu_rate_1000_btn_})
    {
        if (button)
        {
            button->setEnabled(!state_->connection_attempt_in_progress_ && !state_->port_detection_in_progress_ && !state_->epsilon_reconfigure_in_progress_);
        }
    }

    updateSourceModeUi();
    updateRecordingActionStates();
    updateHomeDeviceStatusCapsules();
    updateTemperatureTitleButtonsState();
    updateDeviceConfigState();
}

bool MainWindow::homeDeviceConnected(VaporView::SkyDeviceId device) const
{
    if (isUiTestMode())
    {
        return state_->ui_test_model_->deviceState(device) == VaporView::DeviceState::Connected;
    }
    if (isRemoteSkyMode())
    {
        return state_->remote_sky_controller_->deviceState(device) == VaporView::DeviceState::Connected;
    }

    const CollectorSnapshot collectors = snapshotCollectors();
    switch (device)
    {
    case VaporView::SkyDeviceId::Epsilon:
        return collectors.epsilon && collectors.epsilon->isRunning();
    case VaporView::SkyDeviceId::Ptb:
        return collectors.ptb && collectors.ptb->isRunning();
    case VaporView::SkyDeviceId::Hmp:
        return collectors.hmp && collectors.hmp->isRunning();
    case VaporView::SkyDeviceId::Lidar:
        return collectors.lidar && collectors.lidar->isRunning();
    case VaporView::SkyDeviceId::TemperatureController:
        return collectors.temperature_controller && collectors.temperature_controller->isRunning();
    case VaporView::SkyDeviceId::Ai8TemperatureController:
        return collectors.ai8_temperature_controller && collectors.ai8_temperature_controller->isRunning();
    case VaporView::SkyDeviceId::WaveTcp:
        return state_->tcp_wave_panel_ && state_->tcp_wave_panel_->isConnected();
    case VaporView::SkyDeviceId::All:
        return false;
    }
    return false;
}

bool MainWindow::homeDevicePortSelected(VaporView::SkyDeviceId device) const
{
    if (device == VaporView::SkyDeviceId::WaveTcp)
    {
        return state_->tcp_wave_panel_ != nullptr;
    }

    auto portSelected = [this](const QComboBox *combo) {
        if (!combo)
        {
            return false;
        }
        const QString text = localSerialPortComboValue(combo);
        return !combo->property(kLocalSerialPortManualEntryProperty).toBool() &&
               !text.isEmpty() &&
               !text.startsWith(QStringLiteral("--"));
    };

    switch (device)
    {
    case VaporView::SkyDeviceId::Epsilon:
        return portSelected(state_->epsilon_port_combo_);
    case VaporView::SkyDeviceId::Ptb:
        return portSelected(state_->ptb_port_combo_);
    case VaporView::SkyDeviceId::Hmp:
        return portSelected(state_->hmp_port_combo_);
    case VaporView::SkyDeviceId::Lidar:
        return portSelected(state_->lidar_port_combo_);
    case VaporView::SkyDeviceId::TemperatureController:
        return portSelected(state_->temperature_port_combo_);
    case VaporView::SkyDeviceId::Ai8TemperatureController:
        return portSelected(state_->device_config_.ai8_temperature_port_combo);
    case VaporView::SkyDeviceId::All:
    case VaporView::SkyDeviceId::WaveTcp:
        return false;
    }
    return false;
}

VaporView::DeviceState MainWindow::homeDeviceActionState(VaporView::SkyDeviceId device) const
{
    if (isUiTestMode())
    {
        return state_->ui_test_model_->deviceState(device);
    }
    if (isRemoteSkyMode())
    {
        if (device == VaporView::SkyDeviceId::Ai8TemperatureController)
        {
            return VaporView::DeviceState::Disabled;
        }
        if (!state_->remote_sky_controller_ || !state_->remote_sky_controller_->isOpen())
        {
            return VaporView::DeviceState::Disabled;
        }
        if (device == VaporView::SkyDeviceId::WaveTcp && state_->remote_wave_stream_enable_pending_)
        {
            return VaporView::DeviceState::Connecting;
        }
        const VaporView::DeviceState state = state_->remote_sky_controller_->deviceState(device);
        return state == VaporView::DeviceState::Reconnecting ? VaporView::DeviceState::Connecting : state;
    }

    if (device == VaporView::SkyDeviceId::WaveTcp)
    {
        if (!state_->tcp_wave_panel_)
        {
            return VaporView::DeviceState::Disabled;
        }
        if (state_->tcp_wave_panel_->isConnecting())
        {
            return VaporView::DeviceState::Connecting;
        }
        return state_->tcp_wave_panel_->isConnected() ? VaporView::DeviceState::Connected : VaporView::DeviceState::Disconnected;
    }

    if (homeDeviceConnected(device))
    {
        return VaporView::DeviceState::Connected;
    }
    if (!homeDevicePortSelected(device))
    {
        return VaporView::DeviceState::Disabled;
    }
    if (state_->connection_attempt_in_progress_)
    {
        return VaporView::DeviceState::Connecting;
    }
    return VaporView::DeviceState::Disconnected;
}

void MainWindow::triggerHomeDeviceAction(VaporView::SkyDeviceId device)
{
    const VaporView::DeviceState state = homeDeviceActionState(device);
    if (state == VaporView::DeviceState::Disabled ||
        state == VaporView::DeviceState::Connecting ||
        state == VaporView::DeviceState::Reconnecting)
    {
        return;
    }
    const bool connected = state == VaporView::DeviceState::Connected;
    const bool connectRequested = !connected;
    if (isUiTestMode())
    {
        if (!connectRequested)
        {
            state_->ui_test_model_->setDeviceState(device, VaporView::DeviceState::Disconnected);
            if (device == VaporView::SkyDeviceId::WaveTcp && state_->tcp_wave_panel_)
            {
                state_->tcp_wave_panel_->setUiTestConnected(false);
            }
            publishUiTestEvent(QStringLiteral("ui_test_device_disconnected"),
                               (state_->is_english_ ? QStringLiteral("Disconnected %1") : QStringLiteral("已断开%1"))
                                   .arg(homeDeviceDisplayName(device, state_->is_english_)),
                               {{QStringLiteral("device_id"), VaporView::skyDeviceIdName(device)}});
            updateConnectionStatus(false);
            return;
        }
        state_->ui_test_model_->setDeviceState(device, VaporView::DeviceState::Connecting);
        startHomeDeviceActionSpinner(device);
        updateConnectionStatus(false);
        QTimer::singleShot(350, this, [this, device]() {
            if (!isUiTestMode() || state_->ui_test_model_->deviceState(device) != VaporView::DeviceState::Connecting)
            {
                return;
            }
            state_->ui_test_model_->setDeviceState(device, VaporView::DeviceState::Connected);
            if (device == VaporView::SkyDeviceId::WaveTcp && state_->tcp_wave_panel_)
            {
                state_->tcp_wave_panel_->setUiTestConnected(true);
            }
            publishUiTestEvent(QStringLiteral("ui_test_device_connected"),
                               (state_->is_english_ ? QStringLiteral("Connected %1") : QStringLiteral("已连接%1"))
                                   .arg(homeDeviceDisplayName(device, state_->is_english_)),
                               {{QStringLiteral("device_id"), VaporView::skyDeviceIdName(device)}});
            updateConnectionStatus(false);
        });
        return;
    }
    if (isRemoteSkyMode())
    {
        if (device == VaporView::SkyDeviceId::Ai8TemperatureController)
        {
            return;
        }
        if (!state_->remote_sky_controller_ || !state_->remote_sky_controller_->isOpen())
        {
            return;
        }
        if (connectRequested)
        {
            startHomeDeviceActionSpinner(device);
        }
        if (device == VaporView::SkyDeviceId::WaveTcp)
        {
            requestRemoteWaveTcpConnection(connectRequested);
            return;
        }
        sendRemoteDeviceCommand(connectRequested ? VaporView::CommandId::ConnectDevice : VaporView::CommandId::DisconnectDevice,
                                device);
        state_->remote_sky_controller_->setDeviceState(
            device,
            connectRequested ? VaporView::DeviceState::Connecting : VaporView::DeviceState::Disconnected);
        updateRemoteDeviceButtonText(device, state_->remote_sky_controller_->deviceState(device));
        updateHomeDeviceStatusCapsules();
        return;
    }

    if (device == VaporView::SkyDeviceId::WaveTcp)
    {
        if (state_->tcp_wave_panel_)
        {
            if (connectRequested)
            {
                startHomeDeviceActionSpinner(device);
            }
            state_->tcp_wave_panel_->toggleConnection();
            updateHomeDeviceStatusCapsules();
        }
        return;
    }

    QAction *action = connected ? state_->disconnect_btn_ : state_->connect_btn_;
    if (action && action->isEnabled())
    {
        if (connectRequested)
        {
            for (VaporView::SkyDeviceId candidate : {VaporView::SkyDeviceId::Epsilon,
                                                     VaporView::SkyDeviceId::Ptb,
                                                     VaporView::SkyDeviceId::Hmp,
                                                     VaporView::SkyDeviceId::Lidar,
                                                     VaporView::SkyDeviceId::TemperatureController,
                                                     VaporView::SkyDeviceId::Ai8TemperatureController})
            {
                if (homeDevicePortSelected(candidate) && !homeDeviceConnected(candidate))
                {
                    startHomeDeviceActionSpinner(candidate);
                }
            }
        }
        action->trigger();
    }
}

void MainWindow::startHomeDeviceActionSpinner(VaporView::SkyDeviceId device)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 currentUntilMs = state_->home_device_action_spinner_until_ms_.value(device, 0);
    if (!state_->home_device_action_spinner_started_ms_.contains(device) || currentUntilMs <= nowMs)
    {
        state_->home_device_action_spinner_started_ms_.insert(device, nowMs);
    }
    const qint64 untilMs = nowMs + kHomeDeviceActionSpinnerMinimumMs;
    state_->home_device_action_spinner_until_ms_.insert(device, std::max(currentUntilMs, untilMs));
    if (state_->home_device_action_spinner_timer_ && !state_->home_device_action_spinner_timer_->isActive())
    {
        state_->home_device_action_spinner_timer_->start();
    }
}

bool MainWindow::homeDeviceActionSpinnerActive(VaporView::SkyDeviceId device, qint64 nowMs) const
{
    return state_->home_device_action_spinner_until_ms_.value(device, 0) > nowMs;
}

int MainWindow::homeDeviceActionSpinnerDegrees(VaporView::SkyDeviceId device, qint64 nowMs) const
{
    const qint64 startMs = state_->home_device_action_spinner_started_ms_.value(device, nowMs);
    const qint64 elapsedMs = std::max<qint64>(0, nowMs - startMs);
    const int frame = static_cast<int>((elapsedMs / kHomeDeviceActionSpinnerIntervalMs) %
                                       kHomeDeviceActionSpinnerFrames);
    return (frame * 360) / kHomeDeviceActionSpinnerFrames;
}

void MainWindow::updateHomeDeviceStatusCapsules()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    bool anySpinnerActive = false;
    auto updateCapsule = [this, &anySpinnerActive, nowMs](QLabel *label, QToolButton *button, VaporView::SkyDeviceId device) {
        if (!label)
        {
            return;
        }
        const qint64 spinnerUntilMs = state_->home_device_action_spinner_until_ms_.value(device, 0);
        if (spinnerUntilMs > 0 && spinnerUntilMs <= nowMs)
        {
            state_->home_device_action_spinner_until_ms_.remove(device);
        }
        const VaporView::DeviceState state = homeDeviceActionState(device);
        const bool connected = state == VaporView::DeviceState::Connected;
        const bool connecting = state == VaporView::DeviceState::Connecting || state == VaporView::DeviceState::Reconnecting;
        const bool spinnerActive = connecting || homeDeviceActionSpinnerActive(device, nowMs);
        if (spinnerActive)
        {
            anySpinnerActive = true;
        }
        else
        {
            state_->home_device_action_spinner_started_ms_.remove(device);
        }
        const QString stateKey = connected
            ? QStringLiteral("connected")
            : connecting
                ? QStringLiteral("connecting")
                : state == VaporView::DeviceState::Disabled
                    ? QStringLiteral("disabled")
                    : QStringLiteral("disconnected");
        const QString stateText = connected
            ? (state_->is_english_ ? QStringLiteral("Connected") : QStringLiteral("已连接"))
            : connecting
                ? (state_->is_english_ ? QStringLiteral("Connecting") : QStringLiteral("连接中"))
                : state == VaporView::DeviceState::Disabled
                    ? (state_->is_english_ ? QStringLiteral("Not ready") : QStringLiteral("未就绪"))
                    : (state_->is_english_ ? QStringLiteral("Ready to connect") : QStringLiteral("可以连接"));
        const QString deviceName = homeDeviceDisplayName(device, state_->is_english_);
        label->setText(deviceName);
        label->setProperty("connected", connected);
        label->setProperty("state", stateKey);
        label->setToolTip(state_->is_english_
            ? QStringLiteral("%1 status: %2").arg(deviceName, stateText)
            : QStringLiteral("%1状态：%2").arg(deviceName, stateText));
        label->style()->unpolish(label);
        label->style()->polish(label);

        if (!button)
        {
            return;
        }
        const bool remoteMode = isRemoteSkyMode();
        const bool linkOpen = state_->remote_sky_controller_ && state_->remote_sky_controller_->isOpen();
        const bool enabled = state == VaporView::DeviceState::Disabled || spinnerActive
            ? false
            : isUiTestMode()
                ? true
                : remoteMode
                    ? linkOpen
                    : (device == VaporView::SkyDeviceId::WaveTcp
                        ? state_->tcp_wave_panel_ != nullptr
                        : ((connected && state_->disconnect_btn_ && state_->disconnect_btn_->isEnabled()) ||
                           (!connected && state_->connect_btn_ && state_->connect_btn_->isEnabled())));
        const QString actionText = [&]() {
            if (spinnerActive)
            {
                return state_->is_english_ ? QStringLiteral("Connecting") : QStringLiteral("连接中");
            }
            if (connected)
            {
                return state_->is_english_ ? QStringLiteral("Disconnect") : QStringLiteral("断开");
            }
            if (state == VaporView::DeviceState::Disabled)
            {
                if (remoteMode)
                {
                    return state_->is_english_ ? QStringLiteral("Connect telemetry first") : QStringLiteral("请先连接数传");
                }
                if (device == VaporView::SkyDeviceId::WaveTcp)
                {
                    return state_->is_english_ ? QStringLiteral("TCP wave panel unavailable") : QStringLiteral("TCP 波形面板未就绪");
                }
                return state_->is_english_ ? QStringLiteral("Select port first") : QStringLiteral("请先选择串口");
            }
            return state_->is_english_ ? QStringLiteral("Connect") : QStringLiteral("连接");
        }();
        QString modeHint;
        if (remoteMode)
        {
            modeHint = state_->is_english_ ? QStringLiteral("remote Sky device") : QStringLiteral("天空端设备");
        }
        else if (device == VaporView::SkyDeviceId::WaveTcp)
        {
            modeHint = state_->is_english_ ? QStringLiteral("local TCP wave source") : QStringLiteral("本地 TCP 波形源");
        }
        else
        {
            modeHint = state_->is_english_ ? QStringLiteral("local serial devices") : QStringLiteral("本地串口设备");
        }
        button->setEnabled(enabled);
        if (spinnerActive)
        {
            button->setIcon(createRotatedLucideIcon(QStringLiteral("refresh-cw"),
                                                    toolbarColor(AppThemeColor::HomeDeviceSuccess),
                                                    homeDeviceActionSpinnerDegrees(device, nowMs)));
        }
        else
        {
            const QString iconName = connected ? QStringLiteral("unlink") : QStringLiteral("link");
            const QColor iconColor = connected
                ? toolbarColor(AppThemeColor::HomeDeviceDanger)
                : state == VaporView::DeviceState::Disabled
                    ? toolbarColor(AppThemeColor::ToolbarDisabled)
                    : toolbarColor(AppThemeColor::HomeDeviceSuccess);
            button->setIcon(createLucideIcon(iconName, iconColor));
        }
        button->setToolTip(state_->is_english_
            ? QStringLiteral("%1 %2 (%3)").arg(actionText, deviceName, modeHint)
            : QStringLiteral("%1%2（%3）").arg(actionText, deviceName, modeHint));
        button->setAccessibleName(button->toolTip());
        button->setProperty("connected", connected);
        button->setProperty("state", spinnerActive ? QStringLiteral("connecting") : stateKey);
        button->style()->unpolish(button);
        button->style()->polish(button);
    };

    updateCapsule(state_->home_epsilon_status_lbl_, state_->home_epsilon_action_btn_, VaporView::SkyDeviceId::Epsilon);
    updateCapsule(state_->home_ptb_status_lbl_, state_->home_ptb_action_btn_, VaporView::SkyDeviceId::Ptb);
    updateCapsule(state_->home_hmp_status_lbl_, state_->home_hmp_action_btn_, VaporView::SkyDeviceId::Hmp);
    updateCapsule(state_->home_lidar_status_lbl_, state_->home_lidar_action_btn_, VaporView::SkyDeviceId::Lidar);
    updateCapsule(state_->home_temperature_status_lbl_, state_->home_temperature_action_btn_, VaporView::SkyDeviceId::TemperatureController);
    updateCapsule(state_->home_wave_status_lbl_, state_->home_wave_action_btn_, VaporView::SkyDeviceId::WaveTcp);
    updateCapsule(state_->home_ai8_temperature_status_lbl_, state_->home_ai8_temperature_action_btn_, VaporView::SkyDeviceId::Ai8TemperatureController);
    if (state_->home_device_action_spinner_timer_)
    {
        if (anySpinnerActive)
        {
            if (!state_->home_device_action_spinner_timer_->isActive())
            {
                state_->home_device_action_spinner_timer_->start();
            }
        }
        else
        {
            state_->home_device_action_spinner_timer_->stop();
            state_->home_device_action_spinner_started_ms_.clear();
        }
    }
}

void MainWindow::updateHomeDeviceActionSpinnerIcons()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    bool anySpinnerActive = false;
    bool needsFullRefresh = false;
    auto updateButton = [this, nowMs, &anySpinnerActive, &needsFullRefresh](QToolButton *button, VaporView::SkyDeviceId device) {
        if (!button)
        {
            return;
        }

        const qint64 spinnerUntilMs = state_->home_device_action_spinner_until_ms_.value(device, 0);
        if (spinnerUntilMs > 0 && spinnerUntilMs <= nowMs)
        {
            state_->home_device_action_spinner_until_ms_.remove(device);
            needsFullRefresh = true;
        }
        const VaporView::DeviceState state = homeDeviceActionState(device);
        const bool spinnerActive =
            state == VaporView::DeviceState::Connecting ||
            state == VaporView::DeviceState::Reconnecting ||
            homeDeviceActionSpinnerActive(device, nowMs);
        if (!spinnerActive)
        {
            state_->home_device_action_spinner_started_ms_.remove(device);
            return;
        }

        anySpinnerActive = true;
        button->setIcon(createRotatedLucideIcon(QStringLiteral("refresh-cw"),
                                                toolbarColor(AppThemeColor::HomeDeviceSuccess),
                                                homeDeviceActionSpinnerDegrees(device, nowMs)));
        button->update();
    };

    updateButton(state_->home_epsilon_action_btn_, VaporView::SkyDeviceId::Epsilon);
    updateButton(state_->home_ptb_action_btn_, VaporView::SkyDeviceId::Ptb);
    updateButton(state_->home_hmp_action_btn_, VaporView::SkyDeviceId::Hmp);
    updateButton(state_->home_lidar_action_btn_, VaporView::SkyDeviceId::Lidar);
    updateButton(state_->home_temperature_action_btn_, VaporView::SkyDeviceId::TemperatureController);
    updateButton(state_->home_wave_action_btn_, VaporView::SkyDeviceId::WaveTcp);
    updateButton(state_->home_ai8_temperature_action_btn_, VaporView::SkyDeviceId::Ai8TemperatureController);
    updateButton(state_->device_config_.epsilon_remote_action_btn, VaporView::SkyDeviceId::Epsilon);
    updateButton(state_->device_config_.ptb_remote_action_btn, VaporView::SkyDeviceId::Ptb);
    updateButton(state_->device_config_.hmp_remote_action_btn, VaporView::SkyDeviceId::Hmp);
    updateButton(state_->device_config_.lidar_remote_action_btn, VaporView::SkyDeviceId::Lidar);
    updateButton(state_->device_config_.temperature_remote_action_btn,
                 VaporView::SkyDeviceId::TemperatureController);
    updateButton(state_->device_config_.ai8_temperature_remote_action_btn,
                 VaporView::SkyDeviceId::Ai8TemperatureController);
    updateButton(state_->temperature_title_action_btn_,
                 VaporView::SkyDeviceId::TemperatureController);
    updateButton(state_->ai8_temperature_title_action_btn_,
                 VaporView::SkyDeviceId::Ai8TemperatureController);
    if (needsFullRefresh)
    {
        updateHomeDeviceStatusCapsules();
        updateTemperatureTitleButtonsState();
        updateDeviceConfigState();
        return;
    }
    if (!anySpinnerActive && state_->home_device_action_spinner_timer_)
    {
        state_->home_device_action_spinner_timer_->stop();
        state_->home_device_action_spinner_started_ms_.clear();
    }
}

bool MainWindow::anyCollectorRunning() const
{
    return state_->local_connection_controller_ && state_->local_connection_controller_->anyCollectorRunning();
}

bool MainWindow::anyLocalDeviceConnected() const
{
    return anyCollectorRunning() ||
        (!isRemoteSkyMode() && state_->tcp_wave_panel_ && state_->tcp_wave_panel_->isConnected());
}

void MainWindow::configureLocalConnectionCoordinator()
{
    VaporView::Ground::Devices::LocalConnectionCoordinatorHooks hooks;
    hooks.startSerial = [this](VaporView::Ground::Devices::LocalConnectionRequest request) {
        return state_->local_connection_controller_ &&
               state_->local_connection_controller_->connectAsync(std::move(request));
    };
    hooks.cancelSerial = [this]() {
        if (state_->local_connection_controller_)
        {
            state_->local_connection_controller_->requestCancel();
        }
    };
    hooks.stopSerial = [this]() {
        if (state_->local_connection_controller_)
        {
            state_->local_connection_controller_->disconnect();
        }
    };
    hooks.waveformAvailable = [this]() {
        return !isRemoteSkyMode() && state_->tcp_wave_panel_ != nullptr;
    };
    hooks.waveformConnected = [this]() {
        return !isRemoteSkyMode() && state_->tcp_wave_panel_ &&
               state_->tcp_wave_panel_->isConnected();
    };
    hooks.startWaveform = [this]() {
        if (isRemoteSkyMode() || !state_->tcp_wave_panel_ ||
            state_->tcp_wave_panel_->isConnected() || state_->tcp_wave_panel_->isConnecting())
        {
            return false;
        }
        state_->tcp_wave_panel_->toggleConnection();
        updateConnectionStatus(anyLocalDeviceConnected());
        return state_->tcp_wave_panel_->isConnected() || state_->tcp_wave_panel_->isConnecting();
    };
    hooks.cancelWaveform = [this]() {
        if (isRemoteSkyMode() || !state_->tcp_wave_panel_)
        {
            return;
        }
        if (state_->tcp_wave_panel_->isConnected() || state_->tcp_wave_panel_->isConnecting())
        {
            state_->tcp_wave_panel_->toggleConnection();
        }
    };
    hooks.stopWaveform = hooks.cancelWaveform;
    hooks.finished = [this](const VaporView::Ground::Devices::LocalConnectionResult& result) {
        QTimer::singleShot(0, this, [this, result]() {
            QTimer::singleShot(0, this, [this, result]() {
                const bool connected = result.connected();
                auto outcomeCode = [](VaporView::Ground::Devices::LocalConnectionOutcome outcome) {
                    using Outcome = VaporView::Ground::Devices::LocalConnectionOutcome;
                    switch (outcome)
                    {
                    case Outcome::Completed: return QStringLiteral("COMPLETED");
                    case Outcome::Failed: return QStringLiteral("FAILED");
                    case Outcome::Cancelled: return QStringLiteral("CANCELLED");
                    case Outcome::TimedOut: return QStringLiteral("TIMED_OUT");
                    case Outcome::Rejected: return QStringLiteral("REJECTED");
                    }
                    return QStringLiteral("UNKNOWN");
                };
                const QString outcome = outcomeCode(result.outcome);
                const bool attention =
                    result.outcome == VaporView::Ground::Devices::LocalConnectionOutcome::Failed ||
                    result.outcome == VaporView::Ground::Devices::LocalConnectionOutcome::TimedOut ||
                    result.outcome == VaporView::Ground::Devices::LocalConnectionOutcome::Rejected;
                publishGroundLog(attention ? VaporView::LogLevel::Warning : VaporView::LogLevel::Info,
                                 QStringLiteral("device.connection"),
                                 QStringLiteral("local_connection_summary"),
                                 QStringLiteral("本地连接流程已结束。"),
                                 {{QStringLiteral("serial_connected"), result.serialConnected},
                                  {QStringLiteral("tcp_wave_connected"), result.waveformConnected},
                                  {QStringLiteral("outcome"), outcome},
                                  {QStringLiteral("ui_visibility"), attention
                                      ? QStringLiteral("attention")
                                      : QStringLiteral("details")}});
                finishConnectionAttempt(connected);
            });
        });
    };
    state_->local_connection_coordinator_->setHooks(std::move(hooks));
}

CollectorSnapshot MainWindow::snapshotCollectors() const
{
    return state_->local_connection_controller_
        ? state_->local_connection_controller_->snapshotCollectors()
        : CollectorSnapshot{};
}

void MainWindow::invalidateTemperatureControllerDataUi()
{
    state_->current_temperature_controller_ = VaporView::TemperatureControllerData();
    if (state_->device_panel_coordinator_)
    {
        state_->device_panel_coordinator_->updateTemperatureRate(0.0);
        state_->device_panel_coordinator_->updateTemperatureData(state_->current_temperature_controller_);
    }
}

void MainWindow::stopAllCollectors()
{
    if (state_->local_connection_controller_)
    {
        state_->local_connection_controller_->disconnect();
    }
    if (state_->ai8_temperature_controller_panel_)
    {
        state_->ai8_temperature_controller_panel_->setBackendConnected(false);
        state_->ai8_temperature_controller_panel_->applyLiveData({});
    }
    updateAi8TemperatureTitleStatus();
}

void MainWindow::finishConnectionAttempt(bool connected)
{
    state_->connection_attempt_in_progress_ = false;
    state_->cancel_connection_requested_.store(false);
    const bool overallConnected = connected || anyLocalDeviceConnected();
    if (!overallConnected && state_->recording_service_->isSessionOpen())
    {
        stopRecording(true);
    }
    updateConnectionStatus(overallConnected);
    bool ai8Connected = false;
    if (state_->ai8_temperature_controller_panel_)
    {
        const CollectorSnapshot collectors = snapshotCollectors();
        ai8Connected = collectors.ai8_temperature_controller &&
                       collectors.ai8_temperature_controller->isRunning();
        const QString detail = ai8Connected
            ? QStringLiteral("%1 @ %2")
                  .arg(localSerialPortComboValue(state_->device_config_.ai8_temperature_port_combo),
                       state_->device_config_.ai8_temperature_baud_combo
                           ? state_->device_config_.ai8_temperature_baud_combo->currentText()
                           : QStringLiteral("19200"))
            : QString();
        state_->ai8_temperature_controller_panel_->setBackendConnected(ai8Connected, detail);
    }
    updateAi8TemperatureTitleStatus();
    if (ai8Connected)
    {
        QTimer::singleShot(0, this, [this]() {
            const CollectorSnapshot collectors = snapshotCollectors();
            if (!state_->ai8_temperature_controller_panel_ ||
                !collectors.ai8_temperature_controller ||
                !collectors.ai8_temperature_controller->isRunning())
            {
                return;
            }
            onAi8ReadPageRequested();
        });
    }
}

void MainWindow::onRefreshPortsClicked()
{
    QStringList ports = isUiTestMode()
        ? QStringList{QStringLiteral("UI-TEST-EPSILON"), QStringLiteral("UI-TEST-PTB"),
                      QStringLiteral("UI-TEST-HMP"), QStringLiteral("UI-TEST-LIDAR"),
                      QStringLiteral("UI-TEST-RD105"), QStringLiteral("UI-TEST-SKY"),
                      QStringLiteral("UI-TEST-AI8")}
        : getAvailablePorts();

    auto updateCombo = [this, &ports](QComboBox* combo) {
        if (!combo)
        {
            return;
        }
        refreshLocalSerialPortComboOptions(combo, ports, localSerialPortComboValue(combo));
    };

    updateCombo(state_->epsilon_port_combo_);
    updateCombo(state_->ptb_port_combo_);
    updateCombo(state_->hmp_port_combo_);
    updateCombo(state_->lidar_port_combo_);
    updateCombo(state_->temperature_port_combo_);
    updateCombo(state_->sky_telemetry_port_combo_);
    updateCombo(state_->device_config_.ai8_temperature_port_combo);
    refreshAi8TemperatureTitlePortOptions(
        ports,
        localSerialPortComboValue(state_->device_config_.ai8_temperature_port_combo));
    syncDeviceConfigPageFromHome();
    updateTemperatureControllerTitleText();
    updateTemperatureTitleButtonsState();

    if (isUiTestMode())
    {
        publishUiTestEvent(QStringLiteral("ui_test_serial_ports_refreshed"),
                           state_->is_english_ ? QStringLiteral("Returned fixed test serial ports")
                                               : QStringLiteral("已返回固定测试串口"));
        return;
    }
    publishGroundLog(VaporView::LogLevel::Info,
                     QStringLiteral("device.connection"),
                     QStringLiteral("serial_ports_refreshed"),
                     QStringLiteral("串口列表已刷新。"),
                     {{QStringLiteral("serial_port_count"), ports.size()},
                      {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
}

void MainWindow::onAutoDetectPortsClicked()
{
    if (isUiTestMode())
    {
        onRefreshPortsClicked();
        const QList<QPair<QComboBox *, QString>> detected{
            {state_->epsilon_port_combo_, QStringLiteral("UI-TEST-EPSILON")},
            {state_->ptb_port_combo_, QStringLiteral("UI-TEST-PTB")},
            {state_->hmp_port_combo_, QStringLiteral("UI-TEST-HMP")},
            {state_->lidar_port_combo_, QStringLiteral("UI-TEST-LIDAR")},
            {state_->temperature_port_combo_, QStringLiteral("UI-TEST-RD105")},
            {state_->device_config_.ai8_temperature_port_combo, QStringLiteral("UI-TEST-AI8")}
        };
        for (const auto& item : detected)
        {
            setLocalSerialPortComboText(item.first, item.second);
        }
        syncDeviceConfigPageFromHome();
        publishUiTestEvent(QStringLiteral("ui_test_auto_detection_completed"),
                           state_->is_english_ ? QStringLiteral("Automatic detection completed with fixed test devices")
                                               : QStringLiteral("自动识别已完成，已填入固定测试设备"),
                           {{QStringLiteral("detected_devices"), detected.size()}});
        return;
    }
    if (state_->port_detection_in_progress_)
    {
        state_->cancel_connection_requested_.store(true);
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.connection"),
                         QStringLiteral("serial_port_detection_cancel_requested"),
                         QStringLiteral("已请求取消，正在停止自动识别串口。"),
                         {{QStringLiteral("reason_code"), QStringLiteral("USER_CANCELLED")},
                          {QStringLiteral("ui_visibility"), QStringLiteral("attention")}});
        updateConnectionStatus(state_->is_connected_);
        QApplication::processEvents(QEventLoop::AllEvents);
        return;
    }

    if (state_->is_connected_ || state_->connection_attempt_in_progress_)
    {
        return;
    }

    if (state_->port_detection_thread_.joinable())
    {
        state_->port_detection_thread_.join();
    }

    onRefreshPortsClicked();
    state_->port_detection_in_progress_ = true;
    state_->cancel_connection_requested_.store(false);
    updateConnectionStatus(state_->is_connected_);
    publishGroundLog(VaporView::LogLevel::Info,
                     QStringLiteral("device.connection"),
                     QStringLiteral("serial_port_detection_started"),
                     QStringLiteral("开始自动识别串口。"),
                     {{QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    const QString selectedEpsilonPort = localSerialPortComboValue(state_->epsilon_port_combo_);
    const QString selectedPtbPort = localSerialPortComboValue(state_->ptb_port_combo_);
    const QString selectedHmpPort = localSerialPortComboValue(state_->hmp_port_combo_);
    const QString selectedLidarPort = localSerialPortComboValue(state_->lidar_port_combo_);
    const QString selectedTemperaturePort = localSerialPortComboValue(state_->temperature_port_combo_);
    const QString selectedAi8TemperaturePort = localSerialPortComboValue(
        state_->device_config_.ai8_temperature_port_combo);
    const QString selectedEpsilonBaud = state_->epsilon_baud_combo_ ? state_->epsilon_baud_combo_->currentText().trimmed() : QStringLiteral("921600");
    const QString selectedPtbBaud = state_->ptb_baud_combo_ ? state_->ptb_baud_combo_->currentText().trimmed() : QStringLiteral("9600");
    const QString selectedHmpBaud = state_->hmp_baud_combo_ ? state_->hmp_baud_combo_->currentText().trimmed() : QStringLiteral("19200");
    const QString selectedLidarBaud = state_->lidar_baud_combo_ ? state_->lidar_baud_combo_->currentText().trimmed() : QStringLiteral("500000");
    const QString selectedTemperatureBaud = state_->temperature_baud_combo_ ? state_->temperature_baud_combo_->currentText().trimmed() : QStringLiteral("38400");
    const QString selectedAi8TemperatureBaud = state_->device_config_.ai8_temperature_baud_combo
        ? state_->device_config_.ai8_temperature_baud_combo->currentText().trimmed()
        : QStringLiteral("19200");
    const bool english = state_->is_english_;

    VaporView::Ground::Devices::SerialPortDetectionRequest request;
    request.english = english;
    request.epsilon = {selectedEpsilonPort, selectedEpsilonBaud};
    request.ptb = {selectedPtbPort, selectedPtbBaud};
    request.hmp = {selectedHmpPort, selectedHmpBaud};
    request.lidar = {selectedLidarPort, selectedLidarBaud};
    request.temperatureController = {selectedTemperaturePort, selectedTemperatureBaud};
    request.ai8TemperatureController = {selectedAi8TemperaturePort, selectedAi8TemperatureBaud};
    request.temperatureSlaveAddress = rememberedTemperatureSlaveAddress();
    request.ai8SlaveAddress = state_->ai8_temperature_controller_panel_
        ? state_->ai8_temperature_controller_panel_->currentPageData().global.address
        : 1;

    state_->port_detection_thread_ = std::thread([this, request = std::move(request)]() mutable {
        request.availablePorts =
            VaporView::Ground::Devices::SerialPortDetectionService::availablePorts();
        const auto outcome =
            VaporView::Ground::Devices::SerialPortDetectionService::detect(
                request,
                [this]() { return state_->cancel_connection_requested_.load(); },
                [this](const VaporView::Ground::Devices::SerialPortDetectionService::LogEntry& entry) {
                    QMetaObject::invokeMethod(this, [this, entry]() {
                        publishGroundLog(entry.level,
                                         QStringLiteral("device.connection"),
                                         entry.event,
                                         entry.message,
                                         entry.fields);
                    }, Qt::QueuedConnection);
                });
        QMetaObject::invokeMethod(this, [this, detections = outcome.detections]() {
            const QString selectText = state_->is_english_ ? "-- Select --" : "未选择";
            auto applySelection = [this, &selectText](QComboBox *combo, const QString& value) {
                if (!combo)
                {
                    return;
                }
                if (value != selectText)
                {
                    VaporView::rememberSerialPort(value);
                }
                setLocalSerialPortComboText(combo, value == selectText ? QString() : value);
            };
            auto normalizePort = [&selectText](const QString& value) {
                return (value.isEmpty() || value == selectText) ? selectText : value;
            };

            QHash<QString, QComboBox *> portCombos{
                {QStringLiteral("epsilon"), state_->epsilon_port_combo_},
                {QStringLiteral("ptb"), state_->ptb_port_combo_},
                {QStringLiteral("hmp"), state_->hmp_port_combo_},
                {QStringLiteral("lidar"), state_->lidar_port_combo_},
                {QStringLiteral("temperature"), state_->temperature_port_combo_},
                {QStringLiteral("ai8"), state_->device_config_.ai8_temperature_port_combo},
            };
            QHash<QString, QComboBox *> baudCombos{
                {QStringLiteral("epsilon"), state_->epsilon_baud_combo_},
                {QStringLiteral("ptb"), state_->ptb_baud_combo_},
                {QStringLiteral("hmp"), state_->hmp_baud_combo_},
                {QStringLiteral("lidar"), state_->lidar_baud_combo_},
                {QStringLiteral("temperature"), state_->temperature_baud_combo_},
                {QStringLiteral("ai8"), state_->device_config_.ai8_temperature_baud_combo},
            };
            QHash<QString, QString> detectedPorts;
            QHash<QString, QString> detectedBauds;
            QSet<QString> detectedKeys;
            QSet<QString> detectedPortNames;
            for (const auto& detection : detections)
            {
                const QString port = normalizePort(detection.port);
                if (port == selectText || !portCombos.contains(detection.deviceKey))
                {
                    continue;
                }
                detectedPorts[detection.deviceKey] = port;
                detectedBauds[detection.deviceKey] = detection.baud;
                detectedKeys.insert(detection.deviceKey);
                detectedPortNames.insert(port);
            }
            for (auto it = portCombos.cbegin(); it != portCombos.cend(); ++it)
            {
                const QString current = normalizePort(it.value() ? it.value()->currentText() : QString());
                if (!detectedKeys.contains(it.key()) && detectedPortNames.contains(current))
                {
                    applySelection(it.value(), selectText);
                }
            }
            for (const auto& detection : detections)
            {
                const QString port = detectedPorts.value(detection.deviceKey);
                if (port.isEmpty())
                {
                    continue;
                }
                applySelection(portCombos.value(detection.deviceKey), port);
                if (QComboBox *baud = baudCombos.value(detection.deviceKey, nullptr))
                {
                    baud->setCurrentText(detectedBauds.value(detection.deviceKey));
                }
            }

            state_->port_detection_in_progress_ = false;
            state_->cancel_connection_requested_.store(false);
            updateConnectionStatus(state_->is_connected_);
        }, Qt::QueuedConnection);
    });
}
void MainWindow::onConnectClicked()
{
    if (isUiTestMode())
    {
        if (state_->ui_test_connection_in_progress_)
        {
            return;
        }
        state_->ui_test_connection_in_progress_ = true;
        if (state_->tcp_wave_panel_) state_->tcp_wave_panel_->setUiTestConnected(false);
        state_->ui_test_model_->setAllDevicesConnected(false);
        for (VaporView::SkyDeviceId device : {VaporView::SkyDeviceId::Epsilon,
                                              VaporView::SkyDeviceId::Ptb,
                                              VaporView::SkyDeviceId::Hmp,
                                              VaporView::SkyDeviceId::Lidar,
                                              VaporView::SkyDeviceId::TemperatureController,
                                              VaporView::SkyDeviceId::WaveTcp})
        {
            state_->ui_test_model_->setDeviceState(device, VaporView::DeviceState::Connecting);
        }
        updateConnectionStatus(false);
        publishUiTestEvent(QStringLiteral("ui_test_connection_started"),
                           state_->is_english_ ? QStringLiteral("Simulated connection started")
                                               : QStringLiteral("模拟连接已开始"));
        QTimer::singleShot(500, this, [this]() {
            if (!isUiTestMode() || !state_->ui_test_connection_in_progress_)
            {
                return;
            }
            state_->ui_test_connection_in_progress_ = false;
            state_->ui_test_model_->setAllDevicesConnected(true);
            if (state_->tcp_wave_panel_) state_->tcp_wave_panel_->setUiTestConnected(true);
            updateConnectionStatus(false);
            publishUiTestEvent(QStringLiteral("ui_test_all_devices_connected"),
                               state_->is_english_ ? QStringLiteral("All simulated devices connected")
                                                   : QStringLiteral("所有模拟设备已连接"));
        });
        return;
    }
    if (isRemoteSkyMode())
    {
        clearRemoteSkyDataUi();
        const bool tcpTelemetry = isRemoteSkyTcpMode();
        bool opened = false;
        QString openedText;
        if (tcpTelemetry)
        {
            const QString host = state_->sky_telemetry_tcp_host_edit_ ? state_->sky_telemetry_tcp_host_edit_->text().trimmed() : QString();
            const int tcpPort = state_->sky_telemetry_tcp_port_spin_ ? state_->sky_telemetry_tcp_port_spin_->value() : 39100;
            if (host.isEmpty())
            {
                publishGroundLog(VaporView::LogLevel::Warning,
                                 QStringLiteral("telemetry.connection"),
                                 QStringLiteral("remote_sky_connection_rejected_missing_host"),
                                 QStringLiteral("请先输入天空端数传 IP。"),
                                 {{QStringLiteral("reason_code"), QStringLiteral("MISSING_ENDPOINT")},
                                  {QStringLiteral("transport"), QStringLiteral("tcp")},
                                  {QStringLiteral("ui_dedupe_key"), QStringLiteral("remote_sky:tcp:missing_host")}});
                return;
            }
            openedText = QStringLiteral("%1:%2").arg(host).arg(tcpPort);
            publishGroundLog(VaporView::LogLevel::Info,
                             QStringLiteral("telemetry.connection"),
                             QStringLiteral("remote_sky_connection_started"),
                             QStringLiteral("正在连接天空端 TCP 数传。"),
                             {{QStringLiteral("transport"), QStringLiteral("tcp")},
                              {QStringLiteral("endpoint"), openedText},
                              {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
            opened = state_->remote_sky_controller_ && state_->remote_sky_controller_->openTcp(host, static_cast<quint16>(tcpPort));
        }
        else
        {
            const QString port = localSerialPortComboValue(state_->sky_telemetry_port_combo_);
            const int baud = state_->sky_telemetry_baud_combo_ ? state_->sky_telemetry_baud_combo_->currentText().toInt() : 921600;
            if (port.isEmpty())
            {
                publishGroundLog(VaporView::LogLevel::Warning,
                                 QStringLiteral("telemetry.connection"),
                                 QStringLiteral("remote_sky_connection_rejected_missing_port"),
                                 QStringLiteral("请先选择天空端数传串口。"),
                                 {{QStringLiteral("reason_code"), QStringLiteral("MISSING_ENDPOINT")},
                                  {QStringLiteral("transport"), QStringLiteral("serial")},
                                  {QStringLiteral("ui_dedupe_key"), QStringLiteral("remote_sky:serial:missing_port")}});
                return;
            }
            openedText = QStringLiteral("%1 @ %2").arg(port).arg(baud);
            publishGroundLog(VaporView::LogLevel::Info,
                             QStringLiteral("telemetry.connection"),
                             QStringLiteral("remote_sky_connection_started"),
                             QStringLiteral("正在打开天空端数传串口。"),
                             {{QStringLiteral("transport"), QStringLiteral("serial")},
                              {QStringLiteral("endpoint"), openedText},
                              {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
            opened = state_->remote_sky_controller_ && state_->remote_sky_controller_->open(port, baud);
        }
        if (opened)
        {
            updateConnectionStatus(true);
            state_->remote_sky_controller_->sendCommand(VaporView::CommandId::DisableWaveformStreaming);
            state_->remote_sky_controller_->sendCommand(VaporView::CommandId::RequestStatus);
            publishGroundLog(VaporView::LogLevel::Info,
                             QStringLiteral("telemetry.connection"),
                             QStringLiteral("remote_sky_connection_opened"),
                             QStringLiteral("数传链路已打开，正在等待天空端握手。"),
                             {{QStringLiteral("endpoint"), openedText},
                              {QStringLiteral("transport"), tcpTelemetry ? QStringLiteral("tcp") : QStringLiteral("serial")},
                              {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
        }
        else
        {
            updateConnectionStatus(false);
            publishGroundLog(VaporView::LogLevel::Error,
                             QStringLiteral("telemetry.connection"),
                             QStringLiteral("remote_sky_connection_open_failed"),
                             QStringLiteral("打开天空端数传链路失败。"),
                             {{QStringLiteral("error_code"), QStringLiteral("TELEMETRY_LINK_OPEN_FAILED")},
                              {QStringLiteral("endpoint"), openedText},
                              {QStringLiteral("transport"), tcpTelemetry ? QStringLiteral("tcp") : QStringLiteral("serial")},
                              {QStringLiteral("ui_dedupe_key"), QStringLiteral("remote_sky:connection_open_failed")}});
        }
        return;
    }

    state_->connection_attempt_in_progress_ = true;
    state_->cancel_connection_requested_.store(false);
    updateConnectionStatus(false);

    publishGroundLog(VaporView::LogLevel::Info,
                     QStringLiteral("device.connection"),
                     QStringLiteral("device_connection_started"),
                     QStringLiteral("正在连接本地设备。"),
                     {{QStringLiteral("ui_visibility"), QStringLiteral("details")}});

    state_->current_epsilon_ = VaporView::EpsilonData();
    state_->current_gnss_ = VaporView::GnssData();
    state_->current_imu_ = VaporView::ImuData();
    state_->current_ptb_ = VaporView::PtbData();
    state_->current_hmp_ = VaporView::HmpData();
    state_->current_lidar_ = VaporView::LidarData();
    state_->current_temperature_controller_ = VaporView::TemperatureControllerData();

    if (state_->device_panel_coordinator_)
    {
        state_->device_panel_coordinator_->updateAllData(
            state_->current_epsilon_, state_->current_gnss_, 0,
            state_->current_imu_, 0, state_->current_ptb_, state_->current_hmp_,
            state_->current_lidar_, state_->current_temperature_controller_);
        state_->device_panel_coordinator_->clearRates();
    }
    updateEnvironmentStatusIcons(false, false, false);

    const bool english = state_->is_english_;
    const QString selectText = english ? "-- Select --" : "未选择";
    const QString epsilonPort = localSerialPortComboValue(state_->epsilon_port_combo_);
    const QString ptbPort = localSerialPortComboValue(state_->ptb_port_combo_);
    const QString hmpPort = localSerialPortComboValue(state_->hmp_port_combo_);
    const QString lidarPort = localSerialPortComboValue(state_->lidar_port_combo_);
    const QString temperaturePort = localSerialPortComboValue(state_->temperature_port_combo_);
    const QString ai8TemperaturePort = localSerialPortComboValue(
        state_->device_config_.ai8_temperature_port_combo);
    const QString epsilonBaudText = state_->epsilon_baud_combo_ ? state_->epsilon_baud_combo_->currentText().trimmed() : QStringLiteral("921600");
    const QString ptbBaudText = state_->ptb_baud_combo_->currentText();
    const QString hmpBaudText = state_->hmp_baud_combo_->currentText();
    const QString lidarBaudText = state_->lidar_baud_combo_->currentText();
    const QString temperatureBaudText = state_->temperature_baud_combo_ ? state_->temperature_baud_combo_->currentText().trimmed() : QStringLiteral("38400");
    const QString ai8TemperatureBaudText = state_->device_config_.ai8_temperature_baud_combo
        ? state_->device_config_.ai8_temperature_baud_combo->currentText().trimmed()
        : QStringLiteral("19200");
    const QString epsilonRateText = state_->epsilon_rate_combo_ ? state_->epsilon_rate_combo_->currentText() : QStringLiteral("100");
    const QString ptbRateText = state_->ptb_rate_combo_ ? state_->ptb_rate_combo_->currentText() : QStringLiteral("20");
    const QString hmpRateText = state_->hmp_rate_combo_ ? state_->hmp_rate_combo_->currentText() : QStringLiteral("20");
    const VaporView::PressureSensorProtocol pressureProtocol =
        state_->device_config_.ptb_source_combo &&
            state_->device_config_.ptb_source_combo->currentData().toString() == QStringLiteral("bmp390")
        ? VaporView::PressureSensorProtocol::Bmp390Serial
        : VaporView::PressureSensorProtocol::Ptb210;
    const VaporView::HumiditySensorProtocol humidityProtocol =
        state_->device_config_.hmp_source_combo &&
            state_->device_config_.hmp_source_combo->currentData().toString() == QStringLiteral("sht45")
        ? VaporView::HumiditySensorProtocol::Sht45Serial
        : VaporView::HumiditySensorProtocol::Hmp3Modbus;
    const QString lidarRateText = state_->lidar_rate_combo_ ? state_->lidar_rate_combo_->currentText() : QStringLiteral("100");
    const QString temperatureRateText = state_->temperature_rate_combo_ ? state_->temperature_rate_combo_->currentText() : QString::number(kDefaultTemperatureSampleRateHz);
    const QString ai8TemperatureRateText = state_->device_config_.ai8_temperature_rate_combo
        ? state_->device_config_.ai8_temperature_rate_combo->currentText()
        : QStringLiteral("5");
    const bool skipEpsilonDeviceRate = isRateUnspecified(epsilonRateText);
    const bool skipPtbDeviceRate = isRateUnspecified(ptbRateText);
    const bool skipHmpDeviceRate = isRateUnspecified(hmpRateText);
    const bool skipLidarDeviceRate = isRateUnspecified(lidarRateText);
    const bool skipTemperatureDeviceRate = isRateUnspecified(temperatureRateText);
    const int epsilonRate = effectiveRateOrDefault(epsilonRateText, kDefaultEpsilonSampleRateHz, 200);
    const int ptbRate = clampPtbSampleRate(effectiveRateOrDefault(ptbRateText, kDefaultPtbSampleRateHz, kPtbMaxSampleRateHz));
    const int hmpRate = effectiveRateOrDefault(hmpRateText, kDefaultHmpSampleRateHz);
    const int lidarRate = effectiveRateOrDefault(lidarRateText, kDefaultLidarSampleRateHz, 100);
    const int temperatureRate = effectiveRateOrDefault(temperatureRateText, kDefaultTemperatureSampleRateHz, kMaxTemperatureSampleRateHz);
    const int ai8TemperatureRate = effectiveRateOrDefault(ai8TemperatureRateText, 5, 20);
    QSettings settings = VaporView::applicationConfigSettings();
    settings.beginGroup(QStringLiteral("MainWindow"));
    const std::map<uint8_t, int> epsilonDesiredPacketRates =
        effectiveEpsilonPacketRates(settings);
    if (!skipEpsilonDeviceRate &&
        !epsilonPort.isEmpty() &&
        epsilonPort != selectText &&
        !validateEpsilonPacketBandwidth(epsilonDesiredPacketRates, epsilonBaudText, true))
    {
        state_->connection_attempt_in_progress_ = false;
        updateConnectionStatus(anyCollectorRunning());
        return;
    }
    const int epsilonCallbackRate = epsilonPacketCallbackRate(epsilonDesiredPacketRates, epsilonRate);
    const QString epsilonDesiredPacketSignature = epsilonPacketRatesSignature(epsilonDesiredPacketRates);
    const QString epsilonDesiredPacketSummary = epsilonPacketRatesSummary(epsilonDesiredPacketRates);
    const bool epsilonConfigLikelyMatches =
        !epsilonPort.isEmpty() &&
        epsilonPort != selectText &&
        settings.value("epsilon_last_config_apply_version").toInt() ==
            VaporView::Ground::EpsilonConfigurationService::PacketConfigurationVersion &&
        settings.value("epsilon_last_config_port").toString() == epsilonPort &&
        settings.value("epsilon_last_config_baud").toString() == epsilonBaudText &&
        settings.value("epsilon_last_config_signature").toString() == epsilonDesiredPacketSignature;
    state_->epsilon_sample_rate_ = epsilonRate;
    state_->ptb_sample_rate_ = ptbRate;
    state_->hmp_sample_rate_ = hmpRate;
    state_->lidar_sample_rate_ = lidarRate;
    state_->temperature_sample_rate_ = temperatureRate;

    stopAllCollectors();

    VaporView::Ground::Devices::LocalConnectionRequest request;
    request.english = english;
    request.selectText = selectText;
    request.epsilon = {epsilonPort, epsilonBaudText, epsilonCallbackRate, skipEpsilonDeviceRate};
    request.ptb = {ptbPort, ptbBaudText, ptbRate, skipPtbDeviceRate};
    request.hmp = {hmpPort, hmpBaudText, hmpRate, skipHmpDeviceRate};
    request.lidar = {lidarPort, lidarBaudText, lidarRate, skipLidarDeviceRate};
    request.temperatureController = {
        temperaturePort,
        temperatureBaudText,
        temperatureRate,
        skipTemperatureDeviceRate
    };
    request.ai8TemperatureController = {
        ai8TemperaturePort,
        ai8TemperatureBaudText,
        ai8TemperatureRate,
        false
    };
    request.pressureProtocol = pressureProtocol;
    request.humidityProtocol = humidityProtocol;
    request.temperatureSlaveAddress = rememberedTemperatureSlaveAddress();
    request.ai8SlaveAddress = state_->ai8_temperature_controller_panel_
        ? state_->ai8_temperature_controller_panel_->currentPageData().global.address
        : 1;
    request.epsilonPacketRates = epsilonDesiredPacketRates;
    request.epsilonConfiguredRateHz = epsilonRate;
    request.epsilonPacketRateSignature = epsilonDesiredPacketSignature;
    request.epsilonPacketRateSummary = epsilonDesiredPacketSummary;
    request.epsilonConfigLikelyMatches = epsilonConfigLikelyMatches;
    if (!state_->local_connection_coordinator_->begin(std::move(request)))
    {
        state_->connection_attempt_in_progress_ = false;
        publishGroundLog(VaporView::LogLevel::Warning,
                         QStringLiteral("device.connection"),
                         QStringLiteral("device_connection_rejected_busy"),
                         QStringLiteral("已有设备连接流程正在进行。"),
                         {{QStringLiteral("reason_code"), QStringLiteral("INVALID_STATE")},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("device_connection:busy")}});
        updateConnectionStatus(anyCollectorRunning());
        return;
    }
}
void MainWindow::onDisconnectClicked()
{
    if (isUiTestMode())
    {
        state_->ui_test_connection_in_progress_ = false;
        state_->ui_test_model_->setAllDevicesConnected(false);
        if (state_->tcp_wave_panel_) state_->tcp_wave_panel_->setUiTestConnected(false);
        resetUiTestRecording();
        updateConnectionStatus(false);
        updateRecordingStatusLabel();
        publishUiTestEvent(QStringLiteral("ui_test_all_devices_disconnected"),
                           state_->is_english_ ? QStringLiteral("All simulated devices disconnected")
                                               : QStringLiteral("所有模拟设备已断开"));
        return;
    }
    if (isRemoteSkyMode())
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("telemetry.connection"),
                         QStringLiteral("remote_sky_disconnection_started"),
                         QStringLiteral("正在断开天空端数传。"),
                         {{QStringLiteral("ui_visibility"), QStringLiteral("details")}});
        if (state_->remote_sky_controller_)
        {
            state_->remote_sky_controller_->close();
        }
        state_->remote_recording_state_ = 0;
        clearRemoteSkyDataUi();
        updateConnectionStatus(false);
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("telemetry.connection"),
                         QStringLiteral("remote_sky_disconnected"),
                         QStringLiteral("天空端数传已断开。"),
                         {{QStringLiteral("ui_visibility"), QStringLiteral("details")}});
        return;
    }

    publishGroundLog(VaporView::LogLevel::Info,
                     QStringLiteral("device.connection"),
                     QStringLiteral("device_disconnection_started"),
                     QStringLiteral("正在断开本地设备。"),
                     {{QStringLiteral("ui_visibility"), QStringLiteral("details")}});

    stopRecording(true);
    if (state_->local_connection_coordinator_)
    {
        state_->local_connection_coordinator_->disconnect();
    }
    stopAllCollectors();
    invalidateTemperatureControllerDataUi();
    finishConnectionAttempt(false);
    publishGroundLog(VaporView::LogLevel::Info,
                     QStringLiteral("device.connection"),
                     QStringLiteral("local_device_disconnected"),
                     QStringLiteral("本地设备已断开。"),
                     {{QStringLiteral("ui_visibility"), QStringLiteral("details")}});
}

void MainWindow::onCancelConnectClicked()
{
    if (isUiTestMode())
    {
        if (!state_->ui_test_connection_in_progress_)
        {
            return;
        }
        state_->ui_test_connection_in_progress_ = false;
        state_->ui_test_model_->setAllDevicesConnected(false);
        if (state_->tcp_wave_panel_) state_->tcp_wave_panel_->setUiTestConnected(false);
        updateConnectionStatus(false);
        publishUiTestEvent(QStringLiteral("ui_test_connection_cancelled"),
                           state_->is_english_ ? QStringLiteral("Simulated connection canceled")
                                               : QStringLiteral("模拟连接已取消"),
                           {{QStringLiteral("reason_code"), QStringLiteral("USER_CANCELLED")}});
        return;
    }
    if (!state_->connection_attempt_in_progress_ || !state_->local_connection_coordinator_)
    {
        return;
    }

    state_->cancel_connection_requested_.store(true);
    state_->local_connection_coordinator_->cancel();
    publishGroundLog(VaporView::LogLevel::Info,
                     QStringLiteral("device.connection"),
                     QStringLiteral("device_connection_cancel_requested"),
                     QStringLiteral("已请求取消，正在停止连接流程。"),
                     {{QStringLiteral("reason_code"), QStringLiteral("USER_CANCELLED")},
                      {QStringLiteral("ui_visibility"), QStringLiteral("attention")}});
    QApplication::processEvents(QEventLoop::AllEvents);
}

void MainWindow::onGnssDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.gnss)
    {
        state_->current_gnss_ = collectors.gnss->getLatestData();
    }
}

void MainWindow::onEpsilonDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.epsilon)
    {
        state_->current_epsilon_ = collectors.epsilon->getLatestData();
    }
#ifdef VAPORVIEW_HAS_OSGEARTH
    maybeForwardMap3DSample(
        state_->current_epsilon_,
        VaporView::Ground::Session::GroundRecordingService::currentTimestampUs());
#endif
}

void MainWindow::onImuDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.imu)
    {
        state_->current_imu_ = collectors.imu->getLatestData();
    }
}

void MainWindow::onPtbDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.ptb)
    {
        state_->current_ptb_ = collectors.ptb->getLatestData();
    }
}

void MainWindow::onHmpDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.hmp)
    {
        state_->current_hmp_ = collectors.hmp->getLatestData();
    }
}

void MainWindow::onLidarDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.lidar)
    {
        state_->current_lidar_ = collectors.lidar->getLatestData();
    }
}

void MainWindow::onTemperatureControllerDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.temperature_controller)
    {
        const VaporView::TemperatureControllerData latest = collectors.temperature_controller->getLatestData();
        if (latest.timestamp < state_->current_temperature_controller_.timestamp)
        {
            return;
        }
        state_->current_temperature_controller_ = latest;
    }
}

void MainWindow::onAi8TemperatureControllerDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.ai8_temperature_controller && state_->ai8_temperature_controller_panel_)
    {
        state_->ai8_temperature_controller_panel_->applyLiveData(
            collectors.ai8_temperature_controller->getLatestData());
    }
    updateAi8TemperatureTitleStatus();
}

void MainWindow::onAi8ReadPageRequested()
{
    if (!state_->ai8_temperature_controller_panel_ || !state_->local_connection_controller_)
    {
        return;
    }
    const auto requested = state_->ai8_temperature_controller_panel_->currentPageData();
    state_->ai8_temperature_controller_panel_->setOperationStatus(
        state_->is_english_ ? QStringLiteral("Reading current page...")
                            : QStringLiteral("正在读取当前页…"),
        true);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    const auto result = state_->local_connection_controller_->readAi8Page(
        requested.page,
        requested.selection);
    if (result.success)
    {
        state_->ai8_temperature_controller_panel_->applyPageData(result.data);
    }
    state_->ai8_temperature_controller_panel_->setOperationStatus(result.message, result.success);
    QVariantMap fields{{QStringLiteral("device"), QStringLiteral("AI-8288")},
                       {QStringLiteral("device_id"), QStringLiteral("ai8_temperature_controller")},
                       {QStringLiteral("page"), static_cast<int>(requested.page)},
                       {QStringLiteral("channel"), requested.selection.channel},
                       {QStringLiteral("input_group"), requested.selection.inputGroup},
                       {QStringLiteral("output_group"), requested.selection.outputGroup},
                       {QStringLiteral("ui_visibility"), result.success ? QStringLiteral("details") : QStringLiteral("attention")},
                       {QStringLiteral("details"), result.message}};
    if (!result.success)
    {
        fields.insert(QStringLiteral("error_code"), QStringLiteral("AI8_PAGE_READ_FAILED"));
        fields.insert(QStringLiteral("ui_dedupe_key"), QStringLiteral("ai8:read_page:failed"));
    }
    if (result.success)
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.temperature.command"),
                         QStringLiteral("ai8_page_read_completed"),
                         QStringLiteral("AI-8288 参数页读取完成。"),
                         fields);
    }
    else
    {
        publishGroundLog(VaporView::LogLevel::Error,
                         QStringLiteral("device.temperature.command"),
                         QStringLiteral("ai8_page_read_failed"),
                         QStringLiteral("AI-8288 参数页读取失败。"),
                         fields);
    }
}

void MainWindow::onAi8WritePageRequested()
{
    if (!state_->ai8_temperature_controller_panel_ || !state_->local_connection_controller_)
    {
        return;
    }
    const auto requested = state_->ai8_temperature_controller_panel_->currentPageData();
    state_->ai8_temperature_controller_panel_->setOperationStatus(
        state_->is_english_ ? QStringLiteral("Writing and reading back...")
                            : QStringLiteral("正在写入并回读确认…"),
        true);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    const auto result = state_->local_connection_controller_->writeAi8Page(requested);
    if (result.success)
    {
        state_->ai8_temperature_controller_panel_->applyPageData(result.data);
    }
    state_->ai8_temperature_controller_panel_->setOperationStatus(result.message, result.success);
    QVariantMap fields{{QStringLiteral("device"), QStringLiteral("AI-8288")},
                       {QStringLiteral("device_id"), QStringLiteral("ai8_temperature_controller")},
                       {QStringLiteral("page"), static_cast<int>(requested.page)},
                       {QStringLiteral("channel"), requested.selection.channel},
                       {QStringLiteral("input_group"), requested.selection.inputGroup},
                       {QStringLiteral("output_group"), requested.selection.outputGroup},
                       {QStringLiteral("ui_visibility"), result.success ? QStringLiteral("details") : QStringLiteral("attention")},
                       {QStringLiteral("details"), result.message}};
    if (!result.success)
    {
        fields.insert(QStringLiteral("error_code"), QStringLiteral("AI8_PAGE_WRITE_FAILED"));
        fields.insert(QStringLiteral("ui_dedupe_key"), QStringLiteral("ai8:write_page:failed"));
    }
    if (result.success)
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.temperature.command"),
                         QStringLiteral("ai8_page_write_completed"),
                         QStringLiteral("AI-8288 参数页写入完成。"),
                         fields);
    }
    else
    {
        publishGroundLog(VaporView::LogLevel::Error,
                         QStringLiteral("device.temperature.command"),
                         QStringLiteral("ai8_page_write_failed"),
                         QStringLiteral("AI-8288 参数页写入失败。"),
                         fields);
    }
}

void MainWindow::onRefreshTimer()
{
    if (isUiTestMode())
    {
        applyUiTestSnapshot();
        return;
    }
    if (isRemoteSkyMode())
    {
        refreshRemoteSkyDataUi();
        return;
    }

    const CollectorSnapshot collectors = snapshotCollectors();

    if (state_->device_panel_coordinator_)
    {
        state_->device_panel_coordinator_->updateEnvironmentData(
            state_->current_epsilon_, state_->current_ptb_, state_->current_hmp_, state_->current_lidar_);
    }
    updateEnvironmentStatusIcons(state_->current_lidar_.valid, state_->current_ptb_.valid, state_->current_hmp_.valid);

    DevicePanelRates panelRates;
    if (collectors.ptb)
    {
        panelRates.ptbHz = collectors.ptb->getActualRate();
    }
    if (collectors.hmp)
    {
        panelRates.hmpHz = collectors.hmp->getActualRate();
    }
    if (collectors.lidar)
    {
        panelRates.lidarHz = collectors.lidar->getActualRate();
    }
    if (collectors.epsilon)
    {
        panelRates.epsilonHz = collectors.epsilon->getActualRate();
    }
    if (collectors.gnss)
    {
        panelRates.gnssHz = collectors.gnss->getActualRate();
    }
    if (collectors.imu)
    {
        panelRates.imuHz = collectors.imu->getActualRate();
    }
    if (collectors.temperature_controller)
    {
        panelRates.temperatureHz = collectors.temperature_controller->getActualRate();
    }
    if (state_->device_panel_coordinator_)
    {
        state_->device_panel_coordinator_->updateRates(panelRates);
        state_->device_panel_coordinator_->updateTemperatureData(state_->current_temperature_controller_);
    }
    updateHomeDeviceStatusCapsules();
}
