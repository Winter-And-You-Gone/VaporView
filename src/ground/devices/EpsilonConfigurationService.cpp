#include "ground/devices/EpsilonConfigurationService.h"

#include "data_collector.h"

#include <QSettings>
#include "shared/config/ApplicationConfig.h"
#include "shared/config/SettingsWriteBarrier.h"

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
                    ? QStringLiteral("EPSILON 配置已完成，实时导航流已恢复。")
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

} // namespace

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

    if (!collector->setOutputPacketRates(packet_rates, true))
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
