#include "GroundTelemetryService.h"

#include <QDateTime>
#include <QJsonDocument>

namespace VaporView
{
namespace
{
constexpr int kCommandRetryIntervalMs = 800;
constexpr int kCommandMaxRetries = 3;

quint64 nowUs()
{
    return static_cast<quint64>(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) * 1000ULL;
}

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

}  // namespace

GroundTelemetryService::GroundTelemetryService(QObject *parent)
    : QObject(parent)
    , link_(this)
    , codec_(1024u * 1024u)
{
    connect(&link_, &SerialTelemetryLink::bytesReceived, this, &GroundTelemetryService::onBytesReceived);
    connect(&link_, &SerialTelemetryLink::openChanged, this, &GroundTelemetryService::linkOpenChanged);
    connect(&link_, &SerialTelemetryLink::errorOccurred, this, &GroundTelemetryService::logMessage);
    retry_timer_.setInterval(200);
    connect(&retry_timer_, &QTimer::timeout, this, &GroundTelemetryService::onRetryTimer);
}

bool GroundTelemetryService::open(const QString& portName, int baudRate)
{
    codec_.reset();
    pending_commands_.clear();
    const bool ok = link_.open(portName, baudRate);
    if (ok)
    {
        retry_timer_.start();
    }
    return ok;
}

void GroundTelemetryService::close()
{
    retry_timer_.stop();
    pending_commands_.clear();
    link_.close();
}

bool GroundTelemetryService::isOpen() const
{
    return link_.isOpen();
}

quint16 GroundTelemetryService::sendCommand(CommandId commandId, const QByteArray& payload)
{
    CommandMessage command;
    command.command_id = commandId;
    command.command_seq = next_command_seq_++;
    command.payload = payload;

    PendingCommand pending;
    pending.command = command;
    pending.encodedPayload = TelemetryCodec::serializeCommand(command);
    pending.next_retry_ms = nowMs() + kCommandRetryIntervalMs;
    sendCommandPayload(pending);
    pending_commands_.insert(command.command_seq, pending);
    return command.command_seq;
}

quint16 GroundTelemetryService::sendDeviceCommand(CommandId commandId, SkyDeviceId deviceId)
{
    return sendCommand(commandId, TelemetryCodec::serializeDeviceCommand(deviceId));
}

quint16 GroundTelemetryService::sendRateCommand(CommandId commandId, quint16 hz)
{
    return sendCommand(commandId, TelemetryCodec::serializeRatePayload(hz));
}

quint16 GroundTelemetryService::requestSkyConfig()
{
    return sendCommand(CommandId::GetSkyConfig);
}

quint16 GroundTelemetryService::setSkyConfig(const QJsonObject& config)
{
    return sendCommand(CommandId::SetSkyConfig, QJsonDocument(config).toJson(QJsonDocument::Compact));
}

quint16 GroundTelemetryService::saveSkyConfig()
{
    return sendCommand(CommandId::SaveSkyConfig);
}

void GroundTelemetryService::onBytesReceived(const QByteArray& bytes)
{
    const QVector<TelemetryFrame> frames = codec_.feedBytes(bytes);
    for (const TelemetryFrame& frame : frames)
    {
        dispatchFrame(frame);
    }
}

void GroundTelemetryService::onRetryTimer()
{
    const qint64 currentMs = nowMs();
    QList<quint16> expired;
    for (auto it = pending_commands_.begin(); it != pending_commands_.end(); ++it)
    {
        PendingCommand& pending = it.value();
        if (pending.next_retry_ms > currentMs)
        {
            continue;
        }
        if (pending.retry_count >= kCommandMaxRetries)
        {
            expired.push_back(it.key());
            emit logMessage(QStringLiteral("Command %1 seq %2 timed out")
                                .arg(static_cast<quint16>(pending.command.command_id))
                                .arg(pending.command.command_seq));
            continue;
        }
        ++pending.retry_count;
        pending.next_retry_ms = currentMs + kCommandRetryIntervalMs;
        sendCommandPayload(pending);
    }
    for (quint16 seq : expired)
    {
        pending_commands_.remove(seq);
    }
}

void GroundTelemetryService::dispatchFrame(const TelemetryFrame& frame)
{
    switch (frame.type)
    {
    case MsgType::TelemetryBasic:
    {
        TelemetryBasic data;
        if (TelemetryCodec::parseBasicTelemetry(frame.payload, data))
        {
            emit basicTelemetryUpdated(data);
        }
        break;
    }
    case MsgType::WaveformDownsampled:
    {
        DownsampledWaveform waveform;
        if (TelemetryCodec::parseDownsampledWaveform(frame.payload, waveform))
        {
            emit waveformUpdated(waveform);
        }
        break;
    }
    case MsgType::WaveformFeature:
    {
        WaveformFeature feature;
        if (TelemetryCodec::parseWaveformFeature(frame.payload, feature))
        {
            emit waveformFeatureUpdated(feature);
        }
        break;
    }
    case MsgType::TelemetryStatus:
    {
        TelemetryStatus status;
        if (TelemetryCodec::parseTelemetryStatus(frame.payload, status))
        {
            emit statusUpdated(status);
        }
        break;
    }
    case MsgType::CommandAck:
    {
        CommandAck ack;
        if (TelemetryCodec::parseCommandAck(frame.payload, ack))
        {
            pending_commands_.remove(ack.command_seq);
            emit commandAckReceived(ack);
        }
        break;
    }
    case MsgType::SkyConfig:
    {
        const QJsonDocument document = QJsonDocument::fromJson(frame.payload);
        if (document.isObject())
        {
            emit skyConfigReceived(document.object());
        }
        break;
    }
    case MsgType::SkyConfigApplyResult:
    {
        const QJsonDocument document = QJsonDocument::fromJson(frame.payload);
        if (document.isObject())
        {
            emit skyConfigApplyResultReceived(document.object());
        }
        break;
    }
    case MsgType::Heartbeat:
    case MsgType::Error:
    case MsgType::Command:
    default:
        break;
    }
}

void GroundTelemetryService::sendCommandPayload(PendingCommand& pending)
{
    if (!link_.isOpen())
    {
        return;
    }
    const QByteArray frame = codec_.encodeFrame(
        MsgType::Command,
        pending.encodedPayload,
        next_frame_seq_++,
        nowUs());
    link_.writeBytes(frame);
}

}  // namespace VaporView
