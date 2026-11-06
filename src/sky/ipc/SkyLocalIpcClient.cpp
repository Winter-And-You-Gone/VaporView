#include "SkyLocalIpcClient.h"

#include "LogService.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>

namespace VaporView
{
namespace
{
bool isConnectedAndFresh(const DeviceStatusItem& status, quint64 nowUs, quint64 timeoutUs)
{
    return status.state == DeviceState::Connected &&
           status.last_data_time_us > 0 &&
           nowUs >= status.last_data_time_us &&
           nowUs - status.last_data_time_us <= timeoutUs;
}

QString gnssFixText(quint8 code)
{
    switch (code)
    {
    case 0:
        return QStringLiteral("none");
    case 1:
        return QStringLiteral("single");
    case 2:
        return QStringLiteral("dgps");
    case 4:
        return QStringLiteral("fixed");
    case 5:
        return QStringLiteral("float");
    case 6:
        return QStringLiteral("dead-reckoning");
    default:
        return QStringLiteral("fix-%1").arg(code);
    }
}

}  // namespace

SkyLocalIpcClient::SkyLocalIpcClient(QObject *parent)
    : QObject(parent)
{
    connect(&socket_, &QTcpSocket::connected, this, &SkyLocalIpcClient::onConnected);
    connect(&socket_, &QTcpSocket::disconnected, this, &SkyLocalIpcClient::onDisconnected);
    connect(&socket_, &QTcpSocket::readyRead, this, &SkyLocalIpcClient::onReadyRead);
    connect(&socket_, &QTcpSocket::errorOccurred, this, &SkyLocalIpcClient::onErrorOccurred);
    reconnect_timer_.setInterval(1000);
    reconnect_timer_.setTimerType(Qt::CoarseTimer);
    connect(&reconnect_timer_, &QTimer::timeout, this, &SkyLocalIpcClient::attemptReconnect);
    updateDashboardRates();
}

void SkyLocalIpcClient::publishClientLog(LogLevel level,
                                         const QString& category,
                                         const QString& event,
                                         const QString& message,
                                         QVariantMap fields, const QString& correlationId,
                                         const char *file, const char *function, int line)
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
        fields.insert(QStringLiteral("error_code"), QStringLiteral("SKY_TUI_IPC_ERROR"));
    }

    LogRecord record;
    record.level = level;
    record.source = QStringLiteral("SkyTui");
    record.category = category;
    record.message = message;
    record.correlation_id = correlationId;
    record.fields = fields;
    LogService::addCallLocation(record, file, function, line);
    emit logRecordGenerated(record);
    if (!LogService::withCurrentInstance([](LogService&) {}))
    {
        LogService::writeLogFallback(record);
    }
}

void SkyLocalIpcClient::connectToCore(const QString& host, quint16 port)
{
    core_host_ = host;
    core_port_ = port;
    user_disconnect_requested_ = false;
    reconnect_timer_.stop();
    if (socket_.state() != QAbstractSocket::UnconnectedState)
    {
        socket_.abort();
    }
    decoder_.reset();
    next_frame_seq_ = 1;
    socket_.connectToHost(host, port);
}

void SkyLocalIpcClient::disconnectFromCore()
{
    user_disconnect_requested_ = true;
    reconnect_timer_.stop();
    socket_.disconnectFromHost();
}

bool SkyLocalIpcClient::isConnected() const
{
    return socket_.state() == QAbstractSocket::ConnectedState;
}

void SkyLocalIpcClient::setAutoReconnectEnabled(bool enabled, int intervalMs)
{
    auto_reconnect_enabled_ = enabled;
    reconnect_timer_.setInterval(std::max(100, intervalMs));
    if (!auto_reconnect_enabled_)
    {
        reconnect_timer_.stop();
    }
    else if (!isConnected() && socket_.state() == QAbstractSocket::UnconnectedState)
    {
        scheduleReconnect();
    }
}

bool SkyLocalIpcClient::autoReconnectEnabled() const
{
    return auto_reconnect_enabled_;
}

TelemetryStatus SkyLocalIpcClient::currentStatus() const
{
    return status_;
}

SkyConfig SkyLocalIpcClient::currentConfig() const
{
    return config_;
}

SkyDashboardSnapshot SkyLocalIpcClient::dashboardSnapshot() const
{
    return dashboard_;
}

bool SkyLocalIpcClient::waveformStreamingEnabled() const
{
    return waveform_streaming_enabled_;
}

quint16 SkyLocalIpcClient::requestStatus()
{
    return sendCommand(CommandId::RequestStatus);
}

quint16 SkyLocalIpcClient::startRecording()
{
    return sendCommand(CommandId::StartRecording);
}

quint16 SkyLocalIpcClient::pauseRecording()
{
    return sendCommand(CommandId::PauseRecording);
}

quint16 SkyLocalIpcClient::stopRecording()
{
    return sendCommand(CommandId::StopRecording);
}

quint16 SkyLocalIpcClient::connectDevice(SkyDeviceId id)
{
    return id == SkyDeviceId::All ? connectAllDevices()
                                  : sendCommand(CommandId::ConnectDevice, TelemetryCodec::serializeDeviceCommand(id));
}

quint16 SkyLocalIpcClient::disconnectDevice(SkyDeviceId id)
{
    return id == SkyDeviceId::All ? disconnectAllDevices()
                                  : sendCommand(CommandId::DisconnectDevice, TelemetryCodec::serializeDeviceCommand(id));
}

quint16 SkyLocalIpcClient::reconnectDevice(SkyDeviceId id)
{
    return id == SkyDeviceId::All ? reconnectAllDevices()
                                  : sendCommand(CommandId::ReconnectDevice, TelemetryCodec::serializeDeviceCommand(id));
}

quint16 SkyLocalIpcClient::connectAllDevices()
{
    return sendCommand(CommandId::ConnectAllDevices);
}

quint16 SkyLocalIpcClient::disconnectAllDevices()
{
    return sendCommand(CommandId::DisconnectAllDevices);
}

quint16 SkyLocalIpcClient::reconnectAllDevices()
{
    return sendCommand(CommandId::ReconnectAllDevices);
}

quint16 SkyLocalIpcClient::enableWaveformStreaming()
{
    return sendCommand(CommandId::EnableWaveformStreaming);
}

quint16 SkyLocalIpcClient::disableWaveformStreaming()
{
    return sendCommand(CommandId::DisableWaveformStreaming);
}

quint16 SkyLocalIpcClient::requestOneWaveform()
{
    return sendCommand(CommandId::RequestOneWaveform);
}

quint16 SkyLocalIpcClient::getConfig()
{
    return sendCommand(CommandId::GetSkyConfig);
}

quint16 SkyLocalIpcClient::setConfig(const SkyConfig& config)
{
    return sendCommand(CommandId::SetSkyConfig, QJsonDocument(config.toJson()).toJson(QJsonDocument::Compact));
}

quint16 SkyLocalIpcClient::saveConfig()
{
    return sendCommand(CommandId::SaveSkyConfig);
}

quint16 SkyLocalIpcClient::requestCoreShutdown()
{
    return sendCommand(CommandId::ShutdownCore);
}

void SkyLocalIpcClient::onConnected()
{
    ++connection_generation_;
    const quint64 generation = connection_generation_;
    reconnect_timer_.stop();
    connected_time_us_ = currentTimestampUs();
    emit connectedChanged(true);
    if (generation != connection_generation_ || !isConnected()) return;
    publishClientLog(LogLevel::Info,
                     QStringLiteral("ipc.connection"),
                     QStringLiteral("sky_ipc_connected"),
                     QStringLiteral("SkyCore IPC 已连接。"),
                     {{QStringLiteral("host"), core_host_},
                      {QStringLiteral("port"), core_port_}});
    if (generation != connection_generation_ || !isConnected()) return;
    QTimer::singleShot(0, this, [this, generation]() {
        if (generation != connection_generation_ || !isConnected()) return;
        requestStatus();
        getConfig();
    });
}

void SkyLocalIpcClient::onDisconnected()
{
    ++connection_generation_;
    const quint64 generation = connection_generation_;
    QHash<quint16, ConfigResultContext> awaitingConfig;
    awaitingConfig.swap(config_result_contexts_);
    QHash<quint16, CommandMessage> pending;
    pending.swap(pending_commands_);
    for (const auto& command : pending)
        publishCommandLog(command, QStringLiteral("sky_ipc_command_disconnected"),
                          QStringLiteral("SkyCore IPC 断开，命令未完成。"), LogLevel::Warning);
    for (const auto& context : awaitingConfig) {
        if (!context.awaitingAck)
            publishCommandLog(context.command, QStringLiteral("sky_ipc_config_result_disconnected"),
                              QStringLiteral("SkyCore IPC 已断开，配置关联结果尚未收到。"),
                              LogLevel::Warning, context.command.session_id);
    }
    if (generation != connection_generation_ || isConnected()) return;
    emit connectedChanged(false);
    if (generation != connection_generation_ || isConnected()) return;
    publishClientLog(LogLevel::Info,
                     QStringLiteral("ipc.connection"),
                     QStringLiteral("sky_ipc_disconnected"),
                     QStringLiteral("SkyCore IPC 连接已断开。"));
    if (generation != connection_generation_ || isConnected()) return;
    scheduleReconnect();
}

void SkyLocalIpcClient::onReadyRead()
{
    const quint64 generation = connection_generation_;
    const QVector<TelemetryFrame> frames = decoder_.feedBytes(socket_.readAll());
    for (const TelemetryFrame& frame : frames)
    {
        if (generation != connection_generation_) break;
        dispatchFrame(frame);
    }
}

void SkyLocalIpcClient::rememberConfigResultContext(const CommandMessage& command, bool awaitingAck, bool sessionKnown)
{
    ConfigResultContext evicted;
    bool reportEviction = false;
    if (!config_result_contexts_.contains(command.command_seq) && config_result_contexts_.size() >= 256) {
        const auto oldest = config_result_contexts_.begin();
        evicted = oldest.value();
        reportEviction = !evicted.awaitingAck;
        config_result_contexts_.erase(oldest);
    }
    CommandMessage stored = command;
    stored.payload.clear();
    config_result_contexts_.insert(command.command_seq, {stored, awaitingAck, sessionKnown});
    const quint64 generation = connection_generation_;
    const QString correlationId = command.correlation_id;
    QTimer::singleShot(30000, this, [this, generation, sequence = command.command_seq, correlationId] {
        const auto it = config_result_contexts_.constFind(sequence);
        if (generation == connection_generation_ && it != config_result_contexts_.cend() &&
            it->command.correlation_id == correlationId) {
            const ConfigResultContext expired = *it;
            config_result_contexts_.remove(sequence);
            if (!expired.awaitingAck)
                publishCommandLog(expired.command, QStringLiteral("sky_ipc_config_result_context_expired"),
                                  QStringLiteral("等待 SkyCore 配置关联结果的上下文已过期。"),
                                  LogLevel::Warning, expired.command.session_id);
        }
    });
    if (reportEviction)
        publishCommandLog(evicted.command, QStringLiteral("sky_ipc_config_result_context_evicted"),
                          QStringLiteral("等待 SkyCore 配置关联结果的上下文因容量限制被移除。"),
                          LogLevel::Warning, evicted.command.session_id);
}

void SkyLocalIpcClient::onErrorOccurred(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    if (auto_reconnect_enabled_ && !user_disconnect_requested_)
    {
        if (!reconnect_timer_.isActive())
        {
            publishClientLog(LogLevel::Warning,
                             QStringLiteral("ipc.connection"),
                             QStringLiteral("sky_ipc_unavailable_retrying"),
                             QStringLiteral("SkyCore IPC 暂不可用，将自动重连。"),
                             {{QStringLiteral("reason_code"), QStringLiteral("SKY_IPC_UNAVAILABLE")},
                              {QStringLiteral("socket_error"), static_cast<int>(error)},
                              {QStringLiteral("system_error"), socket_.errorString()}});
        }
        scheduleReconnect();
        return;
    }
    publishClientLog(LogLevel::Error,
                     QStringLiteral("ipc.connection"),
                     QStringLiteral("sky_ipc_socket_error"),
                     QStringLiteral("SkyCore IPC 发生错误。"),
                     {{QStringLiteral("error_code"), QStringLiteral("SKY_IPC_SOCKET_ERROR")},
                      {QStringLiteral("socket_error"), static_cast<int>(error)},
                      {QStringLiteral("system_error"), socket_.errorString()}});
}

quint16 SkyLocalIpcClient::sendCommand(CommandId commandId, const QByteArray& payload)
{
    const bool isBusinessCommand = commandId != CommandId::RequestStatus && commandId != CommandId::GetSkyConfig &&
                                   commandId != CommandId::QueryDeviceStatus && commandId != CommandId::RequestOneWaveform;
    const QString correlationId = isBusinessCommand ? QUuid::createUuid().toString(QUuid::WithoutBraces) : QString();
    if (!isConnected())
    {
        publishClientLog(LogLevel::Warning,
                         QStringLiteral("ipc.command"),
                         QStringLiteral("sky_ipc_command_send_skipped"),
                         QStringLiteral("未连接 SkyCore，命令未发送。"),
                         {{QStringLiteral("reason_code"), QStringLiteral("SKY_IPC_NOT_CONNECTED")},
                          {QStringLiteral("command_id"), commandIdName(commandId)},
                          {QStringLiteral("command_value"), static_cast<quint16>(commandId)}}, correlationId);
        return 0;
    }

    const quint64 generation = connection_generation_;
    CommandMessage command;
    command.command_id = commandId;
    for (quint32 attempt = 0; attempt < 65536; ++attempt) {
        const quint16 candidate = next_command_seq_++;
        if (candidate && !pending_commands_.contains(candidate) && !config_result_contexts_.contains(candidate)) {
            command.command_seq = candidate;
            break;
        }
    }
    if (!command.command_seq) {
        publishClientLog(LogLevel::Warning, QStringLiteral("ipc.command"),
                         QStringLiteral("sky_ipc_command_send_skipped"),
                         QStringLiteral("命令序号已用尽，命令未发送。"),
                         {{QStringLiteral("reason_code"), QStringLiteral("SKY_IPC_SEQUENCE_EXHAUSTED")},
                          {QStringLiteral("command_id"), commandIdName(commandId)},
                          {QStringLiteral("command_value"), static_cast<quint16>(commandId)}}, correlationId);
        return 0;
    }
    command.payload = payload;
    command.config_result_context_requested = commandId == CommandId::SetSkyConfig;
    command.config_ack_session_omission_supported = command.config_result_context_requested;
    command.extended_ack_context_requested = isBusinessCommand;
    command.extended_result_context_requested = command.config_result_context_requested;
    command.correlation_id = correlationId;
    const QByteArray commandPayload = TelemetryCodec::serializeCommand(command);
    if (isBusinessCommand)
    {
        CommandMessage encodedCommand;
        const bool parsed = TelemetryCodec::parseCommand(commandPayload, encodedCommand);
        command.extended_ack_context_requested = parsed && encodedCommand.extended_ack_context_requested;
        command.extended_result_context_requested = parsed && encodedCommand.extended_result_context_requested;
        command.config_ack_session_omission_supported = parsed && encodedCommand.config_ack_session_omission_supported;
        pending_commands_.insert(command.command_seq, command);
        publishCommandLog(command, QStringLiteral("sky_ipc_command_started"),
                          QStringLiteral("已发起 SkyCore IPC 命令。"));
        const auto pending = pending_commands_.constFind(command.command_seq);
        if (generation != connection_generation_ || !isConnected() || pending == pending_commands_.cend() ||
            pending->correlation_id != command.correlation_id) return 0;
        QTimer::singleShot(30000, this, [this, command]() {
            const auto it = pending_commands_.find(command.command_seq);
            if (it == pending_commands_.end() || it->correlation_id != command.correlation_id) return;
            pending_commands_.erase(it);
            publishCommandLog(command, QStringLiteral("sky_ipc_command_timeout"),
                              QStringLiteral("等待 SkyCore IPC 命令响应超时。"), LogLevel::Warning);
        });
    }
    const QByteArray encoded = encoder_.encodeFrame(MsgType::Command,
                                                    commandPayload,
                                                    next_frame_seq_++,
                                                    currentTimestampUs());
    socket_.write(encoded);
    return command.command_seq;
}

void SkyLocalIpcClient::publishCommandLog(const CommandMessage& command, const QString& event,
                                          const QString& message, LogLevel level, const QString& sessionId,
                                          const char *file, const char *function, int line)
{
    LogRecord record;
    record.level = level;
    record.source = QStringLiteral("SkyTui");
    record.category = QStringLiteral("ipc.command");
    record.message = message;
    record.correlation_id = command.correlation_id;
    record.session_id = sessionId;
    record.fields = {{QStringLiteral("event"), event},
                     {QStringLiteral("command_seq"), command.command_seq},
                     {QStringLiteral("command_id"), static_cast<quint16>(command.command_id)}};
    LogService::addCallLocation(record, file, function, line);
    emit logRecordGenerated(record);
    if (!LogService::withCurrentInstance([](LogService&) {}))
        LogService::writeLogFallback(record);
}

void SkyLocalIpcClient::attemptReconnect()
{
    if (!auto_reconnect_enabled_ ||
        user_disconnect_requested_ ||
        core_host_.isEmpty() ||
        core_port_ == 0 ||
        socket_.state() != QAbstractSocket::UnconnectedState)
    {
        return;
    }

    decoder_.reset();
    next_frame_seq_ = 1;
    socket_.connectToHost(core_host_, core_port_);
}

void SkyLocalIpcClient::scheduleReconnect()
{
    if (!auto_reconnect_enabled_ ||
        user_disconnect_requested_ ||
        core_host_.isEmpty() ||
        core_port_ == 0 ||
        isConnected() ||
        reconnect_timer_.isActive())
    {
        return;
    }

    publishClientLog(LogLevel::Info,
                     QStringLiteral("ipc.connection"),
                     QStringLiteral("sky_ipc_reconnect_scheduled"),
                     QStringLiteral("SkyCore IPC 自动重连已计划。"),
                     {{QStringLiteral("retry_delay_ms"), reconnect_timer_.interval()}});
    reconnect_timer_.start();
}

void SkyLocalIpcClient::dispatchFrame(const TelemetryFrame& frame)
{
    switch (frame.type)
    {
    case MsgType::TelemetryBasic:
    {
        TelemetryBasic basic;
        if (TelemetryCodec::parseBasicTelemetry(frame.payload, basic))
        {
            updateFromBasic(basic);
            emit basicReceived(basic);
            emit dashboardUpdated();
        }
        else
        {
            publishClientLog(LogLevel::Warning,
                             QStringLiteral("ipc.protocol"),
                             QStringLiteral("sky_ipc_telemetry_basic_parse_failed"),
                             QStringLiteral("无法解析 SkyCore TelemetryBasic 载荷。"),
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
            updateFromWaveform(waveform);
            emit waveformReceived(waveform);
            emit dashboardUpdated();
        }
        else
        {
            publishClientLog(LogLevel::Warning,
                             QStringLiteral("ipc.protocol"),
                             QStringLiteral("sky_ipc_waveform_downsampled_parse_failed"),
                             QStringLiteral("无法解析 SkyCore WaveformDownsampled 载荷。"),
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
            updateFromFeature(feature);
            emit featureReceived(feature);
            emit dashboardUpdated();
        }
        else
        {
            publishClientLog(LogLevel::Warning,
                             QStringLiteral("ipc.protocol"),
                             QStringLiteral("sky_ipc_waveform_feature_parse_failed"),
                             QStringLiteral("无法解析 SkyCore WaveformFeature 载荷。"),
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
            updateFromStatus(status);
            emit statusReceived(status);
            emit dashboardUpdated();
        }
        else
        {
            publishClientLog(LogLevel::Warning,
                             QStringLiteral("ipc.protocol"),
                             QStringLiteral("sky_ipc_telemetry_status_parse_failed"),
                             QStringLiteral("无法解析 SkyCore TelemetryStatus 载荷。"),
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
            dashboard_.temperature_controller = data;
            emit dashboardUpdated();
        }
        else
        {
            publishClientLog(LogLevel::Warning,
                             QStringLiteral("ipc.protocol"),
                             QStringLiteral("sky_ipc_temperature_controller_status_parse_failed"),
                             QStringLiteral("无法解析 SkyCore RD105 温控状态载荷。"),
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
            dashboard_.ai8_temperature_controller = data;
            emit dashboardUpdated();
        }
        else
        {
            publishClientLog(LogLevel::Warning,
                             QStringLiteral("ipc.protocol"),
                             QStringLiteral("sky_ipc_ai8_temperature_controller_status_parse_failed"),
                             QStringLiteral("无法解析 SkyCore AI-8288 温控状态载荷。"),
                             {{QStringLiteral("message_type"), static_cast<int>(frame.type)},
                              {QStringLiteral("payload_bytes"), frame.payload.size()}});
        }
        break;
    }
    case MsgType::CommandAck:
    {
        const quint64 generation = connection_generation_;
        CommandAck ack;
        if (TelemetryCodec::parseCommandAck(frame.payload, ack))
        {
            const auto pending = pending_commands_.find(ack.command_seq);
            const auto completed = config_result_contexts_.find(ack.command_seq);
            if (pending == pending_commands_.end() && completed != config_result_contexts_.end() && completed->awaitingAck) {
                if (ack.command_id != CommandId::SetSkyConfig || ack.correlation_id.isEmpty() ||
                    (ack.extended_ack_context && !completed->command.extended_ack_context_requested) ||
                    (ack.session_id_omitted && !completed->command.config_ack_session_omission_supported) ||
                    ack.correlation_id != completed->command.correlation_id ||
                    (!ack.session_id_omitted && ack.session_id != completed->command.session_id))
                    break;
                const auto command = completed->command;
                if (ack.session_id_omitted) ack.session_id = command.session_id;
                config_result_contexts_.erase(completed);
                publishCommandLog(command, QStringLiteral("sky_ipc_command_ack"),
                                  QStringLiteral("已收到 SkyCore IPC 命令响应。"),
                                  ack.error_code == CommandErrorCode::Ok ? LogLevel::Info : LogLevel::Warning, ack.session_id);
                if (generation == connection_generation_) emit ackReceived(ack);
                break;
            }
            if ((ack.extended_ack_context && (pending == pending_commands_.end() ||
                                              !pending->extended_ack_context_requested || ack.correlation_id.isEmpty())) ||
                (ack.session_id_omitted && (pending == pending_commands_.end() || ack.command_id != CommandId::SetSkyConfig ||
                                            !pending->config_ack_session_omission_supported)) ||
                (pending != pending_commands_.end() && pending->command_id != ack.command_id) ||
                (!ack.correlation_id.isEmpty() &&
                 (pending == pending_commands_.end() || pending->correlation_id != ack.correlation_id)))
                break;
            if (pending != pending_commands_.end() && pending->command_id == ack.command_id) {
                const auto command = pending.value();
                pending_commands_.erase(pending);
                if (command.command_id == CommandId::SetSkyConfig && ack.config_result_context_expected.value_or(true)) {
                    auto context = command;
                    if (!ack.session_id_omitted) context.session_id = ack.session_id;
                    rememberConfigResultContext(context, false, !ack.correlation_id.isEmpty() && !ack.session_id_omitted);
                    if (generation != connection_generation_) break;
                }
                publishCommandLog(command, QStringLiteral("sky_ipc_command_ack"),
                                  QStringLiteral("已收到 SkyCore IPC 命令响应。"),
                                  ack.error_code == CommandErrorCode::Ok ? LogLevel::Info : LogLevel::Warning,
                                  ack.correlation_id.isEmpty() || ack.session_id_omitted ? QString() : ack.session_id);
            }
            if (generation != connection_generation_) break;
            if (ack.error_code == CommandErrorCode::Ok)
            {
                if (ack.command_id == CommandId::EnableWaveformStreaming)
                {
                    waveform_streaming_enabled_ = true;
                }
                else if (ack.command_id == CommandId::DisableWaveformStreaming)
                {
                    waveform_streaming_enabled_ = false;
                }
            }
            emit ackReceived(ack);
        }
        else
        {
            publishClientLog(LogLevel::Warning,
                             QStringLiteral("ipc.protocol"),
                             QStringLiteral("sky_ipc_command_ack_parse_failed"),
                             QStringLiteral("无法解析 SkyCore CommandAck 载荷。"),
                             {{QStringLiteral("message_type"), static_cast<int>(frame.type)},
                              {QStringLiteral("payload_bytes"), frame.payload.size()}});
        }
        break;
    }
    case MsgType::SkyConfig:
    {
        const QJsonDocument document = QJsonDocument::fromJson(frame.payload);
        SkyConfig config;
        QString error;
        if (document.isObject() && SkyConfig::fromJson(document.object(), config, &error))
        {
            config_ = config;
            updateDashboardRates();
            emit configReceived(config_);
            emit dashboardUpdated();
        }
        else
        {
            publishClientLog(LogLevel::Warning,
                             QStringLiteral("ipc.protocol"),
                             QStringLiteral("sky_ipc_config_parse_failed"),
                             QStringLiteral("无法解析 SkyCore 返回的配置。"),
                             {{QStringLiteral("reason_code"), QStringLiteral("SKY_IPC_CONFIG_PARSE_FAILED")},
                              {QStringLiteral("system_error"), error},
                              {QStringLiteral("payload_bytes"), frame.payload.size()}});
        }
        break;
    }
    case MsgType::SkyConfigApplyResult:
        publishClientLog(LogLevel::Info,
                         QStringLiteral("ipc.config"),
                         QStringLiteral("sky_ipc_config_apply_result_received"),
                         QStringLiteral("已收到 SkyCore 配置应用结果。"),
                         {{QStringLiteral("config_apply_result"), QString::fromUtf8(frame.payload)}});
        break;
    case MsgType::SkyConfigApplyResultContext:
    {
        const quint64 generation = connection_generation_;
        QJsonObject result;
        QString correlationId, sessionId;
        quint16 commandSeq = 0;
        bool extendedResult = false;
        if (!TelemetryCodec::parseSkyConfigApplyResultContext(frame.payload, result, correlationId, sessionId, commandSeq, &extendedResult))
            break;
        auto pending = pending_commands_.find(commandSeq);
        auto completed = config_result_contexts_.find(commandSeq);
        if (pending != pending_commands_.end()) {
            if (pending->command_id != CommandId::SetSkyConfig || pending->correlation_id != correlationId ||
                (extendedResult && !pending->extended_result_context_requested)) break;
            auto context = pending.value();
            context.session_id = sessionId;
            pending_commands_.erase(pending);
            rememberConfigResultContext(context, true, true);
            if (generation != connection_generation_) break;
        } else {
            if (completed == config_result_contexts_.end() || completed->awaitingAck ||
                completed->command.command_id != CommandId::SetSkyConfig ||
                (extendedResult && !completed->command.extended_result_context_requested) ||
                completed->command.correlation_id != correlationId ||
                (completed->sessionKnown && completed->command.session_id != sessionId)) break;
            config_result_contexts_.erase(completed);
        }
        LogRecord record;
        record.level = LogLevel::Info;
        record.source = QStringLiteral("SkyTui");
        record.category = QStringLiteral("ipc.config");
        record.message = QStringLiteral("已收到带关联信息的 SkyCore 配置应用结果。");
        record.correlation_id = correlationId;
        record.session_id = sessionId;
        record.fields = {{QStringLiteral("event"), QStringLiteral("sky_ipc_config_apply_result_context_received")},
                         {QStringLiteral("command_seq"), commandSeq},
                         {QStringLiteral("config_apply_result"), QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact))}};
        LogService::addCallLocation(record, VAPORVIEW_LOG_FILE, VAPORVIEW_LOG_FUNCTION, VAPORVIEW_LOG_LINE);
        emit logRecordGenerated(record);
        if (!LogService::withCurrentInstance([](LogService&) {}))
            LogService::writeLogFallback(record);
        break;
    }
    case MsgType::LogEvent:
    {
        LogRecord record;
        if (TelemetryCodec::parseLogRecord(frame.payload, record))
        {
            emit logRecordReceived(record);
        }
        else
        {
            publishClientLog(LogLevel::Warning,
                             QStringLiteral("ipc.protocol"),
                             QStringLiteral("sky_ipc_log_event_parse_failed"),
                             QStringLiteral("无法解析 SkyCore 日志帧。"),
                             {{QStringLiteral("message_type"), static_cast<int>(frame.type)},
                              {QStringLiteral("payload_bytes"), frame.payload.size()}});
        }
        break;
    }
    case MsgType::Heartbeat:
    case MsgType::Command:
        break;
    case MsgType::Error:
        publishClientLog(LogLevel::Error,
                         QStringLiteral("ipc.protocol"),
                         QStringLiteral("sky_ipc_error_frame_received"),
                         QStringLiteral("已收到 SkyCore telemetry Error 帧。"),
                         {{QStringLiteral("error_code"), QStringLiteral("SKY_IPC_ERROR_FRAME")},
                          {QStringLiteral("payload_hex"), QString::fromLatin1(frame.payload.toHex())},
                          {QStringLiteral("payload_bytes"), frame.payload.size()}});
        break;
    }
}

void SkyLocalIpcClient::updateFromBasic(const TelemetryBasic& basic)
{
    dashboard_.host_time_us = basic.host_time_us;

    if ((basic.validity_flags & BasicHasEpsilonTime) != 0)
    {
        dashboard_.epsilon.device_timestamp_us = basic.epsilon_time_us;
    }
    if ((basic.validity_flags & BasicHasPosition) != 0)
    {
        dashboard_.epsilon.valid = true;
        dashboard_.epsilon.latitude_deg = basic.latitude_deg;
        dashboard_.epsilon.longitude_deg = basic.longitude_deg;
        dashboard_.epsilon.height_m = basic.height_m;
    }
    if ((basic.validity_flags & BasicHasEcef) != 0)
    {
        dashboard_.epsilon.ecef_x_m = basic.ecef_x_m;
        dashboard_.epsilon.ecef_y_m = basic.ecef_y_m;
        dashboard_.epsilon.ecef_z_m = basic.ecef_z_m;
    }
    if ((basic.validity_flags & BasicHasGnssQuality) != 0)
    {
        dashboard_.epsilon.gnss_fix_code = basic.gnss_fix_code;
        dashboard_.epsilon.gnss_satellites = basic.gnss_satellites;
        dashboard_.epsilon.hdop = basic.hdop;
        dashboard_.epsilon.vdop = basic.vdop;
        dashboard_.epsilon.hacc_m = basic.hacc_m;
        dashboard_.epsilon.vacc_m = basic.vacc_m;
        dashboard_.epsilon.heading_valid = basic.heading_valid;
        dashboard_.epsilon.gnss_fix_text = gnssFixText(basic.gnss_fix_code).toStdString();
    }
    if ((basic.validity_flags & BasicHasNedVelocity) != 0)
    {
        dashboard_.epsilon.vel_n_mps = basic.vel_n_mps;
        dashboard_.epsilon.vel_e_mps = basic.vel_e_mps;
        dashboard_.epsilon.vel_d_mps = basic.vel_d_mps;
    }
    if ((basic.validity_flags & BasicHasImu) != 0)
    {
        dashboard_.epsilon.imu_acc_x_mps2 = basic.imu_acc_x_mps2;
        dashboard_.epsilon.imu_acc_y_mps2 = basic.imu_acc_y_mps2;
        dashboard_.epsilon.imu_acc_z_mps2 = basic.imu_acc_z_mps2;
        dashboard_.epsilon.imu_gyr_x_radps = basic.imu_gyr_x_radps;
        dashboard_.epsilon.imu_gyr_y_radps = basic.imu_gyr_y_radps;
        dashboard_.epsilon.imu_gyr_z_radps = basic.imu_gyr_z_radps;
    }
    if ((basic.validity_flags & BasicHasAttitude) != 0)
    {
        dashboard_.epsilon.roll_deg = basic.roll_deg;
        dashboard_.epsilon.pitch_deg = basic.pitch_deg;
        dashboard_.epsilon.yaw_deg = basic.yaw_deg;
    }
    if ((basic.validity_flags & BasicHasEpsilonDiagnostics) != 0)
    {
        dashboard_.epsilon.device_info_valid = basic.device_info_valid;
        dashboard_.epsilon.hardware_name = basic.hardware_name.toStdString();
        dashboard_.epsilon.firmware_name = basic.firmware_name.toStdString();
        dashboard_.epsilon.hardware_version = basic.hardware_version;
        dashboard_.epsilon.firmware_version = basic.firmware_version;
        dashboard_.epsilon.serial_number = basic.serial_number;
        dashboard_.epsilon.raw_frame_count = basic.raw_frame_count;
        dashboard_.epsilon.dropped_frame_count = basic.dropped_frame_count;
        dashboard_.epsilon.imu_packet_rate_hz = basic.imu_packet_rate_hz;
        dashboard_.epsilon.ahrs_packet_rate_hz = basic.ahrs_packet_rate_hz;
        dashboard_.epsilon.insgps_packet_rate_hz = basic.insgps_packet_rate_hz;
        dashboard_.epsilon.sys_state_packet_rate_hz = basic.sys_state_packet_rate_hz;
        dashboard_.epsilon.status_packet_rate_hz = basic.status_packet_rate_hz;
        dashboard_.epsilon.raw_gnss_packet_rate_hz = basic.raw_gnss_packet_rate_hz;
        dashboard_.epsilon.diff_age_s = basic.diff_age_s;
        dashboard_.epsilon.satellite_packet_rate_hz = basic.satellite_packet_rate_hz;
        dashboard_.epsilon.geodetic_packet_rate_hz = basic.geodetic_packet_rate_hz;
        dashboard_.epsilon.ecef_packet_rate_hz = basic.ecef_packet_rate_hz;
        dashboard_.epsilon.euler_orien_packet_rate_hz = basic.euler_orien_packet_rate_hz;
        dashboard_.epsilon.quat_orien_packet_rate_hz = basic.quat_orien_packet_rate_hz;
        dashboard_.epsilon.system_status_bits = basic.status_bits;
        dashboard_.epsilon.filter_status_bits = basic.filter_status_bits;
        dashboard_.epsilon.update_status_bits = basic.update_status_bits;
        dashboard_.epsilon_acquisition_rate_hz = basic.imu_packet_rate_hz > 0.0f
                                                     ? basic.imu_packet_rate_hz
                                                     : kDefaultEpsilonCallbackRateHz;
    }
    if ((basic.validity_flags & BasicHasLidar) != 0)
    {
        dashboard_.lidar.valid = true;
        dashboard_.lidar.distance_m = basic.lidar_height_m;
        dashboard_.lidar.signal_strength = basic.lidar_signal_strength;
    }
    if ((basic.validity_flags & BasicHasTemperature) != 0 || (basic.validity_flags & BasicHasHumidity) != 0)
    {
        dashboard_.hmp.valid = true;
        dashboard_.hmp.temperature = basic.temperature_c;
        dashboard_.hmp.humidity = basic.humidity_percent;
    }
    if ((basic.validity_flags & BasicHasPressure) != 0)
    {
        dashboard_.ptb.valid = true;
        dashboard_.ptb.pressure_hpa = basic.pressure_hpa;
    }
}

void SkyLocalIpcClient::updateFromStatus(const TelemetryStatus& status)
{
    status_ = status;
    dashboard_.telemetry_status = status;
    dashboard_.wave_tcp_acquisition_rate_hz = status.wave_tcp_actual_rate_hz;
    dashboard_.raw_wave_recording_rate_hz = status.wave_tcp_actual_rate_hz;
    dashboard_.telemetry_basic_rate_hz = status.telemetry_basic_rate_hz;
    dashboard_.waveform_feature_rate_hz = status.feature_rate_hz;
    dashboard_.waveform_downsampled_rate_hz = status.waveform_rate_hz;
    dashboard_.uptime_ms = connected_time_us_ > 0
                                ? (currentTimestampUs() - connected_time_us_) / 1000ULL
                                : 0;
    updateDeviceFreshness();
}

void SkyLocalIpcClient::updateFromFeature(const WaveformFeature& feature)
{
    dashboard_.waveform_feature = feature;
    if (feature.quality_flags == 0 && std::isfinite(feature.peak))
    {
        dashboard_.peak_trend.push_back(feature.peak);
        while (dashboard_.peak_trend.size() > 256)
        {
            dashboard_.peak_trend.removeFirst();
        }
    }
}

void SkyLocalIpcClient::updateFromWaveform(const DownsampledWaveform& waveform)
{
    if (waveform.channel_id == 1)
    {
        dashboard_.latest_raw_waveform_preview = waveformPreview(waveform.samples, 2048);
    }
    else
    {
        dashboard_.latest_harmonic_waveform_preview = waveformPreview(waveform.samples, 2048);
    }
}

void SkyLocalIpcClient::updateDashboardRates()
{
    dashboard_.ptb_acquisition_rate_hz = config_.ptb.frequency_hz;
    dashboard_.hmp_acquisition_rate_hz = config_.hmp.frequency_hz;
    dashboard_.lidar_acquisition_rate_hz = config_.lidar.frequency_hz;
    dashboard_.devices_csv_recording_rate_hz = config_.telemetry.basic_rate_hz;
    dashboard_.raw_wave_recording_rate_hz = dashboard_.wave_tcp_acquisition_rate_hz;
    dashboard_.telemetry_basic_rate_hz = config_.telemetry.basic_rate_hz;
    dashboard_.waveform_feature_rate_hz = config_.telemetry.feature_rate_hz;
    dashboard_.waveform_downsampled_rate_hz = config_.telemetry.waveform_rate_hz;
}

void SkyLocalIpcClient::updateDeviceFreshness()
{
    const quint64 nowUs = currentTimestampUs();
    auto findStatus = [this](SkyDeviceId id) {
        for (const DeviceStatusItem& item : status_.devices)
        {
            if (item.device_id == id)
            {
                return item;
            }
        }
        DeviceStatusItem fallback;
        fallback.device_id = id;
        return fallback;
    };

    const DeviceStatusItem epsilon = findStatus(SkyDeviceId::Epsilon);
    const DeviceStatusItem ptb = findStatus(SkyDeviceId::Ptb);
    const DeviceStatusItem hmp = findStatus(SkyDeviceId::Hmp);
    const DeviceStatusItem lidar = findStatus(SkyDeviceId::Lidar);
    const DeviceStatusItem wave = findStatus(SkyDeviceId::WaveTcp);
    dashboard_.epsilon_stale = !isConnectedAndFresh(epsilon, nowUs, 2'000'000ULL);
    dashboard_.ptb_stale = !isConnectedAndFresh(ptb, nowUs, 3'000'000ULL);
    dashboard_.hmp_stale = !isConnectedAndFresh(hmp, nowUs, 3'000'000ULL);
    dashboard_.lidar_stale = !isConnectedAndFresh(lidar, nowUs, 2'000'000ULL);
    dashboard_.waveform_stale = !isConnectedAndFresh(wave, nowUs, 3'000'000ULL);
}

QVector<float> SkyLocalIpcClient::waveformPreview(const QVector<float>& samples, int maxPoints) const
{
    QVector<float> preview;
    if (samples.isEmpty() || maxPoints <= 0)
    {
        return preview;
    }
    if (samples.size() <= maxPoints)
    {
        return samples;
    }
    preview.reserve(maxPoints);
    const double step = static_cast<double>(samples.size()) / static_cast<double>(maxPoints);
    for (int i = 0; i < maxPoints; ++i)
    {
        const int sampleCount = static_cast<int>(samples.size());
        const int begin = std::clamp(static_cast<int>(i * step), 0, sampleCount - 1);
        const int end = std::clamp(static_cast<int>((i + 1) * step), begin + 1, sampleCount);
        float minValue = std::numeric_limits<float>::infinity();
        float maxValue = -std::numeric_limits<float>::infinity();
        for (int j = begin; j < end; ++j)
        {
            const float value = samples.at(j);
            if (!std::isfinite(value))
            {
                continue;
            }
            minValue = std::min(minValue, value);
            maxValue = std::max(maxValue, value);
        }
        preview.push_back(std::isfinite(maxValue) && std::isfinite(minValue)
                              ? (std::abs(maxValue) >= std::abs(minValue) ? maxValue : minValue)
                              : 0.0f);
    }
    return preview;
}

quint64 SkyLocalIpcClient::currentTimestampUs() const
{
    return static_cast<quint64>(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) * 1000ULL;
}

}  // namespace VaporView
