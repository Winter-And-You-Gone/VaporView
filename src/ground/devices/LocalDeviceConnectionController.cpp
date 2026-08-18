#include "ground/devices/LocalDeviceConnectionController.h"

#include "ground/devices/EpsilonConfigurationService.h"
#include "ground/devices/ImuConfigurationService.h"

#include <QSettings>
#include "shared/config/ApplicationConfig.h"
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
            result.epsilonDeviceRateAttempted = true;
            result.epsilonDeviceRateSucceeded =
                collectors.epsilon->setOutputPacketRates(configuration.epsilonPacketRates);
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
        if (collectors.ai8_temperature_controller && collectors.ai8_temperature_controller->isRunning())
        {
            collectors.ai8_temperature_controller->setSampleRate(configuration.ai8TemperatureRateHz);
        }
        return result;
    }

    LocalSampleRateApplyResult setEpsilonSampleRate(
        int callbackRateHz,
        const std::map<uint8_t, int>& packetRates)
    {
        LocalSampleRateApplyResult result;
        const auto collector = registry.snapshot().epsilon;
        if (!collector)
        {
            return result;
        }
        collector->setSampleRate(callbackRateHz);
        if (collector->isRunning())
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

    void setAi8TemperatureSampleRate(int rateHz)
    {
        const auto collector = registry.snapshot().ai8_temperature_controller;
        if (collector)
        {
            collector->setSampleRate(rateHz);
        }
    }

    LocalAi8OperationResult readAi8Page(
        Ai8TemperatureControllerProtocol::Page page,
        const Ai8TemperatureControllerProtocol::Selection& selection)
    {
        LocalAi8OperationResult result;
        const auto collector = registry.snapshot().ai8_temperature_controller;
        if (!collector || !collector->isRunning())
        {
            result.message = english.load()
                ? QStringLiteral("AI-8288 is not connected.")
                : QStringLiteral("AI-8288 尚未连接。");
            return result;
        }
        result.success = collector->readPage(page, selection, result.data, &result.message);
        if (result.success)
        {
            result.message = english.load()
                ? QStringLiteral("Current page was read successfully.")
                : QStringLiteral("当前页读取成功。");
        }
        return result;
    }

    LocalAi8OperationResult writeAi8Page(
        const Ai8TemperatureControllerProtocol::PageData& data)
    {
        LocalAi8OperationResult result;
        const auto collector = registry.snapshot().ai8_temperature_controller;
        if (!collector || !collector->isRunning())
        {
            result.message = english.load()
                ? QStringLiteral("AI-8288 is not connected.")
                : QStringLiteral("AI-8288 尚未连接。");
            return result;
        }
        result.success = collector->writePage(data, &result.message);
        if (result.success)
        {
            QString readError;
            if (!collector->readPage(data.page, data.selection, result.data, &readError))
            {
                result.success = false;
                result.message = readError;
            }
        }
        return result;
    }

    bool applyImuProfile(const ImuProfileRequest& request)
    {
        return ImuConfigurationService::apply(
            request,
            registry.snapshot().imu,
            [this](const ImuConfigurationService::LogEntry& entry) {
                postConnectionLog(entry.level,
                                  entry.event,
                                  entry.message,
                                  entry.fields,
                                  entry.category);
            });
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
    void postConnectionLog(VaporView::LogLevel level,
                 const QString& event,
                 const QString& message,
                 QVariantMap fields = {},
                 const QString& category = QStringLiteral("device.connection")) const
    {
        if (callbacks.log)
        {
            fields.insert(QStringLiteral("ui_visibility"),
                          fields.value(QStringLiteral("ui_visibility"),
                                       level >= VaporView::LogLevel::Warning ? QStringLiteral("attention")
                                                                             : QStringLiteral("details")));
            LocalConnectionLogEntry entry;
            entry.level = level;
            entry.category = category;
            entry.event = event;
            entry.message = message;
            entry.fields = std::move(fields);
            callbacks.log(std::move(entry));
        }
    }

    void postCollectorDiagnostic(const std::string& message) const
    {
        postConnectionLog(VaporView::LogLevel::Debug,
                QStringLiteral("local_device_collector_diagnostic"),
                QStringLiteral("本地设备采集器输出了诊断信息。"),
                {{QStringLiteral("external_raw_text"), QString::fromStdString(message)},
                 {QStringLiteral("ui_visibility"), QStringLiteral("hidden")}});
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
            postCollectorDiagnostic(message);
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
            QSettings settings = VaporView::applicationConfigSettings();
            settings.beginGroup(QStringLiteral("MainWindow"));
            VaporView::setPersistentSetting(settings, QStringLiteral("epsilon_last_config_port"), request.epsilon.port);
            VaporView::setPersistentSetting(settings, QStringLiteral("epsilon_last_config_baud"), request.epsilon.baudText);
            VaporView::setPersistentSetting(settings, QStringLiteral("epsilon_last_config_rate_hz"), request.epsilonConfiguredRateHz);
            VaporView::setPersistentSetting(settings, QStringLiteral("epsilon_last_config_signature"), request.epsilonPacketRateSignature);
            VaporView::setPersistentSetting(
                settings,
                QStringLiteral("epsilon_last_config_apply_version"),
                VaporView::Ground::EpsilonConfigurationService::PacketConfigurationVersion);
        };

        CollectorSet collectors;
        collectors.epsilon = std::make_shared<EpsilonCollector>();
        collectors.ptb = std::make_shared<PtbCollector>();
        collectors.hmp = std::make_shared<HmpCollector>();
        collectors.ptb->setProtocol(request.pressureProtocol);
        collectors.hmp->setProtocol(request.humidityProtocol);
        collectors.lidar = std::make_shared<LidarCollector>();
        collectors.temperature_controller = std::make_shared<TemperatureControllerCollector>();
        collectors.ai8_temperature_controller = std::make_shared<Ai8TemperatureControllerCollector>();
        registry.replaceAll(collectors, useEnglish);

        auto collectorLog = [this](const std::string& message) {
            postCollectorDiagnostic(message);
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
        collectors.ai8_temperature_controller->setSampleRate(
            request.ai8TemperatureController.sampleRateHz);
        collectors.ai8_temperature_controller->setSlaveAddress(
            static_cast<uint8_t>(request.ai8SlaveAddress));

        collectors.epsilon->setLogCallback(collectorLog);
        collectors.ptb->setLogCallback(collectorLog);
        collectors.hmp->setLogCallback(collectorLog);
        collectors.lidar->setLogCallback(collectorLog);
        collectors.temperature_controller->setLogCallback(collectorLog);
        collectors.ai8_temperature_controller->setLogCallback(collectorLog);
        collectors.epsilon->setCancelCallback(cancelRequested);
        collectors.ptb->setCancelCallback(cancelRequested);
        collectors.hmp->setCancelCallback(cancelRequested);
        collectors.lidar->setCancelCallback(cancelRequested);
        collectors.temperature_controller->setCancelCallback(cancelRequested);
        collectors.ai8_temperature_controller->setCancelCallback(cancelRequested);

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
            postConnectionLog(VaporView::LogLevel::Info,
                    QStringLiteral("local_device_connection_cancelled"),
                    QStringLiteral("本地设备连接已取消。"),
                    {{QStringLiteral("reason_code"), QStringLiteral("USER_CANCELLED")}});
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
                postConnectionLog(VaporView::LogLevel::Info,
                        QStringLiteral("local_device_connection_skipped"),
                        QStringLiteral("本地设备未选择端口，已跳过连接。"),
                        {{QStringLiteral("device"), tag},
                         {QStringLiteral("reason_code"), QStringLiteral("PORT_NOT_SELECTED")}});
                return 0;
            }

            ++totalDevices;
            postConnectionLog(VaporView::LogLevel::Info,
                    QStringLiteral("local_device_port_check_started"),
                    QStringLiteral("开始检查本地设备串口。"),
                    {{QStringLiteral("device"), tag},
                     {QStringLiteral("port"), settings.port},
                     {QStringLiteral("baud"), settings.baudText}});
            if (abortIfRequested()) return -1;

            postConnectionLog(VaporView::LogLevel::Info,
                    QStringLiteral("local_device_connection_started"),
                    QStringLiteral("正在连接本地设备。"),
                    {{QStringLiteral("device"), tag},
                     {QStringLiteral("port"), settings.port},
                     {QStringLiteral("baud"), settings.baudText}});
            if (abortIfRequested()) return -1;

            if (!collector->start(settings.port.toStdString(), serialConfig))
            {
                postConnectionLog(VaporView::LogLevel::Warning,
                        QStringLiteral("local_device_port_open_failed"),
                        QStringLiteral("打开本地设备串口失败。"),
                        {{QStringLiteral("device"), tag},
                         {QStringLiteral("port"), settings.port},
                         {QStringLiteral("baud"), settings.baudText},
                         {QStringLiteral("error_code"), QStringLiteral("PORT_OPEN_FAILED")},
                         {QStringLiteral("system_error"), QString::fromStdString(collector->getLastError())}});
                return 0;
            }

            postConnectionLog(VaporView::LogLevel::Info,
                    QStringLiteral("local_device_response_check_started"),
                    QStringLiteral("本地设备串口已打开，正在检测设备响应。"),
                    {{QStringLiteral("device"), tag},
                     {QStringLiteral("port"), settings.port},
                     {QStringLiteral("baud"), settings.baudText}});
            if (abortIfRequested()) return -1;

            if (!collector->checkDeviceResponse())
            {
                if (abortIfRequested()) return -1;
                postConnectionLog(VaporView::LogLevel::Warning,
                        QStringLiteral("local_device_no_response"),
                        tag == QStringLiteral("RD105")
                            ? QStringLiteral("RD105 初始化失败，未通过设备响应检测。")
                            : QStringLiteral("本地设备无响应，请检查电源和连接线。"),
                        {{QStringLiteral("device"), tag},
                         {QStringLiteral("port"), settings.port},
                         {QStringLiteral("baud"), settings.baudText},
                         {QStringLiteral("reason_code"), QStringLiteral("NO_RESPONSE")}});
                collector->stop();
                return 0;
            }

            postConnectionLog(VaporView::LogLevel::Info,
                    QStringLiteral("local_device_connected"),
                    QStringLiteral("本地设备响应正常，连接成功。"),
                    {{QStringLiteral("device"), tag},
                     {QStringLiteral("port"), settings.port},
                     {QStringLiteral("baud"), settings.baudText}});
            if (!onReady())
            {
                collector->stop();
                return 0;
            }
            ++connectedDevices;
            return 1;
        };

        postConnectionLog(VaporView::LogLevel::Info,
                QStringLiteral("local_connection_phase_started"),
                QStringLiteral("开始本地连接流程。"),
                {{QStringLiteral("ui_visibility"), QStringLiteral("details")}});
        if (abortIfRequested()) return;

        if (connectCollector(QStringLiteral("EPSILON"),
                             request.epsilon,
                             collectors.epsilon.get(),
                             SerialConfig::N81(request.epsilon.baudText.toInt()),
                             [&]() {
            collectors.epsilon->setDataCallback([this]() { notifyData(LocalDeviceKind::Epsilon); });
            if (request.epsilonConfigLikelyMatches)
            {
                postConnectionLog(VaporView::LogLevel::Info,
                        QStringLiteral("epsilon_output_reconfigure_skipped_config_unchanged"),
                        QStringLiteral("EPSILON 输出配置与上次保存配置一致，已跳过自动重配。"),
                        {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                         {QStringLiteral("epsilon_packet_profile"), request.epsilonPacketRateSummary},
                         {QStringLiteral("reason_code"), QStringLiteral("CONFIG_UNCHANGED")}});
            }
            else if (request.epsilonPacketRatesMatchDefault &&
                     collectors.epsilon->lastDeviceResponseHadFdilinkFrame())
            {
                postConnectionLog(VaporView::LogLevel::Info,
                        QStringLiteral("epsilon_output_reconfigure_skipped_fdilink_detected"),
                        QStringLiteral("已检测到 EPSILON FDILink 数据流，跳过默认输出配置下发。"),
                        {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                         {QStringLiteral("epsilon_packet_profile"), request.epsilonPacketRateSummary},
                         {QStringLiteral("reason_code"), QStringLiteral("FDILINK_STREAM_DETECTED")}});
            }
            else if (!collectors.epsilon->setOutputPacketRates(request.epsilonPacketRates))
            {
                postConnectionLog(VaporView::LogLevel::Warning,
                        QStringLiteral("epsilon_output_reconfigure_failed"),
                        QStringLiteral("EPSILON 输出配置下发失败，继续使用设备当前输出。"),
                        {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                         {QStringLiteral("epsilon_packet_profile"), request.epsilonPacketRateSummary},
                         {QStringLiteral("error_code"), QStringLiteral("CONFIG_APPLY_FAILED")},
                         {QStringLiteral("fallback_action"), QStringLiteral("CURRENT_DEVICE_OUTPUT")},
                         {QStringLiteral("wiring_hint"), QStringLiteral("MAIN_PRIMARY_RS232_REQUIRED_FOR_CONFIGURATION")}});
            }
            else
            {
                persistEpsilonConfig();
                postConnectionLog(VaporView::LogLevel::Info,
                        QStringLiteral("epsilon_output_reconfigure_completed"),
                        QStringLiteral("EPSILON 输出配置已应用。"),
                        {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                         {QStringLiteral("epsilon_packet_profile"), request.epsilonPacketRateSummary}});
            }
            if (collectors.epsilon->startStreaming()) return true;
            postConnectionLog(VaporView::LogLevel::Error,
                    QStringLiteral("local_device_stream_start_failed"),
                    QStringLiteral("本地设备数据流启动失败。"),
                    {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                     {QStringLiteral("error_code"), QStringLiteral("STREAM_START_FAILED")}});
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
                postConnectionLog(VaporView::LogLevel::Info,
                        QStringLiteral("ptb_sample_rate_command_skipped"),
                        QStringLiteral("已跳过 PTB210 采样频率下发，使用设备当前输出。"),
                        {{QStringLiteral("device"), pressureName},
                         {QStringLiteral("reason_code"), QStringLiteral("RATE_UNSPECIFIED")}});
            }
            else if (!collectors.ptb->setDeviceSampleRate(request.ptb.sampleRateHz))
            {
                postConnectionLog(VaporView::LogLevel::Warning,
                        QStringLiteral("ptb_sample_rate_update_failed"),
                        QStringLiteral("PTB210 采样频率下发失败。"),
                        {{QStringLiteral("device"), pressureName},
                         {QStringLiteral("requested_rate_hz"), request.ptb.sampleRateHz},
                         {QStringLiteral("error_code"), QStringLiteral("SAMPLE_RATE_UPDATE_FAILED")}});
                return false;
            }
            else
            {
                postConnectionLog(VaporView::LogLevel::Info,
                        QStringLiteral("ptb_sample_rate_updated"),
                        QStringLiteral("PTB210 采样频率已更新。"),
                        {{QStringLiteral("device"), pressureName},
                         {QStringLiteral("requested_rate_hz"), request.ptb.sampleRateHz}});
            }
            if (collectors.ptb->startStreaming()) return true;
            postConnectionLog(VaporView::LogLevel::Error,
                    QStringLiteral("local_device_stream_start_failed"),
                    QStringLiteral("本地设备数据流启动失败。"),
                    {{QStringLiteral("device"), pressureName},
                     {QStringLiteral("error_code"), QStringLiteral("STREAM_START_FAILED")}});
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
                postConnectionLog(VaporView::LogLevel::Info,
                        QStringLiteral("hmp_polling_rate_defaulted"),
                        QStringLiteral("HMP3 轮询频率保持不设定，使用默认主机轮询频率。"),
                        {{QStringLiteral("device"), humidityName},
                         {QStringLiteral("effective_rate_hz"), request.hmp.sampleRateHz}});
            }
            else
            {
                postConnectionLog(VaporView::LogLevel::Info,
                        QStringLiteral("hmp_sample_rate_updated"),
                        QStringLiteral("HMP3 采样频率已更新。"),
                        {{QStringLiteral("device"), humidityName},
                         {QStringLiteral("requested_rate_hz"), request.hmp.sampleRateHz}});
            }
            if (collectors.hmp->startStreaming()) return true;
            postConnectionLog(VaporView::LogLevel::Error,
                    QStringLiteral("local_device_stream_start_failed"),
                    QStringLiteral("本地设备数据流启动失败。"),
                    {{QStringLiteral("device"), humidityName},
                     {QStringLiteral("error_code"), QStringLiteral("STREAM_START_FAILED")}});
            return false;
        }) < 0) return;

        if (connectCollector(QStringLiteral("TFA1005-L"),
                             request.lidar,
                             collectors.lidar.get(),
                             SerialConfig::N81(request.lidar.baudText.toInt()),
                             [&]() {
            collectors.lidar->setDataCallback([this]() { notifyData(LocalDeviceKind::Lidar); });
            if (request.lidar.skipDeviceRate)
            {
                postConnectionLog(VaporView::LogLevel::Info,
                        QStringLiteral("lidar_output_rate_command_skipped"),
                        QStringLiteral("已跳过激光测距仪输出频率下发，使用设备默认或自适应输出。"),
                        {{QStringLiteral("device"), QStringLiteral("TFA1005-L")},
                         {QStringLiteral("reason_code"), QStringLiteral("RATE_UNSPECIFIED")}});
            }
            else if (!collectors.lidar->setDeviceSampleRate(request.lidar.sampleRateHz))
            {
                postConnectionLog(VaporView::LogLevel::Warning,
                        QStringLiteral("lidar_output_rate_update_failed"),
                        QStringLiteral("激光测距仪输出频率下发失败，使用设备默认输出。"),
                        {{QStringLiteral("device"), QStringLiteral("TFA1005-L")},
                         {QStringLiteral("requested_rate_hz"), request.lidar.sampleRateHz},
                         {QStringLiteral("error_code"), QStringLiteral("OUTPUT_RATE_UPDATE_FAILED")}});
            }
            else
            {
                postConnectionLog(VaporView::LogLevel::Info,
                        QStringLiteral("lidar_output_rate_updated"),
                        QStringLiteral("激光测距仪输出频率已更新。"),
                        {{QStringLiteral("device"), QStringLiteral("TFA1005-L")},
                         {QStringLiteral("requested_rate_hz"), request.lidar.sampleRateHz}});
            }
            if (collectors.lidar->startStreaming()) return true;
            postConnectionLog(VaporView::LogLevel::Error,
                    QStringLiteral("local_device_stream_start_failed"),
                    QStringLiteral("本地设备数据流启动失败。"),
                    {{QStringLiteral("device"), QStringLiteral("TFA1005-L")},
                     {QStringLiteral("error_code"), QStringLiteral("STREAM_START_FAILED")}});
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
                postConnectionLog(VaporView::LogLevel::Info,
                        QStringLiteral("temperature_polling_rate_defaulted"),
                        QStringLiteral("RD105 轮询频率保持不设定，使用默认主机轮询频率。"),
                        {{QStringLiteral("device"), QStringLiteral("RD105")},
                         {QStringLiteral("effective_rate_hz"), request.temperatureController.sampleRateHz}});
            }
            else
            {
                postConnectionLog(VaporView::LogLevel::Info,
                        QStringLiteral("temperature_polling_rate_updated"),
                        QStringLiteral("RD105 轮询频率已更新。"),
                        {{QStringLiteral("device"), QStringLiteral("RD105")},
                         {QStringLiteral("requested_rate_hz"), request.temperatureController.sampleRateHz}});
            }
            if (collectors.temperature_controller->startStreaming()) return true;
            postConnectionLog(VaporView::LogLevel::Error,
                    QStringLiteral("local_device_stream_start_failed"),
                    QStringLiteral("本地设备数据流启动失败。"),
                    {{QStringLiteral("device"), QStringLiteral("RD105")},
                     {QStringLiteral("error_code"), QStringLiteral("STREAM_START_FAILED")}});
            return false;
        }) < 0) return;

        if (connectCollector(QStringLiteral("AI-8288"),
                             request.ai8TemperatureController,
                             collectors.ai8_temperature_controller.get(),
                             SerialConfig::N81(request.ai8TemperatureController.baudText.toInt()),
                             [&]() {
            collectors.ai8_temperature_controller->setDataCallback(
                [this]() { notifyData(LocalDeviceKind::Ai8TemperatureController); });
            postConnectionLog(VaporView::LogLevel::Info,
                    QStringLiteral("ai8_temperature_polling_rate_updated"),
                    QStringLiteral("AI-8288 主机轮询频率已更新。"),
                    {{QStringLiteral("device"), QStringLiteral("AI-8288")},
                     {QStringLiteral("requested_rate_hz"), request.ai8TemperatureController.sampleRateHz}});
            if (collectors.ai8_temperature_controller->startStreaming()) return true;
            postConnectionLog(VaporView::LogLevel::Error,
                    QStringLiteral("local_device_stream_start_failed"),
                    QStringLiteral("本地设备数据流启动失败。"),
                    {{QStringLiteral("device"), QStringLiteral("AI-8288")},
                     {QStringLiteral("error_code"), QStringLiteral("STREAM_START_FAILED")}});
            return false;
        }) < 0) return;

        postConnectionLog(VaporView::LogLevel::Info,
                QStringLiteral("local_serial_device_phase_completed"),
                QStringLiteral("本地串口设备连接阶段已完成。"),
                {{QStringLiteral("connected_devices"), connectedDevices},
                 {QStringLiteral("total_devices"), totalDevices}});
        if (connectedDevices == 0)
        {
            postConnectionLog(VaporView::LogLevel::Warning,
                    QStringLiteral("local_serial_devices_not_connected"),
                    QStringLiteral("没有串口设备连接成功。"),
                    {{QStringLiteral("connected_devices"), connectedDevices},
                     {QStringLiteral("total_devices"), totalDevices},
                     {QStringLiteral("reason_code"), QStringLiteral("NO_DEVICE_CONNECTED")}});
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
    const std::map<uint8_t, int>& packetRates)
{
    return impl_->setEpsilonSampleRate(callbackRateHz, packetRates);
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

void LocalDeviceConnectionController::setAi8TemperatureSampleRate(int rateHz)
{
    impl_->setAi8TemperatureSampleRate(rateHz);
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

LocalAi8OperationResult LocalDeviceConnectionController::readAi8Page(
    Ai8TemperatureControllerProtocol::Page page,
    const Ai8TemperatureControllerProtocol::Selection& selection)
{
    if (VaporView::settingsWritesSuspended())
    {
        return {};
    }
    return impl_->readAi8Page(page, selection);
}

LocalAi8OperationResult LocalDeviceConnectionController::writeAi8Page(
    const Ai8TemperatureControllerProtocol::PageData& data)
{
    if (VaporView::settingsWritesSuspended())
    {
        return {};
    }
    return impl_->writeAi8Page(data);
}

}  // namespace VaporView::Ground::Devices
