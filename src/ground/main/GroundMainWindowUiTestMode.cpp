#include "ground/main/GroundMainWindowImplementation.h"

#include <QSignalBlocker>

using UiTestScenario = VaporView::Ground::Devices::UiTestScenario;

namespace
{

QVariantMap comboItemState(const QComboBox *combo, int index)
{
    QVariantMap state;
    state.insert(QStringLiteral("text"), combo->itemText(index));
    state.insert(QStringLiteral("icon"), combo->itemIcon(index));
    const QModelIndex modelIndex = combo->model()->index(index, combo->modelColumn(), combo->rootModelIndex());
    const QMap<int, QVariant> itemData = combo->model()->itemData(modelIndex);
    QVariantMap roles;
    for (auto it = itemData.cbegin(); it != itemData.cend(); ++it)
    {
        roles.insert(QString::number(it.key()), it.value());
    }
    state.insert(QStringLiteral("roles"), roles);
    state.insert(QStringLiteral("flags"), static_cast<int>(combo->model()->flags(modelIndex)));
    return state;
}

} // namespace

void MainWindow::captureUiTestWidgetState()
{
    state_->ui_test_widget_states_.clear();
    state_->ui_test_existing_top_levels_.clear();
    QList<QWidget *> roots = QApplication::topLevelWidgets();
    if (!roots.contains(this))
    {
        roots.push_back(this);
    }
    for (QWidget *root : roots) state_->ui_test_existing_top_levels_.push_back(root);

    QSet<QWidget *> captured;
    for (QWidget *root : roots)
    {
        if (!root)
        {
            continue;
        }
        QVariantMap windowState;
        windowState.insert(QStringLiteral("kind"), QStringLiteral("window"));
        windowState.insert(QStringLiteral("geometry"), root->saveGeometry());
        windowState.insert(QStringLiteral("visible"), root->isVisible());
        state_->ui_test_widget_states_.push_back({root, windowState});

        const QList<QWidget *> widgets = root->findChildren<QWidget *>();
        for (QWidget *widget : widgets)
        {
            if (!widget || captured.contains(widget))
            {
                continue;
            }
            QVariantMap value;
            if (auto *combo = qobject_cast<QComboBox *>(widget))
            {
                value.insert(QStringLiteral("kind"), QStringLiteral("combo"));
                value.insert(QStringLiteral("index"), combo->currentIndex());
                value.insert(QStringLiteral("text"), combo->currentText());
                QVariantList items;
                for (int index = 0; index < combo->count(); ++index)
                {
                    items.push_back(comboItemState(combo, index));
                }
                value.insert(QStringLiteral("items"), items);
            }
            else if (auto *edit = qobject_cast<QLineEdit *>(widget))
            {
                value.insert(QStringLiteral("kind"), QStringLiteral("line-edit"));
                value.insert(QStringLiteral("text"), edit->text());
            }
            else if (auto *dateTime = qobject_cast<QDateTimeEdit *>(widget))
            {
                value.insert(QStringLiteral("kind"), QStringLiteral("date-time"));
                value.insert(QStringLiteral("value"), dateTime->dateTime());
            }
            else if (auto *spin = qobject_cast<QSpinBox *>(widget))
            {
                value.insert(QStringLiteral("kind"), QStringLiteral("spin"));
                value.insert(QStringLiteral("value"), spin->value());
            }
            else if (auto *doubleSpin = qobject_cast<QDoubleSpinBox *>(widget))
            {
                value.insert(QStringLiteral("kind"), QStringLiteral("double-spin"));
                value.insert(QStringLiteral("value"), doubleSpin->value());
            }
            else if (auto *button = qobject_cast<QAbstractButton *>(widget); button && button->isCheckable())
            {
                value.insert(QStringLiteral("kind"), QStringLiteral("button"));
                value.insert(QStringLiteral("checked"), button->isChecked());
            }
            else if (auto *slider = qobject_cast<QAbstractSlider *>(widget))
            {
                value.insert(QStringLiteral("kind"), QStringLiteral("slider"));
                value.insert(QStringLiteral("value"), slider->value());
            }
            else if (auto *splitter = qobject_cast<QSplitter *>(widget))
            {
                value.insert(QStringLiteral("kind"), QStringLiteral("splitter"));
                QVariantList sizes;
                for (int size : splitter->sizes()) sizes.push_back(size);
                value.insert(QStringLiteral("sizes"), sizes);
            }
            else if (auto *stack = qobject_cast<QStackedWidget *>(widget))
            {
                value.insert(QStringLiteral("kind"), QStringLiteral("stack"));
                value.insert(QStringLiteral("index"), stack->currentIndex());
            }
            else if (auto *tabs = qobject_cast<QTabWidget *>(widget))
            {
                value.insert(QStringLiteral("kind"), QStringLiteral("tabs"));
                value.insert(QStringLiteral("index"), tabs->currentIndex());
            }
            if (!value.isEmpty())
            {
                captured.insert(widget);
                state_->ui_test_widget_states_.push_back({widget, value});
            }
        }
    }
}

void MainWindow::restoreUiTestWidgetState()
{
    for (QWidget *window : QApplication::topLevelWidgets())
    {
        const bool existed = std::any_of(
            state_->ui_test_existing_top_levels_.cbegin(),
            state_->ui_test_existing_top_levels_.cend(),
            [window](const QPointer<QWidget>& saved) { return saved.data() == window; });
        if (window && window != this && !existed)
        {
            window->close();
        }
    }

    std::vector<std::unique_ptr<QSignalBlocker>> blockers;
    blockers.reserve(static_cast<std::size_t>(state_->ui_test_widget_states_.size()));
    for (const auto& entry : state_->ui_test_widget_states_)
    {
        if (entry.widget)
        {
            blockers.push_back(std::make_unique<QSignalBlocker>(entry.widget.data()));
        }
    }

    for (const auto& entry : state_->ui_test_widget_states_)
    {
        QWidget *widget = entry.widget.data();
        if (!widget)
        {
            continue;
        }
        const QString kind = entry.state.value(QStringLiteral("kind")).toString();
        if (kind == QStringLiteral("window"))
        {
            widget->restoreGeometry(entry.state.value(QStringLiteral("geometry")).toByteArray());
            widget->setVisible(entry.state.value(QStringLiteral("visible")).toBool());
        }
        else if (auto *combo = qobject_cast<QComboBox *>(widget); kind == QStringLiteral("combo") && combo)
        {
            combo->clear();
            const QVariantList items = entry.state.value(QStringLiteral("items")).toList();
            for (const QVariant& itemValue : items)
            {
                const QVariantMap item = itemValue.toMap();
                const QVariantMap roles = item.value(QStringLiteral("roles")).toMap();
                combo->addItem(item.value(QStringLiteral("icon")).value<QIcon>(),
                               item.value(QStringLiteral("text")).toString());
                const int index = combo->count() - 1;
                for (auto it = roles.cbegin(); it != roles.cend(); ++it)
                {
                    combo->setItemData(index, it.value(), it.key().toInt());
                }
                if (auto *model = qobject_cast<QStandardItemModel *>(combo->model()))
                {
                    if (QStandardItem *standardItem = model->item(index))
                    {
                        standardItem->setFlags(static_cast<Qt::ItemFlags>(item.value(QStringLiteral("flags")).toInt()));
                    }
                }
            }
            const int index = entry.state.value(QStringLiteral("index")).toInt();
            combo->setCurrentIndex(index >= -1 && index < combo->count() ? index : -1);
            if (combo->isEditable()) combo->setEditText(entry.state.value(QStringLiteral("text")).toString());
        }
        else if (auto *edit = qobject_cast<QLineEdit *>(widget); kind == QStringLiteral("line-edit") && edit)
        {
            edit->setText(entry.state.value(QStringLiteral("text")).toString());
        }
        else if (auto *dateTime = qobject_cast<QDateTimeEdit *>(widget); kind == QStringLiteral("date-time") && dateTime)
        {
            dateTime->setDateTime(entry.state.value(QStringLiteral("value")).toDateTime());
        }
        else if (auto *spin = qobject_cast<QSpinBox *>(widget); kind == QStringLiteral("spin") && spin)
        {
            spin->setValue(entry.state.value(QStringLiteral("value")).toInt());
        }
        else if (auto *doubleSpin = qobject_cast<QDoubleSpinBox *>(widget); kind == QStringLiteral("double-spin") && doubleSpin)
        {
            doubleSpin->setValue(entry.state.value(QStringLiteral("value")).toDouble());
        }
        else if (auto *button = qobject_cast<QAbstractButton *>(widget); kind == QStringLiteral("button") && button)
        {
            button->setChecked(entry.state.value(QStringLiteral("checked")).toBool());
        }
        else if (auto *slider = qobject_cast<QAbstractSlider *>(widget); kind == QStringLiteral("slider") && slider)
        {
            slider->setValue(entry.state.value(QStringLiteral("value")).toInt());
        }
        else if (auto *splitter = qobject_cast<QSplitter *>(widget); kind == QStringLiteral("splitter") && splitter)
        {
            QList<int> sizes;
            for (const QVariant& size : entry.state.value(QStringLiteral("sizes")).toList()) sizes.push_back(size.toInt());
            splitter->setSizes(sizes);
        }
        else if (auto *stack = qobject_cast<QStackedWidget *>(widget); kind == QStringLiteral("stack") && stack)
        {
            stack->setCurrentIndex(entry.state.value(QStringLiteral("index")).toInt());
        }
        else if (auto *tabs = qobject_cast<QTabWidget *>(widget); kind == QStringLiteral("tabs") && tabs)
        {
            tabs->setCurrentIndex(entry.state.value(QStringLiteral("index")).toInt());
        }
    }
    state_->ui_test_existing_top_levels_.clear();
    state_->ui_test_widget_states_.clear();
}

bool MainWindow::isUiTestMode() const
{
    return state_->ui_test_mode_enabled_;
}

bool MainWindow::canEnterUiTestMode(QString *reason) const
{
    const bool busy = state_->is_connected_ ||
        state_->connection_attempt_in_progress_ ||
        state_->port_detection_in_progress_ ||
        state_->epsilon_reconfigure_in_progress_ ||
        anyCollectorRunning() ||
        (state_->tcp_wave_panel_ &&
         (state_->tcp_wave_panel_->isConnected() || state_->tcp_wave_panel_->isConnecting())) ||
        (state_->remote_sky_controller_ && state_->remote_sky_controller_->isOpen()) ||
        (state_->recording_service_ && state_->recording_service_->isSessionOpen()) ||
        (state_->recording_schedule_controller_ && state_->recording_schedule_controller_->isActive()) ||
        state_->rtk_service_running_ ||
        (state_->rtk_config_dialog_ && state_->rtk_config_dialog_->hasActiveExternalOperation());
    if (busy && reason)
    {
        *reason = state_->is_english_
            ? QStringLiteral("Disconnect devices and stop recording, RTK, detection, and background tasks before entering UI Test Mode.")
            : QStringLiteral("进入界面测试模式前，请断开全部设备并停止记录、RTK、自动识别和后台任务。");
    }
    return !busy;
}

void MainWindow::onUiTestModeTriggered(bool enabled)
{
    setUiTestModeEnabled(enabled);
}

void MainWindow::onUiTestScenarioTriggered(QAction *action)
{
    if (!action || !isUiTestMode())
    {
        return;
    }
    setUiTestScenario(static_cast<UiTestScenario>(action->data().toInt()));
}

void MainWindow::setUiTestModeEnabled(bool enabled)
{
    if (enabled == state_->ui_test_mode_enabled_)
    {
        updateUiTestModeUi();
        return;
    }

    if (enabled)
    {
        QString reason;
        if (!canEnterUiTestMode(&reason))
        {
            if (state_->ui_test_mode_action_)
            {
                const QSignalBlocker blocker(state_->ui_test_mode_action_);
                state_->ui_test_mode_action_->setChecked(false);
            }
            QMessageBox::information(
                this,
                state_->is_english_ ? QStringLiteral("UI Test Mode") : QStringLiteral("界面测试模式"),
                reason);
            return;
        }

        VaporView::setSettingsWritesSuspended(true);
        state_->ui_test_saved_page_index_ = state_->main_page_stack_
            ? state_->main_page_stack_->currentIndex() : 0;
        state_->ui_test_saved_sidebar_width_ = currentAppSidebarWidth();
        state_->ui_test_saved_font_scale_percent_ = state_->font_scale_percent_;
        state_->ui_test_saved_dark_theme_enabled_ = state_->dark_theme_enabled_;
        state_->ui_test_saved_recording_directory_ = state_->recording_directory_;
        state_->ui_test_session_viewer_existed_ = state_->session_viewer_window_ != nullptr;
        state_->ui_test_sky_dialog_existed_ = state_->sky_device_config_dialog_ != nullptr;
#ifdef VAPORVIEW_HAS_OSGEARTH
        state_->ui_test_map3d_window_existed_ =
            state_->map3d_controller_ && state_->map3d_controller_->window() != nullptr;
#endif
        captureUiTestWidgetState();
        state_->ui_test_started_ms_ = QDateTime::currentMSecsSinceEpoch();
        state_->ui_test_connection_in_progress_ = false;
        resetUiTestRecording();
        state_->ui_test_model_->reset(0);
        state_->ui_test_mode_enabled_ = true;
        if (state_->tcp_wave_panel_)
        {
            state_->tcp_wave_panel_->setUiTestMode(true);
        }
        if (state_->rtk_config_dialog_)
        {
            state_->rtk_config_dialog_->setUiTestMode(true);
        }
        if (state_->sky_device_config_dialog_)
        {
            state_->sky_device_config_dialog_->setUiTestMode(true);
        }
        if (state_->session_viewer_window_)
        {
            state_->session_viewer_window_->setUiTestMode(true);
        }
#ifdef VAPORVIEW_HAS_OSGEARTH
        if (state_->map3d_controller_)
        {
            state_->map3d_controller_->setUiTestMode(true);
        }
#endif

        const QStringList testPorts{
            QStringLiteral("UI-TEST-EPSILON"),
            QStringLiteral("UI-TEST-GNSS"),
            QStringLiteral("UI-TEST-IMU"),
            QStringLiteral("UI-TEST-PTB"),
            QStringLiteral("UI-TEST-HMP"),
            QStringLiteral("UI-TEST-LIDAR"),
            QStringLiteral("UI-TEST-RD105"),
            QStringLiteral("UI-TEST-SKY")
        };
        const QList<QComboBox *> portCombos{
            state_->epsilon_port_combo_, state_->gnss_port_combo_, state_->imu_port_combo_,
            state_->ptb_port_combo_, state_->hmp_port_combo_,
            state_->lidar_port_combo_, state_->temperature_port_combo_,
            state_->sky_telemetry_port_combo_
        };
        for (int index = 0; index < portCombos.size(); ++index)
        {
            if (portCombos[index])
            {
                refreshLocalSerialPortComboOptions(portCombos[index], testPorts, testPorts[index]);
                setLocalSerialPortComboText(portCombos[index], testPorts[index]);
            }
        }
        syncDeviceConfigPageFromHome();
        updateUiTestModeUi();
        updateConnectionStatus(false);
        applyUiTestSnapshot();
        logUiTest(state_->is_english_
            ? QStringLiteral("UI Test Mode enabled; device, recording, and settings operations are simulated in memory. RTK mountpoint detection and Test Connection may access the NTRIP network with sandbox settings.")
            : QStringLiteral("界面测试模式已开启；设备、记录和设置操作仅在内存中模拟；RTK“检测挂载点”和“测试连接”可使用沙箱配置访问 NTRIP 网络。"));
        return;
    }

    if (state_->recording_schedule_controller_ && state_->recording_schedule_controller_->isActive())
    {
        state_->recording_schedule_controller_->cancel();
    }
    state_->ui_test_connection_in_progress_ = false;
    resetUiTestRecording();
    state_->ui_test_mode_enabled_ = false;
    if (state_->tcp_wave_panel_)
    {
        state_->tcp_wave_panel_->setUiTestMode(false);
    }
    if (state_->rtk_config_dialog_)
    {
        state_->rtk_config_dialog_->setUiTestMode(false);
    }
    if (state_->sky_device_config_dialog_)
    {
        state_->sky_device_config_dialog_->setUiTestMode(false);
        if (!state_->ui_test_sky_dialog_existed_)
        {
            state_->sky_device_config_dialog_->close();
            delete state_->sky_device_config_dialog_;
            state_->sky_device_config_dialog_ = nullptr;
        }
    }
    if (state_->session_viewer_window_ && !state_->ui_test_session_viewer_existed_)
    {
        state_->session_viewer_window_->close();
        delete state_->session_viewer_window_;
        state_->session_viewer_window_ = nullptr;
    }
    else if (state_->session_viewer_window_)
    {
        state_->session_viewer_window_->setUiTestMode(false);
    }
#ifdef VAPORVIEW_HAS_OSGEARTH
    if (state_->map3d_controller_)
    {
        state_->map3d_controller_->setUiTestMode(false);
        if (!state_->ui_test_map3d_window_existed_)
        {
            state_->map3d_controller_->close();
        }
    }
#endif

    loadRememberedInputState();
    state_->recording_directory_ = state_->ui_test_saved_recording_directory_;
    if (state_->font_scale_percent_ != state_->ui_test_saved_font_scale_percent_)
    {
        setFontScale(state_->ui_test_saved_font_scale_percent_);
    }
    if (state_->dark_theme_enabled_ != state_->ui_test_saved_dark_theme_enabled_)
    {
        state_->dark_theme_enabled_ = state_->ui_test_saved_dark_theme_enabled_;
        if (qApp)
        {
            qApp->setProperty(kAppDarkThemeProperty, state_->dark_theme_enabled_);
        }
        applyStyleConfiguration();
    }
    setAppSidebarWidth(state_->ui_test_saved_sidebar_width_);
    if (state_->main_page_stack_)
    {
        const QSignalBlocker blocker(state_->main_page_stack_);
        state_->main_page_stack_->setCurrentIndex(state_->ui_test_saved_page_index_);
    }
    restoreUiTestWidgetState();
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
    updateUiTestModeUi();
    updateConnectionStatus(false);
    VaporView::setSettingsWritesSuspended(false);
    logUiTest(state_->is_english_ ? QStringLiteral("UI Test Mode disabled")
                                  : QStringLiteral("界面测试模式已关闭"));
}

void MainWindow::setUiTestScenario(UiTestScenario scenario)
{
    if (!isUiTestMode())
    {
        return;
    }
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - state_->ui_test_started_ms_;
    state_->ui_test_model_->setScenario(scenario, elapsed);
    updateUiTestModeUi();
    const QString name = scenario == UiTestScenario::Normal
        ? (state_->is_english_ ? QStringLiteral("Normal operation") : QStringLiteral("正常运行"))
        : scenario == UiTestScenario::PartialFailure
            ? (state_->is_english_ ? QStringLiteral("Partial device failure") : QStringLiteral("部分设备异常"))
            : (state_->is_english_ ? QStringLiteral("Data stalled") : QStringLiteral("数据停更"));
    logUiTest((state_->is_english_ ? QStringLiteral("Scenario: %1") : QStringLiteral("场景：%1")).arg(name));
    applyUiTestSnapshot();
}

void MainWindow::updateUiTestModeUi()
{
    if (state_->ui_test_mode_action_)
    {
        const QSignalBlocker blocker(state_->ui_test_mode_action_);
        state_->ui_test_mode_action_->setChecked(state_->ui_test_mode_enabled_);
    }
    if (QMenu *scenarioMenu = findChild<QMenu *>(QStringLiteral("uiTestScenarioMenu")))
    {
        scenarioMenu->setEnabled(state_->ui_test_mode_enabled_);
    }
    const UiTestScenario scenario = state_->ui_test_model_->scenario();
    for (QAction *action : {state_->ui_test_normal_action_,
                            state_->ui_test_partial_failure_action_,
                            state_->ui_test_stalled_action_})
    {
        if (action)
        {
            const QSignalBlocker blocker(action);
            action->setChecked(action->data().toInt() == static_cast<int>(scenario));
        }
    }
    if (state_->ui_test_mode_badge_)
    {
        state_->ui_test_mode_badge_->setVisible(state_->ui_test_mode_enabled_);
    }
    discardTitleApplicationMenuPanel();
    updateCustomTitleBarTexts();
}

void MainWindow::applyUiTestSnapshot()
{
    if (!isUiTestMode() || !state_->ui_test_model_)
    {
        return;
    }
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - state_->ui_test_started_ms_;
    const auto snapshot = state_->ui_test_model_->snapshot(elapsed);
    state_->current_epsilon_ = snapshot.epsilon;
    state_->current_gnss_ = snapshot.gnss;
    state_->current_imu_ = snapshot.imu;
    state_->current_ptb_ = snapshot.ptb;
    state_->current_hmp_ = snapshot.hmp;
    state_->current_lidar_ = snapshot.lidar;
    state_->current_temperature_controller_ = snapshot.temperature;
    if (state_->device_panel_coordinator_)
    {
        state_->device_panel_coordinator_->updateAllData(
            snapshot.epsilon, snapshot.gnss, static_cast<quint64>(elapsed) * 1000,
            snapshot.imu, static_cast<quint64>(elapsed) * 1000,
            snapshot.ptb, snapshot.hmp, snapshot.lidar, snapshot.temperature);
        DevicePanelRates rates;
        rates.epsilonHz = snapshot.epsilonRateHz;
        rates.gnssHz = snapshot.gnssRateHz;
        rates.imuHz = snapshot.imuRateHz;
        rates.ptbHz = snapshot.ptbRateHz;
        rates.hmpHz = snapshot.hmpRateHz;
        rates.lidarHz = snapshot.lidarRateHz;
        rates.temperatureHz = snapshot.temperatureRateHz;
        state_->device_panel_coordinator_->updateRates(rates);
    }
    updateEnvironmentStatusIcons(snapshot.lidar.valid, snapshot.ptb.valid, snapshot.hmp.valid);
    if (state_->tcp_wave_panel_ && !snapshot.rawWaveform.isEmpty())
    {
        const quint64 timestampUs = static_cast<quint64>(std::max<qint64>(1, elapsed)) * 1000;
        state_->tcp_wave_panel_->injectRemoteRawSignalFrame(timestampUs, snapshot.rawWaveform);
        state_->tcp_wave_panel_->injectRemoteSecondHarmonicFrame(timestampUs, snapshot.harmonicWaveform);
        state_->tcp_wave_panel_->injectRemoteWaveformFeature(snapshot.waveformFeature);
    }
#ifdef VAPORVIEW_HAS_OSGEARTH
    if (snapshot.epsilon.valid)
    {
        maybeForwardMap3DSample(snapshot.epsilon,
            VaporView::Ground::Session::GroundRecordingService::currentTimestampUs());
    }
#endif
    updateHomeDeviceStatusCapsules();
    updateDeviceConfigState();
    updateRecordingStatusLabel();
}

void MainWindow::logUiTest(const QString& message)
{
    log(QStringLiteral("[界面测试] %1").arg(message));
}
