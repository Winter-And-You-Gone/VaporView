#include "ground/devices/UiTestDataModel.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>

namespace VaporView::Ground::Devices
{
namespace
{

constexpr double kPi = 3.14159265358979323846;

std::chrono::steady_clock::time_point sampleTimestamp(qint64 elapsedMs)
{
    return std::chrono::steady_clock::time_point(std::chrono::milliseconds(elapsedMs + 1));
}

bool connected(DeviceState state)
{
    return state == DeviceState::Connected;
}

double boundedRandom(qint64 elapsedMs, int salt, double minimum, double maximum)
{
    const qint64 bucket = std::max<qint64>(0, elapsedMs) / 350;
    std::uint64_t value =
        static_cast<std::uint64_t>(bucket + 1) * 0x9E3779B185EBCA87ull;
    value ^= static_cast<std::uint64_t>(salt + 1) * 0xC2B2AE3D27D4EB4Full;
    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdull;
    value ^= value >> 33;
    value *= 0xc4ceb9fe1a85ec53ull;
    value ^= value >> 33;
    const double unit =
        static_cast<double>(value & 0xFFFFFFull) / static_cast<double>(0xFFFFFFull);
    return minimum + ((maximum - minimum) * unit);
}

} // namespace

UiTestDataModel::UiTestDataModel()
{
    reset();
}

void UiTestDataModel::reset(qint64 elapsedMs)
{
    scenario_ = UiTestScenario::Normal;
    scenario_started_ms_ = elapsedMs;
    device_states_.fill(DeviceState::Connected);
    resetTemperatureState();
}

void UiTestDataModel::setScenario(UiTestScenario scenario, qint64 elapsedMs)
{
    scenario_ = scenario;
    scenario_started_ms_ = elapsedMs;
    device_states_.fill(DeviceState::Connected);
    resetTemperatureState();
    if (scenario == UiTestScenario::PartialFailure)
    {
        setDeviceState(SkyDeviceId::Ptb, DeviceState::Error);
        setDeviceState(SkyDeviceId::Hmp, DeviceState::Disconnected);
        temperature_state_.error_code = 0x0004;
    }
}

UiTestScenario UiTestDataModel::scenario() const
{
    return scenario_;
}

void UiTestDataModel::setAllDevicesConnected(bool isConnected)
{
    device_states_.fill(isConnected ? DeviceState::Connected
                                    : DeviceState::Disconnected);
}

void UiTestDataModel::setDeviceState(SkyDeviceId device, DeviceState state)
{
    const int index = deviceIndex(device);
    if (index >= 0)
    {
        device_states_[static_cast<std::size_t>(index)] = state;
    }
}

DeviceState UiTestDataModel::deviceState(SkyDeviceId device) const
{
    const int index = deviceIndex(device);
    return index >= 0
        ? device_states_[static_cast<std::size_t>(index)]
        : DeviceState::Disabled;
}

void UiTestDataModel::applyTemperatureCommand(
    CommandId command,
    const TemperatureControllerCommand& payload)
{
    const int channelIndex = std::clamp<int>(payload.channel, 1, 2) - 1;
    TemperatureControllerChannelData& channel =
        temperature_state_.channels[static_cast<std::size_t>(channelIndex)];
    switch (command)
    {
    case CommandId::SetTemperatureTarget:
        channel.target_temperature_c = payload.target_temperature_c;
        break;
    case CommandId::SetTemperatureOutputEnabled:
        channel.output_enabled = payload.output_enabled;
        break;
    case CommandId::SetTemperatureOutputMode:
        channel.output_mode = payload.output_mode;
        break;
    case CommandId::SetTemperatureMaxOutputPercent:
        channel.max_output_percent = payload.max_output_percent;
        break;
    case CommandId::SetTemperaturePid:
        channel.kp = payload.kp;
        channel.ki = payload.ki;
        channel.kd = payload.kd;
        break;
    case CommandId::SetTemperatureAutoPid:
        channel.auto_pid_mode = payload.auto_pid_mode;
        break;
    case CommandId::SetTemperatureOvertempUpper:
        channel.overtemp_upper_c = payload.overtemp_upper_c;
        break;
    case CommandId::SetTemperatureOvertempLower:
        channel.overtemp_lower_c = payload.overtemp_lower_c;
        break;
    case CommandId::SetTemperatureSlope:
        channel.temperature_slope_c_per_s = payload.temperature_slope_c_per_s;
        break;
    case CommandId::SetTemperatureStartupDelay:
        channel.startup_delay_s = payload.startup_delay_s;
        break;
    case CommandId::SetTemperatureSensorConfig:
        channel.sensor_model = payload.sensor_model;
        channel.ntc_b = payload.ntc_b;
        channel.ntc_r0 = payload.ntc_r0;
        channel.pt_r0 = payload.pt_r0;
        channel.pt_a = payload.pt_a;
        channel.pt_b = payload.pt_b;
        channel.pt_c = payload.pt_c;
        channel.polynomial_mantissas = payload.polynomial_mantissas;
        for (std::size_t index = 0; index < channel.polynomial_exponents.size(); ++index)
        {
            channel.polynomial_exponents[index] = payload.polynomial_exponents[index];
        }
        break;
    case CommandId::SetTemperatureControllerMode:
        temperature_state_.controller_mode = payload.controller_mode;
        break;
    case CommandId::SetTemperatureDeviceAddress:
        temperature_state_.device_address = payload.device_address;
        break;
    case CommandId::SetTemperatureRs485Baud:
        temperature_state_.rs485_baud_index = payload.rs485_baud_index;
        break;
    case CommandId::SetTemperatureOvertempOutputMode:
        temperature_state_.overtemp_output_mode = payload.overtemp_output_mode;
        break;
    case CommandId::RestoreTemperatureFactoryDefaults:
        resetTemperatureState(false);
        break;
    default:
        break;
    }
}

UiTestSnapshot UiTestDataModel::snapshot(qint64 elapsedMs) const
{
    UiTestSnapshot result;
    const bool stalled = scenario_ == UiTestScenario::DataStalled &&
        elapsedMs - scenario_started_ms_ >= 3000;
    const qint64 sampleElapsedMs = stalled ? scenario_started_ms_ + 3000 : elapsedMs;
    const double seconds = std::max<qint64>(0, sampleElapsedMs) / 1000.0;
    const double slow = std::sin(seconds * 0.35);
    const double medium = std::sin(seconds * 0.9);
    const auto timestamp = sampleTimestamp(sampleElapsedMs);
    result.dataStalled = stalled;

    result.epsilon = EpsilonData();
    result.epsilon.valid = connected(deviceState(SkyDeviceId::Epsilon)) && !stalled;
    result.epsilon.timestamp = timestamp;
    result.epsilon.latitude_deg = 30.24620 + slow * 0.00012;
    result.epsilon.longitude_deg = 120.14530 + std::cos(seconds * 0.35) * 0.00015;
    result.epsilon.height_m = 42.5 + medium * 1.2;
    result.epsilon.ned_n_m = slow * 14.0;
    result.epsilon.ned_e_m = std::cos(seconds * 0.35) * 14.0;
    result.epsilon.vel_n_mps = std::cos(seconds * 0.35) * 1.8;
    result.epsilon.vel_e_mps = -slow * 1.8;
    result.epsilon.vel_d_mps = std::sin(seconds * 0.2) * 0.08;
    result.epsilon.imu_acc_x_mps2 = std::sin(seconds * 2.1) * 0.12;
    result.epsilon.imu_acc_y_mps2 = std::cos(seconds * 1.8) * 0.10;
    result.epsilon.imu_acc_z_mps2 = 9.80665 + std::sin(seconds * 1.4) * 0.04;
    result.epsilon.imu_gyr_x_radps = std::sin(seconds) * 0.01;
    result.epsilon.imu_gyr_y_radps = std::cos(seconds * 0.8) * 0.01;
    result.epsilon.imu_gyr_z_radps = 0.02 + std::sin(seconds * 0.5) * 0.005;
    result.epsilon.roll_deg = slow * 3.0;
    result.epsilon.pitch_deg = medium * 2.0;
    result.epsilon.yaw_deg = std::fmod(seconds * 7.0, 360.0);
    result.epsilon.ahrs_attitude_valid = result.epsilon.valid;
    result.epsilon.euler_orien_valid = result.epsilon.valid;
    result.epsilon.system_state_attitude_valid = result.epsilon.valid;
    result.epsilon.ahrs_roll_deg = result.epsilon.roll_deg;
    result.epsilon.ahrs_pitch_deg = result.epsilon.pitch_deg;
    result.epsilon.ahrs_yaw_deg = result.epsilon.yaw_deg;
    result.epsilon.euler_orien_roll_deg = result.epsilon.roll_deg + 0.02;
    result.epsilon.euler_orien_pitch_deg = result.epsilon.pitch_deg - 0.01;
    result.epsilon.euler_orien_yaw_deg = result.epsilon.yaw_deg + 0.03;
    result.epsilon.system_state_roll_deg = result.epsilon.roll_deg;
    result.epsilon.system_state_pitch_deg = result.epsilon.pitch_deg;
    result.epsilon.system_state_yaw_deg = result.epsilon.yaw_deg;
    result.epsilon.attitude_source_count = 3;
    result.epsilon.gnss_fix_code = 4;
    result.epsilon.gnss_fix_text = "RTK Fixed (UI Test)";
    result.epsilon.gnss_satellites = 24;
    result.epsilon.heading_valid = true;
    result.epsilon.hdop = 0.62;
    result.epsilon.vdop = 0.88;
    result.epsilon.hacc_m = 0.015;
    result.epsilon.vacc_m = 0.025;
    result.epsilon.imu_temp_c = 34.0 + slow;
    result.epsilon.pressure_pa = 101325.0 + medium * 25.0;
    result.epsilon.raw_frame_count = static_cast<uint64_t>(seconds * 100.0);
    result.epsilon.imu_packet_rate_hz = result.epsilon.valid ? 100.0 : 0.0;
    result.epsilon.ahrs_packet_rate_hz = result.epsilon.valid ? 100.0 : 0.0;
    result.epsilon.insgps_packet_rate_hz = result.epsilon.valid ? 20.0 : 0.0;
    result.epsilon.sys_state_packet_rate_hz = result.epsilon.valid ? 20.0 : 0.0;
    if (!result.epsilon.valid)
    {
        result.epsilon.error_message = stalled ? "UI test data stalled" : "UI test device disconnected";
    }

    result.gnss.valid = result.epsilon.valid;
    result.gnss.timestamp = timestamp;
    result.gnss.latitude = result.epsilon.latitude_deg;
    result.gnss.longitude = result.epsilon.longitude_deg;
    result.gnss.altitude = result.epsilon.height_m;
    result.gnss.heading = result.epsilon.yaw_deg;
    result.gnss.position_status = "RTK_FIXED";
    result.gnss.num_satellites_used = 24;
    result.gnss.hdop = result.epsilon.hdop;

    result.imu.valid = result.epsilon.valid;
    result.imu.timestamp = timestamp;
    result.imu.acceleration = {result.epsilon.imu_acc_x_mps2,
                               result.epsilon.imu_acc_y_mps2,
                               result.epsilon.imu_acc_z_mps2};
    result.imu.gyroscope = {result.epsilon.imu_gyr_x_radps,
                            result.epsilon.imu_gyr_y_radps,
                            result.epsilon.imu_gyr_z_radps};
    result.imu.rpy = {result.epsilon.roll_deg,
                      result.epsilon.pitch_deg,
                      result.epsilon.yaw_deg};
    result.imu.temperature = result.epsilon.imu_temp_c;
    result.imu.frame_type = ImuFrameType::HI91;

    result.ptb.valid = connected(deviceState(SkyDeviceId::Ptb)) && !stalled;
    result.ptb.timestamp = timestamp;
    result.ptb.pressure_hpa = 1013.25 + slow * 0.45;
    if (!result.ptb.valid)
    {
        result.ptb.error_message = scenario_ == UiTestScenario::PartialFailure
            ? "UI test pressure sensor error"
            : "UI test data stalled";
    }

    result.hmp.valid = connected(deviceState(SkyDeviceId::Hmp)) && !stalled;
    result.hmp.timestamp = timestamp;
    result.hmp.temperature = 23.8 + medium * 0.8;
    result.hmp.humidity = 56.0 + slow * 3.0;
    if (!result.hmp.valid)
    {
        result.hmp.error_message = scenario_ == UiTestScenario::PartialFailure
            ? "UI test humidity sensor disconnected"
            : "UI test data stalled";
    }

    result.lidar.valid = connected(deviceState(SkyDeviceId::Lidar)) && !stalled;
    result.lidar.timestamp = timestamp;
    result.lidar.distance_m = 18.0 + std::sin(seconds * 0.7) * 2.5;
    result.lidar.signal_strength = static_cast<uint16_t>(820 + 70 * slow);
    if (!result.lidar.valid)
    {
        result.lidar.error_message = "UI test data stalled";
    }

    result.temperature = temperature_state_;
    result.temperature.valid = connected(deviceState(SkyDeviceId::TemperatureController)) && !stalled;
    result.temperature.timestamp = timestamp;
    result.temperature.internal_temperature_c = 31.5 + slow * 0.6;
    for (std::size_t index = 0; index < result.temperature.channels.size(); ++index)
    {
        TemperatureControllerChannelData& channel = result.temperature.channels[index];
        const double target = channel.target_temperature_c;
        channel.measured_temperature_c = target + std::sin(seconds * 0.55 + index) * 0.35;
        channel.output_percent = channel.output_enabled ? 32.0 + 8.0 * medium : 0.0;
        channel.output_current_a = channel.output_enabled ? channel.output_percent * 0.012 : 0.0;
        channel.sensor_resistance_ohm = 10000.0 + channel.measured_temperature_c * 18.0;
    }
    if (!result.temperature.valid)
    {
        result.temperature.error_message = "UI test temperature data stalled";
    }

    result.ai8Temperature.valid =
        connected(deviceState(SkyDeviceId::Ai8TemperatureController)) && !stalled;
    result.ai8Temperature.controlStatesValid = result.ai8Temperature.valid;
    result.ai8Temperature.controlStates.fill(
        Ai8TemperatureControllerProtocol::ChannelControlState::ApidOutput);
    for (int index = 0; index < Ai8TemperatureControllerProtocol::kChannelCount; ++index)
    {
        const double channelOffset = static_cast<double>(index) * 0.65;
        result.ai8Temperature.measuredC[static_cast<std::size_t>(index)] =
            23.5 + channelOffset + std::sin(seconds * 0.7 + index * 0.33) * 0.4;
    }
    if (!result.ai8Temperature.valid)
    {
        result.ai8Temperature.errorMessage = stalled
            ? QStringLiteral("UI test system thermal data stalled")
            : QStringLiteral("UI test system thermal device disconnected");
    }

    result.rawWaveform.reserve(512);
    result.harmonicWaveform.reserve(512);
    for (int index = 0; index < 512; ++index)
    {
        const double x = static_cast<double>(index) / 511.0;
        const double carrier = std::sin(2.0 * kPi * (5.0 * x + seconds * 0.4));
        const double envelope = std::exp(-std::pow((x - 0.48) / 0.16, 2.0));
        result.rawWaveform.push_back(static_cast<float>(0.15 * carrier + 1.1 * envelope));
        result.harmonicWaveform.push_back(static_cast<float>(
            0.08 * std::sin(2.0 * kPi * (10.0 * x + seconds * 0.3)) + 0.55 * envelope));
    }
    if (!connected(deviceState(SkyDeviceId::WaveTcp)) || stalled)
    {
        result.rawWaveform.clear();
        result.harmonicWaveform.clear();
    }
    result.waveformFeature.host_time_us = static_cast<quint64>(std::max<qint64>(1, sampleElapsedMs) * 1000);
    result.waveformFeature.original_point_count = 512;
    result.waveformFeature.search_start_index = 80;
    result.waveformFeature.search_end_index = 430;
    result.waveformFeature.channel_id = 4;
    result.waveformFeature.peak = static_cast<float>(0.55 + slow * 0.04);
    result.waveformFeature.mean = 0.16f;
    result.waveformFeature.rms = 0.24f;
    result.waveformFeature.peak_index = 245.0f + static_cast<float>(slow * 3.0);
    result.waveformFeature.peak_x = result.waveformFeature.peak_index;
    result.waveformFeature.min_value = -0.08f;
    result.waveformFeature.max_value = result.waveformFeature.peak;
    result.waveformFeature.quality_flags =
        scenario_ == UiTestScenario::PartialFailure ? 1u : 0u;

    const bool waveFresh = connected(deviceState(SkyDeviceId::WaveTcp)) && !stalled;
    result.epsilonRateHz = result.epsilon.valid ? boundedRandom(sampleElapsedMs, 1, 120.0, 180.0) : 0.0;
    result.gnssRateHz = result.gnss.valid ? boundedRandom(sampleElapsedMs, 2, 100.0, 140.0) : 0.0;
    result.imuRateHz = result.imu.valid ? boundedRandom(sampleElapsedMs, 3, 190.0, 240.0) : 0.0;
    result.ptbRateHz = result.ptb.valid ? boundedRandom(sampleElapsedMs, 4, 100.0, 135.0) : 0.0;
    result.hmpRateHz = result.hmp.valid ? boundedRandom(sampleElapsedMs, 5, 100.0, 135.0) : 0.0;
    result.lidarRateHz = result.lidar.valid ? boundedRandom(sampleElapsedMs, 6, 120.0, 180.0) : 0.0;
    result.temperatureRateHz = result.temperature.valid ? boundedRandom(sampleElapsedMs, 7, 100.0, 130.0) : 0.0;
    result.waveformFeatureRateHz = waveFresh ? boundedRandom(sampleElapsedMs, 8, 110.0, 160.0) : 0.0;
    result.telemetryStatusRateHz = !stalled ? boundedRandom(sampleElapsedMs, 9, 100.0, 130.0) : 0.0;
    result.rawWaveformRateHz = waveFresh ? boundedRandom(sampleElapsedMs, 10, 115.0, 170.0) : 0.0;
    result.harmonicWaveformRateHz = waveFresh ? boundedRandom(sampleElapsedMs, 11, 100.0, 145.0) : 0.0;
    result.waveCaptureRateHz = waveFresh ? boundedRandom(sampleElapsedMs, 12, 110.0, 165.0) : 0.0;
    result.receiveBitsPerSecond = !stalled ? boundedRandom(sampleElapsedMs, 13, 140'000'000.0, 360'000'000.0) : 0.0;
    result.transmitBitsPerSecond = !stalled ? boundedRandom(sampleElapsedMs, 14, 130'000'000.0, 330'000'000.0) : 0.0;
    return result;
}

int UiTestDataModel::deviceIndex(SkyDeviceId device)
{
    switch (device)
    {
    case SkyDeviceId::Epsilon: return 0;
    case SkyDeviceId::Ptb: return 1;
    case SkyDeviceId::Hmp: return 2;
    case SkyDeviceId::Lidar: return 3;
    case SkyDeviceId::WaveTcp: return 4;
    case SkyDeviceId::TemperatureController: return 5;
    case SkyDeviceId::Ai8TemperatureController: return 6;
    case SkyDeviceId::All: return -1;
    }
    return -1;
}

void UiTestDataModel::resetTemperatureState(bool outputEnabled)
{
    temperature_state_ = TemperatureControllerData();
    temperature_state_.valid = true;
    temperature_state_.device_address = 1;
    temperature_state_.rs485_baud_index = 1;
    temperature_state_.controller_mode = 1;
    temperature_state_.overtemp_output_mode = 1;
    for (std::size_t index = 0; index < temperature_state_.channels.size(); ++index)
    {
        TemperatureControllerChannelData& channel = temperature_state_.channels[index];
        channel.target_temperature_c = index == 0 ? 25.0 : 27.0;
        channel.measured_temperature_c = channel.target_temperature_c;
        channel.output_percent = outputEnabled ? 32.0 : 0.0;
        channel.output_current_a = outputEnabled ? channel.output_percent * 0.012 : 0.0;
        channel.output_mode = 1;
        channel.output_enabled = outputEnabled;
        channel.max_output_percent = 80;
        channel.auto_pid_mode = 1;
        channel.overtemp_upper_c = 45.0;
        channel.overtemp_lower_c = -10.0;
        channel.temperature_slope_c_per_s = 0.5;
        channel.startup_delay_s = 3;
        channel.sensor_resistance_ohm = 10450.0;
        channel.kp = 120;
        channel.ki = 30;
        channel.kd = 5;
        channel.sensor_model = 0;
    }
}

} // namespace VaporView::Ground::Devices
