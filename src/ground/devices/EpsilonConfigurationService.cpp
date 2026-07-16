#include "ground/devices/EpsilonConfigurationService.h"

#include "data_collector.h"

#include <QSettings>

namespace VaporView::Ground
{
namespace
{

void emitLog(const EpsilonConfigurationService::LogCallback& log, const QString& message)
{
    if (log)
    {
        log(message);
    }
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
        emitLog(log, QString::fromStdString(message));
    });
    return collector;
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
    const bool english = operation.english;
    const std::shared_ptr<VaporView::EpsilonCollector> collector = prepareCollector(operation, log);

    if (operation.restart_live_stream)
    {
        emitLog(log, english
            ? QStringLiteral("[EPSILON] Temporarily stopping the live stream for main antenna lever-arm configuration.")
            : QStringLiteral("[EPSILON] 为配置主天线杆臂临时停止当前数据流。"));
        collector->stop();
    }

    if (!collector->start(operation.port.toStdString(), VaporView::SerialConfig::N81(operation.baud)))
    {
        result.error_message = QString(english
                ? "[EPSILON] Failed to open %1 for main antenna lever-arm configuration: %2"
                : "[EPSILON] 打开 %1 进行主天线杆臂配置失败: %2")
            .arg(operation.port, QString::fromStdString(collector->getLastError()));
        emitLog(log, result.error_message);
    }
    else if (!collector->configureMainAntennaLeverArm(x_m, y_m, z_m))
    {
        result.error_message = QString(english
                ? "[EPSILON] Failed to configure main antenna lever arm on %1 @ %2."
                : "[EPSILON] 在 %1 @ %2 上配置主天线杆臂失败。")
            .arg(operation.port, operation.baud_text);
        emitLog(log, result.error_message);
    }
    else
    {
        result.command_succeeded = true;
    }

    if (operation.restart_live_stream)
    {
        if (!collector->startStreaming())
        {
            result.live_stream_restarted = false;
            emitLog(log, english
                ? QStringLiteral("[EPSILON] Main antenna lever-arm command finished, but failed to restart the live navigation stream.")
                : QStringLiteral("[EPSILON] 主天线杆臂命令已结束，但重新启动实时导航流失败。"));
            collector->stop();
        }
        else
        {
            emitLog(log, QString(english
                    ? "[EPSILON] Main antenna lever arm applied and live stream restarted on %1."
                    : "[EPSILON] 主天线杆臂已下发，并已在 %1 上恢复实时数据流。")
                .arg(operation.port));
        }
    }
    else
    {
        collector->stop();
        if (result.command_succeeded)
        {
            emitLog(log, QString(english
                    ? "[EPSILON] Main antenna lever arm applied on %1."
                    : "[EPSILON] 已在 %1 上完成主天线杆臂配置。")
                .arg(operation.port));
        }
    }

    if (!result.command_succeeded && result.error_message.isEmpty())
    {
        result.error_message = english
            ? QStringLiteral("Failed to apply EPSILON main antenna lever arm.")
            : QStringLiteral("EPSILON 主天线杆臂下发失败。");
    }
    else if (result.command_succeeded && !result.live_stream_restarted)
    {
        result.error_message = english
            ? QStringLiteral("The lever arm was sent, but the EPSILON live stream could not be restarted. Reconnect EPSILON manually.")
            : QStringLiteral("杆臂已下发，但 EPSILON 实时数据流未能恢复。请手动重新连接 EPSILON。");
    }

    return result;
}

EpsilonConfigurationResult EpsilonConfigurationService::configureRtcmPort(
    const EpsilonDeviceOperation& operation,
    const QString& forward_port,
    int forward_baud,
    const QString& forward_baud_text,
    const LogCallback& log)
{
    EpsilonConfigurationResult result;
    const bool english = operation.english;
    const std::shared_ptr<VaporView::EpsilonCollector> collector = prepareCollector(operation, log);

    if (operation.restart_live_stream)
    {
        emitLog(log, english
            ? QStringLiteral("[EPSILON] Temporarily stopping the live stream for RTCM-port configuration.")
            : QStringLiteral("[EPSILON] 为配置 RTCM 串口临时停止当前数据流。"));
        collector->stop();
    }

    if (!collector->start(operation.port.toStdString(), VaporView::SerialConfig::N81(operation.baud)))
    {
        result.error_message = QString(english
                ? "[EPSILON] Failed to open %1 for RTCM-port configuration: %2"
                : "[EPSILON] 打开 %1 进行 RTCM 串口配置失败: %2")
            .arg(operation.port, QString::fromStdString(collector->getLastError()));
        emitLog(log, result.error_message);
        return result;
    }

    if (!collector->configureRtcmPort(2, forward_baud))
    {
        result.error_message = QString(english
                ? "[EPSILON] Failed to configure communication port 2 as RTCM on %1 @ %2."
                : "[EPSILON] 在 %1 @ %2 上把第二通信串口配置为 RTCM 失败。")
            .arg(operation.port, operation.baud_text);
        emitLog(log, result.error_message);
        collector->stop();
        return result;
    }

    result.command_succeeded = true;
    {
        QSettings main_settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
        main_settings.setValue(QStringLiteral("epsilon_rtcm_forward_port"), forward_port);
        main_settings.setValue(QStringLiteral("epsilon_rtcm_forward_baud"), forward_baud_text);
        QSettings rtk_settings(QStringLiteral("VaporView"), QStringLiteral("RtkConfig"));
        rtk_settings.setValue(QStringLiteral("output_port"), forward_port);
        rtk_settings.setValue(QStringLiteral("baudrate"), forward_baud_text);
    }

    if (operation.restart_live_stream)
    {
        if (!collector->startStreaming())
        {
            result.live_stream_restarted = false;
            result.error_message = english
                ? QStringLiteral("EPSILON RTCM port was configured, but the live navigation stream could not be restarted.")
                : QStringLiteral("EPSILON RTCM 串口已配置，但实时导航流未能恢复。");
            emitLog(log, english
                ? QStringLiteral("[EPSILON] RTCM-port configuration succeeded, but failed to restart the live navigation stream.")
                : QStringLiteral("[EPSILON] RTCM 串口配置已完成，但重新启动实时导航流失败。"));
            collector->stop();
            return result;
        }

        emitLog(log, QString(english
                ? "[EPSILON] RTCM port is ready. EPSILON live stream restarted on %1, and RTK forwarding is prefilled for %2 @ %3."
                : "[EPSILON] RTCM 串口已就绪，已在 %1 上恢复 EPSILON 实时数据流，并为 %2 @ %3 预填 RTK 转发配置。")
            .arg(operation.port, forward_port, forward_baud_text));
    }
    else
    {
        collector->stop();
        emitLog(log, QString(english
                ? "[EPSILON] RTCM port is ready on %1. RTK forwarding is prefilled for %2 @ %3."
                : "[EPSILON] 已在 %1 上完成 RTCM 串口配置，并为 %2 @ %3 预填 RTK 转发配置。")
            .arg(operation.port, forward_port, forward_baud_text));
    }

    return result;
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
    const bool english = operation.english;
    const std::shared_ptr<VaporView::EpsilonCollector> collector = prepareCollector(operation, log);
    collector->setSampleRate(callback_rate_hz);

    if (operation.restart_live_stream)
    {
        emitLog(log, english
            ? QStringLiteral("[EPSILON] Temporarily stopping the live stream for manual reconfiguration.")
            : QStringLiteral("[EPSILON] 为手动重配临时停止当前数据流。"));
        collector->stop();
    }

    if (!collector->start(operation.port.toStdString(), VaporView::SerialConfig::N81(operation.baud)))
    {
        result.error_message = QString(english
                ? "[EPSILON] Failed to open %1 for manual reconfiguration: %2"
                : "[EPSILON] 打开 %1 进行手动重配失败: %2")
            .arg(operation.port, QString::fromStdString(collector->getLastError()));
        emitLog(log, result.error_message);
        return result;
    }

    if (!collector->setOutputPacketRates(packet_rates, true))
    {
        result.error_message = QString(english
                ? "[EPSILON] Manual reconfiguration failed on %1 @ %2."
                : "[EPSILON] 在 %1 @ %2 上执行手动重配失败。")
            .arg(operation.port, operation.baud_text);
        emitLog(log, result.error_message);
        collector->stop();
        return result;
    }

    result.command_succeeded = true;
    {
        QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
        settings.setValue(QStringLiteral("epsilon_last_config_port"), operation.port);
        settings.setValue(QStringLiteral("epsilon_last_config_baud"), operation.baud_text);
        settings.setValue(QStringLiteral("epsilon_last_config_rate_hz"), output_rate_hz);
        settings.setValue(QStringLiteral("epsilon_last_config_signature"), packet_rate_signature);
        settings.setValue(QStringLiteral("epsilon_last_config_apply_version"), PacketConfigurationVersion);
    }

    if (operation.restart_live_stream)
    {
        if (!collector->startStreaming())
        {
            result.live_stream_restarted = false;
            result.error_message = english
                ? QStringLiteral("EPSILON was reconfigured, but the live navigation stream could not be restarted.")
                : QStringLiteral("EPSILON 已重配，但实时导航流未能恢复。");
            emitLog(log, english
                ? QStringLiteral("[EPSILON] Reconfiguration succeeded, but failed to restart the live navigation stream.")
                : QStringLiteral("[EPSILON] 重配已完成，但重新启动实时导航流失败。"));
            collector->stop();
            return result;
        }

        emitLog(log, QString(english
                ? "[EPSILON] Manual reconfiguration completed and live stream restarted on %1."
                : "[EPSILON] 手动重配完成，已在 %1 上恢复实时数据流。")
            .arg(operation.port));
    }
    else
    {
        collector->stop();
        emitLog(log, QString(english
                ? "[EPSILON] Manual reconfiguration completed on %1. You can connect normally now."
                : "[EPSILON] 已在 %1 上完成手动重配，现在可以正常连接。")
            .arg(operation.port));
    }

    return result;
}

} // namespace VaporView::Ground
