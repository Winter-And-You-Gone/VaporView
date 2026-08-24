#include "TelemetryCodec.h"
#include "logging/BoundedLogRecord.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVector>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace VaporView
{
namespace
{
constexpr char kSof0 = static_cast<char>(0xAA);
constexpr char kSof1 = static_cast<char>(0x55);
constexpr int kSofSize = 2;
constexpr int kCrcSize = 2;
constexpr quint8 kAi8TemperatureStatusPayloadVersion = 1;

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
    case SkyDeviceId::Ai8TemperatureController:
        return QStringLiteral("ai8_temperature_controller");
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
    case CommandId::AutoDetectSerialPorts: return QStringLiteral("AutoDetectSerialPorts");
    case CommandId::CancelSerialPortDetection: return QStringLiteral("CancelSerialPortDetection");
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
    case CommandId::SetTemperatureOvertempUpper: return QStringLiteral("SetTemperatureOvertempUpper");
    case CommandId::SetTemperatureOvertempLower: return QStringLiteral("SetTemperatureOvertempLower");
    case CommandId::SetTemperatureSlope: return QStringLiteral("SetTemperatureSlope");
    case CommandId::SetTemperatureStartupDelay: return QStringLiteral("SetTemperatureStartupDelay");
    case CommandId::DeviceOperation: return QStringLiteral("DeviceOperation");
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
    case CommandErrorCode::SerialPortDetectionInProgress:
        return english ? QStringLiteral("Serial-port detection is already running") : QStringLiteral("串口自动识别正在进行");
    case CommandErrorCode::SerialPortDetectionNotRunning:
        return english ? QStringLiteral("Serial-port detection is not running") : QStringLiteral("当前没有正在运行的串口自动识别任务");
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
    case 7:
        id = SkyDeviceId::Ai8TemperatureController;
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

QByteArray TelemetryCodec::serializeLogRecord(const LogRecord& record)
{
    QByteArray line = LoggingInternal::serializeBoundedLogRecord(record);
    if (line.endsWith('\n'))
    {
        line.chop(1);
    }
    return line;
}

bool TelemetryCodec::parseLogRecord(const QByteArray& payload, LogRecord& record)
{
    if (payload.size() > LogRecordLimits::kMaxSerializedRecordBytes)
    {
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (!document.isObject() || parseError.error != QJsonParseError::NoError)
    {
        return false;
    }
    const QJsonObject object = document.object();
    if (!object.contains(QStringLiteral("message")) || !object.value(QStringLiteral("message")).isString())
    {
        return false;
    }
    record.schema_version = object.value(QStringLiteral("schema_version")).toInt(1);
    record.timestamp_utc = object.value(QStringLiteral("timestamp_utc")).toString();
    record.timestamp_us = static_cast<quint64>(object.value(QStringLiteral("timestamp_us")).toVariant().toULongLong());
    record.level = logLevelFromName(object.value(QStringLiteral("level")).toString());
    record.source = object.value(QStringLiteral("source")).toString();
    record.category = object.value(QStringLiteral("category")).toString();
    record.process_id = static_cast<quint64>(object.value(QStringLiteral("process_id")).toVariant().toULongLong());
    record.thread_id = static_cast<quint64>(object.value(QStringLiteral("thread_id")).toVariant().toULongLong());
    record.sequence = static_cast<quint64>(object.value(QStringLiteral("sequence")).toVariant().toULongLong());
    record.correlation_id = object.value(QStringLiteral("correlation_id")).toString();
    record.session_id = object.value(QStringLiteral("session_id")).toString();
    record.message = object.value(QStringLiteral("message")).toString();
    if (object.value(QStringLiteral("fields")).isObject())
    {
        record.fields = object.value(QStringLiteral("fields")).toObject().toVariantMap();
    }
    record = LoggingInternal::boundLogRecord(std::move(record));
    return true;
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
    payload.reserve(220);
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
    appendFloatLe(payload, data.status_packet_rate_hz);
    appendFloatLe(payload, data.euler_orien_packet_rate_hz);
    appendFloatLe(payload, data.quat_orien_packet_rate_hz);
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
    if (payload.size() - offset >= static_cast<qsizetype>(sizeof(float) * 3))
    {
        if (!(readFloatLe(payload, offset, data.status_packet_rate_hz) &&
              readFloatLe(payload, offset, data.euler_orien_packet_rate_hz) &&
              readFloatLe(payload, offset, data.quat_orien_packet_rate_hz)))
        {
            return false;
        }
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
    appendLe<quint64>(payload, status.raw_navigation_record_count);
    appendLe<quint64>(payload, status.raw_pressure_record_count);
    appendLe<quint64>(payload, status.raw_temperature_humidity_record_count);
    appendLe<quint64>(payload, status.raw_distance_record_count);
    appendLe<quint64>(payload, status.raw_waveform_record_count);
    appendLe<quint64>(payload, status.rtcm_correction_bytes_received);
    appendLe<quint64>(payload, status.rtcm_correction_chunks_received);
    appendLe<quint64>(payload, status.rtcm_correction_dropped_bytes);
    appendLe<quint64>(payload, status.rtcm_correction_dropped_chunks);
    appendLe<quint64>(payload, status.rtcm_correction_last_receive_time_us);
    appendLe<quint64>(payload, status.raw_laser_temperature_controller_record_count);
    appendLe<quint64>(payload, status.raw_system_temperature_controller_record_count);
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
           readOptionalU64(status.raw_navigation_record_count) &&
           readOptionalU64(status.raw_pressure_record_count) &&
           readOptionalU64(status.raw_temperature_humidity_record_count) &&
           readOptionalU64(status.raw_distance_record_count) &&
           readOptionalU64(status.raw_waveform_record_count) &&
           readOptionalU64(status.rtcm_correction_bytes_received) &&
           readOptionalU64(status.rtcm_correction_chunks_received) &&
           readOptionalU64(status.rtcm_correction_dropped_bytes) &&
           readOptionalU64(status.rtcm_correction_dropped_chunks) &&
           readOptionalU64(status.rtcm_correction_last_receive_time_us) &&
           readOptionalU64(status.raw_laser_temperature_controller_record_count) &&
           readOptionalU64(status.raw_system_temperature_controller_record_count);
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
    for (const TemperatureControllerChannelData& channel : data.channels)
    {
        appendDoubleLe(payload, channel.overtemp_upper_c);
        appendDoubleLe(payload, channel.overtemp_lower_c);
        appendDoubleLe(payload, channel.temperature_slope_c_per_s);
        appendLe<quint16>(payload, static_cast<quint16>(std::clamp(channel.startup_delay_s, 0, 65535)));
        appendDoubleLe(payload, channel.sensor_resistance_ohm);
    }
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
    for (TemperatureControllerChannelData& channel : data.channels)
    {
        quint16 startupDelay = 0;
        if (!readDoubleLe(payload, offset, channel.overtemp_upper_c) ||
            !readDoubleLe(payload, offset, channel.overtemp_lower_c) ||
            !readDoubleLe(payload, offset, channel.temperature_slope_c_per_s) ||
            !readLe(payload, offset, startupDelay) ||
            !readDoubleLe(payload, offset, channel.sensor_resistance_ohm))
        {
            break;
        }
        channel.startup_delay_s = startupDelay;
    }
    return true;
}

QJsonObject ai8PageDataToJson(const Ai8TemperatureControllerProtocol::PageData& data)
{
    const auto &c = data.channel;
    const auto &i = data.input;
    const auto &o = data.output;
    const auto &g = data.global;
    QJsonObject channel{{QStringLiteral("setpoint_c"), c.setpointC},
                        {QStringLiteral("measured_c"), c.measuredC},
                        {QStringLiteral("proportional_band"), c.proportionalBand},
                        {QStringLiteral("integral_time_s"), c.integralTimeS},
                        {QStringLiteral("derivative_time_s"), c.derivativeTimeS},
                        {QStringLiteral("channel_input_group"), c.channelInputGroup},
                        {QStringLiteral("correction_entry"), c.correctionEntry},
                        {QStringLiteral("measurement_offset"), c.measurementOffset},
                        {QStringLiteral("channel_output_group_raw"), c.channelOutputGroupRaw},
                        {QStringLiteral("program_number"), c.programNumber},
                        {QStringLiteral("work_mode"), c.workMode},
                        {QStringLiteral("manual_output_percent"), c.manualOutputPercent},
                        {QStringLiteral("high_alarm_c"), c.highAlarmC},
                        {QStringLiteral("low_alarm_c"), c.lowAlarmC},
                        {QStringLiteral("displayed_setpoint_c"), c.displayedSetpointC},
                        {QStringLiteral("alarm_status_raw"), c.alarmStatusRaw},
                        {QStringLiteral("alarm_status_valid"), c.alarmStatusValid}};
    QJsonObject input{{QStringLiteral("input_type"), i.inputType},
                      {QStringLiteral("scale_low"), i.scaleLow},
                      {QStringLiteral("scale_high"), i.scaleHigh},
                      {QStringLiteral("filter"), i.filter},
                      {QStringLiteral("channel_input_group"), i.channelInputGroup},
                      {QStringLiteral("measurement_offset"), i.measurementOffset},
                      {QStringLiteral("correction_entry"), i.correctionEntry}};
    QJsonObject output{{QStringLiteral("control_action"), o.controlAction},
                       {QStringLiteral("deviation_high_alarm"), o.deviationHighAlarm},
                       {QStringLiteral("deviation_low_alarm"), o.deviationLowAlarm},
                       {QStringLiteral("hysteresis"), o.hysteresis},
                       {QStringLiteral("output_low_percent"), o.outputLowPercent},
                       {QStringLiteral("output_high_percent"), o.outputHighPercent},
                       {QStringLiteral("output_high_threshold"), o.outputHighThreshold},
                       {QStringLiteral("rise_slope"), o.riseSlope},
                       {QStringLiteral("fall_slope"), o.fallSlope},
                       {QStringLiteral("setpoint_low_limit"), o.setpointLowLimit},
                       {QStringLiteral("setpoint_high_limit"), o.setpointHighLimit},
                       {QStringLiteral("alarm_reset_flags"), o.alarmResetFlags}};
    QJsonObject global{{QStringLiteral("address"), g.address},
                       {QStringLiteral("baud_rate"), g.baudRate},
                       {QStringLiteral("local_input_channel_count"), g.localInputChannelCount},
                       {QStringLiteral("expansion_input_channel_count"), g.expansionInputChannelCount},
                       {QStringLiteral("control_channel_count"), g.controlChannelCount},
                       {QStringLiteral("control_cycle_s"), g.controlCycleS},
                       {QStringLiteral("run_state_raw"), g.runStateRaw},
                       {QStringLiteral("run_state_is_documented"), g.runStateIsDocumented},
                       {QStringLiteral("run_state_write_requested"), g.runStateWriteRequested},
                       {QStringLiteral("run_state_write_value"), g.runStateWriteValue},
                       {QStringLiteral("common_alarm_output"), g.commonAlarmOutput},
                       {QStringLiteral("independent_alarm_channel_count"), g.independentAlarmChannelCount},
                       {QStringLiteral("independent_alarm_mask"), g.independentAlarmMask},
                       {QStringLiteral("alarm_function_a"), g.alarmFunctionA},
                       {QStringLiteral("alarm_function_b"), g.alarmFunctionB},
                       {QStringLiteral("parameter_lock"), g.parameterLock},
                       {QStringLiteral("sample_mode"), g.sampleMode},
                       {QStringLiteral("decimal_point"), g.decimalPoint},
                       {QStringLiteral("parity_flags"), g.parityFlags},
                       {QStringLiteral("alarm_polarity"), g.alarmPolarity},
                       {QStringLiteral("extra_hysteresis"), g.extraHysteresis},
                       {QStringLiteral("main_status_raw"), g.mainStatusRaw},
                       {QStringLiteral("model_feature"), g.modelFeature},
                       {QStringLiteral("serial_number"), static_cast<qint64>(g.serialNumber)},
                       {QStringLiteral("output_start_channel"), g.outputStartChannel},
                       {QStringLiteral("high_resolution_filter"), g.highResolutionFilter},
                       {QStringLiteral("aif1"), g.aif1},
                       {QStringLiteral("aif2"), g.aif2},
                       {QStringLiteral("p1fa_aif3"), g.p1faAif3},
                       {QStringLiteral("difa"), g.difa},
                       {QStringLiteral("spsr"), g.spsr},
                       {QStringLiteral("at_function"), g.atFunction},
                       {QStringLiteral("aifl_p1pr"), g.aiflP1pr},
                       {QStringLiteral("p1ti_opsn"), g.p1tiOpsn}};
    return QJsonObject{{QStringLiteral("page"), static_cast<int>(data.page)},
                       {QStringLiteral("selection"), QJsonObject{
                           {QStringLiteral("channel"), data.selection.channel},
                           {QStringLiteral("input_group"), data.selection.inputGroup},
                           {QStringLiteral("output_group"), data.selection.outputGroup}}},
                       {QStringLiteral("channel"), channel},
                       {QStringLiteral("input"), input},
                       {QStringLiteral("output"), output},
                       {QStringLiteral("global"), global}};
}

template <typename T>
void readJsonInt(const QJsonObject& object, const char *key, T& target)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (value.isDouble())
    {
        target = static_cast<T>(value.toInt());
    }
}

void readJsonDouble(const QJsonObject& object, const char *key, double& target)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (value.isDouble())
    {
        target = value.toDouble();
    }
}

void readJsonBool(const QJsonObject& object, const char *key, bool& target)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (value.isBool())
    {
        target = value.toBool();
    }
}

bool ai8PageDataFromJson(const QJsonObject& root, Ai8TemperatureControllerProtocol::PageData& data)
{
    const int page = root.value(QStringLiteral("page")).toInt(-1);
    if (page < 0 || page > static_cast<int>(Ai8TemperatureControllerProtocol::Page::Global))
    {
        return false;
    }
    data = Ai8TemperatureControllerProtocol::PageData{};
    data.page = static_cast<Ai8TemperatureControllerProtocol::Page>(page);
    const QJsonObject selection = root.value(QStringLiteral("selection")).toObject();
    readJsonInt(selection, "channel", data.selection.channel);
    readJsonInt(selection, "input_group", data.selection.inputGroup);
    readJsonInt(selection, "output_group", data.selection.outputGroup);
    if (data.selection.channel < 1 || data.selection.channel > Ai8TemperatureControllerProtocol::kChannelCount ||
        data.selection.inputGroup < 1 || data.selection.inputGroup > Ai8TemperatureControllerProtocol::kParameterGroupCount ||
        data.selection.outputGroup < 1 || data.selection.outputGroup > Ai8TemperatureControllerProtocol::kParameterGroupCount)
    {
        return false;
    }

    const QJsonObject channel = root.value(QStringLiteral("channel")).toObject();
    readJsonDouble(channel, "setpoint_c", data.channel.setpointC);
    readJsonDouble(channel, "measured_c", data.channel.measuredC);
    readJsonDouble(channel, "proportional_band", data.channel.proportionalBand);
    readJsonDouble(channel, "integral_time_s", data.channel.integralTimeS);
    readJsonDouble(channel, "derivative_time_s", data.channel.derivativeTimeS);
    readJsonInt(channel, "channel_input_group", data.channel.channelInputGroup);
    readJsonInt(channel, "correction_entry", data.channel.correctionEntry);
    readJsonDouble(channel, "measurement_offset", data.channel.measurementOffset);
    readJsonInt(channel, "channel_output_group_raw", data.channel.channelOutputGroupRaw);
    readJsonInt(channel, "program_number", data.channel.programNumber);
    readJsonInt(channel, "work_mode", data.channel.workMode);
    readJsonDouble(channel, "manual_output_percent", data.channel.manualOutputPercent);
    readJsonDouble(channel, "high_alarm_c", data.channel.highAlarmC);
    readJsonDouble(channel, "low_alarm_c", data.channel.lowAlarmC);
    readJsonDouble(channel, "displayed_setpoint_c", data.channel.displayedSetpointC);
    readJsonInt(channel, "alarm_status_raw", data.channel.alarmStatusRaw);
    readJsonBool(channel, "alarm_status_valid", data.channel.alarmStatusValid);

    const QJsonObject input = root.value(QStringLiteral("input")).toObject();
    readJsonInt(input, "input_type", data.input.inputType);
    readJsonDouble(input, "scale_low", data.input.scaleLow);
    readJsonDouble(input, "scale_high", data.input.scaleHigh);
    readJsonInt(input, "filter", data.input.filter);
    readJsonInt(input, "channel_input_group", data.input.channelInputGroup);
    readJsonDouble(input, "measurement_offset", data.input.measurementOffset);
    readJsonInt(input, "correction_entry", data.input.correctionEntry);

    const QJsonObject output = root.value(QStringLiteral("output")).toObject();
    readJsonInt(output, "control_action", data.output.controlAction);
    readJsonDouble(output, "deviation_high_alarm", data.output.deviationHighAlarm);
    readJsonDouble(output, "deviation_low_alarm", data.output.deviationLowAlarm);
    readJsonDouble(output, "hysteresis", data.output.hysteresis);
    readJsonInt(output, "output_low_percent", data.output.outputLowPercent);
    readJsonInt(output, "output_high_percent", data.output.outputHighPercent);
    readJsonDouble(output, "output_high_threshold", data.output.outputHighThreshold);
    readJsonDouble(output, "rise_slope", data.output.riseSlope);
    readJsonDouble(output, "fall_slope", data.output.fallSlope);
    readJsonDouble(output, "setpoint_low_limit", data.output.setpointLowLimit);
    readJsonDouble(output, "setpoint_high_limit", data.output.setpointHighLimit);
    readJsonInt(output, "alarm_reset_flags", data.output.alarmResetFlags);

    const QJsonObject global = root.value(QStringLiteral("global")).toObject();
    readJsonInt(global, "address", data.global.address);
    readJsonInt(global, "baud_rate", data.global.baudRate);
    readJsonInt(global, "local_input_channel_count", data.global.localInputChannelCount);
    readJsonInt(global, "expansion_input_channel_count", data.global.expansionInputChannelCount);
    readJsonInt(global, "control_channel_count", data.global.controlChannelCount);
    readJsonDouble(global, "control_cycle_s", data.global.controlCycleS);
    readJsonInt(global, "run_state_raw", data.global.runStateRaw);
    readJsonBool(global, "run_state_is_documented", data.global.runStateIsDocumented);
    readJsonBool(global, "run_state_write_requested", data.global.runStateWriteRequested);
    readJsonInt(global, "run_state_write_value", data.global.runStateWriteValue);
    readJsonInt(global, "common_alarm_output", data.global.commonAlarmOutput);
    readJsonInt(global, "independent_alarm_channel_count", data.global.independentAlarmChannelCount);
    readJsonInt(global, "independent_alarm_mask", data.global.independentAlarmMask);
    readJsonInt(global, "alarm_function_a", data.global.alarmFunctionA);
    readJsonInt(global, "alarm_function_b", data.global.alarmFunctionB);
    readJsonInt(global, "parameter_lock", data.global.parameterLock);
    readJsonInt(global, "sample_mode", data.global.sampleMode);
    readJsonInt(global, "decimal_point", data.global.decimalPoint);
    readJsonInt(global, "parity_flags", data.global.parityFlags);
    readJsonInt(global, "alarm_polarity", data.global.alarmPolarity);
    readJsonDouble(global, "extra_hysteresis", data.global.extraHysteresis);
    readJsonInt(global, "main_status_raw", data.global.mainStatusRaw);
    readJsonInt(global, "model_feature", data.global.modelFeature);
    readJsonInt(global, "serial_number", data.global.serialNumber);
    readJsonInt(global, "output_start_channel", data.global.outputStartChannel);
    readJsonInt(global, "high_resolution_filter", data.global.highResolutionFilter);
    readJsonInt(global, "aif1", data.global.aif1);
    readJsonInt(global, "aif2", data.global.aif2);
    readJsonInt(global, "p1fa_aif3", data.global.p1faAif3);
    readJsonInt(global, "difa", data.global.difa);
    readJsonInt(global, "spsr", data.global.spsr);
    readJsonInt(global, "at_function", data.global.atFunction);
    readJsonInt(global, "aifl_p1pr", data.global.aiflP1pr);
    readJsonInt(global, "p1ti_opsn", data.global.p1tiOpsn);
    return true;
}

bool validDeviceOperation(DeviceOperation operation)
{
    return operation == DeviceOperation::ReadParameters ||
           operation == DeviceOperation::WriteParameters ||
           operation == DeviceOperation::FactoryReset ||
           operation == DeviceOperation::ConfigureEpsilonPacketRates ||
           operation == DeviceOperation::ConfigureEpsilonMainAntennaLeverArm ||
           operation == DeviceOperation::ConfigureEpsilonRtcmInput;
}

QByteArray TelemetryCodec::serializeAi8TemperatureControllerStatus(
    const Ai8TemperatureControllerProtocol::LiveData& data)
{
    QByteArray payload;
    payload.reserve(86);
    payload.append(static_cast<char>(kAi8TemperatureStatusPayloadVersion));
    quint8 flags = 0;
    if (data.valid) flags |= 0x01;
    if (data.controlStatesValid) flags |= 0x02;
    if (data.alarmStatusValid) flags |= 0x04;
    if (data.mainStatusValid) flags |= 0x08;
    payload.append(static_cast<char>(flags));
    appendLe<quint16>(payload, 0);
    for (double value : data.measuredC)
    {
        appendDoubleLe(payload, value);
    }
    for (Ai8TemperatureControllerProtocol::ChannelControlState state : data.controlStates)
    {
        payload.append(static_cast<char>(state));
    }
    for (quint16 value : data.alarmStatusRegisters)
    {
        appendLe<quint16>(payload, value);
    }
    appendLe<quint16>(payload, data.mainStatusRaw);
    return payload;
}

bool TelemetryCodec::parseAi8TemperatureControllerStatus(
    const QByteArray& payload,
    Ai8TemperatureControllerProtocol::LiveData& data)
{
    data = Ai8TemperatureControllerProtocol::LiveData();
    qsizetype offset = 0;
    constexpr qsizetype kExpectedPayloadSize =
        1 + 1 + 2 +
        Ai8TemperatureControllerProtocol::kChannelCount * static_cast<int>(sizeof(double)) +
        Ai8TemperatureControllerProtocol::kChannelCount +
        Ai8TemperatureControllerProtocol::kAlarmStatusRegisterCount * static_cast<int>(sizeof(quint16)) +
        static_cast<int>(sizeof(quint16));
    if (payload.size() != kExpectedPayloadSize)
    {
        return false;
    }
    const quint8 version = static_cast<quint8>(payload.at(offset++));
    if (version != kAi8TemperatureStatusPayloadVersion)
    {
        return false;
    }
    const quint8 flags = static_cast<quint8>(payload.at(offset++));
    quint16 reserved = 0;
    if (!readLe(payload, offset, reserved))
    {
        return false;
    }
    data.valid = (flags & 0x01) != 0;
    data.controlStatesValid = (flags & 0x02) != 0;
    data.alarmStatusValid = (flags & 0x04) != 0;
    data.mainStatusValid = (flags & 0x08) != 0;
    for (double& value : data.measuredC)
    {
        if (!readDoubleLe(payload, offset, value))
        {
            return false;
        }
    }
    for (Ai8TemperatureControllerProtocol::ChannelControlState& state : data.controlStates)
    {
        const quint8 raw = static_cast<quint8>(payload.at(offset++));
        if (raw > static_cast<quint8>(Ai8TemperatureControllerProtocol::ChannelControlState::Stopped))
        {
            return false;
        }
        state = static_cast<Ai8TemperatureControllerProtocol::ChannelControlState>(raw);
    }
    for (quint16& value : data.alarmStatusRegisters)
    {
        if (!readLe(payload, offset, value))
        {
            return false;
        }
    }
    return readLe(payload, offset, data.mainStatusRaw) && offset == payload.size();
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
    appendDoubleLe(payload, command.overtemp_upper_c);
    appendDoubleLe(payload, command.overtemp_lower_c);
    appendDoubleLe(payload, command.temperature_slope_c_per_s);
    appendLe<quint16>(payload, command.startup_delay_s);
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
                            quint16 startupDelay = 0;
                            if (readDoubleLe(payload, offset, command.overtemp_upper_c) &&
                                readDoubleLe(payload, offset, command.overtemp_lower_c) &&
                                readDoubleLe(payload, offset, command.temperature_slope_c_per_s) &&
                                readLe(payload, offset, startupDelay))
                            {
                                command.startup_delay_s = startupDelay;
                            }
                        }
                    }
                }
            }
        }
    }
    return true;
}

QByteArray TelemetryCodec::serializeAi8PageData(
    const Ai8TemperatureControllerProtocol::PageData& data)
{
    return QJsonDocument(ai8PageDataToJson(data)).toJson(QJsonDocument::Compact);
}

bool TelemetryCodec::parseAi8PageData(
    const QByteArray& payload,
    Ai8TemperatureControllerProtocol::PageData& data)
{
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    return document.isObject() && ai8PageDataFromJson(document.object(), data);
}

namespace
{

bool readRequiredJsonInt(const QJsonObject& object, const QString& key, int& target)
{
    const QJsonValue value = object.value(key);
    if (!value.isDouble())
    {
        return false;
    }
    const double numeric = value.toDouble();
    if (!std::isfinite(numeric) || std::floor(numeric) != numeric ||
        numeric < static_cast<double>(std::numeric_limits<int>::min()) ||
        numeric > static_cast<double>(std::numeric_limits<int>::max()))
    {
        return false;
    }
    target = static_cast<int>(numeric);
    return true;
}

bool readRequiredJsonDouble(const QJsonObject& object, const QString& key, double& target)
{
    const QJsonValue value = object.value(key);
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
    {
        return false;
    }
    target = value.toDouble();
    return true;
}

}  // namespace

QByteArray TelemetryCodec::serializeEpsilonPacketRatesOperation(
    const EpsilonPacketRatesOperation& operation)
{
    QJsonArray rates;
    for (const auto& entry : operation.packet_rates)
    {
        rates.append(QJsonObject{
            {QStringLiteral("packet_id"), static_cast<int>(entry.first)},
            {QStringLiteral("rate_hz"), entry.second},
        });
    }
    const QJsonObject object{
        {QStringLiteral("version"), 1},
        {QStringLiteral("output_rate_hz"), operation.output_rate_hz},
        {QStringLiteral("callback_rate_hz"), operation.callback_rate_hz},
        {QStringLiteral("packet_rate_signature"), operation.packet_rate_signature},
        {QStringLiteral("packet_rates"), rates},
    };
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool TelemetryCodec::parseEpsilonPacketRatesOperation(
    const QByteArray& payload,
    EpsilonPacketRatesOperation& operation)
{
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject())
    {
        return false;
    }
    const QJsonObject object = document.object();
    int version = 0;
    int outputRate = 0;
    int callbackRate = 0;
    if (!readRequiredJsonInt(object, QStringLiteral("version"), version) ||
        version != 1 ||
        !readRequiredJsonInt(object, QStringLiteral("output_rate_hz"), outputRate) ||
        !readRequiredJsonInt(object, QStringLiteral("callback_rate_hz"), callbackRate) ||
        outputRate <= 0 ||
        callbackRate <= 0)
    {
        return false;
    }
    const QJsonValue ratesValue = object.value(QStringLiteral("packet_rates"));
    if (!ratesValue.isArray())
    {
        return false;
    }
    std::map<uint8_t, int> packetRates;
    for (const QJsonValue& itemValue : ratesValue.toArray())
    {
        if (!itemValue.isObject())
        {
            return false;
        }
        const QJsonObject item = itemValue.toObject();
        int packetId = 0;
        int rateHz = 0;
        if (!readRequiredJsonInt(item, QStringLiteral("packet_id"), packetId) ||
            !readRequiredJsonInt(item, QStringLiteral("rate_hz"), rateHz) ||
            packetId < 0 ||
            packetId > 255 ||
            rateHz < 0)
        {
            return false;
        }
        packetRates[static_cast<uint8_t>(packetId)] = rateHz;
    }
    if (packetRates.empty())
    {
        return false;
    }
    operation = EpsilonPacketRatesOperation{};
    operation.output_rate_hz = outputRate;
    operation.callback_rate_hz = callbackRate;
    operation.packet_rates = std::move(packetRates);
    operation.packet_rate_signature =
        object.value(QStringLiteral("packet_rate_signature")).toString();
    return true;
}

QByteArray TelemetryCodec::serializeEpsilonMainAntennaLeverArmOperation(
    const EpsilonMainAntennaLeverArmOperation& operation)
{
    const QJsonObject object{
        {QStringLiteral("version"), 1},
        {QStringLiteral("x_m"), operation.x_m},
        {QStringLiteral("y_m"), operation.y_m},
        {QStringLiteral("z_m"), operation.z_m},
    };
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool TelemetryCodec::parseEpsilonMainAntennaLeverArmOperation(
    const QByteArray& payload,
    EpsilonMainAntennaLeverArmOperation& operation)
{
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject())
    {
        return false;
    }
    const QJsonObject object = document.object();
    int version = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    if (!readRequiredJsonInt(object, QStringLiteral("version"), version) ||
        version != 1 ||
        !readRequiredJsonDouble(object, QStringLiteral("x_m"), x) ||
        !readRequiredJsonDouble(object, QStringLiteral("y_m"), y) ||
        !readRequiredJsonDouble(object, QStringLiteral("z_m"), z) ||
        std::abs(x) > 100.0 ||
        std::abs(y) > 100.0 ||
        std::abs(z) > 100.0)
    {
        return false;
    }
    operation = EpsilonMainAntennaLeverArmOperation{x, y, z};
    return true;
}

QByteArray TelemetryCodec::serializeEpsilonRtcmInputOperation(
    const EpsilonRtcmInputOperation& operation)
{
    const QJsonObject object{
        {QStringLiteral("version"), 1},
        {QStringLiteral("device_port_index"), operation.device_port_index},
        {QStringLiteral("forward_port"), operation.forward_port},
        {QStringLiteral("forward_baud"), operation.forward_baud},
    };
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool TelemetryCodec::parseEpsilonRtcmInputOperation(
    const QByteArray& payload,
    EpsilonRtcmInputOperation& operation)
{
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject())
    {
        return false;
    }
    const QJsonObject object = document.object();
    int version = 0;
    int devicePortIndex = 0;
    int forwardBaud = 0;
    if (!readRequiredJsonInt(object, QStringLiteral("version"), version) ||
        version != 1 ||
        !readRequiredJsonInt(object, QStringLiteral("device_port_index"), devicePortIndex) ||
        !readRequiredJsonInt(object, QStringLiteral("forward_baud"), forwardBaud) ||
        devicePortIndex < 2 ||
        devicePortIndex > 5 ||
        forwardBaud <= 0)
    {
        return false;
    }
    operation = EpsilonRtcmInputOperation{};
    operation.device_port_index = devicePortIndex;
    operation.forward_port = object.value(QStringLiteral("forward_port")).toString().trimmed();
    operation.forward_baud = forwardBaud;
    return true;
}

QByteArray TelemetryCodec::serializeRtcmCorrectionData(const QByteArray& data)
{
    QByteArray payload;
    appendLe<quint32>(payload, static_cast<quint32>(data.size()));
    payload.append(data);
    return payload;
}

bool TelemetryCodec::parseRtcmCorrectionData(const QByteArray& payload, QByteArray& data)
{
    qsizetype offset = 0;
    quint32 size = 0;
    if (!readLe(payload, offset, size) ||
        size == 0 ||
        size > 4096 ||
        offset + static_cast<qsizetype>(size) != payload.size())
    {
        return false;
    }
    data = payload.mid(offset, static_cast<int>(size));
    return true;
}

QByteArray TelemetryCodec::serializeDeviceOperationRequest(const DeviceOperationRequest& request)
{
    QByteArray payload;
    appendLe<quint32>(payload, request.request_id);
    payload.append(static_cast<char>(request.device_id));
    payload.append(static_cast<char>(request.operation));
    appendLe<quint16>(payload, 0);
    appendLe<quint32>(payload, static_cast<quint32>(request.payload.size()));
    payload.append(request.payload);
    return payload;
}

bool TelemetryCodec::parseDeviceOperationRequest(const QByteArray& payload,
                                                 DeviceOperationRequest& request)
{
    qsizetype offset = 0;
    quint8 deviceValue = 0;
    quint8 operationValue = 0;
    quint16 reserved = 0;
    quint32 payloadSize = 0;
    if (!readLe(payload, offset, request.request_id) ||
        offset + 2 > payload.size())
    {
        return false;
    }
    deviceValue = static_cast<quint8>(payload.at(offset++));
    operationValue = static_cast<quint8>(payload.at(offset++));
    if (!readLe(payload, offset, reserved) || !readLe(payload, offset, payloadSize) ||
        payloadSize > static_cast<quint32>(payload.size() - offset) ||
        offset + static_cast<qsizetype>(payloadSize) != payload.size() ||
        !skyDeviceIdFromValue(deviceValue, request.device_id) ||
        !validDeviceOperation(static_cast<DeviceOperation>(operationValue)))
    {
        return false;
    }
    Q_UNUSED(reserved);
    request.operation = static_cast<DeviceOperation>(operationValue);
    request.payload = payload.mid(offset, static_cast<int>(payloadSize));
    return true;
}

QByteArray TelemetryCodec::serializeDeviceOperationResponse(const DeviceOperationResponse& response)
{
    const QByteArray message = response.error_message.toUtf8();
    const int messageSize = std::min<int>(static_cast<int>(message.size()), 65535);
    QByteArray payload;
    appendLe<quint32>(payload, response.request_id);
    payload.append(static_cast<char>(response.device_id));
    payload.append(static_cast<char>(response.operation));
    appendLe<quint16>(payload, 0);
    appendLe<quint32>(payload, static_cast<quint32>(response.error_code));
    appendLe<quint16>(payload, static_cast<quint16>(messageSize));
    payload.append(message.constData(), messageSize);
    appendLe<quint32>(payload, static_cast<quint32>(response.payload.size()));
    payload.append(response.payload);
    return payload;
}

bool TelemetryCodec::parseDeviceOperationResponse(const QByteArray& payload,
                                                  DeviceOperationResponse& response)
{
    qsizetype offset = 0;
    quint8 deviceValue = 0;
    quint8 operationValue = 0;
    quint16 reserved = 0;
    quint32 errorValue = 0;
    quint16 messageSize = 0;
    quint32 payloadSize = 0;
    if (!readLe(payload, offset, response.request_id) || offset + 2 > payload.size())
    {
        return false;
    }
    deviceValue = static_cast<quint8>(payload.at(offset++));
    operationValue = static_cast<quint8>(payload.at(offset++));
    if (!readLe(payload, offset, reserved) ||
        !readLe(payload, offset, errorValue) ||
        !readLe(payload, offset, messageSize) ||
        offset + static_cast<qsizetype>(messageSize) > payload.size())
    {
        return false;
    }
    response.error_message = QString::fromUtf8(payload.constData() + offset, messageSize);
    offset += messageSize;
    if (!readLe(payload, offset, payloadSize) ||
        payloadSize > static_cast<quint32>(payload.size() - offset) ||
        offset + static_cast<qsizetype>(payloadSize) != payload.size() ||
        !skyDeviceIdFromValue(deviceValue, response.device_id) ||
        !validDeviceOperation(static_cast<DeviceOperation>(operationValue)))
    {
        return false;
    }
    Q_UNUSED(reserved);
    response.operation = static_cast<DeviceOperation>(operationValue);
    response.error_code = static_cast<CommandErrorCode>(errorValue);
    response.payload = payload.mid(offset, static_cast<int>(payloadSize));
    return true;
}

}  // namespace VaporView
