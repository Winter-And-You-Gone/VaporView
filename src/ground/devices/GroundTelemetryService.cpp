#include "ground/devices/GroundTelemetryService.h"

#include "LogService.h"
#include "SerialTelemetryLink.h"
#include "SerialBaudRate.h"
#include "TcpTelemetryLink.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QPointer>
#include <QRandomGenerator>
#include <QScopeGuard>
#include <QUuid>
#include <limits>
#include <algorithm>
#include <chrono>
#include <memory>

namespace VaporView
{
namespace
{
constexpr int kCommandMaxRetries = 3;
constexpr qint64 kBitRateWindowMs = 5000;

void logCommandContext(const CommandMessage& command, const QString& event, LogLevel level,
                       const QVariantMap& extra = {}, const char *file = VAPORVIEW_LOG_FILE,
                       const char *function = VAPORVIEW_LOG_FUNCTION, int line = VAPORVIEW_LOG_LINE)
{
    if (command.correlation_id.isEmpty()) return;
    LogRecord record;
    record.level = level;
    record.source = QStringLiteral("Ground");
    record.category = QStringLiteral("telemetry.command");
    if (event == QStringLiteral("ground_command_send")) record.message = QStringLiteral("已发起天空端命令。");
    else if (event == QStringLiteral("ground_command_retry")) record.message = QStringLiteral("正在重试天空端命令。");
    else if (event == QStringLiteral("ground_command_timeout")) record.message = QStringLiteral("等待天空端命令响应超时。");
    else if (event == QStringLiteral("ground_command_ack_context_mismatch")) record.message = QStringLiteral("天空端响应的关联标识不匹配。");
    else if (event == QStringLiteral("ground_command_disconnected")) record.message = QStringLiteral("连接已断开，天空端命令尚未完成。");
    else if (event == QStringLiteral("ground_device_operation_response")) record.message = QStringLiteral("已收到天空端设备操作结果。");
    else if (event == QStringLiteral("ground_config_apply_result_context")) record.message = QStringLiteral("已收到天空端配置应用关联结果。");
    else if (event == QStringLiteral("ground_device_result_context_expired")) record.message = QStringLiteral("设备操作结果等待上下文已过期。");
    else if (event == QStringLiteral("ground_device_result_context_evicted")) record.message = QStringLiteral("设备操作结果等待上下文因容量限制被移除。");
    else if (event == QStringLiteral("ground_config_result_context_expired")) record.message = QStringLiteral("配置结果等待上下文已过期。");
    else if (event == QStringLiteral("ground_config_result_context_evicted")) record.message = QStringLiteral("配置结果等待上下文因容量限制被移除。");
    else record.message = QStringLiteral("已收到天空端命令响应。");
    record.correlation_id = command.correlation_id;
    record.session_id = command.session_id;
    record.fields = extra;
    LogService::addCallLocation(record, file, function, line);
    record.fields.insert(QStringLiteral("event"), event);
    record.fields.insert(QStringLiteral("command_seq"), command.command_seq);
    record.fields.insert(QStringLiteral("command_id"), static_cast<quint16>(command.command_id));
    if (!LogService::withCurrentInstance([&](LogService& service) { service.publish(record); }))
        LogService::writeLogFallback(record);
}

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
    if (!VaporView::isValidSerialBaudRate(baudRate))
    {
        publishTelemetryLog(LogLevel::Warning,
                            QStringLiteral("telemetry.serial"),
                            QStringLiteral("ground_telemetry_serial_baud_invalid"),
                            QStringLiteral("地面端遥测串口波特率必须为正整数。"),
                            {{QStringLiteral("error_code"), QStringLiteral("INVALID_BAUD_RATE")},
                             {QStringLiteral("port"), portName},
                             {QStringLiteral("configured_baud"), baudRate}});
        return false;
    }
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
    QString systemError;
    const QMetaObject::Connection errorConnection =
        QObject::connect(link.get(), &TelemetryLink::errorOccurred,
                         [&systemError](const QString& error) {
                             systemError = error;
                         });
    const bool ok = link->connectToHost(host, port);
    QObject::disconnect(errorConnection);
    if (!ok)
    {
        const QString endpoint = QStringLiteral("%1:%2").arg(host.trimmed()).arg(port);
        QVariantMap fields{
            {QStringLiteral("error_code"), QStringLiteral("GROUND_TELEMETRY_TCP_CONNECT_FAILED")},
            {QStringLiteral("endpoint"), endpoint},
            {QStringLiteral("host"), host},
            {QStringLiteral("port"), port},
            {QStringLiteral("ui_message"),
             QStringLiteral("无法连接天空端 TCP 数传端点（%1）。").arg(endpoint)},
            {QStringLiteral("ui_visibility"), QStringLiteral("details")},
        };
        if (!systemError.isEmpty())
        {
            fields.insert(QStringLiteral("system_error"), systemError);
        }
        publishTelemetryLog(LogLevel::Error,
                            QStringLiteral("telemetry.tcp"),
                            QStringLiteral("ground_telemetry_tcp_connect_failed"),
                            QStringLiteral("无法连接天空端 TCP 数传端点。"),
                            fields);
        return false;
    }
    transport_type_ = TelemetryTransportType::Tcp;
    return openLink(std::move(link));
}

bool GroundTelemetryService::openTcpAsync(const QString& host, quint16 port)
{
    auto link = std::make_unique<TcpTelemetryLink>();
    const QPointer<TcpTelemetryLink> tcp(link.get());
    const QPointer<GroundTelemetryService> self(this);
    transport_type_ = TelemetryTransportType::Tcp;
    openLink(std::move(link));
    if (!self || !tcp || link_.get() != tcp.data())
        return false;
    return tcp->connectToHostAsync(host, port);
}

bool GroundTelemetryService::isConnecting() const
{
    const auto *tcp = qobject_cast<const TcpTelemetryLink *>(link_.get());
    return tcp && tcp->isConnecting();
}

void GroundTelemetryService::close()
{
    const QPointer<GroundTelemetryService> self(this);
    const bool wasOpen = link_ && link_->isOpen();
    TelemetryLink *const closingLink = link_.get();
    const quint64 generation = link_generation_;
    retry_timer_.stop();
    clearPendingForDisconnect();
    if (!self || generation != link_generation_ || link_.get() != closingLink)
        return;
    rx_byte_samples_.clear();
    tx_byte_samples_.clear();
    if (link_)
    {
        const QPointer<TelemetryLink> linkGuard(link_.get());
        link_->close();
        if (!self || !linkGuard || link_.get() != linkGuard.data())
            return;
        if (open_changed_depth_.contains(linkGuard.data()))
            link_.release()->deleteLater();
        else
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

quint16 GroundTelemetryService::sendCommand(CommandId commandId, const QByteArray& payload, QString *correlationId)
{
    if (correlationId) correlationId->clear();
    DeviceOperationRequest operation;
    const bool isDeviceOperation = commandId == CommandId::DeviceOperation &&
        TelemetryCodec::parseDeviceOperationRequest(payload, operation);
    if (isDeviceOperation)
    {
        bool duplicate = device_requests_being_sent_.contains(operation.request_id) ||
            recently_rejected_operations_.contains(operation.request_id);
        for (auto it = pending_commands_.cbegin(); !duplicate && it != pending_commands_.cend(); ++it)
        {
            DeviceOperationRequest outstanding;
            duplicate = it->command.command_id == CommandId::DeviceOperation &&
                TelemetryCodec::parseDeviceOperationRequest(it->command.payload, outstanding) &&
                outstanding.request_id == operation.request_id;
        }
        if (duplicate)
        {
            publishTelemetryLog(LogLevel::Warning, QStringLiteral("telemetry.command"),
                                QStringLiteral("telemetry_device_request_id_conflict"),
                                QStringLiteral("设备请求标识仍在使用，命令未发送。"),
                                {{QStringLiteral("reason_code"), QStringLiteral("DEVICE_REQUEST_ID_IN_USE")},
                                 {QStringLiteral("request_id"), operation.request_id}});
            return 0;
        }
    }
    quint16 sequence = 0;
    for (quint32 attempt = 0; attempt < 65536; ++attempt)
    {
        const quint16 candidate = next_command_seq_++;
        if (candidate && !pending_commands_.contains(candidate) && !completed_awaiting_ack_.contains(candidate) &&
            !config_completed_awaiting_ack_.contains(candidate) &&
            !config_results_awaited_.contains(candidate) &&
            !command_sequences_being_sent_.contains(candidate))
        {
            sequence = candidate;
            break;
        }
    }
    if (!sequence) return 0;
    // Publishing may synchronously re-enter sendCommand before pending insertion.
    command_sequences_being_sent_.insert(sequence);
    if (isDeviceOperation) device_requests_being_sent_.insert(operation.request_id);
    CommandMessage command;
    command.command_id = commandId;
    command.command_seq = sequence;
    command.payload = payload;
    command.typed_response_context_requested = isDeviceOperation;
    command.config_result_context_requested = commandId == CommandId::SetSkyConfig;
    command.config_ack_session_omission_supported = command.config_result_context_requested;
    if (commandId != CommandId::RequestStatus && commandId != CommandId::QueryDeviceStatus &&
        commandId != CommandId::GetSkyConfig && commandId != CommandId::RequestOneWaveform)
        command.correlation_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    command.extended_ack_context_requested = !command.correlation_id.isEmpty();
    command.extended_result_context_requested = isDeviceOperation || command.config_result_context_requested;

    PendingCommand pending;
    pending.command = command;
    if (isDeviceOperation && operation.device_id == SkyDeviceId::Epsilon)
    {
        pending.retry_interval_ms = 15000;
        if (operation.operation == DeviceOperation::ReadEpsilonSettings ||
            operation.operation == DeviceOperation::ApplyEpsilonSettings ||
            operation.operation == DeviceOperation::RestartEpsilonDevice ||
            operation.operation == DeviceOperation::CalibrateEpsilonLevel ||
            operation.operation == DeviceOperation::CalibrateEpsilonAccelerometer ||
            operation.operation == DeviceOperation::CalibrateEpsilonGyroscope ||
            operation.operation == DeviceOperation::ReadEpsilonDgnss || operation.operation == DeviceOperation::ApplyEpsilonDgnss ||
            operation.operation == DeviceOperation::CalibrateEpsilonMagnetic2D || operation.operation == DeviceOperation::CalibrateEpsilonMagnetic3D ||
            operation.operation == DeviceOperation::CancelEpsilonMagneticCalibration)
        {
            pending.retry_interval_ms = TelemetryCodec::epsilonDeviceOperationTimeoutMs(operation);
            // These operations must never be replayed after an ambiguous timeout.
            pending.retry_count = kCommandMaxRetries;
        }
    }
    if (isDeviceOperation &&
        operation.device_id == SkyDeviceId::Fpga && operation.operation == DeviceOperation::FpgaControl)
    {
        pending.retry_interval_ms = 120000;
        pending.retry_count = kCommandMaxRetries; // Never replay hardware writes.
    }
    pending.encodedPayload = TelemetryCodec::serializeCommand(command);
    if (command.extended_ack_context_requested) {
        CommandMessage encodedCommand;
        const bool parsed = TelemetryCodec::parseCommand(pending.encodedPayload, encodedCommand);
        pending.command.extended_ack_context_requested = parsed && encodedCommand.extended_ack_context_requested;
        pending.command.extended_result_context_requested = parsed && encodedCommand.extended_result_context_requested;
        pending.command.config_ack_session_omission_supported = parsed && encodedCommand.config_ack_session_omission_supported;
    }
    pending.next_retry_ms = nowMs() + pending.retry_interval_ms;
    const quint64 sendGeneration = link_generation_;
    logCommandContext(command, QStringLiteral("ground_command_send"), LogLevel::Info);
    if (sendGeneration == link_generation_)
        sendCommandPayload(pending);
    const bool sentOnSameLink = sendGeneration == link_generation_;
    if (sentOnSameLink)
        pending_commands_.insert(command.command_seq, pending);
    command_sequences_being_sent_.remove(sequence);
    if (isDeviceOperation)
        device_requests_being_sent_.remove(operation.request_id);
    if (!sentOnSameLink) {
        logCommandContext(command, QStringLiteral("ground_command_disconnected"), LogLevel::Warning);
        return 0;
    }
    if (correlationId) *correlationId = command.correlation_id;
    return command.command_seq;
}

quint16 GroundTelemetryService::sendDeviceOperation(const DeviceOperationRequest& request)
{
    return sendCommand(CommandId::DeviceOperation,
                        TelemetryCodec::serializeDeviceOperationRequest(request));
}

bool GroundTelemetryService::sendRtcmCorrectionData(const QByteArray& data)
{
    if (!link_ || !link_->isOpen() || data.isEmpty() || data.size() > 4096)
    {
        return false;
    }
    if (rtcm_sequence_.sequence == std::numeric_limits<quint64>::max())
        rtcm_sequence_ = {QRandomGenerator::global()->generate64() | 1ULL, 0};
    RtcmFrameSequence sequence = rtcm_sequence_;
    ++sequence.sequence;
    const QByteArray payload = TelemetryCodec::serializeRtcmCorrectionData(
        data, rtcm_observability_supported_ ? sequence : RtcmFrameSequence{});
    const QByteArray frame = codec_.encodeFrame(
        MsgType::RtcmCorrectionData,
        payload,
        next_frame_seq_++,
        nowUs());
    const qint64 written = link_->writeBytes(frame);
    if (written > 0)
    {
        // A zero/failed write sent nothing and must not become a link loss.
        // Partial writes consume the sequence: that frame cannot be decoded.
        rtcm_sequence_ = sequence;
        noteTransmittedBytes(written);
    }
    return written == frame.size();
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
    const QPointer<GroundTelemetryService> self(this);
    const quint64 receiveGeneration = link_generation_;
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
        if (receiveGeneration != link_generation_) break;
        dispatchFrame(frame);
        if (!self) return;
    }
}

void GroundTelemetryService::onRetryTimer()
{
    const qint64 currentMs = nowMs();
    const quint64 generation = link_generation_;
    const auto sequences = pending_commands_.keys();
    for (quint16 sequence : sequences)
    {
        auto it = pending_commands_.find(sequence);
        if (it == pending_commands_.end() || it->next_retry_ms > currentMs)
        {
            continue;
        }
        if (it->retry_count >= kCommandMaxRetries)
        {
            const PendingCommand pending = it.value();
            pending_commands_.erase(it);
            logCommandContext(pending.command, QStringLiteral("ground_command_timeout"), LogLevel::Warning);
            if (generation != link_generation_) return;
            const bool awaitingResult = pending.ack_received;
            publishTelemetryLog(LogLevel::Warning,
                                QStringLiteral("telemetry.command"),
                                awaitingResult ? QStringLiteral("telemetry_device_result_timeout")
                                               : QStringLiteral("telemetry_command_ack_timeout"),
                                awaitingResult ? QStringLiteral("天空端设备操作结果等待超时。")
                                               : QStringLiteral("天空端命令 ACK 等待超时。"),
                                {{QStringLiteral("reason_code"), awaitingResult ? QStringLiteral("TELEMETRY_DEVICE_RESULT_TIMEOUT")
                                                                                : QStringLiteral("TELEMETRY_COMMAND_ACK_TIMEOUT")},
                                 {QStringLiteral("command_id"), commandIdName(pending.command.command_id)},
                                 {QStringLiteral("command_value"),
                                  static_cast<quint16>(pending.command.command_id)},
                                 {QStringLiteral("command_seq"), pending.command.command_seq}},
                                pending.command.correlation_id, pending.command.session_id);
            if (generation != link_generation_) return;
            emit commandTimedOut(pending.command.command_id, pending.command.command_seq);
            if (generation != link_generation_) return;
            continue;
        }
        ++it->retry_count;
        it->next_retry_ms = currentMs + it->retry_interval_ms;
        PendingCommand pending = it.value();
        logCommandContext(pending.command, QStringLiteral("ground_command_retry"), LogLevel::Warning,
                          {{QStringLiteral("retry_count"), pending.retry_count}});
        if (generation != link_generation_) return;
        const auto current = pending_commands_.constFind(sequence);
        if (current == pending_commands_.cend() || current->command.correlation_id != pending.command.correlation_id) continue;
        sendCommandPayload(pending);
        if (generation != link_generation_) return;
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
            rtcm_observability_supported_ = status.rtcm_observability_version == 1;
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
        const quint64 dispatchGeneration = link_generation_;
        CommandAck ack;
        if (TelemetryCodec::parseCommandAck(frame.payload, ack))
        {
            const auto pending = pending_commands_.constFind(ack.command_seq);
            const auto configCompleted = config_completed_awaiting_ack_.constFind(ack.command_seq);
            if (pending == pending_commands_.cend() && configCompleted != config_completed_awaiting_ack_.cend())
            {
                if (ack.command_id != CommandId::SetSkyConfig || ack.correlation_id.isEmpty() ||
                    (ack.extended_ack_context && !configCompleted->extended_ack_context_requested) ||
                    (ack.session_id_omitted && !configCompleted->config_ack_session_omission_supported) ||
                    ack.correlation_id != configCompleted->correlation_id ||
                    (!ack.session_id_omitted && ack.session_id != configCompleted->session_id))
                    break;
                const CommandMessage context = *configCompleted;
                if (ack.session_id_omitted) ack.session_id = context.session_id;
                config_completed_awaiting_ack_.remove(ack.command_seq);
                logCommandContext(context, QStringLiteral("ground_command_ack"),
                                  ack.error_code == CommandErrorCode::Ok ? LogLevel::Info : LogLevel::Warning,
                                  {{QStringLiteral("ack_error_code"), static_cast<quint32>(ack.error_code)}});
                if (dispatchGeneration == link_generation_) emit commandAckReceived(ack);
                break;
            }
            const auto completed = completed_awaiting_ack_.constFind(ack.command_seq);
            if (pending == pending_commands_.cend() && completed != completed_awaiting_ack_.cend())
            {
                if (ack.session_id_omitted || (ack.extended_ack_context && !completed->extended_ack_context_requested) ||
                    ack.command_id != completed->command_id ||
                    (completed->command_id == CommandId::DeviceOperation && !ack.typed_response_context_supported) ||
                    ack.correlation_id != completed->correlation_id || ack.session_id != completed->session_id)
                    break;
                const CommandMessage context = *completed;
                completed_awaiting_ack_.remove(ack.command_seq);
                logCommandContext(context, QStringLiteral("ground_command_ack"),
                                  ack.error_code == CommandErrorCode::Ok ? LogLevel::Info : LogLevel::Warning,
                                  {{QStringLiteral("ack_error_code"), static_cast<quint32>(ack.error_code)}});
                if (dispatchGeneration == link_generation_) emit commandAckReceived(ack);
                break;
            }
            if ((ack.extended_ack_context && (pending == pending_commands_.cend() ||
                                              !pending->command.extended_ack_context_requested || ack.correlation_id.isEmpty())) ||
                (ack.session_id_omitted && (pending == pending_commands_.cend() || ack.command_id != CommandId::SetSkyConfig ||
                                            !pending->command.config_ack_session_omission_supported)) ||
                (pending != pending_commands_.cend() && pending->command.command_id != ack.command_id) ||
                (!ack.correlation_id.isEmpty() &&
                 (pending == pending_commands_.cend() || pending->command.correlation_id != ack.correlation_id)) ||
                (pending != pending_commands_.cend() && pending->context_session_observed &&
                 (!ack.typed_response_context_supported || ack.correlation_id != pending->command.correlation_id ||
                  ack.session_id != pending->command.session_id)))
            {
                if (pending != pending_commands_.cend())
                    logCommandContext(pending->command, QStringLiteral("ground_command_ack_context_mismatch"), LogLevel::Warning);
                break;
            }
            CommandMessage ackLogContext;
            bool hasAckLogContext = false;
            if (pending != pending_commands_.cend() && pending->command.command_id == ack.command_id)
            {
                auto context = pending->command;
                pending_commands_[ack.command_seq].ack_received = true;
                pending_commands_[ack.command_seq].command.typed_response_context_requested =
                    ack.typed_response_context_supported && !ack.correlation_id.isEmpty();
                context.typed_response_context_requested = pending_commands_[ack.command_seq].command.typed_response_context_requested;
                if (!ack.correlation_id.isEmpty() && !ack.session_id_omitted) {
                    context.session_id = ack.session_id;
                    pending_commands_[ack.command_seq].command.session_id = ack.session_id;
                }
                ackLogContext = std::move(context);
                hasAckLogContext = true;
            }
            DeviceOperationRequest operation;
            const bool settingsReplyRequired = pending != pending_commands_.cend() &&
                pending->command.command_id == CommandId::DeviceOperation &&
                TelemetryCodec::parseDeviceOperationRequest(pending->command.payload, operation) &&
                (operation.operation == DeviceOperation::FpgaControl || operation.operation == DeviceOperation::ReadEpsilonSettings || operation.operation == DeviceOperation::ApplyEpsilonSettings ||
                 operation.operation == DeviceOperation::RestartEpsilonDevice ||
                 operation.operation == DeviceOperation::CalibrateEpsilonLevel ||
                 operation.operation == DeviceOperation::CalibrateEpsilonAccelerometer ||
                 operation.operation == DeviceOperation::CalibrateEpsilonGyroscope ||
                 operation.operation == DeviceOperation::ReadEpsilonDgnss || operation.operation == DeviceOperation::ApplyEpsilonDgnss ||
                 operation.operation == DeviceOperation::CalibrateEpsilonMagnetic2D || operation.operation == DeviceOperation::CalibrateEpsilonMagnetic3D);
            const bool typedFailureExpected = settingsReplyRequired && ack.error_code != CommandErrorCode::Ok &&
                ack.error_code != CommandErrorCode::UnknownCommand && ack.error_code != CommandErrorCode::InvalidPayload;
            CommandMessage evictedContext;
            quint32 evictedRequestId = 0;
            bool contextEvicted = false;
            CommandMessage evictedConfigContext;
            bool configContextEvicted = false;
            if (typedFailureExpected)
            {
                const quint64 generation = link_generation_;
                const quint32 requestId = operation.request_id;
                const QString correlationId = pending->command.correlation_id;
                QTimer::singleShot(30000, this, [this, generation, requestId, sequence = ack.command_seq, correlationId]() {
                    const auto outstanding = pending_commands_.constFind(sequence);
                    DeviceOperationRequest request;
                    if (generation == link_generation_ && outstanding != pending_commands_.cend() &&
                        outstanding->command.correlation_id == correlationId &&
                        TelemetryCodec::parseDeviceOperationRequest(outstanding->command.payload, request) && request.request_id == requestId) {
                        const CommandMessage context = outstanding->command;
                        pending_commands_.remove(sequence);
                        logCommandContext(context, QStringLiteral("ground_device_result_context_expired"), LogLevel::Warning,
                                          {{QStringLiteral("request_id"), requestId},
                                           {QStringLiteral("stage"), QStringLiteral("awaiting_failed_device_result")}});
                    }
                });
            }
            if (!typedFailureExpected && (!settingsReplyRequired || ack.error_code != CommandErrorCode::Ok) &&
                pending != pending_commands_.cend() && pending->command.command_id == CommandId::DeviceOperation &&
                TelemetryCodec::parseDeviceOperationRequest(pending->command.payload, operation)) {
                if (recently_rejected_operations_.size() >= 256) {
                    const auto evicted = recently_rejected_operations_.begin();
                    evictedRequestId = evicted.key();
                    evictedContext = evicted.value();
                    contextEvicted = true;
                    recently_rejected_operations_.erase(evicted);
                }
                const CommandMessage context = pending->command;
                const quint32 requestId = operation.request_id;
                const quint64 generation = link_generation_;
                recently_rejected_operations_.insert(requestId, context);
                QTimer::singleShot(30000, this, [this, generation, requestId, context] {
                    const auto it = recently_rejected_operations_.constFind(requestId);
                    if (generation == link_generation_ && it != recently_rejected_operations_.cend() &&
                        it->correlation_id == context.correlation_id) {
                        recently_rejected_operations_.remove(requestId);
                        logCommandContext(context, QStringLiteral("ground_device_result_context_expired"), LogLevel::Info,
                                          {{QStringLiteral("request_id"), requestId},
                                           {QStringLiteral("stage"), QStringLiteral("awaiting_device_result")}});
                    }
                });
            }
            if (pending != pending_commands_.cend() && pending->command.command_id == CommandId::SetSkyConfig &&
                ack.config_result_context_expected.value_or(true)) {
                if (config_results_awaited_.size() >= 256) {
                    const auto evicted = config_results_awaited_.begin();
                    evictedConfigContext = evicted->command;
                    configContextEvicted = true;
                    config_results_awaited_.erase(evicted);
                }
                CommandMessage context = pending->command;
                context.payload.clear();
                const auto generation = link_generation_;
                config_results_awaited_.insert(ack.command_seq, {context, !ack.correlation_id.isEmpty() && !ack.session_id_omitted});
                QTimer::singleShot(30000, this, [this, generation, sequence = ack.command_seq, id = context.correlation_id] {
                    const auto entry = config_results_awaited_.constFind(sequence);
                    if (generation == link_generation_ && entry != config_results_awaited_.cend() && entry->command.correlation_id == id) {
                        const CommandMessage expiredContext = entry->command;
                        config_results_awaited_.remove(sequence);
                        logCommandContext(expiredContext, QStringLiteral("ground_config_result_context_expired"), LogLevel::Warning,
                                          {{QStringLiteral("stage"), QStringLiteral("awaiting_config_result")}});
                    }
                });
            }
            if ((!typedFailureExpected && ack.error_code != CommandErrorCode::Ok) || !settingsReplyRequired)
                pending_commands_.remove(ack.command_seq);
            if (contextEvicted) {
                logCommandContext(evictedContext, QStringLiteral("ground_device_result_context_evicted"), LogLevel::Warning,
                                  {{QStringLiteral("request_id"), evictedRequestId},
                                   {QStringLiteral("stage"), QStringLiteral("awaiting_device_result")}});
                if (dispatchGeneration != link_generation_) break;
            }
            if (configContextEvicted) {
                const QPointer<GroundTelemetryService> self(this);
                logCommandContext(evictedConfigContext, QStringLiteral("ground_config_result_context_evicted"), LogLevel::Warning,
                                  {{QStringLiteral("stage"), QStringLiteral("awaiting_config_result")}});
                if (!self) return;
                if (dispatchGeneration != link_generation_) break;
            }
            if (hasAckLogContext)
                logCommandContext(ackLogContext, QStringLiteral("ground_command_ack"),
                                  ack.error_code == CommandErrorCode::Ok ? LogLevel::Info : LogLevel::Warning,
                                  {{QStringLiteral("ack_error_code"), static_cast<quint32>(ack.error_code)}});
            if (dispatchGeneration != link_generation_) break;
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
    case MsgType::SkyConfigApplyResultContext:
    {
        QJsonObject result;
        QString correlationId, sessionId;
        quint16 sequence = 0;
        bool extendedResult = false;
        if (TelemetryCodec::parseSkyConfigApplyResultContext(frame.payload, result, correlationId, sessionId, sequence, &extendedResult))
        {
            auto pending = pending_commands_.find(sequence);
            CommandMessage context;
            if (pending != pending_commands_.end()) {
                if (pending->command.command_id != CommandId::SetSkyConfig ||
                    (extendedResult && !pending->command.extended_result_context_requested) ||
                    pending->command.correlation_id != correlationId ||
                    (pending->context_session_observed && pending->command.session_id != sessionId)) break;
                context = pending->command;
                context.session_id = sessionId;
                if (!pending->ack_received) {
                    if (config_completed_awaiting_ack_.size() >= 256)
                        config_completed_awaiting_ack_.erase(config_completed_awaiting_ack_.begin());
                    CommandMessage reserved = context;
                    reserved.payload.clear();
                    const auto generation = link_generation_;
                    config_completed_awaiting_ack_.insert(sequence, reserved);
                    QTimer::singleShot(30000, this, [this, generation, sequence, correlationId] {
                        const auto entry = config_completed_awaiting_ack_.constFind(sequence);
                        if (generation == link_generation_ && entry != config_completed_awaiting_ack_.cend() && entry->correlation_id == correlationId)
                            config_completed_awaiting_ack_.remove(sequence);
                    });
                }
                pending_commands_.erase(pending);
            } else {
                const auto awaited = config_results_awaited_.constFind(sequence);
                if (awaited == config_results_awaited_.cend() || awaited->command.command_id != CommandId::SetSkyConfig ||
                    awaited->command.correlation_id != correlationId ||
                    (extendedResult && !awaited->command.extended_result_context_requested) ||
                    (awaited->sessionKnown && awaited->command.session_id != sessionId)) break;
                context = awaited->command;
                context.session_id = sessionId;
                config_results_awaited_.remove(sequence);
            }
            logCommandContext(context, QStringLiteral("ground_config_apply_result_context"), LogLevel::Info,
                              {{QStringLiteral("config_apply_result"), result.toVariantMap()}});
        }
        else
        {
            reportProtocolDiagnostic(LogLevel::Warning, QStringLiteral("protocol.parse"),
                                     QStringLiteral("sky_config_apply_context_parse_failed"),
                                     QStringLiteral("无法解析天空端配置应用关联结果。"),
                                     {{QStringLiteral("message_type"), static_cast<int>(frame.type)},
                                      {QStringLiteral("payload_bytes"), frame.payload.size()}});
        }
        break;
    }
    case MsgType::SerialPortDetectionResult:
    {
        const QJsonDocument document = QJsonDocument::fromJson(frame.payload);
        if (document.isObject())
        {
            emit serialPortDetectionResultReceived(document.object());
        }
        else
        {
            reportProtocolDiagnostic(LogLevel::Warning, QStringLiteral("protocol.parse"),
                                     QStringLiteral("serial_port_detection_result_parse_failed"),
                                     QStringLiteral("无法解析串口自动识别结果遥测载荷。"),
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
            data.timestamp = std::chrono::steady_clock::now();
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
    case MsgType::DeviceOperationResponse:
    case MsgType::DeviceOperationResponseContext:
    {
        const quint64 dispatchGeneration = link_generation_;
        const bool contextual = frame.type == MsgType::DeviceOperationResponseContext;
        DeviceOperationResponse response;
        bool extendedResult = false;
        if (contextual ? TelemetryCodec::parseDeviceOperationResponseContext(frame.payload, response, &extendedResult)
                       : TelemetryCodec::parseDeviceOperationResponse(frame.payload, response))
        {
            bool matched = false;
            for (auto it = pending_commands_.begin(); it != pending_commands_.end(); ++it)
            {
                DeviceOperationRequest request;
                if (it->command.command_id == CommandId::DeviceOperation &&
                    TelemetryCodec::parseDeviceOperationRequest(it->command.payload, request) && request.request_id == response.request_id &&
                    request.device_id == response.device_id && request.operation == response.operation)
                {
                    if (contextual && ((extendedResult && !it->command.extended_result_context_requested) ||
                                       response.correlation_id != it->command.correlation_id)) break;
                    if ((it->context_session_observed && !contextual) ||
                        (it->ack_received && contextual != it->command.typed_response_context_requested) ||
                        (contextual && (it->ack_received || it->context_session_observed) &&
                         response.session_id != it->command.session_id)) return;
                    if (contextual && !it->ack_received) {
                        it->command.session_id = response.session_id;
                        it->context_session_observed = true;
                    }
                    if (!contextual) {
                        response.correlation_id = it->command.correlation_id;
                        response.session_id = it->command.session_id;
                    }
                    matched = true;
                    const CommandMessage logContext = it->command;
                    EpsilonMaintenanceResult progress;
                    bool finished = true;
                    if (request.operation == DeviceOperation::CalibrateEpsilonMagnetic2D || request.operation == DeviceOperation::CalibrateEpsilonMagnetic3D)
                    {
                        if (!(response.payload.isEmpty() && response.error_code != CommandErrorCode::Ok) &&
                            (!TelemetryCodec::parseEpsilonMaintenanceResult(response.payload, progress) ||
                            progress.action != (request.operation == DeviceOperation::CalibrateEpsilonMagnetic2D ? EpsilonMaintenanceAction::Magnetic2D : EpsilonMaintenanceAction::Magnetic3D) ||
                            progress.status == EpsilonMaintenanceStatus::Running)) finished = false;
                    }
                    if (finished) {
                        if (contextual && !it->ack_received) {
                            const quint16 sequence = it.key();
                            const quint64 generation = link_generation_;
                            CommandMessage context = it->command;
                            context.payload.clear();
                            const QString correlationId = context.correlation_id;
                            completed_awaiting_ack_.insert(sequence, context);
                            QTimer::singleShot(30000, this, [this, generation, sequence, correlationId] {
                                const auto entry = completed_awaiting_ack_.constFind(sequence);
                                if (generation == link_generation_ && entry != completed_awaiting_ack_.cend() &&
                                    entry->correlation_id == correlationId)
                                    completed_awaiting_ack_.remove(sequence);
                            });
                        }
                        pending_commands_.erase(it);
                    }
                    logCommandContext(logContext, QStringLiteral("ground_device_operation_response"),
                                      response.error_code == CommandErrorCode::Ok ? LogLevel::Info : LogLevel::Warning,
                                      {{QStringLiteral("request_id"), response.request_id},
                                       {QStringLiteral("result_error_code"), static_cast<quint32>(response.error_code)}});
                    break;
                }
            }
            if (dispatchGeneration != link_generation_) break;
            if (!matched) {
                const auto rejected = recently_rejected_operations_.constFind(response.request_id);
                DeviceOperationRequest request;
                if (rejected != recently_rejected_operations_.cend() &&
                    TelemetryCodec::parseDeviceOperationRequest(rejected->payload, request) &&
                    request.device_id == response.device_id && request.operation == response.operation) {
                    if ((extendedResult && !rejected->extended_result_context_requested) ||
                        contextual != rejected->typed_response_context_requested ||
                        (contextual && (response.correlation_id != rejected->correlation_id ||
                                        response.session_id != rejected->session_id))) return;
                    if (!contextual) {
                        response.correlation_id = rejected->correlation_id;
                        response.session_id = rejected->session_id;
                    }
                    matched = true;
                    const CommandMessage logContext = *rejected;
                    recently_rejected_operations_.remove(response.request_id);
                    logCommandContext(logContext, QStringLiteral("ground_device_operation_response"),
                                      response.error_code == CommandErrorCode::Ok ? LogLevel::Info : LogLevel::Warning,
                                      {{QStringLiteral("request_id"), response.request_id},
                                       {QStringLiteral("result_error_code"), static_cast<quint32>(response.error_code)}});
                }
            }
            if (dispatchGeneration != link_generation_) break;
            if (contextual && !matched) break;
            emit deviceOperationResponseReceived(response);
        }
        else
        {
            reportProtocolDiagnostic(LogLevel::Warning, QStringLiteral("protocol.parse"),
                                     QStringLiteral("device_operation_response_parse_failed"),
                                     QStringLiteral("无法解析 DeviceOperationResponse 遥测载荷。"),
                                     {{QStringLiteral("message_type"), static_cast<int>(frame.type)},
                                      {QStringLiteral("payload_bytes"), frame.payload.size()}});
        }
        break;
    }
    case MsgType::Ai8TemperatureControllerStatus:
    {
        Ai8TemperatureControllerProtocol::LiveData data;
        if (TelemetryCodec::parseAi8TemperatureControllerStatus(frame.payload, data))
        {
            emit ai8TemperatureControllerStatusUpdated(data);
        }
        else
        {
            reportProtocolDiagnostic(LogLevel::Warning, QStringLiteral("protocol.parse"),
                                     QStringLiteral("ai8_temperature_controller_status_parse_failed"),
                                     QStringLiteral("无法解析 AI-8288 遥测载荷。"),
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
                LogService::writeLogFallback(record);
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
    case MsgType::FpgaStatus:
    {
        QJsonObject status;
        if (FpgaRemote::parseStatus(frame.payload, status)) emit fpgaStatusUpdated(status);
        break;
    }
    case MsgType::FpgaSensor:
    {
        FpgaSensor::Reading reading;
        if (FpgaRemote::parseSensor(frame.payload, reading)) emit fpgaSensorUpdated(reading);
        break;
    }
    case MsgType::InternalEnvironment:
    {
        FpgaSensor::Reading reading;
        if(FpgaRemote::parseSensor(frame.payload,reading)
            &&((reading.source==0x43&&reading.kind==FpgaSensor::SensorKind::Bmp390)
                ||(reading.source==0x44&&reading.kind==FpgaSensor::SensorKind::Sht45)))emit internalEnvironmentUpdated(reading);
        break;
    }
    case MsgType::FpgaCycleFeature:
    {
        FpgaWave::CompletedStream stream;quint64 host=0;
        if(FpgaRemote::parseCycleFeature(frame.payload,host,stream))emit fpgaCycleFeatureUpdated(host,stream);
        break;
    }
    case MsgType::FpgaWaveformPreview:
    {
        FpgaWave::CompletedStream stream;
        if (FpgaRemote::parseWaveformPreview(frame.payload, stream)) emit fpgaWaveformUpdated(stream);
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
    const QPointer<GroundTelemetryService> self(this);
    codec_.reset();
    close();
    if (!self || link_)
        return false;
    rtcm_sequence_ = {QRandomGenerator::global()->generate64() | 1ULL, 0};
    rtcm_observability_supported_ = false;
    link_ = std::move(link);
    attachLinkSignals();
    ++link_generation_;
    const QPointer<TelemetryLink> installedLink(link_.get());
    clearPendingForDisconnect();
    if (!self || !installedLink || link_.get() != installedLink.data())
        return false;
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
    TelemetryLink *activeLink = link_.get();
    connect(activeLink, &TelemetryLink::bytesReceived, this, [this, activeLink](const QByteArray& bytes) {
        if (link_.get() != activeLink)
        {
            return;
        }
        onBytesReceived(bytes);
    });
    connect(activeLink, &TelemetryLink::openChanged, this, [this, activeLink](bool open) {
        if (link_.get() != activeLink)
        {
            return;
        }
        const QPointer<GroundTelemetryService> self(this);
        ++open_changed_depth_[activeLink];
        const auto depthGuard = qScopeGuard([self, activeLink] {
            if (!self) return;
            auto it = self->open_changed_depth_.find(activeLink);
            if (it != self->open_changed_depth_.end() && --it.value() == 0)
                self->open_changed_depth_.erase(it);
        });
        if (!open)
        {
            retry_timer_.stop();
            const quint64 generation = ++link_generation_;
            clearPendingForDisconnect();
            if (!self || generation != link_generation_ || link_.get() != activeLink)
                return;
        }
        else retry_timer_.start();
        emit linkOpenChanged(open);
    });
    connect(activeLink, &TelemetryLink::errorOccurred, this, [this, activeLink](const QString& systemError) {
        if (link_.get() != activeLink)
        {
            return;
        }
        publishTelemetryLog(LogLevel::Warning,
                            QStringLiteral("telemetry.link"),
                            QStringLiteral("ground_telemetry_link_error"),
                            QStringLiteral("地面端遥测链路异常。"),
                            {{QStringLiteral("reason_code"), QStringLiteral("GROUND_TELEMETRY_LINK_ERROR")},
                             {QStringLiteral("system_error"), systemError}});
    });
    connect(activeLink, &TelemetryLink::logRecordGenerated, this, [this, activeLink](LogRecord record) {
        if (link_.get() != activeLink)
        {
            return;
        }
        if (record.source.isEmpty())
        {
            record.source = QStringLiteral("TelemetryLink");
        }
        if (record.category.isEmpty())
        {
            record.category = QStringLiteral("telemetry.link");
        }
        if (!record.fields.contains(QStringLiteral("ui_visibility")))
        {
            record.fields.insert(QStringLiteral("ui_visibility"),
                                 record.level >= LogLevel::Warning ? QStringLiteral("attention")
                                                                   : QStringLiteral("details"));
        }
        if (!LogService::withCurrentInstance([&](LogService& logService) {
                logService.publish(record);
            }))
        {
            LogService::writeLogFallback(record);
        }
    });
}

void GroundTelemetryService::clearPendingForDisconnect()
{
    QHash<quint16, PendingCommand> pending;
    QHash<quint32, CommandMessage> awaitingResults;
    QHash<quint16, AwaitedConfigResult> awaitingConfigResults;
    pending.swap(pending_commands_);
    awaitingResults.swap(recently_rejected_operations_);
    awaitingConfigResults.swap(config_results_awaited_);
    completed_awaiting_ack_.clear();
    config_completed_awaiting_ack_.clear();
    for (const auto& item : pending)
        logCommandContext(item.command, QStringLiteral("ground_command_disconnected"), LogLevel::Warning);
    for (const auto& command : awaitingResults)
        logCommandContext(command, QStringLiteral("ground_command_disconnected"), LogLevel::Warning,
                          {{QStringLiteral("stage"), QStringLiteral("awaiting_device_result")}});
    for (const auto& item : awaitingConfigResults)
        logCommandContext(item.command, QStringLiteral("ground_command_disconnected"), LogLevel::Warning,
                          {{QStringLiteral("stage"), QStringLiteral("awaiting_config_result")}});
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
                                                 QVariantMap fields, const QString& correlationId,
                                                 const QString& sessionId, const char *file, const char *function, int line)
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
    LogRecord record;
    record.level = level;
    record.source = QStringLiteral("Ground");
    record.category = category;
    record.message = message;
    record.correlation_id = correlationId;
    record.session_id = sessionId;
    record.fields = fields;
    LogService::addCallLocation(record, file, function, line);
    if (!LogService::withCurrentInstance([&](LogService& logService) {
            logService.publish(record);
        }))
    {
        LogService::writeLogFallback(record);
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
