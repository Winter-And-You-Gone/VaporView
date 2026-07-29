#include "ground/devices/LocalDeviceConnectionController.h"

#include "ground/devices/ImuConfigurationService.h"

#include <QSettings>
#include "shared/config/SettingsWriteBarrier.h"

#include <atomic>
#include <thread>
#include <utility>

namespace VaporView::Ground::Devices
{

class LocalDeviceConnectionController::Impl
{
public:
    ~Impl()
    {
        requestCancel();
        wait();
        registry.stopAll();
    }

    bool connectAsync(LocalConnectionRequest request)
    {
        if (inProgress.exchange(true))
        {
            return false;
        }
        wait();
        cancel.store(false);
        english.store(request.english);
        registry.stopAll();
        worker = std::thread([this, request = std::move(request)]() mutable {
            run(std::move(request));
        });
        return true;
    }

    bool connectTemperatureAsync(
        LocalTemperatureConnectionRequest request,
        std::function<void(bool, const QString&)> completion)
    {
        if (inProgress.exchange(true))
        {
            return false;
        }
        wait();
        cancel.store(false);
        english.store(request.english);
        worker = std::thread(
            [this, request = std::move(request), completion = std::move(completion)]() mutable {
                runTemperature(std::move(request), std::move(completion));
            });
        return true;
    }

    void requestCancel()
    {
        cancel.store(true);
    }

    void wait()
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }

    void disconnect()
    {
        requestCancel();
        wait();
        registry.stopAll();
        inProgress.store(false);
        cancel.store(false);
    }

    LocalSampleRateApplyResult applyRunningSampleRates(
        const LocalSampleRateConfiguration& configuration)
    {
        LocalSampleRateApplyResult result;
        const CollectorSet collectors = registry.snapshot();
        if (collectors.epsilon && collectors.epsilon->isRunning())
        {
            collectors.epsilon->setSampleRate(configuration.epsilonCallbackRateHz);
            if (configuration.applyEpsilonDeviceRate)
            {
                result.epsilonDeviceRateAttempted = true;
                result.epsilonDeviceRateSucceeded =
                    collectors.epsilon->setOutputPacketRates(configuration.epsilonPacketRates);
            }
        }
        if (collectors.ptb && collectors.ptb->isRunning())
        {
            collectors.ptb->setSampleRate(configuration.ptbRateHz);
            if (configuration.applyPtbDeviceRate)
            {
                result.ptbDeviceRateAttempted = true;
                result.ptbDeviceRateSucceeded =
                    collectors.ptb->setDeviceSampleRate(configuration.ptbRateHz);
            }
        }
        if (collectors.hmp && collectors.hmp->isRunning())
        {
            collectors.hmp->setSampleRate(configuration.hmpRateHz);
        }
        if (collectors.lidar && collectors.lidar->isRunning())
        {
            collectors.lidar->setSampleRate(configuration.lidarRateHz);
            if (configuration.applyLidarDeviceRate)
            {
                collectors.lidar->setDeviceSampleRate(configuration.lidarRateHz);
            }
        }
        if (collectors.temperature_controller && collectors.temperature_controller->isRunning())
        {
            collectors.temperature_controller->setSampleRate(configuration.temperatureRateHz);
        }
        return result;
    }

    LocalSampleRateApplyResult setEpsilonSampleRate(
        int callbackRateHz,
        const std::map<uint8_t, int>& packetRates,
        bool applyDeviceRate)
    {
        LocalSampleRateApplyResult result;
        const auto collector = registry.snapshot().epsilon;
        if (!collector)
        {
            return result;
        }
        collector->setSampleRate(callbackRateHz);
        if (collector->isRunning() && applyDeviceRate)
        {
            result.epsilonDeviceRateAttempted = true;
            result.epsilonDeviceRateSucceeded = collector->setOutputPacketRates(packetRates);
        }
        return result;
    }

    LocalSampleRateApplyResult setPtbSampleRate(int rateHz, bool applyDeviceRate)
    {
        LocalSampleRateApplyResult result;
        const auto collector = registry.snapshot().ptb;
        if (!collector)
        {
            return result;
        }
        collector->setSampleRate(rateHz);
        if (collector->isRunning() && applyDeviceRate)
        {
            result.ptbDeviceRateAttempted = true;
            result.ptbDeviceRateSucceeded = collector->setDeviceSampleRate(rateHz);
        }
        return result;
    }

    void setHmpSampleRate(int rateHz)
    {
        const auto collector = registry.snapshot().hmp;
        if (collector)
        {
            collector->setSampleRate(rateHz);
        }
    }

    void setLidarSampleRate(int rateHz, bool applyDeviceRate)
    {
        const auto collector = registry.snapshot().lidar;
        if (!collector)
        {
            return;
        }
        collector->setSampleRate(rateHz);
        if (collector->isRunning() && applyDeviceRate)
        {
            collector->setDeviceSampleRate(rateHz);
        }
    }

    void setTemperatureSampleRate(int rateHz)
    {
        const auto collector = registry.snapshot().temperature_controller;
        if (collector)
        {
            collector->setSampleRate(rateHz);
        }
    }

    bool applyImuProfile(const ImuProfileRequest& request)
    {
        return ImuConfigurationService::apply(
            request,
            registry.snapshot().imu,
            [this](const QString& message) { postLog(message); });
    }

    LocalTemperatureCommandResult sendTemperatureCommand(
        CommandId command,
        const TemperatureControllerCommand& payload)
    {
        LocalTemperatureCommandResult result;
        const auto collector = registry.snapshot().temperature_controller;
        if (!collector || !collector->isRunning())
        {
            return result;
        }

        const quint8 channel = payload.channel == 0 ? 1 : payload.channel;
        bool confirmed = false;
        switch (command)
        {
        case CommandId::SetTemperatureTarget:
            confirmed = collector->setTargetTemperature(channel, payload.target_temperature_c);
            break;
        case CommandId::SetTemperatureOutputEnabled:
            confirmed = collector->setOutputEnabled(channel, payload.output_enabled);
            break;
        case CommandId::SetTemperatureOutputMode:
            confirmed = collector->setOutputMode(channel, payload.output_mode);
            break;
        case CommandId::SetTemperatureMaxOutputPercent:
            confirmed = collector->setMaxOutputPercent(channel, payload.max_output_percent);
            break;
        case CommandId::SetTemperaturePid:
            confirmed = collector->setPid(channel, payload.kp, payload.ki, payload.kd);
            break;
        case CommandId::SetTemperatureAutoPid:
            confirmed = collector->setAutoPid(channel, payload.auto_pid_mode);
            break;
        case CommandId::SetTemperatureOvertempUpper:
            confirmed = collector->setOvertempUpper(channel, payload.overtemp_upper_c);
            break;
        case CommandId::SetTemperatureOvertempLower:
            confirmed = collector->setOvertempLower(channel, payload.overtemp_lower_c);
            break;
        case CommandId::SetTemperatureSlope:
            confirmed = collector->setTemperatureSlope(channel, payload.temperature_slope_c_per_s);
            break;
        case CommandId::SetTemperatureStartupDelay:
            confirmed = collector->setStartupDelay(channel, payload.startup_delay_s);
            break;
        case CommandId::SetTemperatureSensorConfig:
            confirmed = collector->setSensorConfig(
                channel,
                payload.sensor_model,
                payload.ntc_b,
                payload.ntc_r0,
                payload.pt_r0,
                payload.pt_a,
                payload.pt_b,
                payload.pt_c,
                payload.polynomial_mantissas,
                payload.polynomial_exponents);
            break;
        case CommandId::SetTemperatureControllerMode:
            confirmed = collector->setControllerMode(payload.controller_mode);
            break;
        case CommandId::SetTemperatureDeviceAddress:
            confirmed = collector->setDeviceAddress(payload.device_address);
            break;
        case CommandId::SetTemperatureRs485Baud:
            confirmed = collector->setRs485BaudIndex(payload.rs485_baud_index);
            break;
        case CommandId::SetTemperatureOvertempOutputMode:
            confirmed = collector->setOvertempOutputMode(payload.overtemp_output_mode);
            break;
        case CommandId::RestoreTemperatureFactoryDefaults:
            confirmed = collector->restoreFactoryDefaults();
            break;
        default:
            break;
        }

        result.status = confirmed
            ? LocalTemperatureCommandStatus::Confirmed
            : LocalTemperatureCommandStatus::Rejected;
        result.latestData = collector->getLatestData();
        return result;
    }

private:
    void postLog(const QString& message) const
    {
        if (callbacks.log)
        {
            callbacks.log(message);
        }
    }

    void finish(bool connected)
    {
        inProgress.store(false);
        cancel.store(false);
        if (callbacks.finished)
        {
            callbacks.finished(connected);
        }
    }

    void notifyData(LocalDeviceKind device) const
    {
        if (callbacks.dataReady)
        {
            callbacks.dataReady(device);
        }
    }

    void runTemperature(
        LocalTemperatureConnectionRequest request,
        std::function<void(bool, const QString&)> completion)
    {
        auto collector = std::make_shared<TemperatureControllerCollector>();
        collector->setEnglish(request.english);
        collector->setSampleRate(request.sampleRateHz);
        collector->setSlaveAddress(static_cast<uint8_t>(request.slaveAddress));
        collector->setDataCallback([this]() {
            notifyData(LocalDeviceKind::TemperatureController);
        });
        collector->setLogCallback([this](const std::string& message) {
            postLog(QString::fromStdString(message));
        });
        collector->setCancelCallback([this]() { return cancel.load(); });

        bool connected = false;
        const bool opened = collector->start(
            request.port.toStdString(),
            SerialConfig::N81(request.baudRate));
        QString resultText;
        if (!opened)
        {
            resultText = QString(request.english
                    ? "[RD105] Failed to open %1: %2"
                    : "[RD105] 打开 %1 失败：%2")
                .arg(request.port, QString::fromStdString(collector->getLastError()));
        }
        else if (cancel.load())
        {
            resultText = request.english
                ? QStringLiteral("[RD105] Connection canceled.")
                : QStringLiteral("[RD105] 已取消连接。");
        }
        else if (!collector->checkDeviceResponse())
        {
            resultText = cancel.load()
                ? (request.english
                       ? QStringLiteral("[RD105] Connection canceled.")
                       : QStringLiteral("[RD105] 已取消连接。"))
                : (request.english
                       ? QStringLiteral("[RD105] Initialization failed; see the preceding log for details.")
                       : QStringLiteral("[RD105] 初始化失败，请查看上方日志。"));
        }
        else if (!collector->startStreaming())
        {
            resultText = request.english
                ? QStringLiteral("[RD105] Failed to start polling.")
                : QStringLiteral("[RD105] 启动轮询失败。");
        }
        else
        {
            auto previous = registry.replaceTemperatureController(collector, request.english);
            if (previous)
            {
                previous->stop();
            }
            connected = true;
            resultText = QString(request.english
                    ? "[RD105] Connected: %1 @ %2, address %3, polling %4 Hz%5"
                    : "[RD105] 已连接：%1 @ %2，站号 %3，轮询 %4 Hz%5")
                .arg(request.port, request.baudText)
                .arg(request.slaveAddress)
                .arg(request.sampleRateHz)
                .arg(request.usesDefaultRate
                        ? (request.english ? " (default)" : "（默认）")
                        : QString());
        }

        if (!connected && opened)
        {
            collector->stop();
        }
        inProgress.store(false);
        cancel.store(false);
        if (completion)
        {
            completion(connected, resultText);
        }
    }

    void run(LocalConnectionRequest request)
    {
        const bool useEnglish = request.english;
        auto persistEpsilonConfig = [&request]() {
            QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
            VaporView::setPersistentSetting(settings, QStringLiteral("epsilon_last_config_port"), request.epsilon.port);
            VaporView::setPersistentSetting(settings, QStringLiteral("epsilon_last_config_baud"), request.epsilon.baudText);
            VaporView::setPersistentSetting(settings, QStringLiteral("epsilon_last_config_rate_hz"), request.epsilonConfiguredRateHz);
            VaporView::setPersistentSetting(settings, QStringLiteral("epsilon_last_config_signature"), request.epsilonPacketRateSignature);
            VaporView::setPersistentSetting(settings, QStringLiteral("epsilon_last_config_apply_version"), 2);
        };

        CollectorSet collectors;
        collectors.epsilon = std::make_shared<EpsilonCollector>();
        collectors.ptb = std::make_shared<PtbCollector>();
        collectors.hmp = std::make_shared<HmpCollector>();
        collectors.ptb->setProtocol(request.pressureProtocol);
        collectors.hmp->setProtocol(request.humidityProtocol);
        collectors.lidar = std::make_shared<LidarCollector>();
        collectors.temperature_controller = std::make_shared<TemperatureControllerCollector>();
        registry.replaceAll(collectors, useEnglish);

        auto collectorLog = [this](const std::string& message) {
            postLog(QString::fromStdString(message));
        };
        auto cancelRequested = [this]() {
            return cancel.load();
        };
        collectors.epsilon->setSampleRate(request.epsilon.sampleRateHz);
        collectors.ptb->setSampleRate(request.ptb.sampleRateHz);
        collectors.hmp->setSampleRate(request.hmp.sampleRateHz);
        collectors.lidar->setSampleRate(request.lidar.sampleRateHz);
        collectors.temperature_controller->setSampleRate(request.temperatureController.sampleRateHz);
        collectors.temperature_controller->setSlaveAddress(
            static_cast<uint8_t>(request.temperatureSlaveAddress));

        collectors.epsilon->setLogCallback(collectorLog);
        collectors.ptb->setLogCallback(collectorLog);
        collectors.hmp->setLogCallback(collectorLog);
        collectors.lidar->setLogCallback(collectorLog);
        collectors.temperature_controller->setLogCallback(collectorLog);
        collectors.epsilon->setCancelCallback(cancelRequested);
        collectors.ptb->setCancelCallback(cancelRequested);
        collectors.hmp->setCancelCallback(cancelRequested);
        collectors.lidar->setCancelCallback(cancelRequested);
        collectors.temperature_controller->setCancelCallback(cancelRequested);

        collectors.epsilon->setRawFrameCallback(
            [this](uint64_t timestampUs,
                   uint8_t packetId,
                   uint8_t serialNumber,
                   const uint8_t *data,
                   size_t size) {
                if (callbacks.rawEpsilonFrame)
                {
                    callbacks.rawEpsilonFrame(timestampUs, packetId, serialNumber, data, size);
                }
            });
        collectors.ptb->setRawResponseCallback(
            [this](uint64_t timestampUs, const uint8_t *data, size_t size) {
                if (callbacks.rawPtbResponse)
                {
                    callbacks.rawPtbResponse(timestampUs, data, size);
                }
            });
        collectors.hmp->setRawResponseCallback(
            [this](uint64_t timestampUs, const uint8_t *data, size_t size) {
                if (callbacks.rawHmpResponse)
                {
                    callbacks.rawHmpResponse(timestampUs, data, size);
                }
            });
        collectors.lidar->setRawFrameCallback(
            [this](uint64_t timestampUs, LidarProtocol protocol, const uint8_t *data, size_t size) {
                if (callbacks.rawLidarFrame)
                {
                    callbacks.rawLidarFrame(timestampUs,
                                            static_cast<quint16>(protocol),
                                            data,
                                            size);
                }
            });

        int totalDevices = 0;
        int connectedDevices = 0;
        auto cancelAttempt = [&]() {
            registry.stopAll();
            postLog(useEnglish ? QStringLiteral("Connection canceled")
                               : QStringLiteral("连接已取消"));
            finish(false);
        };
        auto abortIfRequested = [&]() {
            if (!cancel.load())
            {
                return false;
            }
            cancelAttempt();
            return true;
        };
        auto connectCollector = [&](const QString& tag,
                                    const LocalSerialDeviceSettings& settings,
                                    auto *collector,
                                    const SerialConfig& serialConfig,
                                    auto&& onReady) -> int {
            if (settings.port == request.selectText || settings.port.isEmpty())
            {
                postLog(QString(useEnglish ? "[%1] Skipped (not selected)" : "[%1] 跳过 (未选择)").arg(tag));
                return 0;
            }

            ++totalDevices;
            postLog(QString(useEnglish ? "[%1] Checking port: %2" : "[%1] 检查端口: %2")
                        .arg(tag, settings.port));
            if (abortIfRequested()) return -1;

            postLog(QString(useEnglish ? "[%1] Port selected, connecting..." : "[%1] 已选择端口，正在连接...").arg(tag));
            if (abortIfRequested()) return -1;

            if (!collector->start(settings.port.toStdString(), serialConfig))
            {
                postLog(QString(useEnglish ? "[%1] Failed to open port: %2" : "[%1] 打开端口失败: %2")
                            .arg(tag, QString::fromStdString(collector->getLastError())));
                return 0;
            }

            postLog(QString(useEnglish ? "[%1] Serial port opened, checking device response..."
                                       : "[%1] 串口已打开，正在检测设备响应...").arg(tag));
            if (abortIfRequested()) return -1;

            if (!collector->checkDeviceResponse())
            {
                if (abortIfRequested()) return -1;
                if (tag == QStringLiteral("RD105"))
                {
                    postLog(useEnglish
                        ? QStringLiteral("[RD105] Initialization failed; see the preceding log for details.")
                        : QStringLiteral("[RD105] 初始化失败，请查看上方日志。"));
                }
                else
                {
                    postLog(QString(useEnglish
                        ? "[%1] Device not responding! Check power and cables."
                        : "[%1] 设备无响应！请检查电源和连接线。").arg(tag));
                }
                collector->stop();
                return 0;
            }

            postLog(QString(useEnglish
                ? "[%1] Device responding, connected: %2 @ %3 baud"
                : "[%1] 设备响应正常，连接成功: %2 @ %3 波特率")
                    .arg(tag, settings.port, settings.baudText));
            if (!onReady())
            {
                collector->stop();
                return 0;
            }
            ++connectedDevices;
            return 1;
        };

        postLog(useEnglish ? QStringLiteral("========== Starting Connection ==========")
                           : QStringLiteral("========== 开始连接 =========="));
        if (abortIfRequested()) return;

        if (connectCollector(QStringLiteral("EPSILON"),
                             request.epsilon,
                             collectors.epsilon.get(),
                             SerialConfig::N81(request.epsilon.baudText.toInt()),
                             [&]() {
            collectors.epsilon->setDataCallback([this]() { notifyData(LocalDeviceKind::Epsilon); });
            if (request.epsilon.skipDeviceRate)
            {
                postLog(useEnglish
                    ? QStringLiteral("[EPSILON] Skip output-rate command; using the current device output.")
                    : QStringLiteral("[EPSILON] 跳过输出频率下发，使用设备当前输出。"));
            }
            else if (request.epsilonConfigLikelyMatches)
            {
                postLog(QString(useEnglish
                    ? "[EPSILON] Using the last saved %1 profile; skipping automatic reconfiguration."
                    : "[EPSILON] 使用上次已保存的%1配置，跳过自动重配。")
                        .arg(request.epsilonUsesCustomPacketRates
                            ? (useEnglish ? "custom packet-rate" : "自定义包频率")
                            : (useEnglish ? "grouped output-rate" : "分组输出频率")));
            }
            else if (!collectors.epsilon->setOutputPacketRates(request.epsilonPacketRates))
            {
                postLog(QString(useEnglish
                    ? "[EPSILON] Failed to configure the %1 profile: %2"
                    : "[EPSILON] 配置%1失败：%2")
                        .arg(request.epsilonUsesCustomPacketRates
                            ? (useEnglish ? "custom packet-rate" : "自定义包频率")
                            : (useEnglish ? "grouped output-rate" : "分组输出频率"))
                        .arg(request.epsilonPacketRateSummary));
                return false;
            }
            else
            {
                persistEpsilonConfig();
                postLog(QString(useEnglish
                    ? "[EPSILON] Applied %1 profile: %2"
                    : "[EPSILON] 已应用%1配置：%2")
                        .arg(request.epsilonUsesCustomPacketRates
                            ? (useEnglish ? "custom packet-rate" : "自定义包频率")
                            : (useEnglish ? "grouped output-rate" : "分组输出频率"))
                        .arg(request.epsilonPacketRateSummary));
            }
            if (collectors.epsilon->startStreaming()) return true;
            postLog(useEnglish ? QStringLiteral("[EPSILON] Failed to start navigation stream.")
                               : QStringLiteral("[EPSILON] 启动导航数据流失败。"));
            return false;
        }) < 0) return;

        const bool useBmp390 = request.pressureProtocol == PressureSensorProtocol::Bmp390Serial;
        const QString pressureName = useBmp390 ? QStringLiteral("BMP390") : QStringLiteral("PTB210");
        if (connectCollector(pressureName,
                             request.ptb,
                             collectors.ptb.get(),
                             useBmp390 ? SerialConfig::N81(request.ptb.baudText.toInt())
                                       : SerialConfig::E71(request.ptb.baudText.toInt()),
                             [&]() {
            collectors.ptb->setDataCallback([this]() { notifyData(LocalDeviceKind::Ptb); });
            if (request.ptb.skipDeviceRate)
            {
                postLog(useEnglish ? QStringLiteral("[PTB] Skip sample-rate command; using the current device output.")
                                   : QStringLiteral("[PTB] 跳过采样频率下发，使用设备当前输出。"));
            }
            else if (!collectors.ptb->setDeviceSampleRate(request.ptb.sampleRateHz))
            {
                postLog(QString(useEnglish ? "[PTB] Failed to set sample rate to %1 Hz."
                                           : "[PTB] 采样频率设置为 %1 Hz 失败。")
                            .arg(request.ptb.sampleRateHz));
                return false;
            }
            else
            {
                postLog(QString(useEnglish ? "[PTB] Sample rate set to %1 Hz"
                                           : "[PTB] 采样频率设置为 %1 Hz")
                            .arg(request.ptb.sampleRateHz));
            }
            if (collectors.ptb->startStreaming()) return true;
            postLog(useEnglish ? QStringLiteral("[PTB] Failed to start data stream.")
                               : QStringLiteral("[PTB] 启动数据流失败。"));
            return false;
        }) < 0) return;

        const bool useSht45 = request.humidityProtocol == HumiditySensorProtocol::Sht45Serial;
        const QString humidityName = useSht45 ? QStringLiteral("SHT45") : QStringLiteral("HMP3");
        if (connectCollector(humidityName,
                             request.hmp,
                             collectors.hmp.get(),
                             useSht45 ? SerialConfig::N81(request.hmp.baudText.toInt())
                                      : SerialConfig::N82(request.hmp.baudText.toInt()),
                             [&]() {
            collectors.hmp->setDataCallback([this]() { notifyData(LocalDeviceKind::Hmp); });
            if (request.hmp.skipDeviceRate)
            {
                postLog(useEnglish
                    ? QStringLiteral("[HMP] Polling-rate selection left unset; using the default host polling rate.")
                    : QStringLiteral("[HMP] 轮询频率保持不设定，使用默认主机轮询频率。"));
            }
            else
            {
                postLog(QString(useEnglish ? "[HMP] Sample rate set to %1 Hz"
                                           : "[HMP] 采样频率设置为 %1 Hz")
                            .arg(request.hmp.sampleRateHz));
            }
            if (collectors.hmp->startStreaming()) return true;
            postLog(useEnglish ? QStringLiteral("[HMP] Failed to start data stream.")
                               : QStringLiteral("[HMP] 启动数据流失败。"));
            return false;
        }) < 0) return;

        if (connectCollector(QStringLiteral("LIDAR"),
                             request.lidar,
                             collectors.lidar.get(),
                             SerialConfig::N81(request.lidar.baudText.toInt()),
                             [&]() {
            collectors.lidar->setDataCallback([this]() { notifyData(LocalDeviceKind::Lidar); });
            if (request.lidar.skipDeviceRate)
            {
                postLog(useEnglish
                    ? QStringLiteral("[Lidar] Skip output-rate command; using device default/adaptive output.")
                    : QStringLiteral("[Lidar] 跳过输出频率下发，使用设备默认/自适应输出。"));
            }
            else if (!collectors.lidar->setDeviceSampleRate(request.lidar.sampleRateHz))
            {
                postLog(QString(useEnglish
                    ? "[Lidar] Failed to apply output rate %1 Hz, using device default."
                    : "[Lidar] 应用 %1 Hz 输出频率失败，使用设备默认输出。")
                        .arg(request.lidar.sampleRateHz));
            }
            else
            {
                postLog(QString(useEnglish
                    ? "[Lidar] Output rate set to %1 Hz or host-side limit updated"
                    : "[Lidar] 输出频率已设置为 %1 Hz，或已更新主机侧限频")
                        .arg(request.lidar.sampleRateHz));
            }
            if (collectors.lidar->startStreaming()) return true;
            postLog(useEnglish ? QStringLiteral("[Lidar] Failed to start data stream.")
                               : QStringLiteral("[Lidar] 启动数据流失败。"));
            return false;
        }) < 0) return;

        if (connectCollector(QStringLiteral("RD105"),
                             request.temperatureController,
                             collectors.temperature_controller.get(),
                             SerialConfig::N81(request.temperatureController.baudText.toInt()),
                             [&]() {
            collectors.temperature_controller->setDataCallback(
                [this]() { notifyData(LocalDeviceKind::TemperatureController); });
            if (request.temperatureController.skipDeviceRate)
            {
                postLog(useEnglish
                    ? QStringLiteral("[RD105] Polling-rate selection left unset; using the default host polling rate.")
                    : QStringLiteral("[RD105] 轮询频率保持不设定，使用默认主机轮询频率。"));
            }
            else
            {
                postLog(QString(useEnglish ? "[RD105] Polling rate set to %1 Hz"
                                           : "[RD105] 轮询频率设置为 %1 Hz")
                            .arg(request.temperatureController.sampleRateHz));
            }
            if (collectors.temperature_controller->startStreaming()) return true;
            postLog(useEnglish
                ? QStringLiteral("[RD105] Failed to start temperature controller polling.")
                : QStringLiteral("[RD105] 启动温控器轮询失败。"));
            return false;
        }) < 0) return;

        postLog(QString(useEnglish
            ? "========== Connection Summary: %1/%2 devices connected =========="
            : "========== 连接摘要: %1/%2 设备已连接 ==========")
                .arg(connectedDevices)
                .arg(totalDevices));
        if (connectedDevices == 0)
        {
            postLog(useEnglish ? QStringLiteral("No ports connected")
                               : QStringLiteral("没有端口连接成功"));
            finish(false);
            return;
        }
        finish(true);
    }

public:
    CollectorRegistry registry;
    LocalConnectionCallbacks callbacks;
    std::thread worker;
    std::atomic<bool> inProgress{false};
    std::atomic<bool> cancel{false};
    std::atomic<bool> english{false};
};

LocalDeviceConnectionController::LocalDeviceConnectionController()
    : impl_(std::make_unique<Impl>())
{
}

LocalDeviceConnectionController::~LocalDeviceConnectionController() = default;

void LocalDeviceConnectionController::setCallbacks(LocalConnectionCallbacks callbacks)
{
    impl_->callbacks = std::move(callbacks);
}

bool LocalDeviceConnectionController::connectAsync(LocalConnectionRequest request)
{
    if (VaporView::settingsWritesSuspended())
    {
        return false;
    }
    return impl_->connectAsync(std::move(request));
}

bool LocalDeviceConnectionController::connectTemperatureAsync(
    LocalTemperatureConnectionRequest request,
    std::function<void(bool, const QString&)> completion)
{
    if (VaporView::settingsWritesSuspended())
    {
        if (completion) completion(false, QStringLiteral("UI test mode blocks serial I/O."));
        return false;
    }
    return impl_->connectTemperatureAsync(
        std::move(request),
        std::move(completion));
}

void LocalDeviceConnectionController::requestCancel()
{
    impl_->requestCancel();
}

void LocalDeviceConnectionController::disconnect()
{
    impl_->disconnect();
}

void LocalDeviceConnectionController::wait()
{
    impl_->wait();
}

bool LocalDeviceConnectionController::connectionInProgress() const
{
    return impl_->inProgress.load();
}

bool LocalDeviceConnectionController::cancelRequested() const
{
    return impl_->cancel.load();
}

bool LocalDeviceConnectionController::anyCollectorRunning() const
{
    return impl_->registry.anyRunning();
}

CollectorSet LocalDeviceConnectionController::snapshotCollectors() const
{
    return impl_->registry.snapshot();
}

LocalSampleRateApplyResult LocalDeviceConnectionController::applyRunningSampleRates(
    const LocalSampleRateConfiguration& configuration)
{
    return impl_->applyRunningSampleRates(configuration);
}

LocalSampleRateApplyResult LocalDeviceConnectionController::setEpsilonSampleRate(
    int callbackRateHz,
    const std::map<uint8_t, int>& packetRates,
    bool applyDeviceRate)
{
    return impl_->setEpsilonSampleRate(callbackRateHz, packetRates, applyDeviceRate);
}

LocalSampleRateApplyResult LocalDeviceConnectionController::setPtbSampleRate(
    int rateHz,
    bool applyDeviceRate)
{
    return impl_->setPtbSampleRate(rateHz, applyDeviceRate);
}

void LocalDeviceConnectionController::setHmpSampleRate(int rateHz)
{
    impl_->setHmpSampleRate(rateHz);
}

void LocalDeviceConnectionController::setLidarSampleRate(int rateHz, bool applyDeviceRate)
{
    impl_->setLidarSampleRate(rateHz, applyDeviceRate);
}

void LocalDeviceConnectionController::setTemperatureSampleRate(int rateHz)
{
    impl_->setTemperatureSampleRate(rateHz);
}

bool LocalDeviceConnectionController::applyImuProfile(const ImuProfileRequest& request)
{
    if (VaporView::settingsWritesSuspended())
    {
        return false;
    }
    return impl_->applyImuProfile(request);
}

LocalTemperatureCommandResult LocalDeviceConnectionController::sendTemperatureCommand(
    CommandId command,
    const TemperatureControllerCommand& payload)
{
    if (VaporView::settingsWritesSuspended())
    {
        return {};
    }
    return impl_->sendTemperatureCommand(command, payload);
}

bool LocalDeviceConnectionController::disconnectTemperatureController()
{
    auto collector = impl_->registry.takeTemperatureController();
    if (!collector)
    {
        return false;
    }
    collector->stop();
    return true;
}

}  // namespace VaporView::Ground::Devices
