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
        publishTelemetryLog(LogLevel::Error,
                            QStringLiteral("telemetry.serial"),
                            QStringLiteral("ground_telemetry_serial_open_failed"),
                            QStringLiteral("无法打开地面端遥测串口。"),
                            {{QStringLiteral("error_code"), QStringLiteral("GROUND_TELEMETRY_SERIAL_OPEN_FAILED")},
                             {QStringLiteral("port"), portName},
                             {QStringLiteral("baud"), baudRate}});
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
        publishTelemetryLog(LogLevel::Error,
                            QStringLiteral("telemetry.tcp"),
                            QStringLiteral("ground_telemetry_tcp_connect_failed"),
                            QStringLiteral("无法连接地面端 TCP 遥测端点。"),
                            {{QStringLiteral("error_code"), QStringLiteral("GROUND_TELEMETRY_TCP_CONNECT_FAILED")},
                             {QStringLiteral("host"), host},
                             {QStringLiteral("port"), port}});
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
                                 QStringLiteral("telemetry_crc_or_version_error"),
                                 QStringLiteral("遥测解码器拒绝了 CRC 或协议版本错误的数据帧。"),
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
                                 QStringLiteral("telemetry_frame_dropped"),
                                 QStringLiteral("遥测解码器已丢弃过大或格式错误的数据帧。"),
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
            publishTelemetryLog(LogLevel::Warning,
                                QStringLiteral("telemetry.command"),
                                QStringLiteral("telemetry_command_ack_timeout"),
                                QStringLiteral("天空端命令 ACK 等待超时。"),
                                {{QStringLiteral("reason_code"), QStringLiteral("TELEMETRY_COMMAND_ACK_TIMEOUT")},
                                 {QStringLiteral("command_id"), commandIdName(pending.command.command_id)},
                                 {QStringLiteral("command_value"),
                                  static_cast<quint16>(pending.command.command_id)},
                                 {QStringLiteral("command_seq"), pending.command.command_seq}});
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
                                     QStringLiteral("telemetry_basic_parse_failed"),
                                     QStringLiteral("无法解析 TelemetryBasic 遥测载荷。"),
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
                                     QStringLiteral("waveform_downsampled_parse_failed"),
                                     QStringLiteral("无法解析 WaveformDownsampled 遥测载荷。"),
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
                                     QStringLiteral("waveform_feature_parse_failed"),
                                     QStringLiteral("无法解析 WaveformFeature 遥测载荷。"),
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
                                     QStringLiteral("telemetry_status_parse_failed"),
                                     QStringLiteral("无法解析 TelemetryStatus 遥测载荷。"),
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
                                     QStringLiteral("command_ack_parse_failed"),
                                     QStringLiteral("无法解析 CommandAck 遥测载荷。"),
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
                                     QStringLiteral("sky_config_parse_failed"),
                                     QStringLiteral("无法解析 SkyConfig JSON 遥测载荷。"),
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
                                     QStringLiteral("sky_config_apply_result_parse_failed"),
                                     QStringLiteral("无法解析 SkyConfigApplyResult JSON 遥测载荷。"),
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
                                     QStringLiteral("temperature_controller_status_parse_failed"),
                                     QStringLiteral("无法解析 TemperatureControllerStatus 遥测载荷。"),
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
            const bool published = LogService::withCurrentInstance([&](LogService& logService) {
                record.fields.insert(QStringLiteral("ui_visible"), true);
                if (!record.fields.contains(QStringLiteral("ui_visibility")))
                {
                    record.fields.insert(QStringLiteral("ui_visibility"),
                                         record.level >= LogLevel::Warning ? QStringLiteral("attention")
                                                                    : QStringLiteral("details"));
                }
                logService.publish(record);
            });
            if (!published)
            {
                emit logMessage(record.message);
            }
        }
        else
        {
            reportProtocolDiagnostic(LogLevel::Warning, QStringLiteral("protocol.parse"),
                                     QStringLiteral("log_event_parse_failed"),
                                     QStringLiteral("无法解析 LogEvent 遥测载荷。"),
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
                                 QStringLiteral("telemetry_error_frame_received"),
                                 QStringLiteral("已收到遥测 Error 帧。"),
                                 {{QStringLiteral("error_code"), QStringLiteral("TELEMETRY_ERROR_FRAME")},
                                  {QStringLiteral("payload_hex"), QString::fromLatin1(frame.payload.toHex())},
                                  {QStringLiteral("payload_bytes"), frame.payload.size()}});
        break;
    default:
        reportProtocolDiagnostic(LogLevel::Warning, QStringLiteral("protocol.unknown"),
                                 QStringLiteral("unknown_telemetry_message_type"),
                                 QStringLiteral("已收到未知遥测消息类型。"),
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
    connect(link_.get(), &TelemetryLink::errorOccurred, this, [this](const QString& systemError) {
        publishTelemetryLog(LogLevel::Warning,
                            QStringLiteral("telemetry.link"),
                            QStringLiteral("ground_telemetry_link_error"),
                            QStringLiteral("地面端遥测链路异常。"),
                            {{QStringLiteral("reason_code"), QStringLiteral("GROUND_TELEMETRY_LINK_ERROR")},
                             {QStringLiteral("system_error"), systemError}});
    });
    connect(link_.get(), &TelemetryLink::statusMessage, this, [this](const QString& statusText) {
        publishTelemetryLog(LogLevel::Info,
                            QStringLiteral("telemetry.link"),
                            QStringLiteral("ground_telemetry_link_status"),
                            QStringLiteral("地面端遥测链路状态已更新。"),
                            {{QStringLiteral("external_raw_text"), statusText}});
    });
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

void GroundTelemetryService::publishTelemetryLog(LogLevel level,
                                                 const QString& category,
                                                 const QString& event,
                                                 const QString& message,
                                                 QVariantMap fields)
{
    fields.insert(QStringLiteral("event"), event);
    fields.insert(QStringLiteral("ui_visible"), true);
    if (!fields.contains(QStringLiteral("ui_visibility")))
    {
        fields.insert(QStringLiteral("ui_visibility"),
                      level >= LogLevel::Warning ? QStringLiteral("attention")
                                                 : QStringLiteral("details"));
    }
    if (level >= LogLevel::Error &&
        !fields.contains(QStringLiteral("error_code")) &&
        !fields.contains(QStringLiteral("reason_code")))
    {
        fields.insert(QStringLiteral("error_code"), QStringLiteral("GROUND_TELEMETRY_ERROR"));
    }
    if (!LogService::withCurrentInstance([&](LogService& logService) {
            logService.publish(level, QStringLiteral("Ground"), category, message, fields);
        }))
    {
        emit logMessage(message);
    }
}

void GroundTelemetryService::reportProtocolDiagnostic(LogLevel level,
                                                      const QString& category,
                                                      const QString& event,
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
    if (level >= LogLevel::Error &&
        !recordFields.contains(QStringLiteral("error_code")) &&
        !recordFields.contains(QStringLiteral("reason_code")))
    {
        recordFields.insert(QStringLiteral("error_code"), QStringLiteral("GROUND_PROTOCOL_ERROR"));
    }
    publishTelemetryLog(level, category, event, message, recordFields);
    if (throttle)
    {
        last_decoder_diagnostic_ms_ = currentMs;
    }
}

}  // namespace VaporView
