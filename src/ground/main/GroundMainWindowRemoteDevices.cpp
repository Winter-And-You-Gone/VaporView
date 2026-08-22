#include "ground/main/GroundMainWindowImplementation.h"
#include "ground/devices/DeviceRatePolicy.h"

#include <QStyle>

#include <algorithm>
#include <tuple>

namespace
{

QString remoteSummaryRecordingStateText(quint8 state, bool english)
{
    switch (state)
    {
    case 1:
        return english ? QStringLiteral("Recording") : QStringLiteral("记录中");
    case 2:
        return english ? QStringLiteral("Paused") : QStringLiteral("已暂停");
    default:
        return english ? QStringLiteral("Idle") : QStringLiteral("未记录");
    }
}

QString remoteSummaryBytesText(quint64 bytes)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4)
    {
        value /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', unit == 0 ? 0 : 1).arg(units[unit]);
}

QString compactAvailabilityText(bool hasData, bool english)
{
    return hasData
        ? (english ? QStringLiteral("Yes") : QStringLiteral("有"))
        : (english ? QStringLiteral("No") : QStringLiteral("无"));
}

QString compactAvailabilityWidthText(bool english)
{
    return english ? QStringLiteral("Yes") : QStringLiteral("有");
}

QString summaryItemValueText(const VaporView::Ground::Main::RemoteTelemetrySummarySections::Item& item,
                             bool compactAvailabilityValues,
                             bool english)
{
    if (compactAvailabilityValues && item.compactAvailabilityValue)
    {
        return compactAvailabilityText(item.hasData, english);
    }
    return item.value;
}

QString summaryItemWidthText(const VaporView::Ground::Main::RemoteTelemetrySummarySections::Item& item,
                             bool compactAvailabilityValues,
                             bool english)
{
    if (compactAvailabilityValues && item.compactAvailabilityValue)
    {
        return compactAvailabilityWidthText(english);
    }
    return item.valueWidthText.isEmpty() ? item.value : item.valueWidthText;
}

void setTelemetryAvailability(QWidget *widget, bool available)
{
    if (!widget || widget->property("telemetryAvailable").toBool() == available)
    {
        return;
    }
    widget->setProperty("telemetryAvailable", available);
    if (QStyle *style = widget->style())
    {
        style->unpolish(widget);
        style->polish(widget);
    }
    widget->update();
}

bool updateSummarySectionValues(QVBoxLayout *sectionLayout,
                                const QList<VaporView::Ground::Main::RemoteTelemetrySummarySections::Item>& items,
                                bool compactAvailabilityValues,
                                bool english)
{
    auto *section = sectionLayout ? qobject_cast<QWidget *>(sectionLayout->parent()) : nullptr;
    if (!section)
    {
        return false;
    }

    QList<QFrame *> pills =
        section->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill"));
    std::stable_sort(pills.begin(), pills.end(), [section](QFrame *left, QFrame *right) {
        const QPoint leftPos = left->mapTo(section, QPoint(0, 0));
        const QPoint rightPos = right->mapTo(section, QPoint(0, 0));
        return std::make_tuple(leftPos.y(), leftPos.x()) <
               std::make_tuple(rightPos.y(), rightPos.x());
    });
    if (pills.size() != items.size())
    {
        return false;
    }

    for (int index = 0; index < items.size(); ++index)
    {
        const auto& item = items.at(index);
        QFrame *pill = pills.at(index);
        auto *nameLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryNameLabel"));
        auto *valueLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryValueLabel"));
        if (!nameLabel || !valueLabel || nameLabel->text() != item.label)
        {
            return false;
        }

        const QString valueText = summaryItemValueText(item, compactAvailabilityValues, english);
        const QString widthText = summaryItemWidthText(item, compactAvailabilityValues, english);
        const int requiredValueWidth = std::max(valueLabel->fontMetrics().horizontalAdvance(valueText),
                                               valueLabel->fontMetrics().horizontalAdvance(widthText));
        const int availableValueWidth = std::max(
            valueLabel->width(),
            std::max(valueLabel->minimumWidth(), valueLabel->sizeHint().width()));
        if (requiredValueWidth > availableValueWidth + 1)
        {
            return false;
        }

        if (valueLabel->text() != valueText)
        {
            valueLabel->setText(valueText);
        }
        setTelemetryAvailability(nameLabel, item.hasData);
        setTelemetryAvailability(valueLabel, item.hasData);
    }
    return true;
}

QString rd105OutcomeName(VaporView::Ground::Devices::Rd105OperationOutcome outcome)
{
    using Outcome = VaporView::Ground::Devices::Rd105OperationOutcome;
    switch (outcome)
    {
    case Outcome::Success: return QStringLiteral("success");
    case Outcome::Failed: return QStringLiteral("failed");
    case Outcome::Timeout: return QStringLiteral("timeout");
    case Outcome::Disconnected: return QStringLiteral("disconnected");
    case Outcome::Unsupported: return QStringLiteral("unsupported");
    }
    return QStringLiteral("unknown");
}

QString rd105ExecutionPath(VaporView::Ground::Devices::Rd105Backend backend)
{
    return backend == VaporView::Ground::Devices::Rd105Backend::Remote
        ? QStringLiteral("remote_sky")
        : QStringLiteral("local");
}

} // namespace

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
    // Switching targets updates the shared serial card in several stages (local
    // values, remote values, row visibility, and enabled states). Keep the page
    // from painting those intermediate layouts; otherwise the card briefly
    // disappears while the sky-link row is being inserted below the device rows.
    QWidget *deviceConfigPage = state_->device_config_.page;
    QScrollArea *deviceConfigScrollArea = deviceConfigPage
        ? deviceConfigPage->findChild<QScrollArea *>(QStringLiteral("mainCardsScrollArea"))
        : nullptr;
    const bool pageUpdatesEnabled = deviceConfigPage && deviceConfigPage->updatesEnabled();
    const bool scrollUpdatesEnabled = deviceConfigScrollArea && deviceConfigScrollArea->updatesEnabled();
    const bool viewportUpdatesEnabled = deviceConfigScrollArea && deviceConfigScrollArea->viewport()->updatesEnabled();
    if (pageUpdatesEnabled)
    {
        deviceConfigPage->setUpdatesEnabled(false);
    }
    if (scrollUpdatesEnabled)
    {
        deviceConfigScrollArea->setUpdatesEnabled(false);
    }
    if (viewportUpdatesEnabled)
    {
        deviceConfigScrollArea->viewport()->setUpdatesEnabled(false);
    }
    QWidget *serialCard = state_->device_config_.sky_telemetry_row_widget
        ? state_->device_config_.sky_telemetry_row_widget->parentWidget()
        : nullptr;
    QWidget *deviceConfigContent = deviceConfigScrollArea
        ? deviceConfigScrollArea->widget()
        : nullptr;
    const bool serialCardHeightLocked = serialCard && serialCard->isVisible();
    const bool serialCardUpdatesEnabled = serialCard && serialCard->updatesEnabled();
    const int serialCardCurrentHeight = serialCardHeightLocked ? serialCard->height() : 0;
    const int serialCardMinimumHeight = serialCardHeightLocked
        ? serialCard->minimumHeight()
        : 0;
    const int serialCardMaximumHeight = serialCardHeightLocked
        ? serialCard->maximumHeight()
        : QWIDGETSIZE_MAX;
    if (serialCardUpdatesEnabled && serialCard)
    {
        serialCard->setUpdatesEnabled(false);
    }
    if (serialCardHeightLocked)
    {
        // The parent content layout can otherwise overwrite the card geometry
        // with its pre-switch size before the newly visible row is measured.
        serialCard->setFixedHeight(serialCardCurrentHeight);
        if (index == 1 && !state_->remote_sky_mode_ && state_->device_config_.sky_telemetry_row_widget)
        {
            const QWidget *skyTelemetryRow = state_->device_config_.sky_telemetry_row_widget;
            const int skyRowHeight = std::max(skyTelemetryRow->sizeHint().height(),
                                              skyTelemetryRow->minimumSizeHint().height());
            serialCard->setFixedHeight(serialCardCurrentHeight + skyRowHeight);
        }
    }
    QList<QLayout *> deviceConfigLayouts = {
        serialCard ? serialCard->layout() : nullptr,
        deviceConfigContent ? deviceConfigContent->layout() : nullptr,
        deviceConfigPage ? deviceConfigPage->layout() : nullptr};
    for (QWidget *root : {serialCard})
    {
        if (!root)
        {
            continue;
        }
        for (QLayout *layout : root->findChildren<QLayout *>())
        {
            if (!deviceConfigLayouts.contains(layout))
            {
                deviceConfigLayouts.append(layout);
            }
        }
    }
    QList<QLayout *> disabledDeviceConfigLayouts;
    for (QLayout *layout : deviceConfigLayouts)
    {
        if (layout && layout->isEnabled())
        {
            layout->setEnabled(false);
            disabledDeviceConfigLayouts.append(layout);
        }
    }
    const auto restoreDeviceConfigUpdates = qScopeGuard(
        [deviceConfigPage, deviceConfigScrollArea, pageUpdatesEnabled, scrollUpdatesEnabled,
         viewportUpdatesEnabled, disabledDeviceConfigLayouts, serialCard,
         serialCardHeightLocked, serialCardMinimumHeight, serialCardMaximumHeight,
         serialCardUpdatesEnabled, deviceConfigContent]() {
            for (QLayout *layout : disabledDeviceConfigLayouts)
            {
                layout->setEnabled(true);
                layout->invalidate();
                layout->activate();
            }
            if (serialCardHeightLocked && serialCard)
            {
                serialCard->setMinimumHeight(serialCardMinimumHeight);
                serialCard->setMaximumHeight(serialCardMaximumHeight);
                if (QLayout *layout = serialCard->layout())
                {
                    layout->invalidate();
                    layout->activate();
                }
            }
            // Flush the layout requests generated by the visibility changes while
            // painting is still blocked. Otherwise Qt can apply an intermediate
            // compact card geometry on the first event-loop turn after the switch.
            if (serialCard)
            {
                QCoreApplication::sendPostedEvents(serialCard, QEvent::LayoutRequest);
            }
            if (deviceConfigContent)
            {
                QCoreApplication::sendPostedEvents(deviceConfigContent, QEvent::LayoutRequest);
            }
            if (deviceConfigPage)
            {
                QCoreApplication::sendPostedEvents(deviceConfigPage, QEvent::LayoutRequest);
            }
            if (deviceConfigScrollArea)
            {
                QCoreApplication::sendPostedEvents(deviceConfigScrollArea, QEvent::LayoutRequest);
            }
            if (serialCardHeightLocked && serialCard)
            {
                const int targetHeight = std::max(serialCard->sizeHint().height(),
                                                  serialCard->minimumSizeHint().height());
                serialCard->setFixedHeight(targetHeight);
            }
            if (serialCardUpdatesEnabled && serialCard)
            {
                serialCard->setUpdatesEnabled(true);
                serialCard->update();
            }
            if (viewportUpdatesEnabled && deviceConfigScrollArea)
            {
                deviceConfigScrollArea->viewport()->setUpdatesEnabled(true);
                deviceConfigScrollArea->viewport()->update();
            }
            if (scrollUpdatesEnabled && deviceConfigScrollArea)
            {
                deviceConfigScrollArea->setUpdatesEnabled(true);
                deviceConfigScrollArea->update();
            }
            if (pageUpdatesEnabled && deviceConfigPage)
            {
                deviceConfigPage->setUpdatesEnabled(true);
                deviceConfigPage->update();
            }
        });

    state_->remote_sky_mode_ = index == 1;
    if (state_->ai8_device_session_)
    {
        state_->ai8_device_session_->setBackend(
            state_->remote_sky_mode_
                ? VaporView::Ground::Devices::Ai8Backend::Remote
                : VaporView::Ground::Devices::Ai8Backend::Local);
        if (state_->ai8_temperature_controller_panel_)
        {
            const auto page = state_->ai8_temperature_controller_panel_->currentPageData();
            state_->ai8_device_session_->activatePage(page.page, page.selection);
        }
    }
    if (state_->epsilon_device_session_)
    {
        state_->epsilon_device_session_->setBackend(
            state_->remote_sky_mode_
                ? VaporView::Ground::Devices::EpsilonBackend::Remote
                : VaporView::Ground::Devices::EpsilonBackend::Local);
    }
    if (state_->rd105_device_session_)
    {
        state_->rd105_device_session_->setBackend(
            state_->remote_sky_mode_
                ? VaporView::Ground::Devices::Rd105Backend::Remote
                : VaporView::Ground::Devices::Rd105Backend::Local);
    }
    saveRememberedInputState();
    clearRemoteSkyDataUi();
    syncDeviceConfigEpsilonPanelFromSettings();
    if (state_->tcp_wave_panel_)
    {
        state_->tcp_wave_panel_->setRemoteSkyMode(state_->remote_sky_mode_);
    }
    if (state_->remote_sky_mode_)
    {
        syncDeviceConfigPageForCurrentTarget();
    }
    else
    {
        syncDeviceConfigPageFromHome();
    }
    updateDeviceConfigTexts();
    updateSourceModeUi();
    requestRemoteSkyConfigIfAvailable(false);
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
        const bool remoteDetectionAvailable = remote && (isUiTestMode() ||
            (state_->remote_sky_controller_ && state_->remote_sky_controller_->isOpen()));
        state_->auto_detect_ports_btn_->setEnabled(remote
            ? remoteDetectionAvailable
            : (isUiTestMode() || !state_->is_connected_) && !state_->connection_attempt_in_progress_);
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
    int telemetrySummaryWidth = 0;
    if (state_->data_telemetry_summary_card_)
    {
        telemetrySummaryWidth = state_->data_telemetry_summary_card_->minimumWidth();
        if (QLayout *summaryLayout = state_->data_telemetry_summary_card_->layout())
        {
            summaryLayout->invalidate();
            telemetrySummaryWidth = std::max(telemetrySummaryWidth,
                                             summaryLayout->minimumSize().width());
        }
    }
    const int contentWidth = std::max(deviceControlsWidth, telemetrySummaryWidth);
    return contentWidth +
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
                columnLayout->invalidate();
                columnLayout->activate();
            }
        }
        if (QLayout *deviceLayout = homeDevices->layout())
        {
            deviceLayout->setSizeConstraint(QLayout::SetFixedSize);
            deviceLayout->invalidate();
            deviceLayout->activate();
        }
    }

    const int contentMinimumWidth = homeDeviceOverviewContentMinimumWidth();
    state_->config_group_->setMinimumWidth(contentMinimumWidth);

    if (!state_->home_overview_splitter_ || !state_->temperature_overview_group_)
    {
        return;
    }

    auto rememberAutoMinimumWidth = [this, contentMinimumWidth]() {
        state_->home_overview_splitter_->setProperty(
            kHomeOverviewDeviceAutoMinimumWidthProperty,
            contentMinimumWidth);
    };
    auto setAutoManagedSizes = [this](int leftWidth, int rightWidth) {
        state_->home_overview_splitter_->setProperty(
            kHomeOverviewDeviceProgrammaticResizeProperty,
            true);
        state_->home_overview_splitter_->setSizes({leftWidth, rightWidth});
        state_->home_overview_splitter_->setProperty(
            kHomeOverviewDeviceProgrammaticResizeProperty,
            false);
        state_->home_overview_splitter_->setProperty(
            kHomeOverviewDeviceAutoManagedWidthProperty,
            true);
    };

    const QList<int> sizes = state_->home_overview_splitter_->sizes();
    if (sizes.size() < 2)
    {
        rememberAutoMinimumWidth();
        return;
    }

    const int availableWidth = std::max(0,
                                        std::max(state_->home_overview_splitter_->width(),
                                                 sizes.at(0) + sizes.at(1) + state_->home_overview_splitter_->handleWidth()) -
                                            state_->home_overview_splitter_->handleWidth());
    const int rightMinimumWidth = state_->temperature_overview_group_->minimumWidth();
    if (availableWidth < contentMinimumWidth + rightMinimumWidth)
    {
        rememberAutoMinimumWidth();
        return;
    }

    bool hadPreviousAutoMinimum = false;
    const int previousAutoMinimumWidth = state_->home_overview_splitter_
                                             ->property(kHomeOverviewDeviceAutoMinimumWidthProperty)
                                             .toInt(&hadPreviousAutoMinimum);
    const bool autoManagedWidth = state_->home_overview_splitter_
                                      ->property(kHomeOverviewDeviceAutoManagedWidthProperty)
                                      .toBool();
    const bool belowCurrentMinimum = sizes.at(0) < contentMinimumWidth;
    const bool autoMinimumShrank =
        autoManagedWidth &&
        hadPreviousAutoMinimum &&
        previousAutoMinimumWidth > contentMinimumWidth;
    const bool followsPreviousAutoMinimum =
        autoMinimumShrank &&
        previousAutoMinimumWidth > contentMinimumWidth &&
        std::abs(sizes.at(0) - previousAutoMinimumWidth) <= 1;
    const bool shouldFollowAutoMinimum =
        autoMinimumShrank &&
        sizes.at(0) >= contentMinimumWidth;
    if (!belowCurrentMinimum && !followsPreviousAutoMinimum && !shouldFollowAutoMinimum)
    {
        rememberAutoMinimumWidth();
        return;
    }

    setAutoManagedSizes(contentMinimumWidth,
                        std::max(rightMinimumWidth, availableWidth - contentMinimumWidth));
    rememberAutoMinimumWidth();
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
        updateHomeDeviceOverviewMinimumWidth();
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
    if (state_->ai8_device_session_)
    {
        state_->ai8_device_session_->setRemoteAvailable(false);
    }
    if (state_->epsilon_device_session_)
    {
        state_->epsilon_device_session_->setRemoteAvailable(false);
    }
    if (state_->rd105_device_session_)
    {
        state_->rd105_device_session_->setRemoteAvailable(false);
    }
    state_->remote_sky_online_ = false;
    state_->remote_wave_stream_requested_ = false;
    state_->remote_wave_stream_enable_pending_ = false;
    state_->remote_wave_stream_auto_start_ = true;
    clearPendingRemoteWaveTcpConnection();
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
    if (state_->ai8_temperature_controller_panel_)
    {
        state_->ai8_temperature_controller_panel_->setBackendConnected(false);
        state_->ai8_temperature_controller_panel_->applyLiveData({});
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
    state_->remote_serial_detection_pending_ = false;
    state_->port_detection_in_progress_ = false;
    state_->remote_serial_detection_seq_ = 0;
    state_->remote_serial_detection_cancel_seq_ = 0;
    state_->remote_sky_online_ = false;
    state_->remote_wave_stream_requested_ = false;
    state_->remote_wave_stream_enable_pending_ = false;
    clearPendingRemoteWaveTcpConnection();
    state_->remote_sky_config_loading_ = false;
    state_->remote_sky_config_applying_ = false;
    state_->remote_sky_config_saving_ = false;
    state_->remote_sky_config_read_generation_ = 0;
    state_->remote_sky_config_apply_generation_ = 0;
    if (state_->remote_sky_config_loaded_)
    {
        setRemoteSkyConfigStatus(state_->is_english_
            ? QStringLiteral("Telemetry link closed. Showing last loaded Remote Sky config.")
            : QStringLiteral("天地数传已断开，当前显示上次读取到的天空端配置。"));
    }
    else
    {
        setRemoteSkyConfigStatus(state_->is_english_
            ? QStringLiteral("Telemetry link closed. Remote Sky config is not loaded.")
            : QStringLiteral("天地数传已断开，尚未读取天空端配置。"));
    }
    if (state_->ai8_device_session_)
    {
        state_->ai8_device_session_->setRemoteAvailable(false);
    }
    if (state_->epsilon_device_session_)
    {
        state_->epsilon_device_session_->setRemoteAvailable(false);
    }
    if (state_->rd105_device_session_)
    {
        state_->rd105_device_session_->setRemoteAvailable(false);
    }
    state_->remote_recording_state_ = 0;
    state_->remote_status_.recording_state = 0;
    if (state_->tcp_wave_panel_)
    {
        state_->tcp_wave_panel_->setRemoteWaveTcpState(VaporView::DeviceState::Disconnected);
    }
    if (state_->ai8_temperature_controller_panel_)
    {
        state_->ai8_temperature_controller_panel_->setBackendConnected(false);
        state_->ai8_temperature_controller_panel_->applyLiveData({});
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
    const bool uiTestMode = isUiTestMode() && state_->ui_test_model_;
    VaporView::Ground::Devices::UiTestSnapshot uiTestSnapshot;
    if (uiTestMode)
    {
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - state_->ui_test_started_ms_;
        uiTestSnapshot = state_->ui_test_model_->snapshot(elapsed);
    }
    const bool connected = uiTestMode
        ? !uiTestSnapshot.dataStalled
        : (state_->remote_sky_controller_ && state_->remote_sky_controller_->isOpen());

    auto hasDeviceData = [this, connected](VaporView::SkyDeviceId device, qint64 timeoutMs) {
        return connected && remoteDeviceDataValid(device, timeoutMs);
    };
    auto hasUiTestDeviceData = [this, &uiTestSnapshot](VaporView::SkyDeviceId device) {
        switch (device)
        {
        case VaporView::SkyDeviceId::Epsilon:
            return uiTestSnapshot.epsilon.valid;
        case VaporView::SkyDeviceId::Ptb:
            return uiTestSnapshot.ptb.valid;
        case VaporView::SkyDeviceId::Hmp:
            return uiTestSnapshot.hmp.valid;
        case VaporView::SkyDeviceId::Lidar:
            return uiTestSnapshot.lidar.valid;
        case VaporView::SkyDeviceId::TemperatureController:
            return uiTestSnapshot.temperature.valid;
        case VaporView::SkyDeviceId::Ai8TemperatureController:
            return uiTestSnapshot.ai8Temperature.valid;
        case VaporView::SkyDeviceId::WaveTcp:
            return state_->ui_test_model_->deviceState(VaporView::SkyDeviceId::WaveTcp) ==
                       VaporView::DeviceState::Connected &&
                   !uiTestSnapshot.rawWaveform.isEmpty();
        case VaporView::SkyDeviceId::All:
            return false;
        }
        return false;
    };

    auto hasText = [this](bool hasData) {
        return hasData
            ? (state_->is_english_ ? QStringLiteral("data") : QStringLiteral("有数据"))
            : (state_->is_english_ ? QStringLiteral("none") : QStringLiteral("无数据"));
    };

    auto packetRate = [this, uiTestMode, &uiTestSnapshot](VaporView::MsgType type) {
        if (!uiTestMode)
        {
            return remotePacketRate(type);
        }
        switch (type)
        {
        case VaporView::MsgType::TelemetryBasic:
            return uiTestSnapshot.epsilonRateHz;
        case VaporView::MsgType::WaveformFeature:
            return uiTestSnapshot.waveformFeatureRateHz;
        case VaporView::MsgType::TelemetryStatus:
            return uiTestSnapshot.telemetryStatusRateHz;
        default:
            return 0.0;
        }
    };
    auto waveformRate = [this, uiTestMode, &uiTestSnapshot](quint16 channelId) {
        if (!uiTestMode)
        {
            return remoteWaveformPacketRate(channelId);
        }
        return channelId == 1
            ? uiTestSnapshot.rawWaveformRateHz
            : uiTestSnapshot.harmonicWaveformRateHz;
    };

    const double waveCaptureRateHz = uiTestMode
        ? uiTestSnapshot.waveCaptureRateHz
        : static_cast<double>(state_->remote_status_.wave_tcp_actual_rate_hz);
    const QString actualWaveRate = formatFrequencyText(
        connected ? waveCaptureRateHz : 0.0);
    const double rxBps = connected
        ? (uiTestMode ? uiTestSnapshot.receiveBitsPerSecond : state_->remote_sky_controller_->receiveBitsPerSecond())
        : 0.0;
    const double txBps = connected
        ? (uiTestMode ? uiTestSnapshot.transmitBitsPerSecond : state_->remote_sky_controller_->transmitBitsPerSecond())
        : 0.0;
    const bool linkRateAvailable = connected;
    const bool statusFresh = connected && (uiTestMode ||
        (state_->remote_sky_controller_ &&
         state_->remote_sky_controller_->statusFresh(QDateTime::currentMSecsSinceEpoch())));
    const quint8 recordingState = uiTestMode ? 0 : state_->remote_status_.recording_state;
    const quint64 diskFreeBytes = uiTestMode
        ? static_cast<quint64>(128) * 1024ULL * 1024ULL * 1024ULL
        : state_->remote_status_.disk_free_bytes;
    const quint32 crcErrorCount = uiTestMode ? 0U : state_->remote_status_.crc_error_count;
    const QString unavailableText = QStringLiteral("--");
    const QString targetText = [this]() {
        if (!isRemoteSkyMode())
        {
            return state_->is_english_
                ? QStringLiteral("Local host")
                : QStringLiteral("本机");
        }
        if (isRemoteSkyTcpMode())
        {
            const QString host = state_->sky_telemetry_tcp_host_edit_
                ? state_->sky_telemetry_tcp_host_edit_->text().trimmed()
                : QString();
            const int port = state_->sky_telemetry_tcp_port_spin_
                ? state_->sky_telemetry_tcp_port_spin_->value()
                : 0;
            return QStringLiteral("TCP %1:%2")
                .arg(host.isEmpty() ? QStringLiteral("--") : host)
                .arg(port > 0 ? QString::number(port) : QStringLiteral("--"));
        }
        const QString serialPort = localSerialPortComboValue(state_->sky_telemetry_port_combo_);
        const QString baud = state_->sky_telemetry_baud_combo_
            ? state_->sky_telemetry_baud_combo_->currentText().trimmed()
            : QString();
        return QStringLiteral("Serial %1 %2")
            .arg(serialPort.isEmpty() ? QStringLiteral("--") : serialPort,
                 baud.isEmpty() ? QStringLiteral("--") : baud);
    }();

    auto makeItem = [](const QString& label,
                       const QString& value,
                       bool hasData,
                       const QString& valueWidthText = QString(),
                       bool compactAvailabilityValue = false) {
        RemoteTelemetrySummarySections::Item item;
        item.label = label;
        item.value = value;
        item.valueWidthText = valueWidthText;
        item.hasData = hasData;
        item.compactAvailabilityValue = compactAvailabilityValue;
        return item;
    };

    QList<RemoteTelemetrySummarySections::Item> rateRows;
    QList<RemoteTelemetrySummarySections::Item> linkRows;
    QList<RemoteTelemetrySummarySections::Item> linkStatusRows;
    QList<RemoteTelemetrySummarySections::Item> deviceRows;
    const QString frequencyWidthText = QStringLiteral("999.9 Hz");
    const QString bitRateWidthText = uiTestMode
        ? QStringLiteral("999.9 Mbps")
        : QStringLiteral("999.9 kbps");
    auto appendPacketRate = [&](VaporView::MsgType type, const QString& label) {
        const double rate = packetRate(type);
        rateRows << makeItem(label, formatFrequencyText(rate), connected && rate > 0.0, frequencyWidthText);
    };
    auto appendWaveformRate = [&](quint16 channelId, const QString& label) {
        const double rate = waveformRate(channelId);
        rateRows << makeItem(label, formatFrequencyText(rate), connected && rate > 0.0, frequencyWidthText);
    };
    auto appendDevice = [&](VaporView::SkyDeviceId device, qint64 timeoutMs, const QString& label) {
        const bool hasData = uiTestMode ? hasUiTestDeviceData(device) : hasDeviceData(device, timeoutMs);
        deviceRows << makeItem(label, hasText(hasData), hasData, QString(), true);
    };
    if (state_->is_english_)
    {
        appendPacketRate(VaporView::MsgType::TelemetryBasic, QStringLiteral("Basic"));
        appendPacketRate(VaporView::MsgType::WaveformFeature, QStringLiteral("Feature"));
        appendPacketRate(VaporView::MsgType::TelemetryStatus, QStringLiteral("Status"));
        appendWaveformRate(1, QStringLiteral("Wave raw"));
        appendWaveformRate(4, QStringLiteral("Wave harm."));
        rateRows << makeItem(QStringLiteral("Wave capture"), actualWaveRate, connected && waveCaptureRateHz > 0.0, frequencyWidthText);
        linkRows << makeItem(QStringLiteral("Target"), targetText, true);
        linkRows << makeItem(QStringLiteral("Sky->Ground"), formatBitRate(rxBps), linkRateAvailable, bitRateWidthText);
        linkRows << makeItem(QStringLiteral("Ground->Sky"), formatBitRate(txBps), linkRateAvailable, bitRateWidthText);
        linkRows << makeItem(QStringLiteral("Total"), formatBitRate(rxBps + txBps), linkRateAvailable, bitRateWidthText);
        linkStatusRows << makeItem(QStringLiteral("Disk"),
                                   statusFresh && diskFreeBytes > 0 ? remoteSummaryBytesText(diskFreeBytes) : unavailableText,
                                   statusFresh && diskFreeBytes > 0,
                                   QStringLiteral("999.9 GB"));
        linkStatusRows << makeItem(QStringLiteral("Record"),
                                   statusFresh ? remoteSummaryRecordingStateText(recordingState, true) : unavailableText,
                                   statusFresh,
                                   QStringLiteral("Recording"));
        linkStatusRows << makeItem(QStringLiteral("CRC"),
                                   statusFresh ? QString::number(crcErrorCount) : unavailableText,
                                   statusFresh && crcErrorCount == 0,
                                   QStringLiteral("999999"));
        appendDevice(VaporView::SkyDeviceId::Epsilon, 2000, QStringLiteral("EPSILON"));
        appendDevice(VaporView::SkyDeviceId::Ptb, 3000, QStringLiteral("PTB"));
        appendDevice(VaporView::SkyDeviceId::Hmp, 3000, QStringLiteral("HMP"));
        appendDevice(VaporView::SkyDeviceId::Lidar, 2000, QStringLiteral("Lidar"));
        appendDevice(VaporView::SkyDeviceId::WaveTcp, 3000, QStringLiteral("Wave"));
    }
    else
    {
        appendPacketRate(VaporView::MsgType::TelemetryBasic, QStringLiteral("基础"));
        appendPacketRate(VaporView::MsgType::WaveformFeature, QStringLiteral("特征值"));
        appendPacketRate(VaporView::MsgType::TelemetryStatus, QStringLiteral("状态"));
        appendWaveformRate(1, QStringLiteral("原始波形"));
        appendWaveformRate(4, QStringLiteral("谐波波形"));
        rateRows << makeItem(QStringLiteral("波形采集"), actualWaveRate, connected && waveCaptureRateHz > 0.0, frequencyWidthText);
        linkRows << makeItem(QStringLiteral("目标"), targetText, true);
        linkRows << makeItem(QStringLiteral("天→地"), formatBitRate(rxBps), linkRateAvailable, bitRateWidthText);
        linkRows << makeItem(QStringLiteral("地→天"), formatBitRate(txBps), linkRateAvailable, bitRateWidthText);
        linkRows << makeItem(QStringLiteral("合"), formatBitRate(rxBps + txBps), linkRateAvailable, bitRateWidthText);
        linkStatusRows << makeItem(QStringLiteral("磁盘"),
                                   statusFresh && diskFreeBytes > 0 ? remoteSummaryBytesText(diskFreeBytes) : unavailableText,
                                   statusFresh && diskFreeBytes > 0,
                                   QStringLiteral("999.9 GB"));
        linkStatusRows << makeItem(QStringLiteral("记录"),
                                   statusFresh ? remoteSummaryRecordingStateText(recordingState, false) : unavailableText,
                                   statusFresh,
                                   QStringLiteral("记录中"));
        linkStatusRows << makeItem(QStringLiteral("CRC"),
                                   statusFresh ? QString::number(crcErrorCount) : unavailableText,
                                   statusFresh && crcErrorCount == 0,
                                   QStringLiteral("999999"));
        appendDevice(VaporView::SkyDeviceId::Epsilon, 2000, QStringLiteral("EPSILON"));
        appendDevice(VaporView::SkyDeviceId::Ptb, 3000, QStringLiteral("PTB"));
        appendDevice(VaporView::SkyDeviceId::Hmp, 3000, QStringLiteral("HMP"));
        appendDevice(VaporView::SkyDeviceId::Lidar, 2000, QStringLiteral("Lidar"));
        appendDevice(VaporView::SkyDeviceId::WaveTcp, 3000, QStringLiteral("波形"));
    }

    RemoteTelemetrySummarySections sections;
    sections.rateItems = rateRows;
    sections.linkItems = linkRows;
    sections.linkStatusItems = linkStatusRows;
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
    QStringList summaryStructureTokens{
        QString::number(state_->font_scale_percent_),
        state_->is_english_ ? QStringLiteral("en") : QStringLiteral("zh"),
        qApp && !qApp->styleSheet().isEmpty() ? QStringLiteral("styled") : QStringLiteral("unstyled")};
    const auto appendSummaryStructureTokens = [&summaryStructureTokens](
                                                  const QList<RemoteTelemetrySummarySections::Item>& items) {
        summaryStructureTokens << QString::number(items.size());
        for (const RemoteTelemetrySummarySections::Item& item : items)
        {
            summaryStructureTokens << item.label
                                   << item.valueWidthText
                                   << (item.compactAvailabilityValue ? QStringLiteral("compact") : QStringLiteral("value"));
        }
    };
    appendSummaryStructureTokens(sections.rateItems);
    appendSummaryStructureTokens(sections.linkItems);
    appendSummaryStructureTokens(sections.linkStatusItems);
    appendSummaryStructureTokens(sections.deviceItems);
    const QString summaryStructureKey = summaryStructureTokens.join(QChar(0x1f));
    constexpr auto kSummaryStructureKeyProperty = "vaporViewTelemetrySummaryStructureKey";

    if (state_->data_telemetry_summary_card_)
    {
        state_->data_telemetry_summary_card_->setVisible(true);
    }
    if (state_->device_config_.data_telemetry_summary_card)
    {
        state_->device_config_.data_telemetry_summary_card->setVisible(true);
    }
    QList<RemoteTelemetrySummarySections::Item> deviceConfigDataItems = sections.linkStatusItems;
    for (const RemoteTelemetrySummarySections::Item& item : sections.deviceItems)
    {
        deviceConfigDataItems << item;
    }

    bool homeSummaryNeedsRender = false;
    if (state_->data_telemetry_summary_card_)
    {
        homeSummaryNeedsRender = !(
            updateSummarySectionValues(state_->data_telemetry_summary_layout_,
                                       sections.rateItems,
                                       false,
                                       state_->is_english_) &&
            updateSummarySectionValues(state_->data_telemetry_link_summary_layout_,
                                       sections.linkItems,
                                       false,
                                       state_->is_english_) &&
            updateSummarySectionValues(state_->data_telemetry_device_summary_layout_,
                                       sections.deviceItems,
                                       true,
                                       state_->is_english_));
        if (!homeSummaryNeedsRender)
        {
            state_->data_telemetry_summary_card_->setProperty(kSummaryStructureKeyProperty, summaryStructureKey);
            state_->data_telemetry_summary_card_->update();
        }
    }

    bool deviceConfigSummaryNeedsRender = false;
    if (state_->device_config_.data_telemetry_summary_card)
    {
        deviceConfigSummaryNeedsRender = !(
            updateSummarySectionValues(state_->device_config_.data_telemetry_rate_summary_layout,
                                       sections.rateItems,
                                       false,
                                       state_->is_english_) &&
            updateSummarySectionValues(state_->device_config_.data_telemetry_link_summary_layout,
                                       sections.linkItems,
                                       false,
                                       state_->is_english_) &&
            updateSummarySectionValues(state_->device_config_.data_telemetry_device_summary_layout,
                                       deviceConfigDataItems,
                                       true,
                                       state_->is_english_));
        if (!deviceConfigSummaryNeedsRender)
        {
            state_->device_config_.data_telemetry_summary_card->setProperty(kSummaryStructureKeyProperty, summaryStructureKey);
            state_->device_config_.data_telemetry_summary_card->update();
        }
    }

    if (!homeSummaryNeedsRender && !deviceConfigSummaryNeedsRender)
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
        if (QWidget *section = qobject_cast<QWidget *>(sectionLayout->parent()))
        {
            section->setMinimumWidth(0);
        }

        auto addItemLabel = [this, useSideTitle, compactAvailabilityValues](QHBoxLayout *lineLayout,
                                                                            QWidget *lineWidget,
                                                                            const RemoteTelemetrySummarySections::Item& item) {
            auto *pill = new QFrame(lineWidget);
            pill->setObjectName(QStringLiteral("homeTelemetrySummaryPill"));
            pill->setProperty("deviceConfigLink", useSideTitle);
            pill->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            pill->setMinimumHeight(scalePixels(useSideTitle ? 26 : 28));
            auto *pillLayout = new QHBoxLayout(pill);
            const int horizontalPadding = scalePixels(useSideTitle ? 2 : 5);
            pillLayout->setContentsMargins(horizontalPadding, scalePixels(1), horizontalPadding, scalePixels(1));
            pillLayout->setSpacing(scalePixels(useSideTitle ? 1 : 4));

            auto *nameLabel = new QLabel(item.label, pill);
            nameLabel->setObjectName(QStringLiteral("homeTelemetrySummaryNameLabel"));
            nameLabel->setProperty("deviceConfigLink", useSideTitle);
            nameLabel->setProperty("telemetryAvailable", item.hasData);
            nameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            nameLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            nameLabel->setTextFormat(Qt::PlainText);
            nameLabel->ensurePolished();
            const int nameWidth =
                std::max(nameLabel->sizeHint().width(),
                         nameLabel->fontMetrics().horizontalAdvance(item.label)) +
                scalePixels(useSideTitle ? 2 : 2);
            nameLabel->setFixedWidth(nameWidth);
            nameLabel->setMinimumHeight(nameLabel->fontMetrics().height() + scalePixels(2));
            pillLayout->addWidget(nameLabel, 0, Qt::AlignVCenter);

            const QString valueText = summaryItemValueText(item, compactAvailabilityValues, state_->is_english_);
            auto *valueLabel = new QLabel(valueText, pill);
            valueLabel->setObjectName(QStringLiteral("homeTelemetrySummaryValueLabel"));
            valueLabel->setProperty("deviceConfigLink", useSideTitle);
            valueLabel->setProperty("telemetryAvailable", item.hasData);
            valueLabel->setFont(numericFontFrom(valueLabel->font()));
            valueLabel->setAlignment((useSideTitle ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
            valueLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            valueLabel->setTextFormat(Qt::PlainText);
            valueLabel->ensurePolished();
            valueLabel->setMinimumHeight(valueLabel->fontMetrics().height() + scalePixels(2));
            const QString widthValue = summaryItemWidthText(item, compactAvailabilityValues, state_->is_english_);
            const int polishedValueWidth =
                std::max(valueLabel->sizeHint().width(),
                         std::max(valueLabel->fontMetrics().horizontalAdvance(widthValue),
                                  valueLabel->fontMetrics().horizontalAdvance(valueText))) +
                scalePixels(useSideTitle ? 2 : 2);
            const int valueWidth = polishedValueWidth;
            valueLabel->setFixedWidth(valueWidth);
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
            return pill;
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
        int explicitSectionMinimumWidth = 0;
        struct RenderedSummaryLine
        {
            QWidget *line = nullptr;
            QHBoxLayout *layout = nullptr;
            QVector<QFrame *> pills;
        };
        QVector<RenderedSummaryLine> renderedLines;
        auto addLine = [&](int begin, int end, bool includeTitle) {
            ++renderedLineCount;
            auto *line = new QWidget(lineParent);
            line->setFixedHeight(scalePixels(useSideTitle ? 26 : 28));
            auto *lineLayout = new QHBoxLayout(line);
            lineLayout->setContentsMargins(0, 0, 0, 0);
            lineLayout->setSpacing(scalePixels(useSideTitle ? 2 : 2));
            QVector<QFrame *> linePills;

            if (!useSideTitle && includeTitle && !title.isEmpty())
            {
                auto *titleLabel = new QLabel(line);
                titleLabel->setObjectName(QStringLiteral("homeTelemetrySummaryTitleLabel"));
                titleLabel->setProperty("skyTelemetryTitle", true);
                titleLabel->setText(title);
                titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
                titleLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
                titleLabel->ensurePolished();
                titleLabel->setFixedWidth(titleLabel->fontMetrics().horizontalAdvance(titleLabel->text()) + scalePixels(8));
                lineLayout->addWidget(titleLabel, 0, Qt::AlignVCenter);
            }

            for (int i = begin; i < end; ++i)
            {
                linePills << addItemLabel(lineLayout, line, items.at(i));
            }
            lineLayout->addStretch(1);
            renderedLines.push_back({line, lineLayout, linePills});
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

        if (useSideTitle)
        {
            int widestPillWidth = 0;
            for (const RenderedSummaryLine& renderedLine : renderedLines)
            {
                for (QFrame *pill : renderedLine.pills)
                {
                    widestPillWidth = std::max(
                        widestPillWidth,
                        std::max(pill->minimumWidth(),
                                 std::max(pill->minimumSizeHint().width(), pill->sizeHint().width())));
                }
            }
            for (const RenderedSummaryLine& renderedLine : renderedLines)
            {
                for (QFrame *pill : renderedLine.pills)
                {
                    pill->setFixedWidth(widestPillWidth);
                }
            }
        }

        for (RenderedSummaryLine& renderedLine : renderedLines)
        {
            renderedLine.layout->invalidate();
            renderedLine.layout->activate();
            const QMargins lineMargins = renderedLine.layout->contentsMargins();
            int lineMinimumWidth = lineMargins.left() + lineMargins.right();
            int lineWidgetCount = 0;
            for (int i = 0; i < renderedLine.layout->count(); ++i)
            {
                QWidget *widget = renderedLine.layout->itemAt(i)
                    ? renderedLine.layout->itemAt(i)->widget()
                    : nullptr;
                if (!widget)
                {
                    continue;
                }
                if (lineWidgetCount > 0)
                {
                    lineMinimumWidth += renderedLine.layout->spacing();
                }
                lineMinimumWidth += std::max(
                    widget->minimumWidth(),
                    std::max(widget->minimumSizeHint().width(), widget->sizeHint().width()));
                ++lineWidgetCount;
            }
            renderedLine.line->setMinimumWidth(lineMinimumWidth);
            renderedLine.line->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
            if (!useSideTitle)
            {
                const QMargins sectionMargins = sectionLayout->contentsMargins();
                explicitSectionMinimumWidth = std::max(
                    explicitSectionMinimumWidth,
                    lineMinimumWidth + sectionMargins.left() + sectionMargins.right());
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
            const int sectionMinimumWidth = std::max(sectionLayout->minimumSize().width(), explicitSectionMinimumWidth);
            section->setMinimumWidth(sectionMinimumWidth);
            section->setFixedHeight(sectionHeight);
            section->adjustSize();
            section->updateGeometry();
        }
    };
    auto updateSummaryMinimumWidth = [](QWidget *summaryWidget) {
        if (!summaryWidget)
        {
            return;
        }
        int sectionMinimumWidth = 0;
        const QList<QFrame*> sections =
            summaryWidget->findChildren<QFrame *>(QStringLiteral("homeTelemetrySectionCard"));
        for (const QFrame *section : sections)
        {
            sectionMinimumWidth = std::max(sectionMinimumWidth, section->minimumWidth());
        }
        const QMargins margins = summaryWidget->layout()
            ? summaryWidget->layout()->contentsMargins()
            : QMargins();
        summaryWidget->setMinimumWidth(sectionMinimumWidth + margins.left() + margins.right());
        summaryWidget->updateGeometry();
    };
    if (homeSummaryNeedsRender && state_->data_telemetry_summary_card_)
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
                             1,
                             3);
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
        updateSummaryMinimumWidth(state_->data_telemetry_summary_card_);
        state_->data_telemetry_summary_card_->updateGeometry();
        updateHomeDeviceOverviewMinimumWidth();
        state_->data_telemetry_summary_card_->setProperty(kSummaryStructureKeyProperty, summaryStructureKey);
    }
    if (deviceConfigSummaryNeedsRender && state_->device_config_.data_telemetry_summary_card)
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
                             deviceConfigDataItems,
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
        state_->device_config_.data_telemetry_summary_card->setProperty(kSummaryStructureKeyProperty, summaryStructureKey);
    }
    if (state_->home_overview_splitter_)
    {
        updateConfigCardHeightForSourceMode();
    }
}

void MainWindow::setDeviceConfigEpsilonPacketRates(const std::map<uint8_t, int>& packetRates)
{
    if (!state_->epsilon_config_panel_)
    {
        return;
    }
    state_->epsilon_config_panel_->setPacketRates(packetRates);
}

std::map<uint8_t, int> MainWindow::deviceConfigEpsilonPacketRates() const
{
    std::map<uint8_t, int> packetRates = defaultEpsilonPacketRates();
    if (!state_->epsilon_config_panel_)
    {
        return packetRates;
    }
    for (const auto& entry : state_->epsilon_config_panel_->packetRates())
    {
        packetRates[entry.first] = entry.second;
    }
    return packetRates;
}

void MainWindow::syncDeviceConfigEpsilonPanelFromSettings()
{
    if (!state_->epsilon_config_panel_)
    {
        return;
    }

    QSettings settings = VaporView::applicationConfigSettings();
    settings.beginGroup(QStringLiteral("MainWindow"));
    if (isRemoteSkyMode())
    {
        settings.beginGroup(QStringLiteral("RemoteEpsilonPacketProfile"));
    }
    setDeviceConfigEpsilonPacketRates(loadCustomEpsilonPacketRates(settings));
    state_->epsilon_config_panel_->setRtcmDevicePortIndex(
        isRemoteSkyMode()
            ? state_->remote_sky_config_.epsilon_rtcm.device_port_index
            : settings.value(QStringLiteral("epsilon_rtcm_device_port_index"), 2).toInt());
}

void MainWindow::saveDeviceConfigEpsilonPacketRates(bool applyAfterSave)
{
    if (!state_->epsilon_config_panel_ ||
        state_->connection_attempt_in_progress_ ||
        state_->port_detection_in_progress_ ||
        state_->epsilon_reconfigure_in_progress_)
    {
        return;
    }

    const std::map<uint8_t, int> defaultRates = defaultEpsilonPacketRates();
    const std::map<uint8_t, int> savedPacketRates = deviceConfigEpsilonPacketRates();
    const QString epsilonBaudText = isRemoteSkyMode()
        ? QString::number(state_->remote_sky_config_.epsilon.baud_rate > 0
              ? state_->remote_sky_config_.epsilon.baud_rate
              : 921600)
        : (state_->epsilon_baud_combo_
              ? state_->epsilon_baud_combo_->currentText().trimmed()
              : QStringLiteral("921600"));
    if (!validateEpsilonPacketBandwidth(savedPacketRates, epsilonBaudText, true))
    {
        return;
    }

    QSettings settings = VaporView::applicationConfigSettings();
    settings.beginGroup(QStringLiteral("MainWindow"));
    if (isRemoteSkyMode())
    {
        settings.beginGroup(QStringLiteral("RemoteEpsilonPacketProfile"));
    }
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        const auto it = savedPacketRates.find(option.packet_id);
        VaporView::setPersistentSetting(settings, epsilonPacketRateSettingsKey(option.packet_id),
                          it != savedPacketRates.end() ? it->second : defaultRates.at(option.packet_id));
    }

    VaporView::removePersistentSetting(settings, QStringLiteral("epsilon_last_config_signature"));
    VaporView::removePersistentSetting(settings, QStringLiteral("epsilon_last_config_apply_version"));
    const QString packetRateSummary = epsilonPacketRatesSummary(savedPacketRates);

    publishGroundLog(VaporView::LogLevel::Info,
                     QStringLiteral("configuration.apply"),
                     QStringLiteral("epsilon_packet_profile_saved"),
                     savedPacketRates == defaultRates
                         ? QStringLiteral("已保存 EPSILON 推荐默认包频率配置。")
                         : QStringLiteral("已保存 EPSILON 包频率配置。"),
                     {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                      {QStringLiteral("packet_rate_profile"), savedPacketRates == defaultRates
                          ? QStringLiteral("recommended_default")
                          : QStringLiteral("custom")},
                      {QStringLiteral("packet_rate_summary"), packetRateSummary},
                      {QStringLiteral("ui_visibility"), QStringLiteral("details")}});

    const QString selectText = state_->is_english_ ? "-- Select --" : "未选择";
    const QString epsilonPort = localSerialPortComboValue(state_->epsilon_port_combo_);
    const bool targetReadyForApply = isRemoteSkyMode() ||
        (!epsilonPort.isEmpty() && epsilonPort != selectText);
    if (applyAfterSave &&
        !state_->recording_service_->isActive() &&
        targetReadyForApply)
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("configuration.apply"),
                         QStringLiteral("epsilon_packet_profile_apply_requested"),
                         QStringLiteral("正在应用刚保存的 EPSILON 包频率配置。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("port"), isRemoteSkyMode() ? state_->remote_sky_config_.epsilon.port : epsilonPort},
                          {QStringLiteral("packet_rate_summary"), packetRateSummary},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
        onReconfigureEpsilonClicked();
    }
    else
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("configuration.apply"),
                         QStringLiteral("epsilon_packet_profile_saved_deferred"),
                         QStringLiteral("EPSILON 包频率配置已保存，将在下次连接或重配时生效。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("packet_rate_summary"), packetRateSummary},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
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

void MainWindow::requestRemoteWaveTcpConnection(bool connectRequested, const QString& host, int port)
{
    const bool endpointProvided = !host.isNull() || port > 0;
    const QString endpointHost = host.trimmed();
    const bool endpointValid = !endpointHost.isEmpty() && port >= 1 && port <= 65535;
    if (!connectRequested)
    {
        clearPendingRemoteWaveTcpConnection();
    }
    else if (endpointProvided && !endpointValid)
    {
        setRemoteSkyConfigStatus(state_->is_english_
            ? QStringLiteral("Enter a TCP wave host and a port from 1 to 65535.")
            : QStringLiteral("请输入 TCP 波形主机和 1 到 65535 之间的端口。"),
            true);
        return;
    }

    if (connectRequested && endpointValid)
    {
        if (!state_->remote_sky_config_loaded_ && !isUiTestMode())
        {
            if (!state_->remote_sky_controller_ || !state_->remote_sky_controller_->isOpen())
            {
                setRemoteSkyConfigStatus(state_->is_english_
                    ? QStringLiteral("Telemetry link is disconnected; TCP wave endpoint was not sent.")
                    : QStringLiteral("天地数传已断开，未发送 TCP 波形地址。"),
                    true);
                return;
            }
            state_->remote_wave_connect_after_config_read_ = true;
            state_->remote_wave_pending_host_ = endpointHost;
            state_->remote_wave_pending_port_ = port;
            requestRemoteSkyConfigIfAvailable(true);
            if (!state_->remote_sky_config_loading_)
            {
                clearPendingRemoteWaveTcpConnection();
                return;
            }
            setRemoteSkyConfigStatus(state_->is_english_
                ? QStringLiteral("Reading Remote Sky config before applying the TCP wave endpoint...")
                : QStringLiteral("正在读取天空端配置，然后应用 TCP 波形地址..."));
            return;
        }

        VaporView::SkyConfig config = state_->remote_sky_config_loaded_
            ? state_->remote_sky_config_
            : VaporView::SkyConfig::defaults();
        const bool endpointChanged =
            config.wave_tcp.host.trimmed() != endpointHost ||
            config.wave_tcp.port != port ||
            !config.wave_tcp.enabled;
        if (endpointChanged)
        {
            if (state_->remote_sky_config_applying_)
            {
                setRemoteSkyConfigStatus(state_->is_english_
                    ? QStringLiteral("Remote Sky config is already applying; TCP wave connection was not started.")
                    : QStringLiteral("天空端配置正在应用，暂未发起 TCP 波形连接。"),
                    true);
                return;
            }
            config.wave_tcp.enabled = true;
            config.wave_tcp.host = endpointHost;
            config.wave_tcp.port = port;
            QString error;
            if (!config.validate(&error))
            {
                setRemoteSkyConfigStatus(error, true);
                return;
            }
            state_->remote_sky_config_ = config;
            state_->remote_sky_config_loaded_ = true;
            state_->remote_sky_config_loaded_generation_ =
                state_->remote_sky_controller_ ? state_->remote_sky_controller_->linkGeneration() : 0;
            state_->remote_sky_config_dirty_ = true;
            setRemoteSkyConfigUi(config);
            if (isUiTestMode())
            {
                state_->remote_sky_baseline_config_ = config;
                state_->remote_sky_config_dirty_ = false;
            }
            else
            {
                if (!state_->remote_sky_controller_ || !state_->remote_sky_controller_->isOpen())
                {
                    setRemoteSkyConfigStatus(state_->is_english_
                        ? QStringLiteral("Telemetry link is disconnected; TCP wave endpoint was not sent.")
                        : QStringLiteral("天地数传已断开，未发送 TCP 波形地址。"),
                        true);
                    return;
                }
                state_->remote_wave_connect_after_config_apply_ = true;
                state_->remote_sky_config_applying_ = true;
                state_->remote_sky_config_apply_generation_ = state_->remote_sky_controller_->linkGeneration();
                state_->remote_sky_config_apply_seq_ =
                    state_->remote_sky_controller_->telemetryService()->setSkyConfig(config.toJson());
                if (state_->remote_sky_config_apply_seq_ == 0)
                {
                    state_->remote_sky_config_applying_ = false;
                    state_->remote_sky_config_apply_generation_ = 0;
                    clearPendingRemoteWaveTcpConnection();
                    setRemoteSkyConfigStatus(state_->is_english_
                        ? QStringLiteral("TCP wave endpoint was not sent.")
                        : QStringLiteral("未发送 TCP 波形地址。"),
                        true);
                }
                else
                {
                    setRemoteSkyConfigStatus(state_->is_english_
                        ? QStringLiteral("TCP wave endpoint sent; connecting after it is applied...")
                        : QStringLiteral("TCP 波形地址已发送，应用成功后自动连接..."));
                }
                updateRemoteSkyConfigControlsState();
                return;
            }
        }
    }

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
        publishUiTestEvent(connectRequested
                               ? QStringLiteral("ui_test_remote_wave_connection_enabled")
                               : QStringLiteral("ui_test_remote_wave_connection_disabled"),
                           connectRequested
                               ? (state_->is_english_ ? QStringLiteral("Simulated Sky waveform connection enabled")
                                                      : QStringLiteral("模拟天空端波形连接已开启"))
                               : (state_->is_english_ ? QStringLiteral("Simulated Sky waveform connection disabled")
                                                      : QStringLiteral("模拟天空端波形连接已关闭")),
                           {{QStringLiteral("device_id"), VaporView::skyDeviceIdName(VaporView::SkyDeviceId::WaveTcp)},
                            {QStringLiteral("requested_connected"), connectRequested}});
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
        if (state_->remote_sky_controller_)
        {
            state_->remote_sky_controller_->setDeviceState(VaporView::SkyDeviceId::WaveTcp,
                                                    VaporView::DeviceState::Disconnected);
            state_->remote_sky_controller_->clearDeviceData(VaporView::SkyDeviceId::WaveTcp);
        }
        state_->tcp_wave_panel_->setRemoteWaveTcpState(VaporView::DeviceState::Disconnected);
        updateRemoteTelemetrySummaryLabel();
    }
    sendRemoteDeviceCommand(connectRequested ? VaporView::CommandId::ConnectDevice : VaporView::CommandId::DisconnectDevice,
                            VaporView::SkyDeviceId::WaveTcp);
    updateHomeDeviceStatusCapsules();
}

void MainWindow::clearPendingRemoteWaveTcpConnection()
{
    state_->remote_wave_connect_after_config_read_ = false;
    state_->remote_wave_connect_after_config_apply_ = false;
    state_->remote_wave_pending_host_.clear();
    state_->remote_wave_pending_port_ = 0;
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

void MainWindow::updateAi8TemperatureTitleStatus()
{
    if (!state_->ai8_temperature_title_status_lbl_)
    {
        return;
    }

    const QString text = state_->ai8_temperature_controller_panel_
        ? state_->ai8_temperature_controller_panel_->currentOutputStatusText()
        : (state_->is_english_ ? QStringLiteral("Output: --") : QStringLiteral("输出：--"));
    state_->ai8_temperature_title_status_lbl_->setText(text);
    const QString toolTip = state_->is_english_
        ? QStringLiteral("Current AI-8 output state for the selected channel")
        : QStringLiteral("当前选中 AI-8 通道的输出状态");
    state_->ai8_temperature_title_status_lbl_->setToolTip(toolTip);
    state_->ai8_temperature_title_status_lbl_->setAccessibleName(toolTip);
}

void MainWindow::updateTemperatureTitleButtonsState()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    auto updateTitleButton = [this, nowMs](QToolButton *button,
                                           VaporView::SkyDeviceId device) {
        if (!button)
        {
            return;
        }

        const VaporView::DeviceState deviceState = homeDeviceActionState(device);
        const bool connected = deviceState == VaporView::DeviceState::Connected;
        const bool spinnerActive = deviceState == VaporView::DeviceState::Connecting ||
            deviceState == VaporView::DeviceState::Reconnecting ||
            homeDeviceActionSpinnerActive(device, nowMs);
        const VaporView::CommandId command = connected
            ? VaporView::CommandId::DisconnectDevice
            : VaporView::CommandId::ConnectDevice;
        const bool localActionIdle = !state_->connection_attempt_in_progress_ &&
            !state_->port_detection_in_progress_ && !state_->epsilon_reconfigure_in_progress_;
        const bool remoteMode = isRemoteSkyMode();
        const bool linkOpen = state_->remote_sky_controller_ && state_->remote_sky_controller_->isOpen();

        bool enabled = false;
        if (deviceState != VaporView::DeviceState::Disabled && !spinnerActive)
        {
            if (isUiTestMode())
            {
                enabled = true;
            }
            else if (remoteMode)
            {
                enabled = linkOpen;
            }
            else if (device == VaporView::SkyDeviceId::TemperatureController)
            {
                enabled = !connected
                    ? homeDevicePortSelected(device) && !connected && localActionIdle
                    : connected && localActionIdle;
            }
            else
            {
                enabled = !connected
                    ? homeDevicePortSelected(device) && !connected &&
                        state_->connect_btn_ && state_->connect_btn_->isEnabled()
                    : connected && state_->disconnect_btn_ && state_->disconnect_btn_->isEnabled();
            }
        }

        if (spinnerActive)
        {
            button->setIcon(createRotatedLucideIcon(
                QStringLiteral("refresh-cw"),
                toolbarColor(AppThemeColor::HomeDeviceSuccess),
                homeDeviceActionSpinnerDegrees(device, nowMs)));
        }
        else
        {
            const QColor iconColor = connected
                ? toolbarColor(AppThemeColor::ToolbarBlue)
                : deviceState == VaporView::DeviceState::Disabled
                    ? toolbarColor(AppThemeColor::ToolbarDisabled)
                    : toolbarColor(AppThemeColor::HomeDeviceSuccess);
            button->setIcon(createLucideIcon(deviceConfigRemoteIconName(command), iconColor));
        }
        button->setEnabled(enabled);
        button->setText(QString());
        button->setProperty("connected", connected);
        button->setProperty("state", spinnerActive
            ? QStringLiteral("connecting")
            : deviceState == VaporView::DeviceState::Disabled
                ? QStringLiteral("disabled")
                : connected
                    ? QStringLiteral("connected")
                    : QStringLiteral("disconnected"));
        button->setProperty("temperatureTitleCommand", deviceConfigRemoteActionKey(command));

        const QString deviceName = homeDeviceDisplayName(device, state_->is_english_);
        const QString actionText = spinnerActive
            ? (state_->is_english_ ? QStringLiteral("Connecting") : QStringLiteral("连接中"))
            : deviceConfigRemoteActionText(command, state_->is_english_);
        const QString modeHint = remoteMode
            ? (state_->is_english_ ? QStringLiteral("remote Sky device") : QStringLiteral("天空端设备"))
            : (state_->is_english_ ? QStringLiteral("local serial device") : QStringLiteral("本地串口设备"));
        const QString tooltip = state_->is_english_
            ? QStringLiteral("%1 %2 (%3)").arg(actionText, deviceName, modeHint)
            : QStringLiteral("%1%2（%3）").arg(actionText, deviceName, modeHint);
        button->setToolTip(tooltip);
        button->setAccessibleName(tooltip);
        button->setStatusTip(QString());
        button->style()->unpolish(button);
        button->style()->polish(button);
    };

    updateTitleButton(state_->temperature_title_action_btn_,
                      VaporView::SkyDeviceId::TemperatureController);
    updateTitleButton(state_->ai8_temperature_title_action_btn_,
                      VaporView::SkyDeviceId::Ai8TemperatureController);
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
        publishGroundLog(VaporView::LogLevel::Warning,
                         QStringLiteral("device.connection"),
                         QStringLiteral("temperature_controller_connection_rejected_missing_port"),
                         QStringLiteral("请先选择本地 RD105 串口。"),
                         {{QStringLiteral("device"), QStringLiteral("RD105")},
                          {QStringLiteral("device_id"), QStringLiteral("temperature_controller")},
                          {QStringLiteral("reason_code"), QStringLiteral("MISSING_ENDPOINT")},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("rd105:connect:missing_port")}});
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
        publishGroundLog(VaporView::LogLevel::Warning,
                         QStringLiteral("device.connection"),
                         QStringLiteral("temperature_controller_connection_rejected_invalid_baud"),
                         QStringLiteral("RD105 波特率无效。"),
                         {{QStringLiteral("device"), QStringLiteral("RD105")},
                          {QStringLiteral("device_id"), QStringLiteral("temperature_controller")},
                          {QStringLiteral("reason_code"), QStringLiteral("CONFIG_INVALID")},
                          {QStringLiteral("baud_text"), baudText},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("rd105:connect:invalid_baud")}});
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
    publishGroundLog(VaporView::LogLevel::Info,
                     QStringLiteral("device.connection"),
                     QStringLiteral("temperature_controller_connection_started"),
                     QStringLiteral("正在连接本地 RD105 温控器。"),
                     {{QStringLiteral("device"), QStringLiteral("RD105")},
                      {QStringLiteral("device_id"), QStringLiteral("temperature_controller")},
                      {QStringLiteral("port"), port},
                      {QStringLiteral("baud"), baud},
                      {QStringLiteral("sample_rate_hz"), rate},
                      {QStringLiteral("ui_visibility"), QStringLiteral("details")}});

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
                QVariantMap fields{{QStringLiteral("device"), QStringLiteral("RD105")},
                                   {QStringLiteral("device_id"), QStringLiteral("temperature_controller")},
                                   {QStringLiteral("ui_visibility"),
                                    connected ? QStringLiteral("details") : QStringLiteral("attention")}};
                if (!resultText.isEmpty())
                {
                    fields.insert(connected ? QStringLiteral("details") : QStringLiteral("system_error"),
                                  resultText);
                }
                if (!connected)
                {
                    fields.insert(QStringLiteral("error_code"), QStringLiteral("SERIAL_OPEN_FAILED"));
                    fields.insert(QStringLiteral("ui_dedupe_key"), QStringLiteral("rd105:connect:failed"));
                }
                if (connected)
                {
                    publishGroundLog(VaporView::LogLevel::Info,
                                     QStringLiteral("device.connection"),
                                     QStringLiteral("temperature_controller_connected"),
                                     QStringLiteral("本地 RD105 温控器已连接。"),
                                     fields);
                }
                else
                {
                    publishGroundLog(VaporView::LogLevel::Error,
                                     QStringLiteral("device.connection"),
                                     QStringLiteral("temperature_controller_connection_failed"),
                                     QStringLiteral("本地 RD105 温控器连接失败。"),
                                     fields);
                }
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
        publishGroundLog(VaporView::LogLevel::Warning,
                         QStringLiteral("device.connection"),
                         QStringLiteral("temperature_controller_connection_rejected_busy"),
                         QStringLiteral("另一个本地连接操作正在进行中。"),
                         {{QStringLiteral("device"), QStringLiteral("RD105")},
                          {QStringLiteral("device_id"), QStringLiteral("temperature_controller")},
                          {QStringLiteral("reason_code"), QStringLiteral("INVALID_STATE")},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("rd105:connect:busy")}});
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
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.connection"),
                         QStringLiteral("temperature_controller_disconnected"),
                         QStringLiteral("本地 RD105 温控器已断开。"),
                         {{QStringLiteral("device"), QStringLiteral("RD105")},
                          {QStringLiteral("device_id"), QStringLiteral("temperature_controller")},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
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
        publishUiTestEvent(QStringLiteral("ui_test_device_command_applied"),
                           QString(state_->is_english_ ? "Simulated device command: %1 / %2"
                                                       : "模拟设备命令：%1 / %2")
                               .arg(VaporView::commandIdName(command),
                                    VaporView::deviceStateName(state_->ui_test_model_->deviceState(device))),
                           {{QStringLiteral("device_id"), VaporView::skyDeviceIdName(device)},
                            {QStringLiteral("command"), VaporView::commandIdName(command)},
                            {QStringLiteral("connected"), connected}});
        updateConnectionStatus(false);
        return;
    }
    if (!state_->remote_sky_controller_ || !state_->remote_sky_controller_->isOpen())
    {
        publishGroundLog(VaporView::LogLevel::Warning,
                         QStringLiteral("device.command"),
                         QStringLiteral("remote_device_command_rejected_dependency_unavailable"),
                         QStringLiteral("天空端数传链路未连接，无法下发设备命令。"),
                         {{QStringLiteral("reason_code"), QStringLiteral("DEPENDENCY_UNAVAILABLE")},
                          {QStringLiteral("dependency"), QStringLiteral("remote_sky_telemetry")},
                          {QStringLiteral("device_id"), VaporView::skyDeviceIdName(device)},
                          {QStringLiteral("command"), VaporView::commandIdName(command)},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("remote_device_command:not_connected")}});
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
        publishUiTestEvent(QStringLiteral("ui_test_peak_search_range_applied"),
                           QString(state_->is_english_ ? "Peak search range applied in memory: [%1, %2)"
                                                       : "峰值搜索区间已在内存中应用：[%1, %2)")
                               .arg(startIndex).arg(endIndex),
                           {{QStringLiteral("start_index"), startIndex},
                            {QStringLiteral("end_index"), endIndex}});
        return;
    }
    if (!state_->remote_sky_controller_ || !state_->remote_sky_controller_->isOpen())
    {
        publishGroundLog(VaporView::LogLevel::Warning,
                         QStringLiteral("telemetry.command"),
                         QStringLiteral("peak_search_range_rejected_dependency_unavailable"),
                         QStringLiteral("天空端数传链路未连接，无法下发峰值搜索区间。"),
                         {{QStringLiteral("reason_code"), QStringLiteral("DEPENDENCY_UNAVAILABLE")},
                          {QStringLiteral("dependency"), QStringLiteral("remote_sky_telemetry")},
                          {QStringLiteral("range_start_index"), startIndex},
                          {QStringLiteral("range_end_index"), endIndex},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("peak_search:not_connected")}});
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
    publishGroundLog(VaporView::LogLevel::Info,
                     QStringLiteral("telemetry.command"),
                     QStringLiteral("peak_search_range_sent"),
                     QStringLiteral("峰值搜索区间已下发到天空端。"),
                     {{QStringLiteral("range_start_index"), startIndex},
                      {QStringLiteral("range_end_index"), endIndex},
                      {QStringLiteral("command_seq"), seq},
                      {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
}

void MainWindow::onRd105SessionAvailabilityChanged(bool available, const QString& reason)
{
    Q_UNUSED(available);
    Q_UNUSED(reason);
    updateTemperatureTitleButtonsState();
}

void MainWindow::onRd105SessionOperationStarted(
    quint64 requestId,
    VaporView::CommandId command,
    VaporView::TemperatureControllerCommand payload)
{
    const quint8 channel = payload.channel == 0 ? 1 : payload.channel;
    if (state_->temperature_controller_panel_)
    {
        state_->temperature_controller_panel_->markCommandPending(command, payload);
        state_->temperature_controller_panel_->setCommandStatus(
            temperatureCommandStatusText(command, channel, true));
    }
    QVariantMap fields = temperatureCommandLogFields(command, payload, channel);
    fields.insert(QStringLiteral("device"), QStringLiteral("RD105"));
    fields.insert(QStringLiteral("execution_path"),
                  rd105ExecutionPath(state_->rd105_device_session_
                      ? state_->rd105_device_session_->backend()
                      : VaporView::Ground::Devices::Rd105Backend::Local));
    fields.insert(QStringLiteral("request_id"), requestId);
    fields.insert(QStringLiteral("ui_visibility"), QStringLiteral("details"));
    publishTemperatureCommandLog(VaporView::LogLevel::Info,
                                 QStringLiteral("temperature_command_sent"),
                                 QStringLiteral("RD105 温控命令已提交。"),
                                 fields);
    restoreTemperatureCommandUi(command, channel);
}

void MainWindow::onRd105SessionOperationFinished(
    const VaporView::Ground::Devices::Rd105SessionResult& result)
{
    const quint8 channel = result.payload.channel == 0 ? 1 : result.payload.channel;
    QString detail = result.message;
    if (detail.isEmpty() && !result.success())
    {
        if (result.outcome == VaporView::Ground::Devices::Rd105OperationOutcome::Timeout)
        {
            detail = state_->is_english_ ? QStringLiteral("ACK timed out")
                                         : QStringLiteral("ACK 超时");
        }
        else if (result.error_code != VaporView::CommandErrorCode::Ok)
        {
            detail = VaporView::commandErrorCodeText(result.error_code, state_->is_english_);
        }
        else
        {
            detail = state_->is_english_ ? QStringLiteral("write/read-back confirmation failed")
                                         : QStringLiteral("写入或读回确认失败");
        }
    }

    if (result.backend == VaporView::Ground::Devices::Rd105Backend::Local)
    {
        if (result.has_latest_data &&
            result.latest_data.timestamp >= state_->current_temperature_controller_.timestamp)
        {
            state_->current_temperature_controller_ = result.latest_data;
        }
        if (result.success())
        {
            const auto settingsUpdate =
                VaporView::Ground::Devices::applyConfirmedTemperatureCommand(
                    state_->current_temperature_controller_,
                    result.command,
                    result.payload);
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
        }
        if (state_->device_panel_coordinator_)
        {
            state_->device_panel_coordinator_->updateTemperatureData(
                state_->current_temperature_controller_);
        }
    }
    else if (result.success() &&
             result.command == VaporView::CommandId::SetTemperatureDeviceAddress &&
             state_->remote_sky_config_loaded_)
    {
        state_->remote_sky_config_.temperature_controller.slave_address =
            static_cast<int>(result.payload.device_address);
        if (!state_->remote_sky_config_dirty_)
        {
            state_->remote_sky_baseline_config_.temperature_controller.slave_address =
                state_->remote_sky_config_.temperature_controller.slave_address;
        }
    }

    QVariantMap fields = temperatureCommandLogFields(result.command, result.payload, channel);
    fields.insert(QStringLiteral("device"), QStringLiteral("RD105"));
    fields.insert(QStringLiteral("execution_path"), rd105ExecutionPath(result.backend));
    fields.insert(QStringLiteral("request_id"), result.request_id);
    fields.insert(QStringLiteral("outcome"), rd105OutcomeName(result.outcome));
    fields.insert(QStringLiteral("command_error_code"),
                  commandErrorCodeIdentifier(result.error_code));

    if (result.success())
    {
        fields.insert(QStringLiteral("ui_visibility"), QStringLiteral("details"));
        publishTemperatureCommandLog(VaporView::LogLevel::Info,
                                     QStringLiteral("temperature_command_completed"),
                                     QStringLiteral("RD105 温控命令执行成功。"),
                                     fields);
    }
    else if (result.error_code == VaporView::CommandErrorCode::DeviceNotConnected ||
             result.outcome == VaporView::Ground::Devices::Rd105OperationOutcome::Disconnected)
    {
        fields.insert(QStringLiteral("reason_code"), QStringLiteral("DEVICE_NOT_CONNECTED"));
        fields.insert(QStringLiteral("ui_dedupe_key"),
                      temperatureCommandDedupeKey(
                          QStringLiteral("temperature_command_rejected_not_connected"),
                          result.command,
                          channel));
        publishTemperatureCommandLog(
            VaporView::LogLevel::Warning,
            QStringLiteral("temperature_command_rejected_not_connected"),
            result.backend == VaporView::Ground::Devices::Rd105Backend::Remote
                ? QStringLiteral("天空端 RD105 温控器不可用，无法下发温控命令。")
                : QStringLiteral("本地 RD105 温控器未连接，无法下发温控命令。"),
            fields);
    }
    else if (result.outcome == VaporView::Ground::Devices::Rd105OperationOutcome::Timeout)
    {
        fields.insert(QStringLiteral("error_code"), QStringLiteral("COMMAND_TIMEOUT"));
        fields.insert(QStringLiteral("ui_dedupe_key"),
                      temperatureCommandDedupeKey(
                          QStringLiteral("temperature_command_ack_timeout"),
                          result.command,
                          channel));
        publishTemperatureCommandLog(VaporView::LogLevel::Warning,
                                     QStringLiteral("temperature_command_ack_timeout"),
                                     QStringLiteral("RD105 温控命令 ACK 等待超时。"),
                                     fields);
    }
    else
    {
        fields.insert(QStringLiteral("error_code"),
                      result.error_code == VaporView::CommandErrorCode::ConfigApplyFailed
                          ? QStringLiteral("COMMAND_VERIFY_FAILED")
                          : commandErrorCodeIdentifier(result.error_code));
        fields.insert(QStringLiteral("ui_dedupe_key"),
                      temperatureCommandDedupeKey(
                          QStringLiteral("temperature_command_failed"),
                          result.command,
                          channel));
        publishTemperatureCommandLog(VaporView::LogLevel::Error,
                                     QStringLiteral("temperature_command_failed"),
                                     QStringLiteral("RD105 温控命令执行失败。"),
                                     fields);
    }

    if (state_->temperature_controller_panel_)
    {
        state_->temperature_controller_panel_->clearCommandPending(result.command, channel);
        state_->temperature_controller_panel_->setCommandStatus(
            temperatureCommandStatusText(result.command, channel, false, detail),
            !result.success());
    }
    restoreTemperatureCommandUi(result.command, channel);
}

void MainWindow::sendTemperatureCommand(
    VaporView::CommandId command,
    const VaporView::TemperatureControllerCommand& payload)
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
        publishUiTestEvent(QStringLiteral("ui_test_temperature_command_applied"),
                           QString(state_->is_english_
                                       ? "RD105 command applied in memory: %1"
                                       : "RD105 命令已在内存中应用：%1")
                               .arg(VaporView::commandIdName(command)),
                           {{QStringLiteral("device"), QStringLiteral("RD105")},
                            {QStringLiteral("command"), VaporView::commandIdName(command)},
                            {QStringLiteral("channel"), channel}});
        restoreTemperatureCommandUi(command, channel);
        return;
    }

    if (state_->rd105_device_session_)
    {
        state_->rd105_device_session_->sendCommand(command, payload);
        return;
    }

    VaporView::Ground::Devices::Rd105SessionResult result;
    result.command = command;
    result.payload = payload;
    result.backend = isRemoteSkyMode()
        ? VaporView::Ground::Devices::Rd105Backend::Remote
        : VaporView::Ground::Devices::Rd105Backend::Local;
    result.outcome = VaporView::Ground::Devices::Rd105OperationOutcome::Disconnected;
    result.error_code = VaporView::CommandErrorCode::DeviceNotConnected;
    result.message = state_->is_english_ ? QStringLiteral("RD105 session is not initialized.")
                                         : QStringLiteral("RD105 会话尚未初始化。");
    onRd105SessionOperationFinished(result);
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
    case VaporView::SkyDeviceId::Ai8TemperatureController:
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
    case VaporView::SkyDeviceId::Ai8TemperatureController:
        button = state_->device_config_.ai8_temperature_remote_action_btn;
        break;
    case VaporView::SkyDeviceId::WaveTcp:
    case VaporView::SkyDeviceId::All:
        return;
    }
    if (!button)
    {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const VaporView::DeviceState state = homeDeviceActionState(device);
    const bool connected = state == VaporView::DeviceState::Connected;
    const bool busy = state == VaporView::DeviceState::Connecting ||
        state == VaporView::DeviceState::Reconnecting ||
        homeDeviceActionSpinnerActive(device, nowMs);
    const VaporView::CommandId command = connected
        ? VaporView::CommandId::DisconnectDevice
        : VaporView::CommandId::ConnectDevice;
    applyDeviceConfigRemoteButtonPresentation(button, command, device, state_->is_english_, false);
    if (busy)
    {
        button->setIcon(createRotatedLucideIcon(QStringLiteral("refresh-cw"),
                                                toolbarColor(AppThemeColor::HomeDeviceSuccess),
                                                homeDeviceActionSpinnerDegrees(device, nowMs)));
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
        publishGroundLog(VaporView::LogLevel::Warning,
                         QStringLiteral("device.navigation.command"),
                         QStringLiteral("imu_profile_apply_rejected_missing_port"),
                         QStringLiteral("请先选择 IMU 串口。"),
                         {{QStringLiteral("device"), QStringLiteral("IMU")},
                          {QStringLiteral("reason_code"), QStringLiteral("MISSING_ENDPOINT")},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("imu:profile_apply:missing_port")}});
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
        publishGroundLog(VaporView::LogLevel::Warning,
                         QStringLiteral("device.navigation.command"),
                         QStringLiteral("imu_profile_apply_rejected_unsupported"),
                         QStringLiteral("IMU 输出格式或频率不受支持。"),
                         {{QStringLiteral("device"), QStringLiteral("IMU")},
                          {QStringLiteral("reason_code"), QStringLiteral("COMMAND_NOT_SUPPORTED")},
                          {QStringLiteral("output_format"), targetFormat},
                          {QStringLiteral("rate_hz"), targetRate},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("imu:profile_apply:unsupported")}});
        return false;
    }

    setImuFormatSelection(targetFormat);
    setImuBaudSelection(targetBaud);
    setImuRateSelection(targetRate);
    saveRememberedInputState();

    if (isUiTestMode())
    {
        publishUiTestEvent(QStringLiteral("ui_test_imu_profile_applied"),
                           QString(state_->is_english_ ? "IMU profile applied in memory: %1, %2 baud, %3 Hz"
                                                       : "IMU 配置已在内存中应用：%1，%2 波特，%3 Hz")
                               .arg(targetFormat).arg(targetBaud).arg(targetRate),
                           {{QStringLiteral("output_format"), targetFormat},
                            {QStringLiteral("baud"), targetBaud},
                            {QStringLiteral("rate_hz"), targetRate}});
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
