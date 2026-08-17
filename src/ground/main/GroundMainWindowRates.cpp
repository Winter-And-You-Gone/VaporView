#include "ground/main/GroundMainWindowImplementation.h"
#include "ground/devices/DeviceRatePolicy.h"

void MainWindow::onGlobalRateChanged(const QString& text)
{
    int rate = parseRate(text);
    const bool skipEpsilonDeviceRate = state_->epsilon_rate_combo_ && isRateUnspecified(state_->epsilon_rate_combo_->currentText());
    const bool skipPtbDeviceRate = state_->ptb_rate_combo_ && isRateUnspecified(state_->ptb_rate_combo_->currentText());
    const bool skipHmpDeviceRate = state_->hmp_rate_combo_ && isRateUnspecified(state_->hmp_rate_combo_->currentText());
    const bool skipLidarDeviceRate = state_->lidar_rate_combo_ && isRateUnspecified(state_->lidar_rate_combo_->currentText());
    const bool skipTemperatureDeviceRate = state_->temperature_rate_combo_ && isRateUnspecified(state_->temperature_rate_combo_->currentText());

    state_->epsilon_sample_rate_ = skipEpsilonDeviceRate ? kDefaultEpsilonSampleRateHz : std::clamp(rate, 20, 200);
    state_->ptb_sample_rate_ = skipPtbDeviceRate ? kDefaultPtbSampleRateHz : clampPtbSampleRate(rate);
    state_->hmp_sample_rate_ = skipHmpDeviceRate ? kDefaultHmpSampleRateHz : rate;
    state_->lidar_sample_rate_ = skipLidarDeviceRate ? kDefaultLidarSampleRateHz : std::min(rate, 100);
    state_->temperature_sample_rate_ = skipTemperatureDeviceRate ? kDefaultTemperatureSampleRateHz : std::min(rate, kMaxTemperatureSampleRateHz);

    if (state_->epsilon_rate_combo_) state_->epsilon_rate_combo_->blockSignals(true);
    if (state_->ptb_rate_combo_) state_->ptb_rate_combo_->blockSignals(true);
    if (state_->hmp_rate_combo_) state_->hmp_rate_combo_->blockSignals(true);
    if (state_->lidar_rate_combo_) state_->lidar_rate_combo_->blockSignals(true);
    if (state_->temperature_rate_combo_) state_->temperature_rate_combo_->blockSignals(true);

    if (state_->epsilon_rate_combo_ && !skipEpsilonDeviceRate) state_->epsilon_rate_combo_->setCurrentText(QString::number(state_->epsilon_sample_rate_));
    if (state_->ptb_rate_combo_ && !skipPtbDeviceRate) state_->ptb_rate_combo_->setCurrentText(QString::number(state_->ptb_sample_rate_));
    if (state_->hmp_rate_combo_ && !skipHmpDeviceRate) state_->hmp_rate_combo_->setCurrentText(text);
    if (state_->lidar_rate_combo_ && !skipLidarDeviceRate) state_->lidar_rate_combo_->setCurrentText(QString::number(state_->lidar_sample_rate_));
    if (state_->temperature_rate_combo_ && !skipTemperatureDeviceRate) state_->temperature_rate_combo_->setCurrentText(QString::number(state_->temperature_sample_rate_));

    if (state_->epsilon_rate_combo_) state_->epsilon_rate_combo_->blockSignals(false);
    if (state_->ptb_rate_combo_) state_->ptb_rate_combo_->blockSignals(false);
    if (state_->hmp_rate_combo_) state_->hmp_rate_combo_->blockSignals(false);
    if (state_->lidar_rate_combo_) state_->lidar_rate_combo_->blockSignals(false);
    if (state_->temperature_rate_combo_) state_->temperature_rate_combo_->blockSignals(false);

    QSettings settings = VaporView::applicationConfigSettings();
    settings.beginGroup(QStringLiteral("MainWindow"));
    const std::map<uint8_t, int> epsilonDesiredPacketRates =
        effectiveEpsilonPacketRates(settings);
    const int epsilonCallbackRate = epsilonPacketCallbackRate(epsilonDesiredPacketRates, state_->epsilon_sample_rate_);
    LocalSampleRateConfiguration configuration;
    configuration.epsilonCallbackRateHz = epsilonCallbackRate;
    configuration.epsilonPacketRates = epsilonDesiredPacketRates;
    configuration.applyEpsilonDeviceRate = !skipEpsilonDeviceRate;
    configuration.ptbRateHz = state_->ptb_sample_rate_;
    configuration.applyPtbDeviceRate = !skipPtbDeviceRate;
    configuration.hmpRateHz = state_->hmp_sample_rate_;
    configuration.lidarRateHz = state_->lidar_sample_rate_;
    configuration.applyLidarDeviceRate = !skipLidarDeviceRate;
    configuration.temperatureRateHz = state_->temperature_sample_rate_;
    configuration.ai8TemperatureRateHz = state_->device_config_.ai8_temperature_rate_combo
        ? effectiveRateOrDefault(state_->device_config_.ai8_temperature_rate_combo->currentText(), 5, 20)
        : 5;
    const LocalSampleRateApplyResult rateResult =
        state_->local_connection_controller_->applyRunningSampleRates(configuration);
    const bool epsilonDeviceRateFailed =
        rateResult.epsilonDeviceRateAttempted && !rateResult.epsilonDeviceRateSucceeded;
    const bool ptbDeviceRateFailed =
        rateResult.ptbDeviceRateAttempted && !rateResult.ptbDeviceRateSucceeded;
    if (epsilonDeviceRateFailed)
    {
        publishGroundLog(VaporView::LogLevel::Error,
                         QStringLiteral("device.navigation.command"),
                         QStringLiteral("epsilon_output_rate_command_failed"),
                         QStringLiteral("EPSILON 输出频率下发失败。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("requested_rate_hz"), state_->epsilon_sample_rate_},
                          {QStringLiteral("error_code"), QStringLiteral("COMMAND_VERIFY_FAILED")},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("epsilon:output_rate:command_failed")}});
    }
    if (ptbDeviceRateFailed)
    {
        publishGroundLog(VaporView::LogLevel::Error,
                         QStringLiteral("device.pressure.command"),
                         QStringLiteral("ptb_sample_rate_command_failed"),
                         QStringLiteral("PTB210 采样频率命令下发失败。"),
                         {{QStringLiteral("device"), QStringLiteral("PTB210")},
                          {QStringLiteral("requested_rate_hz"), state_->ptb_sample_rate_},
                          {QStringLiteral("error_code"), QStringLiteral("COMMAND_VERIFY_FAILED")},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("ptb210:sample_rate:command_failed")}});
    }

    if (epsilonDeviceRateFailed || ptbDeviceRateFailed)
    {
        publishGroundLog(VaporView::LogLevel::Warning,
                         QStringLiteral("device.rate"),
                         QStringLiteral("sample_rate_apply_partial_failure"),
                         QStringLiteral("主机侧频率已更新，但一个或多个设备输出频率命令失败。"),
                         {{QStringLiteral("requested_rate_hz"), rate},
                          {QStringLiteral("epsilon_command_failed"), epsilonDeviceRateFailed},
                          {QStringLiteral("ptb_command_failed"), ptbDeviceRateFailed},
                          {QStringLiteral("reason_code"), QStringLiteral("DEPENDENCY_UNAVAILABLE")},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("sample_rate:apply:partial_failure")}});
    }
    else
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.rate"),
                         QStringLiteral("sample_rates_updated"),
                         QStringLiteral("所有频率已更新。"),
                         {{QStringLiteral("requested_rate_hz"), rate},
                          {QStringLiteral("epsilon_rate_hz"), state_->epsilon_sample_rate_},
                          {QStringLiteral("ptb_rate_hz"), state_->ptb_sample_rate_},
                          {QStringLiteral("hmp_rate_hz"), state_->hmp_sample_rate_},
                          {QStringLiteral("lidar_rate_hz"), state_->lidar_sample_rate_},
                          {QStringLiteral("temperature_rate_hz"), state_->temperature_sample_rate_},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
    if (skipEpsilonDeviceRate || skipPtbDeviceRate || skipHmpDeviceRate || skipLidarDeviceRate || skipTemperatureDeviceRate)
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.rate"),
                         QStringLiteral("sample_rate_device_commands_skipped_unspecified"),
                         QStringLiteral("已选择“不设定”的设备保持不下发输出频率命令。"),
                         {{QStringLiteral("epsilon_skipped"), skipEpsilonDeviceRate},
                          {QStringLiteral("ptb_skipped"), skipPtbDeviceRate},
                          {QStringLiteral("hmp_skipped"), skipHmpDeviceRate},
                          {QStringLiteral("lidar_skipped"), skipLidarDeviceRate},
                          {QStringLiteral("temperature_skipped"), skipTemperatureDeviceRate},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
    if (!skipPtbDeviceRate && state_->ptb_sample_rate_ != rate)
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.rate"),
                         QStringLiteral("ptb_sample_rate_capped"),
                         QStringLiteral("PTB210 采样频率已按设备上限限制。"),
                         {{QStringLiteral("device"), QStringLiteral("PTB210")},
                          {QStringLiteral("requested_rate_hz"), rate},
                          {QStringLiteral("effective_rate_hz"), state_->ptb_sample_rate_},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
    if (!skipTemperatureDeviceRate && state_->temperature_sample_rate_ != rate)
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.rate"),
                         QStringLiteral("temperature_polling_rate_capped"),
                         QStringLiteral("RD105 轮询频率已按设备上限限制。"),
                         {{QStringLiteral("device"), QStringLiteral("RD105")},
                          {QStringLiteral("requested_rate_hz"), rate},
                          {QStringLiteral("effective_rate_hz"), state_->temperature_sample_rate_},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
}

void MainWindow::onGnssRateChanged(const QString& text)
{
    const bool skipDeviceRate = isRateUnspecified(text);
    state_->epsilon_sample_rate_ = effectiveRateOrDefault(text, kDefaultEpsilonSampleRateHz, 200);
    QSettings settings = VaporView::applicationConfigSettings();
    settings.beginGroup(QStringLiteral("MainWindow"));
    const std::map<uint8_t, int> epsilonDesiredPacketRates =
        effectiveEpsilonPacketRates(settings);
    const int epsilonCallbackRate = epsilonPacketCallbackRate(epsilonDesiredPacketRates, state_->epsilon_sample_rate_);
    const LocalSampleRateApplyResult rateResult =
        state_->local_connection_controller_->setEpsilonSampleRate(
        epsilonCallbackRate,
        epsilonDesiredPacketRates,
        !skipDeviceRate);
    if (skipDeviceRate)
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.navigation.command"),
                         QStringLiteral("epsilon_output_rate_command_disabled"),
                         QStringLiteral("已禁用 EPSILON 输出频率下发，使用设备当前输出。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("apply_device_rate"), false},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
    else if (rateResult.epsilonDeviceRateAttempted && !rateResult.epsilonDeviceRateSucceeded)
    {
        publishGroundLog(VaporView::LogLevel::Error,
                         QStringLiteral("device.navigation.command"),
                         QStringLiteral("epsilon_output_rate_command_failed"),
                         QStringLiteral("EPSILON 输出频率下发失败。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("requested_rate_hz"), state_->epsilon_sample_rate_},
                          {QStringLiteral("error_code"), QStringLiteral("COMMAND_VERIFY_FAILED")},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("epsilon:output_rate:command_failed")}});
    }
    else if (!rateResult.epsilonDeviceRateAttempted)
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.navigation.command"),
                         QStringLiteral("epsilon_output_rate_saved_deferred"),
                         QStringLiteral("EPSILON 输出频率已保存，将在下次连接时应用。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("requested_rate_hz"), state_->epsilon_sample_rate_},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
    else
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.navigation.command"),
                         QStringLiteral("epsilon_output_rate_updated"),
                         QStringLiteral("EPSILON 输出频率已更新。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("requested_rate_hz"), state_->epsilon_sample_rate_},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
}

void MainWindow::onImuRateChanged(const QString& text)
{
    Q_UNUSED(text);
}

void MainWindow::onPtbRateChanged(const QString& text)
{
    const bool skipDeviceRate = isRateUnspecified(text);
    const int requestedRate = parseRate(text);
    state_->ptb_sample_rate_ = skipDeviceRate ? kDefaultPtbSampleRateHz : clampPtbSampleRate(requestedRate);
    if (skipDeviceRate)
    {
        state_->local_connection_controller_->setPtbSampleRate(
            state_->ptb_sample_rate_,
            false);
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.pressure.command"),
                         QStringLiteral("ptb_sample_rate_command_disabled"),
                         QStringLiteral("已禁用 PTB210 采样频率下发，使用设备当前输出。"),
                         {{QStringLiteral("device"), QStringLiteral("PTB210")},
                          {QStringLiteral("apply_device_rate"), false},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
        return;
    }

    if (state_->ptb_rate_combo_ && state_->ptb_rate_combo_->currentText() != QString::number(state_->ptb_sample_rate_))
    {
        QSignalBlocker blocker(state_->ptb_rate_combo_);
        state_->ptb_rate_combo_->setCurrentText(QString::number(state_->ptb_sample_rate_));
    }

    const LocalSampleRateApplyResult rateResult =
        state_->local_connection_controller_->setPtbSampleRate(
            state_->ptb_sample_rate_,
            true);
    if (rateResult.ptbDeviceRateAttempted && !rateResult.ptbDeviceRateSucceeded)
    {
        publishGroundLog(VaporView::LogLevel::Error,
                         QStringLiteral("device.pressure.command"),
                         QStringLiteral("ptb_sample_rate_command_failed"),
                         QStringLiteral("PTB210 采样频率命令下发失败。"),
                         {{QStringLiteral("device"), QStringLiteral("PTB210")},
                          {QStringLiteral("requested_rate_hz"), state_->ptb_sample_rate_},
                          {QStringLiteral("error_code"), QStringLiteral("COMMAND_VERIFY_FAILED")},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("ptb210:sample_rate:command_failed")}});
        return;
    }
    if (requestedRate != state_->ptb_sample_rate_)
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.rate"),
                         QStringLiteral("ptb_sample_rate_updated_capped"),
                         QStringLiteral("PTB210 采样频率已更新，并按设备上限限制。"),
                         {{QStringLiteral("device"), QStringLiteral("PTB210")},
                          {QStringLiteral("requested_rate_hz"), requestedRate},
                          {QStringLiteral("effective_rate_hz"), state_->ptb_sample_rate_},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
    else
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.rate"),
                         QStringLiteral("ptb_sample_rate_updated"),
                         QStringLiteral("PTB210 采样频率已更新。"),
                         {{QStringLiteral("device"), QStringLiteral("PTB210")},
                          {QStringLiteral("requested_rate_hz"), state_->ptb_sample_rate_},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
}

void MainWindow::onHmpRateChanged(const QString& text)
{
    const bool skipDeviceRate = isRateUnspecified(text);
    state_->hmp_sample_rate_ = effectiveRateOrDefault(text, kDefaultHmpSampleRateHz);
    state_->local_connection_controller_->setHmpSampleRate(state_->hmp_sample_rate_);
    if (skipDeviceRate)
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.rate"),
                         QStringLiteral("hmp_polling_rate_defaulted"),
                         QStringLiteral("HMP3 轮询频率保持不设定，使用默认主机轮询频率。"),
                         {{QStringLiteral("device"), QStringLiteral("HMP3")},
                          {QStringLiteral("effective_rate_hz"), state_->hmp_sample_rate_},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
    else
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.rate"),
                         QStringLiteral("hmp_sample_rate_updated"),
                         QStringLiteral("HMP3 采样频率已更新。"),
                         {{QStringLiteral("device"), QStringLiteral("HMP3")},
                          {QStringLiteral("requested_rate_hz"), state_->hmp_sample_rate_},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
}

void MainWindow::onLidarRateChanged(const QString& text)
{
    const bool skipDeviceRate = isRateUnspecified(text);
    state_->lidar_sample_rate_ = effectiveRateOrDefault(text, kDefaultLidarSampleRateHz, 100);
    state_->local_connection_controller_->setLidarSampleRate(
        state_->lidar_sample_rate_,
        !skipDeviceRate);
    if (skipDeviceRate)
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.lidar.command"),
                         QStringLiteral("lidar_output_rate_command_disabled"),
                         QStringLiteral("已禁用激光测距仪输出频率下发，使用设备默认或自适应输出。"),
                         {{QStringLiteral("device"), QStringLiteral("TFA1005-L")},
                          {QStringLiteral("apply_device_rate"), false},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
    else
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.rate"),
                         QStringLiteral("lidar_sample_rate_updated"),
                         QStringLiteral("激光测距仪采样频率已更新。"),
                         {{QStringLiteral("device"), QStringLiteral("TFA1005-L")},
                          {QStringLiteral("requested_rate_hz"), state_->lidar_sample_rate_},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
}

void MainWindow::onTemperatureRateChanged(const QString& text)
{
    const bool skipDeviceRate = isRateUnspecified(text);
    state_->temperature_sample_rate_ = effectiveRateOrDefault(text, kDefaultTemperatureSampleRateHz, kMaxTemperatureSampleRateHz);
    state_->local_connection_controller_->setTemperatureSampleRate(
        state_->temperature_sample_rate_);
    if (skipDeviceRate)
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.rate"),
                         QStringLiteral("temperature_polling_rate_defaulted"),
                         QStringLiteral("RD105 轮询频率保持不设定，使用默认主机轮询频率。"),
                         {{QStringLiteral("device"), QStringLiteral("RD105")},
                          {QStringLiteral("effective_rate_hz"), state_->temperature_sample_rate_},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
    else
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.rate"),
                         QStringLiteral("temperature_polling_rate_updated"),
                         QStringLiteral("RD105 轮询频率已更新。"),
                         {{QStringLiteral("device"), QStringLiteral("RD105")},
                          {QStringLiteral("requested_rate_hz"), state_->temperature_sample_rate_},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
}

void MainWindow::applyAllSampleRates()
{
    int rate = parseRate(state_->global_rate_combo_ ? state_->global_rate_combo_->currentText() : QString::number(kDefaultHmpSampleRateHz));
    const bool skipEpsilonDeviceRate = state_->epsilon_rate_combo_ && isRateUnspecified(state_->epsilon_rate_combo_->currentText());
    const bool skipPtbDeviceRate = state_->ptb_rate_combo_ && isRateUnspecified(state_->ptb_rate_combo_->currentText());
    const bool skipHmpDeviceRate = state_->hmp_rate_combo_ && isRateUnspecified(state_->hmp_rate_combo_->currentText());
    const bool skipLidarDeviceRate = state_->lidar_rate_combo_ && isRateUnspecified(state_->lidar_rate_combo_->currentText());
    const bool skipTemperatureDeviceRate = state_->temperature_rate_combo_ && isRateUnspecified(state_->temperature_rate_combo_->currentText());
    QSettings settings = VaporView::applicationConfigSettings();
    settings.beginGroup(QStringLiteral("MainWindow"));
    const int epsilonRate = skipEpsilonDeviceRate ? kDefaultEpsilonSampleRateHz : std::clamp(rate, 20, 200);
    const int ptbRate = skipPtbDeviceRate ? kDefaultPtbSampleRateHz : clampPtbSampleRate(rate);
    const int hmpRate = skipHmpDeviceRate ? kDefaultHmpSampleRateHz : rate;
    const int lidarRate = skipLidarDeviceRate ? kDefaultLidarSampleRateHz : std::min(rate, 100);
    const int temperatureRate = skipTemperatureDeviceRate ? kDefaultTemperatureSampleRateHz : std::min(rate, kMaxTemperatureSampleRateHz);
    const std::map<uint8_t, int> epsilonDesiredPacketRates =
        effectiveEpsilonPacketRates(settings);
    const int epsilonCallbackRate = epsilonPacketCallbackRate(epsilonDesiredPacketRates, epsilonRate);

    LocalSampleRateConfiguration configuration;
    configuration.epsilonCallbackRateHz = epsilonCallbackRate;
    configuration.epsilonPacketRates = epsilonDesiredPacketRates;
    configuration.applyEpsilonDeviceRate = !skipEpsilonDeviceRate;
    configuration.ptbRateHz = ptbRate;
    configuration.applyPtbDeviceRate = !skipPtbDeviceRate;
    configuration.hmpRateHz = hmpRate;
    configuration.lidarRateHz = lidarRate;
    configuration.applyLidarDeviceRate = !skipLidarDeviceRate;
    configuration.temperatureRateHz = temperatureRate;
    configuration.ai8TemperatureRateHz = state_->device_config_.ai8_temperature_rate_combo
        ? effectiveRateOrDefault(state_->device_config_.ai8_temperature_rate_combo->currentText(), 5, 20)
        : 5;
    const LocalSampleRateApplyResult rateResult =
        state_->local_connection_controller_->applyRunningSampleRates(configuration);
    const bool epsilonDeviceRateFailed =
        rateResult.epsilonDeviceRateAttempted && !rateResult.epsilonDeviceRateSucceeded;
    const bool ptbDeviceRateFailed =
        rateResult.ptbDeviceRateAttempted && !rateResult.ptbDeviceRateSucceeded;
    if (epsilonDeviceRateFailed)
    {
        publishGroundLog(VaporView::LogLevel::Error,
                         QStringLiteral("device.navigation.command"),
                         QStringLiteral("epsilon_output_rate_command_failed"),
                         QStringLiteral("EPSILON 输出频率下发失败。"),
                         {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                          {QStringLiteral("requested_rate_hz"), epsilonRate},
                          {QStringLiteral("error_code"), QStringLiteral("COMMAND_VERIFY_FAILED")},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("epsilon:output_rate:command_failed")}});
    }
    if (ptbDeviceRateFailed)
    {
        publishGroundLog(VaporView::LogLevel::Error,
                         QStringLiteral("device.pressure.command"),
                         QStringLiteral("ptb_sample_rate_command_failed"),
                         QStringLiteral("PTB210 采样频率命令下发失败。"),
                         {{QStringLiteral("device"), QStringLiteral("PTB210")},
                          {QStringLiteral("requested_rate_hz"), ptbRate},
                          {QStringLiteral("error_code"), QStringLiteral("COMMAND_VERIFY_FAILED")},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("ptb210:sample_rate:command_failed")}});
    }

    if (state_->epsilon_rate_combo_) state_->epsilon_rate_combo_->blockSignals(true);
    if (state_->ptb_rate_combo_) state_->ptb_rate_combo_->blockSignals(true);
    if (state_->hmp_rate_combo_) state_->hmp_rate_combo_->blockSignals(true);
    if (state_->lidar_rate_combo_) state_->lidar_rate_combo_->blockSignals(true);
    if (state_->temperature_rate_combo_) state_->temperature_rate_combo_->blockSignals(true);

    if (state_->epsilon_rate_combo_ && !skipEpsilonDeviceRate) state_->epsilon_rate_combo_->setCurrentText(QString::number(epsilonRate));
    if (state_->ptb_rate_combo_ && !skipPtbDeviceRate) state_->ptb_rate_combo_->setCurrentText(QString::number(ptbRate));
    if (state_->hmp_rate_combo_ && !skipHmpDeviceRate) state_->hmp_rate_combo_->setCurrentText(QString::number(rate));
    if (state_->lidar_rate_combo_ && !skipLidarDeviceRate) state_->lidar_rate_combo_->setCurrentText(QString::number(lidarRate));
    if (state_->temperature_rate_combo_ && !skipTemperatureDeviceRate) state_->temperature_rate_combo_->setCurrentText(QString::number(temperatureRate));

    if (state_->epsilon_rate_combo_) state_->epsilon_rate_combo_->blockSignals(false);
    if (state_->ptb_rate_combo_) state_->ptb_rate_combo_->blockSignals(false);
    if (state_->hmp_rate_combo_) state_->hmp_rate_combo_->blockSignals(false);
    if (state_->lidar_rate_combo_) state_->lidar_rate_combo_->blockSignals(false);
    if (state_->temperature_rate_combo_) state_->temperature_rate_combo_->blockSignals(false);

    state_->gnss_sample_rate_ = rate;
    state_->imu_sample_rate_ = rate;
    state_->ptb_sample_rate_ = ptbRate;
    state_->hmp_sample_rate_ = hmpRate;
    state_->lidar_sample_rate_ = lidarRate;
    state_->temperature_sample_rate_ = temperatureRate;

    if (epsilonDeviceRateFailed || ptbDeviceRateFailed)
    {
        publishGroundLog(VaporView::LogLevel::Warning,
                         QStringLiteral("device.rate"),
                         QStringLiteral("sample_rate_apply_partial_failure"),
                         QStringLiteral("主机侧频率已更新，但一个或多个设备输出频率命令失败。"),
                         {{QStringLiteral("requested_rate_hz"), rate},
                          {QStringLiteral("epsilon_command_failed"), epsilonDeviceRateFailed},
                          {QStringLiteral("ptb_command_failed"), ptbDeviceRateFailed},
                          {QStringLiteral("reason_code"), QStringLiteral("DEPENDENCY_UNAVAILABLE")},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("sample_rate:apply:partial_failure")}});
    }
    else
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.rate"),
                         QStringLiteral("sample_rates_updated"),
                         QStringLiteral("所有频率已更新。"),
                         {{QStringLiteral("requested_rate_hz"), rate},
                          {QStringLiteral("epsilon_rate_hz"), epsilonRate},
                          {QStringLiteral("ptb_rate_hz"), ptbRate},
                          {QStringLiteral("hmp_rate_hz"), hmpRate},
                          {QStringLiteral("lidar_rate_hz"), lidarRate},
                          {QStringLiteral("temperature_rate_hz"), temperatureRate},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
    if (skipEpsilonDeviceRate || skipPtbDeviceRate || skipHmpDeviceRate || skipLidarDeviceRate || skipTemperatureDeviceRate)
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.rate"),
                         QStringLiteral("sample_rate_device_commands_skipped_unspecified"),
                         QStringLiteral("已选择“不设定”的设备保持不下发输出频率命令。"),
                         {{QStringLiteral("epsilon_skipped"), skipEpsilonDeviceRate},
                          {QStringLiteral("ptb_skipped"), skipPtbDeviceRate},
                          {QStringLiteral("hmp_skipped"), skipHmpDeviceRate},
                          {QStringLiteral("lidar_skipped"), skipLidarDeviceRate},
                          {QStringLiteral("temperature_skipped"), skipTemperatureDeviceRate},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
    if (!skipPtbDeviceRate && ptbRate != rate)
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.rate"),
                         QStringLiteral("ptb_sample_rate_capped"),
                         QStringLiteral("PTB210 采样频率已按设备上限限制。"),
                         {{QStringLiteral("device"), QStringLiteral("PTB210")},
                          {QStringLiteral("requested_rate_hz"), rate},
                          {QStringLiteral("effective_rate_hz"), ptbRate},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
    if (!skipTemperatureDeviceRate && temperatureRate != rate)
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("device.rate"),
                         QStringLiteral("temperature_polling_rate_capped"),
                         QStringLiteral("RD105 轮询频率已按设备上限限制。"),
                         {{QStringLiteral("device"), QStringLiteral("RD105")},
                          {QStringLiteral("requested_rate_hz"), rate},
                          {QStringLiteral("effective_rate_hz"), temperatureRate},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
}
