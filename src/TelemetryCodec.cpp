#include "TelemetryCodec.h"

#include <QJsonDocument>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace VaporView
{
namespace
{
constexpr char kSof0 = static_cast<char>(0xAA);
constexpr char kSof1 = static_cast<char>(0x55);
constexpr int kSofSize = 2;
constexpr int kCrcSize = 2;

template <typename T>
void appendLe(QByteArray& out, T value)
{
    T le = qToLittleEndian(value);
    out.append(reinterpret_cast<const char*>(&le), sizeof(T));
}

void appendFloatLe(QByteArray& out, float value)
{
    quint32 bits = 0;
    std::memcpy(&bits, &value, sizeof(float));
    appendLe<quint32>(out, bits);
}

template <typename T>
bool readLe(const QByteArray& payload, qsizetype& offset, T& value)
{
    if (offset < 0 || offset + static_cast<qsizetype>(sizeof(T)) > payload.size())
    {
        return false;
    }
    value = qFromLittleEndian<T>(reinterpret_cast<const uchar*>(payload.constData() + offset));
    offset += sizeof(T);
    return true;
}

bool readFloatLe(const QByteArray& payload, qsizetype& offset, float& value)
{
    quint32 bits = 0;
    if (!readLe(payload, offset, bits))
    {
        return false;
    }
    std::memcpy(&value, &bits, sizeof(float));
    return true;
}

void appendDeviceStatus(QByteArray& out, const DeviceStatusItem& item)
{
    out.append(static_cast<char>(item.device_id));
    out.append(static_cast<char>(item.state));
    appendLe<quint16>(out, item.error_code);
    appendLe<quint32>(out, item.rx_count);
    appendLe<quint32>(out, item.error_count);
    appendLe<quint64>(out, item.last_data_time_us);
}

bool readDeviceStatus(const QByteArray& payload, qsizetype& offset, DeviceStatusItem& item)
{
    if (offset + 20 > payload.size())
    {
        return false;
    }
    SkyDeviceId id = SkyDeviceId::Epsilon;
    if (!skyDeviceIdFromValue(static_cast<quint8>(payload.at(offset)), id))
    {
        return false;
    }
    item.device_id = id;
    item.state = static_cast<DeviceState>(static_cast<quint8>(payload.at(offset + 1)));
    offset += 2;
    return readLe(payload, offset, item.error_code) &&
           readLe(payload, offset, item.rx_count) &&
           readLe(payload, offset, item.error_count) &&
           readLe(payload, offset, item.last_data_time_us);
}

}  // namespace

QString skyDeviceIdName(SkyDeviceId id)
{
    switch (id)
    {
    case SkyDeviceId::Epsilon:
        return QStringLiteral("epsilon");
    case SkyDeviceId::Ptb:
        return QStringLiteral("ptb");
    case SkyDeviceId::Hmp:
        return QStringLiteral("hmp");
    case SkyDeviceId::Lidar:
        return QStringLiteral("lidar");
    case SkyDeviceId::WaveTcp:
        return QStringLiteral("wave_tcp");
    case SkyDeviceId::All:
        return QStringLiteral("all");
    }
    return QStringLiteral("unknown");
}

QString deviceStateName(DeviceState state)
{
    switch (state)
    {
    case DeviceState::Disabled:
        return QStringLiteral("disabled");
    case DeviceState::Disconnected:
        return QStringLiteral("disconnected");
    case DeviceState::Connecting:
        return QStringLiteral("connecting");
    case DeviceState::Connected:
        return QStringLiteral("connected");
    case DeviceState::Error:
        return QStringLiteral("error");
    case DeviceState::Reconnecting:
        return QStringLiteral("reconnecting");
    }
    return QStringLiteral("unknown");
}

QString commandIdName(CommandId id)
{
    switch (id)
    {
    case CommandId::StartRecording: return QStringLiteral("StartRecording");
    case CommandId::PauseRecording: return QStringLiteral("PauseRecording");
    case CommandId::StopRecording: return QStringLiteral("StopRecording");
    case CommandId::SetTelemetryRate: return QStringLiteral("SetTelemetryRate");
    case CommandId::SetWaveformRate: return QStringLiteral("SetWaveformRate");
    case CommandId::SetFeatureRate: return QStringLiteral("SetFeatureRate");
    case CommandId::EnableWaveformStreaming: return QStringLiteral("EnableWaveformStreaming");
    case CommandId::DisableWaveformStreaming: return QStringLiteral("DisableWaveformStreaming");
    case CommandId::RequestOneWaveform: return QStringLiteral("RequestOneWaveform");
    case CommandId::RequestStatus: return QStringLiteral("RequestStatus");
    case CommandId::RebootDevice: return QStringLiteral("RebootDevice");
    case CommandId::QueryDeviceStatus: return QStringLiteral("QueryDeviceStatus");
    case CommandId::ConnectDevice: return QStringLiteral("ConnectDevice");
    case CommandId::DisconnectDevice: return QStringLiteral("DisconnectDevice");
    case CommandId::ReconnectDevice: return QStringLiteral("ReconnectDevice");
    case CommandId::ConnectAllDevices: return QStringLiteral("ConnectAllDevices");
    case CommandId::DisconnectAllDevices: return QStringLiteral("DisconnectAllDevices");
    case CommandId::ReconnectAllDevices: return QStringLiteral("ReconnectAllDevices");
    case CommandId::GetSkyConfig: return QStringLiteral("GetSkyConfig");
    case CommandId::SetSkyConfig: return QStringLiteral("SetSkyConfig");
    case CommandId::SaveSkyConfig: return QStringLiteral("SaveSkyConfig");
    case CommandId::ReloadSkyConfig: return QStringLiteral("ReloadSkyConfig");
    case CommandId::SetPeakSearchRange: return QStringLiteral("SetPeakSearchRange");
    }
    return QStringLiteral("UnknownCommand");
}

QString commandErrorCodeText(CommandErrorCode error, bool english)
{
    switch (error)
    {
    case CommandErrorCode::Ok:
        return english ? QStringLiteral("Ok") : QStringLiteral("成功");
    case CommandErrorCode::UnknownCommand:
        return english ? QStringLiteral("Unknown command") : QStringLiteral("未知命令");
    case CommandErrorCode::InvalidPayload:
        return english ? QStringLiteral("Invalid payload") : QStringLiteral("载荷无效");
    case CommandErrorCode::InvalidDeviceId:
        return english ? QStringLiteral("Invalid device id") : QStringLiteral("设备ID无效");
    case CommandErrorCode::DeviceAlreadyConnected:
        return english ? QStringLiteral("Device already connected") : QStringLiteral("设备已连接");
    case CommandErrorCode::DeviceNotConnected:
        return english ? QStringLiteral("Device not connected") : QStringLiteral("设备未连接");
    case CommandErrorCode::DeviceConnectFailed:
        return english ? QStringLiteral("Device connect failed") : QStringLiteral("设备连接失败");
    case CommandErrorCode::DeviceDisconnectFailed:
        return english ? QStringLiteral("Device disconnect failed") : QStringLiteral("设备断开失败");
    case CommandErrorCode::DeviceReconnectFailed:
        return english ? QStringLiteral("Device reconnect failed") : QStringLiteral("设备重连失败");
    case CommandErrorCode::ConfigInvalid:
        return english ? QStringLiteral("Invalid config") : QStringLiteral("配置无效");
    case CommandErrorCode::ConfigApplyFailed:
        return english ? QStringLiteral("Config apply failed") : QStringLiteral("配置应用失败");
    case CommandErrorCode::ConfigSaveFailed:
        return english ? QStringLiteral("Config save failed") : QStringLiteral("配置保存失败");
    case CommandErrorCode::RecordingAlreadyStarted:
        return english ? QStringLiteral("Recording already started") : QStringLiteral("记录已开始");
    case CommandErrorCode::RecordingNotStarted:
        return english ? QStringLiteral("Recording not started") : QStringLiteral("记录未开始");
    case CommandErrorCode::InternalError:
        return english ? QStringLiteral("Internal error") : QStringLiteral("内部错误");
    }
    return english ? QStringLiteral("Unknown error") : QStringLiteral("未知错误");
}

bool skyDeviceIdFromValue(quint8 value, SkyDeviceId& id)
{
    switch (value)
    {
    case 1:
        id = SkyDeviceId::Epsilon;
        return true;
    case 2:
        id = SkyDeviceId::Ptb;
        return true;
    case 3:
        id = SkyDeviceId::Hmp;
        return true;
    case 4:
        id = SkyDeviceId::Lidar;
        return true;
    case 5:
        id = SkyDeviceId::WaveTcp;
        return true;
    case 255:
        id = SkyDeviceId::All;
        return true;
    default:
        return false;
    }
}

TelemetryCodec::TelemetryCodec(quint32 maxPayloadSize)
    : max_payload_size_(maxPayloadSize)
{
}

QByteArray TelemetryCodec::encodeFrame(MsgType type,
                                       const QByteArray& payload,
                                       quint16 seq,
                                       quint64 timeUs,
                                       quint8 flags) const
{
    QByteArray body;
    body.reserve(kHeaderSizeWithoutSof + payload.size());
    body.append(static_cast<char>(kProtocolVersion));
    body.append(static_cast<char>(type));
    body.append(static_cast<char>(flags));
    appendLe<quint16>(body, seq);
    appendLe<quint64>(body, timeUs);
    appendLe<quint32>(body, static_cast<quint32>(payload.size()));
    body.append(payload);

    const quint16 crc = crc16Ccitt(body.constData(), body.size());
    QByteArray frame;
    frame.reserve(kSofSize + body.size() + kCrcSize);
    frame.append(kSof0);
    frame.append(kSof1);
    frame.append(body);
    appendLe<quint16>(frame, crc);
    return frame;
}

QVector<TelemetryFrame> TelemetryCodec::feedBytes(const QByteArray& bytes)
{
    buffer_.append(bytes);
    QVector<TelemetryFrame> frames;

    while (true)
    {
        int sof = -1;
        for (int i = 0; i + 1 < buffer_.size(); ++i)
        {
            if (buffer_.at(i) == kSof0 && buffer_.at(i + 1) == kSof1)
            {
                sof = i;
                break;
            }
        }

        if (sof < 0)
        {
            if (buffer_.size() > 1)
            {
                buffer_ = buffer_.right(1);
            }
            return frames;
        }
        if (sof > 0)
        {
            buffer_.remove(0, sof);
        }

        const int minFrameSize = kSofSize + kHeaderSizeWithoutSof + kCrcSize;
        if (buffer_.size() < minFrameSize)
        {
            return frames;
        }

        qsizetype offset = kSofSize;
        const quint8 version = static_cast<quint8>(buffer_.at(offset++));
        const quint8 rawType = static_cast<quint8>(buffer_.at(offset++));
        const quint8 flags = static_cast<quint8>(buffer_.at(offset++));
        const quint16 seq = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(buffer_.constData() + offset));
        offset += sizeof(quint16);
        const quint64 timeUs = qFromLittleEndian<quint64>(reinterpret_cast<const uchar*>(buffer_.constData() + offset));
        offset += sizeof(quint64);
        const quint32 len = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(buffer_.constData() + offset));
        offset += sizeof(quint32);

        if (len > max_payload_size_)
        {
            ++dropped_frame_count_;
            buffer_.remove(0, 1);
            continue;
        }

        const qsizetype frameSize = kSofSize + kHeaderSizeWithoutSof + static_cast<qsizetype>(len) + kCrcSize;
        if (buffer_.size() < frameSize)
        {
            return frames;
        }

        const qsizetype crcOffset = kSofSize + kHeaderSizeWithoutSof + static_cast<qsizetype>(len);
        const quint16 expectedCrc = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(buffer_.constData() + crcOffset));
        const quint16 actualCrc = crc16Ccitt(buffer_.constData() + kSofSize, kHeaderSizeWithoutSof + static_cast<qsizetype>(len));
        if (expectedCrc != actualCrc || version != kProtocolVersion)
        {
            ++crc_error_count_;
            buffer_.remove(0, 1);
            continue;
        }

        TelemetryFrame frame;
        frame.version = version;
        frame.type = static_cast<MsgType>(rawType);
        frame.flags = flags;
        frame.seq = seq;
        frame.time_us = timeUs;
        frame.payload = buffer_.mid(kSofSize + kHeaderSizeWithoutSof, static_cast<int>(len));
        frames.push_back(frame);
        buffer_.remove(0, static_cast<int>(frameSize));
    }
}

void TelemetryCodec::reset()
{
    buffer_.clear();
    crc_error_count_ = 0;
    dropped_frame_count_ = 0;
}

quint64 TelemetryCodec::crcErrorCount() const
{
    return crc_error_count_;
}

quint64 TelemetryCodec::droppedFrameCount() const
{
    return dropped_frame_count_;
}

quint16 TelemetryCodec::crc16Ccitt(const char *data, qsizetype size)
{
    quint16 crc = 0xFFFF;
    for (qsizetype i = 0; i < size; ++i)
    {
        crc ^= static_cast<quint16>(static_cast<quint8>(data[i])) << 8;
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc & 0x8000) != 0 ? static_cast<quint16>((crc << 1) ^ 0x1021) : static_cast<quint16>(crc << 1);
        }
    }
    return crc;
}

QByteArray TelemetryCodec::serializeBasicTelemetry(const TelemetryBasic& data)
{
    QByteArray payload;
    payload.reserve(87);
    appendLe<quint64>(payload, data.host_time_us);
    appendLe<quint64>(payload, data.epsilon_time_us);
    for (double value : {data.latitude_deg, data.longitude_deg, data.height_m, data.ecef_x_m, data.ecef_y_m, data.ecef_z_m})
    {
        quint64 bits = 0;
        std::memcpy(&bits, &value, sizeof(double));
        appendLe<quint64>(payload, bits);
    }
    appendFloatLe(payload, data.lidar_height_m);
    appendFloatLe(payload, data.temperature_c);
    appendFloatLe(payload, data.humidity_percent);
    appendFloatLe(payload, data.pressure_hpa);
    appendLe<quint16>(payload, data.status_bits);
    appendLe<quint16>(payload, data.filter_status_bits);
    appendLe<quint16>(payload, data.update_status_bits);
    payload.append(static_cast<char>(data.gnss_fix_code));
    appendLe<quint32>(payload, data.validity_flags);
    return payload;
}

bool TelemetryCodec::parseBasicTelemetry(const QByteArray& payload, TelemetryBasic& data)
{
    data = TelemetryBasic();
    qsizetype offset = 0;
    if (!readLe(payload, offset, data.host_time_us) || !readLe(payload, offset, data.epsilon_time_us))
    {
        return false;
    }
    double *doubles[] = {&data.latitude_deg, &data.longitude_deg, &data.height_m, &data.ecef_x_m, &data.ecef_y_m, &data.ecef_z_m};
    for (double *value : doubles)
    {
        quint64 bits = 0;
        if (!readLe(payload, offset, bits))
        {
            return false;
        }
        std::memcpy(value, &bits, sizeof(double));
    }
    if (!(readFloatLe(payload, offset, data.lidar_height_m) &&
          readFloatLe(payload, offset, data.temperature_c) &&
          readFloatLe(payload, offset, data.humidity_percent) &&
          readFloatLe(payload, offset, data.pressure_hpa) &&
          readLe(payload, offset, data.status_bits)))
    {
        return false;
    }
    (void)readLe(payload, offset, data.filter_status_bits);
    (void)readLe(payload, offset, data.update_status_bits);
    if (offset < payload.size())
    {
        data.gnss_fix_code = static_cast<quint8>(static_cast<unsigned char>(payload.at(offset)));
        ++offset;
    }
    (void)readLe(payload, offset, data.validity_flags);
    return true;
}

QByteArray TelemetryCodec::serializeWaveformFeature(const WaveformFeature& feature)
{
    QByteArray payload;
    payload.reserve(64);
    appendLe<quint64>(payload, feature.host_time_us);
    appendLe<quint64>(payload, feature.epsilon_time_us);
    appendLe<quint16>(payload, feature.channel_id);
    appendLe<quint16>(payload, 0);
    appendLe<quint32>(payload, feature.original_point_count);
    appendLe<quint32>(payload, feature.search_start_index);
    appendLe<quint32>(payload, feature.search_end_index);
    appendFloatLe(payload, feature.peak);
    appendFloatLe(payload, feature.mean);
    appendFloatLe(payload, feature.rms);
    appendFloatLe(payload, feature.peak_index);
    appendFloatLe(payload, feature.peak_x);
    appendFloatLe(payload, feature.min_value);
    appendFloatLe(payload, feature.max_value);
    appendLe<quint32>(payload, feature.quality_flags);
    return payload;
}

bool TelemetryCodec::parseWaveformFeature(const QByteArray& payload, WaveformFeature& feature)
{
    feature = WaveformFeature();
    qsizetype offset = 0;
    quint16 reserved = 0;
    if (!readLe(payload, offset, feature.host_time_us) ||
        !readLe(payload, offset, feature.epsilon_time_us) ||
        !readLe(payload, offset, feature.channel_id) ||
        !readLe(payload, offset, reserved))
    {
        return false;
    }

    if (payload.size() >= 64)
    {
        if (!readLe(payload, offset, feature.original_point_count) ||
            !readLe(payload, offset, feature.search_start_index) ||
            !readLe(payload, offset, feature.search_end_index))
        {
            return false;
        }
    }

    return readFloatLe(payload, offset, feature.peak) &&
           readFloatLe(payload, offset, feature.mean) &&
           readFloatLe(payload, offset, feature.rms) &&
           readFloatLe(payload, offset, feature.peak_index) &&
           readFloatLe(payload, offset, feature.peak_x) &&
           readFloatLe(payload, offset, feature.min_value) &&
           readFloatLe(payload, offset, feature.max_value) &&
           readLe(payload, offset, feature.quality_flags);
}

QByteArray TelemetryCodec::serializeDownsampledWaveform(const DownsampledWaveform& waveform)
{
    QByteArray payload;
    payload.reserve(36 + waveform.samples.size() * static_cast<int>(sizeof(float)));
    appendLe<quint64>(payload, waveform.host_time_us);
    appendLe<quint64>(payload, waveform.epsilon_time_us);
    appendLe<quint32>(payload, waveform.original_point_count);
    appendLe<quint32>(payload, static_cast<quint32>(waveform.samples.size()));
    appendLe<quint16>(payload, waveform.channel_id);
    appendLe<quint16>(payload, waveform.sample_format);
    appendFloatLe(payload, waveform.x_start);
    appendFloatLe(payload, waveform.x_step);
    for (float sample : waveform.samples)
    {
        appendFloatLe(payload, sample);
    }
    return payload;
}

bool TelemetryCodec::parseDownsampledWaveform(const QByteArray& payload, DownsampledWaveform& waveform)
{
    qsizetype offset = 0;
    if (!readLe(payload, offset, waveform.host_time_us) ||
        !readLe(payload, offset, waveform.epsilon_time_us) ||
        !readLe(payload, offset, waveform.original_point_count) ||
        !readLe(payload, offset, waveform.downsampled_point_count) ||
        !readLe(payload, offset, waveform.channel_id) ||
        !readLe(payload, offset, waveform.sample_format) ||
        !readFloatLe(payload, offset, waveform.x_start) ||
        !readFloatLe(payload, offset, waveform.x_step))
    {
        return false;
    }
    if (waveform.sample_format != 1 ||
        waveform.downsampled_point_count > 200000 ||
        offset + static_cast<qsizetype>(waveform.downsampled_point_count * sizeof(float)) > payload.size())
    {
        return false;
    }
    waveform.samples.resize(static_cast<int>(waveform.downsampled_point_count));
    for (quint32 i = 0; i < waveform.downsampled_point_count; ++i)
    {
        if (!readFloatLe(payload, offset, waveform.samples[static_cast<int>(i)]))
        {
            return false;
        }
    }
    return true;
}

QByteArray TelemetryCodec::serializeTelemetryStatus(const TelemetryStatus& status)
{
    const QByteArray sessionName = status.session_name.toUtf8();
    QByteArray payload;
    payload.reserve(64 + sessionName.size() + status.devices.size() * 20);
    payload.append(static_cast<char>(status.recording_state));
    payload.append('\0');
    appendLe<quint16>(payload, static_cast<quint16>(status.devices.size()));
    appendLe<quint64>(payload, status.disk_free_bytes);
    appendFloatLe(payload, status.telemetry_basic_rate_hz);
    appendFloatLe(payload, status.waveform_rate_hz);
    appendFloatLe(payload, status.feature_rate_hz);
    appendFloatLe(payload, status.heartbeat_rate_hz);
    appendFloatLe(payload, status.status_rate_hz);
    appendLe<quint32>(payload, status.rx_total_frames);
    appendLe<quint32>(payload, status.crc_error_count);
    appendLe<quint32>(payload, status.current_seq);
    appendLe<quint64>(payload, status.last_frame_time_us);
    const int sessionNameBytes = std::min<int>(sessionName.size(), 65535);
    appendLe<quint16>(payload, static_cast<quint16>(sessionNameBytes));
    payload.append(sessionName.constData(), sessionNameBytes);
    for (const DeviceStatusItem& item : status.devices)
    {
        appendDeviceStatus(payload, item);
    }
    return payload;
}

bool TelemetryCodec::parseTelemetryStatus(const QByteArray& payload, TelemetryStatus& status)
{
    qsizetype offset = 0;
    if (payload.size() < 2)
    {
        return false;
    }
    status.recording_state = static_cast<quint8>(payload.at(offset++));
    ++offset;
    quint16 deviceCount = 0;
    quint16 sessionNameSize = 0;
    if (!readLe(payload, offset, deviceCount) ||
        !readLe(payload, offset, status.disk_free_bytes) ||
        !readFloatLe(payload, offset, status.telemetry_basic_rate_hz) ||
        !readFloatLe(payload, offset, status.waveform_rate_hz) ||
        !readFloatLe(payload, offset, status.feature_rate_hz) ||
        !readFloatLe(payload, offset, status.heartbeat_rate_hz) ||
        !readFloatLe(payload, offset, status.status_rate_hz) ||
        !readLe(payload, offset, status.rx_total_frames) ||
        !readLe(payload, offset, status.crc_error_count) ||
        !readLe(payload, offset, status.current_seq) ||
        !readLe(payload, offset, status.last_frame_time_us) ||
        !readLe(payload, offset, sessionNameSize))
    {
        return false;
    }
    if (offset + sessionNameSize > payload.size())
    {
        return false;
    }
    status.session_name = QString::fromUtf8(payload.constData() + offset, sessionNameSize);
    offset += sessionNameSize;
    status.devices.clear();
    status.devices.reserve(deviceCount);
    for (quint16 i = 0; i < deviceCount; ++i)
    {
        DeviceStatusItem item;
        if (!readDeviceStatus(payload, offset, item))
        {
            return false;
        }
        status.devices.push_back(item);
    }
    return true;
}

QByteArray TelemetryCodec::serializeCommand(const CommandMessage& command)
{
    QByteArray payload;
    payload.reserve(8 + command.payload.size());
    appendLe<quint16>(payload, static_cast<quint16>(command.command_id));
    appendLe<quint16>(payload, command.command_seq);
    appendLe<quint32>(payload, static_cast<quint32>(command.payload.size()));
    payload.append(command.payload);
    return payload;
}

bool TelemetryCodec::parseCommand(const QByteArray& payload, CommandMessage& command)
{
    qsizetype offset = 0;
    quint16 commandId = 0;
    quint32 payloadLen = 0;
    if (!readLe(payload, offset, commandId) ||
        !readLe(payload, offset, command.command_seq) ||
        !readLe(payload, offset, payloadLen) ||
        offset + static_cast<qsizetype>(payloadLen) > payload.size())
    {
        return false;
    }
    command.command_id = static_cast<CommandId>(commandId);
    command.payload = payload.mid(offset, static_cast<int>(payloadLen));
    return true;
}

QByteArray TelemetryCodec::serializeCommandAck(const CommandAck& ack)
{
    QByteArray payload;
    payload.reserve(12);
    appendLe<quint16>(payload, static_cast<quint16>(ack.command_id));
    appendLe<quint16>(payload, ack.command_seq);
    payload.append(static_cast<char>(ack.result));
    payload.append('\0');
    appendLe<quint32>(payload, static_cast<quint32>(ack.error_code));
    return payload;
}

bool TelemetryCodec::parseCommandAck(const QByteArray& payload, CommandAck& ack)
{
    qsizetype offset = 0;
    quint16 commandId = 0;
    quint32 errorCode = 0;
    quint8 reserved = 0;
    if (!readLe(payload, offset, commandId) ||
        !readLe(payload, offset, ack.command_seq) ||
        offset + 2 > payload.size())
    {
        return false;
    }
    ack.command_id = static_cast<CommandId>(commandId);
    ack.result = static_cast<quint8>(payload.at(offset++));
    reserved = static_cast<quint8>(payload.at(offset++));
    Q_UNUSED(reserved);
    if (!readLe(payload, offset, errorCode))
    {
        return false;
    }
    ack.error_code = static_cast<CommandErrorCode>(errorCode);
    return true;
}

QByteArray TelemetryCodec::serializeDeviceCommand(SkyDeviceId id)
{
    QByteArray payload;
    payload.resize(4);
    payload[0] = static_cast<char>(id);
    payload[1] = payload[2] = payload[3] = '\0';
    return payload;
}

bool TelemetryCodec::parseDeviceCommand(const QByteArray& payload, SkyDeviceId& id)
{
    if (payload.isEmpty())
    {
        id = SkyDeviceId::All;
        return true;
    }
    if (payload.size() < 4)
    {
        return false;
    }
    return skyDeviceIdFromValue(static_cast<quint8>(payload.at(0)), id);
}

QByteArray TelemetryCodec::serializeRatePayload(quint16 hz)
{
    QByteArray payload;
    appendLe<quint16>(payload, hz);
    return payload;
}

bool TelemetryCodec::parseRatePayload(const QByteArray& payload, quint16& hz)
{
    qsizetype offset = 0;
    return readLe(payload, offset, hz);
}

QByteArray TelemetryCodec::serializePeakSearchRange(const PeakSearchRange& range)
{
    QByteArray payload;
    appendLe<quint32>(payload, range.start_index);
    appendLe<quint32>(payload, range.end_index);
    return payload;
}

bool TelemetryCodec::parsePeakSearchRange(const QByteArray& payload, PeakSearchRange& range)
{
    qsizetype offset = 0;
    return readLe(payload, offset, range.start_index) &&
           readLe(payload, offset, range.end_index);
}

}  // namespace VaporView
