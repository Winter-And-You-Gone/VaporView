#ifndef VaporView_DATA_TYPES_H
#define VaporView_DATA_TYPES_H

#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace VaporView
{

enum class ImuFrameType : uint8_t
{
  Unknown = 0,
  HI81 = 0x81,
  HI83 = 0x83,
  HI91 = 0x91,
  HI92 = 0x92
};

struct GnssData
{
  double latitude = 0.0;
  double longitude = 0.0;
  double altitude = 0.0;

  double vel_north = 0.0;
  double vel_east = 0.0;
  double vel_down = 0.0;
  double vel_ground = 0.0;
  double heading = 0.0;
  double heading_pitch = 0.0;
  double heading_length = 0.0;
  std::string heading_type;
  int heading_trackedsvs = 0;
  int heading_solnsvs = 0;
  int heading_ggl1 = 0;
  int heading_ggl1l2 = 0;

  double sigma_lat = 0.0;
  double sigma_lon = 0.0;
  double sigma_alt = 0.0;

  std::string position_status;
  int num_satellites_used = 0;
  int num_satellites_tracked = 0;

  double gdop = 0.0;
  double pdop = 0.0;
  double hdop = 0.0;
  double htdop = 0.0;
  double tdop = 0.0;
  double diff_age = 0.0;
  double undulation = 0.0;
  double elevation_cutoff = 0.0;

  std::chrono::steady_clock::time_point timestamp{};
  bool valid = false;
  std::string error_message;
  std::string raw_sentence;
};

struct ImuData
{
  std::array<double, 3> acceleration{};
  std::array<double, 3> gyroscope{};
  std::array<double, 3> rpy{};
  std::array<double, 4> quaternion{};

  double temperature = std::numeric_limits<double>::quiet_NaN();
  double air_pressure = std::numeric_limits<double>::quiet_NaN();

  uint64_t system_time_us = 0;
  uint32_t system_time_ms = 0;

  bool from_hi83 = false;
  ImuFrameType frame_type = ImuFrameType::Unknown;

  std::chrono::steady_clock::time_point timestamp{};
  bool valid = false;
  std::string error_message;
  std::string raw_sentence;
};

struct EpsilonData
{
  double latitude_deg = 0.0;
  double longitude_deg = 0.0;
  double height_m = 0.0;

  double ecef_x_m = 0.0;
  double ecef_y_m = 0.0;
  double ecef_z_m = 0.0;

  double ned_n_m = 0.0;
  double ned_e_m = 0.0;
  double ned_d_m = 0.0;

  double vel_n_mps = 0.0;
  double vel_e_mps = 0.0;
  double vel_d_mps = 0.0;

  double body_vel_x_mps = 0.0;
  double body_vel_y_mps = 0.0;
  double body_vel_z_mps = 0.0;

  double body_acc_x_mps2 = 0.0;
  double body_acc_y_mps2 = 0.0;
  double body_acc_z_mps2 = 0.0;

  double imu_acc_x_mps2 = 0.0;
  double imu_acc_y_mps2 = 0.0;
  double imu_acc_z_mps2 = 0.0;

  double imu_gyr_x_radps = 0.0;
  double imu_gyr_y_radps = 0.0;
  double imu_gyr_z_radps = 0.0;

  double ang_vel_x_radps = 0.0;
  double ang_vel_y_radps = 0.0;
  double ang_vel_z_radps = 0.0;

  double mag_x_mg = 0.0;
  double mag_y_mg = 0.0;
  double mag_z_mg = 0.0;

  double roll_deg = 0.0;
  double pitch_deg = 0.0;
  double yaw_deg = 0.0;

  double quat_w = 0.0;
  double quat_x = 0.0;
  double quat_y = 0.0;
  double quat_z = 0.0;

  double imu_temp_c = std::numeric_limits<double>::quiet_NaN();
  double pressure_pa = std::numeric_limits<double>::quiet_NaN();
  double pressure_temp_c = std::numeric_limits<double>::quiet_NaN();
  double pressure_altitude_m = std::numeric_limits<double>::quiet_NaN();

  double hdop = 0.0;
  double vdop = 0.0;
  double hacc_m = 0.0;
  double vacc_m = 0.0;
  double lat_std_m = 0.0;
  double lon_std_m = 0.0;
  double height_std_m = 0.0;
  double diff_age_s = 0.0;

  uint64_t device_timestamp_us = 0;
  uint64_t utc_unix_s = 0;
  uint32_t utc_microseconds = 0;

  uint16_t system_status_bits = 0;
  uint16_t filter_status_bits = 0;
  uint16_t update_status_bits = 0;

  int gnss_fix_code = 0;
  int gnss_satellites = 0;
  bool heading_valid = false;

  uint64_t raw_frame_count = 0;
  uint64_t dropped_frame_count = 0;
  uint8_t last_packet_id = 0;
  uint8_t last_serial_number = 0;
  double imu_packet_rate_hz = 0.0;
  double ahrs_packet_rate_hz = 0.0;
  double insgps_packet_rate_hz = 0.0;
  double sys_state_packet_rate_hz = 0.0;
  double raw_gnss_packet_rate_hz = 0.0;
  double satellite_packet_rate_hz = 0.0;
  double geodetic_packet_rate_hz = 0.0;
  double ecef_packet_rate_hz = 0.0;

  std::chrono::steady_clock::time_point timestamp{};
  bool valid = false;
  std::string gnss_fix_text;
  std::string error_message;
};

struct PtbData
{
  double pressure_hpa = 0.0;

  std::chrono::steady_clock::time_point timestamp{};
  bool valid = false;
  std::string error_message;
};

struct HmpData
{
  double humidity = 0.0;
  double temperature = 0.0;

  std::chrono::steady_clock::time_point timestamp{};
  bool valid = false;
  std::string error_message;
};

struct LidarData
{
  double distance_m = 0.0;
  uint16_t signal_strength = 0;

  std::chrono::steady_clock::time_point timestamp{};
  bool valid = false;
  std::string error_message;
};

struct TemperatureControllerChannelData
{
  double target_temperature_c = std::numeric_limits<double>::quiet_NaN();
  double measured_temperature_c = std::numeric_limits<double>::quiet_NaN();
  double output_percent = std::numeric_limits<double>::quiet_NaN();
  double output_current_a = std::numeric_limits<double>::quiet_NaN();
  int output_mode = 0;
  bool output_enabled = false;
  int max_output_percent = 0;
  int auto_pid_mode = 0;
  int kp = 0;
  int ki = 0;
  int kd = 0;
};

struct TemperatureControllerData
{
  std::array<TemperatureControllerChannelData, 2> channels{};
  double internal_temperature_c = std::numeric_limits<double>::quiet_NaN();
  uint16_t error_code = 0;
  int controller_mode = 0;
  int device_address = 1;
  int rs485_baud_index = 1;
  int overtemp_output_mode = 1;

  std::chrono::steady_clock::time_point timestamp{};
  bool valid = false;
  std::string error_message;
};

}

#endif

