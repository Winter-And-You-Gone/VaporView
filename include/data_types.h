#ifndef VaporView_DATA_TYPES_H
#define VaporView_DATA_TYPES_H

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

namespace VaporView
{

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

  double temperature = 0.0;
  double air_pressure = 0.0;

  uint64_t system_time_us = 0;
  uint32_t system_time_ms = 0;

  bool from_hi83 = false;

  std::chrono::steady_clock::time_point timestamp{};
  bool valid = false;
  std::string error_message;
  std::string raw_sentence;
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

}

#endif

