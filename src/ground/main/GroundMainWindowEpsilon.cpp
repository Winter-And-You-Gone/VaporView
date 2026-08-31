#include "ground/main/GroundMainWindowImplementation.h"
#include "ground/devices/DeviceRatePolicy.h"

#include <QSettings>

#include <algorithm>
#include <map>
#include <memory>
namespace
{

constexpr int kUiTestEpsilonReconfigureStepDelayMs = 90;

QString epsilonOperationName(VaporView::Ground::Devices::EpsilonOperation operation)
{
    using Operation = VaporView::Ground::Devices::EpsilonOperation;
    switch (operation)
    {
    case Operation::ConfigurePacketRates: return QStringLiteral("packet_profile");
    case Operation::ConfigureMainAntennaLeverArm: return QStringLiteral("main_antenna_lever_arm");
    case Operation::ConfigureRtcmInput: return QStringLiteral("rtcm_input");
    }
    return QStringLiteral("unknown");
}

QString epsilonOutcomeName(VaporView::Ground::Devices::EpsilonOperationOutcome outcome)
{
    using Outcome = VaporView::Ground::Devices::EpsilonOperationOutcome;
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

} // namespace
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

void MainWindow::onEpsilonSessionAvailabilityChanged(bool available, const QString& reason)
{
    Q_UNUSED(reason);
    Q_UNUSED(available);
    updateDeviceConfigState();
}

void MainWindow::onEpsilonSessionOperationStarted(
    quint64 requestId, VaporView::Ground::Devices::EpsilonOperation operation)
{
    Q_UNUSED(requestId);
    if (operation == VaporView::Ground::Devices::EpsilonOperation::ConfigurePacketRates)
    {
        startEpsilonReconfigureProgress();
    }
    state_->epsilon_reconfigure_in_progress_ = true;
    updateConnectionStatus(anyCollectorRunning());
    updateDeviceConfigState();
}

void MainWindow::onEpsilonSessionOperationFinished(
    const VaporView::Ground::Devices::EpsilonSessionResult& result)
{
    if (result.operation == VaporView::Ground::Devices::EpsilonOperation::ConfigurePacketRates)
    {
        stopEpsilonReconfigureProgress();
    }
    state_->epsilon_reconfigure_in_progress_ = false;
    updateConnectionStatus(anyCollectorRunning());

    const QString operationName = epsilonOperationName(result.operation);
    const QString outcomeName = epsilonOutcomeName(result.outcome);
    QString statusText = result.message;
    if (statusText.isEmpty())
    {
        statusText = result.success()
            ? (state_->is_english_ ? QStringLiteral("EPSILON operation completed.")
                                   : QStringLiteral("EPSILON 操作已完成。"))
            : (state_->is_english_ ? QStringLiteral("EPSILON operation failed.")
                                   : QStringLiteral("EPSILON 操作失败。"));
    }

    QVariantMap fields{{QStringLiteral("device"), QStringLiteral("EPSILON")},
                       {QStringLiteral("request_id"), result.request_id},
                       {QStringLiteral("operation"), operationName},
                       {QStringLiteral("execution_path"), isRemoteSkyMode()
                            ? QStringLiteral("remote_sky")
                            : QStringLiteral("local")},
                       {QStringLiteral("outcome"), outcomeName},
                       {QStringLiteral("command_error_code"),
                        commandErrorCodeIdentifier(result.error_code)},
                       {QStringLiteral("ui_visibility"), result.success()
                            ? QStringLiteral("details") : QStringLiteral("attention")},
                       {QStringLiteral("details"), statusText}};
    if (!result.success())
    {
        fields.insert(QStringLiteral("error_code"), QStringLiteral("EPSILON_OPERATION_FAILED"));
        fields.insert(QStringLiteral("ui_dedupe_key"),
                      QStringLiteral("epsilon:%1:%2").arg(operationName, outcomeName));
    }
    publishGroundLog(result.success() ? VaporView::LogLevel::Info : VaporView::LogLevel::Error,
                     QStringLiteral("device.navigation.command"),
                     result.success() ? QStringLiteral("epsilon_operation_completed")
                                      : QStringLiteral("epsilon_operation_failed"),
                     result.success() ? QStringLiteral("EPSILON 设备操作已完成。")
                                      : QStringLiteral("EPSILON 设备操作失败。"),
                     fields);

    updateDeviceConfigState();
}
void MainWindow::applyEpsilonMainAntennaLeverArm(
    double xM,
    double yM,
    double zM,
    std::function<void(bool, const QString&)> completion)
{
    auto completionHolder =
        std::make_shared<std::function<void(bool, const QString&)>>(std::move(completion));
    auto fail = [completionHolder](const QString& message) {
        if (*completionHolder)
        {
            (*completionHolder)(false, message);
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
        if (*completionHolder)
        {
            (*completionHolder)(true, QString());
        }
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

    VaporView::EpsilonMainAntennaLeverArmOperation operation;
    operation.x_m = xM;
    operation.y_m = yM;
    operation.z_m = zM;
    VaporView::Ground::EpsilonDeviceOperation localDeviceOperation;

    if (isRemoteSkyMode())
    {
        if (!state_->epsilon_device_session_ ||
            !state_->epsilon_device_session_->operationsAvailable())
        {
            fail(state_->is_english_
                ? QStringLiteral("Remote Sky EPSILON is not available.")
                : QStringLiteral("天空端 EPSILON 当前不可用。"));
            return;
        }
    }
    else
    {
        const QString epsilonPort = state_->local_device_config_.epsilon.port;
        if (epsilonPort.isEmpty() || epsilonPort.startsWith(QStringLiteral("--")))
        {
            fail(state_->is_english_
                ? QStringLiteral("Select the EPSILON main serial port first.")
                : QStringLiteral("请先选择 EPSILON 主串口。"));
            return;
        }

        const QString epsilonBaudText = state_->local_device_config_.epsilon.baudText;
        bool baudOk = false;
        const int epsilonBaud = epsilonBaudText.toInt(&baudOk);
        if (!baudOk || epsilonBaud <= 0)
        {
            fail(QString(state_->is_english_ ? "Invalid EPSILON baud rate: %1" : "EPSILON 波特率无效: %1").arg(epsilonBaudText));
            return;
        }

        if (!state_->epsilon_device_session_ ||
            !state_->epsilon_device_session_->operationsAvailable())
        {
            fail(state_->is_english_
                ? QStringLiteral("Local EPSILON is not available.")
                : QStringLiteral("本地 EPSILON 当前不可用。"));
            return;
        }

        const bool english = state_->is_english_;
        const QString values = QStringLiteral("X=%1 m, Y=%2 m, Z=%3 m")
            .arg(QString::number(xM, 'f', 4),
                 QString::number(yM, 'f', 4),
                 QString::number(zM, 'f', 4));
        const std::shared_ptr<VaporView::EpsilonCollector> liveCollector = snapshotCollectors().epsilon;
        const bool shouldRestartCollector = liveCollector && liveCollector->isRunning();

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

        localDeviceOperation.port = epsilonPort;
        localDeviceOperation.baud = epsilonBaud;
        localDeviceOperation.baud_text = epsilonBaudText;
        localDeviceOperation.english = english;
        localDeviceOperation.live_collector = liveCollector;
        localDeviceOperation.restart_live_stream = shouldRestartCollector;
    }

    auto requestId = std::make_shared<quint64>(0);
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(
        state_->epsilon_device_session_.get(),
        &VaporView::Ground::Devices::EpsilonDeviceSession::operationFinished,
        this,
        [requestId, connection, completionHolder](
            const VaporView::Ground::Devices::EpsilonSessionResult& result) mutable {
            if (result.operation !=
                VaporView::Ground::Devices::EpsilonOperation::ConfigureMainAntennaLeverArm)
            {
                return;
            }
            if (*requestId != 0 && result.request_id != *requestId)
            {
                return;
            }
            QObject::disconnect(*connection);
            if (*completionHolder)
            {
                (*completionHolder)(result.success(), result.message);
            }
        });
    *requestId = state_->epsilon_device_session_->configureMainAntennaLeverArm(
        operation, localDeviceOperation);
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
    if (isRemoteSkyMode())
    {
        const QString epsilonPort = state_->remote_sky_config_.epsilon.port;
        const QString epsilonBaud = QString::number(state_->remote_sky_config_.epsilon.baud_rate);
        state_->rtk_config_dialog_->setEpsilonMainPortAndBaud(epsilonPort, epsilonBaud);
        const QString preferredOutputPort = state_->remote_sky_config_.epsilon_rtcm.forward_port.trimmed();
        const QString preferredBaud = QString::number(
            state_->remote_sky_config_.epsilon_rtcm.baud_rate > 0
                ? state_->remote_sky_config_.epsilon_rtcm.baud_rate
                : 115200);
        if (!preferredOutputPort.isEmpty())
        {
            state_->rtk_config_dialog_->setPreferredOutputPortAndBaud(preferredOutputPort, preferredBaud);
        }
        state_->rtk_config_dialog_->setRtcmCorrectionSink(
            [controller = state_->remote_sky_controller_.get()](const QByteArray& payload) {
                return controller && controller->sendRtcmCorrectionData(payload);
            },
            preferredOutputPort.isEmpty()
                ? QStringLiteral("Remote Sky")
                : preferredOutputPort);
    }
    else
    {
        const QString epsilonPort = state_->local_device_config_.epsilon.port;
        const QString epsilonBaud = state_->local_device_config_.epsilon.baudText;
        state_->rtk_config_dialog_->setEpsilonMainPortAndBaud(epsilonPort, epsilonBaud);
        state_->rtk_config_dialog_->setRtcmCorrectionSink({}, QString());
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
    if (state_->main_page_stack_ &&
        state_->combination_navigation_page_ &&
        state_->main_page_stack_->indexOf(state_->combination_navigation_page_) >= 0)
    {
        state_->main_page_stack_->setCurrentWidget(state_->combination_navigation_page_);
    }
    if (state_->combination_navigation_page_)
    {
        state_->combination_navigation_page_->showDifferentialPage();
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

    if (isRemoteSkyMode())
    {
        if (!state_->epsilon_device_session_ ||
            !state_->epsilon_device_session_->operationsAvailable())
        {
            publishGroundLog(VaporView::LogLevel::Warning,
                             QStringLiteral("device.navigation.command"),
                             QStringLiteral("epsilon_rtcm_config_rejected_dependency_unavailable"),
                             QStringLiteral("天空端 EPSILON 当前不可用，无法配置 RTCM。"),
                             {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                              {QStringLiteral("execution_path"), QStringLiteral("remote_sky")},
                              {QStringLiteral("reason_code"), QStringLiteral("DEPENDENCY_UNAVAILABLE")},
                              {QStringLiteral("ui_dedupe_key"), QStringLiteral("epsilon:remote_rtcm_config:not_available")}});
            return;
        }

        int deviceRtcmPortIndex = state_->epsilon_config_panel_
            ? state_->epsilon_config_panel_->rtcmDevicePortIndex()
            : state_->remote_sky_config_.epsilon_rtcm.device_port_index;
        if (deviceRtcmPortIndex < 2 || deviceRtcmPortIndex > 5)
        {
            deviceRtcmPortIndex = 2;
        }

        QDialog dialog(this);
        dialog.setModal(true);
        dialog.setWindowTitle(state_->is_english_
            ? "Configure Remote EPSILON RTCM"
            : "配置天空端 EPSILON RTCM");
        auto *layout = new QVBoxLayout(&dialog);
        auto *hintLabel = new QLabel(
            state_->is_english_
                ? QStringLiteral("This configures the Sky-side EPSILON communication port and the Sky serial port that receives RTCM corrections. Enter the Sky host serial path manually.")
                : QStringLiteral("这里配置天空端 EPSILON 通信串口，以及天空端用于接收 RTCM 差分数据的串口。请手工输入天空端主机串口路径。"),
            &dialog);
        hintLabel->setWordWrap(true);
        layout->addWidget(hintLabel);

        auto *formLayout = new QFormLayout();
        formLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        layout->addLayout(formLayout);

        auto *mainPortValue = new QLabel(
            QStringLiteral("%1 @ %2").arg(state_->remote_sky_config_.epsilon.port,
                                           QString::number(state_->remote_sky_config_.epsilon.baud_rate)),
            &dialog);
        formLayout->addRow(state_->is_english_ ? "Sky EPSILON Main Port:" : "天空端 EPSILON 主串口：",
                           mainPortValue);

        auto *deviceRtcmPortValue = new QLabel(
            state_->is_english_
                ? QStringLiteral("COMM%1 (selected on card)").arg(deviceRtcmPortIndex)
                : QStringLiteral("串口%1（卡片内选择）").arg(deviceRtcmPortIndex),
            &dialog);
        formLayout->addRow(state_->is_english_ ? "EPSILON RTCM Input Port:" : "EPSILON RTCM 输入口：",
                           deviceRtcmPortValue);

        auto *forwardPortCombo = new QComboBox(&dialog);
        forwardPortCombo->setEditable(true);
        forwardPortCombo->setInsertPolicy(QComboBox::NoInsert);
        const QString savedForwardPort = state_->remote_sky_config_.epsilon_rtcm.forward_port.trimmed();
        if (!savedForwardPort.isEmpty())
        {
            forwardPortCombo->addItem(savedForwardPort);
            forwardPortCombo->setCurrentText(savedForwardPort);
        }
        else
        {
            forwardPortCombo->setCurrentText(QStringLiteral("/dev/ttyRTCM"));
        }
        configureComboPopup(forwardPortCombo);
        formLayout->addRow(state_->is_english_ ? "Sky RTCM Forward Port:" : "天空端 RTCM 转发串口：",
                           forwardPortCombo);

        auto *forwardBaudCombo = new QComboBox(&dialog);
        forwardBaudCombo->addItems({QStringLiteral("115200"),
                                    QStringLiteral("230400"),
                                    QStringLiteral("460800"),
                                    QStringLiteral("921600")});
        forwardBaudCombo->setCurrentText(QString::number(
            state_->remote_sky_config_.epsilon_rtcm.baud_rate > 0
                ? state_->remote_sky_config_.epsilon_rtcm.baud_rate
                : 115200));
        configureComboPopup(forwardBaudCombo);
        formLayout->addRow(state_->is_english_ ? "RTCM Port Baud:" : "RTCM 串口波特率：",
                           forwardBaudCombo);

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

        const QString forwardPort = forwardPortCombo->currentText().trimmed();
        bool forwardBaudOk = false;
        const int forwardBaud = forwardBaudCombo->currentText().trimmed().toInt(&forwardBaudOk);
        if (forwardPort.isEmpty() || !forwardBaudOk || forwardBaud <= 0)
        {
            publishGroundLog(VaporView::LogLevel::Warning,
                             QStringLiteral("device.navigation.command"),
                             QStringLiteral("epsilon_rtcm_config_rejected_invalid_remote_forward"),
                             QStringLiteral("天空端 RTCM 转发串口或波特率无效。"),
                             {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                              {QStringLiteral("execution_path"), QStringLiteral("remote_sky")},
                              {QStringLiteral("reason_code"), QStringLiteral("CONFIG_INVALID")},
                              {QStringLiteral("ui_dedupe_key"), QStringLiteral("epsilon:remote_rtcm_config:invalid_forward")}});
            return;
        }

        VaporView::EpsilonRtcmInputOperation operation;
        operation.device_port_index = deviceRtcmPortIndex;
        operation.forward_port = forwardPort;
        operation.forward_baud = forwardBaud;
        const bool shouldOpenRtkDialog = openRtkConfigCheck->isChecked();
        auto requestId = std::make_shared<quint64>(0);
        auto connection = std::make_shared<QMetaObject::Connection>();
        *connection = connect(
            state_->epsilon_device_session_.get(),
            &VaporView::Ground::Devices::EpsilonDeviceSession::operationFinished,
            this,
            [this, requestId, connection, shouldOpenRtkDialog](
                const VaporView::Ground::Devices::EpsilonSessionResult& result) mutable {
                if (result.operation != VaporView::Ground::Devices::EpsilonOperation::ConfigureRtcmInput)
                {
                    return;
                }
                if (*requestId != 0 && result.request_id != *requestId)
                {
                    return;
                }
                QObject::disconnect(*connection);
                if (result.success() && shouldOpenRtkDialog)
                {
                    onRtkConfigClicked();
                }
            });
        *requestId = state_->epsilon_device_session_->configureRtcmInput(operation);
        return;
    }
    const QString selectText = state_->is_english_ ? "-- Select --" : "未选择";
    const QString epsilonPort = state_->local_device_config_.epsilon.port;
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

    const QString epsilonBaudText = state_->local_device_config_.epsilon.baudText;
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
    int deviceRtcmPortIndex = state_->epsilon_config_panel_
        ? state_->epsilon_config_panel_->rtcmDevicePortIndex()
        : settings.value(QStringLiteral("epsilon_rtcm_device_port_index"), 2).toInt();
    if (deviceRtcmPortIndex < 2 || deviceRtcmPortIndex > 5)
    {
        deviceRtcmPortIndex = 2;
    }

    QDialog dialog(this);
    dialog.setModal(true);
    dialog.setWindowTitle(state_->is_english_ ? "Configure EPSILON RTCM Port" : "配置 EPSILON RTCM 串口");

    auto *layout = new QVBoxLayout(&dialog);
    auto *hintLabel = new QLabel(
        state_->is_english_
            ? QStringLiteral("This configures the selected EPSILON communication port as an RTCM input, saves the PC forwarding serial port, and prepares the RTK dialog to stream RTCM continuously into EPSILON. This device-port setting is normally needed only once unless the device, port, or baud rate changes.")
            : QStringLiteral("这个功能会把所选 EPSILON 通信串口配置为 RTCM 输入口，同时保存本机用于转发 RTCM 的串口与波特率，并为后续 RTK 配置对话框做好预填。设备端口通常只需设置一次，除非更换设备、端口或波特率。"),
        &dialog);
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    auto *formLayout = new QFormLayout();
    formLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addLayout(formLayout);

    auto *mainPortValue = new QLabel(QStringLiteral("%1 @ %2").arg(epsilonPort, epsilonBaudText), &dialog);
    formLayout->addRow(state_->is_english_ ? "EPSILON Main Port:" : "EPSILON 主串口：", mainPortValue);

    auto *deviceRtcmPortValue = new QLabel(
        state_->is_english_
            ? QStringLiteral("COMM%1 (selected on card)").arg(deviceRtcmPortIndex)
            : QStringLiteral("串口%1（卡片内选择）").arg(deviceRtcmPortIndex),
        &dialog);
    formLayout->addRow(state_->is_english_ ? "EPSILON RTCM Input Port:" : "EPSILON RTCM 输入口：", deviceRtcmPortValue);

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
                         QStringLiteral("请选择连接到 EPSILON RTCM 输入口的本机串口。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("reason_code"), QStringLiteral("MISSING_ENDPOINT")},
                          {QStringLiteral("main_port"), epsilonPort},
                          {QStringLiteral("device_port"), deviceRtcmPortIndex},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("epsilon:rtcm_config:missing_forward_port")}});
        return;
    }

    if (serialPortNamesReferToSamePort(forwardPort, epsilonPort))
    {
        const QString message = state_->is_english_
            ? QStringLiteral("The RTCM forwarding port must differ from the EPSILON main port. The main port reads real GNSS/FDILink data for NTRIP GGA; connect another PC serial port to the selected EPSILON RTCM input port.")
            : QStringLiteral("RTCM 转发串口不能与 EPSILON 主串口相同。主串口用于读取真实 GNSS/FDILink 数据并生成 NTRIP GGA；请用另一条本机串口连接所选 EPSILON RTCM 输入口。");
        publishGroundLog(VaporView::LogLevel::Warning,
                         QStringLiteral("device.navigation.command"),
                         QStringLiteral("epsilon_rtcm_config_rejected_port_conflict"),
                         QStringLiteral("RTCM 转发串口不能与 EPSILON 主串口相同。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("reason_code"), QStringLiteral("CONFIG_INVALID")},
                          {QStringLiteral("main_port"), epsilonPort},
                          {QStringLiteral("device_port"), deviceRtcmPortIndex},
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

    const std::shared_ptr<VaporView::EpsilonCollector> liveCollector = snapshotCollectors().epsilon;
    const bool shouldRestartCollector = liveCollector && liveCollector->isRunning();
    const bool shouldOpenRtkDialog = openRtkConfigCheck->isChecked();
    const bool english = state_->is_english_;

    if (!state_->epsilon_device_session_ ||
        !state_->epsilon_device_session_->operationsAvailable())
    {
        publishGroundLog(VaporView::LogLevel::Warning,
                         QStringLiteral("device.navigation.command"),
                         QStringLiteral("epsilon_rtcm_config_rejected_dependency_unavailable"),
                         QStringLiteral("本地 EPSILON 当前不可用，无法配置 RTCM。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("execution_path"), QStringLiteral("local")},
                          {QStringLiteral("reason_code"), QStringLiteral("DEPENDENCY_UNAVAILABLE")},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("epsilon:local_rtcm_config:not_available")}});
        return;
    }

    publishGroundLog(VaporView::LogLevel::Info,
                     QStringLiteral("device.navigation.command"),
                     QStringLiteral("epsilon_rtcm_config_started"),
                     QStringLiteral("正在把 EPSILON 通信串口配置为 RTCM。"),
                     {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                      {QStringLiteral("main_port"), epsilonPort},
                      {QStringLiteral("main_baud"), epsilonBaud},
                      {QStringLiteral("device_port"), deviceRtcmPortIndex},
                      {QStringLiteral("forward_port"), forwardPort},
                      {QStringLiteral("forward_baud"), forwardBaud},
                      {QStringLiteral("ui_visibility"), QStringLiteral("details")}});

    VaporView::EpsilonRtcmInputOperation operation;
    operation.device_port_index = deviceRtcmPortIndex;
    operation.forward_port = forwardPort;
    operation.forward_baud = forwardBaud;

    VaporView::Ground::EpsilonDeviceOperation localDeviceOperation;
    localDeviceOperation.port = epsilonPort;
    localDeviceOperation.baud = epsilonBaud;
    localDeviceOperation.baud_text = epsilonBaudText;
    localDeviceOperation.english = english;
    localDeviceOperation.live_collector = liveCollector;
    localDeviceOperation.restart_live_stream = shouldRestartCollector;

    auto requestId = std::make_shared<quint64>(0);
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(
        state_->epsilon_device_session_.get(),
        &VaporView::Ground::Devices::EpsilonDeviceSession::operationFinished,
        this,
        [this, requestId, connection, shouldOpenRtkDialog](
            const VaporView::Ground::Devices::EpsilonSessionResult& result) mutable {
            if (result.operation != VaporView::Ground::Devices::EpsilonOperation::ConfigureRtcmInput)
            {
                return;
            }
            if (*requestId != 0 && result.request_id != *requestId)
            {
                return;
            }
            QObject::disconnect(*connection);
            if (result.success() && shouldOpenRtkDialog)
            {
                onRtkConfigClicked();
            }
        });
    *requestId = state_->epsilon_device_session_->configureRtcmInput(
        operation, localDeviceOperation);
}

void MainWindow::onConfigureEpsilonPacketRatesClicked()
{
    if (state_->connection_attempt_in_progress_ || state_->port_detection_in_progress_ || state_->epsilon_reconfigure_in_progress_)
    {
        return;
    }

    QSettings settings = VaporView::applicationConfigSettings();
    settings.beginGroup(QStringLiteral("MainWindow"));
    if (isRemoteSkyMode())
    {
        settings.beginGroup(QStringLiteral("RemoteEpsilonPacketProfile"));
    }
    const std::map<uint8_t, int> defaultRates = defaultEpsilonPacketRates();
    const std::map<uint8_t, int> initialRates = loadCustomEpsilonPacketRates(settings);

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

    const bool remoteTarget = isRemoteSkyMode();
    auto *hintLabel = new QLabel(
        state_->is_english_
            ? (remoteTarget
                   ? QStringLiteral("Configured for the Remote Sky EPSILON packet profile. The recommended default profile prioritizes stable time and 3D navigation output. Rate limits are reflected by each selector's available options.")
                   : QStringLiteral("Configured for the local EPSILON packet profile. The recommended default profile prioritizes stable time and 3D navigation output. Rate limits are reflected by each selector's available options."))
            : (remoteTarget
                   ? QStringLiteral("配置天空端 EPSILON 包频率 profile。推荐默认配置优先保证稳定的时间与三维导航输出。频率上限由各选择框的可选项体现。")
                   : QStringLiteral("配置本地 EPSILON 包频率 profile。推荐默认配置优先保证稳定的时间与三维导航输出。频率上限由各选择框的可选项体现。")),
        &dialog);
    hintLabel->setWordWrap(true);
    hintLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    layout->addWidget(hintLabel);

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
        const int initialRateHz = initialRates.count(option.packet_id) ? initialRates.at(option.packet_id) : defaultRates.at(option.packet_id);
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
    connect(recommendedDefaultsButton, &QPushButton::clicked, &dialog, [&packetCombos, defaultRates]() {
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
        const int rateHz = combo ? combo->currentData().toInt() : defaultRates.at(option.packet_id);
        savedPacketRates[option.packet_id] = rateHz;
    }
    const QString epsilonBaudText = isRemoteSkyMode()
        ? QString::number(state_->remote_sky_config_.epsilon.baud_rate > 0
              ? state_->remote_sky_config_.epsilon.baud_rate
              : 921600)
        : state_->local_device_config_.epsilon.baudText;
    if (!validateEpsilonPacketBandwidth(savedPacketRates, epsilonBaudText, true))
    {
        return;
    }
    for (const auto& entry : savedPacketRates)
    {
        VaporView::setPersistentSetting(settings, epsilonPacketRateSettingsKey(entry.first), entry.second);
    }
    VaporView::removePersistentSetting(settings, QStringLiteral("epsilon_last_config_signature"));
    VaporView::removePersistentSetting(settings, QStringLiteral("epsilon_last_config_apply_version"));
    const QString packetRateSummary = epsilonPacketRatesSummary(savedPacketRates);

    publishGroundLog(VaporView::LogLevel::Info,
                     QStringLiteral("configuration.apply"),
                     QStringLiteral("epsilon_packet_profile_saved"),
                     (savedPacketRates == defaultRates)
                         ? QStringLiteral("已保存 EPSILON 推荐默认包频率配置。")
                         : QStringLiteral("已保存 EPSILON 包频率配置。"),
                     {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                      {QStringLiteral("packet_rate_profile"), savedPacketRates == defaultRates
                          ? QStringLiteral("recommended_default")
                          : QStringLiteral("custom")},
                      {QStringLiteral("packet_rate_summary"), packetRateSummary},
                      {QStringLiteral("ui_visibility"), QStringLiteral("details")}});

    const QString selectText = state_->is_english_ ? "-- Select --" : "未选择";
    const QString epsilonPort = state_->local_device_config_.epsilon.port;
    const bool targetReadyForApply = isRemoteSkyMode() ||
        (!epsilonPort.isEmpty() && epsilonPort != selectText);
    if (!state_->recording_service_->isActive() &&
        targetReadyForApply)
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
        if (state_->epsilon_reconfigure_in_progress_)
        {
            return;
        }

        QSettings settings = VaporView::applicationConfigSettings();
        settings.beginGroup(QStringLiteral("MainWindow"));
        const std::map<uint8_t, int> packetRates = effectiveEpsilonPacketRates(settings);
        const int outputRateHz = std::clamp(state_->epsilon_sample_rate_, 20, 200);
        const int callbackRateHz = epsilonPacketCallbackRate(packetRates, outputRateHz);
        const QString packetRateSummary = epsilonPacketRatesSummary(packetRates);
        const QString packetRateSignature = epsilonPacketRatesSignature(packetRates);
        const QString uiTestPort = QStringLiteral("UI-TEST-EPSILON");
        constexpr int uiTestBaud = 921600;

        struct UiTestEpsilonStep
        {
            QString process_output;
            QString command;
            QString stage;
            bool is_reply = false;
            bool advances_progress = true;
        };
        auto steps = std::make_shared<QVector<UiTestEpsilonStep>>();
        const auto appendCommand = [steps](const QString& command,
                                            const QString& reply,
                                            const QString& stage) {
            steps->push_back({QStringLiteral("[EPSILON TX] %1").arg(command),
                              command,
                              QStringLiteral("已发送命令 %1").arg(command),
                              false,
                              true});
            steps->push_back({QStringLiteral("[EPSILON RX] %1").arg(reply),
                              command,
                              QStringLiteral("已收到 %1 成功回复").arg(command),
                              true,
                              true});
            if (!stage.isEmpty())
            {
                steps->last().stage = stage;
            }
        };
        appendCommand(QStringLiteral("#fconfig"), QStringLiteral("*#OK"),
                      QStringLiteral("已进入配置模式"));
        appendCommand(QStringLiteral("#fmsg"), QStringLiteral("MSG_IMU 250Hz; MSG_AHRS 50Hz"),
                      QStringLiteral("已读取当前输出配置"));
        for (const auto& entry : packetRates)
        {
            const QString command = QStringLiteral("#fmsg %1 %2")
                .arg(QString::number(entry.first, 16).rightJustified(2, QLatin1Char('0')).toUpper())
                .arg(entry.second);
            appendCommand(command,
                          QStringLiteral("*#OK"),
                          QStringLiteral("已收到数据包 0x%1 成功回复")
                              .arg(QString::number(entry.first, 16)
                                       .rightJustified(2, QLatin1Char('0')).toUpper()));
        }
        appendCommand(QStringLiteral("#fsave"), QStringLiteral("*#OK"),
                      QStringLiteral("已保存输出配置"));
        appendCommand(QStringLiteral("#freboot"), QStringLiteral("(y/n)"),
                      QStringLiteral("已收到重启确认提示"));
        steps->push_back({QStringLiteral("[EPSILON TX] y"),
                          QStringLiteral("y"),
                          QStringLiteral("已发送重启确认"),
                          false,
                          true});
        steps->push_back({QStringLiteral("[EPSILON INFO] serial port reopened after device reboot"),
                          QStringLiteral("serial reopen"),
                          QStringLiteral("设备重启后串口已重新打开"),
                          false,
                          false});
        steps->push_back({QStringLiteral("[FDILink RX] navigation stream restored"),
                          QStringLiteral("FDILink"),
                          QStringLiteral("实时导航流已恢复"),
                          true,
                          true});
        const int totalProgressSteps = static_cast<int>(std::count_if(
            steps->cbegin(), steps->cend(), [](const UiTestEpsilonStep& step) {
                return step.advances_progress;
            }));

        state_->epsilon_reconfigure_in_progress_ = true;
        startEpsilonReconfigureProgress(totalProgressSteps);
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.navigation.command"),
                         QStringLiteral("epsilon_output_reconfigure_started"),
                         QStringLiteral("开始手动重配 EPSILON 输出。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("execution_path"), QStringLiteral("ui_test")},
                          {QStringLiteral("port"), uiTestPort},
                          {QStringLiteral("baud"), uiTestBaud},
                          {QStringLiteral("packet_rate_summary"), packetRateSummary},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.navigation.command"),
                         QStringLiteral("epsilon_live_stream_pause_for_configuration"),
                         QStringLiteral("为手动重配 EPSILON 输出临时停止当前数据流。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("operation"), QStringLiteral("output_reconfigure")},
                          {QStringLiteral("execution_path"), QStringLiteral("ui_test")},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
        updateConnectionStatus(anyCollectorRunning());
        updateDeviceConfigState();

        auto *timer = new QTimer(this);
        timer->setInterval(kUiTestEpsilonReconfigureStepDelayMs);
        timer->setSingleShot(false);
        auto stepIndex = std::make_shared<int>(0);
        auto progressStep = std::make_shared<int>(0);
        connect(timer, &QTimer::timeout, this,
                [this, timer, steps, stepIndex, progressStep, totalProgressSteps,
                 outputRateHz, callbackRateHz, packetRateSignature]() {
            if (!isUiTestMode() || !state_->epsilon_reconfigure_in_progress_)
            {
                timer->stop();
                timer->deleteLater();
                return;
            }

            if (*stepIndex >= steps->size())
            {
                timer->stop();
                timer->deleteLater();
                publishGroundLog(VaporView::LogLevel::Info,
                                 QStringLiteral("device.navigation.command"),
                                 QStringLiteral("epsilon_output_reconfigure_completed"),
                                 QStringLiteral("EPSILON 输出手动重配已完成。"),
                                 {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                                  {QStringLiteral("operation"), QStringLiteral("output_reconfigure")},
                                  {QStringLiteral("execution_path"), QStringLiteral("ui_test")},
                                  {QStringLiteral("port"), QStringLiteral("UI-TEST-EPSILON")},
                                  {QStringLiteral("output_rate_hz"), outputRateHz},
                                  {QStringLiteral("callback_rate_hz"), callbackRateHz},
                                  {QStringLiteral("packet_rate_signature"), packetRateSignature},
                                  {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
                publishGroundLog(VaporView::LogLevel::Info,
                                 QStringLiteral("device.navigation.command"),
                                 QStringLiteral("epsilon_configuration_completed_live_stream_restored"),
                                 QStringLiteral("EPSILON 配置已完成，实时导航流已恢复。"),
                                 {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                                  {QStringLiteral("operation"), QStringLiteral("output_reconfigure")},
                                  {QStringLiteral("execution_path"), QStringLiteral("ui_test")},
                                  {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
                publishGroundLog(VaporView::LogLevel::Info,
                                 QStringLiteral("device.navigation.command"),
                                 QStringLiteral("epsilon_operation_completed"),
                                 QStringLiteral("EPSILON 设备操作已完成。"),
                                 {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                                  {QStringLiteral("request_id"), static_cast<qulonglong>(0)},
                                  {QStringLiteral("operation"), QStringLiteral("packet_profile")},
                                  {QStringLiteral("execution_path"), QStringLiteral("ui_test")},
                                  {QStringLiteral("outcome"), QStringLiteral("success")},
                                  {QStringLiteral("command_error_code"), QStringLiteral("ok")},
                                  {QStringLiteral("details"), QStringLiteral("界面测试模式模拟操作成功。")},
                                  {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
                publishUiTestEvent(
                    QStringLiteral("ui_test_epsilon_output_reconfigure_completed"),
                    state_->is_english_
                        ? QStringLiteral("Simulated EPSILON output reconfiguration completed")
                        : QStringLiteral("模拟 EPSILON 输出重配置已完成"),
                    {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                     {QStringLiteral("total_steps"), totalProgressSteps}});
                setEpsilonReconfigureProgress(totalProgressSteps,
                                               totalProgressSteps,
                                               QStringLiteral("实时导航流已恢复"));
                QTimer::singleShot(220, this, [this]() {
                    if (!isUiTestMode() || !state_->epsilon_reconfigure_in_progress_)
                    {
                        return;
                    }
                    stopEpsilonReconfigureProgress();
                    state_->epsilon_reconfigure_in_progress_ = false;
                    updateConnectionStatus(anyCollectorRunning());
                    updateDeviceConfigState();
                });
                return;
            }

            const UiTestEpsilonStep step = steps->at((*stepIndex)++);
            if (step.advances_progress)
            {
                ++(*progressStep);
            }
            publishGroundLog(VaporView::LogLevel::Info,
                             QStringLiteral("device.collector"),
                             QStringLiteral("epsilon_configuration_collector_output"),
                             QStringLiteral("EPSILON 配置过程输出了采集器诊断信息。"),
                             {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                              {QStringLiteral("process_output"), step.process_output},
                              {QStringLiteral("external_raw_text"), true},
                              {QStringLiteral("execution_path"), QStringLiteral("ui_test")},
                              {QStringLiteral("ui_visibility"), QStringLiteral("hidden")},
                              {QStringLiteral("epsilon_progress_current"), *progressStep},
                              {QStringLiteral("epsilon_progress_total"), totalProgressSteps},
                              {QStringLiteral("epsilon_progress_stage"), step.stage},
                              {QStringLiteral("epsilon_progress_command"), step.command},
                              {QStringLiteral("epsilon_progress_kind"), step.is_reply
                                  ? QStringLiteral("reply") : QStringLiteral("command")},
                              {QStringLiteral("epsilon_progress_success"), step.advances_progress},
                              {QStringLiteral("ui_dedupe_key"),
                               QStringLiteral("epsilon:ui_test:progress:%1").arg(*stepIndex)}});
        });
        timer->start();
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

    if (isRemoteSkyMode())
    {
        const int epsilonRate = std::clamp(state_->epsilon_sample_rate_, 20, 200);
        QSettings settings = VaporView::applicationConfigSettings();
        settings.beginGroup(QStringLiteral("MainWindow"));
        settings.beginGroup(QStringLiteral("RemoteEpsilonPacketProfile"));
        const std::map<uint8_t, int> desiredPacketRates =
            effectiveEpsilonPacketRates(settings);
        const QString epsilonBaudText = QString::number(
            state_->remote_sky_config_.epsilon.baud_rate > 0
                ? state_->remote_sky_config_.epsilon.baud_rate
                : 921600);
        if (!validateEpsilonPacketBandwidth(desiredPacketRates, epsilonBaudText, true))
        {
            return;
        }

        VaporView::EpsilonPacketRatesOperation operation;
        operation.output_rate_hz = epsilonRate;
        operation.callback_rate_hz = epsilonPacketCallbackRate(desiredPacketRates, epsilonRate);
        operation.packet_rates = desiredPacketRates;
        operation.packet_rate_signature = epsilonPacketRatesSignature(desiredPacketRates);
        state_->epsilon_sample_rate_ = epsilonRate;
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.navigation.command"),
                         QStringLiteral("epsilon_output_reconfigure_started"),
                         QStringLiteral("开始通过天空端重配 EPSILON 输出。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("execution_path"), QStringLiteral("remote_sky")},
                          {QStringLiteral("port"), state_->remote_sky_config_.epsilon.port},
                          {QStringLiteral("baud"), state_->remote_sky_config_.epsilon.baud_rate},
                          {QStringLiteral("packet_rate_summary"),
                           epsilonPacketRatesSummary(desiredPacketRates)},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
        if (state_->epsilon_device_session_)
        {
            state_->epsilon_device_session_->configurePacketRates(operation);
        }
        return;
    }
    const QString selectText = state_->is_english_ ? "-- Select --" : "未选择";
    const QString epsilonPort = state_->local_device_config_.epsilon.port;
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

    const QString epsilonBaudText = state_->local_device_config_.epsilon.baudText;
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

    const int epsilonRate = std::clamp(state_->epsilon_sample_rate_, 20, 200);
    state_->epsilon_sample_rate_ = epsilonRate;
    QSettings settings = VaporView::applicationConfigSettings();
    settings.beginGroup(QStringLiteral("MainWindow"));
    const std::map<uint8_t, int> desiredPacketRates = effectiveEpsilonPacketRates(settings);
    if (!validateEpsilonPacketBandwidth(desiredPacketRates, epsilonBaudText, true))
    {
        return;
    }
    const int epsilonCallbackRate = epsilonPacketCallbackRate(desiredPacketRates, epsilonRate);
    const QString desiredPacketRateSignature = epsilonPacketRatesSignature(desiredPacketRates);
    const QString desiredPacketRateSummary = epsilonPacketRatesSummary(desiredPacketRates);

    const std::shared_ptr<VaporView::EpsilonCollector> liveCollector = snapshotCollectors().epsilon;
    const bool shouldRestartCollector = liveCollector && liveCollector->isRunning();
    const bool english = state_->is_english_;

    if (!state_->epsilon_device_session_ ||
        !state_->epsilon_device_session_->operationsAvailable())
    {
        publishGroundLog(VaporView::LogLevel::Warning,
                         QStringLiteral("device.navigation.command"),
                         QStringLiteral("epsilon_output_reconfigure_rejected_dependency_unavailable"),
                         QStringLiteral("本地 EPSILON 当前不可用，无法重新配置输出。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("execution_path"), QStringLiteral("local")},
                          {QStringLiteral("reason_code"), QStringLiteral("DEPENDENCY_UNAVAILABLE")},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("epsilon:local_output_reconfigure:not_available")}});
        return;
    }

    publishGroundLog(VaporView::LogLevel::Info,
                     QStringLiteral("device.navigation.command"),
                     QStringLiteral("epsilon_output_reconfigure_started"),
                     QStringLiteral("开始手动重配 EPSILON 输出。"),
                     {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                      {QStringLiteral("port"), epsilonPort},
                      {QStringLiteral("baud"), epsilonBaud},
                      {QStringLiteral("packet_rate_summary"), desiredPacketRateSummary},
                      {QStringLiteral("ui_visibility"), QStringLiteral("details")}});

    VaporView::EpsilonPacketRatesOperation operation;
    operation.output_rate_hz = epsilonRate;
    operation.callback_rate_hz = epsilonCallbackRate;
    operation.packet_rates = desiredPacketRates;
    operation.packet_rate_signature = desiredPacketRateSignature;

    VaporView::Ground::EpsilonDeviceOperation localDeviceOperation;
    localDeviceOperation.port = epsilonPort;
    localDeviceOperation.baud = epsilonBaud;
    localDeviceOperation.baud_text = epsilonBaudText;
    localDeviceOperation.english = english;
    localDeviceOperation.live_collector = liveCollector;
    localDeviceOperation.restart_live_stream = shouldRestartCollector;

    state_->epsilon_device_session_->configurePacketRates(operation, localDeviceOperation);
}
