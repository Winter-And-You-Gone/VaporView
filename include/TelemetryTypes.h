#ifndef VaporView_TELEMETRY_TYPES_H_
#define VaporView_TELEMETRY_TYPES_H_

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include <array>

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
    TemperatureControllerStatus = 0x07,
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
    SetPeakSearchRange = 34,
    SetTemperatureTarget = 40,
    SetTemperatureOutputEnabled = 41,
    SetTemperatureOutputMode = 42,
    SetTemperatureMaxOutputPercent = 43,
    SetTemperaturePid = 44,
    SetTemperatureAutoPid = 45,
    SetTemperatureControllerMode = 46,
    SetTemperatureDeviceAddress = 47,
    SetTemperatureRs485Baud = 48,
    SetTemperatureOvertempOutputMode = 49,
    RestoreTemperatureFactoryDefaults = 50,
    SetTemperatureSensorConfig = 51,
    ShutdownCore = 90,
};

enum class SkyDeviceId : quint8
{
    Epsilon = 1,
    Ptb = 2,
    Hmp = 3,
    Lidar = 4,
    WaveTcp = 5,
    TemperatureController = 6,
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
    BasicHasGnssQuality = 1u << 7,
    BasicHasNedVelocity = 1u << 8,
    BasicHasImu = 1u << 9,
    BasicHasAttitude = 1u << 10,
    BasicHasLidarStrength = 1u << 11,
    BasicHasEpsilonDiagnostics = 1u << 12,
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
    quint16 gnss_satellites = 0;
    quint16 lidar_signal_strength = 0;
    float hdop = 0.0f;
    float vdop = 0.0f;
    float hacc_m = 0.0f;
    float vacc_m = 0.0f;
    bool heading_valid = false;
    float vel_n_mps = 0.0f;
    float vel_e_mps = 0.0f;
    float vel_d_mps = 0.0f;
    float imu_acc_x_mps2 = 0.0f;
    float imu_acc_y_mps2 = 0.0f;
    float imu_acc_z_mps2 = 0.0f;
    float imu_gyr_x_radps = 0.0f;
    float imu_gyr_y_radps = 0.0f;
    float imu_gyr_z_radps = 0.0f;
    float roll_deg = 0.0f;
    float pitch_deg = 0.0f;
    float yaw_deg = 0.0f;
    quint64 raw_frame_count = 0;
    quint64 dropped_frame_count = 0;
    float imu_packet_rate_hz = 0.0f;
    float ahrs_packet_rate_hz = 0.0f;
    float insgps_packet_rate_hz = 0.0f;
    float sys_state_packet_rate_hz = 0.0f;
    float raw_gnss_packet_rate_hz = 0.0f;
    float satellite_packet_rate_hz = 0.0f;
    float geodetic_packet_rate_hz = 0.0f;
    float ecef_packet_rate_hz = 0.0f;
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
    quint32 original_point_count = 0;
    quint32 search_start_index = 0;
    quint32 search_end_index = 0;
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

struct PeakSearchRange
{
    quint32 start_index = 0;
    quint32 end_index = 0;
};

struct TemperatureControllerCommand
{
    quint8 channel = 1;
    double target_temperature_c = 0.0;
    bool output_enabled = false;
    quint16 output_mode = 0;
    quint16 max_output_percent = 0;
    quint32 kp = 0;
    quint32 ki = 0;
    quint32 kd = 0;
    quint16 auto_pid_mode = 0;
    quint16 controller_mode = 0;
    quint16 device_address = 1;
    quint16 rs485_baud_index = 1;
    quint16 overtemp_output_mode = 1;
    quint16 sensor_model = 0;
    quint32 ntc_b = 395000;
    quint32 ntc_r0 = 10000;
    quint32 pt_r0 = 1000000;
    qint32 pt_a = 3908300;
    qint32 pt_b = -577500;
    qint32 pt_c = -41830;
    std::array<qint64, 8> polynomial_mantissas{};
    std::array<qint16, 8> polynomial_exponents{};
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
    float wave_tcp_actual_rate_hz = 0.0f;
    quint64 recording_start_time_us = 0;
    quint64 recording_elapsed_ms = 0;
    quint64 telemetry_record_count = 0;
    quint64 waveform_feature_record_count = 0;
    quint64 waveform_snapshot_record_count = 0;
    quint64 raw_epsilon_record_count = 0;
    quint64 raw_ptb_record_count = 0;
    quint64 raw_hmp_record_count = 0;
    quint64 raw_lidar_record_count = 0;
    quint64 raw_tcp_wave_record_count = 0;
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
QString commandIdName(CommandId id);
QString commandErrorCodeText(CommandErrorCode error, bool english = false);
bool skyDeviceIdFromValue(quint8 value, SkyDeviceId& id);

}  // namespace VaporView

Q_DECLARE_METATYPE(VaporView::TelemetryBasic)
Q_DECLARE_METATYPE(VaporView::DownsampledWaveform)
Q_DECLARE_METATYPE(VaporView::WaveformFeature)
Q_DECLARE_METATYPE(VaporView::DeviceStatusItem)
Q_DECLARE_METATYPE(VaporView::TelemetryStatus)
Q_DECLARE_METATYPE(VaporView::CommandAck)
Q_DECLARE_METATYPE(VaporView::CommandId)
Q_DECLARE_METATYPE(VaporView::SkyDeviceId)

#endif
