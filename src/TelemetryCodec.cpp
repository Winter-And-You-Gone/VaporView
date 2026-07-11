#include "TelemetryCodec.h"

#include <QJsonDocument>
#include <QVector>
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

void appendDoubleLe(QByteArray& out, double value)
{
    quint64 bits = 0;
    std::memcpy(&bits, &value, sizeof(double));
    appendLe<quint64>(out, bits);
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

bool readDoubleLe(const QByteArray& payload, qsizetype& offset, double& value)
{
    quint64 bits = 0;
    if (!readLe(payload, offset, bits))
    {
        return false;
    }
    std::memcpy(&value, &bits, sizeof(double));
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
    case SkyDeviceId::TemperatureController:
        return QStringLiteral("temperature_controller");
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
    case CommandId::SetTemperatureTarget: return QStringLiteral("SetTemperatureTarget");
    case CommandId::SetTemperatureOutputEnabled: return QStringLiteral("SetTemperatureOutputEnabled");
    case CommandId::SetTemperatureOutputMode: return QStringLiteral("SetTemperatureOutputMode");
    case CommandId::SetTemperatureMaxOutputPercent: return QStringLiteral("SetTemperatureMaxOutputPercent");
    case CommandId::SetTemperaturePid: return QStringLiteral("SetTemperaturePid");
    case CommandId::SetTemperatureAutoPid: return QStringLiteral("SetTemperatureAutoPid");
    case CommandId::SetTemperatureControllerMode: return QStringLiteral("SetTemperatureControllerMode");
    case CommandId::SetTemperatureDeviceAddress: return QStringLiteral("SetTemperatureDeviceAddress");
    case CommandId::SetTemperatureRs485Baud: return QStringLiteral("SetTemperatureRs485Baud");
    case CommandId::SetTemperatureOvertempOutputMode: return QStringLiteral("SetTemperatureOvertempOutputMode");
    case CommandId::RestoreTemperatureFactoryDefaults: return QStringLiteral("RestoreTemperatureFactoryDefaults");
    case CommandId::SetTemperatureSensorConfig: return QStringLiteral("SetTemperatureSensorConfig");
    case CommandId::ShutdownCore: return QStringLiteral("ShutdownCore");
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
    case 6:
        id = SkyDeviceId::TemperatureController;
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
    payload.reserve(208);
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
    appendLe<quint16>(payload, data.gnss_satellites);
    appendLe<quint16>(payload, data.lidar_signal_strength);
    appendFloatLe(payload, data.hdop);
    appendFloatLe(payload, data.vdop);
    appendFloatLe(payload, data.hacc_m);
    appendFloatLe(payload, data.vacc_m);
    payload.append(data.heading_valid ? char(1) : char(0));
    appendFloatLe(payload, data.vel_n_mps);
    appendFloatLe(payload, data.vel_e_mps);
    appendFloatLe(payload, data.vel_d_mps);
    appendFloatLe(payload, data.imu_acc_x_mps2);
    appendFloatLe(payload, data.imu_acc_y_mps2);
    appendFloatLe(payload, data.imu_acc_z_mps2);
    appendFloatLe(payload, data.imu_gyr_x_radps);
    appendFloatLe(payload, data.imu_gyr_y_radps);
    appendFloatLe(payload, data.imu_gyr_z_radps);
    appendFloatLe(payload, data.roll_deg);
    appendFloatLe(payload, data.pitch_deg);
    appendFloatLe(payload, data.yaw_deg);
    appendLe<quint64>(payload, data.raw_frame_count);
    appendLe<quint64>(payload, data.dropped_frame_count);
    appendFloatLe(payload, data.imu_packet_rate_hz);
    appendFloatLe(payload, data.ahrs_packet_rate_hz);
    appendFloatLe(payload, data.insgps_packet_rate_hz);
    appendFloatLe(payload, data.sys_state_packet_rate_hz);
    appendFloatLe(payload, data.raw_gnss_packet_rate_hz);
    appendFloatLe(payload, data.satellite_packet_rate_hz);
    appendFloatLe(payload, data.geodetic_packet_rate_hz);
    appendFloatLe(payload, data.ecef_packet_rate_hz);
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
    if (offset >= payload.size())
    {
        return true;
    }
    if (!(readLe(payload, offset, data.gnss_satellites) &&
          readLe(payload, offset, data.lidar_signal_strength) &&
          readFloatLe(payload, offset, data.hdop) &&
          readFloatLe(payload, offset, data.vdop) &&
          readFloatLe(payload, offset, data.hacc_m) &&
          readFloatLe(payload, offset, data.vacc_m)))
    {
        return false;
    }
    if (offset >= payload.size())
    {
        return false;
    }
    data.heading_valid = payload.at(offset) != 0;
    ++offset;
    if (!(readFloatLe(payload, offset, data.vel_n_mps) &&
          readFloatLe(payload, offset, data.vel_e_mps) &&
          readFloatLe(payload, offset, data.vel_d_mps) &&
          readFloatLe(payload, offset, data.imu_acc_x_mps2) &&
          readFloatLe(payload, offset, data.imu_acc_y_mps2) &&
          readFloatLe(payload, offset, data.imu_acc_z_mps2) &&
          readFloatLe(payload, offset, data.imu_gyr_x_radps) &&
          readFloatLe(payload, offset, data.imu_gyr_y_radps) &&
          readFloatLe(payload, offset, data.imu_gyr_z_radps) &&
          readFloatLe(payload, offset, data.roll_deg) &&
          readFloatLe(payload, offset, data.pitch_deg) &&
          readFloatLe(payload, offset, data.yaw_deg)))
    {
        return false;
    }
    if (offset >= payload.size())
    {
        return true;
    }
    if (!(readLe(payload, offset, data.raw_frame_count) &&
          readLe(payload, offset, data.dropped_frame_count) &&
          readFloatLe(payload, offset, data.imu_packet_rate_hz) &&
          readFloatLe(payload, offset, data.ahrs_packet_rate_hz) &&
          readFloatLe(payload, offset, data.insgps_packet_rate_hz) &&
          readFloatLe(payload, offset, data.sys_state_packet_rate_hz) &&
          readFloatLe(payload, offset, data.raw_gnss_packet_rate_hz) &&
          readFloatLe(payload, offset, data.satellite_packet_rate_hz) &&
          readFloatLe(payload, offset, data.geodetic_packet_rate_hz) &&
          readFloatLe(payload, offset, data.ecef_packet_rate_hz)))
    {
        return false;
    }
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
    appendFloatLe(payload, status.wave_tcp_actual_rate_hz);
    appendLe<quint64>(payload, status.recording_start_time_us);
    appendLe<quint64>(payload, status.recording_elapsed_ms);
    appendLe<quint64>(payload, status.telemetry_record_count);
    appendLe<quint64>(payload, status.waveform_feature_record_count);
    appendLe<quint64>(payload, status.waveform_snapshot_record_count);
    appendLe<quint64>(payload, status.raw_epsilon_record_count);
    appendLe<quint64>(payload, status.raw_ptb_record_count);
    appendLe<quint64>(payload, status.raw_hmp_record_count);
    appendLe<quint64>(payload, status.raw_lidar_record_count);
    appendLe<quint64>(payload, status.raw_tcp_wave_record_count);
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
    if (offset < payload.size())
    {
        if (!readFloatLe(payload, offset, status.wave_tcp_actual_rate_hz))
        {
            return false;
        }
    }
    else
    {
        status.wave_tcp_actual_rate_hz = 0.0f;
    }
    auto readOptionalU64 = [&payload, &offset](quint64& value) {
        if (offset >= payload.size())
        {
            value = 0;
            return true;
        }
        return readLe(payload, offset, value);
    };
    return readOptionalU64(status.recording_start_time_us) &&
           readOptionalU64(status.recording_elapsed_ms) &&
           readOptionalU64(status.telemetry_record_count) &&
           readOptionalU64(status.waveform_feature_record_count) &&
           readOptionalU64(status.waveform_snapshot_record_count) &&
           readOptionalU64(status.raw_epsilon_record_count) &&
           readOptionalU64(status.raw_ptb_record_count) &&
           readOptionalU64(status.raw_hmp_record_count) &&
           readOptionalU64(status.raw_lidar_record_count) &&
           readOptionalU64(status.raw_tcp_wave_record_count);
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

QByteArray TelemetryCodec::serializeTemperatureControllerStatus(const TemperatureControllerData& data)
{
    QByteArray payload;
    payload.reserve(320);
    payload.append(data.valid ? char(1) : char(0));
    payload.append('\0');
    appendLe<quint16>(payload, data.error_code);
    appendDoubleLe(payload, data.internal_temperature_c);
    for (const TemperatureControllerChannelData& channel : data.channels)
    {
        appendDoubleLe(payload, channel.target_temperature_c);
        appendDoubleLe(payload, channel.measured_temperature_c);
        appendDoubleLe(payload, channel.output_percent);
        appendDoubleLe(payload, channel.output_current_a);
        appendLe<quint16>(payload, static_cast<quint16>(std::clamp(channel.output_mode, 0, 65535)));
        payload.append(channel.output_enabled ? char(1) : char(0));
        payload.append('\0');
        appendLe<quint16>(payload, static_cast<quint16>(std::clamp(channel.max_output_percent, 0, 65535)));
        appendLe<quint32>(payload, static_cast<quint32>(std::clamp(channel.kp, 0, std::numeric_limits<int>::max())));
        appendLe<quint32>(payload, static_cast<quint32>(std::clamp(channel.ki, 0, std::numeric_limits<int>::max())));
        appendLe<quint32>(payload, static_cast<quint32>(std::clamp(channel.kd, 0, std::numeric_limits<int>::max())));
        appendLe<quint16>(payload, static_cast<quint16>(std::clamp(channel.sensor_model, 0, 65535)));
        appendLe<quint32>(payload, static_cast<quint32>(std::clamp(channel.ntc_b, 0, std::numeric_limits<int>::max())));
        appendLe<quint32>(payload, static_cast<quint32>(std::clamp(channel.ntc_r0, 0, std::numeric_limits<int>::max())));
        appendLe<quint32>(payload, static_cast<quint32>(std::clamp(channel.pt_r0, 0, std::numeric_limits<int>::max())));
        appendLe<qint32>(payload, static_cast<qint32>(channel.pt_a));
        appendLe<qint32>(payload, static_cast<qint32>(channel.pt_b));
        appendLe<qint32>(payload, static_cast<qint32>(channel.pt_c));
        for (size_t i = 0; i < channel.polynomial_mantissas.size(); ++i)
        {
            appendLe<qint64>(payload, static_cast<qint64>(channel.polynomial_mantissas[i]));
            appendLe<qint16>(payload, static_cast<qint16>(channel.polynomial_exponents[i]));
        }
    }
    appendLe<quint16>(payload, static_cast<quint16>(std::clamp(data.controller_mode, 0, 65535)));
    for (const TemperatureControllerChannelData& channel : data.channels)
    {
        appendLe<quint16>(payload, static_cast<quint16>(std::clamp(channel.auto_pid_mode, 0, 65535)));
    }
    appendLe<quint16>(payload, static_cast<quint16>(std::clamp(data.device_address, 0, 65535)));
    appendLe<quint16>(payload, static_cast<quint16>(std::clamp(data.rs485_baud_index, 0, 65535)));
    appendLe<quint16>(payload, static_cast<quint16>(std::clamp(data.overtemp_output_mode, 0, 65535)));
    return payload;
}

bool TelemetryCodec::parseTemperatureControllerStatus(const QByteArray& payload, TemperatureControllerData& data)
{
    data = TemperatureControllerData();
    qsizetype offset = 0;
    if (payload.size() < 12)
    {
        return false;
    }
    data.valid = payload.at(offset++) != 0;
    ++offset;
    if (!readLe(payload, offset, data.error_code) ||
        !readDoubleLe(payload, offset, data.internal_temperature_c))
    {
        return false;
    }
    for (TemperatureControllerChannelData& channel : data.channels)
    {
        quint16 outputMode = 0;
        quint16 maxOutputPercent = 0;
        quint32 kp = 0;
        quint32 ki = 0;
        quint32 kd = 0;
        if (!readDoubleLe(payload, offset, channel.target_temperature_c) ||
            !readDoubleLe(payload, offset, channel.measured_temperature_c) ||
            !readDoubleLe(payload, offset, channel.output_percent) ||
            !readDoubleLe(payload, offset, channel.output_current_a) ||
            !readLe(payload, offset, outputMode) ||
            offset + 2 > payload.size())
        {
            return false;
        }
        channel.output_enabled = payload.at(offset++) != 0;
        ++offset;
        if (!readLe(payload, offset, maxOutputPercent) ||
            !readLe(payload, offset, kp) ||
            !readLe(payload, offset, ki) ||
            !readLe(payload, offset, kd))
        {
            return false;
        }
        channel.output_mode = outputMode;
        channel.max_output_percent = maxOutputPercent;
        channel.kp = static_cast<int>(std::min<quint32>(kp, static_cast<quint32>(std::numeric_limits<int>::max())));
        channel.ki = static_cast<int>(std::min<quint32>(ki, static_cast<quint32>(std::numeric_limits<int>::max())));
        channel.kd = static_cast<int>(std::min<quint32>(kd, static_cast<quint32>(std::numeric_limits<int>::max())));
        const qsizetype sensorBlockOffset = offset;
        quint16 sensorModel = 0;
        quint32 ntcB = 0;
        quint32 ntcR0 = 0;
        quint32 ptR0 = 0;
        qint32 ptA = 0;
        qint32 ptB = 0;
        qint32 ptC = 0;
        if (readLe(payload, offset, sensorModel) &&
            readLe(payload, offset, ntcB) &&
            readLe(payload, offset, ntcR0) &&
            readLe(payload, offset, ptR0) &&
            readLe(payload, offset, ptA) &&
            readLe(payload, offset, ptB) &&
            readLe(payload, offset, ptC))
        {
            channel.sensor_model = sensorModel;
            channel.ntc_b = static_cast<int>(std::min<quint32>(ntcB, static_cast<quint32>(std::numeric_limits<int>::max())));
            channel.ntc_r0 = static_cast<int>(std::min<quint32>(ntcR0, static_cast<quint32>(std::numeric_limits<int>::max())));
            channel.pt_r0 = static_cast<int>(std::min<quint32>(ptR0, static_cast<quint32>(std::numeric_limits<int>::max())));
            channel.pt_a = ptA;
            channel.pt_b = ptB;
            channel.pt_c = ptC;
            bool polynomialOk = true;
            for (size_t i = 0; i < channel.polynomial_mantissas.size(); ++i)
            {
                qint64 mantissa = 0;
                qint16 exponent = 0;
                if (!readLe(payload, offset, mantissa) ||
                    !readLe(payload, offset, exponent))
                {
                    polynomialOk = false;
                    break;
                }
                channel.polynomial_mantissas[i] = mantissa;
                channel.polynomial_exponents[i] = exponent;
            }
            if (!polynomialOk)
            {
                offset = sensorBlockOffset;
            }
        }
        else
        {
            offset = sensorBlockOffset;
        }
    }
    quint16 controllerMode = 0;
    if (readLe(payload, offset, controllerMode))
    {
        data.controller_mode = controllerMode;
        for (TemperatureControllerChannelData& channel : data.channels)
        {
            quint16 autoPidMode = 0;
            if (!readLe(payload, offset, autoPidMode))
            {
                break;
            }
            channel.auto_pid_mode = autoPidMode;
        }
        quint16 deviceAddress = 0;
        quint16 rs485BaudIndex = 0;
        quint16 overtempOutputMode = 0;
        if (readLe(payload, offset, deviceAddress))
        {
            data.device_address = deviceAddress;
            if (readLe(payload, offset, rs485BaudIndex))
            {
                data.rs485_baud_index = rs485BaudIndex;
                if (readLe(payload, offset, overtempOutputMode))
                {
                    data.overtemp_output_mode = overtempOutputMode;
                }
            }
        }
    }
    return true;
}

QByteArray TelemetryCodec::serializeTemperatureControllerCommand(const TemperatureControllerCommand& command)
{
    QByteArray payload;
    payload.reserve(128);
    payload.append(static_cast<char>(command.channel));
    payload.append(command.output_enabled ? char(1) : char(0));
    appendLe<quint16>(payload, command.output_mode);
    appendDoubleLe(payload, command.target_temperature_c);
    appendLe<quint16>(payload, command.max_output_percent);
    appendLe<quint16>(payload, 0);
    appendLe<quint32>(payload, command.kp);
    appendLe<quint32>(payload, command.ki);
    appendLe<quint32>(payload, command.kd);
    appendLe<quint16>(payload, command.auto_pid_mode);
    appendLe<quint16>(payload, command.controller_mode);
    appendLe<quint16>(payload, command.device_address);
    appendLe<quint16>(payload, command.rs485_baud_index);
    appendLe<quint16>(payload, command.overtemp_output_mode);
    appendLe<quint16>(payload, command.sensor_model);
    appendLe<quint32>(payload, command.ntc_b);
    appendLe<quint32>(payload, command.ntc_r0);
    appendLe<quint32>(payload, command.pt_r0);
    appendLe<qint32>(payload, command.pt_a);
    appendLe<qint32>(payload, command.pt_b);
    appendLe<qint32>(payload, command.pt_c);
    for (size_t i = 0; i < command.polynomial_mantissas.size(); ++i)
    {
        appendLe<qint64>(payload, command.polynomial_mantissas[i]);
        appendLe<qint16>(payload, command.polynomial_exponents[i]);
    }
    return payload;
}

bool TelemetryCodec::parseTemperatureControllerCommand(const QByteArray& payload, TemperatureControllerCommand& command)
{
    command = TemperatureControllerCommand();
    qsizetype offset = 0;
    if (payload.size() < 28)
    {
        return false;
    }
    command.channel = static_cast<quint8>(payload.at(offset++));
    command.output_enabled = payload.at(offset++) != 0;
    quint16 reserved = 0;
    if (!readLe(payload, offset, command.output_mode) ||
        !readDoubleLe(payload, offset, command.target_temperature_c) ||
        !readLe(payload, offset, command.max_output_percent) ||
        !readLe(payload, offset, reserved) ||
        !readLe(payload, offset, command.kp) ||
        !readLe(payload, offset, command.ki) ||
        !readLe(payload, offset, command.kd))
    {
        return false;
    }
    quint16 autoPidMode = 0;
    quint16 controllerMode = 0;
    if (readLe(payload, offset, autoPidMode))
    {
        command.auto_pid_mode = autoPidMode;
        if (readLe(payload, offset, controllerMode))
        {
            command.controller_mode = controllerMode;
            quint16 deviceAddress = 0;
            quint16 rs485BaudIndex = 0;
            quint16 overtempOutputMode = 0;
            if (readLe(payload, offset, deviceAddress))
            {
                command.device_address = deviceAddress;
                if (readLe(payload, offset, rs485BaudIndex))
                {
                    command.rs485_baud_index = rs485BaudIndex;
                    if (readLe(payload, offset, overtempOutputMode))
                    {
                        command.overtemp_output_mode = overtempOutputMode;
                        quint16 sensorModel = 0;
                        quint32 ntcB = 0;
                        quint32 ntcR0 = 0;
                        quint32 ptR0 = 0;
                        qint32 ptA = 0;
                        qint32 ptB = 0;
                        qint32 ptC = 0;
                        if (readLe(payload, offset, sensorModel) &&
                            readLe(payload, offset, ntcB) &&
                            readLe(payload, offset, ntcR0) &&
                            readLe(payload, offset, ptR0) &&
                            readLe(payload, offset, ptA) &&
                            readLe(payload, offset, ptB) &&
                            readLe(payload, offset, ptC))
                        {
                            command.sensor_model = sensorModel;
                            command.ntc_b = ntcB;
                            command.ntc_r0 = ntcR0;
                            command.pt_r0 = ptR0;
                            command.pt_a = ptA;
                            command.pt_b = ptB;
                            command.pt_c = ptC;
                            for (size_t i = 0; i < command.polynomial_mantissas.size(); ++i)
                            {
                                qint64 mantissa = 0;
                                qint16 exponent = 0;
                                if (!readLe(payload, offset, mantissa) ||
                                    !readLe(payload, offset, exponent))
                                {
                                    break;
                                }
                                command.polynomial_mantissas[i] = mantissa;
                                command.polynomial_exponents[i] = exponent;
                            }
                        }
                    }
                }
            }
        }
    }
    return true;
}

}  // namespace VaporView
