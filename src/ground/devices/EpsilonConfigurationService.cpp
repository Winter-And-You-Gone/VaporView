#include "ground/devices/EpsilonConfigurationService.h"

#include "data_collector.h"

#include <QSettings>
#include "shared/config/ApplicationConfig.h"
#include "shared/config/SettingsWriteBarrier.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace VaporView::Ground
{
namespace
{

void emitLog(const EpsilonConfigurationService::LogCallback& log,
             EpsilonConfigurationLogEntry entry)
{
    if (log)
    {
        log(std::move(entry));
    }
}

void emitLog(const EpsilonConfigurationService::LogCallback& log,
             LogLevel level,
             const QString& category,
             const QString& event,
             const QString& message,
             QVariantMap fields = QVariantMap())
{
    emitLog(log, {level, category, event, message, std::move(fields)});
}

std::shared_ptr<VaporView::EpsilonCollector> prepareCollector(
    const EpsilonDeviceOperation& operation,
    const EpsilonConfigurationService::LogCallback& log)
{
    std::shared_ptr<VaporView::EpsilonCollector> collector =
        operation.restart_live_stream && operation.live_collector
        ? operation.live_collector
        : std::make_shared<VaporView::EpsilonCollector>();

    collector->setEnglish(operation.english);
    collector->setLogCallback([log](const std::string& message) {
        emitLog(log,
                LogLevel::Info,
                QStringLiteral("device.collector"),
                QStringLiteral("epsilon_configuration_collector_output"),
                QStringLiteral("EPSILON 配置过程输出了采集器诊断信息。"),
                {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                 {QStringLiteral("process_output"), QString::fromStdString(message)},
                 {QStringLiteral("external_raw_text"), true},
                 {QStringLiteral("ui_visibility"), QStringLiteral("hidden")}});
    });
    return collector;
}

EpsilonConfigurationResult finishOperation(
    const EpsilonDeviceOperation& operation,
    const QString& operationName,
    const std::shared_ptr<VaporView::EpsilonCollector>& collector,
    EpsilonConfigurationResult result,
    const EpsilonConfigurationService::LogCallback& log)
{
    if (!operation.restart_live_stream)
    {
        collector->stop();
        result.live_stream_restarted = true;
        return result;
    }

    const bool english = operation.english;
    collector->stop();
    const bool reopened = collector->start(
        operation.port.toStdString(),
        VaporView::SerialConfig::N81(operation.baud));
    const bool responding = reopened && collector->checkDeviceResponse();
    const bool streaming = responding && collector->startStreaming();
    result.live_stream_restarted = streaming;

    if (streaming)
    {
        const bool maintenance = operationName == QStringLiteral("calibrate_maintenance");
        QVariantMap fields{{QStringLiteral("device"), QStringLiteral("EPSILON")},
                           {QStringLiteral("operation"), operationName},
                           {QStringLiteral("ui_visibility"), result.command_succeeded
                                ? QStringLiteral("details")
                                : QStringLiteral("attention")}};
        if (!result.command_succeeded)
        {
            fields.insert(QStringLiteral("error_code"), QStringLiteral("CONFIG_APPLY_FAILED"));
        }
        emitLog(log,
                result.command_succeeded ? LogLevel::Info : LogLevel::Error,
                QStringLiteral("device.navigation.command"),
                result.command_succeeded
                    ? QStringLiteral("epsilon_configuration_completed_live_stream_restored")
                    : QStringLiteral("epsilon_configuration_failed_live_stream_restored"),
                result.command_succeeded
                    ? (maintenance ? QStringLiteral("EPSILON 维护命令发送及保存阶段已结束，实时导航流已恢复；请查看确认状态并重启设备验证。")
                                   : QStringLiteral("EPSILON 配置已完成，实时导航流已恢复。"))
                    : QStringLiteral("EPSILON 配置失败，但原实时导航流已恢复。"),
                fields);
        return result;
    }

    collector->stop();
    const QString recoveryError = english
        ? QStringLiteral("The EPSILON live navigation stream could not be restored. Reconnect EPSILON manually.")
        : QStringLiteral("EPSILON 实时导航流未能恢复，请手动重新连接 EPSILON。");
    result.error_message = result.error_message.isEmpty()
        ? recoveryError
        : QStringLiteral("%1 %2").arg(result.error_message, recoveryError);
    emitLog(log,
            LogLevel::Error,
            QStringLiteral("device.navigation.command"),
            QStringLiteral("epsilon_live_stream_restore_failed"),
            QStringLiteral("EPSILON 实时导航流未能恢复，请手动重新连接 EPSILON。"),
            {{QStringLiteral("device"), QStringLiteral("EPSILON")},
             {QStringLiteral("operation"), operationName},
             {QStringLiteral("error_code"), QStringLiteral("STREAM_RESTORE_FAILED")},
             {QStringLiteral("recovery_error"), recoveryError},
             {QStringLiteral("ui_dedupe_key"), QStringLiteral("epsilon:live_stream_restore_failed")}});
    return result;
}

EpsilonConfigurationResult performSettingsOperation(
    const EpsilonDeviceOperation& operation,
    const QString& name,
    const std::function<bool(VaporView::EpsilonCollector&, EpsilonSettingsSnapshot&, std::string&)>& command,
    const EpsilonConfigurationService::LogCallback& log)
{
    EpsilonConfigurationResult result;
    result.live_stream_restarted = !operation.restart_live_stream;
    const auto collector = prepareCollector(operation, log);
    if (operation.restart_live_stream)
    {
        emitLog(log, LogLevel::Info, QStringLiteral("device.navigation.command"),
                QStringLiteral("epsilon_live_stream_pause_for_configuration"),
                QStringLiteral("为读取、应用设置或重启 EPSILON 临时停止实时导航流。"),
                {{QStringLiteral("operation"), name},
                 {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
        collector->stop();
    }
    std::string error;
    try
    {
        if (!collector->start(operation.port.toStdString(), VaporView::SerialConfig::N81(operation.baud)))
            error = "Failed to open EPSILON serial port: " + collector->getLastError();
        else
            result.command_succeeded = command(*collector, result.settings_snapshot, error);
    }
    catch (const std::exception& exception)
    {
        error = std::string("EPSILON settings operation failed: ") + exception.what();
    }
    catch (...)
    {
        error = "EPSILON settings operation failed";
    }
    if (!result.command_succeeded && error.empty())
        error = "EPSILON settings operation failed";
    result.error_message = QString::fromStdString(error);
    QVariantMap fields{{QStringLiteral("operation"), name},
             {QStringLiteral("command_succeeded"), result.command_succeeded},
             {QStringLiteral("saved"), result.settings_snapshot.saved},
             {QStringLiteral("readback_verified"), result.settings_snapshot.readback_verified},
             {QStringLiteral("restart_required"), result.settings_snapshot.restart_required},
             {QStringLiteral("error"), result.error_message},
             {QStringLiteral("ui_visibility"), QStringLiteral("details")}};
    if (!result.command_succeeded)
        fields.insert(QStringLiteral("error_code"), QStringLiteral("CONFIG_APPLY_FAILED"));
    emitLog(log, result.command_succeeded ? LogLevel::Info : LogLevel::Error,
            QStringLiteral("device.navigation.command"), QStringLiteral("epsilon_settings_result"),
            result.command_succeeded ? (name == QStringLiteral("calibrate_maintenance")
                ? QStringLiteral("EPSILON 维护命令发送及保存阶段已结束；确认状态以维护结果为准，实际校准效果需重启后验证。")
                : QStringLiteral("EPSILON 设置操作完成；重启后持久化仍需实机验证。"))
                                     : QStringLiteral("EPSILON 设置操作失败，部分设置可能已应用，请重新读取设备。"), fields);
    try
    {
        return finishOperation(operation, name, collector, result, log);
    }
    catch (...)
    {
        try { collector->stop(); } catch (...) { }
        result.live_stream_restarted = false;
        const QString recoveryError = operation.english
            ? QStringLiteral("EPSILON stream recovery failed unexpectedly. Reconnect the device manually.")
            : QStringLiteral("EPSILON 导航流恢复时发生异常，请手动重新连接设备。");
        result.error_message = result.error_message.isEmpty() ? recoveryError
            : result.error_message + QLatin1Char(' ') + recoveryError;
        return result;
    }
}

} // namespace

EpsilonConfigurationResult EpsilonConfigurationService::readSettings(
    const EpsilonDeviceOperation& operation, EpsilonSettingsGroup group, const LogCallback& log)
{
    auto result = performSettingsOperation(operation, QStringLiteral("read_settings"),
        [group](VaporView::EpsilonCollector& collector, EpsilonSettingsSnapshot& snapshot, std::string& error) {
            return collector.readSettings(group, snapshot, error);
        }, log);
    result.settings_snapshot.group = group;
    return result;
}

EpsilonConfigurationResult EpsilonConfigurationService::applySettings(
    const EpsilonDeviceOperation& operation, const EpsilonSettingsOperation& settings, const LogCallback& log)
{
    std::string error;
    if (!validateEpsilonSettings(settings, error))
    {
        EpsilonConfigurationResult result;
        result.error_message = QString::fromStdString(error);
        result.settings_snapshot.group = settings.group;
        // Validation did not interrupt the original stream.
        result.live_stream_restarted = !operation.restart_live_stream ||
            (operation.live_collector && operation.live_collector->isRunning());
        return result;
    }
    auto result = performSettingsOperation(operation, QStringLiteral("apply_settings"),
        [&settings](VaporView::EpsilonCollector& collector, EpsilonSettingsSnapshot& snapshot, std::string& commandError) {
            return collector.applySettings(settings, snapshot, commandError);
        }, log);
    result.settings_snapshot.group = settings.group;
    return result;
}

EpsilonConfigurationResult EpsilonConfigurationService::rebootDevice(
    const EpsilonDeviceOperation& operation, const LogCallback& log)
{
    return performSettingsOperation(operation, QStringLiteral("reboot_device"),
        [](VaporView::EpsilonCollector& collector, EpsilonSettingsSnapshot&, std::string& error) {
            return collector.rebootDevice(error);
        }, log);
}

EpsilonConfigurationResult EpsilonConfigurationService::calibrateMaintenance(
    const EpsilonDeviceOperation& operation,
    VaporView::EpsilonMaintenanceAction action,
    const LogCallback& log)
{
    EpsilonMaintenanceResult maintenance;
    maintenance.action = action;
    if (!validEpsilonMaintenanceAction(action))
    {
        EpsilonConfigurationResult result;
        result.maintenance_result = maintenance;
        result.error_message = QStringLiteral("Invalid EPSILON maintenance action.");
        result.live_stream_restarted = !operation.restart_live_stream ||
            (operation.live_collector && operation.live_collector->isRunning());
        return result;
    }
    auto result = performSettingsOperation(
        operation,
        QStringLiteral("calibrate_maintenance"),
        [action, &maintenance, &operation](VaporView::EpsilonCollector& collector,
                               EpsilonSettingsSnapshot&, std::string& error) {
            maintenance.action = action;
            return collector.runMaintenance(action, maintenance, error, operation.maintenance_progress,
                [cancel = operation.maintenance_cancel]() { return cancel && cancel->load(); });
        },
        log);
    if (!result.live_stream_restarted)
    {
        maintenance.status = EpsilonMaintenanceStatus::Failed;
        maintenance.error = result.error_message.toStdString();
    }
    result.maintenance_result = std::move(maintenance);
    return result;
}

EpsilonConfigurationResult EpsilonConfigurationService::readDgnss(
    const EpsilonDeviceOperation& operation, const LogCallback& log)
{
    EpsilonDgnssSnapshot snapshot;
    auto result = performSettingsOperation(operation, QStringLiteral("read_dgnss"),
        [&snapshot](VaporView::EpsilonCollector& collector, EpsilonSettingsSnapshot&, std::string& error) {
            return collector.readDgnss(snapshot, error);
        }, log);
    result.dgnss_snapshot = std::move(snapshot);
    return result;
}

EpsilonConfigurationResult EpsilonConfigurationService::applyDgnss(
    const EpsilonDeviceOperation& operation, const EpsilonDgnssOperation& settings, const LogCallback& log)
{
    std::string error;
    if (!validateEpsilonDgnss(settings, error))
    {
        EpsilonConfigurationResult result;
        result.error_message = QString::fromStdString(error);
        result.live_stream_restarted = !operation.restart_live_stream ||
            (operation.live_collector && operation.live_collector->isRunning());
        return result;
    }
    EpsilonDgnssSnapshot snapshot;
    auto result = performSettingsOperation(operation, QStringLiteral("apply_dgnss"),
        [&snapshot, &settings](VaporView::EpsilonCollector& collector, EpsilonSettingsSnapshot&, std::string& commandError) {
            return collector.applyDgnss(settings, snapshot, commandError);
        }, log);
    result.dgnss_snapshot = std::move(snapshot);
    return result;
}

EpsilonConfigurationResult EpsilonConfigurationService::applyMainAntennaLeverArm(
    const EpsilonDeviceOperation& operation,
    double x_m,
    double y_m,
    double z_m,
    const LogCallback& log)
{
    EpsilonConfigurationResult result;
    result.live_stream_restarted = !operation.restart_live_stream;
    const bool english = operation.english;
    const std::shared_ptr<VaporView::EpsilonCollector> collector = prepareCollector(operation, log);

    if (operation.restart_live_stream)
    {
        emitLog(log,
                LogLevel::Info,
                QStringLiteral("device.navigation.command"),
                QStringLiteral("epsilon_live_stream_pause_for_configuration"),
                QStringLiteral("为配置 EPSILON 主天线杆臂临时停止当前数据流。"),
                {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                 {QStringLiteral("operation"), QStringLiteral("main_antenna_lever_arm")},
                 {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
        collector->stop();
    }

    if (!collector->start(operation.port.toStdString(), VaporView::SerialConfig::N81(operation.baud)))
    {
        const QString systemError = QString::fromStdString(collector->getLastError());
        result.error_message = QString(english
                ? "[EPSILON] Failed to open %1 for main antenna lever-arm configuration: %2"
                : "[EPSILON] 打开 %1 进行主天线杆臂配置失败: %2")
            .arg(operation.port, systemError);
        emitLog(log,
                LogLevel::Error,
                QStringLiteral("device.navigation.command"),
                QStringLiteral("epsilon_main_antenna_lever_arm_open_failed"),
                QStringLiteral("打开 EPSILON 串口进行主天线杆臂配置失败。"),
                {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                 {QStringLiteral("operation"), QStringLiteral("main_antenna_lever_arm")},
                 {QStringLiteral("port"), operation.port},
                 {QStringLiteral("baud"), operation.baud},
                 {QStringLiteral("system_error"), systemError},
                 {QStringLiteral("error_code"), QStringLiteral("SERIAL_OPEN_FAILED")},
                 {QStringLiteral("ui_dedupe_key"), QStringLiteral("epsilon:main_antenna_lever_arm:open_failed")}});
    }
    else if (!collector->configureMainAntennaLeverArm(x_m, y_m, z_m))
    {
        result.error_message = QString(english
                ? "[EPSILON] Failed to configure main antenna lever arm on %1 @ %2."
                : "[EPSILON] 在 %1 @ %2 上配置主天线杆臂失败。")
            .arg(operation.port, operation.baud_text);
        emitLog(log,
                LogLevel::Error,
                QStringLiteral("device.navigation.command"),
                QStringLiteral("epsilon_main_antenna_lever_arm_config_failed"),
                QStringLiteral("EPSILON 主天线杆臂配置失败。"),
                {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                 {QStringLiteral("operation"), QStringLiteral("main_antenna_lever_arm")},
                 {QStringLiteral("port"), operation.port},
                 {QStringLiteral("baud"), operation.baud},
                 {QStringLiteral("x_m"), x_m},
                 {QStringLiteral("y_m"), y_m},
                 {QStringLiteral("z_m"), z_m},
                 {QStringLiteral("error_code"), QStringLiteral("CONFIG_APPLY_FAILED")},
                 {QStringLiteral("ui_dedupe_key"), QStringLiteral("epsilon:main_antenna_lever_arm:config_failed")}});
    }
    else
    {
        result.command_succeeded = true;
    }

    if (!result.command_succeeded && result.error_message.isEmpty())
    {
        result.error_message = english
            ? QStringLiteral("Failed to apply EPSILON main antenna lever arm.")
            : QStringLiteral("EPSILON 主天线杆臂下发失败。");
    }
    return finishOperation(operation, QStringLiteral("main_antenna_lever_arm"), collector, std::move(result), log);
}

EpsilonConfigurationResult EpsilonConfigurationService::configureRtcmPort(
    const EpsilonDeviceOperation& operation,
    int device_port_index,
    const QString& forward_port,
    int forward_baud,
    const QString& forward_baud_text,
    const LogCallback& log)
{
    EpsilonConfigurationResult result;
    result.live_stream_restarted = !operation.restart_live_stream;
    const bool english = operation.english;
    const std::shared_ptr<VaporView::EpsilonCollector> collector = prepareCollector(operation, log);

    if (operation.restart_live_stream)
    {
        emitLog(log,
                LogLevel::Info,
                QStringLiteral("device.navigation.command"),
                QStringLiteral("epsilon_live_stream_pause_for_configuration"),
                QStringLiteral("为配置 EPSILON RTCM 串口临时停止当前数据流。"),
                {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                 {QStringLiteral("operation"), QStringLiteral("rtcm_port")},
                 {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
        collector->stop();
    }

    if (!collector->start(operation.port.toStdString(), VaporView::SerialConfig::N81(operation.baud)))
    {
        const QString systemError = QString::fromStdString(collector->getLastError());
        result.error_message = QString(english
                ? "[EPSILON] Failed to open %1 for RTCM-port configuration: %2"
                : "[EPSILON] 打开 %1 进行 RTCM 串口配置失败: %2")
            .arg(operation.port, systemError);
        emitLog(log,
                LogLevel::Error,
                QStringLiteral("device.navigation.command"),
                QStringLiteral("epsilon_rtcm_port_open_failed"),
                QStringLiteral("打开 EPSILON 串口进行 RTCM 配置失败。"),
                {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                 {QStringLiteral("operation"), QStringLiteral("rtcm_port")},
                 {QStringLiteral("port"), operation.port},
                 {QStringLiteral("baud"), operation.baud},
                 {QStringLiteral("system_error"), systemError},
                 {QStringLiteral("error_code"), QStringLiteral("SERIAL_OPEN_FAILED")},
                 {QStringLiteral("ui_dedupe_key"), QStringLiteral("epsilon:rtcm_port:open_failed")}});
        return finishOperation(operation, QStringLiteral("rtcm_port"), collector, std::move(result), log);
    }

    if (!collector->configureRtcmPort(device_port_index, forward_baud))
    {
        result.error_message = QString(english
                ? "[EPSILON] Failed to configure communication port %1 as RTCM on %2 @ %3."
                : "[EPSILON] 在 %2 @ %3 上把通信串口 %1 配置为 RTCM 失败。")
            .arg(device_port_index)
            .arg(operation.port, operation.baud_text);
        emitLog(log,
                LogLevel::Error,
                QStringLiteral("device.navigation.command"),
                QStringLiteral("epsilon_rtcm_port_config_failed"),
                QStringLiteral("EPSILON RTCM 串口配置失败。"),
                {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                 {QStringLiteral("operation"), QStringLiteral("rtcm_port")},
                 {QStringLiteral("port"), operation.port},
                 {QStringLiteral("baud"), operation.baud},
                 {QStringLiteral("device_port"), device_port_index},
                 {QStringLiteral("forward_port"), forward_port},
                 {QStringLiteral("forward_baud"), forward_baud},
                 {QStringLiteral("error_code"), QStringLiteral("CONFIG_APPLY_FAILED")},
                 {QStringLiteral("ui_dedupe_key"), QStringLiteral("epsilon:rtcm_port:config_failed")}});
        return finishOperation(operation, QStringLiteral("rtcm_port"), collector, std::move(result), log);
    }

    result.command_succeeded = true;
    {
        QSettings main_settings = VaporView::applicationConfigSettings();
        main_settings.beginGroup(QStringLiteral("MainWindow"));
        VaporView::setPersistentSetting(main_settings, QStringLiteral("epsilon_rtcm_device_port_index"), device_port_index);
        VaporView::setPersistentSetting(main_settings, QStringLiteral("epsilon_rtcm_forward_port"), forward_port);
        VaporView::setPersistentSetting(main_settings, QStringLiteral("epsilon_rtcm_forward_baud"), forward_baud_text);
    }

    emitLog(log,
            LogLevel::Info,
            QStringLiteral("device.navigation.command"),
            QStringLiteral("epsilon_rtcm_port_config_completed"),
            QStringLiteral("EPSILON RTCM 串口配置已完成，RTK 转发配置已预填。"),
            {{QStringLiteral("device"), QStringLiteral("EPSILON")},
             {QStringLiteral("operation"), QStringLiteral("rtcm_port")},
             {QStringLiteral("port"), operation.port},
             {QStringLiteral("device_port"), device_port_index},
             {QStringLiteral("forward_port"), forward_port},
             {QStringLiteral("forward_baud"), forward_baud},
             {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    return finishOperation(operation, QStringLiteral("rtcm_port"), collector, std::move(result), log);
}

EpsilonConfigurationResult EpsilonConfigurationService::configurePacketRates(
    const EpsilonDeviceOperation& operation,
    int output_rate_hz,
    int callback_rate_hz,
    const std::map<uint8_t, int>& packet_rates,
    const QString& packet_rate_signature,
    const LogCallback& log)
{
    EpsilonConfigurationResult result;
    result.live_stream_restarted = !operation.restart_live_stream;
    const bool english = operation.english;
    const std::shared_ptr<VaporView::EpsilonCollector> collector = prepareCollector(operation, log);
    collector->setSampleRate(callback_rate_hz);

    const int totalProgressSteps = static_cast<int>(2 * (packet_rates.size() + 4) + 2);
    int progressStep = 0;
    int progressLogSequence = 0;
    const auto emitProgress = [&](const QString& processOutput,
                                  const QString& command,
                                  const QString& stage,
                                  bool isReply,
                                  bool successful,
                                  int current) {
        emitLog(log,
                LogLevel::Info,
                QStringLiteral("device.collector"),
                QStringLiteral("epsilon_configuration_collector_output"),
                QStringLiteral("EPSILON 配置过程输出了采集器诊断信息。"),
                {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                 {QStringLiteral("process_output"), processOutput},
                 {QStringLiteral("external_raw_text"), true},
                 {QStringLiteral("ui_visibility"), QStringLiteral("hidden")},
                 {QStringLiteral("epsilon_progress_current"), current},
                 {QStringLiteral("epsilon_progress_total"), totalProgressSteps},
                 {QStringLiteral("epsilon_progress_stage"), stage},
                 {QStringLiteral("epsilon_progress_command"), command},
                 {QStringLiteral("epsilon_progress_kind"), isReply
                      ? QStringLiteral("reply") : QStringLiteral("command")},
                 {QStringLiteral("epsilon_progress_success"), successful},
                 {QStringLiteral("ui_dedupe_key"),
                  QStringLiteral("epsilon:output_reconfigure:progress:%1")
                      .arg(++progressLogSequence)}});
    };
    const VaporView::EpsilonCollector::CommandProgressCallback progress =
        [&](const std::string& command, bool isReply, bool successful) {
            if (successful)
            {
                progressStep = std::min(totalProgressSteps - 1, progressStep + 1);
            }
            const QString commandText = QString::fromStdString(command);
            const QString stage = isReply
                ? (english
                    ? QStringLiteral("Received successful reply for %1").arg(commandText)
                    : QStringLiteral("已收到 %1 成功回复").arg(commandText))
                : (successful
                    ? (english
                        ? QStringLiteral("Sent command %1").arg(commandText)
                        : QStringLiteral("已发送命令 %1").arg(commandText))
                    : (english
                        ? QStringLiteral("Command %1 failed to send").arg(commandText)
                        : QStringLiteral("命令 %1 发送失败").arg(commandText)));
            const QString processOutput = QStringLiteral("[%1] %2")
                .arg(isReply ? QStringLiteral("EPSILON RX")
                             : QStringLiteral("EPSILON TX"),
                     commandText);
            emitProgress(processOutput,
                         commandText,
                         stage,
                         isReply,
                         successful,
                         progressStep);
        };

    if (operation.restart_live_stream)
    {
        emitLog(log,
                LogLevel::Info,
                QStringLiteral("device.navigation.command"),
                QStringLiteral("epsilon_live_stream_pause_for_configuration"),
                QStringLiteral("为手动重配 EPSILON 输出临时停止当前数据流。"),
                {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                 {QStringLiteral("operation"), QStringLiteral("output_reconfigure")},
                 {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
        collector->stop();
    }

    if (!collector->start(operation.port.toStdString(), VaporView::SerialConfig::N81(operation.baud)))
    {
        const QString systemError = QString::fromStdString(collector->getLastError());
        result.error_message = QString(english
                ? "[EPSILON] Failed to open %1 for manual reconfiguration: %2"
                : "[EPSILON] 打开 %1 进行手动重配失败: %2")
            .arg(operation.port, systemError);
        emitLog(log,
                LogLevel::Error,
                QStringLiteral("device.navigation.command"),
                QStringLiteral("epsilon_output_reconfigure_open_failed"),
                QStringLiteral("打开 EPSILON 串口进行手动重配失败。"),
                {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                 {QStringLiteral("operation"), QStringLiteral("output_reconfigure")},
                 {QStringLiteral("port"), operation.port},
                 {QStringLiteral("baud"), operation.baud},
                 {QStringLiteral("system_error"), systemError},
                 {QStringLiteral("error_code"), QStringLiteral("SERIAL_OPEN_FAILED")},
                 {QStringLiteral("ui_dedupe_key"), QStringLiteral("epsilon:output_reconfigure:open_failed")}});
        return finishOperation(operation, QStringLiteral("output_reconfigure"), collector, std::move(result), log);
    }

    if (!collector->setOutputPacketRates(packet_rates, true, progress))
    {
        result.error_message = QString(english
                ? "[EPSILON] Manual reconfiguration failed on %1 @ %2."
                : "[EPSILON] 在 %1 @ %2 上执行手动重配失败。")
            .arg(operation.port, operation.baud_text);
        emitLog(log,
                LogLevel::Error,
                QStringLiteral("device.navigation.command"),
                QStringLiteral("epsilon_output_reconfigure_failed"),
                QStringLiteral("EPSILON 输出手动重配失败。"),
                {{QStringLiteral("device"), QStringLiteral("EPSILON")},
                 {QStringLiteral("operation"), QStringLiteral("output_reconfigure")},
                 {QStringLiteral("port"), operation.port},
                 {QStringLiteral("baud"), operation.baud},
                 {QStringLiteral("output_rate_hz"), output_rate_hz},
                 {QStringLiteral("callback_rate_hz"), callback_rate_hz},
                 {QStringLiteral("packet_rate_signature"), packet_rate_signature},
                 {QStringLiteral("error_code"), QStringLiteral("CONFIG_APPLY_FAILED")},
                 {QStringLiteral("ui_dedupe_key"), QStringLiteral("epsilon:output_reconfigure:failed")}});
        return finishOperation(operation, QStringLiteral("output_reconfigure"), collector, std::move(result), log);
    }

    progressStep = totalProgressSteps;
    emitProgress(QStringLiteral("[FDILink RX] navigation stream restored"),
                 QStringLiteral("FDILink"),
                 english ? QStringLiteral("Live navigation stream restored")
                         : QStringLiteral("实时导航流已恢复"),
                 true,
                 true,
                 progressStep);

    result.command_succeeded = true;
    {
        QSettings settings = VaporView::applicationConfigSettings();
        settings.beginGroup(QStringLiteral("MainWindow"));
        VaporView::setPersistentSetting(settings, QStringLiteral("epsilon_last_config_port"), operation.port);
        VaporView::setPersistentSetting(settings, QStringLiteral("epsilon_last_config_baud"), operation.baud_text);
        VaporView::setPersistentSetting(settings, QStringLiteral("epsilon_last_config_rate_hz"), output_rate_hz);
        VaporView::setPersistentSetting(settings, QStringLiteral("epsilon_last_config_signature"), packet_rate_signature);
        VaporView::setPersistentSetting(settings, QStringLiteral("epsilon_last_config_apply_version"), PacketConfigurationVersion);
    }

    emitLog(log,
            LogLevel::Info,
            QStringLiteral("device.navigation.command"),
            QStringLiteral("epsilon_output_reconfigure_completed"),
            QStringLiteral("EPSILON 输出手动重配已完成。"),
            {{QStringLiteral("device"), QStringLiteral("EPSILON")},
             {QStringLiteral("operation"), QStringLiteral("output_reconfigure")},
             {QStringLiteral("port"), operation.port},
             {QStringLiteral("output_rate_hz"), output_rate_hz},
             {QStringLiteral("callback_rate_hz"), callback_rate_hz},
             {QStringLiteral("packet_rate_signature"), packet_rate_signature},
             {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    return finishOperation(operation, QStringLiteral("output_reconfigure"), collector, std::move(result), log);
}

} // namespace VaporView::Ground
