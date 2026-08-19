#include "SkyLocalIpcClient.h"

#include "LogService.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
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
        fields.insert(QStringLiteral("error_code"), QStringLiteral("SKY_TUI_IPC_ERROR"));
    }

    LogRecord record;
    record.level = level;
    record.source = QStringLiteral("SkyTui");
    record.category = category;
    record.message = message;
    record.fields = fields;
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
    reconnect_timer_.stop();
    connected_time_us_ = currentTimestampUs();
    emit connectedChanged(true);
    publishClientLog(LogLevel::Info,
                     QStringLiteral("ipc.connection"),
                     QStringLiteral("sky_ipc_connected"),
                     QStringLiteral("SkyCore IPC 已连接。"),
                     {{QStringLiteral("host"), core_host_},
                      {QStringLiteral("port"), core_port_}});
    QTimer::singleShot(0, this, [this]() {
        requestStatus();
        getConfig();
    });
}

void SkyLocalIpcClient::onDisconnected()
{
    emit connectedChanged(false);
    publishClientLog(LogLevel::Info,
                     QStringLiteral("ipc.connection"),
                     QStringLiteral("sky_ipc_disconnected"),
                     QStringLiteral("SkyCore IPC 连接已断开。"));
    scheduleReconnect();
}

void SkyLocalIpcClient::onReadyRead()
{
    const QVector<TelemetryFrame> frames = decoder_.feedBytes(socket_.readAll());
    for (const TelemetryFrame& frame : frames)
    {
        dispatchFrame(frame);
    }
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
    if (!isConnected())
    {
        publishClientLog(LogLevel::Warning,
                         QStringLiteral("ipc.command"),
                         QStringLiteral("sky_ipc_command_send_skipped"),
                         QStringLiteral("未连接 SkyCore，命令未发送。"),
                         {{QStringLiteral("reason_code"), QStringLiteral("SKY_IPC_NOT_CONNECTED")},
                          {QStringLiteral("command_id"), commandIdName(commandId)},
                          {QStringLiteral("command_value"), static_cast<quint16>(commandId)}});
        return 0;
    }

    CommandMessage command;
    command.command_id = commandId;
    command.command_seq = next_command_seq_++;
    command.payload = payload;
    const QByteArray encoded = encoder_.encodeFrame(MsgType::Command,
                                                    TelemetryCodec::serializeCommand(command),
                                                    next_frame_seq_++,
                                                    currentTimestampUs());
    socket_.write(encoded);
    return command.command_seq;
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
                             QStringLiteral("无法解析 SkyCore 激光温控状态载荷。"),
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
                             QStringLiteral("无法解析 SkyCore 系统温控状态载荷。"),
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
        dashboard_.epsilon.raw_frame_count = basic.raw_frame_count;
        dashboard_.epsilon.dropped_frame_count = basic.dropped_frame_count;
        dashboard_.epsilon.imu_packet_rate_hz = basic.imu_packet_rate_hz;
        dashboard_.epsilon.ahrs_packet_rate_hz = basic.ahrs_packet_rate_hz;
        dashboard_.epsilon.insgps_packet_rate_hz = basic.insgps_packet_rate_hz;
        dashboard_.epsilon.sys_state_packet_rate_hz = basic.sys_state_packet_rate_hz;
        dashboard_.epsilon.raw_gnss_packet_rate_hz = basic.raw_gnss_packet_rate_hz;
        dashboard_.epsilon.satellite_packet_rate_hz = basic.satellite_packet_rate_hz;
        dashboard_.epsilon.geodetic_packet_rate_hz = basic.geodetic_packet_rate_hz;
        dashboard_.epsilon.ecef_packet_rate_hz = basic.ecef_packet_rate_hz;
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
