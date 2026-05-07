#ifndef VaporView_TELEMETRY_TYPES_H_
#define VaporView_TELEMETRY_TYPES_H_

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace VaporView
{

enum class AppMode : quint8
{
    Ground = 0,
    Sky = 1,
};

enum class GroundSource : quint8
{
    Local = 0,
    RemoteSky = 1,
};

enum class MsgType : quint8
{
    TelemetryBasic = 0x01,
    WaveformDownsampled = 0x02,
    WaveformFeature = 0x03,
    TelemetryStatus = 0x04,
    SkyConfig = 0x05,
    SkyConfigApplyResult = 0x06,
    Command = 0x10,
    CommandAck = 0x11,
    Heartbeat = 0x20,
    Error = 0x7F,
};

enum class CommandId : quint16
{
    StartRecording = 1,
    PauseRecording = 2,
    StopRecording = 3,
    SetTelemetryRate = 4,
    SetWaveformRate = 5,
    SetFeatureRate = 6,
    EnableWaveformStreaming = 7,
    DisableWaveformStreaming = 8,
    RequestOneWaveform = 9,
    RequestStatus = 10,
    RebootDevice = 11,
    QueryDeviceStatus = 20,
    ConnectDevice = 21,
    DisconnectDevice = 22,
    ReconnectDevice = 23,
    ConnectAllDevices = 24,
    DisconnectAllDevices = 25,
    ReconnectAllDevices = 26,
    GetSkyConfig = 30,
    SetSkyConfig = 31,
    SaveSkyConfig = 32,
    ReloadSkyConfig = 33,
};

enum class SkyDeviceId : quint8
{
    Epsilon = 1,
    Ptb = 2,
    Hmp = 3,
    Lidar = 4,
    WaveTcp = 5,
    All = 255,
};

enum class DeviceState : quint8
{
    Disabled = 0,
    Disconnected = 1,
    Connecting = 2,
    Connected = 3,
    Error = 4,
    Reconnecting = 5,
};

enum class CommandErrorCode : quint32
{
    Ok = 0,
    UnknownCommand = 1,
    InvalidPayload = 2,
    InvalidDeviceId = 3,
    DeviceAlreadyConnected = 4,
    DeviceNotConnected = 5,
    DeviceConnectFailed = 6,
    DeviceDisconnectFailed = 7,
    DeviceReconnectFailed = 8,
    ConfigInvalid = 9,
    ConfigApplyFailed = 10,
    ConfigSaveFailed = 11,
    RecordingAlreadyStarted = 12,
    RecordingNotStarted = 13,
    InternalError = 100,
};

struct TelemetryFrame
{
    quint8 version = 1;
    MsgType type = MsgType::Error;
    quint8 flags = 0;
    quint16 seq = 0;
    quint64 time_us = 0;
    QByteArray payload;
};

enum TelemetryBasicValidityFlag : quint32
{
    BasicHasEpsilonTime = 1u << 0,
    BasicHasPosition = 1u << 1,
    BasicHasEcef = 1u << 2,
    BasicHasLidar = 1u << 3,
    BasicHasTemperature = 1u << 4,
    BasicHasHumidity = 1u << 5,
    BasicHasPressure = 1u << 6,
};

struct TelemetryBasic
{
    quint64 host_time_us = 0;
    quint64 epsilon_time_us = 0;
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    double height_m = 0.0;
    double ecef_x_m = 0.0;
    double ecef_y_m = 0.0;
    double ecef_z_m = 0.0;
    float lidar_height_m = 0.0f;
    float temperature_c = 0.0f;
    float humidity_percent = 0.0f;
    float pressure_hpa = 0.0f;
    quint16 status_bits = 0;
    quint16 filter_status_bits = 0;
    quint16 update_status_bits = 0;
    quint8 gnss_fix_code = 0;
    quint32 validity_flags = 0;
};

struct DownsampledWaveform
{
    quint64 host_time_us = 0;
    quint64 epsilon_time_us = 0;
    quint32 original_point_count = 0;
    quint32 downsampled_point_count = 0;
    quint16 channel_id = 0;
    quint16 sample_format = 1;
    float x_start = 0.0f;
    float x_step = 1.0f;
    QVector<float> samples;
};

struct WaveformFeature
{
    quint64 host_time_us = 0;
    quint64 epsilon_time_us = 0;
    quint16 channel_id = 0;
    float peak = 0.0f;
    float mean = 0.0f;
    float rms = 0.0f;
    float peak_index = 0.0f;
    float peak_x = 0.0f;
    float min_value = 0.0f;
    float max_value = 0.0f;
    quint32 quality_flags = 0;
};

struct DeviceStatusItem
{
    SkyDeviceId device_id = SkyDeviceId::Epsilon;
    DeviceState state = DeviceState::Disconnected;
    quint16 error_code = 0;
    quint32 rx_count = 0;
    quint32 error_count = 0;
    quint64 last_data_time_us = 0;
};

struct TelemetryStatus
{
    quint8 recording_state = 0;
    QString session_name;
    quint64 disk_free_bytes = 0;
    float telemetry_basic_rate_hz = 10.0f;
    float waveform_rate_hz = 1.0f;
    float feature_rate_hz = 10.0f;
    float heartbeat_rate_hz = 1.0f;
    float status_rate_hz = 1.0f;
    quint32 rx_total_frames = 0;
    quint32 crc_error_count = 0;
    quint32 current_seq = 0;
    quint64 last_frame_time_us = 0;
    QVector<DeviceStatusItem> devices;
};

struct CommandMessage
{
    CommandId command_id = CommandId::RequestStatus;
    quint16 command_seq = 0;
    QByteArray payload;
};

struct CommandAck
{
    CommandId command_id = CommandId::RequestStatus;
    quint16 command_seq = 0;
    quint8 result = 0;
    CommandErrorCode error_code = CommandErrorCode::Ok;
};

QString skyDeviceIdName(SkyDeviceId id);
QString deviceStateName(DeviceState state);
bool skyDeviceIdFromValue(quint8 value, SkyDeviceId& id);

}  // namespace VaporView

Q_DECLARE_METATYPE(VaporView::TelemetryBasic)
Q_DECLARE_METATYPE(VaporView::DownsampledWaveform)
Q_DECLARE_METATYPE(VaporView::WaveformFeature)
Q_DECLARE_METATYPE(VaporView::DeviceStatusItem)
Q_DECLARE_METATYPE(VaporView::TelemetryStatus)
Q_DECLARE_METATYPE(VaporView::CommandAck)
Q_DECLARE_METATYPE(VaporView::SkyDeviceId)

#endif
