#include "ground/devices/GroundTelemetryService.h"

#include "LogService.h"
#include "SerialTelemetryLink.h"
#include "TcpTelemetryLink.h"

#include <QDateTime>
#include <QJsonDocument>
#include <algorithm>
#include <memory>

namespace VaporView
{
namespace
{
constexpr int kCommandRetryIntervalMs = 800;
constexpr int kCommandMaxRetries = 3;
constexpr qint64 kBitRateWindowMs = 5000;

quint64 nowUs()
{
    return static_cast<quint64>(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) * 1000ULL;
}

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

void pruneSamples(QVector<GroundTelemetryService::ByteSample>& samples, qint64 currentMs)
{
    while (!samples.isEmpty() && currentMs - samples.front().time_ms > kBitRateWindowMs)
    {
        samples.removeFirst();
    }
}

double samplesBitsPerSecond(QVector<GroundTelemetryService::ByteSample> samples, qint64 currentMs)
{
    pruneSamples(samples, currentMs);
    if (samples.isEmpty())
    {
        return 0.0;
    }
    qint64 bytes = 0;
    qint64 firstMs = currentMs;
    for (const GroundTelemetryService::ByteSample& sample : samples)
    {
        bytes += sample.bytes;
        firstMs = std::min(firstMs, sample.time_ms);
    }
    const qint64 elapsedMs = std::max<qint64>(1000, std::min(kBitRateWindowMs, currentMs - firstMs));
    return static_cast<double>(bytes) * 8.0 * 1000.0 / static_cast<double>(elapsedMs);
}

}  // namespace

GroundTelemetryService::GroundTelemetryService(QObject *parent)
    : QObject(parent)
    , codec_(1024u * 1024u)
{
    retry_timer_.setInterval(200);
    connect(&retry_timer_, &QTimer::timeout, this, &GroundTelemetryService::onRetryTimer);
}

bool GroundTelemetryService::open(const QString& portName, int baudRate)
{
    auto link = std::make_unique<SerialTelemetryLink>();
    const bool ok = link->open(portName, baudRate);
    if (!ok)
    {
        emit logMessage(QStringLiteral("Failed to open telemetry serial port: %1").arg(portName));
        return false;
    }
    transport_type_ = TelemetryTransportType::Serial;
    return openLink(std::move(link));
}

bool GroundTelemetryService::openTcp(const QString& host, quint16 port)
{
    auto link = std::make_unique<TcpTelemetryLink>();
    const bool ok = link->connectToHost(host, port);
    if (!ok)
    {
        emit logMessage(QStringLiteral("Failed to connect telemetry TCP endpoint: %1:%2").arg(host).arg(port));
        return false;
    }
    transport_type_ = TelemetryTransportType::Tcp;
    return openLink(std::move(link));
}

void GroundTelemetryService::close()
{
    const bool wasOpen = link_ && link_->isOpen();
    retry_timer_.stop();
    pending_commands_.clear();
    rx_byte_samples_.clear();
    tx_byte_samples_.clear();
    if (link_)
    {
        link_->close();
        link_.reset();
    }
    if (!wasOpen)
    {
        ++link_generation_;
    }
}

bool GroundTelemetryService::isOpen() const
{
    return link_ && link_->isOpen();
}

TelemetryTransportType GroundTelemetryService::transportType() const
{
    return transport_type_;
}

QString GroundTelemetryService::endpointDescription() const
{
    return link_ ? link_->endpointDescription() : QString();
}

quint64 GroundTelemetryService::linkGeneration() const
{
    return link_generation_;
}

double GroundTelemetryService::receiveBitsPerSecond() const
{
    return samplesBitsPerSecond(rx_byte_samples_, nowMs());
}

double GroundTelemetryService::transmitBitsPerSecond() const
{
    return samplesBitsPerSecond(tx_byte_samples_, nowMs());
}

quint64 GroundTelemetryService::totalReceivedBytes() const
{
    return total_rx_bytes_;
}

quint64 GroundTelemetryService::totalTransmittedBytes() const
{
    return total_tx_bytes_;
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

quint16 GroundTelemetryService::sendPeakSearchRangeCommand(quint32 startIndex, quint32 endIndex)
{
    PeakSearchRange range;
    range.start_index = startIndex;
    range.end_index = endIndex;
    return sendCommand(CommandId::SetPeakSearchRange, TelemetryCodec::serializePeakSearchRange(range));
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
    noteReceivedBytes(bytes.size());
    const QVector<TelemetryFrame> frames = codec_.feedBytes(bytes);
    const qint64 currentMs = nowMs();
    const quint64 crcErrors = codec_.crcErrorCount();
    if (crcErrors > last_logged_crc_errors_ &&
        (last_decoder_diagnostic_ms_ == 0 || currentMs - last_decoder_diagnostic_ms_ >= 1000))
    {
        reportProtocolDiagnostic(LogLevel::Warning,
                                 QStringLiteral("protocol.crc"),
                                 QStringLiteral("Telemetry decoder rejected frames due to CRC or protocol-version errors."),
                                 {{QStringLiteral("total_errors"), static_cast<qulonglong>(crcErrors)},
                                  {QStringLiteral("delta"), static_cast<qulonglong>(crcErrors - last_logged_crc_errors_)}});
        last_logged_crc_errors_ = crcErrors;
        last_decoder_diagnostic_ms_ = currentMs;
    }
    const quint64 droppedFrames = codec_.droppedFrameCount();
    if (droppedFrames > last_logged_dropped_frames_ &&
        (last_decoder_diagnostic_ms_ == 0 || currentMs - last_decoder_diagnostic_ms_ >= 1000))
    {
        reportProtocolDiagnostic(LogLevel::Warning,
                                 QStringLiteral("protocol.frame"),
                                 QStringLiteral("Telemetry decoder dropped oversized or malformed frames."),
                                 {{QStringLiteral("total_dropped"), static_cast<qulonglong>(droppedFrames)},
                                  {QStringLiteral("delta"), static_cast<qulonglong>(droppedFrames - last_logged_dropped_frames_)}});
        last_logged_dropped_frames_ = droppedFrames;
        last_decoder_diagnostic_ms_ = currentMs;
    }
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
            emit logMessage(QStringLiteral("命令 %1(%2) 序号 %3 超时：未收到天空端 ACK")
                                .arg(commandIdName(pending.command.command_id))
                                .arg(static_cast<quint16>(pending.command.command_id))
                                .arg(pending.command.command_seq));
            emit commandTimedOut(pending.command.command_id, pending.command.command_seq);
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
        else
        {
            reportProtocolDiagnostic(LogLevel::Warning, QStringLiteral("protocol.parse"),
                                     QStringLiteral("Failed to parse TelemetryBasic payload."),
                                     {{QStringLiteral("message_type"), static_cast<int>(frame.type)},
                                      {QStringLiteral("payload_bytes"), frame.payload.size()}});
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
        else
        {
            reportProtocolDiagnostic(LogLevel::Warning, QStringLiteral("protocol.parse"),
                                     QStringLiteral("Failed to parse WaveformDownsampled payload."),
                                     {{QStringLiteral("message_type"), static_cast<int>(frame.type)},
                                      {QStringLiteral("payload_bytes"), frame.payload.size()}});
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
        else
        {
            reportProtocolDiagnostic(LogLevel::Warning, QStringLiteral("protocol.parse"),
                                     QStringLiteral("Failed to parse WaveformFeature payload."),
                                     {{QStringLiteral("message_type"), static_cast<int>(frame.type)},
                                      {QStringLiteral("payload_bytes"), frame.payload.size()}});
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
        else
        {
            reportProtocolDiagnostic(LogLevel::Warning, QStringLiteral("protocol.parse"),
                                     QStringLiteral("Failed to parse TelemetryStatus payload."),
                                     {{QStringLiteral("message_type"), static_cast<int>(frame.type)},
                                      {QStringLiteral("payload_bytes"), frame.payload.size()}});
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
        else
        {
            reportProtocolDiagnostic(LogLevel::Warning, QStringLiteral("protocol.parse"),
                                     QStringLiteral("Failed to parse CommandAck payload."),
                                     {{QStringLiteral("message_type"), static_cast<int>(frame.type)},
                                      {QStringLiteral("payload_bytes"), frame.payload.size()}});
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
        else
        {
            reportProtocolDiagnostic(LogLevel::Warning, QStringLiteral("protocol.parse"),
                                     QStringLiteral("Failed to parse SkyConfig JSON payload."),
                                     {{QStringLiteral("message_type"), static_cast<int>(frame.type)},
                                      {QStringLiteral("payload_bytes"), frame.payload.size()}});
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
        else
        {
            reportProtocolDiagnostic(LogLevel::Warning, QStringLiteral("protocol.parse"),
                                     QStringLiteral("Failed to parse SkyConfigApplyResult JSON payload."),
                                     {{QStringLiteral("message_type"), static_cast<int>(frame.type)},
                                      {QStringLiteral("payload_bytes"), frame.payload.size()}});
        }
        break;
    }
    case MsgType::TemperatureControllerStatus:
    {
        TemperatureControllerData data;
        if (TelemetryCodec::parseTemperatureControllerStatus(frame.payload, data))
        {
            emit temperatureControllerStatusUpdated(data);
        }
        else
        {
            reportProtocolDiagnostic(LogLevel::Warning, QStringLiteral("protocol.parse"),
                                     QStringLiteral("Failed to parse TemperatureControllerStatus payload."),
                                     {{QStringLiteral("message_type"), static_cast<int>(frame.type)},
                                      {QStringLiteral("payload_bytes"), frame.payload.size()}});
        }
        break;
    }
    case MsgType::LogEvent:
    {
        LogRecord record;
        if (TelemetryCodec::parseLogRecord(frame.payload, record))
        {
            if (LogService *logService = LogService::instance())
            {
                record.fields.insert(QStringLiteral("ui_visible"), true);
                logService->publish(record);
            }
            else
            {
                emit logMessage(record.message);
            }
        }
        else
        {
            reportProtocolDiagnostic(LogLevel::Warning, QStringLiteral("protocol.parse"),
                                     QStringLiteral("Failed to parse LogEvent payload."),
                                     {{QStringLiteral("message_type"), static_cast<int>(frame.type)},
                                      {QStringLiteral("payload_bytes"), frame.payload.size()}});
        }
        break;
    }
    case MsgType::Heartbeat:
    case MsgType::Command:
        break;
    case MsgType::Error:
        reportProtocolDiagnostic(LogLevel::Error, QStringLiteral("protocol.error"),
                                 QStringLiteral("Received telemetry Error frame."),
                                 {{QStringLiteral("payload_hex"), QString::fromLatin1(frame.payload.toHex())},
                                  {QStringLiteral("payload_bytes"), frame.payload.size()}});
        break;
    default:
        reportProtocolDiagnostic(LogLevel::Warning, QStringLiteral("protocol.unknown"),
                                 QStringLiteral("Received unknown telemetry message type."),
                                 {{QStringLiteral("message_type"), static_cast<int>(frame.type)},
                                  {QStringLiteral("payload_bytes"), frame.payload.size()}});
        break;
    }
}

void GroundTelemetryService::sendCommandPayload(PendingCommand& pending)
{
    if (!link_ || !link_->isOpen())
    {
        return;
    }
    const QByteArray frame = codec_.encodeFrame(
        MsgType::Command,
        pending.encodedPayload,
        next_frame_seq_++,
        nowUs());
    const qint64 written = link_->writeBytes(frame);
    if (written > 0)
    {
        noteTransmittedBytes(written);
    }
}

bool GroundTelemetryService::openLink(std::unique_ptr<TelemetryLink> link)
{
    codec_.reset();
    close();
    link_ = std::move(link);
    attachLinkSignals();
    ++link_generation_;
    pending_commands_.clear();
    rx_byte_samples_.clear();
    tx_byte_samples_.clear();
    total_rx_bytes_ = 0;
    total_tx_bytes_ = 0;
    last_logged_crc_errors_ = 0;
    last_logged_dropped_frames_ = 0;
    last_decoder_diagnostic_ms_ = 0;
    if (link_ && link_->isOpen())
    {
        retry_timer_.start();
        emit linkOpenChanged(true);
        return true;
    }
    return false;
}

void GroundTelemetryService::attachLinkSignals()
{
    if (!link_)
    {
        return;
    }
    connect(link_.get(), &TelemetryLink::bytesReceived, this, [this](const QByteArray& bytes) {
        onBytesReceived(bytes);
    });
    connect(link_.get(), &TelemetryLink::openChanged, this, [this](bool open) {
        if (!open)
        {
            retry_timer_.stop();
            pending_commands_.clear();
            ++link_generation_;
        }
        emit linkOpenChanged(open);
    });
    connect(link_.get(), &TelemetryLink::errorOccurred, this, &GroundTelemetryService::logMessage);
    connect(link_.get(), &TelemetryLink::statusMessage, this, &GroundTelemetryService::logMessage);
}

void GroundTelemetryService::noteReceivedBytes(qint64 bytes)
{
    if (bytes <= 0)
    {
        return;
    }
    const qint64 currentMs = nowMs();
    rx_byte_samples_.push_back({currentMs, bytes});
    total_rx_bytes_ += static_cast<quint64>(bytes);
    pruneSamples(rx_byte_samples_, currentMs);
}

void GroundTelemetryService::noteTransmittedBytes(qint64 bytes)
{
    if (bytes <= 0)
    {
        return;
    }
    const qint64 currentMs = nowMs();
    tx_byte_samples_.push_back({currentMs, bytes});
    total_tx_bytes_ += static_cast<quint64>(bytes);
    pruneSamples(tx_byte_samples_, currentMs);
}

void GroundTelemetryService::reportProtocolDiagnostic(LogLevel level,
                                                      const QString& category,
                                                      const QString& message,
                                                      const QVariantMap& fields)
{
    const qint64 currentMs = nowMs();
    const bool throttle = category == QStringLiteral("protocol.parse") ||
        category == QStringLiteral("protocol.unknown");
    if (throttle && last_decoder_diagnostic_ms_ != 0 &&
        currentMs - last_decoder_diagnostic_ms_ < 1000)
    {
        return;
    }
    QVariantMap recordFields = fields;
    recordFields.insert(QStringLiteral("ui_visible"), true);
    if (LogService *logService = LogService::instance())
    {
        logService->publish(level, QStringLiteral("Ground"), category, message, recordFields);
    }
    else
    {
        emit logMessage(message);
    }
    if (throttle)
    {
        last_decoder_diagnostic_ms_ = currentMs;
    }
}

}  // namespace VaporView
