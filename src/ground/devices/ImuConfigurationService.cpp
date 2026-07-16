#include "ground/devices/ImuConfigurationService.h"

#include "serial_port.h"

#include <QThread>

#include <string>

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

void postLog(const ImuConfigurationService::LogCallback& log, const QString& message)
{
    if (log)
    {
        log(message);
    }
}

template <typename Sender>
bool sendCommand(
    Sender&& sender,
    const QString& command,
    const ImuConfigurationService::LogCallback& log,
    int waitMs = 80)
{
    const std::string bytes = command.toStdString();
    if (!sender(bytes, waitMs))
    {
        return false;
    }
    postLog(log, QStringLiteral("[IMU 发送] %1").arg(command.trimmed()));
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
        postLog(log, QString(request.english
                ? "[IMU] Failed to reopen IMU port: %1"
                : "[IMU] 重新打开 IMU 串口失败: %1")
            .arg(QString::fromStdString(collector->getLastError())));
        return false;
    }
    collector->setOutputMessageType(request.outputFormat.toStdString());
    if (!collector->checkDeviceResponse())
    {
        postLog(log, request.english
            ? QStringLiteral("[IMU] No response after reopening IMU port")
            : QStringLiteral("[IMU] 重新打开 IMU 串口后未收到设备响应"));
        collector->stop();
        return false;
    }
    if (!collector->startStreaming())
    {
        postLog(log, request.english
            ? QStringLiteral("[IMU] Failed to restart IMU data stream")
            : QStringLiteral("[IMU] 重新启动 IMU 数据流失败"));
        collector->stop();
        return false;
    }
    postLog(log, QString(request.english
            ? "[IMU] Reconnected at %1 baud, %2 Hz, %3"
            : "[IMU] 已按 %1 波特率、%2 Hz、%3 重新连接")
        .arg(request.targetBaud)
        .arg(request.targetRateHz)
        .arg(request.outputFormat));
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
        postLog(log, request.english
            ? QStringLiteral("Unsupported IMU format or rate")
            : QStringLiteral("IMU 输出格式或频率不受支持"));
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
        if (!sendCommand(collectorSend, QStringLiteral("LOG HI91 ONTIME 0\r\n"), log) ||
            !sendCommand(collectorSend, QStringLiteral("LOG HI92 ONTIME 0\r\n"), log) ||
            !sendCommand(
                collectorSend,
                QStringLiteral("LOG %1 ONTIME %2\r\n")
                    .arg(request.outputFormat, targetPeriod),
                log) ||
            !sendCommand(collectorSend, QStringLiteral("SAVECONFIG\r\n"), log, 120))
        {
            return false;
        }
        configured = true;

        if (request.targetBaud != request.currentBaud)
        {
            if (!sendCommand(
                    collectorSend,
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
            postLog(log, QString(request.english
                    ? "[IMU] Unable to open %1 for direct configuration, saved for next connection"
                    : "[IMU] 无法打开 %1 直接配置，已保存到下次连接时应用")
                .arg(request.port));
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

        if (!sendCommand(directSend, QStringLiteral("LOG HI91 ONTIME 0\r\n"), log) ||
            !sendCommand(directSend, QStringLiteral("LOG HI92 ONTIME 0\r\n"), log) ||
            !sendCommand(
                directSend,
                QStringLiteral("LOG %1 ONTIME %2\r\n")
                    .arg(request.outputFormat, targetPeriod),
                log) ||
            !sendCommand(directSend, QStringLiteral("SAVECONFIG\r\n"), log, 120))
        {
            return false;
        }
        configured = true;
        if (request.targetBaud != request.currentBaud)
        {
            if (!sendCommand(
                    directSend,
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
                !sendCommand(directSend, QStringLiteral("SAVECONFIG\r\n"), log, 120))
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
            postLog(log, request.english
                ? QStringLiteral("[IMU] Failed to persist baud rate after reconnect")
                : QStringLiteral("[IMU] 重连后保存波特率配置失败"));
        }
    }
    else if (collectorRunning)
    {
        collector->setSampleRate(request.targetRateHz);
    }

    if (configured)
    {
        postLog(log, QString(request.english
                ? "IMU profile applied: %1, %2 baud, %3 Hz"
                : "IMU 配置已应用: %1, %2 波特率, %3 Hz")
            .arg(request.outputFormat)
            .arg(request.targetBaud)
            .arg(request.targetRateHz));
    }
    return configured;
}

}  // namespace VaporView::Ground::Devices
