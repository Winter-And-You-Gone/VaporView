#include "ground/main/GroundMainWindowImplementation.h"
#include "ground/devices/DeviceRatePolicy.h"

bool MainWindow::validateEpsilonPacketBandwidth(
    const std::map<uint8_t, int>& packetRates,
    const QString& baudText,
    bool showWarning)
{
    bool baudOk = false;
    const int baudRate = baudText.trimmed().toInt(&baudOk);
    const EpsilonSerialBandwidth bandwidth = epsilonSerialBandwidth(
        packetRates,
        baudOk ? baudRate : 0);
    if (bandwidth.fits())
    {
        return true;
    }

    const QString message = state_->is_english_
        ? QStringLiteral("The selected EPSILON packet rates require about %1 kbit/s, which exceeds the %2 kbit/s safe limit for %3 baud (80% utilization). Reduce packet rates or use a higher main-port baud rate.")
              .arg(QString::number(bandwidth.required_bits_per_second / 1000.0, 'f', 2),
                   QString::number(bandwidth.limit_bits_per_second / 1000.0, 'f', 2),
                   baudText)
        : QStringLiteral("当前 EPSILON 包频率约需 %1 kbit/s，超过 %3 波特率按 80% 利用率计算的 %2 kbit/s 安全上限。请降低包频率或提高主串口波特率。")
              .arg(QString::number(bandwidth.required_bits_per_second / 1000.0, 'f', 2),
                   QString::number(bandwidth.limit_bits_per_second / 1000.0, 'f', 2),
                   baudText);
    publishGroundLog(VaporView::LogLevel::Warning,
                     QStringLiteral("device.navigation.command"),
                     QStringLiteral("epsilon_packet_profile_rejected_bandwidth"),
                     QStringLiteral("EPSILON 包频率超过串口安全带宽。"),
                     {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                      {QStringLiteral("required_kbps"), bandwidth.required_bits_per_second / 1000.0},
                      {QStringLiteral("limit_kbps"), bandwidth.limit_bits_per_second / 1000.0},
                      {QStringLiteral("baud_text"), baudText},
                      {QStringLiteral("reason_code"), QStringLiteral("CONFIG_INVALID")},
                      {QStringLiteral("details"), message},
                      {QStringLiteral("ui_dedupe_key"), QStringLiteral("epsilon:packet_profile:bandwidth")}});
    if (showWarning)
    {
        QMessageBox::warning(this,
                             state_->is_english_ ? QStringLiteral("Packet Rates Exceed Serial Bandwidth")
                                                 : QStringLiteral("包频率超过串口带宽"),
                             message);
    }
    return false;
}

void MainWindow::applyEpsilonMainAntennaLeverArm(
    double xM,
    double yM,
    double zM,
    std::function<void(bool, const QString&)> completion)
{
    auto fail = [&completion](const QString& message) {
        if (completion)
        {
            completion(false, message);
        }
    };

    if (isUiTestMode())
    {
        publishUiTestEvent(QStringLiteral("ui_test_epsilon_main_antenna_lever_arm_applied"),
                           QString(state_->is_english_
                               ? "Applied EPSILON main antenna lever arm in memory: X=%1, Y=%2, Z=%3 m"
                               : "已在内存中应用 EPSILON 主天线杆臂：X=%1，Y=%2，Z=%3 m")
                               .arg(xM, 0, 'f', 4).arg(yM, 0, 'f', 4).arg(zM, 0, 'f', 4),
                           {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                            {QStringLiteral("x_m"), xM},
                            {QStringLiteral("y_m"), yM},
                            {QStringLiteral("z_m"), zM}});
        if (completion) completion(true, QString());
        return;
    }

    if (state_->connection_attempt_in_progress_ || state_->port_detection_in_progress_ || state_->epsilon_reconfigure_in_progress_)
    {
        fail(state_->is_english_
            ? QStringLiteral("EPSILON is busy. Wait for the current connection or configuration task to finish.")
            : QStringLiteral("EPSILON 当前正忙，请等待连接或配置任务结束后再试。"));
        return;
    }

    if (state_->recording_service_->isActive())
    {
        fail(state_->is_english_
            ? QStringLiteral("Stop recording before configuring the EPSILON main antenna lever arm.")
            : QStringLiteral("请先结束记录，再配置 EPSILON 主天线杆臂。"));
        return;
    }

    const QString epsilonPort = localSerialPortComboValue(state_->epsilon_port_combo_);
    if (epsilonPort.isEmpty() || epsilonPort.startsWith(QStringLiteral("--")))
    {
        fail(state_->is_english_
            ? QStringLiteral("Select the EPSILON main serial port first.")
            : QStringLiteral("请先选择 EPSILON 主串口。"));
        return;
    }

    const QString epsilonBaudText = state_->epsilon_baud_combo_ ? state_->epsilon_baud_combo_->currentText().trimmed() : QStringLiteral("921600");
    bool baudOk = false;
    const int epsilonBaud = epsilonBaudText.toInt(&baudOk);
    if (!baudOk || epsilonBaud <= 0)
    {
        fail(QString(state_->is_english_ ? "Invalid EPSILON baud rate: %1" : "EPSILON 波特率无效: %1").arg(epsilonBaudText));
        return;
    }

    const bool english = state_->is_english_;
    const QString values = QStringLiteral("X=%1 m, Y=%2 m, Z=%3 m")
        .arg(QString::number(xM, 'f', 4),
             QString::number(yM, 'f', 4),
             QString::number(zM, 'f', 4));
    const std::shared_ptr<VaporView::EpsilonCollector> liveCollector = snapshotCollectors().epsilon;
    const bool shouldRestartCollector = liveCollector && liveCollector->isRunning();

    state_->epsilon_reconfigure_in_progress_ = true;
    updateConnectionStatus(state_->is_connected_);

    publishGroundLog(VaporView::LogLevel::Info,
                     QStringLiteral("device.navigation.command"),
                     QStringLiteral("epsilon_main_antenna_lever_arm_config_started"),
                     QStringLiteral("正在通过主串口下发 EPSILON 主天线杆臂配置。"),
                     {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                      {QStringLiteral("operation"), QStringLiteral("main_antenna_lever_arm")},
                      {QStringLiteral("port"), epsilonPort},
                      {QStringLiteral("baud"), epsilonBaud},
                      {QStringLiteral("values"), values},
                      {QStringLiteral("ui_visibility"), QStringLiteral("details")}});

    if (state_->epsilon_reconfigure_thread_.joinable())
    {
        state_->epsilon_reconfigure_thread_.join();
    }

    state_->epsilon_reconfigure_thread_ = std::thread([
        this,
        xM,
        yM,
        zM,
        epsilonPort,
        epsilonBaud,
        epsilonBaudText,
        english,
        liveCollector,
        shouldRestartCollector,
        completion = std::move(completion)]() mutable {
        VaporView::Ground::EpsilonDeviceOperation operation;
        operation.port = epsilonPort;
        operation.baud = epsilonBaud;
        operation.baud_text = epsilonBaudText;
        operation.english = english;
        operation.live_collector = liveCollector;
        operation.restart_live_stream = shouldRestartCollector;

        const auto serviceLog = [this](VaporView::Ground::EpsilonConfigurationLogEntry entry) {
            QMetaObject::invokeMethod(this, [this, entry = std::move(entry)]() mutable {
                publishGroundLog(entry.level,
                                 entry.category,
                                 entry.event,
                                 entry.message,
                                 std::move(entry.fields));
            }, Qt::QueuedConnection);
        };
        const VaporView::Ground::EpsilonConfigurationResult result =
            VaporView::Ground::EpsilonConfigurationService::applyMainAntennaLeverArm(
                operation, xM, yM, zM, serviceLog);

        QMetaObject::invokeMethod(this, [this, result, completion = std::move(completion)]() mutable {
            state_->epsilon_reconfigure_in_progress_ = false;
            updateConnectionStatus(anyCollectorRunning());
            if (completion)
            {
                completion(result.succeeded(), result.error_message);
            }
        }, Qt::QueuedConnection);
    });
}

void MainWindow::syncRtkConfigPageState()
{
    if (!state_->rtk_config_dialog_)
    {
        return;
    }

    state_->rtk_config_dialog_->setEpsilonDataProvider([this]() {
        const CollectorSnapshot collectors = snapshotCollectors();
        return collectors.epsilon ? collectors.epsilon->getLatestData() : state_->current_epsilon_;
    });
    state_->rtk_config_dialog_->setEpsilonMainAntennaLeverArmApplier([this](
        double x,
        double y,
        double z,
        RtkConfigDialog::EpsilonLeverArmCompletion completion) {
        applyEpsilonMainAntennaLeverArm(x, y, z, std::move(completion));
    });
    {
        const QString epsilonPort = localSerialPortComboValue(state_->epsilon_port_combo_);
        const QString epsilonBaud = state_->epsilon_baud_combo_ ? state_->epsilon_baud_combo_->currentText().trimmed() : QStringLiteral("921600");
        state_->rtk_config_dialog_->setEpsilonMainPortAndBaud(epsilonPort, epsilonBaud);
    }
    {
        QSettings settings = VaporView::applicationConfigSettings();
        settings.beginGroup(QStringLiteral("MainWindow"));
        const QString preferredOutputPort = settings.value("epsilon_rtcm_forward_port").toString().trimmed();
        const QString preferredBaud = settings.value("epsilon_rtcm_forward_baud", "115200").toString().trimmed();
        if (!preferredOutputPort.isEmpty())
        {
            state_->rtk_config_dialog_->setPreferredOutputPortAndBaud(preferredOutputPort, preferredBaud);
        }
    }
    state_->rtk_config_dialog_->setFontScale(state_->font_scale_percent_);
    state_->rtk_config_dialog_->setEnglish(state_->is_english_);
    state_->rtk_config_dialog_->setUiTestMode(isUiTestMode());
}

void MainWindow::onRtkConfigClicked()
{
    syncRtkConfigPageState();
    if (state_->main_page_stack_ && state_->rtk_config_dialog_ && state_->main_page_stack_->indexOf(state_->rtk_config_dialog_) >= 0)
    {
        state_->main_page_stack_->setCurrentWidget(state_->rtk_config_dialog_);
    }
    if (state_->rtk_config_nav_btn_)
    {
        state_->rtk_config_nav_btn_->setChecked(true);
    }
    updateSidebarNavIcons();
    updateCustomTitleBarTexts();
}

void MainWindow::onConfigureEpsilonRtcmPortClicked()
{
    if (isUiTestMode())
    {
        publishUiTestEvent(QStringLiteral("ui_test_epsilon_rtcm_port_config_accepted"),
                           state_->is_english_ ? QStringLiteral("Simulated EPSILON RTCM port configuration accepted")
                                               : QStringLiteral("模拟 EPSILON RTCM 串口配置已接受"),
                           {{QStringLiteral("device"), QStringLiteral("EPSILON")}});
        return;
    }
    if (state_->connection_attempt_in_progress_ || state_->port_detection_in_progress_ || state_->epsilon_reconfigure_in_progress_)
    {
        return;
    }

    if (state_->recording_service_->isActive())
    {
        publishGroundLog(VaporView::LogLevel::Warning,
                         QStringLiteral("device.navigation.command"),
                         QStringLiteral("epsilon_rtcm_config_rejected_recording_active"),
                         QStringLiteral("请先结束记录，再配置 EPSILON RTCM 串口。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("reason_code"), QStringLiteral("INVALID_STATE")},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("epsilon:rtcm_config:recording_active")}});
        return;
    }

    const QString selectText = state_->is_english_ ? "-- Select --" : "未选择";
    const QString epsilonPort = localSerialPortComboValue(state_->epsilon_port_combo_);
    if (epsilonPort.isEmpty() || epsilonPort == selectText)
    {
        publishGroundLog(VaporView::LogLevel::Warning,
                         QStringLiteral("device.navigation.command"),
                         QStringLiteral("epsilon_rtcm_config_rejected_missing_main_port"),
                         QStringLiteral("请先选择 EPSILON 主串口。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("reason_code"), QStringLiteral("MISSING_ENDPOINT")},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("epsilon:rtcm_config:missing_main_port")}});
        return;
    }

    const QString epsilonBaudText = state_->epsilon_baud_combo_ ? state_->epsilon_baud_combo_->currentText().trimmed() : QStringLiteral("921600");
    bool epsilonBaudOk = false;
    const int epsilonBaud = epsilonBaudText.toInt(&epsilonBaudOk);
    if (!epsilonBaudOk || epsilonBaud <= 0)
    {
        publishGroundLog(VaporView::LogLevel::Warning,
                         QStringLiteral("device.navigation.command"),
                         QStringLiteral("epsilon_rtcm_config_rejected_invalid_main_baud"),
                         QStringLiteral("EPSILON 波特率无效。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("reason_code"), QStringLiteral("CONFIG_INVALID")},
                          {QStringLiteral("baud_text"), epsilonBaudText},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("epsilon:rtcm_config:invalid_main_baud")}});
        return;
    }

    QSettings settings = VaporView::applicationConfigSettings();
    settings.beginGroup(QStringLiteral("MainWindow"));
    const QStringList availablePorts = getAvailablePorts();

    QDialog dialog(this);
    dialog.setModal(true);
    dialog.setWindowTitle(state_->is_english_ ? "Configure EPSILON RTCM Port" : "配置 EPSILON RTCM 串口");

    auto *layout = new QVBoxLayout(&dialog);
    auto *hintLabel = new QLabel(
        state_->is_english_
            ? QStringLiteral("This configures EPSILON communication port 2 as an RTCM input port, saves the output-forwarding serial port on this PC, and prepares the RTK dialog to stream RTCM continuously into EPSILON.")
            : QStringLiteral("这个功能会把 EPSILON 的第二通信串口配置为 RTCM 输入口，同时保存本机用于转发 RTCM 的串口与波特率，并为后续 RTK 配置对话框做好预填。"),
        &dialog);
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    auto *formLayout = new QFormLayout();
    formLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addLayout(formLayout);

    auto *mainPortValue = new QLabel(QStringLiteral("%1 @ %2").arg(epsilonPort, epsilonBaudText), &dialog);
    formLayout->addRow(state_->is_english_ ? "EPSILON Main Port:" : "EPSILON 主串口：", mainPortValue);

    auto *deviceRtcmPortValue = new QLabel(state_->is_english_ ? "COMM2 (RTCM)" : "串口2（RTCM）", &dialog);
    formLayout->addRow(state_->is_english_ ? "Device RTCM Port:" : "设备 RTCM 串口：", deviceRtcmPortValue);

    auto *forwardPortCombo = new QComboBox(&dialog);
    const QString savedForwardPort = settings.value("epsilon_rtcm_forward_port").toString().trimmed();
    refreshLocalSerialPortComboOptions(forwardPortCombo, availablePorts, savedForwardPort);
    configureComboPopup(forwardPortCombo);
    formLayout->addRow(state_->is_english_ ? "PC RTCM Forward Port:" : "本机 RTCM 转发串口：", forwardPortCombo);

    auto *forwardBaudCombo = new QComboBox(&dialog);
    forwardBaudCombo->addItems({QStringLiteral("115200"),
                                QStringLiteral("230400"),
                                QStringLiteral("460800"),
                                QStringLiteral("921600")});
    forwardBaudCombo->setCurrentText(settings.value("epsilon_rtcm_forward_baud", "115200").toString());
    configureComboPopup(forwardBaudCombo);
    formLayout->addRow(state_->is_english_ ? "RTCM Port Baud:" : "RTCM 串口波特率：", forwardBaudCombo);

    auto *openRtkConfigCheck = new QCheckBox(
        state_->is_english_ ? "Open RTK Config after success" : "成功后打开 RTK 配置",
        &dialog);
    openRtkConfigCheck->setChecked(true);
    layout->addWidget(openRtkConfigCheck);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    VaporView::installCustomTitleBar(&dialog, false);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const QString forwardPort = localSerialPortComboValue(forwardPortCombo);
    if (forwardPort.isEmpty())
    {
        publishGroundLog(VaporView::LogLevel::Warning,
                         QStringLiteral("device.navigation.command"),
                         QStringLiteral("epsilon_rtcm_config_rejected_missing_forward_port"),
                         QStringLiteral("请选择连接到 EPSILON 第二串口的本机串口。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("reason_code"), QStringLiteral("MISSING_ENDPOINT")},
                          {QStringLiteral("main_port"), epsilonPort},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("epsilon:rtcm_config:missing_forward_port")}});
        return;
    }

    if (serialPortNamesReferToSamePort(forwardPort, epsilonPort))
    {
        const QString message = state_->is_english_
            ? QStringLiteral("The RTCM forwarding port must differ from the EPSILON main port. The main port reads real GNSS/FDILink data for NTRIP GGA; connect another PC serial port to EPSILON COMM2 for RTCM input.")
            : QStringLiteral("RTCM 转发串口不能与 EPSILON 主串口相同。主串口用于读取真实 GNSS/FDILink 数据并生成 NTRIP GGA；请用另一条本机串口连接 EPSILON COMM2 写入 RTCM。");
        publishGroundLog(VaporView::LogLevel::Warning,
                         QStringLiteral("device.navigation.command"),
                         QStringLiteral("epsilon_rtcm_config_rejected_port_conflict"),
                         QStringLiteral("RTCM 转发串口不能与 EPSILON 主串口相同。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("reason_code"), QStringLiteral("CONFIG_INVALID")},
                          {QStringLiteral("main_port"), epsilonPort},
                          {QStringLiteral("forward_port"), forwardPort},
                          {QStringLiteral("details"), message},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("epsilon:rtcm_config:port_conflict")}});
        QMessageBox::warning(this,
                             state_->is_english_ ? QStringLiteral("Serial Port Conflict") : QStringLiteral("串口冲突"),
                             message);
        return;
    }

    bool forwardBaudOk = false;
    const QString forwardBaudText = forwardBaudCombo->currentText().trimmed();
    const int forwardBaud = forwardBaudText.toInt(&forwardBaudOk);
    if (!forwardBaudOk || forwardBaud <= 0)
    {
        publishGroundLog(VaporView::LogLevel::Warning,
                         QStringLiteral("device.navigation.command"),
                         QStringLiteral("epsilon_rtcm_config_rejected_invalid_forward_baud"),
                         QStringLiteral("RTCM 转发波特率无效。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("reason_code"), QStringLiteral("CONFIG_INVALID")},
                          {QStringLiteral("baud_text"), forwardBaudText},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("epsilon:rtcm_config:invalid_forward_baud")}});
        return;
    }

    if (state_->epsilon_reconfigure_thread_.joinable())
    {
        state_->epsilon_reconfigure_thread_.join();
    }

    const std::shared_ptr<VaporView::EpsilonCollector> liveCollector = snapshotCollectors().epsilon;
    const bool shouldRestartCollector = liveCollector && liveCollector->isRunning();
    const bool shouldOpenRtkDialog = openRtkConfigCheck->isChecked();
    const bool english = state_->is_english_;

    state_->epsilon_reconfigure_in_progress_ = true;
    updateConnectionStatus(state_->is_connected_);
    publishGroundLog(VaporView::LogLevel::Info,
                     QStringLiteral("device.navigation.command"),
                     QStringLiteral("epsilon_rtcm_config_started"),
                     QStringLiteral("正在把 EPSILON 第二通信串口配置为 RTCM。"),
                     {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                      {QStringLiteral("main_port"), epsilonPort},
                      {QStringLiteral("main_baud"), epsilonBaud},
                      {QStringLiteral("forward_port"), forwardPort},
                      {QStringLiteral("forward_baud"), forwardBaud},
                      {QStringLiteral("ui_visibility"), QStringLiteral("details")}});

    state_->epsilon_reconfigure_thread_ = std::thread([this,
                                               english,
                                               epsilonPort,
                                               epsilonBaud,
                                               epsilonBaudText,
                                               forwardPort,
                                               forwardBaud,
                                               forwardBaudText,
                                               liveCollector,
                                               shouldRestartCollector,
                                               shouldOpenRtkDialog]() {
        auto postLog = [this](VaporView::Ground::EpsilonConfigurationLogEntry entry) {
            QMetaObject::invokeMethod(this, [this, entry = std::move(entry)]() mutable {
                publishGroundLog(entry.level,
                                 entry.category,
                                 entry.event,
                                 entry.message,
                                 std::move(entry.fields));
            }, Qt::QueuedConnection);
        };
        auto finishOnUi = [this](bool openRtkDialog) {
            QMetaObject::invokeMethod(this, [this, openRtkDialog]() {
                state_->epsilon_reconfigure_in_progress_ = false;
                updateConnectionStatus(anyCollectorRunning());
                if (openRtkDialog)
                {
                    onRtkConfigClicked();
                }
            }, Qt::QueuedConnection);
        };

        VaporView::Ground::EpsilonDeviceOperation operation;
        operation.port = epsilonPort;
        operation.baud = epsilonBaud;
        operation.baud_text = epsilonBaudText;
        operation.english = english;
        operation.live_collector = liveCollector;
        operation.restart_live_stream = shouldRestartCollector;

        const VaporView::Ground::EpsilonConfigurationResult result =
            VaporView::Ground::EpsilonConfigurationService::configureRtcmPort(
                operation,
                forwardPort,
                forwardBaud,
                forwardBaudText,
                postLog);
        finishOnUi(result.succeeded() && shouldOpenRtkDialog);
    });
}

void MainWindow::onConfigureEpsilonPacketRatesClicked()
{
    if (state_->connection_attempt_in_progress_ || state_->port_detection_in_progress_ || state_->epsilon_reconfigure_in_progress_)
    {
        return;
    }

    const QString epsilonRateText = state_->epsilon_rate_combo_ ? state_->epsilon_rate_combo_->currentText() : QStringLiteral("100");
    const int groupedRateHz = effectiveRateOrDefault(epsilonRateText, kDefaultEpsilonSampleRateHz, 200);
    QSettings settings = VaporView::applicationConfigSettings();
    settings.beginGroup(QStringLiteral("MainWindow"));
    const bool customEnabled = settings.value("epsilon_custom_packet_rates_enabled", false).toBool();
    const std::map<uint8_t, int> defaultRates = defaultEpsilonPacketRates();
    const std::map<uint8_t, int> groupedRates = groupedEpsilonPacketRates(groupedRateHz);
    const std::map<uint8_t, int> initialRates = customEnabled
        ? loadCustomEpsilonPacketRates(settings, groupedRateHz)
        : groupedRates;

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("epsilonPacketRatesDialog"));
    dialog.setModal(true);
    dialog.setWindowTitle(state_->is_english_ ? "EPSILON Packet Rates" : "EPSILON 包频率设置");
    dialog.setStyleSheet(applyAppThemeTokens(QStringLiteral(
        "QDialog#epsilonPacketRatesDialog,"
        "QDialog#epsilonPacketRatesDialog QWidget#epsilonPacketRatesContent,"
        "QDialog#epsilonPacketRatesDialog QWidget#epsilonPacketRatesCell { background-color: @vv-surface; }"
        "QDialog#epsilonPacketRatesDialog QLabel,"
        "QDialog#epsilonPacketRatesDialog QCheckBox { background-color: transparent; }"),
        state_->dark_theme_enabled_));

    auto *layout = new QVBoxLayout(&dialog);
    layout->setSpacing(10);

    auto *hintLabel = new QLabel(
        state_->is_english_
            ? QStringLiteral("Configured from the local EPSILON ground-station profile. The recommended default profile prioritizes stable time and 3D navigation output. Rate limits are reflected by each selector's available options. If any packet differs from the grouped profile, the custom profile will be enabled automatically when you save.")
            : QStringLiteral("配置范围来自本地 EPSILON 官方地面站配置。推荐默认配置优先保证稳定的时间与三维导航输出。频率上限由各选择框的可选项体现。只要任一数据包偏离分组模式，保存时就会自动启用自定义配置。"),
        &dialog);
    hintLabel->setWordWrap(true);
    hintLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    layout->addWidget(hintLabel);

    auto *enableCustomCheck = new QCheckBox(
        state_->is_english_
            ? QStringLiteral("Use custom EPSILON packet rates for future connect/reconfigure operations")
            : QStringLiteral("后续连接和重配时使用这组自定义 EPSILON 包频率"),
        &dialog);
    enableCustomCheck->setChecked(customEnabled);
    layout->addWidget(enableCustomCheck);

    auto *formWidget = new QWidget(&dialog);
    formWidget->setObjectName(QStringLiteral("epsilonPacketRatesContent"));
    formWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *formLayout = new QGridLayout(formWidget);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setHorizontalSpacing(16);
    formLayout->setVerticalSpacing(10);
    formLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(formWidget, 0, Qt::AlignLeft);

    auto packetRateText = [this](int rateHz) {
        return epsilonPacketRateDisplayText(rateHz, state_->is_english_);
    };
    int packetRateComboWidth = 0;
    {
        QComboBox comboProbe(&dialog);
        const QFontMetrics comboMetrics(comboProbe.font());
        for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
        {
            for (int rateHz : option.supported_rates_hz)
            {
                packetRateComboWidth = std::max(packetRateComboWidth,
                                               comboMetrics.horizontalAdvance(packetRateText(rateHz)));
            }
        }
    }
    packetRateComboWidth = std::max(128, packetRateComboWidth + 64);
    const QFontMetrics rowLabelMetrics(hintLabel->font());

    constexpr int kPacketRateDialogColumnCount = 3;
    std::map<uint8_t, QComboBox*> packetCombos;
    int packetIndex = 0;
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        const QString rowLabelText = epsilonPacketDialogRowLabel(option, state_->is_english_);
        const int cellWidth = std::max(packetRateComboWidth, rowLabelMetrics.horizontalAdvance(rowLabelText) + 8);
        auto *cell = new QWidget(formWidget);
        cell->setObjectName(QStringLiteral("epsilonPacketRatesCell"));
        cell->setFixedWidth(cellWidth);
        auto *cellLayout = new QVBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(4);

        auto *label = new QLabel(rowLabelText, cell);
        label->setWordWrap(false);
        label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        cellLayout->addWidget(label);

        auto *combo = new QComboBox(cell);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        combo->setFixedWidth(packetRateComboWidth);
        combo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        for (int rateHz : option.supported_rates_hz)
        {
            combo->addItem(packetRateText(rateHz), rateHz);
        }
        configureComboPopup(combo);
        const int initialRateHz = initialRates.count(option.packet_id) ? initialRates.at(option.packet_id) : groupedRates.at(option.packet_id);
        const int comboIndex = combo->findData(initialRateHz);
        if (comboIndex >= 0)
        {
            combo->setCurrentIndex(comboIndex);
        }
        packetCombos[option.packet_id] = combo;
        cellLayout->addWidget(combo, 0, Qt::AlignLeft);
        const int row = packetIndex / kPacketRateDialogColumnCount;
        const int column = packetIndex % kPacketRateDialogColumnCount;
        formLayout->addWidget(cell, row, column, Qt::AlignTop);
        ++packetIndex;
    }

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QPushButton *recommendedDefaultsButton = buttonBox->addButton(
        state_->is_english_ ? QStringLiteral("Use Recommended Defaults") : QStringLiteral("恢复推荐默认值"),
        QDialogButtonBox::ResetRole);
    connect(recommendedDefaultsButton, &QPushButton::clicked, &dialog, [&packetCombos, defaultRates, enableCustomCheck]() {
        enableCustomCheck->setChecked(true);
        for (const auto& entry : packetCombos)
        {
            const auto it = defaultRates.find(entry.first);
            if (it == defaultRates.end())
            {
                continue;
            }
            QComboBox *combo = entry.second;
            const int index = combo ? combo->findData(it->second) : -1;
            if (combo && index >= 0)
            {
                combo->setCurrentIndex(index);
            }
        }
    });
    QPushButton *groupedDefaultsButton = buttonBox->addButton(
        state_->is_english_ ? QStringLiteral("Use Grouped Profile") : QStringLiteral("切换到分组模式"),
        QDialogButtonBox::ActionRole);
    connect(groupedDefaultsButton, &QPushButton::clicked, &dialog, [&packetCombos, groupedRates, enableCustomCheck]() {
        enableCustomCheck->setChecked(false);
        for (const auto& entry : packetCombos)
        {
            const auto it = groupedRates.find(entry.first);
            if (it == groupedRates.end())
            {
                continue;
            }
            QComboBox *combo = entry.second;
            const int index = combo ? combo->findData(it->second) : -1;
            if (combo && index >= 0)
            {
                combo->setCurrentIndex(index);
            }
        }
    });
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    VaporView::installCustomTitleBar(&dialog, false);
    if (QLayout *dialogLayout = dialog.layout())
    {
        dialogLayout->invalidate();
    }
    const QSize targetMinimumSize(state_->is_english_ ? QSize(700, 360) : QSize(720, 360));
    const QSize preferredSize = dialog.sizeHint().expandedTo(targetMinimumSize);
    const QSize targetSize = VaporView::defaultWindowSizeWithinScreenFraction(
        this,
        preferredSize,
        0.85,
        targetMinimumSize);
    dialog.setMinimumSize(targetSize);
    dialog.resize(targetSize);
    VaporView::centerWindowOnScreen(&dialog, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    std::map<uint8_t, int> savedPacketRates;
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        QComboBox *combo = packetCombos[option.packet_id];
        const int rateHz = combo ? combo->currentData().toInt() : groupedRates.at(option.packet_id);
        savedPacketRates[option.packet_id] = rateHz;
    }
    const QString epsilonBaudText = state_->epsilon_baud_combo_
        ? state_->epsilon_baud_combo_->currentText().trimmed()
        : QStringLiteral("921600");
    if (!validateEpsilonPacketBandwidth(savedPacketRates, epsilonBaudText, true))
    {
        return;
    }
    for (const auto& entry : savedPacketRates)
    {
        VaporView::setPersistentSetting(settings, epsilonPacketRateSettingsKey(entry.first), entry.second);
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
    const bool effectiveCustomEnabled = enableCustomCheck->isChecked() || hasCustomOverrides;
    VaporView::setPersistentSetting(settings, QStringLiteral("epsilon_custom_packet_rates_enabled"), effectiveCustomEnabled);
    VaporView::setPersistentSetting(settings, QStringLiteral("epsilon_custom_packet_rates_user_saved"), effectiveCustomEnabled);
    VaporView::removePersistentSetting(settings, QStringLiteral("epsilon_last_config_signature"));
    VaporView::removePersistentSetting(settings, QStringLiteral("epsilon_last_config_apply_version"));
    const QString packetRateSummary = epsilonPacketRatesSummary(savedPacketRates);

    if (hasCustomOverrides && !enableCustomCheck->isChecked())
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("configuration.apply"),
                         QStringLiteral("epsilon_packet_profile_custom_enabled"),
                         QStringLiteral("检测到包频率已偏离分组模式，已自动启用自定义包频率配置。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("packet_rate_summary"), packetRateSummary},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }

    if (effectiveCustomEnabled)
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("configuration.apply"),
                         QStringLiteral("epsilon_packet_profile_saved"),
                         (savedPacketRates == defaultRates)
                             ? QStringLiteral("已保存 EPSILON 推荐默认包频率配置。")
                             : QStringLiteral("已保存 EPSILON 自定义包频率配置。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("packet_rate_profile"), savedPacketRates == defaultRates
                              ? QStringLiteral("recommended_default")
                              : QStringLiteral("custom")},
                          {QStringLiteral("packet_rate_summary"), packetRateSummary},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
    else
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("configuration.apply"),
                         QStringLiteral("epsilon_packet_profile_disabled"),
                         QStringLiteral("已关闭 EPSILON 自定义包频率，后续将使用分组配置。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("grouped_rate_hz"), groupedRateHz},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }

    const QString selectText = state_->is_english_ ? "-- Select --" : "未选择";
    const QString epsilonPort = localSerialPortComboValue(state_->epsilon_port_combo_);
    if (!state_->recording_service_->isActive() &&
        !epsilonPort.isEmpty() &&
        epsilonPort != selectText &&
        !isRateUnspecified(epsilonRateText))
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("configuration.apply"),
                         QStringLiteral("epsilon_packet_profile_apply_requested"),
                         QStringLiteral("正在应用刚保存的 EPSILON 包频率配置。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("port"), epsilonPort},
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
    syncDeviceConfigEpsilonPanelFromSettings();
}

void MainWindow::onReconfigureEpsilonClicked()
{
    if (isUiTestMode())
    {
        publishUiTestEvent(QStringLiteral("ui_test_epsilon_output_reconfigure_completed"),
                           state_->is_english_ ? QStringLiteral("Simulated EPSILON output reconfiguration completed")
                                               : QStringLiteral("模拟 EPSILON 输出重配置已完成"),
                           {{QStringLiteral("device"), QStringLiteral("EPSILON")}});
        return;
    }
    if (state_->connection_attempt_in_progress_ || state_->port_detection_in_progress_ || state_->epsilon_reconfigure_in_progress_)
    {
        return;
    }

    if (state_->recording_service_->isActive())
    {
        publishGroundLog(VaporView::LogLevel::Warning,
                         QStringLiteral("device.navigation.command"),
                         QStringLiteral("epsilon_output_reconfigure_rejected_recording_active"),
                         QStringLiteral("请先结束记录，再重新配置 EPSILON 输出。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("reason_code"), QStringLiteral("INVALID_STATE")},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("epsilon:output_reconfigure:recording_active")}});
        return;
    }

    const QString selectText = state_->is_english_ ? "-- Select --" : "未选择";
    const QString epsilonPort = localSerialPortComboValue(state_->epsilon_port_combo_);
    if (epsilonPort.isEmpty() || epsilonPort == selectText)
    {
        publishGroundLog(VaporView::LogLevel::Warning,
                         QStringLiteral("device.navigation.command"),
                         QStringLiteral("epsilon_output_reconfigure_rejected_missing_port"),
                         QStringLiteral("请先选择 EPSILON 串口。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("reason_code"), QStringLiteral("MISSING_ENDPOINT")},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("epsilon:output_reconfigure:missing_port")}});
        return;
    }

    const QString epsilonBaudText = state_->epsilon_baud_combo_ ? state_->epsilon_baud_combo_->currentText().trimmed() : QStringLiteral("921600");
    bool baudOk = false;
    const int epsilonBaud = epsilonBaudText.toInt(&baudOk);
    if (!baudOk || epsilonBaud <= 0)
    {
        publishGroundLog(VaporView::LogLevel::Warning,
                         QStringLiteral("device.navigation.command"),
                         QStringLiteral("epsilon_output_reconfigure_rejected_invalid_baud"),
                         QStringLiteral("EPSILON 波特率无效。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("reason_code"), QStringLiteral("CONFIG_INVALID")},
                          {QStringLiteral("baud_text"), epsilonBaudText},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("epsilon:output_reconfigure:invalid_baud")}});
        return;
    }

    const QString epsilonRateText = state_->epsilon_rate_combo_ ? state_->epsilon_rate_combo_->currentText() : QStringLiteral("100");
    if (isRateUnspecified(epsilonRateText))
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.navigation.command"),
                         QStringLiteral("epsilon_output_reconfigure_skipped_rate_unspecified"),
                         QStringLiteral("EPSILON 频率为“不设定”，已跳过输出频率下发。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("reason_code"), QStringLiteral("COMMAND_NOT_SUPPORTED")},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
        return;
    }

    const int epsilonRate = effectiveRateOrDefault(epsilonRateText, kDefaultEpsilonSampleRateHz, 200);
    state_->epsilon_sample_rate_ = epsilonRate;
    QSettings settings = VaporView::applicationConfigSettings();
    settings.beginGroup(QStringLiteral("MainWindow"));
    bool usingCustomPacketProfile = false;
    const std::map<uint8_t, int> desiredPacketRates = effectiveEpsilonPacketRates(settings, epsilonRate, &usingCustomPacketProfile);
    if (!validateEpsilonPacketBandwidth(desiredPacketRates, epsilonBaudText, true))
    {
        return;
    }
    const int epsilonCallbackRate = epsilonPacketCallbackRate(desiredPacketRates, epsilonRate);
    const QString desiredPacketRateSignature = epsilonPacketRatesSignature(desiredPacketRates);
    const QString desiredPacketRateSummary = epsilonPacketRatesSummary(desiredPacketRates);

    if (state_->epsilon_reconfigure_thread_.joinable())
    {
        state_->epsilon_reconfigure_thread_.join();
    }

    const std::shared_ptr<VaporView::EpsilonCollector> liveCollector = snapshotCollectors().epsilon;
    const bool shouldRestartCollector = liveCollector && liveCollector->isRunning();
    const bool english = state_->is_english_;

    state_->epsilon_reconfigure_in_progress_ = true;
    updateConnectionStatus(state_->is_connected_);
    publishGroundLog(VaporView::LogLevel::Info,
                     QStringLiteral("device.navigation.command"),
                     QStringLiteral("epsilon_output_reconfigure_started"),
                     QStringLiteral("开始手动重配 EPSILON 输出。"),
                     {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                      {QStringLiteral("port"), epsilonPort},
                      {QStringLiteral("baud"), epsilonBaud},
                      {QStringLiteral("packet_rate_profile"), usingCustomPacketProfile ? QStringLiteral("custom")
                                                                                       : QStringLiteral("grouped")},
                      {QStringLiteral("packet_rate_summary"), desiredPacketRateSummary},
                      {QStringLiteral("ui_visibility"), QStringLiteral("details")}});

    state_->epsilon_reconfigure_thread_ = std::thread([this,
                                               english,
                                               epsilonPort,
                                               epsilonBaud,
                                               epsilonBaudText,
                                               epsilonRate,
                                               epsilonCallbackRate,
                                               desiredPacketRates,
                                               desiredPacketRateSignature,
                                               liveCollector,
                                               shouldRestartCollector]() {
        auto postLog = [this](VaporView::Ground::EpsilonConfigurationLogEntry entry) {
            QMetaObject::invokeMethod(this, [this, entry = std::move(entry)]() mutable {
                publishGroundLog(entry.level,
                                 entry.category,
                                 entry.event,
                                 entry.message,
                                 std::move(entry.fields));
            }, Qt::QueuedConnection);
        };
        auto finishOnUi = [this]() {
            QMetaObject::invokeMethod(this, [this]() {
                state_->epsilon_reconfigure_in_progress_ = false;
                updateConnectionStatus(anyCollectorRunning());
            }, Qt::QueuedConnection);
        };

        VaporView::Ground::EpsilonDeviceOperation operation;
        operation.port = epsilonPort;
        operation.baud = epsilonBaud;
        operation.baud_text = epsilonBaudText;
        operation.english = english;
        operation.live_collector = liveCollector;
        operation.restart_live_stream = shouldRestartCollector;

        VaporView::Ground::EpsilonConfigurationService::configurePacketRates(
            operation,
            epsilonRate,
            epsilonCallbackRate,
            desiredPacketRates,
            desiredPacketRateSignature,
            postLog);
        finishOnUi();
    });
}
