#include "ground/main/GroundMainWindowImplementation.h"
#include "ground/devices/DeviceRatePolicy.h"

bool MainWindow::isRemoteSkyMode() const
{
    return state_->remote_sky_mode_;
}

bool MainWindow::isRemoteSkyTcpMode() const
{
    if (!state_->sky_telemetry_transport_combo_)
    {
        return true;
    }
    return state_->sky_telemetry_transport_combo_->currentData().toString() != QStringLiteral("serial");
}

void MainWindow::onDataSourceModeChanged(int index)
{
    state_->remote_sky_mode_ = index == 1;
    saveRememberedInputState();
    clearRemoteSkyDataUi();
    if (state_->tcp_wave_panel_)
    {
        state_->tcp_wave_panel_->setRemoteSkyMode(state_->remote_sky_mode_);
    }
    updateSourceModeUi();
    updateRecordingActionStates();
}

void MainWindow::updateSourceModeUi()
{
    const bool remote = isRemoteSkyMode();
    const bool localInputsEnabled = !remote && (isUiTestMode() || !state_->is_connected_) &&
        !state_->connection_attempt_in_progress_ && !state_->port_detection_in_progress_ && !state_->epsilon_reconfigure_in_progress_;
    const QList<QWidget*> localWidgets = {state_->epsilon_port_combo_, state_->epsilon_baud_combo_, state_->ptb_port_combo_, state_->ptb_baud_combo_,
                                          state_->hmp_port_combo_, state_->hmp_baud_combo_, state_->lidar_port_combo_, state_->lidar_baud_combo_,
                                          state_->temperature_port_combo_, state_->temperature_baud_combo_,
                                          state_->epsilon_packet_rates_btn_, state_->ptb_rate_combo_, state_->hmp_rate_combo_, state_->lidar_rate_combo_,
                                          state_->temperature_rate_combo_, state_->device_config_.ptb_source_combo,
                                          state_->device_config_.hmp_source_combo};
    for (QWidget *widget : localWidgets)
    {
        if (widget)
        {
            widget->setEnabled(localInputsEnabled);
        }
    }
    if (state_->auto_detect_ports_btn_)
    {
        state_->auto_detect_ports_btn_->setEnabled(!remote && (isUiTestMode() || !state_->is_connected_) && !state_->connection_attempt_in_progress_);
    }
    if (state_->source_mode_switch_)
    {
        state_->source_mode_switch_->setEnabled(isUiTestMode() || (!state_->is_connected_ && !state_->connection_attempt_in_progress_));
        state_->source_mode_switch_->setSwitchChecked(remote, state_->source_mode_switch_->switchChecked() != remote);
    }
    const bool remoteInputsEnabled = remote && (isUiTestMode() || !state_->is_connected_) && !state_->connection_attempt_in_progress_;
    const bool tcpTelemetry = isRemoteSkyTcpMode();
    if (state_->sky_telemetry_transport_combo_) state_->sky_telemetry_transport_combo_->setEnabled(remoteInputsEnabled);
    if (state_->sky_telemetry_port_combo_) state_->sky_telemetry_port_combo_->setEnabled(remoteInputsEnabled && !tcpTelemetry);
    if (state_->sky_telemetry_baud_combo_) state_->sky_telemetry_baud_combo_->setEnabled(remoteInputsEnabled && !tcpTelemetry);
    if (state_->sky_telemetry_tcp_host_edit_) state_->sky_telemetry_tcp_host_edit_->setEnabled(remoteInputsEnabled && tcpTelemetry);
    if (state_->sky_telemetry_tcp_port_spin_) state_->sky_telemetry_tcp_port_spin_->setEnabled(remoteInputsEnabled && tcpTelemetry);
    if (state_->sky_telemetry_row_widget_) state_->sky_telemetry_row_widget_->setVisible(true);
    if (state_->sky_telemetry_transport_lbl_) state_->sky_telemetry_transport_lbl_->setVisible(true);
    if (state_->sky_telemetry_transport_combo_) state_->sky_telemetry_transport_combo_->setVisible(true);
    if (state_->sky_telemetry_port_lbl_) state_->sky_telemetry_port_lbl_->setVisible(!tcpTelemetry);
    if (state_->sky_telemetry_port_combo_) state_->sky_telemetry_port_combo_->setVisible(!tcpTelemetry);
    if (state_->sky_telemetry_baud_lbl_) state_->sky_telemetry_baud_lbl_->setVisible(!tcpTelemetry);
    if (state_->sky_telemetry_baud_combo_) state_->sky_telemetry_baud_combo_->setVisible(!tcpTelemetry);
    if (state_->sky_telemetry_tcp_host_lbl_) state_->sky_telemetry_tcp_host_lbl_->setVisible(tcpTelemetry);
    if (state_->sky_telemetry_tcp_host_edit_) state_->sky_telemetry_tcp_host_edit_->setVisible(tcpTelemetry);
    if (state_->sky_telemetry_tcp_port_lbl_) state_->sky_telemetry_tcp_port_lbl_->setVisible(tcpTelemetry);
    if (state_->sky_telemetry_tcp_port_spin_) state_->sky_telemetry_tcp_port_spin_->setVisible(tcpTelemetry);
    const bool remoteActionsAvailable = remote && (isUiTestMode() ||
        (state_->remote_sky_controller_ && state_->remote_sky_controller_->isOpen()));
    if (state_->sky_device_config_btn_) state_->sky_device_config_btn_->setEnabled(remoteActionsAvailable);
    setRemoteDeviceButtonsEnabled(remoteActionsAvailable);
    updateTemperatureControllerTitleText();
    updateTemperatureTitleButtonsState();
    updateRemoteTelemetrySummaryLabel();
    updateHomeDeviceStatusCapsules();
    updateConfigCardHeightForSourceMode();
    updateDeviceConfigState();
}

int MainWindow::scaledConfiguredHeight(QWidget *widget, int baseHeight) const
{
    if (widget && widget->property(kBaseMinHeightProperty).isValid())
    {
        return scalePixels(baseHeight);
    }
    return baseHeight;
}

int MainWindow::homeDeviceOverviewContentMinimumWidth() const
{
    if (!state_->config_group_)
    {
        return 0;
    }

    QWidget *homeDevices =
        state_->config_group_->findChild<QWidget *>(QStringLiteral("homeOverviewDeviceGrid"));
    if (!homeDevices)
    {
        return 0;
    }

    const QMargins cardMargins = state_->config_group_->layout()
        ? state_->config_group_->layout()->contentsMargins()
        : QMargins();
    QWidget *body = state_->config_group_->findChild<QWidget *>(QStringLiteral("homeOverviewDeviceBody"));
    const QMargins bodyMargins = body && body->layout()
        ? body->layout()->contentsMargins()
        : QMargins(scalePixels(kHomeOverviewBodyPadding),
                   scalePixels(kHomeOverviewBodyPadding),
                   scalePixels(kHomeOverviewBodyPadding),
                   scalePixels(kConfigHomeBodyBottomPadding));
    const int deviceControlsWidth = std::max(
        homeDevices->minimumWidth(),
        std::max(homeDevices->minimumSizeHint().width(), homeDevices->sizeHint().width()));
    return deviceControlsWidth +
           cardMargins.left() +
           cardMargins.right() +
           bodyMargins.left() +
           bodyMargins.right();
}

void MainWindow::updateHomeDeviceOverviewMinimumWidth()
{
    if (!state_->config_group_)
    {
        return;
    }

    if (QWidget *homeDevices =
            state_->config_group_->findChild<QWidget *>(QStringLiteral("homeOverviewDeviceGrid")))
    {
        homeDevices->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        const QList<QWidget *> columns =
            homeDevices->findChildren<QWidget *>(QStringLiteral("homeDeviceColumn"),
                                                  Qt::FindDirectChildrenOnly);
        for (QWidget *column : columns)
        {
            if (QLayout *columnLayout = column->layout())
            {
                columnLayout->setSizeConstraint(QLayout::SetFixedSize);
            }
        }
        if (QLayout *deviceLayout = homeDevices->layout())
        {
            deviceLayout->setSizeConstraint(QLayout::SetFixedSize);
        }
    }

    const int contentMinimumWidth = homeDeviceOverviewContentMinimumWidth();
    state_->config_group_->setMinimumWidth(contentMinimumWidth);

    if (!state_->home_overview_splitter_ || !state_->temperature_overview_group_)
    {
        return;
    }

    const QList<int> sizes = state_->home_overview_splitter_->sizes();
    if (sizes.size() < 2 || sizes.at(0) >= contentMinimumWidth)
    {
        return;
    }

    const int availableWidth = std::max(0,
                                        std::max(state_->home_overview_splitter_->width(),
                                                 sizes.at(0) + sizes.at(1) + state_->home_overview_splitter_->handleWidth()) -
                                            state_->home_overview_splitter_->handleWidth());
    const int rightMinimumWidth = state_->temperature_overview_group_->minimumWidth();
    if (availableWidth < contentMinimumWidth + rightMinimumWidth)
    {
        return;
    }

    state_->home_overview_splitter_->setSizes({
        contentMinimumWidth,
        std::max(rightMinimumWidth, availableWidth - contentMinimumWidth)
    });
}

void MainWindow::updateConfigCardHeightForSourceMode()
{
    if (!state_->config_group_)
    {
        return;
    }

    int minimumHeight = scaledConfiguredHeight(state_->config_group_, kConfigCardMinHeight);
    if (state_->data_telemetry_summary_card_)
    {
        if (QLayout *summaryLayout = state_->data_telemetry_summary_card_->layout())
        {
            summaryLayout->invalidate();
            summaryLayout->activate();
        }
        int summaryHeight = std::max(state_->data_telemetry_summary_card_->sizeHint().height(),
                                     state_->data_telemetry_summary_card_->minimumSizeHint().height());
        summaryHeight = std::max(summaryHeight + scalePixels(kHomeTelemetrySummaryHeightPadding),
                                 scalePixels(kMainPageInputHeight));
        state_->data_telemetry_summary_card_->setMinimumHeight(summaryHeight);
        state_->data_telemetry_summary_card_->setMaximumHeight(summaryHeight);
        const int homeDeviceRowHeight = scalePixels((kHomeDeviceRowHeight * kHomeDeviceGridRows) +
                                                    (kHomeDeviceGridRowGap * (kHomeDeviceGridRows - 1)));
        const int homeBodySpacing = scalePixels(2);
        const int homeBodyTopPadding = scalePixels(kHomeOverviewBodyPadding);
        const int homeBodyBottomPadding = scalePixels(kConfigHomeBodyBottomPadding);
        minimumHeight = std::max(minimumHeight,
                                 kMainPageTitleBarHeight +
                                     homeBodyTopPadding +
                                     homeDeviceRowHeight +
                                     homeBodySpacing +
                                     summaryHeight +
                                     homeBodyBottomPadding +
                                     scalePixels(kConfigCardBottomPadding));
    }

    state_->config_group_->setProperty(kMainCardMinimumHeightProperty, minimumHeight);
    state_->config_group_->setMinimumHeight(minimumHeight);
    if (state_->temperature_overview_group_)
    {
        const int temperatureMinimumHeight = std::max(state_->temperature_overview_group_->minimumSizeHint().height(),
                                                      state_->temperature_overview_group_->sizeHint().height());
        minimumHeight = std::max(minimumHeight, temperatureMinimumHeight);
        state_->temperature_overview_group_->setProperty(kMainCardMinimumHeightProperty, minimumHeight);
        state_->temperature_overview_group_->setMinimumHeight(minimumHeight);
    }
    if (state_->home_overview_splitter_)
    {
        const int currentHeight = state_->home_overview_splitter_->height();
        const int contentMinimumHeight = minimumHeight;
        state_->home_overview_splitter_->setProperty(kMainCardMinimumHeightProperty,
                                                     contentMinimumHeight);
        const int targetHeight = std::max(currentHeight, contentMinimumHeight);
        if (state_->config_group_->minimumHeight() != targetHeight ||
            state_->config_group_->maximumHeight() != targetHeight)
        {
            state_->config_group_->setFixedHeight(targetHeight);
        }
        if (state_->temperature_overview_group_ &&
            (state_->temperature_overview_group_->minimumHeight() != targetHeight ||
             state_->temperature_overview_group_->maximumHeight() != targetHeight))
        {
            state_->temperature_overview_group_->setFixedHeight(targetHeight);
        }
        if (state_->home_overview_splitter_->minimumHeight() != targetHeight ||
            state_->home_overview_splitter_->maximumHeight() != targetHeight)
        {
            state_->home_overview_splitter_->setFixedHeight(targetHeight);
        }
        return;
    }
    if (state_->config_group_->height() < minimumHeight)
    {
        state_->config_group_->setFixedHeight(minimumHeight);
    }
}

void MainWindow::clearRemoteSkyDataUi()
{
    state_->remote_sky_controller_->reset();
    state_->remote_temperature_commands_.clear();
    state_->remote_sky_online_ = false;
    state_->remote_wave_stream_requested_ = false;
    state_->remote_wave_stream_enable_pending_ = false;
    state_->remote_wave_stream_auto_start_ = true;
    state_->remote_status_ = VaporView::TelemetryStatus();
    state_->remote_recording_state_ = 0;

    state_->current_epsilon_ = VaporView::EpsilonData();
    state_->current_gnss_ = VaporView::GnssData();
    state_->current_imu_ = VaporView::ImuData();
    state_->current_ptb_ = VaporView::PtbData();
    state_->current_hmp_ = VaporView::HmpData();
    state_->current_lidar_ = VaporView::LidarData();
    state_->current_temperature_controller_ = VaporView::TemperatureControllerData();

    state_->current_ptb_.error_message = remoteNoDataText(state_->is_english_).toStdString();
    state_->current_hmp_.error_message = remoteNoDataText(state_->is_english_).toStdString();
    state_->current_lidar_.error_message = remoteNoDataText(state_->is_english_).toStdString();
    if (state_->tcp_wave_panel_)
    {
        state_->tcp_wave_panel_->setRemoteWaveTcpState(VaporView::DeviceState::Disconnected);
    }

    if (state_->device_panel_coordinator_)
    {
        state_->device_panel_coordinator_->clearRates();
        state_->device_panel_coordinator_->updateAllData(
            state_->current_epsilon_, state_->current_gnss_, 0,
            state_->current_imu_, 0, state_->current_ptb_, state_->current_hmp_,
            state_->current_lidar_, state_->current_temperature_controller_);
    }
    updateEnvironmentStatusIcons(false, false, false);
    updateSourceModeUi();
    updateHomeDeviceStatusCapsules();
    updateRecordingStatusLabel();
}

void MainWindow::markRemoteSkyLinkClosed()
{
    state_->remote_sky_controller_->markLinkClosed();
    state_->remote_sky_online_ = false;
    state_->remote_wave_stream_requested_ = false;
    state_->remote_wave_stream_enable_pending_ = false;
    state_->remote_temperature_commands_.clear();
    state_->remote_recording_state_ = 0;
    state_->remote_status_.recording_state = 0;
    if (state_->tcp_wave_panel_)
    {
        state_->tcp_wave_panel_->setRemoteWaveTcpState(VaporView::DeviceState::Disconnected);
    }
    refreshRemoteSkyDataUi();
    updateSourceModeUi();
    updateHomeDeviceStatusCapsules();
    updateRecordingStatusLabel();
}

bool MainWindow::remoteDeviceDataValid(VaporView::SkyDeviceId device, qint64 timeout_ms) const
{
    if (!state_->remote_sky_controller_ || !state_->remote_sky_controller_->isOpen())
    {
        return false;
    }
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!state_->remote_sky_controller_->statusFresh(nowMs))
    {
        return false;
    }
    if (state_->remote_sky_controller_->deviceState(device) != VaporView::DeviceState::Connected)
    {
        return false;
    }
    return state_->remote_sky_controller_->deviceDataFresh(device, nowMs, timeout_ms);
}

QString MainWindow::remoteDeviceInvalidText(VaporView::SkyDeviceId device, qint64 timeout_ms) const
{
    if (!state_->remote_sky_controller_ || !state_->remote_sky_controller_->isOpen())
    {
        return remoteDisconnectedText(state_->is_english_);
    }
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (state_->remote_sky_controller_->lastStatusMs() <= 0)
    {
        return remoteNoDataText(state_->is_english_);
    }
    if (!state_->remote_sky_controller_->statusFresh(nowMs))
    {
        return remoteStaleText(state_->is_english_);
    }
    if (state_->remote_sky_controller_->deviceState(device) != VaporView::DeviceState::Connected)
    {
        return remoteDisconnectedText(state_->is_english_);
    }
    const qint64 lastDataMs = state_->remote_sky_controller_->lastDeviceDataMs(device);
    if (lastDataMs <= 0)
    {
        return remoteNoDataText(state_->is_english_);
    }
    if (!state_->remote_sky_controller_->deviceDataFresh(device, nowMs, timeout_ms))
    {
        return remoteStaleText(state_->is_english_);
    }
    return remoteNoDataText(state_->is_english_);
}

double MainWindow::remotePacketRate(VaporView::MsgType type) const
{
    return state_->remote_sky_controller_->packetRate(type);
}

double MainWindow::remoteWaveformPacketRate(quint16 channelId) const
{
    return state_->remote_sky_controller_->waveformPacketRate(channelId);
}

MainWindow::RemoteTelemetrySummarySections MainWindow::remoteTelemetrySummarySections() const
{
    const bool connected = state_->remote_sky_controller_ && state_->remote_sky_controller_->isOpen();

    auto hasDeviceData = [this, connected](VaporView::SkyDeviceId device, qint64 timeoutMs) {
        return connected && remoteDeviceDataValid(device, timeoutMs);
    };

    auto hasText = [this](bool hasData) {
        return hasData
            ? (state_->is_english_ ? QStringLiteral("data") : QStringLiteral("有数据"))
            : (state_->is_english_ ? QStringLiteral("none") : QStringLiteral("无数据"));
    };

    const QString actualWaveRate = formatFrequencyText(
        connected ? static_cast<double>(state_->remote_status_.wave_tcp_actual_rate_hz) : 0.0);
    const double rxBps = connected ? state_->remote_sky_controller_->receiveBitsPerSecond() : 0.0;
    const double txBps = connected ? state_->remote_sky_controller_->transmitBitsPerSecond() : 0.0;

    auto makeItem = [](const QString& label, const QString& value, bool hasData, const QString& valueWidthText = QString()) {
        RemoteTelemetrySummarySections::Item item;
        item.label = label;
        item.value = value;
        item.valueWidthText = valueWidthText;
        item.hasData = hasData;
        return item;
    };

    QList<RemoteTelemetrySummarySections::Item> rateRows;
    QList<RemoteTelemetrySummarySections::Item> linkRows;
    QList<RemoteTelemetrySummarySections::Item> deviceRows;
    const QString frequencyWidthText = QStringLiteral("999.9 Hz");
    const QString bitRateWidthText = QStringLiteral("999.9 Mbps");
    auto appendPacketRate = [&](VaporView::MsgType type, const QString& label) {
        const double rate = remotePacketRate(type);
        rateRows << makeItem(label, formatFrequencyText(rate), connected && rate > 0.0, frequencyWidthText);
    };
    auto appendWaveformRate = [&](quint16 channelId, const QString& label) {
        const double rate = remoteWaveformPacketRate(channelId);
        rateRows << makeItem(label, formatFrequencyText(rate), connected && rate > 0.0, frequencyWidthText);
    };
    auto appendDevice = [&](VaporView::SkyDeviceId device, qint64 timeoutMs, const QString& label) {
        const bool hasData = hasDeviceData(device, timeoutMs);
        deviceRows << makeItem(label, hasText(hasData), hasData);
    };
    if (state_->is_english_)
    {
        appendPacketRate(VaporView::MsgType::TelemetryBasic, QStringLiteral("Basic:"));
        appendPacketRate(VaporView::MsgType::WaveformFeature, QStringLiteral("Feature:"));
        appendPacketRate(VaporView::MsgType::TelemetryStatus, QStringLiteral("Status:"));
        appendWaveformRate(1, QStringLiteral("Wave raw:"));
        appendWaveformRate(4, QStringLiteral("Wave harm.:"));
        rateRows << makeItem(QStringLiteral("Wave capture:"), actualWaveRate, connected && state_->remote_status_.wave_tcp_actual_rate_hz > 0.0f, frequencyWidthText);
        linkRows << makeItem(QStringLiteral("Sky->Ground:"), formatBitRate(rxBps), connected && rxBps > 0.0, bitRateWidthText);
        linkRows << makeItem(QStringLiteral("Ground->Sky:"), formatBitRate(txBps), connected && txBps > 0.0, bitRateWidthText);
        linkRows << makeItem(QStringLiteral("Total:"), formatBitRate(rxBps + txBps), connected && (rxBps + txBps) > 0.0, bitRateWidthText);
        appendDevice(VaporView::SkyDeviceId::Epsilon, 2000, QStringLiteral("EPSILON:"));
        appendDevice(VaporView::SkyDeviceId::Ptb, 3000, QStringLiteral("PTB:"));
        appendDevice(VaporView::SkyDeviceId::Hmp, 3000, QStringLiteral("HMP:"));
        appendDevice(VaporView::SkyDeviceId::Lidar, 2000, QStringLiteral("Lidar:"));
        appendDevice(VaporView::SkyDeviceId::WaveTcp, 3000, QStringLiteral("Wave:"));
    }
    else
    {
        appendPacketRate(VaporView::MsgType::TelemetryBasic, QStringLiteral("基础:"));
        appendPacketRate(VaporView::MsgType::WaveformFeature, QStringLiteral("特征值:"));
        appendPacketRate(VaporView::MsgType::TelemetryStatus, QStringLiteral("状态:"));
        appendWaveformRate(1, QStringLiteral("原始波形:"));
        appendWaveformRate(4, QStringLiteral("谐波波形:"));
        rateRows << makeItem(QStringLiteral("波形采集:"), actualWaveRate, connected && state_->remote_status_.wave_tcp_actual_rate_hz > 0.0f, frequencyWidthText);
        linkRows << makeItem(QStringLiteral("天→地"), formatBitRate(rxBps), connected && rxBps > 0.0, bitRateWidthText);
        linkRows << makeItem(QStringLiteral("地→天"), formatBitRate(txBps), connected && txBps > 0.0, bitRateWidthText);
        linkRows << makeItem(QStringLiteral("合"), formatBitRate(rxBps + txBps), connected && (rxBps + txBps) > 0.0, bitRateWidthText);
        appendDevice(VaporView::SkyDeviceId::Epsilon, 2000, QStringLiteral("EPSILON："));
        appendDevice(VaporView::SkyDeviceId::Ptb, 3000, QStringLiteral("PTB："));
        appendDevice(VaporView::SkyDeviceId::Hmp, 3000, QStringLiteral("HMP："));
        appendDevice(VaporView::SkyDeviceId::Lidar, 2000, QStringLiteral("Lidar："));
        appendDevice(VaporView::SkyDeviceId::WaveTcp, 3000, QStringLiteral("波形："));
    }

    RemoteTelemetrySummarySections sections;
    sections.rateItems = rateRows;
    sections.linkItems = linkRows;
    sections.deviceItems = deviceRows;
    return sections;
}

void MainWindow::updateRemoteTelemetrySummaryLabel()
{
    if (!state_->data_telemetry_summary_card_ && !state_->device_config_.data_telemetry_summary_card)
    {
        return;
    }
    const RemoteTelemetrySummarySections sections = remoteTelemetrySummarySections();
    QStringList summaryRenderTokens{
        QString::number(state_->font_scale_percent_),
        state_->is_english_ ? QStringLiteral("en") : QStringLiteral("zh")};
    const auto appendSummaryRenderTokens = [&summaryRenderTokens](
                                               const QList<RemoteTelemetrySummarySections::Item>& items) {
        summaryRenderTokens << QString::number(items.size());
        for (const RemoteTelemetrySummarySections::Item& item : items)
        {
            summaryRenderTokens << item.label
                                << item.value
                                << item.valueWidthText
                                << (item.hasData ? QStringLiteral("1") : QStringLiteral("0"));
        }
    };
    appendSummaryRenderTokens(sections.rateItems);
    appendSummaryRenderTokens(sections.linkItems);
    appendSummaryRenderTokens(sections.deviceItems);
    const QString summaryRenderKey = summaryRenderTokens.join(QChar(0x1f));
    constexpr auto kSummaryRenderKeyProperty = "vaporViewTelemetrySummaryRenderKey";

    if (state_->data_telemetry_summary_card_)
    {
        state_->data_telemetry_summary_card_->setVisible(true);
    }
    if (state_->device_config_.data_telemetry_summary_card)
    {
        state_->device_config_.data_telemetry_summary_card->setVisible(true);
    }
    const bool homeSummaryCurrent = !state_->data_telemetry_summary_card_ ||
        state_->data_telemetry_summary_card_->property(kSummaryRenderKeyProperty).toString() == summaryRenderKey;
    const bool deviceConfigSummaryCurrent = !state_->device_config_.data_telemetry_summary_card ||
        state_->device_config_.data_telemetry_summary_card->property(kSummaryRenderKeyProperty).toString() == summaryRenderKey;
    if (homeSummaryCurrent && deviceConfigSummaryCurrent)
    {
        if (state_->home_overview_splitter_)
        {
            updateConfigCardHeightForSourceMode();
        }
        return;
    }

    auto clearLayout = [](QLayout *layout) {
        if (!layout)
        {
            return;
        }
        while (QLayoutItem *item = layout->takeAt(0))
        {
            if (QWidget *widget = item->widget())
            {
                delete widget;
            }
            else if (QLayout *childLayout = item->layout())
            {
                while (QLayoutItem *childItem = childLayout->takeAt(0))
                {
                    if (QWidget *childWidget = childItem->widget())
                    {
                        delete childWidget;
                    }
                    delete childItem;
                }
                delete childLayout;
            }
            delete item;
        }
    };
    auto renderSummarySection = [this, &clearLayout](QWidget *summaryParent,
                                                     QVBoxLayout *sectionLayout,
                                                     const QString& title,
                                                     const QList<RemoteTelemetrySummarySections::Item>& items,
                                                     int firstLineItemCount,
                                                     int followingLineItemCount = -1,
                                                     bool useSideTitle = false,
                                                     bool compactAvailabilityValues = false) {
        if (!summaryParent || !sectionLayout)
        {
            return;
        }
        clearLayout(sectionLayout);

        auto addItemLabel = [this, useSideTitle, compactAvailabilityValues](QHBoxLayout *lineLayout,
                                                                            QWidget *lineWidget,
                                                                            const RemoteTelemetrySummarySections::Item& item) {
            auto *pill = new QFrame(lineWidget);
            pill->setObjectName(QStringLiteral("homeTelemetrySummaryPill"));
            pill->setProperty("deviceConfigLink", useSideTitle);
            pill->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            pill->setMinimumHeight(scalePixels(useSideTitle ? 26 : 28));
            auto *pillLayout = new QHBoxLayout(pill);
            const int horizontalPadding = scalePixels(useSideTitle ? 4 : 7);
            pillLayout->setContentsMargins(horizontalPadding, scalePixels(1), horizontalPadding, scalePixels(1));
            pillLayout->setSpacing(scalePixels(useSideTitle ? 2 : 3));

            auto *nameLabel = new QLabel(item.label, pill);
            nameLabel->setObjectName(QStringLiteral("homeTelemetrySummaryNameLabel"));
            nameLabel->setProperty("deviceConfigLink", useSideTitle);
            nameLabel->setProperty("telemetryAvailable", item.hasData);
            nameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            nameLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            nameLabel->setTextFormat(Qt::PlainText);
            nameLabel->ensurePolished();
            nameLabel->setMinimumWidth(nameLabel->fontMetrics().horizontalAdvance(item.label) + scalePixels(1));
            nameLabel->setMinimumHeight(nameLabel->fontMetrics().height() + scalePixels(2));
            pillLayout->addWidget(nameLabel, 0, Qt::AlignVCenter);

            const QString compactValue = item.hasData
                ? (state_->is_english_ ? QStringLiteral("Yes") : QStringLiteral("有"))
                : (state_->is_english_ ? QStringLiteral("No") : QStringLiteral("无"));
            const QString valueText = compactAvailabilityValues ? compactValue : item.value;
            auto *valueLabel = new QLabel(valueText, pill);
            valueLabel->setObjectName(QStringLiteral("homeTelemetrySummaryValueLabel"));
            valueLabel->setProperty("deviceConfigLink", useSideTitle);
            valueLabel->setProperty("telemetryAvailable", item.hasData);
            valueLabel->setFont(numericFontFrom(valueLabel->font()));
            valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            valueLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            valueLabel->setTextFormat(Qt::PlainText);
            valueLabel->ensurePolished();
            valueLabel->setMinimumHeight(valueLabel->fontMetrics().height() + scalePixels(2));
            const QString widthValue = compactAvailabilityValues
                ? (state_->is_english_ ? QStringLiteral("Yes") : QStringLiteral("有"))
                : (item.valueWidthText.isEmpty() ? valueText : item.valueWidthText);
            const int valueWidth = std::max(valueLabel->fontMetrics().horizontalAdvance(widthValue),
                                            valueLabel->fontMetrics().horizontalAdvance(valueText)) + scalePixels(2);
            valueLabel->setMinimumWidth(valueWidth);
            valueLabel->setMaximumWidth(valueWidth);
            pillLayout->addWidget(valueLabel, 0, Qt::AlignVCenter);
            if (!useSideTitle)
            {
                const int pillWidth = pillLayout->contentsMargins().left() +
                                      nameLabel->minimumWidth() +
                                      pillLayout->spacing() +
                                      valueLabel->minimumWidth() +
                                      pillLayout->contentsMargins().right();
                pill->setFixedWidth(pillWidth);
            }

            lineLayout->addWidget(pill, 0, Qt::AlignVCenter);
        };

        auto *lineParent = summaryParent;
        QVBoxLayout *linesLayout = sectionLayout;
        if (useSideTitle)
        {
            auto verticalTitleText = [](const QString& source) {
                if (source.contains(QLatin1Char(' ')))
                {
                    return source.split(QLatin1Char(' '), Qt::SkipEmptyParts).join(QLatin1Char('\n'));
                }

                QStringList characters;
                characters.reserve(source.size());
                for (const QChar ch : source)
                {
                    if (!ch.isSpace())
                    {
                        characters << QString(ch);
                    }
                }
                return characters.join(QLatin1Char('\n'));
            };

            auto *sectionBody = new QWidget(summaryParent);
            auto *sectionBodyLayout = new QHBoxLayout(sectionBody);
            sectionBodyLayout->setContentsMargins(0, 0, 0, 0);
            sectionBodyLayout->setSpacing(8);

            auto *titlePane = new QFrame(sectionBody);
            titlePane->setObjectName(QStringLiteral("deviceTelemetrySectionTitlePane"));
            titlePane->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
            titlePane->setMinimumWidth(kEpsilonSideTitleWidth);
            titlePane->setMaximumWidth(kEpsilonSideTitleWidth);
            auto *titlePaneLayout = new QVBoxLayout(titlePane);
            titlePaneLayout->setContentsMargins(4, 4, 4, 4);
            titlePaneLayout->setSpacing(0);

            auto *titleLabel = new QLabel(titlePane);
            titleLabel->setObjectName(QStringLiteral("deviceTelemetrySectionTitleLabel"));
            titleLabel->setProperty("plainTitle", title);
            titleLabel->setText(verticalTitleText(title));
            titleLabel->setAlignment(Qt::AlignCenter);
            titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            titlePaneLayout->addWidget(titleLabel, 1, Qt::AlignCenter);
            sectionBodyLayout->addWidget(titlePane, 0);

            lineParent = new QWidget(sectionBody);
            auto *contentLayout = new QVBoxLayout(lineParent);
            contentLayout->setContentsMargins(0, 2, 6, 2);
            contentLayout->setSpacing(2);
            linesLayout = contentLayout;
            sectionBodyLayout->addWidget(lineParent, 1);
            sectionLayout->addWidget(sectionBody, 0, Qt::AlignTop);
        }

        int renderedLineCount = 0;
        auto addLine = [&](int begin, int end, bool includeTitle) {
            ++renderedLineCount;
            auto *line = new QWidget(lineParent);
            line->setFixedHeight(scalePixels(useSideTitle ? 26 : 28));
            auto *lineLayout = new QHBoxLayout(line);
            lineLayout->setContentsMargins(0, 0, 0, 0);
            lineLayout->setSpacing(scalePixels(useSideTitle ? 4 : 2));

            if (!useSideTitle && includeTitle && !title.isEmpty())
            {
                auto *titleLabel = new QLabel(line);
                titleLabel->setObjectName(QStringLiteral("homeTelemetrySummaryTitleLabel"));
                titleLabel->setProperty("skyTelemetryTitle", true);
                titleLabel->setText(title + (state_->is_english_ ? QStringLiteral(":") : QStringLiteral("：")));
                titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
                titleLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
                titleLabel->setMinimumWidth(titleLabel->fontMetrics().horizontalAdvance(titleLabel->text()) + scalePixels(4));
                lineLayout->addWidget(titleLabel, 0, Qt::AlignVCenter);
            }

            for (int i = begin; i < end; ++i)
            {
                addItemLabel(lineLayout, line, items.at(i));
            }
            lineLayout->addStretch(1);
            linesLayout->addWidget(line, 0, Qt::AlignLeft | Qt::AlignTop);
        };

        const int itemCount = static_cast<int>(items.size());
        const int firstLineCount = (firstLineItemCount < 0 || firstLineItemCount >= itemCount)
            ? itemCount
            : firstLineItemCount;
        addLine(0, firstLineCount, true);

        if (firstLineCount < itemCount)
        {
            const int remainingLineCount = followingLineItemCount > 0
                ? followingLineItemCount
                : itemCount - firstLineCount;
            for (int begin = firstLineCount; begin < itemCount; begin += remainingLineCount)
            {
                addLine(begin, std::min(begin + remainingLineCount, itemCount), false);
            }
        }

        sectionLayout->invalidate();
        sectionLayout->activate();
        if (QWidget *section = qobject_cast<QWidget *>(sectionLayout->parent()))
        {
            const int rowHeight = scalePixels(useSideTitle ? 26 : 28);
            const int rowSpacing = scalePixels(2);
            const int rowMargins = useSideTitle
                ? scalePixels(4)
                : sectionLayout->contentsMargins().top() + sectionLayout->contentsMargins().bottom();
            const int borderAllowance = scalePixels(2);
            const int sectionHeight = rowMargins +
                                      (renderedLineCount * rowHeight) +
                                      (std::max(0, renderedLineCount - 1) * rowSpacing) +
                                      borderAllowance;
            section->setFixedHeight(sectionHeight);
            section->adjustSize();
            section->updateGeometry();
        }
    };
    if (state_->data_telemetry_summary_card_)
    {
        renderSummarySection(state_->data_telemetry_summary_card_,
                             state_->data_telemetry_summary_layout_,
                             state_->is_english_ ? QStringLiteral("Sky-ground data stream rates") : QStringLiteral("天地数据流频率"),
                             sections.rateItems,
                             3,
                             3);
        renderSummarySection(state_->data_telemetry_summary_card_,
                             state_->data_telemetry_link_summary_layout_,
                             state_->is_english_ ? QStringLiteral("Link rate") : QStringLiteral("链路速率"),
                             sections.linkItems,
                             -1);
        renderSummarySection(state_->data_telemetry_summary_card_,
                             state_->data_telemetry_device_summary_layout_,
                             state_->is_english_ ? QStringLiteral("Data") : QStringLiteral("数据"),
                             sections.deviceItems,
                             -1,
                             -1,
                             false,
                             true);
        if (QLayout *summaryLayout = state_->data_telemetry_summary_card_->layout())
        {
            summaryLayout->invalidate();
            summaryLayout->activate();
        }
        state_->data_telemetry_summary_card_->updateGeometry();
        updateHomeDeviceOverviewMinimumWidth();
        state_->data_telemetry_summary_card_->setProperty(kSummaryRenderKeyProperty, summaryRenderKey);
    }
    if (state_->device_config_.data_telemetry_summary_card)
    {
        renderSummarySection(state_->device_config_.data_telemetry_summary_card,
                             state_->device_config_.data_telemetry_rate_summary_layout,
                             state_->is_english_ ? QStringLiteral("Data stream rates") : QStringLiteral("数据频率"),
                             sections.rateItems,
                             2,
                             2,
                             true);
        renderSummarySection(state_->device_config_.data_telemetry_summary_card,
                             state_->device_config_.data_telemetry_link_summary_layout,
                             state_->is_english_ ? QStringLiteral("Link rate") : QStringLiteral("链路速率"),
                             sections.linkItems,
                             1,
                             1,
                             true);
        renderSummarySection(state_->device_config_.data_telemetry_summary_card,
                             state_->device_config_.data_telemetry_device_summary_layout,
                             state_->is_english_ ? QStringLiteral("Data") : QStringLiteral("数据"),
                             sections.deviceItems,
                             3,
                             3,
                             true,
                             true);
        if (QLayout *summaryLayout = state_->device_config_.data_telemetry_summary_card->layout())
        {
            summaryLayout->invalidate();
            summaryLayout->activate();
        }
        state_->device_config_.data_telemetry_summary_card->updateGeometry();
        state_->device_config_.data_telemetry_summary_card->setProperty(kSummaryRenderKeyProperty, summaryRenderKey);
    }
    if (state_->home_overview_splitter_)
    {
        updateConfigCardHeightForSourceMode();
    }
}

void MainWindow::setDeviceConfigEpsilonPacketRates(const std::map<uint8_t, int>& packetRates)
{
    for (QComboBox *combo : state_->device_config_.epsilon_packet_rate_combos)
    {
        if (!combo)
        {
            continue;
        }
        const auto packetId = static_cast<uint8_t>(combo->property("epsilonPacketId").toUInt());
        const auto it = packetRates.find(packetId);
        if (it == packetRates.end())
        {
            continue;
        }
        const int index = combo->findData(it->second);
        if (index >= 0)
        {
            const QSignalBlocker blocker(combo);
            combo->setCurrentIndex(index);
        }
    }
}

std::map<uint8_t, int> MainWindow::deviceConfigEpsilonPacketRates() const
{
    const QString epsilonRateText = state_->epsilon_rate_combo_ ? state_->epsilon_rate_combo_->currentText() : QStringLiteral("100");
    const int groupedRateHz = effectiveRateOrDefault(epsilonRateText, kDefaultEpsilonSampleRateHz, 200);
    std::map<uint8_t, int> packetRates = groupedEpsilonPacketRates(groupedRateHz);
    for (QComboBox *combo : state_->device_config_.epsilon_packet_rate_combos)
    {
        if (!combo)
        {
            continue;
        }
        const auto packetId = static_cast<uint8_t>(combo->property("epsilonPacketId").toUInt());
        const QVariant rateValue = combo->currentData();
        if (rateValue.isValid())
        {
            packetRates[packetId] = rateValue.toInt();
        }
    }
    return packetRates;
}

void MainWindow::syncDeviceConfigEpsilonPanelFromSettings()
{
    if (!state_->device_config_.epsilon_config_card)
    {
        return;
    }

    const QString epsilonRateText = state_->epsilon_rate_combo_ ? state_->epsilon_rate_combo_->currentText() : QStringLiteral("100");
    const int groupedRateHz = effectiveRateOrDefault(epsilonRateText, kDefaultEpsilonSampleRateHz, 200);
    QSettings settings = VaporView::applicationConfigSettings();
    settings.beginGroup(QStringLiteral("MainWindow"));
    const bool customEnabled = settings.value("epsilon_custom_packet_rates_enabled", false).toBool();
    const std::map<uint8_t, int> packetRates = customEnabled
        ? loadCustomEpsilonPacketRates(settings, groupedRateHz)
        : groupedEpsilonPacketRates(groupedRateHz);
    if (state_->device_config_.epsilon_packet_custom_check)
    {
        const QSignalBlocker blocker(state_->device_config_.epsilon_packet_custom_check);
        state_->device_config_.epsilon_packet_custom_check->setChecked(customEnabled);
    }
    setDeviceConfigEpsilonPacketRates(packetRates);
}

void MainWindow::saveDeviceConfigEpsilonPacketRates(bool applyAfterSave)
{
    if (!state_->device_config_.epsilon_config_card ||
        state_->connection_attempt_in_progress_ ||
        state_->port_detection_in_progress_ ||
        state_->epsilon_reconfigure_in_progress_)
    {
        return;
    }

    const QString epsilonRateText = state_->epsilon_rate_combo_ ? state_->epsilon_rate_combo_->currentText() : QStringLiteral("100");
    const int groupedRateHz = effectiveRateOrDefault(epsilonRateText, kDefaultEpsilonSampleRateHz, 200);
    const std::map<uint8_t, int> groupedRates = groupedEpsilonPacketRates(groupedRateHz);
    const std::map<uint8_t, int> defaultRates = defaultEpsilonPacketRates();
    const std::map<uint8_t, int> savedPacketRates = deviceConfigEpsilonPacketRates();
    const QString epsilonBaudText = state_->epsilon_baud_combo_
        ? state_->epsilon_baud_combo_->currentText().trimmed()
        : QStringLiteral("921600");
    if (!validateEpsilonPacketBandwidth(savedPacketRates, epsilonBaudText, true))
    {
        return;
    }

    QSettings settings = VaporView::applicationConfigSettings();
    settings.beginGroup(QStringLiteral("MainWindow"));
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        const auto it = savedPacketRates.find(option.packet_id);
        VaporView::setPersistentSetting(settings, epsilonPacketRateSettingsKey(option.packet_id),
                          it != savedPacketRates.end() ? it->second : groupedRates.at(option.packet_id));
    }

    bool hasCustomOverrides = false;
    for (const auto& entry : savedPacketRates)
    {
        const auto groupedIt = groupedRates.find(entry.first);
        if (groupedIt != groupedRates.end() && groupedIt->second != entry.second)
        {
            hasCustomOverrides = true;
            break;
        }
    }
    const bool effectiveCustomEnabled =
        (state_->device_config_.epsilon_packet_custom_check && state_->device_config_.epsilon_packet_custom_check->isChecked()) ||
        hasCustomOverrides;
    VaporView::setPersistentSetting(settings, QStringLiteral("epsilon_custom_packet_rates_enabled"), effectiveCustomEnabled);
    VaporView::setPersistentSetting(settings, QStringLiteral("epsilon_custom_packet_rates_user_saved"), effectiveCustomEnabled);
    VaporView::removePersistentSetting(settings, QStringLiteral("epsilon_last_config_signature"));
    VaporView::removePersistentSetting(settings, QStringLiteral("epsilon_last_config_apply_version"));

    if (hasCustomOverrides &&
        state_->device_config_.epsilon_packet_custom_check &&
        !state_->device_config_.epsilon_packet_custom_check->isChecked())
    {
        const QSignalBlocker blocker(state_->device_config_.epsilon_packet_custom_check);
        state_->device_config_.epsilon_packet_custom_check->setChecked(true);
        log(state_->is_english_
                ? "[EPSILON] Packet-rate overrides detected, so the custom packet-rate profile has been enabled automatically."
                : "[EPSILON] 检测到包频率已偏离分组模式，已自动启用自定义包频率配置。");
    }

    if (effectiveCustomEnabled)
    {
        log(QString(state_->is_english_
                        ? ((savedPacketRates == defaultRates)
                               ? "[EPSILON] Recommended default packet-rate profile saved: %1"
                               : "[EPSILON] Custom packet-rate profile saved: %1")
                        : ((savedPacketRates == defaultRates)
                               ? "[EPSILON] 已保存推荐默认包频率配置: %1"
                               : "[EPSILON] 已保存自定义包频率配置: %1"))
                .arg(epsilonPacketRatesSummary(savedPacketRates)));
    }
    else
    {
        log(QString(state_->is_english_
                        ? "[EPSILON] Custom packet-rate profile disabled. The grouped %1 Hz profile will be used."
                        : "[EPSILON] 已关闭自定义包频率，后续将使用分组 %1 Hz 配置。")
                .arg(groupedRateHz));
    }

    const QString selectText = state_->is_english_ ? "-- Select --" : "未选择";
    const QString epsilonPort = localSerialPortComboValue(state_->epsilon_port_combo_);
    if (applyAfterSave &&
        !state_->recording_service_->isActive() &&
        !epsilonPort.isEmpty() &&
        epsilonPort != selectText &&
        !isRateUnspecified(epsilonRateText))
    {
        log(state_->is_english_
                ? "[EPSILON] Applying the saved packet-rate profile now..."
                : "[EPSILON] 正在应用刚保存的包频率配置...");
        onReconfigureEpsilonClicked();
    }
    else
    {
        log(state_->is_english_
                ? "[EPSILON] Packet-rate profile saved. It will be used on the next connect/reconfigure."
                : "[EPSILON] 包频率配置已保存，将在下次连接或重配时生效。");
    }
}

void MainWindow::updateEnvironmentStatusIcons(bool lidarValid, bool ptbValid, bool hmpValid)
{
    auto updateIcon = [this](QLabel *label, bool valid, const QString& zhName, const QString& enName) {
        if (!label)
        {
            return;
        }
        const QIcon icon = createLucideIcon(valid ? QStringLiteral("check") : QStringLiteral("circle-x"),
                                            valid ? toolbarColor(AppThemeColor::ToolbarGreen) : toolbarColor(AppThemeColor::ToolbarRed));
        label->setPixmap(icon.pixmap(QSize(kEnvStatusIconSize, kEnvStatusIconSize)));
        const QString name = state_->is_english_ ? enName : zhName;
        const QString state = valid
            ? (state_->is_english_ ? QStringLiteral("valid") : QStringLiteral("有效"))
            : (state_->is_english_ ? QStringLiteral("no data") : QStringLiteral("无数据"));
        label->setToolTip(state_->is_english_
            ? QStringLiteral("%1: %2").arg(name, state)
            : QStringLiteral("%1：%2").arg(name, state));
    };

    updateIcon(state_->env_lidar_status_icon_, lidarValid, QStringLiteral("Lidar"), QStringLiteral("Lidar"));
    updateIcon(state_->env_ptb_status_icon_, ptbValid, QStringLiteral("PTB"), QStringLiteral("PTB"));
    updateIcon(state_->env_hmp_status_icon_, hmpValid, QStringLiteral("HMP"), QStringLiteral("HMP"));
}

void MainWindow::refreshRemoteSkyDataUi()
{
    VaporView::EpsilonData epsilon = state_->current_epsilon_;
    VaporView::PtbData ptb = state_->current_ptb_;
    VaporView::HmpData hmp = state_->current_hmp_;
    VaporView::LidarData lidar = state_->current_lidar_;

    const bool epsilonValid = remoteDeviceDataValid(VaporView::SkyDeviceId::Epsilon, 2000);
    const bool ptbValid = remoteDeviceDataValid(VaporView::SkyDeviceId::Ptb, 3000);
    const bool hmpValid = remoteDeviceDataValid(VaporView::SkyDeviceId::Hmp, 3000);
    const bool lidarValid = remoteDeviceDataValid(VaporView::SkyDeviceId::Lidar, 2000);

    epsilon.valid = epsilonValid;
    ptb.valid = ptbValid;
    hmp.valid = hmpValid;
    lidar.valid = lidarValid;
    if (!ptbValid) ptb.error_message = remoteDeviceInvalidText(VaporView::SkyDeviceId::Ptb, 3000).toStdString();
    if (!hmpValid) hmp.error_message = remoteDeviceInvalidText(VaporView::SkyDeviceId::Hmp, 3000).toStdString();
    if (!lidarValid) lidar.error_message = remoteDeviceInvalidText(VaporView::SkyDeviceId::Lidar, 2000).toStdString();

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const double basicRate = state_->remote_sky_controller_->statusFresh(nowMs)
        ? state_->remote_status_.telemetry_basic_rate_hz
        : 0.0;
    if (state_->device_panel_coordinator_)
    {
        DevicePanelRates panelRates;
        panelRates.epsilonHz = epsilonValid ? basicRate : 0.0;
        panelRates.ptbHz = ptbValid ? basicRate : 0.0;
        panelRates.hmpHz = hmpValid ? basicRate : 0.0;
        panelRates.lidarHz = lidarValid ? basicRate : 0.0;
        panelRates.temperatureHz = remoteDeviceDataValid(
            VaporView::SkyDeviceId::TemperatureController, 3000)
            ? state_->remote_status_.status_rate_hz
            : 0.0;
        state_->device_panel_coordinator_->updateRates(panelRates);
        state_->device_panel_coordinator_->updateEnvironmentData(epsilon, ptb, hmp, lidar);
        state_->device_panel_coordinator_->updateTemperatureData(state_->current_temperature_controller_);
    }
    updateEnvironmentStatusIcons(lidarValid, ptbValid, hmpValid);
    updateRemoteTelemetrySummaryLabel();
    updateHomeDeviceStatusCapsules();
}

void MainWindow::requestRemoteWaveTcpConnection(bool connectRequested)
{
    if (isUiTestMode())
    {
        state_->ui_test_model_->setDeviceState(
            VaporView::SkyDeviceId::WaveTcp,
            connectRequested ? VaporView::DeviceState::Connected : VaporView::DeviceState::Disconnected);
        if (state_->tcp_wave_panel_)
        {
            state_->tcp_wave_panel_->setRemoteWaveTcpState(
                connectRequested ? VaporView::DeviceState::Connected : VaporView::DeviceState::Disconnected);
        }
        logUiTest(connectRequested
            ? (state_->is_english_ ? QStringLiteral("Simulated Sky waveform connection enabled") : QStringLiteral("模拟天空端波形连接已开启"))
            : (state_->is_english_ ? QStringLiteral("Simulated Sky waveform connection disabled") : QStringLiteral("模拟天空端波形连接已关闭")));
        updateConnectionStatus(false);
        return;
    }
    state_->remote_wave_stream_requested_ = false;
    state_->remote_wave_stream_auto_start_ = connectRequested;
    if (state_->remote_sky_controller_ && state_->remote_sky_controller_->isOpen())
    {
        if (connectRequested)
        {
            state_->remote_wave_stream_enable_pending_ = true;
            state_->remote_sky_controller_->sendCommand(VaporView::CommandId::EnableWaveformStreaming);
        }
        else
        {
            state_->remote_wave_stream_enable_pending_ = false;
            state_->remote_sky_controller_->sendCommand(VaporView::CommandId::DisableWaveformStreaming);
        }
    }
    if (!connectRequested && state_->tcp_wave_panel_)
    {
        state_->remote_sky_controller_->setDeviceState(VaporView::SkyDeviceId::WaveTcp,
                                                VaporView::DeviceState::Disconnected);
        state_->remote_sky_controller_->clearDeviceData(VaporView::SkyDeviceId::WaveTcp);
        state_->tcp_wave_panel_->setRemoteWaveTcpState(VaporView::DeviceState::Disconnected);
        updateRemoteTelemetrySummaryLabel();
    }
    sendRemoteDeviceCommand(connectRequested ? VaporView::CommandId::ConnectDevice : VaporView::CommandId::DisconnectDevice,
                            VaporView::SkyDeviceId::WaveTcp);
    updateHomeDeviceStatusCapsules();
}

QPushButton *MainWindow::createRemoteDeviceButton(const QString& text, VaporView::CommandId command, VaporView::SkyDeviceId device)
{
    auto *button = new QPushButton(text, this);
    button->setFixedHeight(kMainPageButtonHeight);
    button->setMinimumWidth(60);
    const QString action = command == VaporView::CommandId::ConnectDevice
        ? QStringLiteral("连接")
        : command == VaporView::CommandId::DisconnectDevice
            ? QStringLiteral("断开")
            : QStringLiteral("重连");
    button->setToolTip(QStringLiteral("请求天空端%1 %2").arg(action, skyDeviceDisplayName(device)));
    connect(button, &QPushButton::clicked, this, [this, command, device]() {
        sendRemoteDeviceCommand(command, device);
    });
    return button;
}

void MainWindow::setRemoteDeviceButtonsEnabled(bool enabled)
{
    for (QPushButton *button : {state_->epsilon_remote_connect_btn_, state_->epsilon_remote_disconnect_btn_, state_->epsilon_remote_reconnect_btn_,
                               state_->ptb_remote_connect_btn_, state_->ptb_remote_disconnect_btn_, state_->ptb_remote_reconnect_btn_,
                               state_->hmp_remote_connect_btn_, state_->hmp_remote_disconnect_btn_, state_->hmp_remote_reconnect_btn_,
                               state_->lidar_remote_connect_btn_, state_->lidar_remote_disconnect_btn_, state_->lidar_remote_reconnect_btn_,
                               state_->temperature_remote_connect_btn_, state_->temperature_remote_disconnect_btn_, state_->temperature_remote_reconnect_btn_})
    {
        if (button)
        {
            button->setEnabled(enabled);
        }
    }
    for (QWidget *widget : {state_->epsilon_remote_buttons_widget_, state_->ptb_remote_buttons_widget_, state_->hmp_remote_buttons_widget_, state_->lidar_remote_buttons_widget_, state_->temperature_remote_buttons_widget_})
    {
        if (widget)
        {
            widget->setVisible(true);
        }
    }
}

void MainWindow::updateTemperatureControllerTitleText()
{
    if (!state_->temperature_controller_inline_title_lbl_)
    {
        return;
    }

    const QString portText = localSerialPortComboValue(state_->temperature_port_combo_);
    const bool hasPort = !portText.isEmpty() && !portText.startsWith(QStringLiteral("--"));
    const QString base = state_->is_english_
        ? QStringLiteral("RD105 Laser Driver Board Temperature Controller")
        : QStringLiteral("RD105激光驱动板温控器");
    const QString portDisplay = hasPort
        ? portText
        : (state_->is_english_ ? QStringLiteral("No serial port") : QStringLiteral("未选择串口"));
    state_->temperature_controller_inline_title_lbl_->setText(
        state_->temperature_title_port_combo_ ? QStringLiteral("%1 ·").arg(base) : base);
    state_->temperature_controller_inline_title_lbl_->setToolTip(QString());

    if (!state_->temperature_title_port_combo_)
    {
        return;
    }

    QStringList availablePorts;
    if (state_->temperature_port_combo_)
    {
        for (int index = 0; index < state_->temperature_port_combo_->count(); ++index)
        {
            const QString candidate = localSerialPortItemValue(state_->temperature_port_combo_, index);
            if (!candidate.isEmpty() && !availablePorts.contains(candidate))
            {
                availablePorts.append(candidate);
            }
        }
    }
    if (hasPort && !availablePorts.contains(portText))
    {
        availablePorts.append(portText);
    }

    const QSignalBlocker blocker(state_->temperature_title_port_combo_);
    state_->temperature_title_port_combo_->clear();
    if (!hasPort)
    {
        state_->temperature_title_port_combo_->addItem(portDisplay, QString());
    }
    for (const QString& port : availablePorts)
    {
        state_->temperature_title_port_combo_->addItem(port, port);
    }
    const int selectedIndex = hasPort
        ? state_->temperature_title_port_combo_->findData(portText)
        : 0;
    state_->temperature_title_port_combo_->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);
    state_->temperature_title_port_combo_->setEnabled(
        state_->temperature_port_combo_ && state_->temperature_port_combo_->isEnabled() && !availablePorts.isEmpty());
    state_->temperature_title_port_combo_->ensurePolished();
    const int titlePortWidth = std::clamp(
        state_->temperature_title_port_combo_->fontMetrics().horizontalAdvance(portDisplay) +
            scalePixels(kTemperatureTitlePortChromeWidth),
        scalePixels(kTemperatureTitlePortMinimumWidth),
        scalePixels(kTemperatureTitlePortMaximumWidth));
    state_->temperature_title_port_combo_->setFixedWidth(titlePortWidth);
    const QString portToolTip = state_->is_english_
        ? QStringLiteral("RD105 serial port: %1. Click to choose another port.").arg(portDisplay)
        : QStringLiteral("当前 RD105 串口：%1。点击可选择其他串口。").arg(portDisplay);
    state_->temperature_title_port_combo_->setToolTip(portToolTip);
    state_->temperature_title_port_combo_->setAccessibleName(portToolTip);
    state_->temperature_title_port_combo_->updateGeometry();
}

void MainWindow::updateTemperatureTitleButtonsState()
{
    const QString connectText = state_->is_english_ ? QStringLiteral("Connect") : QStringLiteral("连接");
    const QString disconnectText = state_->is_english_ ? QStringLiteral("Disconnect") : QStringLiteral("断开");
    const QString reconnectText = state_->is_english_ ? QStringLiteral("Reconnect") : QStringLiteral("重连");
    if (state_->temperature_remote_connect_btn_) state_->temperature_remote_connect_btn_->setText(connectText);
    if (state_->temperature_remote_disconnect_btn_) state_->temperature_remote_disconnect_btn_->setText(disconnectText);
    if (state_->temperature_remote_reconnect_btn_) state_->temperature_remote_reconnect_btn_->setText(reconnectText);
    for (QPushButton *button : {state_->temperature_remote_connect_btn_,
                                state_->temperature_remote_disconnect_btn_,
                                state_->temperature_remote_reconnect_btn_})
    {
        if (button)
        {
            fitButtonMinimumWidth(button, 64);
        }
    }

    if (isRemoteSkyMode())
    {
        const bool remoteCommandEnabled = state_->remote_sky_controller_ && state_->remote_sky_controller_->isOpen();
        if (state_->temperature_remote_connect_btn_) state_->temperature_remote_connect_btn_->setEnabled(remoteCommandEnabled);
        if (state_->temperature_remote_disconnect_btn_) state_->temperature_remote_disconnect_btn_->setEnabled(remoteCommandEnabled);
        if (state_->temperature_remote_reconnect_btn_) state_->temperature_remote_reconnect_btn_->setEnabled(remoteCommandEnabled);
        updateRemoteDeviceButtonText(VaporView::SkyDeviceId::TemperatureController,
                                     state_->remote_sky_controller_->deviceState(VaporView::SkyDeviceId::TemperatureController));
        return;
    }

    const bool hasPort = homeDevicePortSelected(VaporView::SkyDeviceId::TemperatureController);
    const bool connected = homeDeviceConnected(VaporView::SkyDeviceId::TemperatureController);
    const bool localActionIdle = !state_->connection_attempt_in_progress_ &&
        !state_->port_detection_in_progress_ && !state_->epsilon_reconfigure_in_progress_;
    const bool canConnect = hasPort && !connected && localActionIdle;
    const bool canDisconnect = connected && localActionIdle;
    const bool canReconnect = hasPort && localActionIdle;
    if (state_->temperature_remote_connect_btn_) state_->temperature_remote_connect_btn_->setEnabled(canConnect);
    if (state_->temperature_remote_disconnect_btn_) state_->temperature_remote_disconnect_btn_->setEnabled(canDisconnect);
    if (state_->temperature_remote_reconnect_btn_) state_->temperature_remote_reconnect_btn_->setEnabled(canReconnect);

    const QString portText = localSerialPortComboValue(state_->temperature_port_combo_);
    const QString portDisplay = hasPort
        ? portText
        : (state_->is_english_ ? QStringLiteral("No serial port selected") : QStringLiteral("未选择串口"));
    if (state_->temperature_remote_connect_btn_)
    {
        state_->temperature_remote_connect_btn_->setToolTip(state_->is_english_
            ? QStringLiteral("Connect the local RD105 on %1").arg(portDisplay)
            : QStringLiteral("连接本地 RD105：%1").arg(portDisplay));
    }
    if (state_->temperature_remote_disconnect_btn_)
    {
        state_->temperature_remote_disconnect_btn_->setToolTip(state_->is_english_
            ? QStringLiteral("Disconnect the local RD105 on %1").arg(portDisplay)
            : QStringLiteral("断开本地 RD105：%1").arg(portDisplay));
    }
    if (state_->temperature_remote_reconnect_btn_)
    {
        state_->temperature_remote_reconnect_btn_->setToolTip(state_->is_english_
            ? QStringLiteral("Reconnect the local RD105 on %1").arg(portDisplay)
            : QStringLiteral("重连本地 RD105：%1").arg(portDisplay));
    }
}

void MainWindow::handleTemperatureTitleButton(VaporView::CommandId command)
{
    if (isRemoteSkyMode())
    {
        sendRemoteDeviceCommand(command, VaporView::SkyDeviceId::TemperatureController);
        return;
    }

    switch (command)
    {
    case VaporView::CommandId::ConnectDevice:
        connectLocalTemperatureController();
        break;
    case VaporView::CommandId::DisconnectDevice:
        disconnectLocalTemperatureController();
        break;
    case VaporView::CommandId::ReconnectDevice:
        reconnectLocalTemperatureController();
        break;
    default:
        break;
    }
}

#ifdef VAPORVIEW_MAIN_WINDOW_TESTING
void MainWindow::testSetLocalTemperatureCommandObserver(std::function<void(VaporView::CommandId)> observer)
{
    state_->local_temperature_command_test_observer_ = std::move(observer);
}
#endif

void MainWindow::connectLocalTemperatureController()
{
#ifdef VAPORVIEW_MAIN_WINDOW_TESTING
    if (state_->local_temperature_command_test_observer_)
    {
        state_->local_temperature_command_test_observer_(VaporView::CommandId::ConnectDevice);
        return;
    }
#endif

    if (isRemoteSkyMode() || state_->connection_attempt_in_progress_ ||
        state_->port_detection_in_progress_ || state_->epsilon_reconfigure_in_progress_ ||
        homeDeviceConnected(VaporView::SkyDeviceId::TemperatureController))
    {
        return;
    }

    const QString port = localSerialPortComboValue(state_->temperature_port_combo_);
    const QString selectText = state_->is_english_ ? QStringLiteral("-- Select --") : QStringLiteral("未选择");
    if (port.isEmpty() || port == selectText || port.startsWith(QStringLiteral("--")))
    {
        log(state_->is_english_ ? "Select the local RD105 serial port first." : "请先选择本地 RD105 串口。");
        updateTemperatureTitleButtonsState();
        return;
    }

    const QString baudText = state_->temperature_baud_combo_
        ? state_->temperature_baud_combo_->currentText().trimmed()
        : QStringLiteral("38400");
    bool baudOk = false;
    const int baud = baudText.toInt(&baudOk);
    if (!baudOk || baud <= 0)
    {
        log(QString(state_->is_english_ ? "Invalid RD105 baud rate: %1" : "RD105 波特率无效：%1").arg(baudText));
        return;
    }

    const bool english = state_->is_english_;
    const QString rateText = state_->temperature_rate_combo_
        ? state_->temperature_rate_combo_->currentText()
        : QString::number(kDefaultTemperatureSampleRateHz);
    const bool useDefaultRate = isRateUnspecified(rateText);
    const int rate = effectiveRateOrDefault(rateText,
                                            kDefaultTemperatureSampleRateHz,
                                            kMaxTemperatureSampleRateHz);
    const int slaveAddress = rememberedTemperatureSlaveAddress();
    state_->temperature_sample_rate_ = rate;
    state_->connection_attempt_in_progress_ = true;
    state_->cancel_connection_requested_.store(false);
    invalidateTemperatureControllerDataUi();
    startHomeDeviceActionSpinner(VaporView::SkyDeviceId::TemperatureController);
    updateConnectionStatus(anyCollectorRunning());
    log(QString(english ? "[RD105] Connecting %1 @ %2..." : "[RD105] 正在连接 %1 @ %2...")
            .arg(port, baudText));

    VaporView::Ground::Devices::LocalTemperatureConnectionRequest request;
    request.english = english;
    request.port = port;
    request.baudText = baudText;
    request.baudRate = baud;
    request.sampleRateHz = rate;
    request.slaveAddress = slaveAddress;
    request.usesDefaultRate = useDefaultRate;
    const bool started = state_->local_connection_controller_->connectTemperatureAsync(
        std::move(request),
        [this](bool connected, const QString& resultText) {
            QMetaObject::invokeMethod(this, [this, connected, resultText]() {
                log(resultText);
                state_->connection_attempt_in_progress_ = false;
                state_->cancel_connection_requested_.store(false);
                if (!connected)
                {
                    invalidateTemperatureControllerDataUi();
                }
                updateConnectionStatus(anyCollectorRunning());
                updateTemperatureTitleButtonsState();
            }, Qt::QueuedConnection);
        });
    if (!started)
    {
        state_->connection_attempt_in_progress_ = false;
        updateTemperatureTitleButtonsState();
        log(english
            ? QStringLiteral("Another local connection operation is already running.")
            : QStringLiteral("另一个本地连接操作正在进行中。"));
    }
}

void MainWindow::disconnectLocalTemperatureController()
{
#ifdef VAPORVIEW_MAIN_WINDOW_TESTING
    if (state_->local_temperature_command_test_observer_)
    {
        state_->local_temperature_command_test_observer_(VaporView::CommandId::DisconnectDevice);
        return;
    }
#endif

    if (isRemoteSkyMode() || state_->connection_attempt_in_progress_)
    {
        return;
    }

    if (state_->local_connection_controller_->disconnectTemperatureController())
    {
        log(state_->is_english_ ? "[RD105] Disconnecting local controller..."
                        : "[RD105] 正在断开本地温控器...");
        log(state_->is_english_ ? "[RD105] Local controller disconnected."
                        : "[RD105] 本地温控器已断开。");
    }
    invalidateTemperatureControllerDataUi();
    updateConnectionStatus(anyCollectorRunning());
    updateTemperatureTitleButtonsState();
}

void MainWindow::reconnectLocalTemperatureController()
{
#ifdef VAPORVIEW_MAIN_WINDOW_TESTING
    if (state_->local_temperature_command_test_observer_)
    {
        state_->local_temperature_command_test_observer_(VaporView::CommandId::ReconnectDevice);
        return;
    }
#endif

    if (isRemoteSkyMode() || state_->connection_attempt_in_progress_ ||
        state_->port_detection_in_progress_ || state_->epsilon_reconfigure_in_progress_)
    {
        return;
    }

    disconnectLocalTemperatureController();
    QTimer::singleShot(0, this, [this]() { connectLocalTemperatureController(); });
}

void MainWindow::sendRemoteDeviceCommand(VaporView::CommandId command, VaporView::SkyDeviceId device)
{
    if (isUiTestMode())
    {
        const bool connected = command != VaporView::CommandId::DisconnectDevice;
        state_->ui_test_model_->setDeviceState(
            device, connected ? VaporView::DeviceState::Connected : VaporView::DeviceState::Disconnected);
        logUiTest(QString(state_->is_english_ ? "Simulated device command: %1 / %2"
                                              : "模拟设备命令：%1 / %2")
                      .arg(VaporView::commandIdName(command), VaporView::deviceStateName(
                          state_->ui_test_model_->deviceState(device))));
        updateConnectionStatus(false);
        return;
    }
    if (!state_->remote_sky_controller_ || !state_->remote_sky_controller_->isOpen())
    {
        log(state_->is_english_ ? "Remote Sky telemetry link is not connected" : "天空端数传链路未连接");
        return;
    }
    state_->remote_sky_controller_->sendDeviceCommand(command, device);
}

void MainWindow::sendRemotePeakSearchRange(quint32 startIndex, quint32 endIndex)
{
    if (isUiTestMode())
    {
        if (state_->tcp_wave_panel_)
        {
            state_->tcp_wave_panel_->applyRemotePeakSearchRange(startIndex, endIndex);
        }
        logUiTest(QString(state_->is_english_ ? "Peak search range applied in memory: [%1, %2)"
                                              : "峰值搜索区间已在内存中应用：[%1, %2)")
                      .arg(startIndex).arg(endIndex));
        return;
    }
    if (!state_->remote_sky_controller_ || !state_->remote_sky_controller_->isOpen())
    {
        log(state_->is_english_ ? "Remote Sky telemetry link is not connected" : "天空端数传链路未连接");
        if (state_->tcp_wave_panel_)
        {
            state_->tcp_wave_panel_->rejectRemotePeakSearchRange(state_->is_english_ ? QStringLiteral("link is not connected") : QStringLiteral("数传链路未连接"));
        }
        return;
    }
    const quint16 seq = state_->remote_sky_controller_->sendPeakSearchRangeCommand(startIndex, endIndex);
    VaporView::PeakSearchRange range;
    range.start_index = startIndex;
    range.end_index = endIndex;
    state_->remote_peak_search_commands_.insert(seq, range);
    log(QString(state_->is_english_
            ? "Peak search range sent to sky: [%1, %2), seq=%3"
            : "峰值搜索区间已下发到天空端：[%1, %2)，序号=%3")
            .arg(startIndex)
            .arg(endIndex == 0 ? QStringLiteral("end") : QString::number(endIndex))
            .arg(seq));
}

void MainWindow::sendTemperatureCommand(VaporView::CommandId command, const VaporView::TemperatureControllerCommand& payload)
{
    if (isUiTestMode())
    {
        state_->ui_test_model_->applyTemperatureCommand(command, payload);
        applyUiTestSnapshot();
        const quint8 channel = payload.channel == 0 ? 1 : payload.channel;
        if (state_->temperature_controller_panel_)
        {
            state_->temperature_controller_panel_->clearCommandPending(command, channel);
            state_->temperature_controller_panel_->setCommandStatus(
                temperatureCommandStatusText(command, channel, false));
        }
        logUiTest(QString(state_->is_english_ ? "RD105 command applied in memory: %1"
                                              : "RD105 命令已在内存中应用：%1")
                      .arg(VaporView::commandIdName(command)));
        restoreTemperatureCommandUi(command, channel);
        return;
    }
    if (isRemoteSkyMode())
    {
        sendRemoteTemperatureCommand(command, payload);
        return;
    }

    const quint8 channel = payload.channel == 0 ? 1 : payload.channel;
    const auto commandResult =
        state_->local_connection_controller_->sendTemperatureCommand(command, payload);
    if (commandResult.status != LocalTemperatureCommandStatus::NotConnected)
    {
        if (state_->temperature_controller_panel_)
        {
            state_->temperature_controller_panel_->markCommandPending(command, payload);
        }
        auto applyConfirmedLocalCommand = [this, command, &payload]() {
            const auto settingsUpdate =
                VaporView::Ground::Devices::applyConfirmedTemperatureCommand(
                    state_->current_temperature_controller_,
                    command,
                    payload);
            VaporView::Ground::Devices::persistTemperatureSerialSettings(settingsUpdate);
            if (settingsUpdate.baudRate && state_->temperature_baud_combo_)
            {
                const QString baudText = QString::number(*settingsUpdate.baudRate);
                const QSignalBlocker blocker(state_->temperature_baud_combo_);
                if (state_->temperature_baud_combo_->findText(baudText) < 0)
                {
                    state_->temperature_baud_combo_->addItem(baudText);
                }
                state_->temperature_baud_combo_->setCurrentText(baudText);
            }
        };

        if (commandResult.status == LocalTemperatureCommandStatus::Confirmed)
        {
            const VaporView::TemperatureControllerData& latest = commandResult.latestData;
            if (latest.valid && latest.timestamp >= state_->current_temperature_controller_.timestamp)
            {
                state_->current_temperature_controller_ = latest;
            }
            applyConfirmedLocalCommand();
            if (state_->temperature_controller_panel_)
            {
                state_->temperature_controller_panel_->clearCommandPending(command, channel);
                state_->temperature_controller_panel_->setCommandStatus(temperatureCommandStatusText(command, channel, false));
            }
            if (state_->device_panel_coordinator_)
            {
                state_->device_panel_coordinator_->updateTemperatureData(state_->current_temperature_controller_);
            }
            log(isTemperatureCommonCommand(command)
                    ? QString(state_->is_english_
                          ? "RD105 local command confirmed: %1"
                          : "RD105 本地命令已确认：%1")
                          .arg(VaporView::commandIdName(command))
                    : QString(state_->is_english_
                          ? "RD105 local command confirmed: %1 channel=%2"
                          : "RD105 本地命令已确认：%1 通道=%2")
                          .arg(VaporView::commandIdName(command))
                          .arg(channel));
            restoreTemperatureCommandUi(command, channel);
            return;
        }

        const QString failedDetail = state_->is_english_
            ? QStringLiteral("write/read-back confirmation failed")
            : QStringLiteral("写入或读回确认失败");
        log(state_->is_english_
            ? QStringLiteral("RD105 local command failed: write/read-back confirmation failed")
            : QStringLiteral("RD105 本地命令失败：写入或读回确认失败"));
        if (state_->temperature_controller_panel_)
        {
            state_->temperature_controller_panel_->clearCommandPending(command, channel);
            state_->temperature_controller_panel_->setCommandStatus(
                temperatureCommandStatusText(command, channel, false, failedDetail),
                true);
        }
        const VaporView::TemperatureControllerData& latest = commandResult.latestData;
        if (latest.timestamp >= state_->current_temperature_controller_.timestamp)
        {
            state_->current_temperature_controller_ = latest;
        }
        restoreTemperatureCommandUi(command, channel);
        return;
    }

    const QString detail = state_->is_english_
        ? QStringLiteral("local RD105 controller is not connected")
        : QStringLiteral("本地 RD105 温控器未连接");
    log(state_->is_english_
        ? QStringLiteral("Local RD105 temperature controller is not connected")
        : QStringLiteral("本地 RD105 温控器未连接，无法下发温控命令"));
    if (state_->temperature_controller_panel_)
    {
        state_->temperature_controller_panel_->clearCommandPending(command, channel);
        state_->temperature_controller_panel_->setCommandStatus(
            temperatureCommandStatusText(command, channel, false, detail),
            true);
    }
    restoreTemperatureCommandUi(command, channel);
}

void MainWindow::sendRemoteTemperatureCommand(VaporView::CommandId command, const VaporView::TemperatureControllerCommand& payload)
{
    if (isUiTestMode())
    {
        sendTemperatureCommand(command, payload);
        return;
    }
    if (!state_->remote_sky_controller_ || !state_->remote_sky_controller_->isOpen())
    {
        log(state_->is_english_ ? "Remote Sky telemetry link is not connected" : "天空端数传链路未连接");
        const quint8 channel = payload.channel == 0 ? 1 : payload.channel;
        if (state_->temperature_controller_panel_)
        {
            state_->temperature_controller_panel_->setCommandStatus(
                temperatureCommandStatusText(command,
                                             channel,
                                             false,
                                             state_->is_english_ ? QStringLiteral("Remote Sky telemetry link is not connected")
                                                         : QStringLiteral("天空端数传链路未连接")),
                true);
        }
        restoreTemperatureCommandUi(command, channel);
        return;
    }
    const quint16 seq = state_->remote_sky_controller_->sendCommand(command, VaporView::TelemetryCodec::serializeTemperatureControllerCommand(payload));
    state_->remote_temperature_commands_.insert(seq, payload);
    if (state_->temperature_controller_panel_)
    {
        state_->temperature_controller_panel_->markCommandPending(command, payload);
        state_->temperature_controller_panel_->setCommandStatus(temperatureCommandStatusText(command, payload.channel, true));
    }
    restoreTemperatureCommandUi(command, payload.channel == 0 ? 1 : payload.channel);
    if (isTemperatureCommonCommand(command))
    {
        log(QString(state_->is_english_
                ? "RD105 command sent: %1 seq=%2"
                : "RD105 命令已下发：%1 序号=%2")
                .arg(VaporView::commandIdName(command))
                .arg(seq));
    }
    else
    {
        log(QString(state_->is_english_
                ? "RD105 command sent: %1 channel=%2 seq=%3"
                : "RD105 命令已下发：%1 通道=%2 序号=%3")
                .arg(VaporView::commandIdName(command))
                .arg(payload.channel)
                .arg(seq));
    }
}

void MainWindow::restoreTemperatureCommandUi(VaporView::CommandId command, quint8 channel)
{
    if (command != VaporView::CommandId::SetTemperatureOutputEnabled)
    {
        return;
    }

    const int channelIndex = static_cast<int>(channel == 0 ? 0 : channel - 1);
    if (channelIndex < 0 || channelIndex >= static_cast<int>(state_->current_temperature_controller_.channels.size()))
    {
        return;
    }

    const bool outputEnabled =
        state_->current_temperature_controller_.valid &&
        state_->current_temperature_controller_.channels[channelIndex].output_enabled;
    if (state_->temperature_controller_panel_)
    {
        state_->temperature_controller_panel_->setOutputEnabledControl(static_cast<quint8>(channelIndex + 1), outputEnabled);
    }
    if (state_->temperature_overview_panel_)
    {
        state_->temperature_overview_panel_->updateData(state_->current_temperature_controller_);
    }
}

bool MainWindow::isTemperatureCommand(VaporView::CommandId command) const
{
    return command == VaporView::CommandId::SetTemperatureTarget ||
           command == VaporView::CommandId::SetTemperatureOutputEnabled ||
           command == VaporView::CommandId::SetTemperatureOutputMode ||
           command == VaporView::CommandId::SetTemperatureMaxOutputPercent ||
           command == VaporView::CommandId::SetTemperaturePid ||
           command == VaporView::CommandId::SetTemperatureAutoPid ||
           command == VaporView::CommandId::SetTemperatureOvertempUpper ||
           command == VaporView::CommandId::SetTemperatureOvertempLower ||
           command == VaporView::CommandId::SetTemperatureSlope ||
           command == VaporView::CommandId::SetTemperatureStartupDelay ||
           command == VaporView::CommandId::SetTemperatureSensorConfig ||
           command == VaporView::CommandId::SetTemperatureControllerMode ||
           command == VaporView::CommandId::SetTemperatureDeviceAddress ||
           command == VaporView::CommandId::SetTemperatureRs485Baud ||
           command == VaporView::CommandId::SetTemperatureOvertempOutputMode ||
           command == VaporView::CommandId::RestoreTemperatureFactoryDefaults;
}

QString MainWindow::temperatureCommandStatusText(VaporView::CommandId command, quint8 channel, bool pending, const QString& detail) const
{
    QString action;
    switch (command)
    {
    case VaporView::CommandId::SetTemperatureTarget:
        action = state_->is_english_ ? QStringLiteral("target temperature") : QStringLiteral("目标温度");
        break;
    case VaporView::CommandId::SetTemperatureOutputEnabled:
        action = state_->is_english_ ? QStringLiteral("output enable") : QStringLiteral("输出使能");
        break;
    case VaporView::CommandId::SetTemperatureOutputMode:
        action = state_->is_english_ ? QStringLiteral("output mode") : QStringLiteral("输出模式");
        break;
    case VaporView::CommandId::SetTemperatureMaxOutputPercent:
        action = state_->is_english_ ? QStringLiteral("max output") : QStringLiteral("最大输出");
        break;
    case VaporView::CommandId::SetTemperaturePid:
        action = state_->is_english_ ? QStringLiteral("PID") : QStringLiteral("PID");
        break;
    case VaporView::CommandId::SetTemperatureAutoPid:
        action = state_->is_english_ ? QStringLiteral("auto PID") : QStringLiteral("自动 PID");
        break;
    case VaporView::CommandId::SetTemperatureOvertempUpper:
        action = state_->is_english_ ? QStringLiteral("high temperature alarm") : QStringLiteral("高温报警");
        break;
    case VaporView::CommandId::SetTemperatureOvertempLower:
        action = state_->is_english_ ? QStringLiteral("low temperature alarm") : QStringLiteral("低温报警");
        break;
    case VaporView::CommandId::SetTemperatureSlope:
        action = state_->is_english_ ? QStringLiteral("temperature rate") : QStringLiteral("温度变化速率");
        break;
    case VaporView::CommandId::SetTemperatureStartupDelay:
        action = state_->is_english_ ? QStringLiteral("startup output delay") : QStringLiteral("开机输出延时");
        break;
    case VaporView::CommandId::SetTemperatureSensorConfig:
        action = state_->is_english_ ? QStringLiteral("sensor config") : QStringLiteral("传感器配置");
        break;
    case VaporView::CommandId::SetTemperatureControllerMode:
        action = state_->is_english_ ? QStringLiteral("controller mode") : QStringLiteral("温控器模式");
        break;
    case VaporView::CommandId::SetTemperatureDeviceAddress:
        action = state_->is_english_ ? QStringLiteral("RS485 address") : QStringLiteral("485站号");
        break;
    case VaporView::CommandId::SetTemperatureRs485Baud:
        action = state_->is_english_ ? QStringLiteral("RS485 baud") : QStringLiteral("485波特率");
        break;
    case VaporView::CommandId::SetTemperatureOvertempOutputMode:
        action = state_->is_english_ ? QStringLiteral("over-temp output mode") : QStringLiteral("过温输出模式");
        break;
    case VaporView::CommandId::RestoreTemperatureFactoryDefaults:
        action = state_->is_english_ ? QStringLiteral("factory reset") : QStringLiteral("恢复出厂设置");
        break;
    default:
        action = VaporView::commandIdName(command);
        break;
    }
    if (isTemperatureCommonCommand(command))
    {
        if (pending)
        {
            return state_->is_english_
                ? QStringLiteral("%1 command sent; waiting for ACK and read-back confirmation...").arg(action)
                : QStringLiteral("%1命令已下发，等待 ACK 和读回确认...").arg(action);
        }
        if (detail.isEmpty())
        {
            return state_->is_english_
                ? QStringLiteral("%1 command confirmed.").arg(action)
                : QStringLiteral("%1命令已确认成功。").arg(action);
        }
        return state_->is_english_
            ? QStringLiteral("%1 command failed: %2").arg(action, detail)
            : QStringLiteral("%1命令失败：%2").arg(action, detail);
    }
    if (pending)
    {
        return state_->is_english_
            ? QStringLiteral("Channel %1 %2 command sent; waiting for ACK and read-back confirmation...").arg(channel).arg(action)
            : QStringLiteral("通道%1%2命令已下发，等待 ACK 和读回确认...").arg(channel).arg(action);
    }
    if (detail.isEmpty())
    {
        return state_->is_english_
            ? QStringLiteral("Channel %1 %2 command confirmed.").arg(channel).arg(action)
            : QStringLiteral("通道%1%2命令已确认成功。" ).arg(channel).arg(action);
    }
    return state_->is_english_
        ? QStringLiteral("Channel %1 %2 command failed: %3").arg(channel).arg(action, detail)
        : QStringLiteral("通道%1%2命令失败：%3").arg(channel).arg(action, detail);
}

void MainWindow::updateRemoteDeviceButtonText(VaporView::SkyDeviceId device, VaporView::DeviceState state)
{
    QPushButton *connectButton = nullptr;
    QPushButton *disconnectButton = nullptr;
    QPushButton *reconnectButton = nullptr;
    switch (device)
    {
    case VaporView::SkyDeviceId::Epsilon:
        connectButton = state_->epsilon_remote_connect_btn_; disconnectButton = state_->epsilon_remote_disconnect_btn_; reconnectButton = state_->epsilon_remote_reconnect_btn_;
        break;
    case VaporView::SkyDeviceId::Ptb:
        connectButton = state_->ptb_remote_connect_btn_; disconnectButton = state_->ptb_remote_disconnect_btn_; reconnectButton = state_->ptb_remote_reconnect_btn_;
        break;
    case VaporView::SkyDeviceId::Hmp:
        connectButton = state_->hmp_remote_connect_btn_; disconnectButton = state_->hmp_remote_disconnect_btn_; reconnectButton = state_->hmp_remote_reconnect_btn_;
        break;
    case VaporView::SkyDeviceId::Lidar:
        connectButton = state_->lidar_remote_connect_btn_; disconnectButton = state_->lidar_remote_disconnect_btn_; reconnectButton = state_->lidar_remote_reconnect_btn_;
        break;
    case VaporView::SkyDeviceId::TemperatureController:
        connectButton = state_->temperature_remote_connect_btn_; disconnectButton = state_->temperature_remote_disconnect_btn_; reconnectButton = state_->temperature_remote_reconnect_btn_;
        break;
    case VaporView::SkyDeviceId::WaveTcp:
        if (state_->tcp_wave_panel_)
        {
            state_->tcp_wave_panel_->setRemoteWaveTcpState(state_->remote_wave_stream_requested_ && state == VaporView::DeviceState::Connected
                ? VaporView::DeviceState::Connected
                : VaporView::DeviceState::Disconnected);
        }
        updateHomeDeviceStatusCapsules();
        return;
    case VaporView::SkyDeviceId::All:
        return;
    }
    const QString stateText = VaporView::deviceStateName(state);
    if (connectButton) connectButton->setToolTip(QStringLiteral("请求天空端连接 %1（当前：%2）").arg(skyDeviceDisplayName(device), stateText));
    if (disconnectButton) disconnectButton->setToolTip(QStringLiteral("请求天空端断开 %1（当前：%2）").arg(skyDeviceDisplayName(device), stateText));
    if (reconnectButton) reconnectButton->setToolTip(QStringLiteral("请求天空端重连 %1（当前：%2）").arg(skyDeviceDisplayName(device), stateText));
    updateDeviceConfigRemoteActionButton(device);
    updateHomeDeviceStatusCapsules();
}

void MainWindow::updateDeviceConfigRemoteActionButton(VaporView::SkyDeviceId device)
{
    QToolButton *button = nullptr;
    switch (device)
    {
    case VaporView::SkyDeviceId::Epsilon:
        button = state_->device_config_.epsilon_remote_action_btn;
        break;
    case VaporView::SkyDeviceId::Ptb:
        button = state_->device_config_.ptb_remote_action_btn;
        break;
    case VaporView::SkyDeviceId::Hmp:
        button = state_->device_config_.hmp_remote_action_btn;
        break;
    case VaporView::SkyDeviceId::Lidar:
        button = state_->device_config_.lidar_remote_action_btn;
        break;
    case VaporView::SkyDeviceId::TemperatureController:
        button = state_->device_config_.temperature_remote_action_btn;
        break;
    case VaporView::SkyDeviceId::WaveTcp:
    case VaporView::SkyDeviceId::All:
        return;
    }
    if (!button)
    {
        return;
    }

    const VaporView::DeviceState state = homeDeviceActionState(device);
    const bool connected = state == VaporView::DeviceState::Connected;
    const bool busy = state == VaporView::DeviceState::Connecting ||
        state == VaporView::DeviceState::Reconnecting ||
        homeDeviceActionSpinnerActive(device, QDateTime::currentMSecsSinceEpoch());
    const VaporView::CommandId command = connected
        ? VaporView::CommandId::DisconnectDevice
        : VaporView::CommandId::ConnectDevice;
    applyDeviceConfigRemoteButtonPresentation(button, command, device, state_->is_english_, false);
    if (busy)
    {
        button->setIcon(createRotatedLucideIcon(QStringLiteral("link"),
                                                toolbarColor(AppThemeColor::HomeDeviceSuccess),
                                                (state_->home_device_action_spinner_step_ * 360) /
                                                    kHomeDeviceActionSpinnerFrames));
    }
    const bool remoteMode = isRemoteSkyMode();
    const bool linkOpen = state_->remote_sky_controller_ && state_->remote_sky_controller_->isOpen();
    const bool enabled = state == VaporView::DeviceState::Disabled || busy
        ? false
        : isUiTestMode()
            ? true
            : remoteMode
                ? linkOpen
                : ((connected && state_->disconnect_btn_ && state_->disconnect_btn_->isEnabled()) ||
                   (!connected && state_->connect_btn_ && state_->connect_btn_->isEnabled()));
    button->setEnabled(enabled);

    const QString actionText = busy
        ? (state_->is_english_ ? QStringLiteral("Connecting") : QStringLiteral("连接中"))
        : connected
            ? (state_->is_english_ ? QStringLiteral("Disconnect") : QStringLiteral("断开"))
            : state == VaporView::DeviceState::Disabled
                ? (remoteMode
                    ? (state_->is_english_ ? QStringLiteral("Connect telemetry first") : QStringLiteral("请先连接数传"))
                    : (state_->is_english_ ? QStringLiteral("Select port first") : QStringLiteral("请先选择串口")))
                : (state_->is_english_ ? QStringLiteral("Connect") : QStringLiteral("连接"));
    const QString deviceName = homeDeviceDisplayName(device, state_->is_english_);
    const QString modeHint = remoteMode
        ? (state_->is_english_ ? QStringLiteral("remote Sky device") : QStringLiteral("天空端设备"))
        : (state_->is_english_ ? QStringLiteral("local serial device") : QStringLiteral("本地串口设备"));
    const QString tooltip = state_->is_english_
        ? QStringLiteral("%1 %2 (%3)").arg(actionText, deviceName, modeHint)
        : QStringLiteral("%1%2（%3）").arg(actionText, deviceName, modeHint);
    button->setToolTip(tooltip);
    button->setAccessibleName(tooltip);
    button->setProperty("connected", connected);
    button->setProperty("state", busy
        ? QStringLiteral("connecting")
        : state == VaporView::DeviceState::Disabled
            ? QStringLiteral("disabled")
            : connected
                ? QStringLiteral("connected")
                : QStringLiteral("disconnected"));
    button->style()->unpolish(button);
    button->style()->polish(button);
}

void MainWindow::setImuFormatSelection(const QString& format)
{
    applyComboText(state_->imu_format_combo_, format);
}

void MainWindow::setImuBaudSelection(int baud)
{
    applyComboText(state_->imu_baud_combo_, QString::number(baud));
}

void MainWindow::setImuRateSelection(int rate)
{
    applyComboText(state_->imu_rate_combo_, QString::number(rate));
    state_->imu_sample_rate_ = parseRate(state_->imu_rate_combo_->currentText());
}

bool MainWindow::applyImuDeviceProfile(const QString& requestedFormat, int requestedBaud, int requestedRate)
{
    if (state_->connection_attempt_in_progress_ || state_->port_detection_in_progress_)
    {
        return false;
    }

    const QString selectText = state_->is_english_ ? "-- Select --" : "未选择";
    const QString port = state_->imu_port_combo_ ? state_->imu_port_combo_->currentText().trimmed() : QString();
    if (port.isEmpty() || port == selectText)
    {
        log(state_->is_english_ ? "Select an IMU serial port first" : "请先选择 IMU 串口");
        return false;
    }

    bool baudOk = false;
    const int currentBaud = (state_->imu_baud_combo_ ? state_->imu_baud_combo_->currentText() : QStringLiteral("921600")).toInt(&baudOk);
    const int effectiveCurrentBaud = baudOk && currentBaud > 0 ? currentBaud : 921600;
    const QString currentFormat = state_->imu_format_combo_ ? state_->imu_format_combo_->currentText().trimmed().toUpper() : QStringLiteral("HI91");
    const int currentRate = parseRate(state_->imu_rate_combo_ ? state_->imu_rate_combo_->currentText() : QStringLiteral("200"));

    const QString targetFormat = requestedFormat.isEmpty() ? currentFormat : requestedFormat.trimmed().toUpper();
    const int targetBaud = requestedBaud > 0 ? requestedBaud : effectiveCurrentBaud;
    const int targetRate = requestedRate > 0 ? requestedRate : currentRate;

    if (!VaporView::Ground::Devices::ImuConfigurationService::isSupported(
            targetFormat,
            targetRate))
    {
        log(state_->is_english_ ? "Unsupported IMU format or rate" : "IMU 输出格式或频率不受支持");
        return false;
    }

    setImuFormatSelection(targetFormat);
    setImuBaudSelection(targetBaud);
    setImuRateSelection(targetRate);
    saveRememberedInputState();

    if (isUiTestMode())
    {
        logUiTest(QString(state_->is_english_ ? "IMU profile applied in memory: %1, %2 baud, %3 Hz"
                                              : "IMU 配置已在内存中应用：%1，%2 波特，%3 Hz")
                      .arg(targetFormat).arg(targetBaud).arg(targetRate));
        return true;
    }

    VaporView::Ground::Devices::ImuProfileRequest request;
    request.english = state_->is_english_;
    request.port = port;
    request.outputFormat = targetFormat;
    request.currentBaud = effectiveCurrentBaud;
    request.targetBaud = targetBaud;
    request.targetRateHz = targetRate;
    return state_->local_connection_controller_->applyImuProfile(request);
}
