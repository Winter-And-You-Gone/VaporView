#include "ground/main/GroundMainWindowImplementation.h"
#include "ground/devices/DeviceRatePolicy.h"

void MainWindow::setEnglish(bool english)
{
    auto setNativeMenuTitle = [this](QMenu *menu, const QString& title) {
        if (!menu || state_->custom_title_bar_)
        {
            return;
        }
        menu->setTitle(title);
    };

    state_->is_english_ = english;
    if (qApp)
    {
        qApp->setProperty(kEnglishProperty, state_->is_english_);
    }

    setNativeMenuTitle(state_->data_menu_, english ? QStringLiteral("&Data") : QStringLiteral("数据(&D)"));
    state_->recording_directory_action_->setText(english ? "Recording Folder..." : "记录目录...");
    setNativeMenuTitle(state_->recording_rate_menu_, english ? QStringLiteral("Record Rates") : QStringLiteral("记录频率"));
    rebuildRecordingRateMenu();
    setNativeMenuTitle(state_->devices_menu_, english ? QStringLiteral("&Devices") : QStringLiteral("设备(&E)"));
    if (state_->epsilon_packet_rates_action_)
    {
        state_->epsilon_packet_rates_action_->setText(english ? "EPSILON Packet Rates..." : "设置EPSILON包频率...");
    }
    if (state_->epsilon_rtcm_port_action_)
    {
        state_->epsilon_rtcm_port_action_->setText(english ? "Configure EPSILON RTCM Port..." : "配置EPSILON RTCM串口...");
    }
    if (state_->epsilon_reconfigure_action_)
    {
        state_->epsilon_reconfigure_action_->setText(english ? "Reconfigure EPSILON Output..." : "重新配置EPSILON输出...");
    }
    state_->session_viewer_action_->setText(english ? "Data Viewer..." : "数据查看器...");
    setNativeMenuTitle(state_->view_menu_, english ? QStringLiteral("&View") : QStringLiteral("视图(&V)"));
#ifdef VAPORVIEW_HAS_OSGEARTH
    if (state_->map3d_action_)
    {
        state_->map3d_action_->setText(english ? "3D Map..." : "三维地图...");
        state_->map3d_action_->setToolTip(english ? "Open 3D map" : "打开三维地图");
        state_->map3d_action_->setStatusTip(state_->map3d_action_->toolTip());
    }
    if (state_->map3d_diagnostics_action_)
    {
        state_->map3d_diagnostics_action_->setText(english ? "Map Data Diagnostics..." : "地图数据诊断...");
        state_->map3d_diagnostics_action_->setToolTip(english ? "Open 3D map data diagnostics" : "打开三维地图数据诊断");
        state_->map3d_diagnostics_action_->setStatusTip(state_->map3d_diagnostics_action_->toolTip());
    }
#endif
    state_->exit_action_->setText(english ? "E&xit" : "退出(&X)");

    setNativeMenuTitle(state_->font_menu_, english ? QStringLiteral("Font &Size") : QStringLiteral("字号(&S)"));
    state_->font_tiny_action_->setText(english ? "Tiny (70%)" : "超小 (70%)");
    state_->font_extra_small_action_->setText(english ? "Extra Small (80%)" : "特小 (80%)");
    state_->font_small_action_->setText(english ? "Small (90%)" : "小号 (90%)");
    state_->font_normal_action_->setText(english ? "Normal (100%)" : "标准 (100%)");
    state_->font_large_action_->setText(english ? "Large (115%)" : "大号 (115%)");
    state_->font_extra_large_action_->setText(english ? "Extra Large (130%)" : "超大 (130%)");

    setNativeMenuTitle(state_->language_menu_, english ? QStringLiteral("&Language") : QStringLiteral("语言(&L)"));
    state_->lang_action_->setText(english ? "Switch to Chinese" : "切换到英文");
    state_->lang_action_->setToolTip(english ? "Switch to Chinese" : "切换到英文");
    state_->lang_action_->setStatusTip(english ? "Switch interface language" : "切换界面语言");
    updateThemeAction();
    updateCustomTitleBarTexts();
    discardTitleApplicationMenuPanel();

    setNativeMenuTitle(state_->help_menu_, english ? QStringLiteral("&Help") : QStringLiteral("帮助(&H)"));
    state_->about_action_->setText(english ? "&About" : "关于(&A)");

    state_->refresh_ports_btn_->setText(english ? "Refresh" : "刷新");
    state_->refresh_ports_btn_->setToolTip(english ? "Refresh ports" : "刷新串口");
    state_->refresh_ports_btn_->setStatusTip(state_->refresh_ports_btn_->toolTip());
    state_->connect_btn_->setText(english ? "Connect" : "连接");
    state_->connect_btn_->setToolTip(english ? "Connect" : "连接");
    state_->connect_btn_->setStatusTip(state_->connect_btn_->toolTip());
    state_->cancel_connect_btn_->setText(english ? "Cancel" : "取消");
    state_->cancel_connect_btn_->setToolTip(english ? "Cancel connection" : "取消连接");
    state_->cancel_connect_btn_->setStatusTip(state_->cancel_connect_btn_->toolTip());
    state_->disconnect_btn_->setText(english ? "Disconnect" : "断开");
    state_->disconnect_btn_->setToolTip(english ? "Disconnect" : "断开连接");
    state_->disconnect_btn_->setStatusTip(state_->disconnect_btn_->toolTip());
    updateScheduledRecordingAction();
    state_->start_recording_btn_->setText(english ? "Start Recording" : "开始记录");
    state_->start_recording_btn_->setToolTip(english ? "Start recording" : "开始记录");
    state_->start_recording_btn_->setStatusTip(state_->start_recording_btn_->toolTip());
    state_->pause_recording_btn_->setText(english ? "Pause Recording" : "暂停记录");
    state_->pause_recording_btn_->setToolTip(english ? "Pause recording" : "暂停记录");
    state_->pause_recording_btn_->setStatusTip(state_->pause_recording_btn_->toolTip());
    state_->stop_recording_btn_->setText(english ? "Stop Recording" : "结束记录");
    state_->stop_recording_btn_->setToolTip(english ? "Stop recording" : "结束记录");
    state_->stop_recording_btn_->setStatusTip(state_->stop_recording_btn_->toolTip());
    state_->clear_log_action_->setText(english ? "Clear Log" : "清空日志");
    state_->clear_log_action_->setToolTip(english ? "Clear Log" : "清空日志");
    state_->clear_log_action_->setStatusTip(english ? "Clear Log" : "清空日志");
    updateLogFilterAction();
    state_->rtk_config_action_->setText(english ? "RTK Config" : "RTK配置");
    updateRtkConfigIcon();
    state_->session_viewer_action_->setToolTip(english ? "Data viewer" : "数据查看器");
    state_->session_viewer_action_->setStatusTip(state_->session_viewer_action_->toolTip());

    state_->status_label_->setText(english ? "Ready" : "就绪");

    state_->config_group_->setTitle(QString());
    state_->data_group_->setTitle(QString());
    state_->tcp_wave_group_->setTitle(QString());

    if (state_->epsilon_group_) state_->epsilon_group_->setTitle(QString());
    if (state_->gnss_group_) state_->gnss_group_->setTitle(QString());
    if (state_->imu_group_) state_->imu_group_->setTitle(QString());
    if (state_->env_group_) state_->env_group_->setTitle(QString());

    if (state_->epsilon_lbl_) state_->epsilon_lbl_->setText(english ? "EPSILON:" : "EPSILON:");
    if (state_->gnss_lbl_) state_->gnss_lbl_->setText(english ? "GNSS:" : "GNSS:");
    if (state_->imu_lbl_) state_->imu_lbl_->setText(english ? "IMU:" : "IMU:");
    if (state_->ptb_lbl_) state_->ptb_lbl_->setText(english ? "PTB210:" : "PTB210:");
    if (state_->hmp_lbl_) state_->hmp_lbl_->setText(english ? "HMP3:" : "HMP3:");
    if (state_->lidar_lbl_) state_->lidar_lbl_->setText(english ? "TFA1500-L:" : "TFA1500-L:");
    if (state_->temperature_lbl_) state_->temperature_lbl_->setText(QStringLiteral("RD105:"));

    if (state_->config_inline_title_lbl_)
    {
        state_->config_inline_title_lbl_->setText(english ? "Device Overview" : "设备概览");
    }
    if (state_->data_source_mode_lbl_) state_->data_source_mode_lbl_->setText(english ? "Source:" : "数据源:");
    if (state_->source_mode_switch_) state_->source_mode_switch_->setEnglish(english);
    if (state_->sky_telemetry_transport_lbl_) state_->sky_telemetry_transport_lbl_->setText(english ? "Link:" : "链路:");
    updateSkyTelemetryTransportComboTexts(state_->sky_telemetry_transport_combo_, english);
    if (state_->sky_telemetry_tcp_host_lbl_) state_->sky_telemetry_tcp_host_lbl_->setText(english ? "Sky IP:" : "天空端IP:");
    if (state_->sky_telemetry_tcp_port_lbl_) state_->sky_telemetry_tcp_port_lbl_->setText(english ? "Port:" : "端口:");
    if (state_->sky_telemetry_port_lbl_) state_->sky_telemetry_port_lbl_->setText(english ? "Serial:" : "串口:");
    if (state_->sky_telemetry_baud_lbl_) state_->sky_telemetry_baud_lbl_->setText(english ? "Baud:" : "波特率:");
    if (state_->sky_device_config_btn_) state_->sky_device_config_btn_->setText(english ? "Sky Device Config" : "天空端设备配置");
    if (state_->data_source_mode_combo_)
    {
        const QSignalBlocker blocker(state_->data_source_mode_combo_);
        state_->data_source_mode_combo_->setItemText(0, sourceModeDisplayText(english, 0));
        state_->data_source_mode_combo_->setItemText(1, sourceModeDisplayText(english, 1));
    }
    if (state_->auto_detect_ports_btn_)
    {
        state_->auto_detect_ports_btn_->setText(state_->port_detection_in_progress_
            ? (english ? "Cancel Auto Detect" : "取消自动识别")
            : (english ? "Auto Detect Ports" : "自动识别串口"));
        state_->auto_detect_ports_btn_->setToolTip(state_->port_detection_in_progress_
            ? (english ? "Stop the current serial-port detection task." : "停止当前串口自动识别任务。")
            : (english ? "Probe available serial ports and automatically assign detected devices."
                       : "扫描可用串口，并将识别出的设备自动填入对应端口。"));
    }
    if (state_->log_inline_title_lbl_)
    {
        state_->log_inline_title_lbl_->setText(english ? "Log" : "日志");
    }
    updateAppSidebarButtonTexts();
    if (state_->recording_status_title_lbl_)
    {
        state_->recording_status_title_lbl_->setText(english ? "Recording Status" : "记录状态");
    }
    if (state_->log_side_panel_)
    {
        state_->log_side_panel_->setMinimumWidth(minimumLogSidePanelWidth());
    }
    if (state_->epsilon_inline_title_lbl_)
    {
        state_->epsilon_inline_title_lbl_->setText(english ? "EPSILON Integrated Navigation" : "EPSILON组合导航");
    }
    if (state_->gnss_inline_title_lbl_)
    {
        state_->gnss_inline_title_lbl_->setText(english ? "GNSS / RTK" : "GNSS / RTK");
    }
    if (state_->imu_inline_title_lbl_)
    {
        state_->imu_inline_title_lbl_->setText(english ? "IMU" : "IMU");
    }
    if (state_->env_inline_title_lbl_)
    {
        state_->env_inline_title_lbl_->setText(english ? "Environment / Range" : "环境与测距");
    }
    if (state_->temperature_overview_inline_title_lbl_)
    {
        state_->temperature_overview_inline_title_lbl_->setText(english ? "Laser Driver Temperature Overview" : "激光驱动温控概览");
    }
    if (state_->temperature_overview_panel_)
    {
        state_->temperature_overview_panel_->setEnglish(english);
    }
    if (state_->temperature_controller_inline_title_lbl_)
    {
        updateTemperatureControllerTitleText();
    }
    updateTemperatureTitleButtonsState();
    if (state_->global_rate_lbl_) state_->global_rate_lbl_->setText(english ? "Global Rate:" : "统一频率:");
    if (state_->epsilon_rate_lbl_) state_->epsilon_rate_lbl_->setText(english ? "Packets:" : "包频率:");
    if (state_->epsilon_packet_rates_btn_)
    {
        state_->epsilon_packet_rates_btn_->setText(english ? "Packet Rates..." : "配置EPSILON包频率...");
        state_->epsilon_packet_rates_btn_->setToolTip(english
            ? "Configure EPSILON packet output rates"
            : "配置 EPSILON 各数据包输出频率");
    }
    if (state_->gnss_rate_lbl_) state_->gnss_rate_lbl_->setText(english ? "Rate:" : "频率:");
    if (state_->imu_rate_lbl_) state_->imu_rate_lbl_->setText(english ? "Rate:" : "频率:");
    if (state_->imu_apply_btn_)
    {
        state_->imu_apply_btn_->setText(english ? "Apply IMU" : "应用IMU");
        state_->imu_apply_btn_->setToolTip(english ? "Apply the selected IMU format, baud rate, and output frequency" : "应用当前选择的 IMU 输出格式、波特率和输出频率");
    }
    if (state_->imu_hi91_btn_)
    {
        state_->imu_hi91_btn_->setToolTip(english ? "Switch IMU output to HI91 immediately" : "立即切换 IMU 输出为 HI91");
    }
    if (state_->imu_hi92_btn_)
    {
        state_->imu_hi92_btn_->setToolTip(english ? "Switch IMU output to HI92 immediately" : "立即切换 IMU 输出为 HI92");
    }
    if (state_->imu_baud_115200_btn_)
    {
        state_->imu_baud_115200_btn_->setToolTip(english ? "Switch IMU baud rate to 115200" : "一键切换 IMU 波特率到 115200");
    }
    if (state_->imu_baud_921600_btn_)
    {
        state_->imu_baud_921600_btn_->setToolTip(english ? "Switch IMU baud rate to 921600" : "一键切换 IMU 波特率到 921600");
    }
    if (state_->imu_rate_100_btn_)
    {
        state_->imu_rate_100_btn_->setToolTip(english ? "Switch IMU output frequency to 100 Hz" : "一键切换 IMU 输出频率到 100 Hz");
    }
    if (state_->imu_rate_200_btn_)
    {
        state_->imu_rate_200_btn_->setToolTip(english ? "Switch IMU output frequency to 200 Hz" : "一键切换 IMU 输出频率到 200 Hz");
    }
    if (state_->imu_rate_500_btn_)
    {
        state_->imu_rate_500_btn_->setToolTip(english ? "Switch IMU output frequency to 500 Hz" : "一键切换 IMU 输出频率到 500 Hz");
    }
    if (state_->imu_rate_1000_btn_)
    {
        state_->imu_rate_1000_btn_->setToolTip(english ? "Switch IMU output frequency to 1000 Hz" : "一键切换 IMU 输出频率到 1000 Hz");
    }
    state_->ptb_rate_lbl_->setText(english ? "Rate:" : "频率:");
    state_->hmp_rate_lbl_->setText(english ? "Rate:" : "频率:");
    state_->lidar_rate_lbl_->setText(english ? "Rate:" : "频率:");
    if (state_->temperature_rate_lbl_) state_->temperature_rate_lbl_->setText(english ? "Poll:" : "轮询:");
    for (QComboBox *combo : {state_->ptb_rate_combo_, state_->hmp_rate_combo_, state_->lidar_rate_combo_, state_->temperature_rate_combo_})
    {
        if (!combo)
        {
            continue;
        }
        const QSignalBlocker blocker(combo);
        const QString oldText = english ? QStringLiteral("不设定") : QStringLiteral("No Set");
        const QString newText = english ? QStringLiteral("No Set") : QStringLiteral("不设定");
        const int idx = combo->findText(oldText);
        if (idx >= 0)
        {
            combo->setItemText(idx, newText);
        }
        else if (combo->findText(newText) < 0)
        {
            combo->addItem(newText);
        }
        if (isRateUnspecified(combo->currentText()))
        {
            combo->setCurrentText(newText);
        }
    }

    if (state_->epsilon_panel_) state_->epsilon_panel_->setEnglish(english);
    if (state_->gnss_panel_) state_->gnss_panel_->setEnglish(english);
    if (state_->imu_panel_) state_->imu_panel_->setEnglish(english);
    if (state_->ptb_panel_) state_->ptb_panel_->setEnglish(english);
    if (state_->hmp_panel_) state_->hmp_panel_->setEnglish(english);
    if (state_->lidar_panel_) state_->lidar_panel_->setEnglish(english);
    if (state_->tcp_wave_panel_) state_->tcp_wave_panel_->setEnglish(english);
    if (state_->sky_device_config_dialog_) state_->sky_device_config_dialog_->setEnglish(english);

    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.epsilon) collectors.epsilon->setEnglish(english);
    if (collectors.gnss) collectors.gnss->setEnglish(english);
    if (collectors.imu) collectors.imu->setEnglish(english);
    if (collectors.ptb) collectors.ptb->setEnglish(english);
    if (collectors.hmp) collectors.hmp->setEnglish(english);
    if (collectors.lidar) collectors.lidar->setEnglish(english);
    if (collectors.temperature_controller) collectors.temperature_controller->setEnglish(english);

    if (state_->rtk_config_dialog_)
    {
        state_->rtk_config_dialog_->setEnglish(english);
    }
    if (state_->session_viewer_window_)
    {
        state_->session_viewer_window_->setEnglish(english);
    }

    if (isRemoteSkyMode())
    {
        refreshRemoteSkyDataUi();
    }
    else
    {
        updateEnvironmentStatusIcons(state_->current_lidar_.valid, state_->current_ptb_.valid, state_->current_hmp_.valid);
    }
    updateSourceModeUi();
    updateDeviceConfigTexts();
    updateSidebarNavIcons();
    updateRecordingStatusLabel();
}

void MainWindow::onOpenSessionViewerClicked()
{
    showBusyStatusTaskProgress(state_->is_english_ ? "Opening Data Viewer..." : "正在打开数据查看器...");
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    if (!state_->session_viewer_window_)
    {
        state_->session_viewer_window_ = new SessionViewerWindow();
        state_->session_viewer_window_->setAttribute(Qt::WA_QuitOnClose, false);
        connect(state_->session_viewer_window_, &QObject::destroyed, this, [this]() {
            state_->session_viewer_window_ = nullptr;
        });
        state_->session_viewer_window_->setEnglish(state_->is_english_);
    }

    state_->session_viewer_window_->setDefaultDataDirectory(
        state_->recording_directory_.isEmpty() ? defaultRecordingDirectory() : state_->recording_directory_);

    const bool wasMinimized =
        state_->session_viewer_window_->isMinimized() ||
        state_->session_viewer_window_->windowState().testFlag(Qt::WindowMinimized);
    const bool restoreMaximized =
        state_->session_viewer_window_->isMaximized() ||
        state_->session_viewer_window_->windowState().testFlag(Qt::WindowMaximized);
    if (!wasMinimized)
    {
        VaporView::centerWindowOnScreen(state_->session_viewer_window_, this);
    }
    if (wasMinimized)
    {
        state_->session_viewer_window_->setWindowState(
            state_->session_viewer_window_->windowState() & ~Qt::WindowMinimized);
        if (restoreMaximized)
        {
            state_->session_viewer_window_->showMaximized();
        }
        else
        {
            state_->session_viewer_window_->showNormal();
        }
    }
    else
    {
        state_->session_viewer_window_->show();
    }
    state_->session_viewer_window_->raise();
    state_->session_viewer_window_->activateWindow();
    hideStatusTaskProgress();
}

#ifdef VAPORVIEW_HAS_OSGEARTH
void MainWindow::onOpenMap3DWindowClicked()
{
    if (state_->map3d_controller_)
    {
        state_->map3d_controller_->open();
    }
}

void MainWindow::onOpenMap3DDiagnosticsClicked()
{
    if (state_->map3d_controller_)
    {
        state_->map3d_controller_->showDiagnostics();
    }
}
#else
void MainWindow::onOpenMap3DWindowClicked()
{
    QMessageBox::information(this,
                             QStringLiteral("VaporView 3D Map"),
                             state_->is_english_
                                 ? QStringLiteral("3D map module is not enabled. Rebuild with -DVAPORVIEW_ENABLE_OSGEARTH=ON.")
                                 : QStringLiteral("三维地图模块未启用。请使用 -DVAPORVIEW_ENABLE_OSGEARTH=ON 重新构建。"));
}

void MainWindow::onOpenMap3DDiagnosticsClicked()
{
    onOpenMap3DWindowClicked();
}
#endif

void MainWindow::onSwitchLanguage()
{
    if (state_->language_switch_in_progress_)
    {
        return;
    }

    state_->language_switch_in_progress_ = true;
    QTimer::singleShot(0, this, [this]() {
        state_->is_english_ = !state_->is_english_;
        setEnglish(state_->is_english_);
        log(state_->is_english_ ? "Language switched to English" : "语言已切换为中文");
        state_->language_switch_in_progress_ = false;
    });
}
void MainWindow::showAboutDialog()
{
    const QString title = state_->is_english_ ? QStringLiteral("About VaporView") : QStringLiteral("关于 VaporView");
    const QString text = state_->is_english_
        ? QStringLiteral(
              "VaporView Application\n\n"
              "Version 1.0.1\n\n"
              "Integrated navigation and environment monitoring system.\n\n"
              "Supported devices:\n"
              "- EPSILON Integrated Navigation (FDILink)\n"
              "- PTB210 Barometer\n"
              "- HMP3 Temperature/Humidity Sensor\n"
              "- TFA1500-L Laser Rangefinder")
        : QStringLiteral(
              "VaporView 应用程序\n\n"
              "版本 1.0.1\n\n"
              "组合导航与环境监控系统。\n\n"
              "支持的设备:\n"
              "- EPSILON 组合导航一体机 (FDILink)\n"
              "- PTB210 气压计\n"
              "- HMP3 温湿度传感器\n"
              "- TFA1500-L 激光测距模块");
    QMessageBox::about(this, title, text);
}

void MainWindow::updateThemeAction()
{
    if (!state_->theme_toggle_action_)
    {
        return;
    }

    const bool targetLight = state_->dark_theme_enabled_;
    state_->theme_toggle_action_->setIcon(targetLight ? createLightThemeIcon() : createDarkThemeIcon());
    state_->theme_toggle_action_->setText(targetLight
        ? (state_->is_english_ ? "Light Theme" : "亮色模式")
        : (state_->is_english_ ? "Dark Theme" : "暗色模式"));
    state_->theme_toggle_action_->setToolTip(targetLight
        ? (state_->is_english_ ? "Switch to light theme" : "切换到亮色模式")
        : (state_->is_english_ ? "Switch to dark theme" : "切换到暗色模式"));
    state_->theme_toggle_action_->setStatusTip(state_->theme_toggle_action_->toolTip());
}

void MainWindow::updateThemedIcons()
{
    if (state_->lang_action_)
    {
        state_->lang_action_->setIcon(createLanguageIcon());
    }
    if (state_->refresh_ports_btn_)
    {
        state_->refresh_ports_btn_->setIcon(createRefreshIcon());
    }
    if (state_->connect_btn_)
    {
        state_->connect_btn_->setIcon(createConnectIcon());
    }
    if (state_->cancel_connect_btn_)
    {
        state_->cancel_connect_btn_->setIcon(createCancelIcon());
    }
    if (state_->disconnect_btn_)
    {
        state_->disconnect_btn_->setIcon(createDisconnectIcon());
    }
    if (state_->scheduled_recording_action_)
    {
        state_->scheduled_recording_action_->setIcon(createTimerIcon());
    }
    if (state_->start_recording_btn_)
    {
        state_->start_recording_btn_->setIcon(createPlayIcon());
    }
    if (state_->pause_recording_btn_)
    {
        state_->pause_recording_btn_->setIcon(createPauseIcon());
    }
    if (state_->stop_recording_btn_)
    {
        state_->stop_recording_btn_->setIcon(createStopIcon());
    }
    updateRtkConfigIcon();
    if (state_->clear_log_action_)
    {
        state_->clear_log_action_->setIcon(createClearLogIcon());
    }
    if (state_->session_viewer_action_)
    {
        state_->session_viewer_action_->setIcon(createWaveformViewerIcon());
    }
#ifdef VAPORVIEW_HAS_OSGEARTH
    if (state_->map3d_action_)
    {
        state_->map3d_action_->setIcon(createLucideIcon(QStringLiteral("earth"), toolbarColor(AppThemeColor::ToolbarBlue)));
    }
    if (state_->map3d_diagnostics_action_)
    {
        state_->map3d_diagnostics_action_->setIcon(createLucideIcon(QStringLiteral("activity"), toolbarColor(AppThemeColor::ToolbarBlue)));
    }
#endif
    if (state_->temperature_overview_panel_)
    {
        state_->temperature_overview_panel_->updateThemedIcons();
    }
    updateSidebarNavIcons();
    updateCustomLogoPixmap();
    if (state_->log_filter_btn_)
    {
        state_->log_filter_btn_->setIcon(createLogFilterIcon());
    }
    updateFontScaleMenuCheckIcons();
    updateLogSidePanelToggleButton();
    updateSectionTitleIcons(this, state_->dark_theme_enabled_);
    updateLogFilterAction();
}

void MainWindow::updateRtkConfigIcon()
{
    const QString baseText = state_->is_english_ ? QStringLiteral("RTK config") : QStringLiteral("RTK配置");
    const QString stateText = state_->rtk_service_running_
        ? (state_->is_english_ ? QStringLiteral("running") : QStringLiteral("运行中"))
        : (state_->is_english_ ? QStringLiteral("stopped") : QStringLiteral("未启动"));
    const QString statusTip = QStringLiteral("%1 (%2)").arg(baseText, stateText);
    if (state_->rtk_config_action_)
    {
        state_->rtk_config_action_->setIcon(createRtkSatelliteIcon(state_->rtk_service_running_));
        state_->rtk_config_action_->setToolTip(statusTip);
        state_->rtk_config_action_->setStatusTip(statusTip);
    }
    if (state_->rtk_config_nav_btn_)
    {
        state_->rtk_config_nav_btn_->setToolTip(statusTip);
        state_->rtk_config_nav_btn_->setStatusTip(statusTip);
    }
}

void MainWindow::updateFontScaleMenuCheckIcons()
{
    const QIcon checkIcon = createMenuCheckIcon(state_->dark_theme_enabled_);
    const auto applyIcon = [this, &checkIcon](QAction *action, int minPercent, int maxPercent) {
        if (!action)
        {
            return;
        }
        action->setIcon(state_->font_scale_percent_ >= minPercent && state_->font_scale_percent_ <= maxPercent ? checkIcon : QIcon());
    };

    applyIcon(state_->font_tiny_action_, 70, 75);
    applyIcon(state_->font_extra_small_action_, 76, 85);
    applyIcon(state_->font_small_action_, 86, 95);
    applyIcon(state_->font_normal_action_, 96, 107);
    applyIcon(state_->font_large_action_, 108, 122);
    applyIcon(state_->font_extra_large_action_, 123, 150);
}

QString MainWindow::currentMainPageTitleText() const
{
    const int pageIndex = state_->main_page_stack_ ? state_->main_page_stack_->currentIndex() : 0;
    if (state_->app_nav_button_group_)
    {
        if (QAbstractButton *button = state_->app_nav_button_group_->button(pageIndex))
        {
            const QString accessibleName = button->accessibleName().trimmed();
            if (!accessibleName.isEmpty())
            {
                return accessibleName;
            }
            const QString text = button->text().trimmed();
            if (!text.isEmpty())
            {
                return text;
            }
            const QString toolTip = button->toolTip().trimmed();
            if (!toolTip.isEmpty())
            {
                return toolTip;
            }
        }
    }

    switch (pageIndex)
    {
    case 1:
        return state_->is_english_ ? QStringLiteral("Device") : QStringLiteral("设备配置");
    case 2:
        return state_->is_english_ ? QStringLiteral("Thermal") : QStringLiteral("温控");
    case 0:
    default:
        return state_->is_english_ ? QStringLiteral("Home") : QStringLiteral("首页");
    }
}

void MainWindow::updateCustomTitleBarTexts()
{
    if (state_->custom_title_label_)
    {
        state_->custom_title_label_->setText(currentMainPageTitleText());
    }
    if (state_->title_menu_btn_)
    {
        state_->title_menu_btn_->setToolTip(state_->is_english_ ? "Menu" : "菜单");
        state_->title_menu_btn_->setStatusTip(state_->title_menu_btn_->toolTip());
    }
    updateCustomLogoTooltip();
    if (state_->title_language_btn_)
    {
        state_->title_language_btn_->setToolTip(state_->is_english_ ? "Switch to Chinese" : "切换到英文");
        state_->title_language_btn_->setStatusTip(state_->is_english_ ? "Switch interface language" : "切换界面语言");
    }
    updateLogSidePanelToggleButton();
    if (state_->window_minimize_btn_)
    {
        state_->window_minimize_btn_->setToolTip(state_->is_english_ ? "Minimize" : "最小化");
        state_->window_minimize_btn_->setStatusTip(state_->window_minimize_btn_->toolTip());
    }
    if (state_->window_close_btn_)
    {
        state_->window_close_btn_->setToolTip(state_->is_english_ ? "Close" : "关闭");
        state_->window_close_btn_->setStatusTip(state_->window_close_btn_->toolTip());
    }
    updateWindowControlButtons();
}

void MainWindow::updateCustomTitleBarStyle()
{
    if (!state_->custom_title_bar_)
    {
        return;
    }

    state_->custom_title_bar_->setFixedHeight(scalePixels(48));
    const QSize actionButtonSize(scalePixels(34), scalePixels(34));
    const QSize windowButtonSize(scalePixels(34), scalePixels(34));
    const QSize iconSize(scalePixels(24), scalePixels(24));
    const QSize maximizeIconSize(scalePixels(21), scalePixels(21));

    const auto buttons = state_->custom_title_bar_->findChildren<QToolButton *>();
    for (QToolButton *button : buttons)
    {
        if (!button)
        {
            continue;
        }
        const bool windowButton = button == state_->window_minimize_btn_ ||
                                  button == state_->window_maximize_btn_ ||
                                  button == state_->window_close_btn_;
        button->setFixedSize(windowButton ? windowButtonSize : actionButtonSize);
        button->setIconSize(iconSize);
    }

    if (state_->window_maximize_btn_)
    {
        state_->window_maximize_btn_->setIconSize(maximizeIconSize);
    }
    if (state_->log_clear_btn_)
    {
        state_->log_clear_btn_->setFixedSize(actionButtonSize);
        state_->log_clear_btn_->setIconSize(iconSize);
    }
    if (state_->log_filter_btn_)
    {
        state_->log_filter_btn_->setFixedSize(actionButtonSize);
        state_->log_filter_btn_->setIconSize(iconSize);
    }
    updateCustomLogoPixmap();
    updateCustomLogoTooltip();
    const QIcon logoIcon = createVaporViewLogoIcon(state_->dark_theme_enabled_);
    if (!logoIcon.isNull())
    {
        setWindowIcon(logoIcon);
        qApp->setWindowIcon(logoIcon);
    }

    if (state_->title_menu_btn_)
    {
        state_->title_menu_btn_->setIcon(createTitleBarIcon(QStringLiteral("menu"), state_->dark_theme_enabled_));
    }
    if (state_->title_language_btn_)
    {
        state_->title_language_btn_->setIcon(createLanguageIcon());
    }
    updateLogSidePanelToggleButton();
    if (state_->title_application_panel_)
    {
        state_->title_application_panel_->hide();
        state_->title_application_panel_->setStyleSheet(titleApplicationPanelStyleSheet(state_->dark_theme_enabled_));
    }
    if (state_->title_application_sub_panel_)
    {
        state_->title_application_sub_panel_->hide();
        state_->title_application_sub_panel_->setStyleSheet(titleApplicationPanelStyleSheet(state_->dark_theme_enabled_));
    }
    if (state_->title_application_nested_panel_)
    {
        state_->title_application_nested_panel_->hide();
        state_->title_application_nested_panel_->setStyleSheet(titleApplicationPanelStyleSheet(state_->dark_theme_enabled_));
    }
    if (state_->window_minimize_btn_)
    {
        state_->window_minimize_btn_->setIcon(createTitleBarIcon(QStringLiteral("minus"), state_->dark_theme_enabled_));
    }
    if (state_->window_close_btn_)
    {
        state_->window_close_btn_->setIcon(createTitleBarIcon(QStringLiteral("x"), state_->dark_theme_enabled_));
    }
    updateWindowControlButtons();
}

void MainWindow::updateWindowControlButtons()
{
    if (!state_->window_maximize_btn_)
    {
        return;
    }

    const bool shouldRestore = isWindowMaximizedForUi();
    state_->window_maximize_btn_->setIcon(createTitleBarIcon(shouldRestore ? QStringLiteral("copy") : QStringLiteral("square"),
                                                     state_->dark_theme_enabled_));
    state_->window_maximize_btn_->setToolTip(shouldRestore
        ? (state_->is_english_ ? "Restore" : "还原")
        : (state_->is_english_ ? "Maximize" : "最大化"));
    state_->window_maximize_btn_->setStatusTip(state_->window_maximize_btn_->toolTip());
}

void MainWindow::toggleWindowMaximized()
{
    if (isFullScreen())
    {
        return;
    }

    if (isWindowMaximizedForUi())
    {
        const QRect restoreGeometry = state_->normal_window_geometry_.isValid()
            ? state_->normal_window_geometry_
            : fallbackNormalWindowGeometry();
        setWindowState(windowState() & ~Qt::WindowMaximized);
        showNormal();
        if (restoreGeometry.isValid())
        {
            setGeometry(restoreGeometry);
        }
    }
    else
    {
        rememberNormalWindowGeometry();
        showMaximized();
    }

    updateWindowControlButtons();
    updateWindowBorderFrames();
    updateWindowResizeHandles();
    QTimer::singleShot(0, this, &MainWindow::updateWindowControlButtons);
    QTimer::singleShot(60, this, &MainWindow::updateWindowControlButtons);
}

bool MainWindow::isWindowMaximizedForUi() const
{
    if (isMaximized() || windowState().testFlag(Qt::WindowMaximized))
    {
        return true;
    }
    if (isFullScreen())
    {
        return false;
    }

    const QRect availableGeometry = currentScreenAvailableGeometry();
    if (!availableGeometry.isValid())
    {
        return false;
    }

    const int tolerance = std::max(3, scalePixels(3));
    auto coversAvailableGeometry = [&](const QRect& rect) {
        return rect.isValid() &&
               rect.left() <= availableGeometry.left() + tolerance &&
               rect.top() <= availableGeometry.top() + tolerance &&
               rect.right() >= availableGeometry.right() - tolerance &&
               rect.bottom() >= availableGeometry.bottom() - tolerance &&
               rect.width() >= availableGeometry.width() - tolerance &&
               rect.height() >= availableGeometry.height() - tolerance;
    };

    return coversAvailableGeometry(frameGeometry()) || coversAvailableGeometry(geometry());
}

void MainWindow::rememberNormalWindowGeometry()
{
    if (isFullScreen() || isMaximized() || windowState().testFlag(Qt::WindowMaximized))
    {
        return;
    }

    const QRect currentGeometry = geometry();
    if (!currentGeometry.isValid() || currentGeometry.width() <= 0 || currentGeometry.height() <= 0)
    {
        return;
    }

    const QRect availableGeometry = currentScreenAvailableGeometry();
    if (availableGeometry.isValid())
    {
        const int tolerance = std::max(3, scalePixels(3));
        const bool visuallyMaximized =
            currentGeometry.left() <= availableGeometry.left() + tolerance &&
            currentGeometry.top() <= availableGeometry.top() + tolerance &&
            currentGeometry.right() >= availableGeometry.right() - tolerance &&
            currentGeometry.bottom() >= availableGeometry.bottom() - tolerance &&
            currentGeometry.width() >= availableGeometry.width() - tolerance &&
            currentGeometry.height() >= availableGeometry.height() - tolerance;
        if (visuallyMaximized)
        {
            return;
        }
    }

    state_->normal_window_geometry_ = currentGeometry;
}

QRect MainWindow::fallbackNormalWindowGeometry() const
{
    const QRect availableGeometry = currentScreenAvailableGeometry();
    const QSize minimumSize = this->minimumSize().expandedTo(minimumSizeHint());
    QSize targetSize = state_->base_window_size_.expandedTo(minimumSize);
    if (availableGeometry.isValid())
    {
        targetSize = targetSize.boundedTo(availableGeometry.size()).expandedTo(minimumSize.boundedTo(availableGeometry.size()));
        const QPoint topLeft(
            availableGeometry.left() + std::max(0, (availableGeometry.width() - targetSize.width()) / 2),
            availableGeometry.top() + std::max(0, (availableGeometry.height() - targetSize.height()) / 2));
        return QRect(topLeft, targetSize);
    }
    return QRect(QPoint(80, 80), targetSize);
}

QRect MainWindow::currentScreenAvailableGeometry() const
{
    const QScreen *targetScreen = screen();
    if (!targetScreen && windowHandle())
    {
        targetScreen = windowHandle()->screen();
    }
    if (!targetScreen)
    {
        targetScreen = QGuiApplication::primaryScreen();
    }
    return targetScreen ? targetScreen->availableGeometry() : QRect();
}

void MainWindow::setupWindowBorderFrames()
{
    auto createBorder = [this]() {
        auto *border = new QFrame(this);
        border->setAttribute(Qt::WA_TransparentForMouseEvents);
        border->setFocusPolicy(Qt::NoFocus);
        border->setFrameShape(QFrame::NoFrame);
        border->setLineWidth(0);
        border->setAutoFillBackground(false);
        return border;
    };

    state_->window_border_top_ = createBorder();
    state_->window_border_right_ = createBorder();
    state_->window_border_bottom_ = createBorder();
    state_->window_border_left_ = createBorder();

    state_->window_border_top_->hide();
    state_->window_border_bottom_->setStyleSheet(QStringLiteral("background-color: %1; border: none;")
        .arg(appThemeColorName(AppThemeColor::SurfaceSunken, state_->dark_theme_enabled_)));
    const QString verticalBorderStyle = QStringLiteral("background-color: %1; border: none;")
        .arg(appThemeColorName(AppThemeColor::SurfaceSunken, state_->dark_theme_enabled_));
    state_->window_border_left_->setStyleSheet(verticalBorderStyle);
    state_->window_border_right_->setStyleSheet(verticalBorderStyle);

    updateWindowBorderFrames();
}

void MainWindow::updateWindowBorderFrames()
{
    const bool visible = !isFullScreen() && !isWindowMaximizedForUi();
    const int borderThickness = 1;
    if (state_->window_border_top_)
    {
        state_->window_border_top_->setVisible(false);
    }
    if (state_->window_border_left_)
    {
        state_->window_border_left_->setVisible(visible);
        state_->window_border_left_->setGeometry(0, 0, borderThickness, height());
        state_->window_border_left_->raise();
    }
    if (state_->window_border_right_)
    {
        state_->window_border_right_->setVisible(visible);
        state_->window_border_right_->setGeometry(std::max(0, width() - borderThickness), 0, borderThickness, height());
        state_->window_border_right_->raise();
    }
    if (state_->window_border_bottom_)
    {
        state_->window_border_bottom_->setVisible(visible);
        state_->window_border_bottom_->setGeometry(0, std::max(0, height() - borderThickness), width(), borderThickness);
        state_->window_border_bottom_->raise();
    }
}

void MainWindow::setupWindowResizeHandles()
{
    const QVector<Qt::Edges> edges = {
        Qt::TopEdge | Qt::LeftEdge,
        Qt::TopEdge,
        Qt::TopEdge | Qt::RightEdge,
        Qt::LeftEdge,
        Qt::RightEdge,
        Qt::BottomEdge | Qt::LeftEdge,
        Qt::BottomEdge,
        Qt::BottomEdge | Qt::RightEdge,
    };

    state_->window_resize_handles_.reserve(edges.size());
    for (Qt::Edges edgeSet : edges)
    {
        auto *handle = createWindowResizeHandle(edgeSet, this);
        handle->setObjectName(QStringLiteral("windowResizeHandle"));
        state_->window_resize_handles_.append(handle);
    }

    updateWindowResizeHandles();
}

void MainWindow::updateWindowResizeHandles()
{
    if (state_->window_resize_handles_.size() != 8)
    {
        return;
    }

    const bool visible = !isFullScreen() && !isWindowMaximizedForUi();
    const int thickness = scalePixels(8);
    const int w = width();
    const int h = height();
    const int rightX = std::max(0, w - thickness);
    const int bottomY = std::max(0, h - thickness);

    const QVector<QRect> geometries = {
        QRect(0, 0, thickness, thickness),
        QRect(thickness, 0, std::max(0, w - thickness * 2), thickness),
        QRect(rightX, 0, thickness, thickness),
        QRect(0, thickness, thickness, std::max(0, h - thickness * 2)),
        QRect(rightX, thickness, thickness, std::max(0, h - thickness * 2)),
        QRect(0, bottomY, thickness, thickness),
        QRect(thickness, bottomY, std::max(0, w - thickness * 2), thickness),
        QRect(rightX, bottomY, thickness, thickness),
    };

    for (int i = 0; i < state_->window_resize_handles_.size(); ++i)
    {
        QWidget *handle = state_->window_resize_handles_.at(i);
        handle->setVisible(visible);
        handle->setGeometry(geometries.at(i));
        handle->raise();
    }

    updateWindowBorderFrames();
    if (state_->title_application_panel_ && state_->title_application_panel_->isVisible())
    {
        state_->title_application_panel_->raise();
    }
    if (state_->title_application_sub_panel_ && state_->title_application_sub_panel_->isVisible())
    {
        state_->title_application_sub_panel_->raise();
    }
    if (state_->title_application_nested_panel_ && state_->title_application_nested_panel_->isVisible())
    {
        state_->title_application_nested_panel_->raise();
    }
}

void MainWindow::onToggleTheme()
{
    state_->dark_theme_enabled_ = !state_->dark_theme_enabled_;
    if (qApp)
    {
        qApp->setProperty(kAppDarkThemeProperty, state_->dark_theme_enabled_);
    }
    discardTitleApplicationMenuPanel();
    applyStyleConfiguration();
    updateThemeAction();

    QSettings settings("VaporView", "MainWindow");
    settings.setValue("dark_theme_enabled", state_->dark_theme_enabled_);

    log(state_->dark_theme_enabled_
        ? (state_->is_english_ ? "Theme switched to dark" : "已切换为暗色模式")
        : (state_->is_english_ ? "Theme switched to light" : "已切换为亮色模式"));
}

void MainWindow::onFontScaleTriggered(QAction *action)
{
    if (!action)
    {
        return;
    }

    const int percent = action->data().toInt();
    if (percent == state_->font_scale_percent_)
    {
        return;
    }

    setFontScale(percent);
    log(QString(state_->is_english_ ? "Font size set to %1%" : "字体大小已设置为 %1%").arg(percent));
}
