#include "ground/devices/ImuConfigurationService.h"

#include "serial_port.h"

#include <QThread>

#include <string>
#include <utility>

namespace VaporView::Ground::Devices
{
namespace
{

QString periodText(int hz)
{
    switch (hz)
    {
    case 1: return QStringLiteral("1");
    case 2: return QStringLiteral("0.5");
    case 5: return QStringLiteral("0.2");
    case 10: return QStringLiteral("0.1");
    case 20: return QStringLiteral("0.05");
    case 50: return QStringLiteral("0.02");
    case 100: return QStringLiteral("0.01");
    case 200: return QStringLiteral("0.005");
    case 250: return QStringLiteral("0.004");
    case 500: return QStringLiteral("0.002");
    case 1000: return QStringLiteral("0.001");
    default: return {};
    }
}

QVariantMap profileFields(const ImuProfileRequest& request)
{
    return {{QStringLiteral("device"), QStringLiteral("IMU")},
            {QStringLiteral("port"), request.port},
            {QStringLiteral("current_baud"), request.currentBaud},
            {QStringLiteral("target_baud"), request.targetBaud},
            {QStringLiteral("rate_hz"), request.targetRateHz},
            {QStringLiteral("output_format"), request.outputFormat}};
}

void postImuLog(const ImuConfigurationService::LogCallback& log,
             ImuConfigurationService::LogEntry entry)
{
    if (log)
    {
        log(entry);
    }
}

void postImuLog(const ImuConfigurationService::LogCallback& log,
             LogLevel level,
             const QString& event,
             const QString& message,
             QVariantMap fields = QVariantMap())
{
    if (!fields.contains(QStringLiteral("ui_visibility")))
    {
        fields.insert(QStringLiteral("ui_visibility"),
                      level >= LogLevel::Warning ? QStringLiteral("attention")
                                                 : QStringLiteral("details"));
    }
    postImuLog(log, {level,
                  QStringLiteral("device.navigation.command"),
                  event,
                  message,
                  std::move(fields)});
}

template <typename Sender>
bool sendCommand(
    Sender&& sender,
    const ImuProfileRequest& request,
    const QString& command,
    const ImuConfigurationService::LogCallback& log,
    int waitMs = 80)
{
    const std::string bytes = command.toStdString();
    QVariantMap fields = profileFields(request);
    fields.insert(QStringLiteral("command"), command.trimmed());
    fields.insert(QStringLiteral("wait_ms"), waitMs);
    if (!sender(bytes, waitMs))
    {
        fields.insert(QStringLiteral("error_code"), QStringLiteral("COMMAND_WRITE_FAILED"));
        postImuLog(log,
                LogLevel::Error,
                QStringLiteral("imu_profile_command_write_failed"),
                QStringLiteral("IMU 配置命令发送失败。"),
                fields);
        return false;
    }
    fields.insert(QStringLiteral("ui_visibility"), QStringLiteral("hidden"));
    postImuLog(log,
            LogLevel::Debug,
            QStringLiteral("imu_profile_command_sent"),
            QStringLiteral("已向 IMU 发送配置命令。"),
            fields);
    return true;
}

bool restartCollector(
    const ImuProfileRequest& request,
    const std::shared_ptr<ImuCollector>& collector,
    const ImuConfigurationService::LogCallback& log)
{
    collector->setSampleRate(request.targetRateHz);
    if (!collector->start(
            request.port.toStdString(),
            SerialConfig::N81(request.targetBaud)))
    {
        QVariantMap fields = profileFields(request);
        fields.insert(QStringLiteral("system_error"), QString::fromStdString(collector->getLastError()));
        fields.insert(QStringLiteral("error_code"), QStringLiteral("SERIAL_OPEN_FAILED"));
        postImuLog(log,
                LogLevel::Error,
                QStringLiteral("imu_profile_reconnect_open_failed"),
                QStringLiteral("重新打开 IMU 串口失败。"),
                fields);
        return false;
    }
    collector->setOutputMessageType(request.outputFormat.toStdString());
    if (!collector->checkDeviceResponse())
    {
        QVariantMap fields = profileFields(request);
        fields.insert(QStringLiteral("error_code"), QStringLiteral("DEVICE_NO_RESPONSE"));
        postImuLog(log,
                LogLevel::Error,
                QStringLiteral("imu_profile_reconnect_no_response"),
                QStringLiteral("重新打开 IMU 串口后未收到设备响应。"),
                fields);
        collector->stop();
        return false;
    }
    if (!collector->startStreaming())
    {
        QVariantMap fields = profileFields(request);
        fields.insert(QStringLiteral("error_code"), QStringLiteral("STREAM_START_FAILED"));
        postImuLog(log,
                LogLevel::Error,
                QStringLiteral("imu_profile_reconnect_stream_start_failed"),
                QStringLiteral("重新启动 IMU 数据流失败。"),
                fields);
        collector->stop();
        return false;
    }
    postImuLog(log,
            LogLevel::Info,
            QStringLiteral("imu_profile_reconnect_completed"),
            QStringLiteral("IMU 已按目标配置重新连接。"),
            profileFields(request));
    return true;
}

}  // namespace

bool ImuConfigurationService::isSupported(const QString& outputFormat, int rateHz)
{
    const QString normalized = outputFormat.trimmed().toUpper();
    return (normalized == QStringLiteral("HI91") || normalized == QStringLiteral("HI92")) &&
           !periodText(rateHz).isEmpty();
}

bool ImuConfigurationService::apply(
    const ImuProfileRequest& request,
    const std::shared_ptr<ImuCollector>& collector,
    const LogCallback& log)
{
    const QString targetPeriod = periodText(request.targetRateHz);
    if (!isSupported(request.outputFormat, request.targetRateHz))
    {
        QVariantMap fields = profileFields(request);
        fields.insert(QStringLiteral("reason_code"), QStringLiteral("COMMAND_NOT_SUPPORTED"));
        postImuLog(log,
                LogLevel::Warning,
                QStringLiteral("imu_profile_apply_rejected_unsupported"),
                QStringLiteral("IMU 输出格式或频率不受支持。"),
                fields);
        return false;
    }

    const bool collectorRunning = collector && collector->isRunning();
    bool configured = false;
    bool needRestart = false;

    if (collectorRunning)
    {
        collector->setOutputMessageType(request.outputFormat.toStdString());
        auto collectorSend = [&collector](const std::string& command, int waitMs) {
            return collector->sendAsciiCommand(command, waitMs);
        };
        if (!sendCommand(collectorSend, request, QStringLiteral("LOG HI91 ONTIME 0\r\n"), log) ||
            !sendCommand(collectorSend, request, QStringLiteral("LOG HI92 ONTIME 0\r\n"), log) ||
            !sendCommand(
                collectorSend,
                request,
                QStringLiteral("LOG %1 ONTIME %2\r\n")
                    .arg(request.outputFormat, targetPeriod),
                log) ||
            !sendCommand(collectorSend, request, QStringLiteral("SAVECONFIG\r\n"), log, 120))
        {
            return false;
        }
        configured = true;

        if (request.targetBaud != request.currentBaud)
        {
            if (!sendCommand(
                    collectorSend,
                    request,
                    QStringLiteral("SERIALCONFIG %1\r\n").arg(request.targetBaud),
                    log,
                    150))
            {
                return false;
            }
            needRestart = true;
        }
    }
    else
    {
        SerialPort port;
        if (!port.open(
                request.port.toStdString(),
                SerialConfig::N81(request.currentBaud)))
        {
            QVariantMap fields = profileFields(request);
            fields.insert(QStringLiteral("system_error"), QString::fromStdString(port.lastError()));
            fields.insert(QStringLiteral("reason_code"), QStringLiteral("PORT_OPEN_FAILED"));
            postImuLog(log,
                    LogLevel::Warning,
                    QStringLiteral("imu_profile_direct_open_failed_saved"),
                    QStringLiteral("无法打开 IMU 串口直接配置，已保存供下次连接时应用。"),
                    fields);
            return true;
        }

        auto directSend = [&port](const std::string& command, int waitMs) {
            const bool ok = port.write(command.c_str(), command.size()) ==
                            static_cast<ssize_t>(command.size());
            if (ok && waitMs > 0)
            {
                QThread::msleep(static_cast<unsigned long>(waitMs));
            }
            return ok;
        };

        if (!sendCommand(directSend, request, QStringLiteral("LOG HI91 ONTIME 0\r\n"), log) ||
            !sendCommand(directSend, request, QStringLiteral("LOG HI92 ONTIME 0\r\n"), log) ||
            !sendCommand(
                directSend,
                request,
                QStringLiteral("LOG %1 ONTIME %2\r\n")
                    .arg(request.outputFormat, targetPeriod),
                log) ||
            !sendCommand(directSend, request, QStringLiteral("SAVECONFIG\r\n"), log, 120))
        {
            return false;
        }
        configured = true;
        if (request.targetBaud != request.currentBaud)
        {
            if (!sendCommand(
                    directSend,
                    request,
                    QStringLiteral("SERIALCONFIG %1\r\n").arg(request.targetBaud),
                    log,
                    150))
            {
                return false;
            }
            port.close();
            if (port.open(
                    request.port.toStdString(),
                    SerialConfig::N81(request.targetBaud)) &&
                !sendCommand(directSend, request, QStringLiteral("SAVECONFIG\r\n"), log, 120))
            {
                return false;
            }
        }
        port.close();
    }

    if (collectorRunning && needRestart)
    {
        collector->stop();
        if (!restartCollector(request, collector, log))
        {
            return false;
        }
        if (!collector->sendAsciiCommand("SAVECONFIG\r\n", 120))
        {
            QVariantMap fields = profileFields(request);
            fields.insert(QStringLiteral("command"), QStringLiteral("SAVECONFIG"));
            fields.insert(QStringLiteral("error_code"), QStringLiteral("CONFIG_SAVE_FAILED"));
            postImuLog(log,
                    LogLevel::Warning,
                    QStringLiteral("imu_profile_reconnect_save_failed"),
                    QStringLiteral("IMU 重连后保存波特率配置失败。"),
                    fields);
        }
    }
    else if (collectorRunning)
    {
        collector->setSampleRate(request.targetRateHz);
    }

    if (configured)
    {
        postImuLog(log,
                LogLevel::Info,
                QStringLiteral("imu_profile_applied"),
                QStringLiteral("IMU 配置已应用。"),
                profileFields(request));
    }
    return configured;
}

}  // namespace VaporView::Ground::Devices
