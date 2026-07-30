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
    bool epsilonUsesCustomPacketRates = false;
    const std::map<uint8_t, int> epsilonDesiredPacketRates =
        effectiveEpsilonPacketRates(settings, state_->epsilon_sample_rate_, &epsilonUsesCustomPacketRates);
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
    const LocalSampleRateApplyResult rateResult =
        state_->local_connection_controller_->applyRunningSampleRates(configuration);
    const bool epsilonDeviceRateFailed =
        rateResult.epsilonDeviceRateAttempted && !rateResult.epsilonDeviceRateSucceeded;
    const bool ptbDeviceRateFailed =
        rateResult.ptbDeviceRateAttempted && !rateResult.ptbDeviceRateSucceeded;
    if (epsilonDeviceRateFailed)
    {
        log(state_->is_english_
            ? "EPSILON output-rate command failed; the live stream recovery result is shown above."
            : "EPSILON 输出频率下发失败；实时数据流恢复结果请查看上方日志。");
    }
    if (ptbDeviceRateFailed)
    {
        log(QString(state_->is_english_
            ? "PTB sample rate command failed for %1 Hz"
            : "PTB采样频率命令下发失败：%1 Hz").arg(state_->ptb_sample_rate_));
    }

    if (epsilonDeviceRateFailed || ptbDeviceRateFailed)
    {
        log(state_->is_english_
            ? "Host-side rates were updated, but one or more device output-rate commands failed."
            : "主机侧频率已更新，但一个或多个设备输出频率命令失败。");
    }
    else if (epsilonUsesCustomPacketRates)
    {
        log(QString(state_->is_english_
                        ? "All rates set to %1 Hz; EPSILON keeps the saved custom packet-rate profile."
                        : "所有频率已设置为 %1 Hz；EPSILON 保持已保存的自定义包频率配置。")
                .arg(rate));
    }
    else
    {
        log(QString(state_->is_english_ ? "All rates set to %1 Hz" : "所有频率已设置为 %1 Hz").arg(rate));
    }
    if (skipEpsilonDeviceRate || skipPtbDeviceRate || skipHmpDeviceRate || skipLidarDeviceRate || skipTemperatureDeviceRate)
    {
        log(state_->is_english_
            ? "Devices set to No Set keep their output-rate commands disabled."
            : "已选择“不设定”的设备保持不下发输出频率命令。");
    }
    if (!skipPtbDeviceRate && state_->ptb_sample_rate_ != rate)
    {
        log(QString(state_->is_english_
            ? "PTB sample rate capped at %1 Hz"
            : "PTB采样频率已限制为 %1 Hz").arg(state_->ptb_sample_rate_));
    }
    if (!skipTemperatureDeviceRate && state_->temperature_sample_rate_ != rate)
    {
        log(QString(state_->is_english_
            ? "RD105 polling rate capped at %1 Hz"
            : "RD105 轮询频率已限制为 %1 Hz").arg(state_->temperature_sample_rate_));
    }
}

void MainWindow::onGnssRateChanged(const QString& text)
{
    const bool skipDeviceRate = isRateUnspecified(text);
    state_->epsilon_sample_rate_ = effectiveRateOrDefault(text, kDefaultEpsilonSampleRateHz, 200);
    QSettings settings = VaporView::applicationConfigSettings();
    settings.beginGroup(QStringLiteral("MainWindow"));
    bool epsilonUsesCustomPacketRates = false;
    const std::map<uint8_t, int> epsilonDesiredPacketRates =
        effectiveEpsilonPacketRates(settings, state_->epsilon_sample_rate_, &epsilonUsesCustomPacketRates);
    const int epsilonCallbackRate = epsilonPacketCallbackRate(epsilonDesiredPacketRates, state_->epsilon_sample_rate_);
    const LocalSampleRateApplyResult rateResult =
        state_->local_connection_controller_->setEpsilonSampleRate(
        epsilonCallbackRate,
        epsilonDesiredPacketRates,
        !skipDeviceRate);
    if (skipDeviceRate)
    {
        log(state_->is_english_
            ? "EPSILON output-rate command disabled; using the current device output."
            : "已禁用 EPSILON 输出频率下发，使用设备当前输出。");
    }
    else if (rateResult.epsilonDeviceRateAttempted && !rateResult.epsilonDeviceRateSucceeded)
    {
        log(state_->is_english_
            ? "EPSILON output-rate command failed; the live stream recovery result is shown above."
            : "EPSILON 输出频率下发失败；实时数据流恢复结果请查看上方日志。");
    }
    else if (!rateResult.epsilonDeviceRateAttempted)
    {
        log(state_->is_english_
            ? "EPSILON output rate saved for the next connection."
            : "EPSILON 输出频率已保存，将在下次连接时应用。");
    }
    else if (epsilonUsesCustomPacketRates)
    {
        log(QString(state_->is_english_
                        ? "EPSILON grouped rate was set to %1 Hz, but the saved custom packet-rate profile remains active."
                        : "EPSILON 分组频率已设置为 %1 Hz，但当前仍启用已保存的自定义包频率配置。")
                .arg(state_->epsilon_sample_rate_));
    }
    else
    {
        log(QString(state_->is_english_ ? "EPSILON output rate set to %1 Hz" : "EPSILON 输出频率已设置为 %1 Hz").arg(state_->epsilon_sample_rate_));
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
        log(state_->is_english_
            ? "PTB sample-rate command disabled; using the current device output."
            : "已禁用 PTB 采样频率下发，使用设备当前输出。");
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
        log(QString(state_->is_english_
            ? "PTB sample rate command failed for %1 Hz"
            : "PTB采样频率命令下发失败：%1 Hz").arg(state_->ptb_sample_rate_));
        return;
    }
    if (requestedRate != state_->ptb_sample_rate_)
    {
        log(QString(state_->is_english_
            ? "PTB sample rate set to %1 Hz (capped from %2 Hz)"
            : "PTB采样频率已设置为 %1 Hz（由 %2 Hz 限制）")
            .arg(state_->ptb_sample_rate_)
            .arg(requestedRate));
    }
    else
    {
        log(QString(state_->is_english_ ? "PTB sample rate set to %1 Hz" : "PTB采样频率已设置为 %1 Hz").arg(state_->ptb_sample_rate_));
    }
}

void MainWindow::onHmpRateChanged(const QString& text)
{
    const bool skipDeviceRate = isRateUnspecified(text);
    state_->hmp_sample_rate_ = effectiveRateOrDefault(text, kDefaultHmpSampleRateHz);
    state_->local_connection_controller_->setHmpSampleRate(state_->hmp_sample_rate_);
    if (skipDeviceRate)
    {
        log(state_->is_english_
            ? "HMP polling-rate selection left unset; using the default host polling rate."
            : "HMP 轮询频率保持不设定，使用默认主机轮询频率。");
    }
    else
    {
        log(QString(state_->is_english_ ? "HMP sample rate set to %1 Hz" : "HMP采样频率已设置为 %1 Hz").arg(state_->hmp_sample_rate_));
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
        log(state_->is_english_ ? "Lidar output-rate command disabled; using device default/adaptive output" : "已禁用激光测距仪输出频率下发，使用设备默认/自适应输出");
    }
    else
    {
        log(QString(state_->is_english_ ? "Lidar sample rate set to %1 Hz" : "激光测距仪采样频率已设置为 %1 Hz").arg(state_->lidar_sample_rate_));
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
        log(state_->is_english_
            ? "RD105 polling-rate selection left unset; using the default host polling rate."
            : "RD105 轮询频率保持不设定，使用默认主机轮询频率。");
    }
    else
    {
        log(QString(state_->is_english_ ? "RD105 polling rate set to %1 Hz" : "RD105 轮询频率已设置为 %1 Hz").arg(state_->temperature_sample_rate_));
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
    bool epsilonUsesCustomPacketRates = false;
    const int epsilonRate = skipEpsilonDeviceRate ? kDefaultEpsilonSampleRateHz : std::clamp(rate, 20, 200);
    const int ptbRate = skipPtbDeviceRate ? kDefaultPtbSampleRateHz : clampPtbSampleRate(rate);
    const int hmpRate = skipHmpDeviceRate ? kDefaultHmpSampleRateHz : rate;
    const int lidarRate = skipLidarDeviceRate ? kDefaultLidarSampleRateHz : std::min(rate, 100);
    const int temperatureRate = skipTemperatureDeviceRate ? kDefaultTemperatureSampleRateHz : std::min(rate, kMaxTemperatureSampleRateHz);
    const std::map<uint8_t, int> epsilonDesiredPacketRates =
        effectiveEpsilonPacketRates(settings, epsilonRate, &epsilonUsesCustomPacketRates);
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
    const LocalSampleRateApplyResult rateResult =
        state_->local_connection_controller_->applyRunningSampleRates(configuration);
    const bool epsilonDeviceRateFailed =
        rateResult.epsilonDeviceRateAttempted && !rateResult.epsilonDeviceRateSucceeded;
    const bool ptbDeviceRateFailed =
        rateResult.ptbDeviceRateAttempted && !rateResult.ptbDeviceRateSucceeded;
    if (epsilonDeviceRateFailed)
    {
        log(state_->is_english_
            ? "EPSILON output-rate command failed; the live stream recovery result is shown above."
            : "EPSILON 输出频率下发失败；实时数据流恢复结果请查看上方日志。");
    }
    if (ptbDeviceRateFailed)
    {
        log(QString(state_->is_english_
            ? "PTB sample rate command failed for %1 Hz"
            : "PTB采样频率命令下发失败：%1 Hz").arg(ptbRate));
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
        log(state_->is_english_
            ? "Host-side rates were updated, but one or more device output-rate commands failed."
            : "主机侧频率已更新，但一个或多个设备输出频率命令失败。");
    }
    else
    {
        log(QString(state_->is_english_ ? "All rates set to %1 Hz" : "所有频率已设置为 %1 Hz").arg(rate));
    }
    if (skipEpsilonDeviceRate || skipPtbDeviceRate || skipHmpDeviceRate || skipLidarDeviceRate || skipTemperatureDeviceRate)
    {
        log(state_->is_english_
            ? "Devices set to No Set keep their output-rate commands disabled."
            : "已选择“不设定”的设备保持不下发输出频率命令。");
    }
    if (!skipPtbDeviceRate && ptbRate != rate)
    {
        log(QString(state_->is_english_
            ? "PTB sample rate capped at %1 Hz"
            : "PTB采样频率已限制为 %1 Hz").arg(ptbRate));
    }
    if (!skipTemperatureDeviceRate && temperatureRate != rate)
    {
        log(QString(state_->is_english_
            ? "RD105 polling rate capped at %1 Hz"
            : "RD105 轮询频率已限制为 %1 Hz").arg(temperatureRate));
    }
}
