#include "SkyConfig.h"
#include "TelemetryCodec.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void testProtocolEnumValues()
{
    require(static_cast<quint8>(VaporView::MsgType::TelemetryBasic) == 0x01, "MsgType TelemetryBasic value");
    require(static_cast<quint8>(VaporView::MsgType::WaveformDownsampled) == 0x02, "MsgType WaveformDownsampled value");
    require(static_cast<quint8>(VaporView::MsgType::WaveformFeature) == 0x03, "MsgType WaveformFeature value");
    require(static_cast<quint8>(VaporView::MsgType::TelemetryStatus) == 0x04, "MsgType TelemetryStatus value");
    require(static_cast<quint8>(VaporView::MsgType::Command) == 0x10, "MsgType Command value");
    require(static_cast<quint8>(VaporView::MsgType::CommandAck) == 0x11, "MsgType CommandAck value");
    require(static_cast<quint16>(VaporView::CommandId::StartRecording) == 1, "CommandId StartRecording value");
    require(static_cast<quint16>(VaporView::CommandId::RequestStatus) == 10, "CommandId RequestStatus value");
    require(static_cast<quint16>(VaporView::CommandId::RebootDevice) == 11, "CommandId RebootDevice value");
    require(static_cast<quint16>(VaporView::CommandId::ConnectDevice) == 21, "CommandId ConnectDevice value");
    require(static_cast<quint16>(VaporView::CommandId::SetPeakSearchRange) == 34, "CommandId SetPeakSearchRange value");
    require(static_cast<quint16>(VaporView::CommandId::SetTemperatureTarget) == 40, "CommandId SetTemperatureTarget value");
    require(static_cast<quint16>(VaporView::CommandId::SetTemperatureAutoPid) == 45, "CommandId SetTemperatureAutoPid value");
    require(static_cast<quint16>(VaporView::CommandId::SetTemperatureControllerMode) == 46, "CommandId SetTemperatureControllerMode value");
    require(static_cast<quint16>(VaporView::CommandId::SetTemperatureDeviceAddress) == 47, "CommandId SetTemperatureDeviceAddress value");
    require(static_cast<quint16>(VaporView::CommandId::SetTemperatureRs485Baud) == 48, "CommandId SetTemperatureRs485Baud value");
    require(static_cast<quint16>(VaporView::CommandId::SetTemperatureOvertempOutputMode) == 49, "CommandId SetTemperatureOvertempOutputMode value");
    require(static_cast<quint16>(VaporView::CommandId::RestoreTemperatureFactoryDefaults) == 50, "CommandId RestoreTemperatureFactoryDefaults value");
    require(static_cast<quint16>(VaporView::CommandId::SetTemperatureOvertempUpper) == 52, "CommandId SetTemperatureOvertempUpper value");
    require(static_cast<quint16>(VaporView::CommandId::SetTemperatureStartupDelay) == 55, "CommandId SetTemperatureStartupDelay value");
    require(static_cast<quint16>(VaporView::CommandId::ShutdownCore) == 90, "CommandId ShutdownCore value");
}

void testFrameRoundTrip()
{
    VaporView::TelemetryCodec codec;
    VaporView::TelemetryBasic basic;
    basic.host_time_us = 11;
    basic.epsilon_time_us = 22;
    basic.latitude_deg = 31.25;
    basic.longitude_deg = 121.5;
    basic.height_m = 1234.5;
    basic.ecef_x_m = 1.0;
    basic.ecef_y_m = 2.0;
    basic.ecef_z_m = 3.0;
    basic.lidar_height_m = 44.0f;
    basic.temperature_c = 25.0f;
    basic.humidity_percent = 60.0f;
    basic.pressure_hpa = 900.0f;
    basic.status_bits = 0x55AA;
    basic.filter_status_bits = 0x0060;
    basic.update_status_bits = 0x0003;
    basic.gnss_fix_code = 6;
    basic.gnss_satellites = 18;
    basic.lidar_signal_strength = 180;
    basic.hdop = 0.75f;
    basic.vdop = 1.25f;
    basic.hacc_m = 0.012f;
    basic.vacc_m = 0.034f;
    basic.heading_valid = true;
    basic.vel_n_mps = 1.25f;
    basic.vel_e_mps = -2.5f;
    basic.vel_d_mps = 0.75f;
    basic.imu_acc_x_mps2 = 9.1f;
    basic.imu_acc_y_mps2 = -0.2f;
    basic.imu_acc_z_mps2 = 0.3f;
    basic.imu_gyr_x_radps = 0.004f;
    basic.imu_gyr_y_radps = -0.005f;
    basic.imu_gyr_z_radps = 0.006f;
    basic.roll_deg = 1.25f;
    basic.pitch_deg = -2.5f;
    basic.yaw_deg = 88.75f;
    basic.raw_frame_count = 1234;
    basic.dropped_frame_count = 5;
    basic.imu_packet_rate_hz = 100.0f;
    basic.ahrs_packet_rate_hz = 40.0f;
    basic.insgps_packet_rate_hz = 41.0f;
    basic.sys_state_packet_rate_hz = 42.0f;
    basic.raw_gnss_packet_rate_hz = 50.0f;
    basic.satellite_packet_rate_hz = 59.0f;
    basic.geodetic_packet_rate_hz = 10.0f;
    basic.ecef_packet_rate_hz = 11.0f;
    basic.validity_flags = VaporView::BasicHasEpsilonTime |
                           VaporView::BasicHasPosition |
                           VaporView::BasicHasEcef |
                           VaporView::BasicHasLidar |
                           VaporView::BasicHasTemperature |
                           VaporView::BasicHasHumidity |
                           VaporView::BasicHasPressure |
                           VaporView::BasicHasGnssQuality |
                           VaporView::BasicHasNedVelocity |
                           VaporView::BasicHasImu |
                           VaporView::BasicHasAttitude |
                           VaporView::BasicHasLidarStrength |
                           VaporView::BasicHasEpsilonDiagnostics;

    const QByteArray payload = VaporView::TelemetryCodec::serializeBasicTelemetry(basic);
    VaporView::TelemetryBasic parsedLegacy;
    require(VaporView::TelemetryCodec::parseBasicTelemetry(payload.left(91), parsedLegacy), "parse legacy basic telemetry");
    require(parsedLegacy.gnss_satellites == 0, "legacy basic satellites default");
    const QByteArray frame = codec.encodeFrame(VaporView::MsgType::TelemetryBasic, payload, 7, 99);
    const QByteArray noisy = QByteArray("noise") + frame.left(frame.size() / 2);
    require(codec.feedBytes(noisy).isEmpty(), "partial frame should not decode");
    const QVector<VaporView::TelemetryFrame> frames = codec.feedBytes(frame.mid(frame.size() / 2) + frame);
    require(frames.size() == 2, "sticky frame decode count");
    VaporView::TelemetryBasic parsed;
    require(VaporView::TelemetryCodec::parseBasicTelemetry(frames.front().payload, parsed), "parse basic telemetry");
    require(parsed.host_time_us == basic.host_time_us, "basic host time");
    require(std::fabs(parsed.latitude_deg - basic.latitude_deg) < 0.000001, "basic latitude");
    require(parsed.filter_status_bits == basic.filter_status_bits, "basic filter status");
    require(parsed.gnss_fix_code == basic.gnss_fix_code, "basic gnss fix");
    require(parsed.validity_flags == basic.validity_flags, "basic validity flags");
    require(parsed.gnss_satellites == basic.gnss_satellites, "basic satellites");
    require(parsed.lidar_signal_strength == basic.lidar_signal_strength, "basic lidar strength");
    require(std::fabs(parsed.hacc_m - basic.hacc_m) < 0.000001f, "basic hacc");
    require(parsed.heading_valid == basic.heading_valid, "basic heading valid");
    require(std::fabs(parsed.vel_e_mps - basic.vel_e_mps) < 0.000001f, "basic ned velocity");
    require(std::fabs(parsed.imu_acc_x_mps2 - basic.imu_acc_x_mps2) < 0.000001f, "basic imu accel");
    require(std::fabs(parsed.imu_gyr_z_radps - basic.imu_gyr_z_radps) < 0.000001f, "basic imu gyro");
    require(std::fabs(parsed.yaw_deg - basic.yaw_deg) < 0.000001f, "basic attitude");
    require(parsed.raw_frame_count == basic.raw_frame_count, "basic raw frame count");
    require(parsed.dropped_frame_count == basic.dropped_frame_count, "basic dropped frame count");
    require(std::fabs(parsed.imu_packet_rate_hz - basic.imu_packet_rate_hz) < 0.000001f, "basic imu packet rate");
    require(std::fabs(parsed.ecef_packet_rate_hz - basic.ecef_packet_rate_hz) < 0.000001f, "basic ecef packet rate");
}

void testCrcError()
{
    VaporView::TelemetryCodec encoder;
    VaporView::TelemetryCodec decoder;
    QByteArray frame = encoder.encodeFrame(VaporView::MsgType::Heartbeat, QByteArray("abc"), 1, 2);
    frame[frame.size() - 1] = static_cast<char>(frame.at(frame.size() - 1) ^ 0x7F);
    require(decoder.feedBytes(frame).isEmpty(), "bad crc should drop frame");
    require(decoder.crcErrorCount() > 0, "crc error count");
}

void testCommandAndDevicePayload()
{
    VaporView::CommandMessage command;
    command.command_id = VaporView::CommandId::ConnectDevice;
    command.command_seq = 42;
    command.payload = VaporView::TelemetryCodec::serializeDeviceCommand(VaporView::SkyDeviceId::Lidar);
    VaporView::CommandMessage parsed;
    require(VaporView::TelemetryCodec::parseCommand(VaporView::TelemetryCodec::serializeCommand(command), parsed), "parse command");
    require(parsed.command_id == VaporView::CommandId::ConnectDevice, "command id");
    VaporView::SkyDeviceId id = VaporView::SkyDeviceId::All;
    require(VaporView::TelemetryCodec::parseDeviceCommand(parsed.payload, id), "parse device payload");
    require(id == VaporView::SkyDeviceId::Lidar, "device id");

    VaporView::CommandAck ack;
    ack.command_id = command.command_id;
    ack.command_seq = command.command_seq;
    ack.result = 1;
    ack.error_code = VaporView::CommandErrorCode::DeviceConnectFailed;
    VaporView::CommandAck parsedAck;
    require(VaporView::TelemetryCodec::parseCommandAck(VaporView::TelemetryCodec::serializeCommandAck(ack), parsedAck), "parse ack");
    require(parsedAck.error_code == VaporView::CommandErrorCode::DeviceConnectFailed, "ack error");
}

void testWaveform()
{
    QVector<float> original;
    original.resize(50000);
    for (int i = 0; i < original.size(); ++i)
    {
        original[i] = static_cast<float>(i);
    }
    VaporView::DownsampledWaveform waveform;
    waveform.original_point_count = static_cast<quint32>(original.size());
    waveform.channel_id = 4;
    waveform.x_step = 10.0f;
    for (int i = 0; i < original.size(); i += 10)
    {
        waveform.samples.push_back(original.at(i));
    }
    waveform.downsampled_point_count = static_cast<quint32>(waveform.samples.size());
    VaporView::DownsampledWaveform parsed;
    require(VaporView::TelemetryCodec::parseDownsampledWaveform(VaporView::TelemetryCodec::serializeDownsampledWaveform(waveform), parsed), "parse waveform");
    require(parsed.samples.size() == 5000, "downsample count");
    require(parsed.samples.at(1234) == 12340.0f, "downsample value");

    VaporView::WaveformFeature feature;
    feature.host_time_us = 100;
    feature.epsilon_time_us = 90;
    feature.original_point_count = 50000;
    feature.search_start_index = 1000;
    feature.search_end_index = 8000;
    feature.channel_id = 4;
    feature.peak = 0.5f;
    feature.mean = 0.1f;
    feature.rms = 0.2f;
    feature.peak_index = 1200.0f;
    feature.peak_x = 1200.0f;
    feature.min_value = -0.1f;
    feature.max_value = 1.0f;
    feature.quality_flags = 3;
    VaporView::WaveformFeature parsedFeature;
    require(VaporView::TelemetryCodec::parseWaveformFeature(VaporView::TelemetryCodec::serializeWaveformFeature(feature), parsedFeature), "parse feature");
    require(parsedFeature.search_start_index == feature.search_start_index, "feature search start");
    require(parsedFeature.search_end_index == feature.search_end_index, "feature search end");

    VaporView::PeakSearchRange range;
    range.start_index = 123;
    range.end_index = 456;
    VaporView::PeakSearchRange parsedRange;
    require(VaporView::TelemetryCodec::parsePeakSearchRange(VaporView::TelemetryCodec::serializePeakSearchRange(range), parsedRange), "parse peak search range");
    require(parsedRange.start_index == 123 && parsedRange.end_index == 456, "peak search range values");

    VaporView::TemperatureControllerCommand command;
    command.channel = 2;
    command.target_temperature_c = 25.5;
    command.output_enabled = true;
    command.output_mode = 3;
    command.max_output_percent = 80;
    command.kp = 11;
    command.ki = 22;
    command.kd = 33;
    command.auto_pid_mode = 1;
    command.controller_mode = 3;
    command.device_address = 9;
    command.rs485_baud_index = 5;
    command.overtemp_output_mode = 1;
    command.overtemp_upper_c = 499.12345;
    command.overtemp_lower_c = -40.54321;
    command.temperature_slope_c_per_s = 1.234;
    command.startup_delay_s = 15;
    command.sensor_model = 1;
    command.ntc_b = 395000;
    command.ntc_r0 = 10000;
    command.pt_r0 = 1000000;
    command.pt_a = 3908300;
    command.pt_b = -577500;
    command.pt_c = -41830;
    command.polynomial_mantissas[0] = 5412000000000LL;
    command.polynomial_exponents[0] = -1;
    command.polynomial_mantissas[1] = -2245952000000LL;
    command.polynomial_exponents[1] = -2;
    VaporView::TemperatureControllerCommand parsedCommand;
    require(VaporView::TelemetryCodec::parseTemperatureControllerCommand(
                VaporView::TelemetryCodec::serializeTemperatureControllerCommand(command), parsedCommand),
            "parse temperature controller command");
    require(parsedCommand.channel == 2, "temperature command channel");
    require(std::fabs(parsedCommand.target_temperature_c - 25.5) < 0.000001, "temperature command target");
    require(parsedCommand.output_enabled, "temperature command output enabled");
    require(parsedCommand.output_mode == 3 && parsedCommand.max_output_percent == 80, "temperature command output values");
    require(parsedCommand.kp == 11 && parsedCommand.ki == 22 && parsedCommand.kd == 33, "temperature command pid values");
    require(parsedCommand.auto_pid_mode == 1 && parsedCommand.controller_mode == 3, "temperature command advanced values");
    require(parsedCommand.device_address == 9 &&
                parsedCommand.rs485_baud_index == 5 &&
                parsedCommand.overtemp_output_mode == 1,
            "temperature command common settings values");
    require(std::fabs(parsedCommand.overtemp_upper_c - 499.12345) < 0.000001 &&
                std::fabs(parsedCommand.overtemp_lower_c + 40.54321) < 0.000001 &&
                std::fabs(parsedCommand.temperature_slope_c_per_s - 1.234) < 0.000001 &&
                parsedCommand.startup_delay_s == 15,
            "temperature command professional parameter values");
    require(parsedCommand.sensor_model == 1 &&
                parsedCommand.ntc_b == 395000 &&
                parsedCommand.ntc_r0 == 10000 &&
                parsedCommand.pt_r0 == 1000000,
            "temperature command sensor resistance values");
    require(parsedCommand.pt_a == 3908300 &&
                parsedCommand.pt_b == -577500 &&
                parsedCommand.pt_c == -41830,
            "temperature command PT coefficient values");
    require(parsedCommand.polynomial_mantissas[0] == 5412000000000LL &&
                parsedCommand.polynomial_exponents[0] == -1 &&
                parsedCommand.polynomial_mantissas[1] == -2245952000000LL &&
                parsedCommand.polynomial_exponents[1] == -2,
            "temperature command polynomial values");

    VaporView::TemperatureControllerData status;
    status.valid = true;
    status.internal_temperature_c = 32.25;
    status.error_code = 7;
    status.controller_mode = 2;
    status.device_address = 9;
    status.rs485_baud_index = 5;
    status.overtemp_output_mode = 1;
    status.channels[0].target_temperature_c = 25.0;
    status.channels[0].measured_temperature_c = 24.9;
    status.channels[0].output_enabled = true;
    status.channels[0].output_mode = 1;
    status.channels[0].max_output_percent = 70;
    status.channels[0].auto_pid_mode = 1;
    status.channels[0].overtemp_upper_c = 500.0;
    status.channels[0].overtemp_lower_c = -35.0;
    status.channels[0].temperature_slope_c_per_s = 0.125;
    status.channels[0].startup_delay_s = 12;
    status.channels[0].sensor_resistance_ohm = 11948.4923;
    status.channels[0].kp = 100;
    status.channels[0].sensor_model = 2;
    status.channels[0].ntc_b = 395000;
    status.channels[0].ntc_r0 = 10000;
    status.channels[0].pt_r0 = 1000000;
    status.channels[0].pt_a = 3908300;
    status.channels[0].pt_b = -577500;
    status.channels[0].pt_c = -41830;
    status.channels[0].polynomial_mantissas[3] = 12345000000000LL;
    status.channels[0].polynomial_exponents[3] = -4;
    status.channels[1].target_temperature_c = 26.0;
    status.channels[1].output_percent = 12.5;
    status.channels[1].auto_pid_mode = 2;
    status.channels[1].sensor_model = 3;
    status.channels[1].ntc_b = 410000;
    status.channels[1].ntc_r0 = 22000;
    status.channels[1].pt_r0 = 1000100;
    status.channels[1].pt_a = 3908301;
    status.channels[1].pt_b = -577501;
    status.channels[1].pt_c = -41831;
    status.channels[1].polynomial_mantissas[7] = -9876500000000LL;
    status.channels[1].polynomial_exponents[7] = 5;
    VaporView::TemperatureControllerData parsedStatus;
    require(VaporView::TelemetryCodec::parseTemperatureControllerStatus(
                VaporView::TelemetryCodec::serializeTemperatureControllerStatus(status), parsedStatus),
            "parse temperature controller status");
    require(parsedStatus.valid, "temperature status valid");
    require(parsedStatus.error_code == 7, "temperature status error code");
    require(std::fabs(parsedStatus.internal_temperature_c - 32.25) < 0.000001, "temperature status internal temp");
    require(parsedStatus.controller_mode == 2, "temperature status controller mode");
    require(parsedStatus.device_address == 9 &&
                parsedStatus.rs485_baud_index == 5 &&
                parsedStatus.overtemp_output_mode == 1,
            "temperature status common settings");
    require(parsedStatus.channels[0].output_enabled && parsedStatus.channels[0].kp == 100 && parsedStatus.channels[0].auto_pid_mode == 1, "temperature status channel one");
    require(std::fabs(parsedStatus.channels[0].overtemp_upper_c - 500.0) < 0.000001 &&
                std::fabs(parsedStatus.channels[0].overtemp_lower_c + 35.0) < 0.000001 &&
                std::fabs(parsedStatus.channels[0].temperature_slope_c_per_s - 0.125) < 0.000001 &&
                parsedStatus.channels[0].startup_delay_s == 12 &&
                std::fabs(parsedStatus.channels[0].sensor_resistance_ohm - 11948.4923) < 0.000001,
            "temperature status channel one professional parameters");
    require(std::fabs(parsedStatus.channels[1].output_percent - 12.5) < 0.000001 && parsedStatus.channels[1].auto_pid_mode == 2, "temperature status channel two");
    require(parsedStatus.channels[0].sensor_model == 2 &&
                parsedStatus.channels[0].ntc_b == 395000 &&
                parsedStatus.channels[0].ntc_r0 == 10000 &&
                parsedStatus.channels[0].pt_r0 == 1000000 &&
                parsedStatus.channels[0].pt_a == 3908300 &&
                parsedStatus.channels[0].pt_b == -577500 &&
                parsedStatus.channels[0].pt_c == -41830,
            "temperature status channel one sensor config");
    require(parsedStatus.channels[0].polynomial_mantissas[3] == 12345000000000LL &&
                parsedStatus.channels[0].polynomial_exponents[3] == -4,
            "temperature status channel one polynomial config");
    require(parsedStatus.channels[1].sensor_model == 3 &&
                parsedStatus.channels[1].ntc_b == 410000 &&
                parsedStatus.channels[1].ntc_r0 == 22000 &&
                parsedStatus.channels[1].pt_r0 == 1000100 &&
                parsedStatus.channels[1].pt_a == 3908301 &&
                parsedStatus.channels[1].pt_b == -577501 &&
                parsedStatus.channels[1].pt_c == -41831,
            "temperature status channel two independent sensor config");
    require(parsedStatus.channels[1].polynomial_mantissas[7] == -9876500000000LL &&
                parsedStatus.channels[1].polynomial_exponents[7] == 5,
            "temperature status channel two polynomial config");
}

void testSkyConfigDiff()
{
    VaporView::SkyConfig a = VaporView::SkyConfig::defaults();
    require(a.temperature_controller.baud_rate == 38400, "sky config RD105 default baud");
    VaporView::SkyConfig b = a;
    require(!a.diff(b).epsilon_changed, "unchanged epsilon");
    b.epsilon.baud_rate = 115200;
    const VaporView::SkyConfigDiff diff = a.diff(b);
    require(diff.epsilon_changed, "epsilon changed");
    require(!diff.ptb_changed && !diff.telemetry_changed, "other config unchanged");

    const QJsonObject json = b.toJson();
    VaporView::SkyConfig parsed;
    QString error;
    require(VaporView::SkyConfig::fromJson(json, parsed, &error), "sky config parse");
    require(parsed.epsilon.baud_rate == 115200, "sky config baud");
}

void testSkyConfigRejectsInvalidJsonTypes()
{
    VaporView::SkyConfig parsed;
    QString error;

    QJsonObject sectionType = VaporView::SkyConfig::defaults().toJson();
    sectionType.insert(QStringLiteral("epsilon"), QJsonArray{});
    require(!VaporView::SkyConfig::fromJson(sectionType, parsed, &error), "sky config rejects non-object section");
    require(error.contains(QStringLiteral("epsilon")) && error.contains(QStringLiteral("object")),
            "sky config section type error message");

    QJsonObject badBaud = VaporView::SkyConfig::defaults().toJson();
    QJsonObject epsilon = badBaud.value(QStringLiteral("epsilon")).toObject();
    epsilon.insert(QStringLiteral("baud"), QStringLiteral("bad"));
    badBaud.insert(QStringLiteral("epsilon"), epsilon);
    error.clear();
    require(!VaporView::SkyConfig::fromJson(badBaud, parsed, &error), "sky config rejects string baud");
    require(error.contains(QStringLiteral("epsilon.baud")) && error.contains(QStringLiteral("integer")),
            "sky config baud type error message");

    QJsonObject badEnabled = VaporView::SkyConfig::defaults().toJson();
    QJsonObject wave = badEnabled.value(QStringLiteral("wave_tcp")).toObject();
    wave.insert(QStringLiteral("enabled"), QStringLiteral("true"));
    badEnabled.insert(QStringLiteral("wave_tcp"), wave);
    error.clear();
    require(!VaporView::SkyConfig::fromJson(badEnabled, parsed, &error), "sky config rejects string bool");
    require(error.contains(QStringLiteral("wave_tcp.enabled")) && error.contains(QStringLiteral("boolean")),
            "sky config bool type error message");
}

void testTelemetryStatus()
{
    VaporView::TelemetryStatus status;
    status.recording_state = 1;
    status.session_name = QStringLiteral("test-session");
    status.disk_free_bytes = 123456789;
    status.telemetry_basic_rate_hz = 10.0f;
    status.waveform_rate_hz = 1.0f;
    status.feature_rate_hz = 20.0f;
    status.heartbeat_rate_hz = 1.0f;
    status.status_rate_hz = 2.0f;
    status.wave_tcp_actual_rate_hz = 199.5f;
    status.recording_start_time_us = 123000000;
    status.recording_elapsed_ms = 4567;
    status.telemetry_record_count = 12;
    status.waveform_feature_record_count = 3;
    status.waveform_snapshot_record_count = 4;
    status.raw_navigation_record_count = 100;
    status.raw_pressure_record_count = 20;
    status.raw_temperature_humidity_record_count = 30;
    status.raw_distance_record_count = 40;
    status.raw_waveform_record_count = 50;
    VaporView::DeviceStatusItem wave;
    wave.device_id = VaporView::SkyDeviceId::WaveTcp;
    wave.state = VaporView::DeviceState::Connected;
    wave.rx_count = 42;
    status.devices.push_back(wave);

    VaporView::TelemetryStatus parsed;
    require(VaporView::TelemetryCodec::parseTelemetryStatus(VaporView::TelemetryCodec::serializeTelemetryStatus(status), parsed),
            "parse telemetry status");
    require(parsed.session_name == status.session_name, "status session");
    require(parsed.devices.size() == 1 && parsed.devices.front().device_id == VaporView::SkyDeviceId::WaveTcp, "status device");
    require(std::fabs(parsed.wave_tcp_actual_rate_hz - status.wave_tcp_actual_rate_hz) < 0.001f,
            "status wave tcp actual rate");
    require(parsed.recording_elapsed_ms == status.recording_elapsed_ms, "status recording elapsed");
    require(parsed.raw_navigation_record_count == status.raw_navigation_record_count, "status raw epsilon count");
    require(parsed.raw_waveform_record_count == status.raw_waveform_record_count, "status raw tcp wave count");
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testProtocolEnumValues();
    testFrameRoundTrip();
    testCrcError();
    testCommandAndDevicePayload();
    testWaveform();
    testSkyConfigDiff();
    testSkyConfigRejectsInvalidJsonTypes();
    testTelemetryStatus();
    std::cout << "telemetry_codec_test passed\n";
    return 0;
}
