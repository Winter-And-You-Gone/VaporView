#include "ground/devices/EpsilonConfigurationService.h"

#include "data_collector.h"

#include <QSettings>

#include <utility>

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

EpsilonConfigurationResult finishOperation(
    const EpsilonDeviceOperation& operation,
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
        emitLog(log, result.command_succeeded
            ? (english
                   ? QStringLiteral("[EPSILON] Configuration completed and the live navigation stream was restored.")
                   : QStringLiteral("[EPSILON] 配置已完成，实时导航流已恢复。"))
            : (english
                   ? QStringLiteral("[EPSILON] Configuration failed, but the previous live navigation stream was restored.")
                   : QStringLiteral("[EPSILON] 配置失败，但原实时导航流已恢复。")));
        return result;
    }

    collector->stop();
    const QString recoveryError = english
        ? QStringLiteral("The EPSILON live navigation stream could not be restored. Reconnect EPSILON manually.")
        : QStringLiteral("EPSILON 实时导航流未能恢复，请手动重新连接 EPSILON。");
    result.error_message = result.error_message.isEmpty()
        ? recoveryError
        : QStringLiteral("%1 %2").arg(result.error_message, recoveryError);
    emitLog(log, QStringLiteral("[EPSILON] %1").arg(recoveryError));
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

    if (!result.command_succeeded && result.error_message.isEmpty())
    {
        result.error_message = english
            ? QStringLiteral("Failed to apply EPSILON main antenna lever arm.")
            : QStringLiteral("EPSILON 主天线杆臂下发失败。");
    }
    return finishOperation(operation, collector, std::move(result), log);
}

EpsilonConfigurationResult EpsilonConfigurationService::configureRtcmPort(
    const EpsilonDeviceOperation& operation,
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
        return finishOperation(operation, collector, std::move(result), log);
    }

    if (!collector->configureRtcmPort(2, forward_baud))
    {
        result.error_message = QString(english
                ? "[EPSILON] Failed to configure communication port 2 as RTCM on %1 @ %2."
                : "[EPSILON] 在 %1 @ %2 上把第二通信串口配置为 RTCM 失败。")
            .arg(operation.port, operation.baud_text);
        emitLog(log, result.error_message);
        return finishOperation(operation, collector, std::move(result), log);
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

    emitLog(log, QString(english
            ? "[EPSILON] RTCM port is ready on %1. RTK forwarding is prefilled for %2 @ %3."
            : "[EPSILON] 已在 %1 上完成 RTCM 串口配置，并为 %2 @ %3 预填 RTK 转发配置。")
        .arg(operation.port, forward_port, forward_baud_text));
    return finishOperation(operation, collector, std::move(result), log);
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
        return finishOperation(operation, collector, std::move(result), log);
    }

    if (!collector->setOutputPacketRates(packet_rates, true))
    {
        result.error_message = QString(english
                ? "[EPSILON] Manual reconfiguration failed on %1 @ %2."
                : "[EPSILON] 在 %1 @ %2 上执行手动重配失败。")
            .arg(operation.port, operation.baud_text);
        emitLog(log, result.error_message);
        return finishOperation(operation, collector, std::move(result), log);
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

    emitLog(log, QString(english
            ? "[EPSILON] Manual reconfiguration completed on %1."
            : "[EPSILON] 已在 %1 上完成手动重配。")
        .arg(operation.port));
    return finishOperation(operation, collector, std::move(result), log);
}

} // namespace VaporView::Ground
