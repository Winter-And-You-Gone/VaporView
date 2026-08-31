#include "ground/main/GroundMainWindowImplementation.h"
#include "ground/devices/DeviceRatePolicy.h"
#include "ground/widgets/SerialPortComboSupport.h"

#include <QJsonArray>

namespace
{

constexpr const char *kHomeDeviceIconKeyProperty = "_vv_home_device_icon_key";

bool setDynamicPropertyIfChanged(QObject *object, const char *name, const QVariant& value)
{
    if (!object || object->property(name) == value)
    {
        return false;
    }
    object->setProperty(name, value);
    return true;
}

void setLabelTextIfChanged(QLabel *label, const QString& text)
{
    if (label && label->text() != text)
    {
        label->setText(text);
    }
}

void setWidgetToolTipIfChanged(QWidget *widget, const QString& text)
{
    if (widget && widget->toolTip() != text)
    {
        widget->setToolTip(text);
    }
}

}  // namespace

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
        if (state_->device_config_.auto_detect_ports_btn)
        {
            state_->device_config_.auto_detect_ports_btn->setEnabled(!state_->ui_test_connection_in_progress_);
            state_->device_config_.auto_detect_ports_btn->setText(state_->is_english_ ? QStringLiteral("Auto Detect Ports")
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
    if (state_->device_config_.auto_detect_ports_btn)
    {
        const bool remoteDetectionAvailable = isRemoteSkyMode() &&
            (isUiTestMode() || (state_->remote_sky_controller_ && state_->remote_sky_controller_->isOpen()));
        state_->device_config_.auto_detect_ports_btn->setEnabled(isRemoteSkyMode()
            ? remoteDetectionAvailable
            : (!connected && !state_->connection_attempt_in_progress_ && !state_->epsilon_reconfigure_in_progress_));
        state_->device_config_.auto_detect_ports_btn->setText(state_->port_detection_in_progress_
            ? (state_->is_english_ ? "Cancel Auto Detect" : "取消自动识别")
            : (state_->is_english_ ? "Auto Detect Ports" : "自动识别串口"));
        state_->device_config_.auto_detect_ports_btn->setToolTip(state_->port_detection_in_progress_
            ? (state_->is_english_ ? "Stop the current serial-port detection task." : "停止当前串口自动识别任务。")
            : (state_->is_english_ ? "Probe available serial ports and automatically assign detected devices."
                           : "扫描可用串口，并将识别出的设备自动填入对应端口。"));
    }

    for (QComboBox *combo : {state_->device_config_.epsilon_port_combo,
                             state_->device_config_.ptb_port_combo,
                             state_->device_config_.hmp_port_combo,
                             state_->device_config_.lidar_port_combo,
                             state_->device_config_.temperature_port_combo,
                             state_->device_config_.ai8_temperature_port_combo})
    {
        if (combo) combo->setEnabled(inputsEnabled);
    }
    updateTemperatureControllerTitleText();
    for (QComboBox *combo : {state_->device_config_.epsilon_baud_combo,
                             state_->device_config_.ptb_baud_combo,
                             state_->device_config_.hmp_baud_combo,
                             state_->device_config_.lidar_baud_combo,
                             state_->device_config_.temperature_baud_combo,
                             state_->device_config_.ai8_temperature_baud_combo})
    {
        if (combo) combo->setEnabled(inputsEnabled);
    }
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

bool MainWindow::localDeviceEnabled(VaporView::SkyDeviceId device) const
{
    if (isUiTestMode() || isRemoteSkyMode() || device == VaporView::SkyDeviceId::WaveTcp)
    {
        return true;
    }

    switch (device)
    {
    case VaporView::SkyDeviceId::Epsilon:
        return state_->local_device_config_.epsilon.enabled;
    case VaporView::SkyDeviceId::Ptb:
        return state_->local_device_config_.ptb.enabled;
    case VaporView::SkyDeviceId::Hmp:
        return state_->local_device_config_.hmp.enabled;
    case VaporView::SkyDeviceId::Lidar:
        return state_->local_device_config_.lidar.enabled;
    case VaporView::SkyDeviceId::TemperatureController:
        return state_->local_device_config_.temperatureController.enabled;
    case VaporView::SkyDeviceId::Ai8TemperatureController:
        return state_->local_device_config_.ai8TemperatureController.enabled;
    case VaporView::SkyDeviceId::All:
    case VaporView::SkyDeviceId::WaveTcp:
        return true;
    }
    return true;
}

bool MainWindow::homeDevicePortSelected(VaporView::SkyDeviceId device) const
{
    if (device == VaporView::SkyDeviceId::WaveTcp)
    {
        return state_->tcp_wave_panel_ != nullptr;
    }

    auto portSelected = [](const QString& text, bool manualEntry) {
        return !manualEntry && !text.isEmpty() && !text.startsWith(QStringLiteral("--"));
    };

    switch (device)
    {
    case VaporView::SkyDeviceId::Epsilon:
        return portSelected(state_->local_device_config_.epsilon.port, false);
    case VaporView::SkyDeviceId::Ptb:
        return portSelected(state_->local_device_config_.ptb.port, false);
    case VaporView::SkyDeviceId::Hmp:
        return portSelected(state_->local_device_config_.hmp.port, false);
    case VaporView::SkyDeviceId::Lidar:
        return portSelected(state_->local_device_config_.lidar.port, false);
    case VaporView::SkyDeviceId::TemperatureController:
        return portSelected(state_->local_device_config_.temperatureController.port, false);
    case VaporView::SkyDeviceId::Ai8TemperatureController:
        return portSelected(state_->local_device_config_.ai8TemperatureController.port, false);
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
    if (!localDeviceEnabled(device))
    {
        return VaporView::DeviceState::Disabled;
    }
    if (!homeDevicePortSelected(device))
    {
        return VaporView::DeviceState::Disabled;
    }
    if (state_->connection_attempt_in_progress_)
    {
        const QVariant singleConnectTargetValue = property(kHomeDeviceSingleConnectTargetProperty);
        if (!singleConnectTargetValue.isValid() ||
            static_cast<VaporView::SkyDeviceId>(singleConnectTargetValue.toInt()) == device)
        {
            return VaporView::DeviceState::Connecting;
        }
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
            startHomeDeviceActionSpinner(device);
            setProperty(kHomeDeviceSingleConnectTargetProperty, static_cast<int>(device));
        }
        action->trigger();
        if (connectRequested && !state_->connection_attempt_in_progress_)
        {
            setProperty(kHomeDeviceSingleConnectTargetProperty, QVariant());
        }
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
        setLabelTextIfChanged(label, deviceName);
        bool labelStyleChanged = false;
        labelStyleChanged |= setDynamicPropertyIfChanged(label, "connected", connected);
        labelStyleChanged |= setDynamicPropertyIfChanged(label, "state", stateKey);
        setWidgetToolTipIfChanged(label, state_->is_english_
            ? QStringLiteral("%1 status: %2").arg(deviceName, stateText)
            : QStringLiteral("%1状态：%2").arg(deviceName, stateText));
        if (labelStyleChanged)
        {
            label->style()->unpolish(label);
            label->style()->polish(label);
        }

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
            button->setProperty(kHomeDeviceIconKeyProperty, QStringLiteral("spinner"));
            button->setIcon(createRotatedLucideIcon(QStringLiteral("refresh-cw"),
                                                    toolbarColor(AppThemeColor::HomeDeviceSuccess),
                                                    homeDeviceActionSpinnerDegrees(device, nowMs)));
        }
        else
        {
            const QString iconName = connected ? QStringLiteral("unlink") : QStringLiteral("link");
            const QColor iconColor = connected
                ? toolbarColor(AppThemeColor::ToolbarBlue)
                : state == VaporView::DeviceState::Disabled
                    ? toolbarColor(AppThemeColor::ToolbarDisabled)
                    : toolbarColor(AppThemeColor::HomeDeviceSuccess);
            const QString iconKey = QStringLiteral("%1|%2").arg(iconName, iconColor.name(QColor::HexArgb));
            if (button->property(kHomeDeviceIconKeyProperty).toString() != iconKey)
            {
                button->setProperty(kHomeDeviceIconKeyProperty, iconKey);
                button->setIcon(createLucideIcon(iconName, iconColor));
            }
        }
        const QString buttonToolTip = state_->is_english_
            ? QStringLiteral("%1 %2 (%3)").arg(actionText, deviceName, modeHint)
            : QStringLiteral("%1%2（%3）").arg(actionText, deviceName, modeHint);
        if (button->toolTip() != buttonToolTip)
        {
            button->setToolTip(buttonToolTip);
        }
        if (button->accessibleName() != buttonToolTip)
        {
            button->setAccessibleName(buttonToolTip);
        }
        bool buttonStyleChanged = false;
        buttonStyleChanged |= setDynamicPropertyIfChanged(button, "connected", connected);
        buttonStyleChanged |= setDynamicPropertyIfChanged(button, "state", spinnerActive ? QStringLiteral("connecting") : stateKey);
        if (buttonStyleChanged)
        {
            button->style()->unpolish(button);
            button->style()->polish(button);
        }
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
    updateButton(state_->device_config_.tcp_wave_remote_action_btn,
                 VaporView::SkyDeviceId::WaveTcp);
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
    if (state_->ai8_device_session_)
    {
        state_->ai8_device_session_->setLocalAvailable(false);
    }
    if (state_->epsilon_device_session_)
    {
        state_->epsilon_device_session_->setLocalAvailable(false);
    }
    if (state_->rd105_device_session_)
    {
        state_->rd105_device_session_->setLocalAvailable(false);
    }
    updateAi8TemperatureTitleStatus();
}

void MainWindow::finishConnectionAttempt(bool connected)
{
    state_->connection_attempt_in_progress_ = false;
    state_->cancel_connection_requested_.store(false);
    setProperty(kHomeDeviceSingleConnectTargetProperty, QVariant());
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
                  .arg(state_->local_device_config_.ai8TemperatureController.port,
                       state_->local_device_config_.ai8TemperatureController.baudText)
            : QString();
        state_->ai8_temperature_controller_panel_->setBackendConnected(ai8Connected, detail);
        if (state_->ai8_device_session_)
        {
            state_->ai8_device_session_->setLocalAvailable(ai8Connected, detail);
        }
    }
    if (state_->epsilon_device_session_)
    {
        const CollectorSnapshot collectors = snapshotCollectors();
        const bool epsilonConnected = collectors.epsilon && collectors.epsilon->isRunning();
        const QString detail = epsilonConnected
            ? QStringLiteral("%1 @ %2")
                  .arg(state_->local_device_config_.epsilon.port,
                       state_->local_device_config_.epsilon.baudText)
            : QString();
        state_->epsilon_device_session_->setLocalAvailable(epsilonConnected, detail);
    }
    if (state_->rd105_device_session_)
    {
        const CollectorSnapshot collectors = snapshotCollectors();
        const bool rd105Connected = collectors.temperature_controller &&
                                    collectors.temperature_controller->isRunning();
        const QString detail = rd105Connected
            ? QStringLiteral("%1 @ %2")
                  .arg(state_->local_device_config_.temperatureController.port,
                       state_->local_device_config_.temperatureController.baudText)
            : QString();
        state_->rd105_device_session_->setLocalAvailable(rd105Connected, detail);
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

    updateCombo(state_->device_config_.epsilon_port_combo);
    updateCombo(state_->device_config_.ptb_port_combo);
    updateCombo(state_->device_config_.hmp_port_combo);
    updateCombo(state_->device_config_.lidar_port_combo);
    updateCombo(state_->device_config_.temperature_port_combo);
    updateCombo(state_->sky_telemetry_port_combo_);
    updateCombo(state_->device_config_.sky_telemetry_port_combo);
    updateCombo(state_->device_config_.ai8_temperature_port_combo);
    refreshAi8TemperatureTitlePortOptions(
        ports,
        localSerialPortComboValue(state_->device_config_.ai8_temperature_port_combo));
    updateLocalDeviceConfigFromUi();
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

void MainWindow::startRemoteSerialPortDetection()
{
    if (!state_->remote_sky_controller_ || !state_->remote_sky_controller_->isOpen() ||
        state_->port_detection_in_progress_)
    {
        return;
    }
    state_->port_detection_in_progress_ = true;
    state_->remote_serial_detection_seq_ =
        state_->remote_sky_controller_->sendCommand(VaporView::CommandId::AutoDetectSerialPorts);
    if (state_->remote_serial_detection_seq_ == 0)
    {
        state_->port_detection_in_progress_ = false;
        setRemoteSkyConfigStatus(state_->is_english_
            ? QStringLiteral("Remote serial-port detection was not sent.")
            : QStringLiteral("未发送远程串口自动识别命令。"), true);
        updateConnectionStatus(state_->is_connected_);
        return;
    }
    publishGroundLog(VaporView::LogLevel::Info,
                     QStringLiteral("device.connection"),
                     QStringLiteral("remote_serial_port_detection_started"),
                     QStringLiteral("已请求天空端自动识别串口。"),
                     {{QStringLiteral("command_seq"), state_->remote_serial_detection_seq_},
                      {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    setRemoteSkyConfigStatus(state_->is_english_
        ? QStringLiteral("Sky is detecting serial ports...")
        : QStringLiteral("天空端正在自动识别串口..."));
    updateConnectionStatus(state_->is_connected_);
}

void MainWindow::onRemoteSerialPortDetectionResult(const QJsonObject& result)
{
    if (!isRemoteSkyMode() || !state_->remote_sky_controller_ ||
        !state_->remote_sky_controller_->isOpen())
    {
        return;
    }
    state_->port_detection_in_progress_ = false;
    state_->remote_serial_detection_seq_ = 0;
    state_->remote_serial_detection_cancel_seq_ = 0;
    const bool canceled = result.value(QStringLiteral("canceled")).toBool();
    const bool success = result.value(QStringLiteral("success")).toBool();
    if (success && !canceled)
    {
        VaporView::SkyConfig config = state_->remote_sky_config_;
        const QJsonArray detections = result.value(QStringLiteral("detections")).toArray();
        for (const QJsonValue& value : detections)
        {
            const QJsonObject item = value.toObject();
            const QString key = item.value(QStringLiteral("device_key")).toString();
            const QString port = item.value(QStringLiteral("port")).toString().trimmed();
            const int baud = item.value(QStringLiteral("baud")).toString().toInt();
            if (port.isEmpty() || baud <= 0)
            {
                continue;
            }
            if (key == QStringLiteral("epsilon")) { config.epsilon.port = port; config.epsilon.baud_rate = baud; }
            else if (key == QStringLiteral("ptb")) { config.ptb.port = port; config.ptb.baud_rate = baud; }
            else if (key == QStringLiteral("hmp")) { config.hmp.port = port; config.hmp.baud_rate = baud; }
            else if (key == QStringLiteral("lidar")) { config.lidar.port = port; config.lidar.baud_rate = baud; }
            else if (key == QStringLiteral("temperature")) { config.temperature_controller.port = port; config.temperature_controller.baud_rate = baud; }
            else if (key == QStringLiteral("ai8")) { config.ai8_temperature_controller.port = port; config.ai8_temperature_controller.baud_rate = baud; }
        }
        state_->remote_sky_config_ = config;
        state_->remote_sky_config_loaded_ = true;
        state_->remote_sky_config_loaded_generation_ =
            state_->remote_sky_controller_ ? state_->remote_sky_controller_->linkGeneration() : 0;
        state_->remote_sky_config_dirty_ = true;
        setRemoteSkyConfigUi(config);
        setRemoteSkyConfigStatus(state_->is_english_
            ? QStringLiteral("Remote detection completed; review and apply/save the detected ports.")
            : QStringLiteral("远程串口自动识别完成，结果已填入配置，请检查后点击应用/保存。"));
    }
    else
    {
        setRemoteSkyConfigStatus(canceled
            ? (state_->is_english_ ? QStringLiteral("Remote serial-port detection canceled.")
                                   : QStringLiteral("远程串口自动识别已取消。"))
            : (state_->is_english_ ? QStringLiteral("Remote serial-port detection failed.")
                                   : QStringLiteral("远程串口自动识别失败。")),
            !canceled);
    }
    updateConnectionStatus(state_->is_connected_);
}

void MainWindow::onAutoDetectPortsClicked()
{
    if (isUiTestMode())
    {
        onRefreshPortsClicked();
        state_->local_device_config_.epsilon.port = QStringLiteral("UI-TEST-EPSILON");
        state_->local_device_config_.ptb.port = QStringLiteral("UI-TEST-PTB");
        state_->local_device_config_.hmp.port = QStringLiteral("UI-TEST-HMP");
        state_->local_device_config_.lidar.port = QStringLiteral("UI-TEST-LIDAR");
        state_->local_device_config_.temperatureController.port = QStringLiteral("UI-TEST-RD105");
        state_->local_device_config_.ai8TemperatureController.port = QStringLiteral("UI-TEST-AI8");
        refreshDeviceConfigUiFromLocalModel();
        saveRememberedInputState();
        publishUiTestEvent(QStringLiteral("ui_test_auto_detection_completed"),
                           state_->is_english_ ? QStringLiteral("Automatic detection completed with fixed test devices")
                                               : QStringLiteral("自动识别已完成，已填入固定测试设备"),
                           {{QStringLiteral("detected_devices"), 6}});
        return;
    }
    if (isRemoteSkyMode())
    {
        if (state_->port_detection_in_progress_)
        {
            if (state_->remote_sky_controller_ && state_->remote_sky_controller_->isOpen())
            {
                state_->remote_serial_detection_cancel_seq_ =
                    state_->remote_sky_controller_->sendCommand(
                        VaporView::CommandId::CancelSerialPortDetection);
                publishGroundLog(VaporView::LogLevel::Info,
                                 QStringLiteral("device.connection"),
                                 QStringLiteral("serial_port_detection_cancel_requested"),
                                 QStringLiteral("已请求天空端取消自动识别串口。"),
                                 {{QStringLiteral("reason_code"), QStringLiteral("USER_CANCELLED")},
                                  {QStringLiteral("ui_visibility"), QStringLiteral("attention")} });
            }
            return;
        }
        if (!state_->remote_sky_controller_ || !state_->remote_sky_controller_->isOpen())
        {
            return;
        }
        if (!state_->remote_sky_config_loaded_)
        {
            state_->remote_serial_detection_pending_ = true;
            requestRemoteSkyConfigIfAvailable(false);
            setRemoteSkyConfigStatus(state_->is_english_
                ? QStringLiteral("Reading Remote Sky config before detection...")
                : QStringLiteral("正在读取远程配置，读取完成后开始自动识别串口..."));
            return;
        }
        startRemoteSerialPortDetection();
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
    updateLocalDeviceConfigFromUi();
    const auto& localConfig = state_->local_device_config_;
    const QString selectedEpsilonPort = localConfig.epsilon.port;
    const QString selectedPtbPort = localConfig.ptb.port;
    const QString selectedHmpPort = localConfig.hmp.port;
    const QString selectedLidarPort = localConfig.lidar.port;
    const QString selectedTemperaturePort = localConfig.temperatureController.port;
    const QString selectedAi8TemperaturePort = localConfig.ai8TemperatureController.port;
    const QString selectedEpsilonBaud = localConfig.epsilon.baudText;
    const QString selectedPtbBaud = localConfig.ptb.baudText;
    const QString selectedHmpBaud = localConfig.hmp.baudText;
    const QString selectedLidarBaud = localConfig.lidar.baudText;
    const QString selectedTemperatureBaud = localConfig.temperatureController.baudText;
    const QString selectedAi8TemperatureBaud = localConfig.ai8TemperatureController.baudText;
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
            applyLocalPortDetections(detections);

            state_->port_detection_in_progress_ = false;
            state_->cancel_connection_requested_.store(false);
            updateConnectionStatus(state_->is_connected_);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::applyLocalPortDetections(
    const QVector<VaporView::Ground::Devices::SerialPortDetectionResult>& detections)
{
    const QString selectText = state_->is_english_ ? "-- Select --" : "未选择";
    auto normalizePort = [&selectText](const QString& value) {
        return (value.isEmpty() || value == selectText) ? selectText : value;
    };
    QHash<QString, QString> currentPorts{
        {QStringLiteral("epsilon"), state_->local_device_config_.epsilon.port},
        {QStringLiteral("ptb"), state_->local_device_config_.ptb.port},
        {QStringLiteral("hmp"), state_->local_device_config_.hmp.port},
        {QStringLiteral("lidar"), state_->local_device_config_.lidar.port},
        {QStringLiteral("temperature"), state_->local_device_config_.temperatureController.port},
        {QStringLiteral("ai8"), state_->local_device_config_.ai8TemperatureController.port},
    };
    QHash<QString, QString> detectedPorts;
    QHash<QString, QString> detectedBauds;
    QSet<QString> detectedKeys;
    QSet<QString> detectedPortNames;
    for (const auto& detection : detections)
    {
        const QString port = normalizePort(detection.port);
        if (port == selectText || !currentPorts.contains(detection.deviceKey))
        {
            continue;
        }
        detectedPorts[detection.deviceKey] = port;
        detectedBauds[detection.deviceKey] = detection.baud;
        detectedKeys.insert(detection.deviceKey);
        detectedPortNames.insert(port);
    }
    auto clearModelPort = [this](const QString& key) {
        if (key == QStringLiteral("epsilon")) state_->local_device_config_.epsilon.port.clear();
        else if (key == QStringLiteral("ptb")) state_->local_device_config_.ptb.port.clear();
        else if (key == QStringLiteral("hmp")) state_->local_device_config_.hmp.port.clear();
        else if (key == QStringLiteral("lidar")) state_->local_device_config_.lidar.port.clear();
        else if (key == QStringLiteral("temperature")) state_->local_device_config_.temperatureController.port.clear();
        else if (key == QStringLiteral("ai8")) state_->local_device_config_.ai8TemperatureController.port.clear();
    };
    for (auto it = currentPorts.begin(); it != currentPorts.end(); ++it)
    {
        const QString current = normalizePort(it.value());
        if (!detectedKeys.contains(it.key()) && detectedPortNames.contains(current))
        {
            clearModelPort(it.key());
        }
    }
    auto assign = [this](const QString& key, const QString& port, const QString& baud) {
        const QString normalizedPort = port == (state_->is_english_ ? "-- Select --" : "未选择") ? QString() : port;
        if (key == QStringLiteral("epsilon")) { state_->local_device_config_.epsilon.port = normalizedPort; state_->local_device_config_.epsilon.baudText = baud; }
        else if (key == QStringLiteral("ptb")) { state_->local_device_config_.ptb.port = normalizedPort; state_->local_device_config_.ptb.baudText = baud; }
        else if (key == QStringLiteral("hmp")) { state_->local_device_config_.hmp.port = normalizedPort; state_->local_device_config_.hmp.baudText = baud; }
        else if (key == QStringLiteral("lidar")) { state_->local_device_config_.lidar.port = normalizedPort; state_->local_device_config_.lidar.baudText = baud; }
        else if (key == QStringLiteral("temperature")) { state_->local_device_config_.temperatureController.port = normalizedPort; state_->local_device_config_.temperatureController.baudText = baud; }
        else if (key == QStringLiteral("ai8")) { state_->local_device_config_.ai8TemperatureController.port = normalizedPort; state_->local_device_config_.ai8TemperatureController.baudText = baud; }
        if (!normalizedPort.isEmpty()) VaporView::rememberSerialPort(normalizedPort);
    };
    for (const auto& detection : detections)
    {
        const QString port = detectedPorts.value(detection.deviceKey);
        if (!port.isEmpty())
        {
            assign(detection.deviceKey, port, detectedBauds.value(detection.deviceKey));
        }
    }
    refreshDeviceConfigUiFromLocalModel();
    saveRememberedInputState();
}

#ifdef VAPORVIEW_MAIN_WINDOW_TESTING
QJsonObject MainWindow::testLocalDeviceConfigSnapshot() const
{
    const auto& config = state_->local_device_config_;
    auto serial = [](const VaporView::Ground::Devices::LocalSerialDeviceSettings& value) {
        return QJsonObject{{QStringLiteral("port"), value.port},
                           {QStringLiteral("baud"), value.baudText},
                           {QStringLiteral("rate_hz"), value.sampleRateHz},
                           {QStringLiteral("enabled"), value.enabled}};
    };
    return QJsonObject{
        {QStringLiteral("epsilon"), serial(config.epsilon)},
        {QStringLiteral("ptb"), serial(config.ptb)},
        {QStringLiteral("hmp"), serial(config.hmp)},
        {QStringLiteral("lidar"), serial(config.lidar)},
        {QStringLiteral("temperature"), serial(config.temperatureController)},
        {QStringLiteral("ai8"), serial(config.ai8TemperatureController)},
        {QStringLiteral("pressure_source"), config.pressureSource},
        {QStringLiteral("humidity_source"), config.humiditySource},
    };
}

void MainWindow::testApplyLocalPortDetection(const QString& deviceKey,
                                             const QString& port,
                                             const QString& baud)
{
    applyLocalPortDetections({{deviceKey, port, baud}});
}
#endif

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
                              {QStringLiteral("ui_message"),
                               QStringLiteral("无法打开天空端数传链路（%1）。请确认 SkyCore 已启动并监听该地址。")
                                   .arg(openedText)},
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
    if (state_->epsilon_config_panel_)
    {
        state_->epsilon_config_panel_->setLivePacketRates(state_->current_epsilon_);
    }
    updateEnvironmentStatusIcons(false, false, false);

    const bool english = state_->is_english_;
    const QString selectText = english ? "-- Select --" : "未选择";
    updateLocalDeviceConfigFromUi();
    const auto& localConfig = state_->local_device_config_;
    const QString epsilonPort = localConfig.epsilon.port;
    const QString epsilonBaudText = localConfig.epsilon.baudText;
    const QString ptbRateText = localConfig.ptbRateText;
    const QString hmpRateText = localConfig.hmpRateText;
    const VaporView::PressureSensorProtocol pressureProtocol = localConfig.pressureProtocol;
    const VaporView::HumiditySensorProtocol humidityProtocol = localConfig.humidityProtocol;
    const QString lidarRateText = localConfig.lidarRateText;
    const QString temperatureRateText = localConfig.temperatureRateText;
    const QString ai8TemperatureRateText = localConfig.ai8RateText;
    const bool skipPtbDeviceRate = isRateUnspecified(ptbRateText);
    const bool skipHmpDeviceRate = isRateUnspecified(hmpRateText);
    const bool skipLidarDeviceRate = isRateUnspecified(lidarRateText);
    const bool skipTemperatureDeviceRate = isRateUnspecified(temperatureRateText);
    const int epsilonRate = std::clamp(state_->epsilon_sample_rate_, 20, 200);
    const int ptbRate = clampPtbSampleRate(effectiveRateOrDefault(ptbRateText, kDefaultPtbSampleRateHz, kPtbMaxSampleRateHz));
    const int hmpRate = effectiveRateOrDefault(hmpRateText, kDefaultHmpSampleRateHz);
    const int lidarRate = effectiveRateOrDefault(lidarRateText, kDefaultLidarSampleRateHz, 100);
    const int temperatureRate = effectiveRateOrDefault(temperatureRateText, kDefaultTemperatureSampleRateHz, kMaxTemperatureSampleRateHz);
    const int ai8TemperatureRate = effectiveRateOrDefault(ai8TemperatureRateText, 5, 20);
    QSettings settings = VaporView::applicationConfigSettings();
    settings.beginGroup(QStringLiteral("MainWindow"));
    const std::map<uint8_t, int> epsilonDesiredPacketRates =
        effectiveEpsilonPacketRates(settings);
    const QVariant singleConnectTargetValue = property(kHomeDeviceSingleConnectTargetProperty);
    const bool singleDeviceConnect = singleConnectTargetValue.isValid();
    const VaporView::SkyDeviceId singleConnectTarget = singleDeviceConnect
        ? static_cast<VaporView::SkyDeviceId>(singleConnectTargetValue.toInt())
        : VaporView::SkyDeviceId::All;
    auto localRequestedFor = [&](VaporView::SkyDeviceId device) {
        return !singleDeviceConnect || singleConnectTarget == device;
    };
    const bool epsilonEnabled = localRequestedFor(VaporView::SkyDeviceId::Epsilon) &&
        localConfig.epsilon.enabled;

    if (epsilonEnabled &&
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
        epsilonEnabled &&
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
    request.includeWaveform = !singleDeviceConnect;
    request.selectText = selectText;
    request.epsilon = VaporView::Ground::Devices::makeLocalConnectionSettings(
        localConfig.epsilon, localRequestedFor(VaporView::SkyDeviceId::Epsilon),
        epsilonCallbackRate, false);
    request.ptb = VaporView::Ground::Devices::makeLocalConnectionSettings(
        localConfig.ptb, localRequestedFor(VaporView::SkyDeviceId::Ptb),
        ptbRate, skipPtbDeviceRate);
    request.hmp = VaporView::Ground::Devices::makeLocalConnectionSettings(
        localConfig.hmp, localRequestedFor(VaporView::SkyDeviceId::Hmp),
        hmpRate, skipHmpDeviceRate);
    request.lidar = VaporView::Ground::Devices::makeLocalConnectionSettings(
        localConfig.lidar, localRequestedFor(VaporView::SkyDeviceId::Lidar),
        lidarRate, skipLidarDeviceRate);
    request.temperatureController = VaporView::Ground::Devices::makeLocalConnectionSettings(
        localConfig.temperatureController,
        localRequestedFor(VaporView::SkyDeviceId::TemperatureController),
        temperatureRate,
        skipTemperatureDeviceRate);
    request.ai8TemperatureController = VaporView::Ground::Devices::makeLocalConnectionSettings(
        localConfig.ai8TemperatureController,
        localRequestedFor(VaporView::SkyDeviceId::Ai8TemperatureController),
        ai8TemperatureRate,
        false);
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
    request.epsilonPacketRatesMatchDefault =
        VaporView::Ground::DeviceRates::epsilonPacketRatesMatchDefault(epsilonDesiredPacketRates);
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
        if (state_->epsilon_device_session_ &&
            !isRemoteSkyMode() &&
            !state_->epsilon_device_session_->operationPending())
        {
            const QString detail = QStringLiteral("%1 @ %2")
                .arg(state_->local_device_config_.epsilon.port,
                     state_->local_device_config_.epsilon.baudText);
            state_->epsilon_device_session_->setLocalAvailable(
                collectors.epsilon->isRunning(), detail);
        }
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
        if (state_->rd105_device_session_ && !isRemoteSkyMode())
        {
            const bool available = collectors.temperature_controller->isRunning();
            const QString detail = available
                ? QStringLiteral("%1 @ %2")
                      .arg(state_->local_device_config_.temperatureController.port,
                           state_->local_device_config_.temperatureController.baudText)
                : QString();
            state_->rd105_device_session_->setLocalAvailable(available, detail);
        }
    }
}

void MainWindow::onAi8TemperatureControllerDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    const bool available = collectors.ai8_temperature_controller &&
        collectors.ai8_temperature_controller->isRunning();
    if (state_->ai8_device_session_ && !isRemoteSkyMode())
    {
        const QString detail = available
            ? QStringLiteral("%1 @ %2")
                  .arg(state_->local_device_config_.ai8TemperatureController.port,
                       state_->local_device_config_.ai8TemperatureController.baudText)
            : QString();
        state_->ai8_device_session_->setLocalAvailable(available, detail);
    }
    if (available && state_->ai8_temperature_controller_panel_)
    {
        state_->ai8_temperature_controller_panel_->applyLiveData(
            collectors.ai8_temperature_controller->getLatestData());
    }
    updateAi8TemperatureTitleStatus();
}

void MainWindow::onAi8ReadPageRequested()
{
    if (!state_->ai8_temperature_controller_panel_ || !state_->ai8_device_session_)
    {
        return;
    }
    const auto requested = state_->ai8_temperature_controller_panel_->currentPageData();
    state_->ai8_device_session_->activatePage(requested.page, requested.selection);
    state_->ai8_device_session_->readPage(requested.page, requested.selection);
}

void MainWindow::onAi8WritePageRequested()
{
    if (!state_->ai8_temperature_controller_panel_ || !state_->ai8_device_session_)
    {
        return;
    }
    const auto requested = state_->ai8_temperature_controller_panel_->currentPageData();
    state_->ai8_device_session_->activatePage(requested.page, requested.selection);
    state_->ai8_device_session_->writePage(requested);
}

void MainWindow::onAi8SessionAvailabilityChanged(bool available, const QString& reason)
{
    if (!state_->ai8_temperature_controller_panel_)
    {
        return;
    }
    state_->ai8_temperature_controller_panel_->setPageCommandsEnabled(
        available, available ? QString() : reason);
}

void MainWindow::onAi8SessionOperationStarted(
    quint64 requestId, VaporView::Ground::Devices::Ai8Operation operation)
{
    Q_UNUSED(requestId);
    if (!state_->ai8_temperature_controller_panel_)
    {
        return;
    }
    const bool writing = operation != VaporView::Ground::Devices::Ai8Operation::Read;
    state_->ai8_temperature_controller_panel_->setOperationStatus(
        writing
            ? (state_->is_english_ ? QStringLiteral("Writing and reading back...")
                                   : QStringLiteral("正在写入并回读确认…"))
            : (state_->is_english_ ? QStringLiteral("Reading current page...")
                                   : QStringLiteral("正在读取当前页…")),
        true);
    state_->ai8_temperature_controller_panel_->setPageCommandsEnabled(
        false,
        state_->is_english_ ? QStringLiteral("Waiting for the device backend...")
                            : QStringLiteral("正在等待设备后端响应…"));
}

void MainWindow::onAi8SessionOperationFinished(
    const VaporView::Ground::Devices::Ai8SessionResult& result)
{
    if (!state_->ai8_temperature_controller_panel_ || !state_->ai8_device_session_)
    {
        return;
    }

    using Operation = VaporView::Ground::Devices::Ai8Operation;
    using Outcome = VaporView::Ground::Devices::Ai8OperationOutcome;
    QString statusText = result.message;
    if (result.success())
    {
        statusText = result.operation == Operation::Read
            ? (state_->is_english_ ? QStringLiteral("Parameters were read.")
                                   : QStringLiteral("参数读取完成。"))
            : (state_->is_english_
                   ? QStringLiteral("Parameters were written and confirmed by read-back.")
                   : QStringLiteral("参数已写入并回读确认。"));
    }
    else if (statusText.isEmpty())
    {
        statusText = state_->is_english_ ? QStringLiteral("AI-8 operation failed.")
                                         : QStringLiteral("AI-8 操作失败。");
    }

    state_->ai8_temperature_controller_panel_->setOperationStatus(
        statusText, result.success());
    state_->ai8_temperature_controller_panel_->setPageCommandsEnabled(
        state_->ai8_device_session_->operationsAvailable(),
        state_->ai8_device_session_->operationsAvailable() ? QString() : statusText);

    QString operationName;
    switch (result.operation)
    {
    case Operation::Read: operationName = QStringLiteral("read"); break;
    case Operation::Write: operationName = QStringLiteral("write"); break;
    case Operation::FactoryReset: operationName = QStringLiteral("factory_reset"); break;
    }
    QString outcomeName;
    switch (result.outcome)
    {
    case Outcome::Success: outcomeName = QStringLiteral("success"); break;
    case Outcome::Failed: outcomeName = QStringLiteral("failed"); break;
    case Outcome::Timeout: outcomeName = QStringLiteral("timeout"); break;
    case Outcome::Disconnected: outcomeName = QStringLiteral("disconnected"); break;
    case Outcome::Unsupported: outcomeName = QStringLiteral("unsupported"); break;
    }
    QVariantMap fields{{QStringLiteral("device"), QStringLiteral("AI-8288")},
                       {QStringLiteral("device_id"), QStringLiteral("ai8_temperature_controller")},
                       {QStringLiteral("request_id"), result.request_id},
                       {QStringLiteral("operation"), operationName},
                       {QStringLiteral("outcome"), outcomeName},
                       {QStringLiteral("page"), static_cast<int>(result.requested.page)},
                       {QStringLiteral("channel"), result.requested.selection.channel},
                       {QStringLiteral("input_group"), result.requested.selection.inputGroup},
                       {QStringLiteral("output_group"), result.requested.selection.outputGroup},
                       {QStringLiteral("command_error_code"),
                        commandErrorCodeIdentifier(result.error_code)},
                       {QStringLiteral("ui_visibility"), result.success()
                            ? QStringLiteral("details") : QStringLiteral("attention")},
                       {QStringLiteral("details"), statusText}};
    if (!result.success())
    {
        fields.insert(QStringLiteral("error_code"), QStringLiteral("AI8_OPERATION_FAILED"));
        fields.insert(QStringLiteral("ui_dedupe_key"),
                      QStringLiteral("ai8:%1:%2").arg(operationName, outcomeName));
    }
    publishGroundLog(result.success() ? VaporView::LogLevel::Info : VaporView::LogLevel::Error,
                     QStringLiteral("device.temperature.command"),
                     result.success() ? QStringLiteral("ai8_operation_completed")
                                      : QStringLiteral("ai8_operation_failed"),
                     result.success() ? QStringLiteral("AI-8288 参数操作完成。")
                                      : QStringLiteral("AI-8288 参数操作失败。"),
                     fields);
}

void MainWindow::onAi8SessionPageDataAvailable(
    const VaporView::Ai8TemperatureControllerProtocol::PageData& pageData)
{
    if (state_->ai8_temperature_controller_panel_)
    {
        state_->ai8_temperature_controller_panel_->applyPageData(pageData);
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
    if (state_->epsilon_config_panel_)
    {
        state_->epsilon_config_panel_->setLivePacketRates(state_->current_epsilon_);
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
