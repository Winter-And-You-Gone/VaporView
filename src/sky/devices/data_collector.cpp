#include "data_collector.h"
#include "geo/CoordinateTransform.h"
#include "geo/GeoTypes.h"
#include "TemperatureControllerProtocol.h"
#include "hipnuc_dec.h"
#include "pvtsln_data.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <regex>
#include <sstream>
#include <utility>
#include <vector>

namespace VaporView
{
namespace
{
constexpr size_t kGnssLineBufferMaxBytes = 64 * 1024;
constexpr size_t kGnssLineBufferKeepBytes = 1024;
constexpr size_t kPtbLineBufferMaxBytes = 4096;
constexpr size_t kPtbLineBufferKeepBytes = 128;

void sleepMs(int ms)
{
  if (ms > 0)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  }
}

std::string formatDetectionProgress(const char* action, int attempt, int total_attempts, double remaining_seconds)
{
  std::ostringstream oss;
  oss << "\r正在" << action << "，第" << attempt << "/" << total_attempts
      << "轮，" << std::fixed << std::setprecision(2) << remaining_seconds << "秒";
  return oss.str();
}

double computeRemainingSeconds(const std::chrono::steady_clock::time_point& start_time, int timeout_ms)
{
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start_time).count();
  const auto remaining_ms = std::max<int64_t>(0, static_cast<int64_t>(timeout_ms) - elapsed_ms);
  return remaining_ms / 1000.0;
}

size_t trimUnterminatedLineBuffer(std::string& buffer, size_t maxBytes, size_t keepBytes)
{
  if (buffer.size() <= maxBytes)
  {
    return 0;
  }

  const size_t bytesToKeep = std::min(buffer.size(), keepBytes);
  const size_t bytesToDrop = buffer.size() - bytesToKeep;
  buffer.erase(0, bytesToDrop);
  return bytesToDrop;
}

uint16_t modbusCrc16Local(const uint8_t* data, size_t len)
{
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++)
  {
    crc ^= data[i];
    for (int j = 0; j < 8; j++)
    {
      if (crc & 0x0001)
      {
        crc >>= 1;
        crc ^= 0xA001;
      }
      else
      {
        crc >>= 1;
      }
    }
  }
  return crc;
}

float decodeFloatLELocal(uint16_t reg0, uint16_t reg1)
{
  uint32_t val = static_cast<uint32_t>(reg0) | (static_cast<uint32_t>(reg1) << 16);
  float result;
  std::memcpy(&result, &val, sizeof(float));
  return result;
}

bool isSupportedImuMessageType(const std::string& message_type)
{
  return message_type == "HI91" || message_type == "HI92";
}

bool imuSampleRateToPeriod(int hz, double& period_seconds)
{
  switch (hz)
  {
  case 1: period_seconds = 1.0; return true;
  case 2: period_seconds = 0.5; return true;
  case 5: period_seconds = 0.2; return true;
  case 10: period_seconds = 0.1; return true;
  case 20: period_seconds = 0.05; return true;
  case 50: period_seconds = 0.02; return true;
  case 100: period_seconds = 0.01; return true;
  case 200: period_seconds = 0.005; return true;
  case 250: period_seconds = 0.004; return true;
  case 500: period_seconds = 0.002; return true;
  case 1000: period_seconds = 0.001; return true;
  default:
    return false;
  }
}

uint64_t systemTimestampUs()
{
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());
}

constexpr uint8_t kFdilinkFrameHead = 0xFC;
constexpr uint8_t kFdilinkFrameTail = 0xFD;
constexpr double kRadToDeg = 57.295779513082320876798154814105;
constexpr int kEpsilonDefaultBaud = 921600;

constexpr uint8_t kMsgImu = 0x40;
constexpr uint8_t kMsgAhrs = 0x41;
constexpr uint8_t kMsgInsGps = 0x42;
constexpr uint8_t kMsgSystemState = 0x50;
constexpr uint8_t kMsgUnixTime = 0x51;
constexpr uint8_t kMsgFormattedTime = 0x52;
constexpr uint8_t kMsgStatus = 0x53;
constexpr uint8_t kMsgRawGnss = 0x59;
constexpr uint8_t kMsgSatellites = 0x5A;
constexpr int kTemperatureControllerModbusCommandGapMs = 5;
constexpr uint8_t kMsgGeodeticPos = 0x5C;
constexpr uint8_t kMsgEcefPos = 0x5D;
constexpr uint8_t kMsgEulerOrien = 0x63;
constexpr uint8_t kMsgQuatOrien = 0x64;
constexpr uint8_t kMsgMainMavlinkTunnel = 0xF0;

constexpr uint8_t kMavlinkV1Stx = 0xFE;
constexpr uint8_t kMavlinkMsgHeartbeat = 0;
constexpr uint8_t kMavlinkMsgSysStatus = 1;
constexpr uint8_t kMavlinkMsgGpsRawInt = 24;
constexpr uint8_t kMavlinkMsgAttitude = 30;
constexpr uint8_t kMavlinkMsgLocalPositionNed = 32;
constexpr uint8_t kMavlinkMsgGlobalPositionInt = 33;
constexpr uint8_t kMavlinkMsgFdiTelemetryF = 150;

constexpr uint16_t kFdiTelemetrySystemsAndClock = 1156;

constexpr int kAqmavDatasetGps = 4;
constexpr int kAqmavDatasetUkf = 5;
constexpr int kAqmavDatasetImu = 12;
constexpr int kAqmavDatasetImuRaw = 16;
constexpr int kAqmavDatasetSystemsAndClock = 23;

uint8_t fdilinkCrc8(const uint8_t* data, size_t len)
{
  static const uint8_t table[] = {
      0, 94, 188, 226, 97, 63, 221, 131, 194, 156, 126, 32, 163, 253, 31, 65,
      157, 195, 33, 127, 252, 162, 64, 30, 95, 1, 227, 189, 62, 96, 130, 220,
      35, 125, 159, 193, 66, 28, 254, 160, 225, 191, 93, 3, 128, 222, 60, 98,
      190, 224, 2, 92, 223, 129, 99, 61, 124, 34, 192, 158, 29, 67, 161, 255,
      70, 24, 250, 164, 39, 121, 155, 197, 132, 218, 56, 102, 229, 187, 89, 7,
      219, 133, 103, 57, 186, 228, 6, 88, 25, 71, 165, 251, 120, 38, 196, 154,
      101, 59, 217, 135, 4, 90, 184, 230, 167, 249, 27, 69, 198, 152, 122, 36,
      248, 166, 68, 26, 153, 199, 37, 123, 58, 100, 134, 216, 91, 5, 231, 185,
      140, 210, 48, 110, 237, 179, 81, 15, 78, 16, 242, 172, 47, 113, 147, 205,
      17, 79, 173, 243, 112, 46, 204, 146, 211, 141, 111, 49, 178, 236, 14, 80,
      175, 241, 19, 77, 206, 144, 114, 44, 109, 51, 209, 143, 12, 82, 176, 238,
      50, 108, 142, 208, 83, 13, 239, 177, 240, 174, 76, 18, 145, 207, 45, 115,
      202, 148, 118, 40, 171, 245, 23, 73, 8, 86, 180, 234, 105, 55, 213, 139,
      87, 9, 235, 181, 54, 104, 138, 212, 149, 203, 41, 119, 244, 170, 72, 22,
      233, 183, 85, 11, 136, 214, 52, 106, 43, 117, 151, 201, 74, 20, 246, 168,
      116, 42, 200, 150, 21, 75, 169, 247, 182, 232, 10, 84, 215, 137, 107, 53};
  uint8_t crc = 0;
  for (size_t i = 0; i < len; ++i)
  {
    crc = table[crc ^ data[i]];
  }
  return crc;
}

uint16_t fdilinkCrc16(const uint8_t* data, size_t len)
{
  static const uint16_t table[256] = {
      0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
      0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
      0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
      0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
      0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
      0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
      0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
      0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
      0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
      0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
      0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
      0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
      0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
      0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
      0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
      0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
      0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
      0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
      0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
      0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
      0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
      0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
      0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
      0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
      0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
      0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
      0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
      0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
      0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
      0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
      0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
      0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0};
  uint16_t crc = 0;
  for (size_t i = 0; i < len; ++i)
  {
    crc = table[((crc >> 8) ^ data[i]) & 0xFFu] ^ static_cast<uint16_t>(crc << 8);
  }
  return crc;
}

uint16_t readU16LE(const uint8_t* data)
{
  return static_cast<uint16_t>(data[0]) |
      (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t readU32LE(const uint8_t* data)
{
  return static_cast<uint32_t>(data[0]) |
      (static_cast<uint32_t>(data[1]) << 8) |
      (static_cast<uint32_t>(data[2]) << 16) |
      (static_cast<uint32_t>(data[3]) << 24);
}

int16_t readI16LE(const uint8_t* data)
{
  return static_cast<int16_t>(readU16LE(data));
}

int32_t readI32LE(const uint8_t* data)
{
  return static_cast<int32_t>(readU32LE(data));
}

uint64_t readU64LE(const uint8_t* data)
{
  uint64_t value = 0;
  for (int i = 0; i < 8; ++i)
  {
    value |= static_cast<uint64_t>(data[i]) << (8 * i);
  }
  return value;
}

int64_t readI64LE(const uint8_t* data)
{
  uint64_t value = 0;
  for (int i = 0; i < 8; ++i)
  {
    value |= static_cast<uint64_t>(data[i]) << (8 * i);
  }
  return static_cast<int64_t>(value);
}

float readFloatLE(const uint8_t* data)
{
  float value = 0.0f;
  std::memcpy(&value, data, sizeof(float));
  return value;
}

double readDoubleLE(const uint8_t* data)
{
  double value = 0.0;
  std::memcpy(&value, data, sizeof(double));
  return value;
}

double radToDeg(double radians)
{
  return radians * kRadToDeg;
}

bool finiteEuler(double rollDeg, double pitchDeg, double yawDeg)
{
  return std::isfinite(rollDeg) && std::isfinite(pitchDeg) && std::isfinite(yawDeg);
}

bool finiteQuaternion(double w, double x, double y, double z)
{
  if (!std::isfinite(w) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
  {
    return false;
  }
  const double norm = std::hypot(std::hypot(w, x), std::hypot(y, z));
  return std::isfinite(norm) && norm > 1.0e-6;
}

void quaternionToEulerDeg(double quatW,
                          double quatX,
                          double quatY,
                          double quatZ,
                          double& rollDeg,
                          double& pitchDeg,
                          double& yawDeg)
{
  const double norm = std::hypot(std::hypot(quatW, quatX), std::hypot(quatY, quatZ));
  if (!std::isfinite(norm) || norm <= 1.0e-6)
  {
    const double invalid = std::numeric_limits<double>::quiet_NaN();
    rollDeg = invalid;
    pitchDeg = invalid;
    yawDeg = invalid;
    return;
  }

  const double w = quatW / norm;
  const double x = quatX / norm;
  const double y = quatY / norm;
  const double z = quatZ / norm;
  rollDeg = radToDeg(std::atan2(2.0 * (w * x + y * z),
                                1.0 - 2.0 * (x * x + y * y)));
  pitchDeg = radToDeg(std::asin(std::clamp(2.0 * (w * y - z * x), -1.0, 1.0)));
  yawDeg = radToDeg(std::atan2(2.0 * (w * z + x * y),
                               1.0 - 2.0 * (y * y + z * z)));
}

double attitudeAngleDeltaDeg(double aDeg, double bDeg)
{
  if (!std::isfinite(aDeg) || !std::isfinite(bDeg))
  {
    return std::numeric_limits<double>::quiet_NaN();
  }
  double delta = std::fmod(std::abs(aDeg - bDeg), 360.0);
  if (delta > 180.0)
  {
    delta = 360.0 - delta;
  }
  return delta;
}

double attitudeEulerDeltaDeg(double rollA,
                             double pitchA,
                             double yawA,
                             double rollB,
                             double pitchB,
                             double yawB)
{
  if (!finiteEuler(rollA, pitchA, yawA) || !finiteEuler(rollB, pitchB, yawB))
  {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return std::max({attitudeAngleDeltaDeg(rollA, rollB),
                   attitudeAngleDeltaDeg(pitchA, pitchB),
                   attitudeAngleDeltaDeg(yawA, yawB)});
}

void updateEpsilonAttitudeState(EpsilonData& data)
{
  const double invalid = std::numeric_limits<double>::quiet_NaN();

  const bool hasAhrsEuler = data.ahrs_attitude_valid &&
      finiteEuler(data.ahrs_roll_deg, data.ahrs_pitch_deg, data.ahrs_yaw_deg);
  const bool hasAhrsQuat = data.ahrs_attitude_valid &&
      finiteQuaternion(data.ahrs_quat_w, data.ahrs_quat_x, data.ahrs_quat_y, data.ahrs_quat_z);
  const bool hasEulerOrien = data.euler_orien_valid &&
      finiteEuler(data.euler_orien_roll_deg, data.euler_orien_pitch_deg, data.euler_orien_yaw_deg);
  const bool hasQuatOrien = data.quat_orien_valid &&
      finiteQuaternion(data.quat_orien_w, data.quat_orien_x, data.quat_orien_y, data.quat_orien_z);
  const bool hasQuatOrienEuler = data.quat_orien_valid &&
      finiteEuler(data.quat_orien_roll_deg, data.quat_orien_pitch_deg, data.quat_orien_yaw_deg);

  data.attitude_source_count = 0;
  if (data.ahrs_attitude_valid) ++data.attitude_source_count;
  if (data.euler_orien_valid) ++data.attitude_source_count;
  if (data.quat_orien_valid) ++data.attitude_source_count;

  if (hasQuatOrien)
  {
    data.quat_w = data.quat_orien_w;
    data.quat_x = data.quat_orien_x;
    data.quat_y = data.quat_orien_y;
    data.quat_z = data.quat_orien_z;
  }
  else if (hasAhrsQuat)
  {
    data.quat_w = data.ahrs_quat_w;
    data.quat_x = data.ahrs_quat_x;
    data.quat_y = data.ahrs_quat_y;
    data.quat_z = data.ahrs_quat_z;
  }

  if (hasEulerOrien)
  {
    data.roll_deg = data.euler_orien_roll_deg;
    data.pitch_deg = data.euler_orien_pitch_deg;
    data.yaw_deg = data.euler_orien_yaw_deg;
  }
  else if (hasAhrsEuler)
  {
    data.roll_deg = data.ahrs_roll_deg;
    data.pitch_deg = data.ahrs_pitch_deg;
    data.yaw_deg = data.ahrs_yaw_deg;
  }
  else if (hasQuatOrienEuler)
  {
    data.roll_deg = data.quat_orien_roll_deg;
    data.pitch_deg = data.quat_orien_pitch_deg;
    data.yaw_deg = data.quat_orien_yaw_deg;
  }

  data.attitude_delta_ahrs_euler_deg =
      hasAhrsEuler && hasEulerOrien
          ? attitudeEulerDeltaDeg(data.ahrs_roll_deg,
                                  data.ahrs_pitch_deg,
                                  data.ahrs_yaw_deg,
                                  data.euler_orien_roll_deg,
                                  data.euler_orien_pitch_deg,
                                  data.euler_orien_yaw_deg)
          : invalid;
  data.attitude_delta_ahrs_quat_deg =
      hasAhrsEuler && hasQuatOrienEuler
          ? attitudeEulerDeltaDeg(data.ahrs_roll_deg,
                                  data.ahrs_pitch_deg,
                                  data.ahrs_yaw_deg,
                                  data.quat_orien_roll_deg,
                                  data.quat_orien_pitch_deg,
                                  data.quat_orien_yaw_deg)
          : invalid;
  data.attitude_delta_euler_quat_deg =
      hasEulerOrien && hasQuatOrienEuler
          ? attitudeEulerDeltaDeg(data.euler_orien_roll_deg,
                                  data.euler_orien_pitch_deg,
                                  data.euler_orien_yaw_deg,
                                  data.quat_orien_roll_deg,
                                  data.quat_orien_pitch_deg,
                                  data.quat_orien_yaw_deg)
          : invalid;

  data.attitude_delta_max_deg = invalid;
  for (double delta : {data.attitude_delta_ahrs_euler_deg,
                      data.attitude_delta_ahrs_quat_deg,
                      data.attitude_delta_euler_quat_deg})
  {
    if (std::isfinite(delta))
    {
      data.attitude_delta_max_deg = std::isfinite(data.attitude_delta_max_deg)
          ? std::max(data.attitude_delta_max_deg, delta)
          : delta;
    }
  }
}

bool resolveEpsilonEcefFromLlh(EpsilonData& data, bool hasResolvedLlh)
{
  if (VaporView::Geo::isPlausibleEcef(data.ecef_x_m, data.ecef_y_m, data.ecef_z_m))
  {
    return true;
  }
  if (!hasResolvedLlh)
  {
    return false;
  }

  VaporView::Geo::EcefPoint derived;
  if (!VaporView::Geo::deriveEcefFromLlh(data.latitude_deg, data.longitude_deg, data.height_m, derived))
  {
    return false;
  }
  data.ecef_x_m = derived.xM;
  data.ecef_y_m = derived.yM;
  data.ecef_z_m = derived.zM;
  return true;
}

int64_t daysFromCivil(int year, unsigned month, unsigned day)
{
  year -= month <= 2 ? 1 : 0;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned monthPrime = month > 2 ? month - 3 : month + 9;
  const unsigned dayOfYear = (153 * monthPrime + 2) / 5 + day - 1;
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(dayOfEra) - 719468;
}

bool utcPartsToUnix(int year, int month, int day, int hour, int minute, double second,
    uint64_t& unixSeconds, uint32_t& microseconds)
{
  if (year >= 0 && year < 100)
  {
    year += 2000;
  }
  if (year < 1970 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31 ||
      hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0.0 || second >= 61.0)
  {
    return false;
  }

  const int wholeSecond = static_cast<int>(std::floor(second));
  const double fractionalSecond = second - wholeSecond;
  const int64_t days = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
  if (days < 0)
  {
    return false;
  }

  unixSeconds = static_cast<uint64_t>(days * 86400 + hour * 3600 + minute * 60 + wholeSecond);
  microseconds = static_cast<uint32_t>(std::lround(fractionalSecond * 1000000.0));
  if (microseconds >= 1000000u)
  {
    unixSeconds += 1;
    microseconds -= 1000000u;
  }
  return true;
}

std::string epsilonGnssFixName(int fix_code)
{
  switch (fix_code)
  {
  case 0: return "NO_GPS";
  case 1: return "NO_FIX";
  case 2: return "2D";
  case 3: return "3D";
  case 4: return "DGPS";
  case 5: return "RTK_FLOAT";
  case 6: return "RTK_FIXED";
  case 7: return "STATIC";
  case 8: return "PPP";
  case 9: return "RTK_DUAL";
  default: return "UNKNOWN";
  }
}

bool isSupportedEpsilonRate(int hz)
{
  return hz == 20 || hz == 50 || hz == 100 || hz == 200;
}

const std::vector<int>& supportedEpsilonPacketRates(uint8_t packet_id)
{
  static const std::vector<int> kCommonRates = {0, 1, 2, 5, 10, 20, 50, 100, 250, 500};
  static const std::vector<int> kImuRates = {0, 1, 2, 5, 10, 20, 50, 100, 200, 250, 500, 1000};
  return packet_id == kMsgImu ? kImuRates : kCommonRates;
}

bool isSupportedEpsilonPacketRate(uint8_t packet_id, int hz)
{
  const std::vector<int>& supported_rates = supportedEpsilonPacketRates(packet_id);
  return std::find(supported_rates.cbegin(), supported_rates.cend(), hz) != supported_rates.cend();
}

int nearestSupportedEpsilonPacketRate(uint8_t packet_id, int desired_hz)
{
  int fallback_hz = 0;
  for (int hz : supportedEpsilonPacketRates(packet_id))
  {
    if (hz == desired_hz)
    {
      return hz;
    }
    if (hz <= desired_hz)
    {
      fallback_hz = hz;
    }
  }
  return fallback_hz;
}

std::map<uint8_t, int> desiredEpsilonPacketRates(int hz)
{
  const int navLowRate = std::min(hz, 20);
  return {
      {kMsgImu, nearestSupportedEpsilonPacketRate(kMsgImu, hz)},
      {kMsgAhrs, nearestSupportedEpsilonPacketRate(kMsgAhrs, hz)},
      {kMsgInsGps, nearestSupportedEpsilonPacketRate(kMsgInsGps, hz)},
      {kMsgSystemState, nearestSupportedEpsilonPacketRate(kMsgSystemState, hz)},
      {kMsgStatus, nearestSupportedEpsilonPacketRate(kMsgStatus, navLowRate)},
      {kMsgRawGnss, navLowRate},
      {kMsgSatellites, navLowRate},
      {kMsgGeodeticPos, navLowRate},
      {kMsgEcefPos, navLowRate},
      {kMsgEulerOrien, nearestSupportedEpsilonPacketRate(kMsgEulerOrien, hz)},
      {kMsgQuatOrien, nearestSupportedEpsilonPacketRate(kMsgQuatOrien, hz)},
  };
}

std::string trimAscii(const std::string& input)
{
  const auto first = input.find_first_not_of(" \r\n\t");
  if (first == std::string::npos)
  {
    return std::string();
  }
  const auto last = input.find_last_not_of(" \r\n\t");
  return input.substr(first, last - first + 1);
}

std::map<uint8_t, int> parseFmsgResponse(const std::string& response)
{
  std::map<uint8_t, int> rates;
  std::istringstream stream(response);
  std::string line;
  const std::regex pattern(R"(\[([0-9A-Fa-f]{2})\]\s+(-?\d+(?:\.\d+)?)\s*Hz)", std::regex::icase);
  while (std::getline(stream, line))
  {
    std::smatch match;
    if (!std::regex_search(line, match, pattern))
    {
      continue;
    }
    const int packetId = std::stoi(match[1].str(), nullptr, 16);
    const double parsedRate = std::stod(match[2].str());
    const int rate = static_cast<int>(std::round(parsedRate));
    rates[static_cast<uint8_t>(packetId)] = std::max(0, rate);
  }
  return rates;
}

bool readValidFdilinkFrame(SerialPort& serial,
                           std::vector<uint8_t>& buffer,
                           std::vector<uint8_t>* frame,
                           int timeout_ms,
                           uint8_t* packet_id = nullptr,
                           uint8_t* serial_number = nullptr)
{
  buffer.clear();
  const auto start = std::chrono::steady_clock::now();
  uint8_t chunk[512];
  while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < timeout_ms)
  {
    const ssize_t readBytes = serial.read(chunk, sizeof(chunk));
    if (readBytes > 0)
    {
      buffer.insert(buffer.end(), chunk, chunk + readBytes);
      while (buffer.size() >= 8)
      {
        const auto it = std::find(buffer.begin(), buffer.end(), kFdilinkFrameHead);
        if (it == buffer.end())
        {
          buffer.clear();
          break;
        }
        if (it != buffer.begin())
        {
          buffer.erase(buffer.begin(), it);
        }
        if (buffer.size() < 8)
        {
          break;
        }
        const size_t payloadSize = buffer[2];
        const size_t frameSize = 8 + payloadSize;
        if (buffer.size() < frameSize)
        {
          break;
        }
        if (fdilinkCrc8(buffer.data(), 4) != buffer[4] ||
            fdilinkCrc16(buffer.data() + 7, payloadSize) !=
                static_cast<uint16_t>((static_cast<uint16_t>(buffer[5]) << 8) | buffer[6]) ||
            buffer[frameSize - 1] != kFdilinkFrameTail)
        {
          buffer.erase(buffer.begin());
          continue;
        }
        if (frame)
        {
          frame->assign(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(frameSize));
        }
        if (packet_id)
        {
          *packet_id = buffer[1];
        }
        if (serial_number)
        {
          *serial_number = buffer[3];
        }
        buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(frameSize));
        return true;
      }
    }
    sleepMs(20);
  }
  return false;
}

bool containsEpsilonAsciiAck(const std::string& text)
{
  return text.find("*#OK") != std::string::npos ||
      text.find("#OK") != std::string::npos ||
      text.find("ERROR") != std::string::npos ||
      text.find("error") != std::string::npos;
}

bool containsEpsilonAsciiOk(const std::string& text)
{
  return text.find("*#OK") != std::string::npos ||
      text.find("#OK") != std::string::npos;
}

std::string readPrintableSerialResponse(SerialPort& serial, int totalWaitMs, bool stopOnAck)
{
  std::string filtered;
  const auto start = std::chrono::steady_clock::now();
  auto lastDataTime = start;
  auto ackTime = start;
  bool sawAck = false;
  uint8_t chunk[256];

  while (true)
  {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
    const auto idleMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastDataTime).count();
    const auto ackAgeMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - ackTime).count();
    if (elapsedMs >= totalWaitMs &&
        (!stopOnAck || !sawAck || idleMs >= 120 || ackAgeMs >= 250))
    {
      break;
    }

    const ssize_t n = serial.read(chunk, sizeof(chunk));
    if (n > 0)
    {
      lastDataTime = std::chrono::steady_clock::now();
      for (ssize_t i = 0; i < n; ++i)
      {
        const char ch = static_cast<char>(chunk[i]);
        if (ch == '\r' || ch == '\n' || ch == '\t' || (ch >= 0x20 && ch <= 0x7E))
        {
          filtered.push_back(ch);
        }
      }
      if (stopOnAck && !sawAck && containsEpsilonAsciiAck(filtered))
      {
        sawAck = true;
        ackTime = std::chrono::steady_clock::now();
      }
      continue;
    }

    sleepMs(20);
  }

  return filtered;
}

using EpsilonLogFn = std::function<void(const std::string&)>;

std::string readLoggedEpsilonAsciiResponse(SerialPort& serial, const EpsilonLogFn& logFn, int totalWaitMs)
{
  const std::string filtered = readPrintableSerialResponse(serial, totalWaitMs, true);
  std::istringstream stream(filtered);
  std::string line;
  while (std::getline(stream, line))
  {
    const std::string trimmed = trimAscii(line);
    if (!trimmed.empty())
    {
      logFn("[EPSILON RX] " + trimmed);
    }
  }
  return filtered;
}

std::string sendLoggedEpsilonAsciiCommand(SerialPort& serial,
                                          const EpsilonLogFn& logFn,
                                          bool english,
                                          const std::string& command,
                                          int waitMs)
{
  logFn("[EPSILON TX] " + trimAscii(command));
  const ssize_t written = serial.write(command.c_str(), command.size());
  if (written != static_cast<ssize_t>(command.size()))
  {
    logFn(std::string(english ? "EPSILON: failed to send command: " : "EPSILON：命令发送失败：") +
          trimAscii(command));
    return std::string();
  }
  if (waitMs > 0)
  {
    sleepMs(waitMs);
  }
  const std::string response = waitMs > 0
      ? readLoggedEpsilonAsciiResponse(serial, logFn, std::max(180, waitMs))
      : std::string();
  const std::string trimmedCommand = trimAscii(command);
  const bool expectedPrompt = trimmedCommand == "#freboot" && response.find("(y/n)") != std::string::npos;
  const bool expectedFmsgList = trimmedCommand == "#fmsg" && response.find("MSG_") != std::string::npos;
  if (command != "y\r\n" && command != "Y\r\n" && !expectedPrompt &&
      !expectedFmsgList && !containsEpsilonAsciiAck(response))
  {
    logFn(std::string(english
                          ? "EPSILON: no explicit ASCII acknowledgement for command: "
                          : "EPSILON：命令未收到明确 ASCII 确认：") +
          trimmedCommand);
  }
  return response;
}

bool rebootEpsilonAndReopenSerial(SerialPort& serial,
                                  const std::string& portName,
                                  const SerialConfig& config,
                                  const EpsilonLogFn& logFn,
                                  bool english)
{
  if (portName.empty())
  {
    logFn(english
              ? "EPSILON: cannot reopen after reboot because the port name is unknown"
              : "EPSILON：串口名未知，重启后无法重新打开串口");
    return false;
  }

  const std::string rebootResponse = sendLoggedEpsilonAsciiCommand(serial, logFn, english, "#freboot\r\n", 2000);
  if (rebootResponse.find("(y/n)") == std::string::npos)
  {
    logFn(english
              ? "EPSILON: reboot command did not return the confirmation prompt"
              : "EPSILON：重启命令没有返回确认提示");
    return false;
  }

  sendLoggedEpsilonAsciiCommand(serial, logFn, english, "y\r\n", 0);
  sleepMs(150);

  serial.close();
  logFn(english
            ? "EPSILON: reboot confirmed; waiting for the serial port to return"
            : "EPSILON：已确认重启，等待串口恢复");
  sleepMs(4000);

  constexpr int kReopenAttempts = 20;
  for (int attempt = 1; attempt <= kReopenAttempts; ++attempt)
  {
    if (serial.open(portName, config))
    {
      serial.flush();
      sleepMs(500);
      logFn(english
                ? "EPSILON: serial port reopened after device reboot"
                : "EPSILON：设备重启后已重新打开串口");
      return true;
    }
    sleepMs(500);
  }

  logFn(english
            ? "EPSILON: failed to reopen serial port after device reboot"
            : "EPSILON：设备重启后重新打开串口失败");
  return false;
}

bool waitForEpsilonNavigationStreamRestore(SerialPort& serial,
                                           const EpsilonLogFn& logFn,
                                           int timeoutMs,
                                           const char* successMessage,
                                           const char* failureMessage)
{
  std::vector<uint8_t> frameBuffer;
  std::vector<uint8_t> frame;
  uint8_t packetId = 0;
  if (!readValidFdilinkFrame(serial, frameBuffer, &frame, timeoutMs, &packetId, nullptr))
  {
    logFn(failureMessage);
    return false;
  }

  if (successMessage && std::strlen(successMessage) > 0)
  {
    if (std::string(successMessage).find("%u") != std::string::npos)
    {
      char buffer[256];
      std::snprintf(buffer, sizeof(buffer), successMessage, static_cast<unsigned>(packetId));
      logFn(buffer);
    }
    else
    {
      logFn(successMessage);
    }
  }
  return true;
}

int epsilonSerialBaudToParamValue(int baudRate)
{
  switch (baudRate)
  {
  case 9600: return 1;
  case 19200: return 2;
  case 38400: return 3;
  case 76800: return 4;
  case 115200: return 5;
  case 230400: return 6;
  case 460800: return 7;
  case 921600: return 8;
  case 2625000: return 9;
  case 5250000: return 10;
  case 10500000: return 11;
  default: return 0;
  }
}

const char* lidarProtocolName(LidarProtocol protocol)
{
  switch (protocol)
  {
  case LidarProtocol::TFA1500DistanceFrame:
    return "TFA1500-L";
  case LidarProtocol::TFA1500LowFrequencyFrame:
    return "TFA1500-L 低频";
  case LidarProtocol::TFA1500HighFrequency:
    return "TFA1500-L 高频";
  case LidarProtocol::ObservedAaB7Frame:
    return "AA-B7 激光测距帧";
  case LidarProtocol::Unknown:
  default:
    return "未知协议";
  }
}

constexpr uint8_t kTfa1500Header = 0x5C;
constexpr size_t kTfa1500FrameSize = 5;
constexpr uint8_t kTfa1500LowFrequencyHeader = 0x55;
constexpr size_t kTfa1500LowFrequencyMinFrameSize = 6;
constexpr uint8_t kObservedAaHeader = 0xAA;
constexpr uint8_t kObservedB7Type = 0xB7;
constexpr uint8_t kObservedBbTail = 0xBB;
constexpr size_t kObservedAaB7FrameSize = 10;

bool parseTfa1500FrameLocal(const uint8_t* frame, size_t size, LidarData& sample)
{
  if (!frame || size < kTfa1500FrameSize || frame[0] != kTfa1500Header)
  {
    return false;
  }

  uint8_t sum = 0;
  for (size_t i = 1; i < 4; ++i)
  {
    sum = static_cast<uint8_t>(sum + frame[i]);
  }
  const uint8_t checksum = static_cast<uint8_t>(~sum);
  if (checksum != frame[4])
  {
    return false;
  }

  const uint32_t distance_cm =
      static_cast<uint32_t>(frame[1]) |
      (static_cast<uint32_t>(frame[2]) << 8) |
      (static_cast<uint32_t>(frame[3]) << 16);
  const bool valid = distance_cm != 0x003FFFFFu;

  sample.distance_m = valid ? (static_cast<double>(distance_cm) / 100.0) : 0.0;
  sample.signal_strength = 0;
  sample.timestamp = std::chrono::steady_clock::now();
  sample.valid = valid && distance_cm > 0;
  sample.error_message = sample.valid ? "" : "未检测到目标或超出量程";
  return true;
}

bool parseTfa1500LowFrequencyFrameLocal(const uint8_t* frame, size_t size, LidarData& sample, size_t& consumed_size)
{
  consumed_size = 0;
  if (!frame || size < kTfa1500LowFrequencyMinFrameSize || frame[0] != kTfa1500LowFrequencyHeader)
  {
    return false;
  }

  const size_t payload_len = static_cast<size_t>(frame[2]);
  const size_t frame_len = 3 + payload_len + 1;
  if (frame_len < kTfa1500LowFrequencyMinFrameSize || size < frame_len)
  {
    return false;
  }

  uint8_t checksum = 0;
  for (size_t i = 0; i < frame_len - 1; ++i)
  {
    checksum ^= frame[i];
  }
  if (checksum != frame[frame_len - 1])
  {
    return false;
  }

  consumed_size = frame_len;

  if (frame[1] != 0x02 || payload_len < 7)
  {
    return false;
  }

  const uint32_t distance_dm =
      (static_cast<uint32_t>(frame[4]) << 16) |
      (static_cast<uint32_t>(frame[5]) << 8) |
      static_cast<uint32_t>(frame[6]);

  sample.distance_m = distance_dm / 10.0;
  sample.signal_strength = 0;
  sample.timestamp = std::chrono::steady_clock::now();
  sample.valid = distance_dm > 0;
  sample.error_message = sample.valid ? "" : "未检测到目标或超出量程";
  return true;
}

bool parseObservedAaB7FrameLocal(const uint8_t* frame, size_t size, LidarData& sample)
{
  if (!frame || size < kObservedAaB7FrameSize)
  {
    return false;
  }
  if (frame[0] != kObservedAaHeader || frame[1] != kObservedB7Type || frame[2] != kObservedAaB7FrameSize || frame[9] != kObservedBbTail)
  {
    return false;
  }

  const uint16_t distance_a = static_cast<uint16_t>(frame[3] << 8) | frame[4];
  const uint16_t distance_b = static_cast<uint16_t>(frame[5] << 8) | frame[6];
  if (distance_a != distance_b)
  {
    return false;
  }

  sample.distance_m = distance_a / 100.0;
  sample.signal_strength = frame[8];
  sample.timestamp = std::chrono::steady_clock::now();
  sample.valid = distance_a > 0;
  sample.error_message = sample.valid ? "" : "未检测到目标或超出量程";
  return true;
}

bool extractNextTfa1500Sample(std::vector<uint8_t>& buffer, LidarData& sample, std::vector<uint8_t>* raw_frame = nullptr)
{
  while (buffer.size() >= kTfa1500FrameSize)
  {
    auto header = std::find(buffer.begin(), buffer.end(), kTfa1500Header);
    if (header == buffer.end())
    {
      buffer.clear();
      return false;
    }
    if (std::distance(header, buffer.end()) < static_cast<std::ptrdiff_t>(kTfa1500FrameSize))
    {
      buffer.erase(buffer.begin(), header);
      return false;
    }
    if (parseTfa1500FrameLocal(&(*header), kTfa1500FrameSize, sample))
    {
      if (raw_frame)
      {
        raw_frame->assign(header, header + kTfa1500FrameSize);
      }
      buffer.erase(buffer.begin(), header + kTfa1500FrameSize);
      return true;
    }
    buffer.erase(buffer.begin(), header + 1);
  }
  return false;
}

bool extractNextTfa1500LowFrequencySample(std::vector<uint8_t>& buffer, LidarData& sample, std::vector<uint8_t>* raw_frame = nullptr)
{
  while (buffer.size() >= kTfa1500LowFrequencyMinFrameSize)
  {
    auto header = std::find(buffer.begin(), buffer.end(), kTfa1500LowFrequencyHeader);
    if (header == buffer.end())
    {
      buffer.clear();
      return false;
    }

    std::vector<uint8_t>::difference_type remaining = std::distance(header, buffer.end());
    if (remaining < static_cast<std::ptrdiff_t>(kTfa1500LowFrequencyMinFrameSize))
    {
      buffer.erase(buffer.begin(), header);
      return false;
    }

    const size_t payload_len = static_cast<size_t>(*(header + 2));
    const size_t frame_len = 3 + payload_len + 1;
    if (frame_len < kTfa1500LowFrequencyMinFrameSize)
    {
      buffer.erase(buffer.begin(), header + 1);
      continue;
    }
    if (remaining < static_cast<std::ptrdiff_t>(frame_len))
    {
      buffer.erase(buffer.begin(), header);
      return false;
    }

    size_t consumed_size = 0;
    if (parseTfa1500LowFrequencyFrameLocal(&(*header), static_cast<size_t>(remaining), sample, consumed_size))
    {
      if (raw_frame)
      {
        raw_frame->assign(header, header + consumed_size);
      }
      buffer.erase(buffer.begin(), header + consumed_size);
      return true;
    }

    if (consumed_size > 0)
    {
      buffer.erase(buffer.begin(), header + consumed_size);
      continue;
    }

    buffer.erase(buffer.begin(), header + 1);
  }
  return false;
}

bool extractNextObservedAaB7Sample(std::vector<uint8_t>& buffer, LidarData& sample, std::vector<uint8_t>* raw_frame = nullptr)
{
  while (buffer.size() >= kObservedAaB7FrameSize)
  {
    auto header = std::find(buffer.begin(), buffer.end(), kObservedAaHeader);
    if (header == buffer.end())
    {
      buffer.clear();
      return false;
    }

    if (std::distance(header, buffer.end()) < static_cast<std::ptrdiff_t>(kObservedAaB7FrameSize))
    {
      buffer.erase(buffer.begin(), header);
      return false;
    }

    if (parseObservedAaB7FrameLocal(&(*header), static_cast<size_t>(std::distance(header, buffer.end())), sample))
    {
      if (raw_frame)
      {
        raw_frame->assign(header, header + kObservedAaB7FrameSize);
      }
      buffer.erase(buffer.begin(), header + kObservedAaB7FrameSize);
      return true;
    }

    buffer.erase(buffer.begin(), header + 1);
  }
  return false;
}

bool extractNextLidarSample(std::vector<uint8_t>& buffer,
                            LidarProtocol protocol_hint,
                            LidarData& sample,
                            LidarProtocol& detected_protocol,
                            std::vector<uint8_t>* raw_frame = nullptr)
{
  if (protocol_hint == LidarProtocol::TFA1500HighFrequency)
  {
    if (extractNextTfa1500Sample(buffer, sample, raw_frame))
    {
      detected_protocol = LidarProtocol::TFA1500HighFrequency;
      return true;
    }
    return false;
  }

  if (protocol_hint == LidarProtocol::TFA1500DistanceFrame)
  {
    if (extractNextTfa1500Sample(buffer, sample, raw_frame))
    {
      detected_protocol = LidarProtocol::TFA1500DistanceFrame;
      return true;
    }
    return false;
  }

  if (protocol_hint == LidarProtocol::TFA1500LowFrequencyFrame)
  {
    if (extractNextTfa1500LowFrequencySample(buffer, sample, raw_frame))
    {
      detected_protocol = LidarProtocol::TFA1500LowFrequencyFrame;
      return true;
    }
    return false;
  }

  if (protocol_hint == LidarProtocol::ObservedAaB7Frame)
  {
    if (extractNextObservedAaB7Sample(buffer, sample, raw_frame))
    {
      detected_protocol = LidarProtocol::ObservedAaB7Frame;
      return true;
    }
    return false;
  }

  while (!buffer.empty())
  {
    auto tfa = std::find(buffer.begin(), buffer.end(), kTfa1500Header);
    auto tfa_low = std::find(buffer.begin(), buffer.end(), kTfa1500LowFrequencyHeader);
    auto aa_b7 = std::find(buffer.begin(), buffer.end(), kObservedAaHeader);

    if (tfa == buffer.end() && tfa_low == buffer.end() && aa_b7 == buffer.end())
    {
      buffer.clear();
      return false;
    }

    auto earliest = buffer.end();
    if (tfa != buffer.end() && (earliest == buffer.end() || tfa < earliest)) earliest = tfa;
    if (tfa_low != buffer.end() && (earliest == buffer.end() || tfa_low < earliest)) earliest = tfa_low;
    if (aa_b7 != buffer.end() && (earliest == buffer.end() || aa_b7 < earliest)) earliest = aa_b7;

    auto sync_failed_candidate = [&buffer](std::vector<uint8_t>::iterator candidate,
                                           const std::vector<uint8_t>& parsed_suffix) {
      const size_t candidate_offset = static_cast<size_t>(std::distance(buffer.begin(), candidate));
      const size_t original_suffix_size = buffer.size() - candidate_offset;
      if (parsed_suffix.size() < original_suffix_size)
      {
        buffer.assign(parsed_suffix.begin(), parsed_suffix.end());
        return true;
      }
      if (candidate == buffer.begin())
      {
        return false;
      }
      buffer.erase(buffer.begin(), candidate);
      return true;
    };

    if (earliest == tfa)
    {
      std::vector<uint8_t> slice(tfa, buffer.end());
      if (extractNextTfa1500Sample(slice, sample, raw_frame))
      {
        detected_protocol = LidarProtocol::TFA1500DistanceFrame;
        buffer.assign(slice.begin(), slice.end());
        return true;
      }

      if (!sync_failed_candidate(tfa, slice))
      {
        return false;
      }
      continue;
    }

    if (earliest == tfa_low)
    {
      std::vector<uint8_t> slice(tfa_low, buffer.end());
      if (extractNextTfa1500LowFrequencySample(slice, sample, raw_frame))
      {
        detected_protocol = LidarProtocol::TFA1500LowFrequencyFrame;
        buffer.assign(slice.begin(), slice.end());
        return true;
      }

      if (!sync_failed_candidate(tfa_low, slice))
      {
        return false;
      }
      continue;
    }

    if (earliest == aa_b7)
    {
      std::vector<uint8_t> slice(aa_b7, buffer.end());
      if (extractNextObservedAaB7Sample(slice, sample, raw_frame))
      {
        detected_protocol = LidarProtocol::ObservedAaB7Frame;
        buffer.assign(slice.begin(), slice.end());
        return true;
      }

      if (!sync_failed_candidate(aa_b7, slice))
      {
        return false;
      }
      continue;
    }
    buffer.erase(buffer.begin(), earliest + 1);
  }

  return false;
}

enum class HmpParseResult
{
  None,
  Data,
  Exception,
  CrcError
};

HmpParseResult parseHmpResponse(const uint8_t* buffer,
                                size_t size,
                                float& humidity,
                                float& temperature,
                                uint8_t& exception_code,
                                uint8_t function_code,
                                size_t* frame_offset = nullptr,
                                size_t* frame_size = nullptr)
{
  constexpr uint8_t kSlaveAddr = 240;
  const uint8_t exception_fc = static_cast<uint8_t>(function_code | 0x80);

  for (size_t i = 0; i < size; ++i)
  {
    if (buffer[i] != kSlaveAddr)
    {
      continue;
    }

    if (i + 5 <= size && buffer[i + 1] == exception_fc)
    {
      uint16_t recv_crc = static_cast<uint16_t>(buffer[i + 3]) | (static_cast<uint16_t>(buffer[i + 4]) << 8);
      uint16_t calc_crc = modbusCrc16Local(buffer + i, 3);
      if (recv_crc == calc_crc)
      {
        if (frame_offset)
        {
          *frame_offset = i;
        }
        if (frame_size)
        {
          *frame_size = 5;
        }
        exception_code = buffer[i + 2];
        return HmpParseResult::Exception;
      }
      return HmpParseResult::CrcError;
    }

    if (i + 3 <= size && buffer[i + 1] == function_code)
    {
      uint8_t byte_count = buffer[i + 2];
      size_t frame_len = static_cast<size_t>(3 + byte_count + 2);
      if (i + frame_len > size)
      {
        continue;
      }

      uint16_t recv_crc = static_cast<uint16_t>(buffer[i + frame_len - 2]) |
                          (static_cast<uint16_t>(buffer[i + frame_len - 1]) << 8);
      uint16_t calc_crc = modbusCrc16Local(buffer + i, frame_len - 2);
      if (recv_crc != calc_crc)
      {
        return HmpParseResult::CrcError;
      }

      if (byte_count >= 8)
      {
        if (frame_offset)
        {
          *frame_offset = i;
        }
        if (frame_size)
        {
          *frame_size = frame_len;
        }
        uint16_t reg0 = static_cast<uint16_t>(buffer[i + 3] << 8) | buffer[i + 4];
        uint16_t reg1 = static_cast<uint16_t>(buffer[i + 5] << 8) | buffer[i + 6];
        uint16_t reg2 = static_cast<uint16_t>(buffer[i + 7] << 8) | buffer[i + 8];
        uint16_t reg3 = static_cast<uint16_t>(buffer[i + 9] << 8) | buffer[i + 10];

        humidity = decodeFloatLELocal(reg0, reg1);
        temperature = decodeFloatLELocal(reg2, reg3);
        return HmpParseResult::Data;
      }
    }
  }

  return HmpParseResult::None;
}

}

bool parseEnvironmentSerialLine(const std::string& line, EnvironmentSerialValues& values)
{
  static const std::regex temperature_pattern(
      R"((?:^|[^a-z])(?:t|temperature)\s*[:=]\s*([-+]?(?:\d+(?:\.\d*)?|\.\d+)))",
      std::regex::icase);
  static const std::regex humidity_pattern(
      R"((?:humidity|rh)\s*[:=]\s*([-+]?(?:\d+(?:\.\d*)?|\.\d+)))",
      std::regex::icase);
  static const std::regex pressure_pattern(
      R"((?:^|[^a-z])(?:p|pressure)\s*[:=]\s*([-+]?(?:\d+(?:\.\d*)?|\.\d+)))",
      std::regex::icase);

  auto parseMatch = [&line](const std::regex& pattern, double& output) {
    std::smatch match;
    if (!std::regex_search(line, match, pattern) || match.size() < 2)
    {
      return false;
    }
    try
    {
      output = std::stod(match[1].str());
      return std::isfinite(output);
    }
    catch (const std::exception&)
    {
      return false;
    }
  };

  bool matched = false;
  double value = 0.0;
  if (parseMatch(temperature_pattern, value) && value >= -80.0 && value <= 125.0)
  {
    values.has_temperature = true;
    values.temperature_c = value;
    matched = true;
  }
  if (parseMatch(humidity_pattern, value) && value >= 0.0 && value <= 100.0)
  {
    values.has_humidity = true;
    values.humidity_rh = value;
    matched = true;
  }
  if (parseMatch(pressure_pattern, value))
  {
    std::string lower = line;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
      return static_cast<char>(std::tolower(ch));
    });
    const double pressure_hpa = lower.find("hpa") != std::string::npos ? value : value / 100.0;
    if (pressure_hpa >= 250.0 && pressure_hpa <= 1300.0)
    {
      values.has_pressure = true;
      values.pressure_hpa = pressure_hpa;
      matched = true;
    }
  }
  return matched;
}

DataCollector::DataCollector()
{
}

DataCollector::~DataCollector()
{
  stop();
}

bool DataCollector::start(const std::string& port, const SerialConfig& config)
{
  if (running_.load() || serial_.isOpen())
  {
    return false;
  }

  if (!serial_.open(port, config))
  {
    return false;
  }

  port_name_ = port;
  serial_config_ = config;

  if (!initialize())
  {
    serial_.close();
    return false;
  }

  return true;
}

bool DataCollector::startStreaming()
{
  if (running_.load() || !serial_.isOpen())
  {
    return false;
  }

  freq_calc_start_ = std::chrono::steady_clock::now();
  last_emit_time_ = std::chrono::steady_clock::now();
  data_count_ = 0;
  actual_rate_.store(0.0);

  running_.store(true);
  thread_ = std::thread(&DataCollector::run, this);
  return true;
}

void DataCollector::stop()
{
  running_.store(false);
  if (thread_.joinable())
  {
    thread_.join();
  }
  cleanup();
  serial_.close();
}

bool DataCollector::isRunning() const
{
  return running_.load();
}

bool DataCollector::checkDeviceResponse()
{
  return true;
}

void DataCollector::setDataCallback(DataCallback callback)
{
  std::lock_guard<std::mutex> lock(mutex_);
  data_callback_ = std::move(callback);
}

void DataCollector::setLogCallback(LogCallback callback)
{
  std::lock_guard<std::mutex> lock(mutex_);
  log_callback_ = std::move(callback);
}

void DataCollector::setCancelCallback(CancelCallback callback)
{
  std::lock_guard<std::mutex> lock(mutex_);
  cancel_callback_ = std::move(callback);
}

void DataCollector::setEnglish(bool english)
{
  log_english_.store(english);
}

void DataCollector::log(const std::string& message)
{
  LogCallback callback;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    callback = log_callback_;
  }
  if (callback)
  {
    callback(message);
  }
}

bool DataCollector::isEnglishLog() const
{
  return log_english_.load();
}

bool DataCollector::isCancelRequested() const
{
  CancelCallback cancel_callback;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cancel_callback = cancel_callback_;
  }
  return cancel_callback && cancel_callback();
}

std::string DataCollector::getLastError() const
{
  return serial_.lastError();
}

void DataCollector::setSampleRate(int hz)
{
  if (hz < 1) hz = 1;
  if (hz > 1000) hz = 1000;
  sample_rate_hz_.store(hz);
}

int DataCollector::getSampleRate() const
{
  return sample_rate_hz_.load();
}

bool DataCollector::shouldEmitData()
{
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - last_emit_time_).count();
  const int sample_rate = std::max(1, sample_rate_hz_.load());
  int interval_us = 1000000 / sample_rate;
  return elapsed >= interval_us;
}

void DataCollector::updateLastEmitTime()
{
  last_emit_time_ = std::chrono::steady_clock::now();
}

double DataCollector::getActualRate() const
{
  return actual_rate_.load();
}

void DataCollector::recordDataReceived()
{
  auto now = std::chrono::steady_clock::now();
  data_count_++;
  
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - freq_calc_start_).count();
  if (elapsed >= 1000)
  {
    actual_rate_.store(data_count_ * 1000.0 / elapsed);
    data_count_ = 0;
    freq_calc_start_ = now;
  }
}

bool DataCollector::setDeviceSampleRate(int hz)
{
  (void)hz;
  return true;
}

bool DataCollector::initialize()
{
  return true;
}

void DataCollector::cleanup()
{
}

GnssData GnssCollector::getLatestData()
{
  std::lock_guard<std::mutex> lock(mutex_);
  return latest_data_;
}

bool GnssCollector::setDeviceSampleRate(int hz)
{
  if (hz < 1) hz = 1;
  if (hz > 20) hz = 20;
  
  double interval = 1.0 / hz;
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2) << interval;
  std::string cmd = "PVTSLNA COM3 " + oss.str() + "\r\n";
  
  log("[RTK TX] " + cmd);
  
  ssize_t written = serial_.write(cmd.c_str(), cmd.length());
  if (written != static_cast<ssize_t>(cmd.length()))
  {
    log("[RTK 发送] 命令发送失败");
    return false;
  }
  
  sleepMs(300);
  
  char response[1024];
  ssize_t n = serial_.read(response, sizeof(response) - 1);
  if (n > 0)
  {
    response[n] = '\0';
    std::string resp(response);
    
    std::string rx_line;
    std::istringstream iss(resp);
    while (std::getline(iss, rx_line))
    {
      while (!rx_line.empty() && (rx_line.back() == '\r' || rx_line.back() == '\n'))
      {
        rx_line.pop_back();
      }
      
      if (rx_line.empty()) continue;
      
      if (rx_line.find("OK") != std::string::npos ||
          rx_line.find("ERROR") != std::string::npos ||
          rx_line.find("$") == 0 ||
          rx_line.find("#") == 0)
      {
        log("[RTK RX] " + rx_line);
      }
      else if (rx_line.find("PVTSLN") != std::string::npos)
      {
        log("[RTK 接收] PVTSLNA 输出已启动，频率 " + std::to_string(hz) + " Hz");
        break;
      }
    }
  }
  else
  {
    log("[RTK 接收] 未收到响应（命令可能已生效）");
  }
  
  return true;
}

bool GnssCollector::checkDeviceResponse()
{
  char buffer[256];
  constexpr int max_wait_ms = 2000;
  constexpr int step_ms = 100;
  const auto start_time = std::chrono::steady_clock::now();
  int elapsed_ms = 0;

  while (elapsed_ms < max_wait_ms)
  {
    if (isCancelRequested())
    {
      return false;
    }
    log(formatDetectionProgress("等待GNSS数据帧", 1, 1, computeRemainingSeconds(start_time, max_wait_ms)));
    ssize_t n = serial_.read(buffer, sizeof(buffer));
    if (n > 0)
    {
      std::string data(buffer, static_cast<size_t>(n));
      if (data.find("$") != std::string::npos || data.find("#") != std::string::npos)
      {
        return true;
      }
    }
    sleepMs(step_ms);
    elapsed_ms += step_ms;
  }

  return false;
}

void GnssCollector::run()
{
  std::string buffer;
  buffer.reserve(4096);
  bool line_buffer_limit_logged = false;
  char chunk[1024];

  while (running_.load())
  {
    ssize_t n = serial_.read(chunk, sizeof(chunk));
    if (n > 0)
    {
      buffer.append(chunk, static_cast<size_t>(n));
      if (buffer.find('\n') == std::string::npos)
      {
        const size_t dropped = trimUnterminatedLineBuffer(buffer, kGnssLineBufferMaxBytes, kGnssLineBufferKeepBytes);
        if (dropped > 0 && !line_buffer_limit_logged)
        {
          log(isEnglishLog()
              ? "GNSS: dropped oversized unterminated line buffer while waiting for newline"
              : "GNSS：长时间未收到换行，已丢弃过长文本缓冲");
          line_buffer_limit_logged = true;
        }
      }

      size_t pos = 0;
      while ((pos = buffer.find('\n')) != std::string::npos)
      {
        if (pos > kGnssLineBufferMaxBytes)
        {
          buffer.erase(0, pos + 1);
          if (!line_buffer_limit_logged)
          {
            log(isEnglishLog()
                ? "GNSS: dropped oversized text line"
                : "GNSS：已丢弃超长文本行");
          }
          line_buffer_limit_logged = false;
          continue;
        }
        std::string line = buffer.substr(0, pos);
        buffer.erase(0, pos + 1);
        line_buffer_limit_logged = false;

        if (!line.empty() && line.back() == '\r')
        {
          line.pop_back();
        }

        if (line.find("PVTSLN") != std::string::npos)
        {
          unicore_um982_driver::PVTSLNData pvt_data;
          std::string error;
          if (unicore_um982_driver::parsePVTSLN(line, pvt_data, &error))
          {
            DataCallback callback;
            {
              std::lock_guard<std::mutex> lock(mutex_);
              latest_data_.latitude = pvt_data.latitude;
              latest_data_.longitude = pvt_data.longitude;
              latest_data_.altitude = pvt_data.altitude;
              latest_data_.vel_north = pvt_data.velocity_north;
              latest_data_.vel_east = pvt_data.velocity_east;
              latest_data_.vel_down = pvt_data.velocity_up;
              latest_data_.vel_ground = pvt_data.psrvel_ground;
              latest_data_.heading = pvt_data.heading;
              latest_data_.heading_pitch = pvt_data.heading_pitch;
              latest_data_.heading_length = pvt_data.heading_length;
              latest_data_.sigma_lat = pvt_data.sigma_latitude;
              latest_data_.sigma_lon = pvt_data.sigma_longitude;
              latest_data_.sigma_alt = pvt_data.sigma_altitude;
              latest_data_.position_status = pvt_data.position_status;
              latest_data_.num_satellites_used = pvt_data.num_satellites_used;
              latest_data_.num_satellites_tracked = pvt_data.num_satellites_tracked;
              latest_data_.gdop = pvt_data.gdop;
              latest_data_.pdop = pvt_data.pdop;
              latest_data_.hdop = pvt_data.hdop;
              latest_data_.htdop = pvt_data.htdop;
              latest_data_.tdop = pvt_data.tdop;
              latest_data_.diff_age = pvt_data.bestpos_diff_age;
              latest_data_.undulation = pvt_data.undulation;
              latest_data_.elevation_cutoff = pvt_data.elevation_cutoff;
              latest_data_.timestamp = std::chrono::steady_clock::now();
              latest_data_.valid = true;
              latest_data_.raw_sentence = line;
              latest_data_.error_message.clear();
              callback = data_callback_;
            }

            recordDataReceived();

            if (callback && shouldEmitData())
            {
              updateLastEmitTime();
              callback();
            }
          }
        }
      }
    }
    else
    {
      sleepMs(10);
    }
  }
}

EpsilonData EpsilonCollector::getLatestData()
{
  std::lock_guard<std::mutex> lock(mutex_);
  return latest_data_;
}

void EpsilonCollector::setRawFrameCallback(RawFrameCallback callback)
{
  std::lock_guard<std::mutex> lock(mutex_);
  raw_frame_callback_ = std::move(callback);
}

bool EpsilonCollector::checkDeviceResponse()
{
  const bool english = isEnglishLog();
  std::vector<uint8_t> buffer;
  std::vector<uint8_t> frame;
  uint8_t packetId = 0;
  if (!readValidFdilinkFrame(serial_, buffer, &frame, 2500, &packetId, nullptr))
  {
    log(english
            ? "EPSILON: no navigation frame detected, trying command-mode handshake"
            : "EPSILON：未检测到导航数据帧，尝试进入命令模式握手");

    auto sendCommandForProbe = [this](const std::string& command, int waitMs) {
      const ssize_t written = serial_.write(command.c_str(), command.size());
      if (written != static_cast<ssize_t>(command.size()))
      {
        return std::string();
      }
      sleepMs(waitMs);
      return readPrintableSerialResponse(serial_, std::max(waitMs, 600), true);
    };

    serial_.flush();
    sleepMs(80);

    const std::string configResponse = sendCommandForProbe("#fconfig\r\n", 1500);
    if (!configResponse.empty())
    {
      std::istringstream stream(configResponse);
      std::string line;
      while (std::getline(stream, line))
      {
        const std::string trimmed = trimAscii(line);
        if (!trimmed.empty())
        {
          log("[EPSILON RX] " + trimmed);
        }
      }
    }

    if (!containsEpsilonAsciiAck(configResponse))
    {
      return false;
    }

    const std::string fmsgResponse = sendCommandForProbe("#fmsg\r\n", 1500);
    if (!fmsgResponse.empty())
    {
      std::istringstream stream(fmsgResponse);
      std::string line;
      int lineCount = 0;
      while (std::getline(stream, line) && lineCount < 12)
      {
        const std::string trimmed = trimAscii(line);
        if (!trimmed.empty())
        {
          log("[EPSILON RX] " + trimmed);
          ++lineCount;
        }
      }
    }

    sendCommandForProbe("#fdeconfig\r\n", 1500);
    if (readValidFdilinkFrame(serial_, buffer, &frame, 2500, &packetId, nullptr))
    {
      log(std::string(english
                          ? "EPSILON: recovered navigation stream with FDILink frame "
                          : "EPSILON：已恢复导航数据流，FDILink 数据包 ") +
          std::to_string(packetId));
      return true;
    }

    log(english
            ? "EPSILON: device responds to commands, but no navigation frame was restored yet"
            : "EPSILON：设备可响应命令，但导航数据流尚未恢复");
    return false;
  }

  log(std::string(english ? "EPSILON: detected FDILink frame " : "EPSILON：检测到 FDILink 数据包 ") +
      std::to_string(packetId));
  return true;
}

bool EpsilonCollector::setDeviceSampleRate(int hz)
{
  if (!isSupportedEpsilonRate(hz))
  {
    log(std::string(isEnglishLog() ? "EPSILON: unsupported output rate " : "EPSILON：不支持的输出频率 ") +
        std::to_string(hz) + " Hz");
    return false;
  }
  if (!setOutputPacketRates(desiredEpsilonPacketRates(hz)))
  {
    return false;
  }
  sample_rate_hz_.store(hz);
  return true;
}

bool EpsilonCollector::setOutputPacketRates(const std::map<uint8_t, int>& packetRates, bool forceApply)
{
  const bool english = isEnglishLog();
  if (packetRates.empty())
  {
    log(english
            ? "EPSILON: no packet rates were provided for configuration"
            : "EPSILON：没有可用于配置的数据包频率");
    return false;
  }
  for (const auto& entry : packetRates)
  {
    if (!isSupportedEpsilonPacketRate(entry.first, entry.second))
    {
      std::ostringstream oss;
      oss << (english ? "EPSILON: unsupported packet rate " : "EPSILON：数据包频率不受支持：")
          << entry.second << " Hz"
          << (english ? " for packet 0x" : "，数据包 0x")
          << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
          << static_cast<int>(entry.first);
      log(oss.str());
      return false;
    }
  }

  if (running_.load())
  {
    const std::string portName = port_name_;
    const SerialConfig serialConfig = serial_config_;
    log(english
            ? "EPSILON: temporarily stopping the live stream to apply packet-rate changes"
            : "EPSILON：为修改数据包频率临时停止实时数据流");
    stop();
    if (!start(portName, serialConfig))
    {
      log(english
              ? "EPSILON: failed to reopen the serial port for packet-rate configuration"
              : "EPSILON：为配置数据包频率重新打开串口失败");
      return false;
    }

    const bool configured = setOutputPacketRates(packetRates, forceApply);
    if (!serial_.isOpen() && !start(portName, serialConfig))
    {
      log(english
              ? "EPSILON: packet-rate attempt finished, but the serial port could not be reopened"
              : "EPSILON：数据包频率配置尝试已结束，但串口无法重新打开");
      return false;
    }
    if (!checkDeviceResponse() || !startStreaming())
    {
      log(english
              ? "EPSILON: packet-rate attempt finished, but the live navigation stream could not be restored"
              : "EPSILON：数据包频率配置尝试已结束，但实时导航流未能恢复");
      stop();
      return false;
    }

    log(configured
            ? (english
                   ? "EPSILON: packet rates applied and live navigation stream restored"
                   : "EPSILON：数据包频率已应用，实时导航流已恢复")
            : (english
                   ? "EPSILON: packet-rate configuration failed; the previous live navigation stream was restored"
                   : "EPSILON：数据包频率配置失败，原实时导航流已恢复"));
    return configured;
  }

  if (!serial_.isOpen())
  {
    log(english ? "EPSILON: serial port is not open" : "EPSILON：串口未打开");
    return false;
  }

  const EpsilonLogFn logFn = [this](const std::string& message) { log(message); };
  auto failPacketConfiguration = [this, &logFn, english](const std::string& message) {
    log(message);
    sendLoggedEpsilonAsciiCommand(serial_, logFn, english, "#fdeconfig\r\n", 700);
    return false;
  };

  constexpr int kConfigCommandWaitMs = 1500;
  serial_.flush();
  sleepMs(80);

  const std::string configResponse = sendLoggedEpsilonAsciiCommand(serial_, logFn, english, "#fconfig\r\n", kConfigCommandWaitMs);
  if (!containsEpsilonAsciiOk(configResponse))
  {
    return failPacketConfiguration(english
                                       ? "EPSILON: failed to enter configuration mode; packet rates were not changed"
                                       : "EPSILON：进入配置模式失败，数据包频率未修改");
  }

  const std::string fmsgResponse = sendLoggedEpsilonAsciiCommand(serial_, logFn, english, "#fmsg\r\n", kConfigCommandWaitMs);
  const auto currentRates = parseFmsgResponse(fmsgResponse);

  bool needsReconfigure = currentRates.size() < packetRates.size();
  for (const auto& entry : packetRates)
  {
    const auto it = currentRates.find(entry.first);
    if (it == currentRates.end() || it->second != entry.second)
    {
      needsReconfigure = true;
      break;
    }
  }

  if (needsReconfigure || forceApply)
  {
    if (!needsReconfigure && forceApply)
    {
      log(english
              ? "EPSILON: output configuration matches requested packet rates; reapplying and rebooting to activate the standard FDILink stream"
              : "EPSILON：当前输出配置已匹配目标频率，仍将重新下发并重启以激活标准 FDILink 数据流");
    }
    for (const auto& entry : packetRates)
    {
      char command[32];
      std::snprintf(command, sizeof(command), "#fmsg %02X %d\r\n", entry.first, entry.second);
      const std::string response = sendLoggedEpsilonAsciiCommand(serial_, logFn, english, command, kConfigCommandWaitMs);
      if (!containsEpsilonAsciiOk(response))
      {
        std::ostringstream oss;
        oss << (english ? "EPSILON: failed to set packet 0x" : "EPSILON：设置数据包 0x")
            << std::uppercase << std::hex
            << std::setw(2) << std::setfill('0') << static_cast<int>(entry.first)
            << (english
                    ? " output rate; packet-rate configuration was not saved"
                    : " 输出频率失败，数据包频率配置未保存");
        return failPacketConfiguration(oss.str());
      }
    }

    const std::string saveResponse = sendLoggedEpsilonAsciiCommand(serial_, logFn, english, "#fsave\r\n", kConfigCommandWaitMs);
    if (!containsEpsilonAsciiOk(saveResponse))
    {
      return failPacketConfiguration(english
                                         ? "EPSILON: failed to save packet-rate configuration"
                                         : "EPSILON：保存数据包频率配置失败");
    }

    if (!rebootEpsilonAndReopenSerial(serial_, port_name_, serial_config_, logFn, english))
    {
      return false;
    }

    if (!waitForEpsilonNavigationStreamRestore(serial_,
                                               logFn,
                                               8000,
                                               english
                                                   ? "EPSILON: output configuration saved, rebooted, and navigation stream restored with FDILink frame %u"
                                                   : "EPSILON：输出配置已保存并重启，导航数据流已恢复，FDILink 数据包 %u",
                                               english
                                                   ? "EPSILON: configuration saved and rebooted, but no FDILink frame was observed after the port returned"
                                                   : "EPSILON：配置已保存并重启，但串口恢复后没有检测到 FDILink 数据包"))
    {
      return false;
    }
  }
  else
  {
    log(english
            ? "EPSILON: output configuration already matches requested packet rates"
            : "EPSILON：当前输出配置已匹配目标数据包频率");
    sendLoggedEpsilonAsciiCommand(serial_, logFn, english, "#fdeconfig\r\n", kConfigCommandWaitMs);
    return waitForEpsilonNavigationStreamRestore(serial_,
                                                 logFn,
                                                 3000,
                                                 english
                                                     ? "EPSILON: returned to navigation mode with FDILink frame %u"
                                                     : "EPSILON：已返回导航模式，检测到 FDILink 数据包 %u",
                                                 english
                                                     ? "EPSILON: configuration query completed, but navigation stream is still silent"
                                                     : "EPSILON：配置查询已完成，但导航数据流仍无输出");
  }
  return true;
}

bool EpsilonCollector::configureRtcmPort(int portIndex, int baudRate)
{
  const bool english = isEnglishLog();
  if (portIndex < 1 || portIndex > 5)
  {
    log(english
            ? "EPSILON: invalid communication port index for RTCM configuration"
            : "EPSILON：RTCM 串口配置的通信端口编号无效");
    return false;
  }
  if (portIndex == 1)
  {
    log(english
            ? "EPSILON: refusing to change communication port 1 because it must remain the Main port"
            : "EPSILON：拒绝修改通信端口 1，该端口必须保持 Main");
    return false;
  }
  if (!serial_.isOpen())
  {
    log(english ? "EPSILON: serial port is not open" : "EPSILON：串口未打开");
    return false;
  }
  if (running_.load())
  {
    log(english
            ? "EPSILON: stop the live stream before configuring the RTCM port"
            : "EPSILON：配置 RTCM 串口前请先停止实时数据流");
    return false;
  }

  const int baudParamValue = epsilonSerialBaudToParamValue(baudRate);
  if (baudParamValue == 0)
  {
    log(std::string(english ? "EPSILON: unsupported RTCM serial baud rate " : "EPSILON：不支持的 RTCM 串口波特率 ") +
        std::to_string(baudRate));
    return false;
  }

  const EpsilonLogFn logFn = [this](const std::string& message) { log(message); };
  constexpr int kConfigCommandWaitMs = 1500;

  serial_.flush();
  sleepMs(80);

  const auto failConfiguration = [this, &logFn, english](const std::string& message) {
    log(message);
    sendLoggedEpsilonAsciiCommand(serial_, logFn, english, "#fdeconfig\r\n", kConfigCommandWaitMs);
    return false;
  };

  const std::string configResponse = sendLoggedEpsilonAsciiCommand(serial_, logFn, english, "#fconfig\r\n", kConfigCommandWaitMs);
  if (!containsEpsilonAsciiOk(configResponse))
  {
    return failConfiguration(english
                                 ? "EPSILON: failed to enter configuration mode; RTCM port was not changed"
                                 : "EPSILON：进入配置模式失败，RTCM 串口未修改");
  }

  char command[64];
  std::snprintf(command, sizeof(command), "#fparam set COMM_STREAM_TYP%d 3\r\n", portIndex);
  if (!containsEpsilonAsciiOk(sendLoggedEpsilonAsciiCommand(serial_, logFn, english, command, kConfigCommandWaitMs)))
  {
    return failConfiguration(english
                                 ? "EPSILON: failed to set the RTCM stream type; RTCM port was not saved"
                                 : "EPSILON：设置 RTCM 数据流类型失败，RTCM 串口配置未保存");
  }

  std::snprintf(command, sizeof(command), "#fparam set COMM_BAUD%d %d\r\n", portIndex, baudParamValue);
  if (!containsEpsilonAsciiOk(sendLoggedEpsilonAsciiCommand(serial_, logFn, english, command, kConfigCommandWaitMs)))
  {
    return failConfiguration(english
                                 ? "EPSILON: failed to set the RTCM port baud rate; RTCM port was not saved"
                                 : "EPSILON：设置 RTCM 串口波特率失败，RTCM 串口配置未保存");
  }

  if (!containsEpsilonAsciiOk(sendLoggedEpsilonAsciiCommand(serial_, logFn, english, "#fsave\r\n", kConfigCommandWaitMs)))
  {
    return failConfiguration(english
                                 ? "EPSILON: failed to save the RTCM port configuration"
                                 : "EPSILON：保存 RTCM 串口配置失败");
  }

  if (!rebootEpsilonAndReopenSerial(serial_, port_name_, serial_config_, logFn, english))
  {
    return false;
  }

  return waitForEpsilonNavigationStreamRestore(serial_,
                                               logFn,
                                               8000,
                                               english
                                                   ? "EPSILON: RTCM port configuration saved, rebooted, and navigation stream restored"
                                                   : "EPSILON：RTCM 串口配置已保存并重启，导航数据流已恢复",
                                               english
                                                   ? "EPSILON: RTCM port configuration was saved and rebooted, but no FDILink frame was observed after the port returned"
                                                   : "EPSILON：RTCM 串口配置已保存并重启，但串口恢复后没有检测到 FDILink 数据包");
}

bool EpsilonCollector::configureMainAntennaLeverArm(double xM, double yM, double zM)
{
  const bool english = isEnglishLog();
  if (!std::isfinite(xM) || !std::isfinite(yM) || !std::isfinite(zM))
  {
    log(english
            ? "EPSILON: invalid main antenna lever-arm value"
            : "EPSILON：主天线杆臂数值无效");
    return false;
  }
  if (!serial_.isOpen())
  {
    log(english ? "EPSILON: serial port is not open" : "EPSILON：串口未打开");
    return false;
  }
  if (running_.load())
  {
    log(english
            ? "EPSILON: stop the live stream before configuring the main antenna lever arm"
            : "EPSILON：配置主天线杆臂前请先停止实时数据流");
    return false;
  }

  const EpsilonLogFn logFn = [this](const std::string& message) { log(message); };
  constexpr int kConfigCommandWaitMs = 1500;

  serial_.flush();
  sleepMs(80);

  const auto failConfiguration = [this, &logFn, english](const std::string& message) {
    log(message);
    sendLoggedEpsilonAsciiCommand(serial_, logFn, english, "#fdeconfig\r\n", kConfigCommandWaitMs);
    return false;
  };

  const std::string configResponse = sendLoggedEpsilonAsciiCommand(serial_, logFn, english, "#fconfig\r\n", kConfigCommandWaitMs);
  if (!containsEpsilonAsciiOk(configResponse))
  {
    return failConfiguration(english
                                 ? "EPSILON: failed to enter configuration mode; main antenna lever arm was not changed"
                                 : "EPSILON：进入配置模式失败，主天线杆臂未修改");
  }

  char command[128];
  std::snprintf(command, sizeof(command), "#fantearm %.6f %.6f %.6f\r\n", xM, yM, zM);
  if (!containsEpsilonAsciiOk(sendLoggedEpsilonAsciiCommand(serial_, logFn, english, command, kConfigCommandWaitMs)))
  {
    return failConfiguration(english
                                 ? "EPSILON: failed to set the main antenna lever arm; configuration was not saved"
                                 : "EPSILON：设置主天线杆臂失败，配置未保存");
  }

  if (!containsEpsilonAsciiOk(sendLoggedEpsilonAsciiCommand(serial_, logFn, english, "#fsave\r\n", kConfigCommandWaitMs)))
  {
    return failConfiguration(english
                                 ? "EPSILON: failed to save the main antenna lever-arm configuration"
                                 : "EPSILON：保存主天线杆臂配置失败");
  }

  sendLoggedEpsilonAsciiCommand(serial_, logFn, english, "#fdeconfig\r\n", kConfigCommandWaitMs);

  return waitForEpsilonNavigationStreamRestore(serial_,
                                               logFn,
                                               5000,
                                               english
                                                   ? "EPSILON: main antenna lever arm saved and navigation stream restored"
                                                   : "EPSILON：主天线杆臂已保存，导航数据流已恢复",
                                               english
                                                   ? "EPSILON: main antenna lever arm was saved, but no FDILink frame was observed after leaving config mode"
                                                   : "EPSILON：主天线杆臂已保存，但退出配置模式后未检测到 FDILink 数据包");
}

void EpsilonCollector::run()
{
  std::vector<uint8_t> buffer;
  buffer.reserve(8192);
  uint8_t chunk[1024];
  bool havePreviousSerial = false;
  uint8_t previousSerial = 0;
  uint64_t droppedFrames = 0;
  struct PacketRateTracker
  {
    int count = 0;
    double rate_hz = 0.0;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    void record()
    {
      const auto now = std::chrono::steady_clock::now();
      ++count;
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
      if (elapsed >= 1000)
      {
        rate_hz = count * 1000.0 / elapsed;
        count = 0;
        start = now;
      }
    }
  };
  PacketRateTracker imuRateTracker;
  PacketRateTracker ahrsRateTracker;
  PacketRateTracker insGpsRateTracker;
  PacketRateTracker sysStateRateTracker;
  PacketRateTracker rawGnssRateTracker;
  PacketRateTracker satelliteRateTracker;
  PacketRateTracker geodeticRateTracker;
  PacketRateTracker ecefRateTracker;
  bool reportedInvalidEcef = false;
  bool hasResolvedLlh = false;

  auto consumeFrame = [this,
                       &havePreviousSerial,
                       &previousSerial,
                       &droppedFrames,
                       &imuRateTracker,
                       &ahrsRateTracker,
                       &insGpsRateTracker,
                       &sysStateRateTracker,
                       &rawGnssRateTracker,
                       &satelliteRateTracker,
                       &geodeticRateTracker,
                       &ecefRateTracker,
                       &reportedInvalidEcef,
                       &hasResolvedLlh](const std::vector<uint8_t>& frame, uint64_t hostTimestampUs) {
    if (frame.size() < 8)
    {
      return;
    }

    const uint8_t packetId = frame[1];
    const uint8_t serialNumber = frame[3];
    const size_t payloadSize = frame[2];
    if (frame.size() != payloadSize + 8)
    {
      return;
    }
    const uint8_t* payload = frame.data() + 7;

    if (havePreviousSerial)
    {
      const uint8_t expected = static_cast<uint8_t>(previousSerial + 1);
      if (serialNumber != expected)
      {
        const uint8_t delta = static_cast<uint8_t>(serialNumber - expected);
        droppedFrames += delta;
      }
    }
    havePreviousSerial = true;
    previousSerial = serialNumber;

    switch (packetId)
    {
    case kMsgImu:
      imuRateTracker.record();
      break;
    case kMsgAhrs:
      ahrsRateTracker.record();
      break;
    case kMsgInsGps:
      insGpsRateTracker.record();
      break;
    case kMsgSystemState:
      sysStateRateTracker.record();
      break;
    case kMsgRawGnss:
      rawGnssRateTracker.record();
      break;
    case kMsgSatellites:
      satelliteRateTracker.record();
      break;
    case kMsgGeodeticPos:
      geodeticRateTracker.record();
      break;
    case kMsgEcefPos:
      ecefRateTracker.record();
      break;
    default:
      break;
    }

    DataCallback callback;
    RawFrameCallback rawCallback;
    std::string invalidEcefWarning;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      latest_data_.last_packet_id = packetId;
      latest_data_.last_serial_number = serialNumber;
      latest_data_.raw_frame_count += 1;
      latest_data_.dropped_frame_count = droppedFrames;
      latest_data_.timestamp = std::chrono::steady_clock::now();
      latest_data_.valid = true;
      latest_data_.error_message.clear();

      if (packetId == kMsgImu && payloadSize >= 56)
      {
        latest_data_.imu_gyr_x_radps = readFloatLE(payload + 0);
        latest_data_.imu_gyr_y_radps = readFloatLE(payload + 4);
        latest_data_.imu_gyr_z_radps = readFloatLE(payload + 8);
        latest_data_.imu_acc_x_mps2 = readFloatLE(payload + 12);
        latest_data_.imu_acc_y_mps2 = readFloatLE(payload + 16);
        latest_data_.imu_acc_z_mps2 = readFloatLE(payload + 20);
        latest_data_.mag_x_mg = readFloatLE(payload + 24);
        latest_data_.mag_y_mg = readFloatLE(payload + 28);
        latest_data_.mag_z_mg = readFloatLE(payload + 32);
        latest_data_.imu_temp_c = std::numeric_limits<double>::quiet_NaN();
        latest_data_.pressure_pa = std::numeric_limits<double>::quiet_NaN();
        latest_data_.pressure_temp_c = std::numeric_limits<double>::quiet_NaN();
        latest_data_.device_timestamp_us = static_cast<uint64_t>(readI64LE(payload + 48));
      }
      else if (packetId == kMsgAhrs && payloadSize >= 48)
      {
        latest_data_.ang_vel_x_radps = readFloatLE(payload + 0);
        latest_data_.ang_vel_y_radps = readFloatLE(payload + 4);
        latest_data_.ang_vel_z_radps = readFloatLE(payload + 8);
        latest_data_.ahrs_roll_deg = radToDeg(readFloatLE(payload + 12));
        latest_data_.ahrs_pitch_deg = radToDeg(readFloatLE(payload + 16));
        latest_data_.ahrs_yaw_deg = radToDeg(readFloatLE(payload + 20));
        latest_data_.ahrs_quat_w = readFloatLE(payload + 24);
        latest_data_.ahrs_quat_x = readFloatLE(payload + 28);
        latest_data_.ahrs_quat_y = readFloatLE(payload + 32);
        latest_data_.ahrs_quat_z = readFloatLE(payload + 36);
        latest_data_.ahrs_attitude_valid = true;
        latest_data_.device_timestamp_us = static_cast<uint64_t>(readI64LE(payload + 40));
        updateEpsilonAttitudeState(latest_data_);
      }
      else if (packetId == kMsgInsGps && payloadSize >= 72)
      {
        latest_data_.body_vel_x_mps = readFloatLE(payload + 0);
        latest_data_.body_vel_y_mps = readFloatLE(payload + 4);
        latest_data_.body_vel_z_mps = readFloatLE(payload + 8);
        latest_data_.body_acc_x_mps2 = readFloatLE(payload + 12);
        latest_data_.body_acc_y_mps2 = readFloatLE(payload + 16);
        latest_data_.body_acc_z_mps2 = readFloatLE(payload + 20);
        latest_data_.ned_n_m = readFloatLE(payload + 24);
        latest_data_.ned_e_m = readFloatLE(payload + 28);
        latest_data_.ned_d_m = readFloatLE(payload + 32);
        latest_data_.vel_n_mps = readFloatLE(payload + 36);
        latest_data_.vel_e_mps = readFloatLE(payload + 40);
        latest_data_.vel_d_mps = readFloatLE(payload + 44);
        latest_data_.pressure_altitude_m = std::numeric_limits<double>::quiet_NaN();
        latest_data_.device_timestamp_us = static_cast<uint64_t>(readI64LE(payload + 64));
      }
      else if (packetId == kMsgSystemState && payloadSize >= 14)
      {
        latest_data_.system_status_bits = readU16LE(payload + 0);
        latest_data_.filter_status_bits = readU16LE(payload + 2);
        latest_data_.update_status_bits = readU16LE(payload + 4);
        latest_data_.utc_unix_s = readU32LE(payload + 6);
        latest_data_.utc_microseconds = readU32LE(payload + 10);
        latest_data_.gnss_fix_code = static_cast<int>((latest_data_.filter_status_bits >> 4) & 0x0F);
        latest_data_.gnss_fix_text = epsilonGnssFixName(latest_data_.gnss_fix_code);
        if (payloadSize >= 38)
        {
          latest_data_.latitude_deg = radToDeg(readDoubleLE(payload + 14));
          latest_data_.longitude_deg = radToDeg(readDoubleLE(payload + 22));
          latest_data_.height_m = readDoubleLE(payload + 30);
          hasResolvedLlh = true;
          resolveEpsilonEcefFromLlh(latest_data_, hasResolvedLlh);
        }
        if (payloadSize >= 50)
        {
          latest_data_.vel_n_mps = readFloatLE(payload + 38);
          latest_data_.vel_e_mps = readFloatLE(payload + 42);
          latest_data_.vel_d_mps = readFloatLE(payload + 46);
        }
        if (payloadSize >= 62)
        {
          latest_data_.body_acc_x_mps2 = readFloatLE(payload + 50);
          latest_data_.body_acc_y_mps2 = readFloatLE(payload + 54);
          latest_data_.body_acc_z_mps2 = readFloatLE(payload + 58);
        }
        if (payloadSize >= 78)
        {
          latest_data_.roll_deg = radToDeg(readFloatLE(payload + 66));
          latest_data_.pitch_deg = radToDeg(readFloatLE(payload + 70));
          latest_data_.yaw_deg = radToDeg(readFloatLE(payload + 74));
        }
        if (payloadSize >= 90)
        {
          latest_data_.ang_vel_x_radps = readFloatLE(payload + 78);
          latest_data_.ang_vel_y_radps = readFloatLE(payload + 82);
          latest_data_.ang_vel_z_radps = readFloatLE(payload + 86);
        }
        if (payloadSize >= 102)
        {
          latest_data_.lat_std_m = readFloatLE(payload + 90);
          latest_data_.lon_std_m = readFloatLE(payload + 94);
          latest_data_.height_std_m = readFloatLE(payload + 98);
        }
      }
      else if (packetId == kMsgUnixTime && payloadSize >= 8)
      {
        sysStateRateTracker.record();
        latest_data_.utc_unix_s = readU32LE(payload + 0);
        latest_data_.utc_microseconds = readU32LE(payload + 4);
      }
      else if (packetId == kMsgFormattedTime && payloadSize >= 14)
      {
        sysStateRateTracker.record();
        const uint32_t microseconds = readU32LE(payload + 0);
        uint64_t utcSeconds = 0;
        uint32_t utcMicroseconds = 0;
        if (utcPartsToUnix(
                static_cast<int>(readU16LE(payload + 4)),
                static_cast<int>(payload[8]),
                static_cast<int>(payload[9]),
                static_cast<int>(payload[11]),
                static_cast<int>(payload[12]),
                static_cast<double>(payload[13]) + microseconds / 1000000.0,
                utcSeconds,
                utcMicroseconds))
        {
          latest_data_.utc_unix_s = utcSeconds;
          latest_data_.utc_microseconds = utcMicroseconds;
        }
      }
      else if (packetId == kMsgStatus && payloadSize >= 4)
      {
        latest_data_.system_status_bits = readU16LE(payload + 0);
        latest_data_.filter_status_bits = readU16LE(payload + 2);
        latest_data_.gnss_fix_code = static_cast<int>((latest_data_.filter_status_bits >> 4) & 0x0F);
        latest_data_.gnss_fix_text = epsilonGnssFixName(latest_data_.gnss_fix_code);
      }
      else if (packetId == kMsgRawGnss && payloadSize >= 74)
      {
        latest_data_.utc_unix_s = readU32LE(payload + 0);
        latest_data_.utc_microseconds = readU32LE(payload + 4);
        latest_data_.lat_std_m = readFloatLE(payload + 44);
        latest_data_.lon_std_m = readFloatLE(payload + 48);
        latest_data_.height_std_m = readFloatLE(payload + 52);
        latest_data_.diff_age_s = readFloatLE(payload + 64);
        const uint16_t rawGnssStatus = readU16LE(payload + 72);
        latest_data_.gnss_fix_code = static_cast<int>(rawGnssStatus & 0x0Fu);
        latest_data_.gnss_fix_text = epsilonGnssFixName(latest_data_.gnss_fix_code);
        latest_data_.heading_valid = ((rawGnssStatus >> 8) & 0x01u) != 0;
      }
      else if (packetId == kMsgSatellites && payloadSize >= 9)
      {
        latest_data_.hdop = readFloatLE(payload + 0);
        latest_data_.vdop = readFloatLE(payload + 4);
        latest_data_.gnss_satellites = static_cast<int>(payload[8]);
      }
      else if (packetId == kMsgGeodeticPos && payloadSize >= 32)
      {
        latest_data_.latitude_deg = radToDeg(readDoubleLE(payload + 0));
        latest_data_.longitude_deg = radToDeg(readDoubleLE(payload + 8));
        latest_data_.height_m = readDoubleLE(payload + 16);
        latest_data_.hacc_m = readFloatLE(payload + 24);
        latest_data_.vacc_m = readFloatLE(payload + 28);
        hasResolvedLlh = true;
        resolveEpsilonEcefFromLlh(latest_data_, hasResolvedLlh);
      }
      else if (packetId == kMsgEcefPos && payloadSize >= 24)
      {
        const double xM = readDoubleLE(payload + 0);
        const double yM = readDoubleLE(payload + 8);
        const double zM = readDoubleLE(payload + 16);
        if (VaporView::Geo::isPlausibleEcef(xM, yM, zM))
        {
          latest_data_.ecef_x_m = xM;
          latest_data_.ecef_y_m = yM;
          latest_data_.ecef_z_m = zM;
          reportedInvalidEcef = false;
        }
        else
        {
          const double invalid = std::numeric_limits<double>::quiet_NaN();
          latest_data_.ecef_x_m = invalid;
          latest_data_.ecef_y_m = invalid;
          latest_data_.ecef_z_m = invalid;
          resolveEpsilonEcefFromLlh(latest_data_, hasResolvedLlh);
          if (!reportedInvalidEcef)
          {
            std::ostringstream message;
            message << "EPSILON: ignoring implausible ECEF packet ("
                    << xM << ", " << yM << ", " << zM
                    << ") m; using LLH-derived WGS84 ECEF when available";
            invalidEcefWarning = message.str();
            reportedInvalidEcef = true;
          }
        }
      }
      else if (packetId == kMsgEulerOrien && payloadSize >= 12)
      {
        latest_data_.euler_orien_roll_deg = radToDeg(readFloatLE(payload + 0));
        latest_data_.euler_orien_pitch_deg = radToDeg(readFloatLE(payload + 4));
        latest_data_.euler_orien_yaw_deg = radToDeg(readFloatLE(payload + 8));
        latest_data_.euler_orien_valid = true;
        updateEpsilonAttitudeState(latest_data_);
      }
      else if (packetId == kMsgQuatOrien && payloadSize >= 16)
      {
        latest_data_.quat_orien_w = readFloatLE(payload + 0);
        latest_data_.quat_orien_x = readFloatLE(payload + 4);
        latest_data_.quat_orien_y = readFloatLE(payload + 8);
        latest_data_.quat_orien_z = readFloatLE(payload + 12);
        quaternionToEulerDeg(latest_data_.quat_orien_w,
                             latest_data_.quat_orien_x,
                             latest_data_.quat_orien_y,
                             latest_data_.quat_orien_z,
                             latest_data_.quat_orien_roll_deg,
                             latest_data_.quat_orien_pitch_deg,
                             latest_data_.quat_orien_yaw_deg);
        latest_data_.quat_orien_valid = true;
        updateEpsilonAttitudeState(latest_data_);
      }
      else if (packetId == kMsgMainMavlinkTunnel && payloadSize >= 8)
      {
        std::vector<uint8_t> mavlinkPayload(payloadSize);
        for (size_t i = 0; i < payloadSize; ++i)
        {
          mavlinkPayload[i] = static_cast<uint8_t>(~payload[i]);
        }

        for (size_t offset = 0; offset + 8 <= mavlinkPayload.size();)
        {
          if (mavlinkPayload[offset] != kMavlinkV1Stx)
          {
            ++offset;
            continue;
          }

          const size_t mavlinkPayloadSize = mavlinkPayload[offset + 1];
          const size_t mavlinkFrameSize = mavlinkPayloadSize + 8;
          if (offset + mavlinkFrameSize > mavlinkPayload.size())
          {
            break;
          }

          const uint8_t mavlinkMessageId = mavlinkPayload[offset + 5];
          const uint8_t* mavlinkData = mavlinkPayload.data() + offset + 6;
          switch (mavlinkMessageId)
          {
          case kMavlinkMsgHeartbeat:
            sysStateRateTracker.record();
            break;
          case kMavlinkMsgSysStatus:
            sysStateRateTracker.record();
            break;
          case kMavlinkMsgGpsRawInt:
            if (mavlinkPayloadSize >= 30)
            {
              rawGnssRateTracker.record();
              latest_data_.device_timestamp_us = readU64LE(mavlinkData + 0);
              latest_data_.latitude_deg = readI32LE(mavlinkData + 8) / 10000000.0;
              latest_data_.longitude_deg = readI32LE(mavlinkData + 12) / 10000000.0;
              latest_data_.height_m = readI32LE(mavlinkData + 16) / 1000.0;
              hasResolvedLlh = true;
              resolveEpsilonEcefFromLlh(latest_data_, hasResolvedLlh);
              const uint16_t eph = readU16LE(mavlinkData + 20);
              const uint16_t epv = readU16LE(mavlinkData + 22);
              if (eph != 0xFFFFu)
              {
                latest_data_.hacc_m = eph / 100.0;
                latest_data_.lat_std_m = latest_data_.hacc_m;
                latest_data_.lon_std_m = latest_data_.hacc_m;
              }
              if (epv != 0xFFFFu)
              {
                latest_data_.vacc_m = epv / 100.0;
                latest_data_.height_std_m = latest_data_.vacc_m;
              }
              latest_data_.gnss_fix_code = static_cast<int>(mavlinkData[28]);
              latest_data_.gnss_fix_text = epsilonGnssFixName(latest_data_.gnss_fix_code);
              latest_data_.gnss_satellites = static_cast<int>(mavlinkData[29]);
            }
            break;
          case kMavlinkMsgAttitude:
            if (mavlinkPayloadSize >= 28)
            {
              ahrsRateTracker.record();
              latest_data_.device_timestamp_us = static_cast<uint64_t>(readU32LE(mavlinkData + 0)) * 1000u;
              latest_data_.roll_deg = radToDeg(readFloatLE(mavlinkData + 4));
              latest_data_.pitch_deg = radToDeg(readFloatLE(mavlinkData + 8));
              latest_data_.yaw_deg = radToDeg(readFloatLE(mavlinkData + 12));
              latest_data_.ang_vel_x_radps = readFloatLE(mavlinkData + 16);
              latest_data_.ang_vel_y_radps = readFloatLE(mavlinkData + 20);
              latest_data_.ang_vel_z_radps = readFloatLE(mavlinkData + 24);
            }
            break;
          case kMavlinkMsgLocalPositionNed:
            if (mavlinkPayloadSize >= 28)
            {
              insGpsRateTracker.record();
              latest_data_.device_timestamp_us = static_cast<uint64_t>(readU32LE(mavlinkData + 0)) * 1000u;
              latest_data_.ned_n_m = readFloatLE(mavlinkData + 4);
              latest_data_.ned_e_m = readFloatLE(mavlinkData + 8);
              latest_data_.ned_d_m = readFloatLE(mavlinkData + 12);
              latest_data_.vel_n_mps = readFloatLE(mavlinkData + 16);
              latest_data_.vel_e_mps = readFloatLE(mavlinkData + 20);
              latest_data_.vel_d_mps = readFloatLE(mavlinkData + 24);
            }
            break;
          case kMavlinkMsgGlobalPositionInt:
            if (mavlinkPayloadSize >= 28)
            {
              geodeticRateTracker.record();
              latest_data_.device_timestamp_us = static_cast<uint64_t>(readU32LE(mavlinkData + 0)) * 1000u;
              latest_data_.latitude_deg = readI32LE(mavlinkData + 4) / 10000000.0;
              latest_data_.longitude_deg = readI32LE(mavlinkData + 8) / 10000000.0;
              latest_data_.height_m = readI32LE(mavlinkData + 12) / 1000.0;
              hasResolvedLlh = true;
              resolveEpsilonEcefFromLlh(latest_data_, hasResolvedLlh);
              latest_data_.vel_n_mps = readI16LE(mavlinkData + 20) / 100.0;
              latest_data_.vel_e_mps = readI16LE(mavlinkData + 22) / 100.0;
              latest_data_.vel_d_mps = readI16LE(mavlinkData + 24) / 100.0;
            }
            break;
          case kMavlinkMsgFdiTelemetryF:
            if (mavlinkPayloadSize >= 80)
            {
              std::array<float, 20> values{};
              for (size_t i = 0; i < values.size(); ++i)
              {
                values[i] = readFloatLE(mavlinkData + i * sizeof(float));
              }

              int datasetIndex = -1;
              if (mavlinkPayloadSize >= 81)
              {
                datasetIndex = static_cast<int>(mavlinkData[80]);
              }
              if ((datasetIndex < 0 || datasetIndex >= kAqmavDatasetSystemsAndClock + 8) && mavlinkPayloadSize >= 82)
              {
                datasetIndex = static_cast<int>(readU16LE(mavlinkData + 80));
              }

              const uint16_t statusDataset = static_cast<uint16_t>(std::lround(values[19]));
              if (datasetIndex == kAqmavDatasetImu)
              {
                ahrsRateTracker.record();
                imuRateTracker.record();
                latest_data_.roll_deg = values[0];
                latest_data_.pitch_deg = values[1];
                latest_data_.yaw_deg = values[2];
                latest_data_.imu_gyr_x_radps = values[3];
                latest_data_.imu_gyr_y_radps = values[4];
                latest_data_.imu_gyr_z_radps = values[5];
                latest_data_.ang_vel_x_radps = values[3];
                latest_data_.ang_vel_y_radps = values[4];
                latest_data_.ang_vel_z_radps = values[5];
                latest_data_.imu_acc_x_mps2 = values[6];
                latest_data_.imu_acc_y_mps2 = values[7];
                latest_data_.imu_acc_z_mps2 = values[8];
                latest_data_.mag_x_mg = values[9];
                latest_data_.mag_y_mg = values[10];
                latest_data_.mag_z_mg = values[11];
                latest_data_.imu_temp_c = std::numeric_limits<double>::quiet_NaN();
                latest_data_.pressure_pa = std::numeric_limits<double>::quiet_NaN();
                latest_data_.pressure_temp_c = std::numeric_limits<double>::quiet_NaN();
                latest_data_.pressure_altitude_m = std::numeric_limits<double>::quiet_NaN();
              }
              else if (datasetIndex == kAqmavDatasetImuRaw)
              {
                imuRateTracker.record();
                latest_data_.imu_acc_x_mps2 = values[0];
                latest_data_.imu_acc_y_mps2 = values[1];
                latest_data_.imu_acc_z_mps2 = values[2];
                latest_data_.imu_gyr_x_radps = values[3];
                latest_data_.imu_gyr_y_radps = values[4];
                latest_data_.imu_gyr_z_radps = values[5];
                latest_data_.mag_x_mg = values[6];
                latest_data_.mag_y_mg = values[7];
                latest_data_.mag_z_mg = values[8];
              }
              else if (datasetIndex == kAqmavDatasetUkf)
              {
                insGpsRateTracker.record();
                latest_data_.quat_w = values[6];
                latest_data_.quat_x = values[7];
                latest_data_.quat_y = values[8];
                latest_data_.quat_z = values[9];
                latest_data_.ned_n_m = values[10];
                latest_data_.ned_e_m = values[11];
                latest_data_.ned_d_m = values[12];
                latest_data_.vel_n_mps = values[13];
                latest_data_.vel_e_mps = values[14];
                latest_data_.vel_d_mps = values[15];
              }
              else if (datasetIndex == kAqmavDatasetGps)
              {
                rawGnssRateTracker.record();
                latest_data_.hacc_m = values[0];
                latest_data_.vacc_m = values[1];
                latest_data_.height_m = values[3];
                latest_data_.hdop = values[4];
                latest_data_.vel_n_mps = values[6];
                latest_data_.vel_e_mps = values[7];
                latest_data_.vel_d_mps = values[8];
                latest_data_.gnss_fix_code = static_cast<int>(std::lround(values[14]));
                latest_data_.gnss_fix_text = epsilonGnssFixName(latest_data_.gnss_fix_code);
                latest_data_.gnss_satellites = static_cast<int>(std::lround(values[15]));
              }

              if ((datasetIndex == kAqmavDatasetSystemsAndClock || statusDataset == kFdiTelemetrySystemsAndClock) &&
                  latest_data_.utc_unix_s == 0)
              {
                sysStateRateTracker.record();
                uint64_t utcSeconds = 0;
                uint32_t utcMicroseconds = 0;
                if (utcPartsToUnix(
                        static_cast<int>(std::lround(values[7])),
                        static_cast<int>(std::lround(values[8])),
                        static_cast<int>(std::lround(values[9])),
                        static_cast<int>(std::lround(values[10])),
                        static_cast<int>(std::lround(values[11])),
                        static_cast<double>(values[12]),
                        utcSeconds,
                        utcMicroseconds))
                {
                  latest_data_.utc_unix_s = utcSeconds;
                  latest_data_.utc_microseconds = utcMicroseconds;
                }
              }
            }
            break;
          default:
            break;
          }
          offset += mavlinkFrameSize;
        }
      }

      latest_data_.imu_packet_rate_hz = imuRateTracker.rate_hz;
      latest_data_.ahrs_packet_rate_hz = ahrsRateTracker.rate_hz;
      latest_data_.insgps_packet_rate_hz = insGpsRateTracker.rate_hz;
      latest_data_.sys_state_packet_rate_hz = sysStateRateTracker.rate_hz;
      latest_data_.raw_gnss_packet_rate_hz = rawGnssRateTracker.rate_hz;
      latest_data_.satellite_packet_rate_hz = satelliteRateTracker.rate_hz;
      latest_data_.geodetic_packet_rate_hz = geodeticRateTracker.rate_hz;
      latest_data_.ecef_packet_rate_hz = ecefRateTracker.rate_hz;

      callback = data_callback_;
      rawCallback = raw_frame_callback_;
    }

    if (!invalidEcefWarning.empty())
    {
      log(invalidEcefWarning);
    }

    if (rawCallback)
    {
      rawCallback(hostTimestampUs, packetId, serialNumber, frame.data(), frame.size());
    }

    recordDataReceived();
    if (callback && shouldEmitData())
    {
      updateLastEmitTime();
      callback();
    }
  };

  while (running_.load())
  {
    const ssize_t n = serial_.read(chunk, sizeof(chunk));
    if (n <= 0)
    {
      sleepMs(5);
      continue;
    }

    buffer.insert(buffer.end(), chunk, chunk + n);
    while (buffer.size() >= 8)
    {
      const auto head = std::find(buffer.begin(), buffer.end(), kFdilinkFrameHead);
      if (head == buffer.end())
      {
        buffer.clear();
        break;
      }
      if (head != buffer.begin())
      {
        buffer.erase(buffer.begin(), head);
      }
      if (buffer.size() < 8)
      {
        break;
      }

      const size_t payloadSize = buffer[2];
      const size_t frameSize = payloadSize + 8;
      if (buffer.size() < frameSize)
      {
        break;
      }

      const uint16_t headerCrc16 = static_cast<uint16_t>((static_cast<uint16_t>(buffer[5]) << 8) | buffer[6]);
      const bool crc8Ok = fdilinkCrc8(buffer.data(), 4) == buffer[4];
      const bool crc16Ok = fdilinkCrc16(buffer.data() + 7, payloadSize) == headerCrc16;
      const bool tailOk = buffer[frameSize - 1] == kFdilinkFrameTail;
      if (!crc8Ok || !crc16Ok || !tailOk)
      {
        if (!crc8Ok || !crc16Ok)
        {
          std::lock_guard<std::mutex> lock(mutex_);
          latest_data_.error_message = !crc8Ok ? "FDILink CRC8 mismatch" : "FDILink CRC16 mismatch";
        }
        buffer.erase(buffer.begin());
        continue;
      }

      std::vector<uint8_t> frame(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(frameSize));
      buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(frameSize));
      consumeFrame(frame, systemTimestampUs());
    }
  }
}

ImuData ImuCollector::getLatestData()
{
  std::lock_guard<std::mutex> lock(mutex_);
  return latest_data_;
}

void ImuCollector::setRawPacketCallback(RawPacketCallback callback)
{
  std::lock_guard<std::mutex> lock(mutex_);
  raw_packet_callback_ = std::move(callback);
}

bool ImuCollector::setOutputMessageType(const std::string& message_type)
{
  if (!isSupportedImuMessageType(message_type))
  {
    log("IMU: 不支持的输出消息类型: " + message_type);
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  output_message_type_ = message_type;
  return true;
}

std::string ImuCollector::outputMessageType() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return output_message_type_;
}

bool ImuCollector::sendAsciiCommand(const std::string& command, int wait_ms)
{
  if (!serial_.isOpen())
  {
    log("IMU: 串口未打开");
    return false;
  }

  const ssize_t written = serial_.write(command.c_str(), command.size());
  if (written != static_cast<ssize_t>(command.size()))
  {
    log("IMU: 命令发送失败: " + command);
    return false;
  }

  if (wait_ms > 0)
  {
    sleepMs(wait_ms);
  }
  return true;
}

bool ImuCollector::setDeviceSampleRate(int hz)
{
  double period = 0.0;
  if (!imuSampleRateToPeriod(hz, period))
  {
    log("IMU: 不支持的采样频率: " + std::to_string(hz) + " Hz");
    return false;
  }

  const std::string message_type = outputMessageType();
  if (!sendAsciiCommand("LOG HI91 ONTIME 0\r\n"))
  {
    return false;
  }
  if (!sendAsciiCommand("LOG HI92 ONTIME 0\r\n"))
  {
    return false;
  }

  char cmd[64];
  std::snprintf(cmd, sizeof(cmd), "LOG %s ONTIME %.3f\r\n", message_type.c_str(), period);
  if (!sendAsciiCommand(cmd))
  {
    log("IMU: 采样频率命令发送失败");
    return false;
  }

  log("IMU: 已将 " + message_type + " 采样频率设置为 " + std::to_string(hz) +
      " Hz（周期: " + std::to_string(period) + " 秒）");
  sample_rate_hz_.store(hz);
  return true;
}

bool ImuCollector::checkDeviceResponse()
{
  char buffer[2048];
  hipnuc_raw_t raw{};

  constexpr int max_wait_ms = 2000;
  constexpr int step_ms = 100;
  const auto start_time = std::chrono::steady_clock::now();
  int elapsed_ms = 0;

  while (elapsed_ms < max_wait_ms)
  {
    if (isCancelRequested())
    {
      return false;
    }
    log(formatDetectionProgress("等待IMU数据帧", 1, 1, computeRemainingSeconds(start_time, max_wait_ms)));
    ssize_t n = serial_.read(buffer, sizeof(buffer));
    if (n > 0)
    {
      for (ssize_t j = 0; j < n; j++)
      {
        int ret = hipnuc_input(&raw, static_cast<uint8_t>(buffer[j]));
        if (ret == 1)
        {
          return true;
        }
      }
    }
    sleepMs(step_ms);
    elapsed_ms += step_ms;
  }

  return false;
}

void ImuCollector::run()
{
  hipnuc_raw_t raw{};
  char chunk[2048];

  while (running_.load())
  {
    ssize_t n = serial_.read(chunk, sizeof(chunk));
    if (n > 0)
    {
      for (ssize_t i = 0; i < n; i++)
      {
        int ret = hipnuc_input(&raw, static_cast<uint8_t>(chunk[i]));
        if (ret == 1)
        {
          ImuData sample;
          sample.timestamp = std::chrono::steady_clock::now();
          sample.frame_type = ImuFrameType::Unknown;

          if (raw.hi83.tag == 0x83)
          {
            sample.valid = true;
            sample.from_hi83 = true;
            sample.frame_type = ImuFrameType::HI83;
            sample.system_time_us = raw.hi83.system_time_us;
            sample.acceleration[0] = raw.hi83.acc_b[0];
            sample.acceleration[1] = raw.hi83.acc_b[1];
            sample.acceleration[2] = raw.hi83.acc_b[2];
            sample.gyroscope[0] = raw.hi83.gyr_b[0];
            sample.gyroscope[1] = raw.hi83.gyr_b[1];
            sample.gyroscope[2] = raw.hi83.gyr_b[2];
            sample.rpy[0] = raw.hi83.rpy[0];
            sample.rpy[1] = raw.hi83.rpy[1];
            sample.rpy[2] = raw.hi83.rpy[2];
            sample.quaternion[0] = raw.hi83.quat[0];
            sample.quaternion[1] = raw.hi83.quat[1];
            sample.quaternion[2] = raw.hi83.quat[2];
            sample.quaternion[3] = raw.hi83.quat[3];
            sample.temperature = std::numeric_limits<double>::quiet_NaN();
            sample.air_pressure = std::numeric_limits<double>::quiet_NaN();
          }
          else if (raw.hi91.tag == 0x91)
          {
            sample.valid = true;
            sample.from_hi83 = false;
            sample.frame_type = ImuFrameType::HI91;
            sample.system_time_ms = raw.hi91.system_time;
            sample.acceleration[0] = raw.hi91.acc[0] * 9.8;
            sample.acceleration[1] = raw.hi91.acc[1] * 9.8;
            sample.acceleration[2] = raw.hi91.acc[2] * 9.8;
            sample.gyroscope[0] = raw.hi91.gyr[0];
            sample.gyroscope[1] = raw.hi91.gyr[1];
            sample.gyroscope[2] = raw.hi91.gyr[2];
            sample.rpy[0] = raw.hi91.roll;
            sample.rpy[1] = raw.hi91.pitch;
            sample.rpy[2] = raw.hi91.yaw;
            sample.quaternion[0] = raw.hi91.quat[0];
            sample.quaternion[1] = raw.hi91.quat[1];
            sample.quaternion[2] = raw.hi91.quat[2];
            sample.quaternion[3] = raw.hi91.quat[3];
            sample.temperature = std::numeric_limits<double>::quiet_NaN();
            sample.air_pressure = std::numeric_limits<double>::quiet_NaN();
          }
          else if (raw.hi92.tag == 0x92)
          {
            sample.valid = true;
            sample.from_hi83 = false;
            sample.frame_type = ImuFrameType::HI92;
            sample.acceleration[0] = static_cast<double>(raw.hi92.acc_b[0]) * 0.0048828;
            sample.acceleration[1] = static_cast<double>(raw.hi92.acc_b[1]) * 0.0048828;
            sample.acceleration[2] = static_cast<double>(raw.hi92.acc_b[2]) * 0.0048828;
            sample.gyroscope[0] = static_cast<double>(raw.hi92.gyr_b[0]) * 0.001;
            sample.gyroscope[1] = static_cast<double>(raw.hi92.gyr_b[1]) * 0.001;
            sample.gyroscope[2] = static_cast<double>(raw.hi92.gyr_b[2]) * 0.001;
            sample.rpy[0] = static_cast<double>(raw.hi92.roll) * 0.001;
            sample.rpy[1] = static_cast<double>(raw.hi92.pitch) * 0.001;
            sample.rpy[2] = static_cast<double>(raw.hi92.yaw) * 0.001;
            sample.quaternion[0] = static_cast<double>(raw.hi92.quat[0]) * 0.0001;
            sample.quaternion[1] = static_cast<double>(raw.hi92.quat[1]) * 0.0001;
            sample.quaternion[2] = static_cast<double>(raw.hi92.quat[2]) * 0.0001;
            sample.quaternion[3] = static_cast<double>(raw.hi92.quat[3]) * 0.0001;
            sample.temperature = std::numeric_limits<double>::quiet_NaN();
            sample.air_pressure = std::numeric_limits<double>::quiet_NaN();
          }
          else if (raw.hi81.tag == 0x81)
          {
            sample.valid = true;
            sample.from_hi83 = false;
            sample.frame_type = ImuFrameType::HI81;
            sample.acceleration[0] = static_cast<double>(raw.hi81.acc_b[0]);
            sample.acceleration[1] = static_cast<double>(raw.hi81.acc_b[1]);
            sample.acceleration[2] = static_cast<double>(raw.hi81.acc_b[2]);
            sample.gyroscope[0] = static_cast<double>(raw.hi81.gyr_b[0]);
            sample.gyroscope[1] = static_cast<double>(raw.hi81.gyr_b[1]);
            sample.gyroscope[2] = static_cast<double>(raw.hi81.gyr_b[2]);
            sample.rpy[0] = raw.hi81.roll / 100.0;
            sample.rpy[1] = raw.hi81.pitch / 100.0;
            sample.rpy[2] = raw.hi81.yaw / 100.0;
            sample.temperature = std::numeric_limits<double>::quiet_NaN();
          }

          if (sample.valid)
          {
            DataCallback callback;
            RawPacketCallback raw_callback;
            {
              std::lock_guard<std::mutex> lock(mutex_);
              latest_data_ = sample;
              callback = data_callback_;
              raw_callback = raw_packet_callback_;
            }

            recordDataReceived();

            if (raw_callback)
            {
              const size_t packet_size = static_cast<size_t>(raw.len + 6);
              uint8_t frame_tag = 0;
              switch (sample.frame_type)
              {
              case ImuFrameType::HI81:
                frame_tag = 0x81;
                break;
              case ImuFrameType::HI83:
                frame_tag = 0x83;
                break;
              case ImuFrameType::HI91:
                frame_tag = 0x91;
                break;
              case ImuFrameType::HI92:
                frame_tag = 0x92;
                break;
              case ImuFrameType::Unknown:
              default:
                break;
              }
              raw_callback(systemTimestampUs(), frame_tag, raw.buf, packet_size);
            }

            if (callback && shouldEmitData())
            {
              updateLastEmitTime();
              callback();
            }
          }
        }
      }
    }
    else
    {
      sleepMs(5);
    }
  }
}

PtbData PtbCollector::getLatestData()
{
  std::lock_guard<std::mutex> lock(mutex_);
  return latest_data_;
}

void PtbCollector::setRawResponseCallback(RawResponseCallback callback)
{
  std::lock_guard<std::mutex> lock(mutex_);
  raw_response_callback_ = std::move(callback);
}

void PtbCollector::setProtocol(PressureSensorProtocol protocol)
{
  std::lock_guard<std::mutex> lock(mutex_);
  protocol_ = protocol;
}

PressureSensorProtocol PtbCollector::protocol() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return protocol_;
}

bool PtbCollector::setDeviceSampleRate(int hz)
{
  if (protocol() == PressureSensorProtocol::Bmp390Serial)
  {
    sample_rate_hz_.store(std::max(1, hz));
    return true;
  }

  if (hz < 1 || hz > 70)
  {
    log("PTB210: 不支持的采样频率: " + std::to_string(hz) + " Hz（有效范围: 1-70 Hz）");
    return false;
  }

  int mpm = hz * 60;
  if (mpm < 6) mpm = 6;
  if (mpm > 4200) mpm = 4200;

  serial_.write(PtbCollector::PTB_CMD_STOP, std::strlen(PtbCollector::PTB_CMD_STOP));
  sleepMs(50);

  const char* average_cmd = ".AVRG.0\r";
  ssize_t written = serial_.write(average_cmd, std::strlen(average_cmd));
  if (written != static_cast<ssize_t>(std::strlen(average_cmd)))
  {
    log("PTB210: 发送 AVRG 命令失败");
    return false;
  }

  sleepMs(50);

  char cmd[32];
  snprintf(cmd, sizeof(cmd), ".MPM.%d\r", mpm);
  
  written = serial_.write(cmd, strlen(cmd));
  if (written != static_cast<ssize_t>(std::strlen(cmd)))
  {
    log("PTB210: 发送 MPM 命令失败");
    return false;
  }

  sleepMs(100);

  const char* reset_cmd = ".RESET\r";
  written = serial_.write(reset_cmd, strlen(reset_cmd));
  if (written != static_cast<ssize_t>(std::strlen(reset_cmd)))
  {
    log("PTB210: 发送 RESET 命令失败");
    return false;
  }

  sleepMs(500);

  if (running_.load())
  {
    written = serial_.write(PTB_CMD_CONTINUOUS, std::strlen(PTB_CMD_CONTINUOUS));
    if (written != static_cast<ssize_t>(std::strlen(PTB_CMD_CONTINUOUS)))
    {
      log("PTB210: 恢复连续输出失败");
      return false;
    }
  }

  log("PTB210: 已将采样频率设置为 " + std::to_string(hz) + " Hz（MPM: " + std::to_string(mpm) + "）");
  sample_rate_hz_.store(hz);
  return true;
}

bool PtbCollector::initialize()
{
  return serial_.setNonBlocking(true);
}

void PtbCollector::cleanup()
{
  if (!serial_.isOpen())
  {
    return;
  }

  if (protocol() == PressureSensorProtocol::Ptb210)
  {
    serial_.write(PTB_CMD_STOP, std::strlen(PTB_CMD_STOP));
    sleepMs(50);
  }
  serial_.flush();
}

bool PtbCollector::checkDeviceResponse()
{
  if (protocol() == PressureSensorProtocol::Bmp390Serial)
  {
    char chunk[256];
    std::string buffer;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1800);
    serial_.flush();
    while (std::chrono::steady_clock::now() < deadline)
    {
      if (isCancelRequested())
      {
        return false;
      }
      const ssize_t n = serial_.read(chunk, sizeof(chunk));
      if (n > 0)
      {
        buffer.append(chunk, static_cast<size_t>(n));
        size_t line_end = std::string::npos;
        while ((line_end = buffer.find_first_of("\r\n")) != std::string::npos)
        {
          const std::string line = buffer.substr(0, line_end);
          buffer.erase(0, line_end + 1);
          EnvironmentSerialValues values;
          if (parseEnvironmentSerialLine(line, values) && values.has_pressure)
          {
            return true;
          }
        }
      }
      else
      {
        sleepMs(10);
      }
    }
    return false;
  }

  char response[256];
  constexpr int max_attempts = 5;
  constexpr int total_wait_ms = 500;
  constexpr int poll_interval_ms = 20;

  auto isNumericPressure = [](std::string value) {
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' '))
    {
      value.pop_back();
    }
    while (!value.empty() && value.front() == ' ')
    {
      value.erase(0, 1);
    }
    if (value.empty())
    {
      return false;
    }
    try
    {
      (void)std::stod(value);
      return true;
    }
    catch (const std::exception&)
    {
      return false;
    }
  };

  for (int i = 0; i < max_attempts; i++)
  {
    if (isCancelRequested())
    {
      return false;
    }

    const auto attempt_start = std::chrono::steady_clock::now();
    std::string buffer;
    buffer.reserve(256);

    log(formatDetectionProgress("发送PTB压力查询", i + 1, max_attempts, computeRemainingSeconds(attempt_start, total_wait_ms)));
    serial_.flush();
    if (serial_.write(PTB_CMD_PRESSURE, std::strlen(PTB_CMD_PRESSURE)) != static_cast<ssize_t>(std::strlen(PTB_CMD_PRESSURE)))
    {
      log("PTB210: 发送压力探测命令失败");
      continue;
    }

    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - attempt_start).count() < total_wait_ms)
    {
      if (isCancelRequested())
      {
        return false;
      }

      log(formatDetectionProgress("等待PTB压力返回", i + 1, max_attempts, computeRemainingSeconds(attempt_start, total_wait_ms)));
      ssize_t n = serial_.read(response, sizeof(response));
      if (n > 0)
      {
        buffer.append(response, static_cast<size_t>(n));

        size_t line_end = std::string::npos;
        while ((line_end = buffer.find_first_of("\r\n")) != std::string::npos)
        {
          std::string line = buffer.substr(0, line_end);
          buffer.erase(0, line_end + 1);
          while (!buffer.empty() && (buffer.front() == '\r' || buffer.front() == '\n'))
          {
            buffer.erase(0, 1);
          }

          if (isNumericPressure(line))
          {
            return true;
          }
        }
      }
      else
      {
        sleepMs(poll_interval_ms);
      }
    }

    if (isNumericPressure(buffer))
    {
      return true;
    }
  }
  
  return false;
}

void PtbCollector::run()
{
  if (protocol() == PressureSensorProtocol::Bmp390Serial)
  {
    char chunk[256];
    std::string buffer;
    buffer.reserve(512);
    while (running_.load())
    {
      const ssize_t n = serial_.read(chunk, sizeof(chunk));
      if (n <= 0)
      {
        sleepMs(5);
        continue;
      }
      buffer.append(chunk, static_cast<size_t>(n));
      trimUnterminatedLineBuffer(buffer, kPtbLineBufferMaxBytes, kPtbLineBufferKeepBytes);
      size_t line_end = std::string::npos;
      while ((line_end = buffer.find_first_of("\r\n")) != std::string::npos)
      {
        const std::string raw_line = buffer.substr(0, line_end + 1);
        const std::string line = buffer.substr(0, line_end);
        buffer.erase(0, line_end + 1);
        while (!buffer.empty() && (buffer.front() == '\r' || buffer.front() == '\n'))
        {
          buffer.erase(0, 1);
        }

        EnvironmentSerialValues values;
        if (!parseEnvironmentSerialLine(line, values) || !values.has_pressure)
        {
          continue;
        }

        DataCallback callback;
        RawResponseCallback raw_callback;
        const uint64_t host_timestamp_us = systemTimestampUs();
        {
          std::lock_guard<std::mutex> lock(mutex_);
          latest_data_.pressure_hpa = values.pressure_hpa;
          latest_data_.valid = true;
          latest_data_.timestamp = std::chrono::steady_clock::now();
          latest_data_.error_message.clear();
          callback = data_callback_;
          raw_callback = raw_response_callback_;
        }
        recordDataReceived();
        if (raw_callback)
        {
          raw_callback(host_timestamp_us,
                       reinterpret_cast<const uint8_t*>(raw_line.data()),
                       raw_line.size());
        }
        if (callback && shouldEmitData())
        {
          updateLastEmitTime();
          callback();
        }
      }
    }
    return;
  }

  char chunk[256];
  std::string buffer;
  buffer.reserve(512);
  bool line_buffer_limit_logged = false;

  auto startContinuousOutput = [this]() -> bool {
    serial_.flush();
    ssize_t written = serial_.write(PTB_CMD_CONTINUOUS, std::strlen(PTB_CMD_CONTINUOUS));
    if (written != static_cast<ssize_t>(std::strlen(PTB_CMD_CONTINUOUS)))
    {
      log("PTB210: 启动连续输出失败");
      return false;
    }
    return true;
  };

  auto processLine = [this](std::string line, const std::string& raw_line) -> bool {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
    {
      line.pop_back();
    }

    if (line.empty())
    {
      return false;
    }

    try
    {
      double pressure = std::stod(line);
      DataCallback callback;
      RawResponseCallback raw_callback;
      const uint64_t host_timestamp_us = systemTimestampUs();
      {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_data_.pressure_hpa = pressure;
        latest_data_.valid = true;
        latest_data_.timestamp = std::chrono::steady_clock::now();
        latest_data_.error_message.clear();
        callback = data_callback_;
        raw_callback = raw_response_callback_;
      }

      recordDataReceived();

      if (raw_callback && !raw_line.empty())
      {
        raw_callback(host_timestamp_us, reinterpret_cast<const uint8_t*>(raw_line.data()), raw_line.size());
      }

      if (callback && shouldEmitData())
      {
        updateLastEmitTime();
        callback();
      }
      return true;
    }
    catch (const std::exception&)
    {
      // Ignore command echoes or control lines and wait for the next numeric sample.
      return false;
    }
  };

  // PTB210 needs a short recovery window after .RESET before it accepts .BP reliably.
  sleepMs(500);
  if (!startContinuousOutput())
  {
    return;
  }

  auto last_data_time = std::chrono::steady_clock::now();
  auto last_bp_time = std::chrono::steady_clock::now();

  while (running_.load())
  {
    ssize_t n = serial_.read(chunk, sizeof(chunk));
    if (n > 0)
    {
      buffer.append(chunk, static_cast<size_t>(n));
      if (buffer.find_first_of("\r\n") == std::string::npos)
      {
        const size_t dropped = trimUnterminatedLineBuffer(buffer, kPtbLineBufferMaxBytes, kPtbLineBufferKeepBytes);
        if (dropped > 0 && !line_buffer_limit_logged)
        {
          log(isEnglishLog()
              ? "PTB210: dropped oversized unterminated line buffer while waiting for newline"
              : "PTB210：长时间未收到换行，已丢弃过长文本缓冲");
          line_buffer_limit_logged = true;
        }
      }

      size_t line_end = std::string::npos;
      while ((line_end = buffer.find_first_of("\r\n")) != std::string::npos)
      {
        if (line_end > kPtbLineBufferMaxBytes)
        {
          buffer.erase(0, line_end + 1);
          if (!line_buffer_limit_logged)
          {
            log(isEnglishLog()
                ? "PTB210: dropped oversized text line"
                : "PTB210：已丢弃超长文本行");
          }
          line_buffer_limit_logged = false;
          continue;
        }
        const std::string raw_line = buffer.substr(0, line_end + 1);
        const std::string line = raw_line;
        buffer.erase(0, line_end + 1);
        line_buffer_limit_logged = false;
        while (!buffer.empty() && (buffer.front() == '\r' || buffer.front() == '\n'))
        {
          buffer.erase(0, 1);
        }

        if (processLine(line, raw_line))
        {
          last_data_time = std::chrono::steady_clock::now();
        }
      }
    }
    else
    {
      const int interval_ms = std::max(50, 1000 / std::max(1, getSampleRate()));
      const int restart_timeout_ms = std::max(1000, interval_ms * 4);
      const auto now = std::chrono::steady_clock::now();
      const auto silence_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_data_time).count();
      const auto since_bp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_bp_time).count();
      if (silence_ms >= restart_timeout_ms && since_bp_ms >= 500)
      {
        log("PTB210: 暂未收到连续数据，正在重试 .BP");
        if (startContinuousOutput())
        {
          last_bp_time = std::chrono::steady_clock::now();
        }
      }
      sleepMs(5);
    }
  }
}

HmpData HmpCollector::getLatestData()
{
  std::lock_guard<std::mutex> lock(mutex_);
  return latest_data_;
}

void HmpCollector::setRawResponseCallback(RawResponseCallback callback)
{
  std::lock_guard<std::mutex> lock(mutex_);
  raw_response_callback_ = std::move(callback);
}

void HmpCollector::setProtocol(HumiditySensorProtocol protocol)
{
  std::lock_guard<std::mutex> lock(mutex_);
  protocol_ = protocol;
}

HumiditySensorProtocol HmpCollector::protocol() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return protocol_;
}

bool HmpCollector::initialize()
{
  serial_.setNonBlocking(true);
  return true;
}

bool HmpCollector::checkDeviceResponse()
{
  if (protocol() == HumiditySensorProtocol::Sht45Serial)
  {
    char chunk[256];
    std::string buffer;
    bool has_temperature = false;
    bool has_humidity = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1800);
    serial_.flush();
    while (std::chrono::steady_clock::now() < deadline)
    {
      if (isCancelRequested())
      {
        return false;
      }
      const ssize_t n = serial_.read(chunk, sizeof(chunk));
      if (n > 0)
      {
        buffer.append(chunk, static_cast<size_t>(n));
        size_t line_end = std::string::npos;
        while ((line_end = buffer.find_first_of("\r\n")) != std::string::npos)
        {
          const std::string line = buffer.substr(0, line_end);
          buffer.erase(0, line_end + 1);
          EnvironmentSerialValues values;
          if (parseEnvironmentSerialLine(line, values))
          {
            has_temperature = has_temperature || values.has_temperature;
            has_humidity = has_humidity || values.has_humidity;
            if (has_temperature && has_humidity)
            {
              return true;
            }
          }
        }
      }
      else
      {
        sleepMs(10);
      }
    }
    return false;
  }

  uint8_t request[8];
  uint8_t response[64];
  constexpr uint8_t function_code = 0x03;

  request[0] = HMP3_SLAVE_ADDR;
  request[1] = function_code;
  request[2] = 0x00;
  request[3] = 0x00;
  request[4] = 0x00;
  request[5] = 0x04;
  uint16_t crc = modbusCrc16(request, 6);
  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  int max_attempts = 5;

  for (int i = 0; i < max_attempts; i++)
  {
    if (isCancelRequested())
    {
      return false;
    }
    constexpr int total_wait_ms = 500;
    const auto attempt_start = std::chrono::steady_clock::now();
    log(formatDetectionProgress("发送HMP寄存器查询", i + 1, max_attempts, computeRemainingSeconds(attempt_start, total_wait_ms)));
    serial_.flush();
    serial_.write(request, 8);
    constexpr int step_ms = 25;
    size_t total = 0;
    int elapsed = 0;
    while (elapsed < total_wait_ms && total < sizeof(response))
    {
      if (isCancelRequested())
      {
        return false;
      }
      log(formatDetectionProgress("等待HMP寄存器返回", i + 1, max_attempts, computeRemainingSeconds(attempt_start, total_wait_ms)));
      ssize_t chunk = serial_.read(response + total, sizeof(response) - total);
      if (chunk > 0)
      {
        total += static_cast<size_t>(chunk);
      }
      sleepMs(step_ms);
      elapsed += step_ms;
    }

    ssize_t n = static_cast<ssize_t>(total);
    float humidity = 0.0f;
    float temperature = 0.0f;
    uint8_t exception_code = 0;
    const size_t response_size = n > 0 ? static_cast<size_t>(n) : 0U;
    HmpParseResult parsed = parseHmpResponse(response, response_size, humidity, temperature, exception_code, function_code);
    if (parsed == HmpParseResult::Data)
    {
      return true;
    }
  }

  return false;
}

uint16_t HmpCollector::modbusCrc16(const uint8_t* data, size_t len)
{
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++)
  {
    crc ^= data[i];
    for (int j = 0; j < 8; j++)
    {
      if (crc & 0x0001)
      {
        crc >>= 1;
        crc ^= 0xA001;
      }
      else
      {
        crc >>= 1;
      }
    }
  }
  return crc;
}

float HmpCollector::decodeFloatLE(uint16_t reg0, uint16_t reg1)
{
  uint32_t val = static_cast<uint32_t>(reg0) | (static_cast<uint32_t>(reg1) << 16);
  float result;
  std::memcpy(&result, &val, sizeof(float));
  return result;
}

void HmpCollector::run()
{
  if (protocol() == HumiditySensorProtocol::Sht45Serial)
  {
    char chunk[256];
    std::string buffer;
    buffer.reserve(512);
    double pending_temperature = 0.0;
    double pending_humidity = 0.0;
    bool has_temperature = false;
    bool has_humidity = false;

    while (running_.load())
    {
      const ssize_t n = serial_.read(chunk, sizeof(chunk));
      if (n <= 0)
      {
        sleepMs(5);
        continue;
      }
      buffer.append(chunk, static_cast<size_t>(n));
      trimUnterminatedLineBuffer(buffer, kPtbLineBufferMaxBytes, kPtbLineBufferKeepBytes);
      size_t line_end = std::string::npos;
      while ((line_end = buffer.find_first_of("\r\n")) != std::string::npos)
      {
        const std::string raw_line = buffer.substr(0, line_end + 1);
        const std::string line = buffer.substr(0, line_end);
        buffer.erase(0, line_end + 1);
        while (!buffer.empty() && (buffer.front() == '\r' || buffer.front() == '\n'))
        {
          buffer.erase(0, 1);
        }

        EnvironmentSerialValues values;
        if (!parseEnvironmentSerialLine(line, values))
        {
          continue;
        }
        if (values.has_temperature)
        {
          pending_temperature = values.temperature_c;
          has_temperature = true;
        }
        if (values.has_humidity)
        {
          pending_humidity = values.humidity_rh;
          has_humidity = true;
        }

        RawResponseCallback raw_callback;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          raw_callback = raw_response_callback_;
        }
        if (raw_callback)
        {
          raw_callback(systemTimestampUs(),
                       reinterpret_cast<const uint8_t*>(raw_line.data()),
                       raw_line.size());
        }

        if (!has_temperature || !has_humidity)
        {
          continue;
        }

        DataCallback callback;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          latest_data_.temperature = pending_temperature;
          latest_data_.humidity = pending_humidity;
          latest_data_.valid = true;
          latest_data_.timestamp = std::chrono::steady_clock::now();
          latest_data_.error_message.clear();
          callback = data_callback_;
        }
        recordDataReceived();
        has_temperature = false;
        has_humidity = false;
        if (callback && shouldEmitData())
        {
          updateLastEmitTime();
          callback();
        }
      }
    }
    return;
  }

  uint8_t request[8];
  uint8_t response[64];

  while (running_.load())
  {
    auto start_time = std::chrono::steady_clock::now();
    const int requested_rate_hz = getSampleRate();
    const int interval_ms = std::max(1, 1000 / requested_rate_hz);
    const int response_wait_ms = std::min(500, std::max(100, interval_ms));
    
    request[0] = HMP3_SLAVE_ADDR;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x00;
    request[4] = 0x00;
    request[5] = 0x04;
    uint16_t crc = modbusCrc16(request, 6);
    request[6] = crc & 0xFF;
    request[7] = (crc >> 8) & 0xFF;

    serial_.write(request, 8);
    float humidity = 0.0f;
    float temperature = 0.0f;
    uint8_t exception_code = 0;
    HmpParseResult parsed = HmpParseResult::None;
    size_t raw_frame_offset = 0;
    size_t raw_frame_size = 0;
    size_t total = 0;
    int elapsed_wait_ms = 0;
    constexpr int step_ms = 25;

    while (running_.load() && elapsed_wait_ms < response_wait_ms && total < sizeof(response))
    {
      ssize_t chunk = serial_.read(response + total, sizeof(response) - total);
      if (chunk > 0)
      {
        total += static_cast<size_t>(chunk);
        parsed = parseHmpResponse(response, total, humidity, temperature, exception_code, 0x03, &raw_frame_offset, &raw_frame_size);
        if (parsed != HmpParseResult::None)
        {
          break;
        }
      }

      sleepMs(step_ms);
      elapsed_wait_ms += step_ms;
    }

    if (parsed == HmpParseResult::None && total > 0)
    {
      parsed = parseHmpResponse(response, total, humidity, temperature, exception_code, 0x03, &raw_frame_offset, &raw_frame_size);
    }

    if (parsed == HmpParseResult::Data)
    {
      DataCallback callback;
      RawResponseCallback raw_callback;
      const uint64_t host_timestamp_us = systemTimestampUs();
      {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_data_.humidity = humidity;
        latest_data_.temperature = temperature;
        latest_data_.valid = true;
        latest_data_.timestamp = std::chrono::steady_clock::now();
        latest_data_.error_message.clear();
        callback = data_callback_;
        raw_callback = raw_response_callback_;
      }

      recordDataReceived();

      if (raw_callback && raw_frame_size > 0 && raw_frame_offset + raw_frame_size <= total)
      {
        raw_callback(host_timestamp_us, response + raw_frame_offset, raw_frame_size);
      }

      if (callback && shouldEmitData())
      {
        updateLastEmitTime();
        callback();
      }
    }
    else if (parsed == HmpParseResult::Exception)
    {
      std::lock_guard<std::mutex> lock(mutex_);
      latest_data_.valid = false;
      latest_data_.error_message = "Modbus 错误: " + std::to_string(exception_code);
    }
    else if (parsed == HmpParseResult::CrcError)
    {
      std::lock_guard<std::mutex> lock(mutex_);
      latest_data_.valid = false;
      latest_data_.error_message = "CRC 校验错误";
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();
    int remaining = interval_ms - static_cast<int>(elapsed);
    if (remaining > 0)
    {
      sleepMs(remaining);
    }
  }
}

TemperatureControllerData TemperatureControllerCollector::getLatestData()
{
  std::lock_guard<std::mutex> lock(mutex_);
  return latest_data_;
}

void TemperatureControllerCollector::setRawFrameCallback(RawFrameCallback callback)
{
  std::lock_guard<std::mutex> lock(mutex_);
  raw_frame_callback_ = std::move(callback);
}

void TemperatureControllerCollector::setSlaveAddress(uint8_t slave_address)
{
  slave_address_ = slave_address == 0 ? 1 : slave_address;
}

uint8_t TemperatureControllerCollector::slaveAddress() const
{
  return slave_address_;
}

bool TemperatureControllerCollector::initialize()
{
  channel_count_ = 1;
  polynomial_exponents_supported_ = true;
  serial_.setNonBlocking(true);
  return true;
}

void TemperatureControllerCollector::publishRawFrame(const std::vector<uint8_t>& frame)
{
  RawFrameCallback raw_callback;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    raw_callback = raw_frame_callback_;
  }
  if (raw_callback && !frame.empty())
  {
    raw_callback(systemTimestampUs(), frame.data(), frame.size());
  }
}

bool TemperatureControllerCollector::readResponseFrame(uint8_t function_code, std::vector<uint8_t>& frame, int wait_ms)
{
  frame.clear();
  std::vector<uint8_t> buffer;
  buffer.reserve(64);
  const auto start = std::chrono::steady_clock::now();
  uint8_t chunk[64];
  while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < wait_ms)
  {
    ssize_t read_bytes = serial_.read(chunk, sizeof(chunk));
    if (read_bytes > 0)
    {
      buffer.insert(buffer.end(), chunk, chunk + read_bytes);
      while (buffer.size() >= 5)
      {
        auto it = std::find(buffer.begin(), buffer.end(), slave_address_);
        if (it == buffer.end())
        {
          buffer.clear();
          break;
        }
        if (it != buffer.begin())
        {
          buffer.erase(buffer.begin(), it);
        }
        if (buffer.size() < 5)
        {
          break;
        }
        const uint8_t received_function = buffer[1];
        size_t frame_size = 0;
        if (received_function == function_code)
        {
          if (function_code == 0x03)
          {
            frame_size = static_cast<size_t>(buffer[2]) + 5;
          }
          else if (function_code == 0x10)
          {
            frame_size = 8;
          }
          else
          {
            buffer.erase(buffer.begin());
            continue;
          }
        }
        else if (received_function == static_cast<uint8_t>(function_code | 0x80u))
        {
          frame_size = 5;
        }
        else
        {
          buffer.erase(buffer.begin());
          continue;
        }
        if (buffer.size() < frame_size)
        {
          break;
        }
        frame.assign(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(frame_size));
        publishRawFrame(frame);
        return true;
      }
    }
    else
    {
      sleepMs(10);
    }
  }
  return false;
}

bool TemperatureControllerCollector::readRegisters(uint16_t address, uint16_t count, std::vector<uint16_t>& registers, int wait_ms)
{
  std::lock_guard<std::mutex> lock(modbus_mutex_);
  return readRegistersUnlocked(address, count, registers, wait_ms);
}

bool TemperatureControllerCollector::readRegistersUnlocked(uint16_t address, uint16_t count, std::vector<uint16_t>& registers, int wait_ms)
{
  using namespace TemperatureControllerProtocol;
  const QByteArray request = buildReadRegistersRequest(slave_address_, address, count);
  sleepMs(kTemperatureControllerModbusCommandGapMs);
  serial_.flush();
  if (serial_.write(request.constData(), static_cast<size_t>(request.size())) != request.size())
  {
    return false;
  }
  std::vector<uint8_t> frame;
  if (!readResponseFrame(0x03, frame, wait_ms))
  {
    return false;
  }
  const QByteArray response(reinterpret_cast<const char*>(frame.data()), static_cast<int>(frame.size()));
  const ReadResponse parsed = parseReadRegistersResponse(response, slave_address_, count);
  if (parsed.status != FrameStatus::Ok)
  {
    return false;
  }
  registers.assign(parsed.registers.cbegin(), parsed.registers.cend());
  return true;
}

bool TemperatureControllerCollector::queryAscii(const std::string& command, std::string& response, int wait_ms)
{
  std::lock_guard<std::mutex> lock(modbus_mutex_);
  response.clear();
  sleepMs(kTemperatureControllerModbusCommandGapMs);
  serial_.flush();
  if (serial_.write(command.data(), command.size()) != static_cast<ssize_t>(command.size()))
  {
    return false;
  }

  const auto start = std::chrono::steady_clock::now();
  auto last_data = start;
  uint8_t chunk[256];
  while (std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - start).count() < wait_ms)
  {
    const ssize_t read_bytes = serial_.read(chunk, sizeof(chunk));
    if (read_bytes > 0)
    {
      response.append(reinterpret_cast<const char*>(chunk), static_cast<size_t>(read_bytes));
      last_data = std::chrono::steady_clock::now();
    }
    else
    {
      const auto quiet_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - last_data).count();
      if (!response.empty() && quiet_ms >= 80)
      {
        break;
      }
      sleepMs(10);
    }
  }

  if (!response.empty())
  {
    publishRawFrame(std::vector<uint8_t>(response.cbegin(), response.cend()));
  }
  return !response.empty();
}

bool TemperatureControllerCollector::writeRegisters(uint16_t address, const std::vector<uint16_t>& registers, int wait_ms)
{
  std::lock_guard<std::mutex> lock(modbus_mutex_);
  return writeRegistersUnlocked(address, registers, wait_ms);
}

bool TemperatureControllerCollector::writeRegistersUnlocked(uint16_t address, const std::vector<uint16_t>& registers, int wait_ms)
{
  using namespace TemperatureControllerProtocol;
  const QVector<uint16_t> values(registers.cbegin(), registers.cend());
  const QByteArray request = buildWriteRegistersRequest(slave_address_, address, values);
  sleepMs(kTemperatureControllerModbusCommandGapMs);
  serial_.flush();
  if (serial_.write(request.constData(), static_cast<size_t>(request.size())) != request.size())
  {
    return false;
  }
  std::vector<uint8_t> frame;
  if (!readResponseFrame(0x10, frame, wait_ms))
  {
    return false;
  }
  const QByteArray response(reinterpret_cast<const char*>(frame.data()), static_cast<int>(frame.size()));
  const WriteResponse parsed = parseWriteRegistersResponse(response, slave_address_, address, static_cast<uint16_t>(registers.size()));
  return parsed.status == FrameStatus::Ok;
}

bool TemperatureControllerCollector::writeAndConfirm(uint8_t channel, uint16_t address, const std::vector<uint16_t>& registers)
{
  std::lock_guard<std::mutex> lock(modbus_mutex_);
  if (!writeRegistersUnlocked(address, registers, 200))
  {
    return false;
  }
  std::vector<uint16_t> read_back;
  if (!readRegistersUnlocked(address, static_cast<uint16_t>(registers.size()), read_back, 200))
  {
    return false;
  }
  (void)channel;
  return read_back == registers;
}

bool TemperatureControllerCollector::readChannel(uint8_t channel, TemperatureControllerChannelData& channel_data)
{
  using namespace TemperatureControllerProtocol;
  std::vector<uint16_t> registers;
  if (!readRegisters(channelAddress(channel, Register::TargetTemperature), 2, registers)) return false;
  channel_data.target_temperature_c = rawToTemperatureCelsius(decodeInt32(QVector<uint16_t>(registers.cbegin(), registers.cend())));
  if (!readRegisters(channelAddress(channel, Register::MeasuredTemperature), 2, registers)) return false;
  channel_data.measured_temperature_c = rawToTemperatureCelsius(decodeInt32(QVector<uint16_t>(registers.cbegin(), registers.cend())));
  if (!readRegisters(channelAddress(channel, Register::OutputEnabled), 1, registers)) return false;
  channel_data.output_enabled = registers[0] != 0;
  if (!readRegisters(channelAddress(channel, Register::OutputMode), 1, registers)) return false;
  channel_data.output_mode = registers[0];
  if (!readRegisters(channelAddress(channel, Register::AutoPid), 1, registers)) return false;
  channel_data.auto_pid_mode = static_cast<int>(registers[0]);
  if (!readRegisters(channelAddress(channel, Register::TemperatureSlope), 1, registers)) return false;
  channel_data.temperature_slope_c_per_s = static_cast<double>(decodeUInt16(QVector<uint16_t>(registers.cbegin(), registers.cend()))) / 1000.0;
  if (!readRegisters(channelAddress(channel, Register::MaxOutputPercent), 1, registers)) return false;
  channel_data.max_output_percent = static_cast<int>(registers[0]);
  if (!readRegisters(channelAddress(channel, Register::StartupDelay), 1, registers)) return false;
  channel_data.startup_delay_s = static_cast<int>(decodeUInt16(QVector<uint16_t>(registers.cbegin(), registers.cend())));
  if (!readRegisters(channelAddress(channel, Register::Kp), 2, registers)) return false;
  channel_data.kp = static_cast<int>(decodeUInt32(QVector<uint16_t>(registers.cbegin(), registers.cend())));
  if (!readRegisters(channelAddress(channel, Register::Ki), 2, registers)) return false;
  channel_data.ki = static_cast<int>(decodeUInt32(QVector<uint16_t>(registers.cbegin(), registers.cend())));
  if (!readRegisters(channelAddress(channel, Register::Kd), 2, registers)) return false;
  channel_data.kd = static_cast<int>(decodeUInt32(QVector<uint16_t>(registers.cbegin(), registers.cend())));
  if (!readRegisters(channelAddress(channel, Register::OutputDuty), 4, registers)) return false;
  channel_data.output_percent = static_cast<double>(decodeInt64(QVector<uint16_t>(registers.cbegin(), registers.cend()))) / 20000.0;
  if (!readRegisters(channelAddress(channel, Register::OutputCurrent), 1, registers)) return false;
  channel_data.output_current_a = registers[0] / 1000.0;
  if (!readRegisters(channelAddress(channel, Register::Resistor), 4, registers)) return false;
  channel_data.sensor_resistance_ohm = static_cast<double>(decodeUInt64(QVector<uint16_t>(registers.cbegin(), registers.cend()))) / 1000000.0;
  if (!readRegisters(channelAddress(channel, Register::SensorModel), 1, registers)) return false;
  channel_data.sensor_model = static_cast<int>(decodeUInt16(QVector<uint16_t>(registers.cbegin(), registers.cend())));
  if (!readRegisters(channelAddress(channel, Register::NtcB), 2, registers)) return false;
  channel_data.ntc_b = static_cast<int>(decodeUInt32(QVector<uint16_t>(registers.cbegin(), registers.cend())));
  if (!readRegisters(channelAddress(channel, Register::NtcR0), 2, registers)) return false;
  channel_data.ntc_r0 = static_cast<int>(decodeUInt32(QVector<uint16_t>(registers.cbegin(), registers.cend())));
  if (!readRegisters(channelAddress(channel, Register::PtR0), 2, registers)) return false;
  channel_data.pt_r0 = static_cast<int>(decodeUInt32(QVector<uint16_t>(registers.cbegin(), registers.cend())));
  if (!readRegisters(channelAddress(channel, Register::PtA), 2, registers)) return false;
  channel_data.pt_a = static_cast<int>(decodeInt32(QVector<uint16_t>(registers.cbegin(), registers.cend())));
  if (!readRegisters(channelAddress(channel, Register::PtB), 2, registers)) return false;
  channel_data.pt_b = static_cast<int>(decodeInt32(QVector<uint16_t>(registers.cbegin(), registers.cend())));
  if (!readRegisters(channelAddress(channel, Register::PtC), 2, registers)) return false;
  channel_data.pt_c = static_cast<int>(decodeInt32(QVector<uint16_t>(registers.cbegin(), registers.cend())));
  if (!readRegisters(channelAddress(channel, Register::OvertempUpper), 2, registers)) return false;
  channel_data.overtemp_upper_c = rawToTemperatureCelsius(decodeInt32(QVector<uint16_t>(registers.cbegin(), registers.cend())));
  if (!readRegisters(channelAddress(channel, Register::OvertempLower), 2, registers)) return false;
  channel_data.overtemp_lower_c = rawToTemperatureCelsius(decodeInt32(QVector<uint16_t>(registers.cbegin(), registers.cend())));
  for (int i = 0; i < 8; ++i)
  {
    const auto mantissaRegister = static_cast<Register>(
        static_cast<quint16>(Register::PolynomialA0Mantissa) + static_cast<quint16>(i * 5));
    const auto exponentRegister = static_cast<Register>(
        static_cast<quint16>(Register::PolynomialA0Exponent) + static_cast<quint16>(i * 5));
    if (!readRegisters(channelAddress(channel, mantissaRegister), 4, registers)) return false;
    channel_data.polynomial_mantissas[static_cast<size_t>(i)] =
        decodeInt64(QVector<uint16_t>(registers.cbegin(), registers.cend()));
    if (polynomial_exponents_supported_)
    {
      if (readRegisters(channelAddress(channel, exponentRegister), 1, registers))
      {
        channel_data.polynomial_exponents[static_cast<size_t>(i)] =
            static_cast<int>(decodeInt16(QVector<uint16_t>(registers.cbegin(), registers.cend())));
      }
      else
      {
        polynomial_exponents_supported_ = false;
      }
    }
  }
  return true;
}

bool TemperatureControllerCollector::readSnapshot(TemperatureControllerData& sample)
{
  using namespace TemperatureControllerProtocol;
  sample = TemperatureControllerData{};
  if (!readChannel(1, sample.channels[0])) return false;
  if (channel_count_ > 1 && !readChannel(2, sample.channels[1])) return false;
  std::vector<uint16_t> registers;
  if (!readRegisters(static_cast<uint16_t>(Register::DeviceAddress), 1, registers)) return false;
  sample.device_address = static_cast<int>(registers[0]);
  if (!readRegisters(static_cast<uint16_t>(Register::Rs485Baud), 1, registers)) return false;
  sample.rs485_baud_index = static_cast<int>(registers[0]);
  if (!readRegisters(static_cast<uint16_t>(Register::OvertempOutputMode), 1, registers)) return false;
  sample.overtemp_output_mode = static_cast<int>(registers[0]);
  if (!readRegisters(static_cast<uint16_t>(Register::InternalTemperature), 1, registers)) return false;
  sample.internal_temperature_c = decodeInt16(QVector<uint16_t>(registers.cbegin(), registers.cend()));
  if (!readRegisters(static_cast<uint16_t>(Register::ErrorCode), 1, registers)) return false;
  sample.error_code = registers[0];
  if (!readRegisters(static_cast<uint16_t>(Register::ControllerMode), 1, registers)) return false;
  sample.controller_mode = static_cast<int>(decodeInt16(QVector<uint16_t>(registers.cbegin(), registers.cend())));
  sample.valid = true;
  sample.timestamp = std::chrono::steady_clock::now();
  sample.error_message.clear();
  return true;
}

bool TemperatureControllerCollector::checkDeviceResponse()
{
  log(isEnglishLog() ? "Serial port opened. Communicating with the temperature controller; please wait."
                     : "打开串口成功，正在通讯温控器，请稍等");

  auto queryWithRetry = [this](const std::string& command, std::string& response) {
    for (int attempt = 0; attempt < 3 && !isCancelRequested(); ++attempt)
    {
      const int wait_ms = command == "INQUIRE=1@" ? 3000 : 1200;
      if (queryAscii(command, response, wait_ms))
      {
        return true;
      }
    }
    return false;
  };
  auto decimalValue = [](const std::string& response, const std::string& key, std::string& value) {
    const size_t key_pos = response.find(key);
    if (key_pos == std::string::npos)
    {
      return false;
    }
    const size_t value_pos = key_pos + key.size();
    size_t value_end = value_pos;
    while (value_end < response.size() && response[value_end] >= '0' && response[value_end] <= '9')
    {
      ++value_end;
    }
    if (value_end == value_pos)
    {
      return false;
    }
    value = response.substr(value_pos, value_end - value_pos);
    return true;
  };

  std::string response;
  std::string model_name;
  if (!queryWithRetry("TEC=?@", response) || !decimalValue(response, "TEC=", model_name))
  {
    log(isEnglishLog() ? "Failed to read the temperature controller model."
                       : "温控器型号读取失败。");
    return false;
  }
  log(isEnglishLog() ? "Temperature controller model: " + model_name
                     : "当前温控器型号为" + model_name);

  std::string firmware_version;
  if (!queryWithRetry("FPV=?@", response) || !decimalValue(response, "FPV=", firmware_version))
  {
    log(isEnglishLog() ? "Failed to read the temperature controller firmware version."
                       : "温控器版本号读取失败。");
    return false;
  }
  log(isEnglishLog() ? "Temperature controller firmware version: " + firmware_version
                     : "当前温控器版本号为" + firmware_version);

  log(isEnglishLog() ? "Reading parameters..." : "参数读取中...");
  if (!queryWithRetry("INQUIRE=1@", response))
  {
    log(isEnglishLog() ? "Failed to read temperature controller parameters."
                       : "参数读取失败。");
    return false;
  }
  channel_count_ = response.find("TC2:TG=") == std::string::npos ? 1 : 2;
  static constexpr const char *kRequiredParameters[] = {
      "TC1:TG=", "TC1:LIMITED=", "TC1:MODE=", "TC1:ENABLE=", "TC1:KP=",
      "TC1:KI=", "TC1:KD=", "TC1:RP=", "TC1:BX=", "TC1:PT1000RP=",
      "TC1:CHRATIO=", "TC1:SPEED=", "TC1:STEADYIOB=", "TC1:OVERTEMPUP=",
      "TC1:OVERTEMPLOWER=", "TC1:FDEADV=", "TC1:BDEADV=", "TC1:NTCRP=",
      "TC1:PTRP=", "TC1:PTA=", "TC1:PTB=", "TC1:PTC=", "TC1:PIDPOL=",
  };
  const bool complete = std::all_of(std::begin(kRequiredParameters),
                                    std::end(kRequiredParameters),
                                    [&response](const char *parameter) {
                                      return response.find(parameter) != std::string::npos;
                                    });
  if (isCancelRequested() || !complete)
  {
    log(isEnglishLog() ? "Failed to read temperature controller parameters."
                       : "参数读取失败。");
    return false;
  }
  log(isEnglishLog() ? "Parameter reading complete." : "参数读取完成。");
  return true;
}

bool TemperatureControllerCollector::setTargetTemperature(uint8_t channel, double celsius)
{
  using namespace TemperatureControllerProtocol;
  const QVector<uint16_t> values = encodeInt32(temperatureCelsiusToRaw(celsius));
  return writeAndConfirm(channel, channelAddress(channel, Register::TargetTemperature), std::vector<uint16_t>(values.cbegin(), values.cend()));
}

bool TemperatureControllerCollector::setOutputEnabled(uint8_t channel, bool enabled)
{
  using namespace TemperatureControllerProtocol;
  const QVector<uint16_t> values = encodeUInt16(enabled ? 1 : 0);
  return writeAndConfirm(channel, channelAddress(channel, Register::OutputEnabled), std::vector<uint16_t>(values.cbegin(), values.cend()));
}

bool TemperatureControllerCollector::setOutputMode(uint8_t channel, uint16_t mode)
{
  using namespace TemperatureControllerProtocol;
  if (mode > 3)
  {
    return false;
  }
  const QVector<uint16_t> values = encodeUInt16(mode);
  return writeAndConfirm(channel, channelAddress(channel, Register::OutputMode), std::vector<uint16_t>(values.cbegin(), values.cend()));
}

bool TemperatureControllerCollector::setMaxOutputPercent(uint8_t channel, uint16_t percent)
{
  using namespace TemperatureControllerProtocol;
  if (percent > 90)
  {
    return false;
  }
  const QVector<uint16_t> values = encodeUInt16(percent);
  return writeAndConfirm(channel, channelAddress(channel, Register::MaxOutputPercent), std::vector<uint16_t>(values.cbegin(), values.cend()));
}

bool TemperatureControllerCollector::setPid(uint8_t channel, uint32_t kp, uint32_t ki, uint32_t kd)
{
  using namespace TemperatureControllerProtocol;
  const QVector<uint16_t> kpValues = encodeUInt32(kp);
  const QVector<uint16_t> kiValues = encodeUInt32(ki);
  const QVector<uint16_t> kdValues = encodeUInt32(kd);
  return writeAndConfirm(channel, channelAddress(channel, Register::Kp), std::vector<uint16_t>(kpValues.cbegin(), kpValues.cend())) &&
         writeAndConfirm(channel, channelAddress(channel, Register::Ki), std::vector<uint16_t>(kiValues.cbegin(), kiValues.cend())) &&
         writeAndConfirm(channel, channelAddress(channel, Register::Kd), std::vector<uint16_t>(kdValues.cbegin(), kdValues.cend()));
}

bool TemperatureControllerCollector::setAutoPid(uint8_t channel, uint16_t mode)
{
  using namespace TemperatureControllerProtocol;
  if (mode > 2)
  {
    return false;
  }
  const QVector<uint16_t> values = encodeUInt16(mode);
  return writeAndConfirm(channel, channelAddress(channel, Register::AutoPid), std::vector<uint16_t>(values.cbegin(), values.cend()));
}

bool TemperatureControllerCollector::setOvertempUpper(uint8_t channel, double celsius)
{
  using namespace TemperatureControllerProtocol;
  if (!std::isfinite(celsius) || celsius < -3000.0 || celsius > 5000.0)
  {
    return false;
  }
  const QVector<uint16_t> values = encodeInt32(overtempCelsiusToRaw(celsius));
  return writeAndConfirm(channel, channelAddress(channel, Register::OvertempUpper), std::vector<uint16_t>(values.cbegin(), values.cend()));
}

bool TemperatureControllerCollector::setOvertempLower(uint8_t channel, double celsius)
{
  using namespace TemperatureControllerProtocol;
  if (!std::isfinite(celsius) || celsius < -3000.0 || celsius > 5000.0)
  {
    return false;
  }
  const QVector<uint16_t> values = encodeInt32(overtempCelsiusToRaw(celsius));
  return writeAndConfirm(channel, channelAddress(channel, Register::OvertempLower), std::vector<uint16_t>(values.cbegin(), values.cend()));
}

bool TemperatureControllerCollector::setTemperatureSlope(uint8_t channel, double celsius_per_second)
{
  using namespace TemperatureControllerProtocol;
  if (!std::isfinite(celsius_per_second) || celsius_per_second < 0.0 || celsius_per_second > 10.0)
  {
    return false;
  }
  const QVector<uint16_t> values = encodeUInt16(static_cast<quint16>(qRound(celsius_per_second * 1000.0)));
  return writeAndConfirm(channel, channelAddress(channel, Register::TemperatureSlope), std::vector<uint16_t>(values.cbegin(), values.cend()));
}

bool TemperatureControllerCollector::setStartupDelay(uint8_t channel, uint16_t seconds)
{
  using namespace TemperatureControllerProtocol;
  if (seconds < 3 || seconds > 180)
  {
    return false;
  }
  const QVector<uint16_t> values = encodeUInt16(seconds);
  return writeAndConfirm(channel, channelAddress(channel, Register::StartupDelay), std::vector<uint16_t>(values.cbegin(), values.cend()));
}

bool TemperatureControllerCollector::setControllerMode(uint16_t mode)
{
  using namespace TemperatureControllerProtocol;
  if (mode > 3)
  {
    return false;
  }
  const QVector<uint16_t> values = encodeInt16(static_cast<int16_t>(mode));
  return writeAndConfirm(1, static_cast<uint16_t>(Register::ControllerMode), std::vector<uint16_t>(values.cbegin(), values.cend()));
}

bool TemperatureControllerCollector::setDeviceAddress(uint16_t address)
{
  using namespace TemperatureControllerProtocol;
  if (address == 0 || address > 247)
  {
    return false;
  }
  const QVector<uint16_t> values = encodeUInt16(address);
  std::lock_guard<std::mutex> lock(modbus_mutex_);
  if (!writeRegistersUnlocked(static_cast<uint16_t>(Register::DeviceAddress),
                              std::vector<uint16_t>(values.cbegin(), values.cend()),
                              200))
  {
    return false;
  }
  slave_address_ = static_cast<uint8_t>(address);
  std::vector<uint16_t> read_back;
  return readRegistersUnlocked(static_cast<uint16_t>(Register::DeviceAddress), 1, read_back, 500) &&
         !read_back.empty() &&
         read_back[0] == address;
}

bool TemperatureControllerCollector::setRs485BaudIndex(uint16_t baud_index)
{
  using namespace TemperatureControllerProtocol;
  if (baud_index > 7)
  {
    return false;
  }
  const QVector<uint16_t> values = encodeUInt16(baud_index);
  return writeRegisters(static_cast<uint16_t>(Register::Rs485Baud),
                        std::vector<uint16_t>(values.cbegin(), values.cend()),
                        500);
}

bool TemperatureControllerCollector::setOvertempOutputMode(uint16_t mode)
{
  using namespace TemperatureControllerProtocol;
  if (mode > 1)
  {
    return false;
  }
  const QVector<uint16_t> values = encodeUInt16(mode);
  return writeAndConfirm(1, static_cast<uint16_t>(Register::OvertempOutputMode), std::vector<uint16_t>(values.cbegin(), values.cend()));
}

bool TemperatureControllerCollector::setSensorConfig(uint8_t channel,
                                                     uint16_t sensor_model,
                                                     uint32_t ntc_b,
                                                     uint32_t ntc_r0,
                                                     uint32_t pt_r0,
                                                     int32_t pt_a,
                                                     int32_t pt_b,
                                                     int32_t pt_c,
                                                     const std::array<int64_t, 8>& polynomial_mantissas,
                                                     const std::array<int16_t, 8>& polynomial_exponents)
{
  using namespace TemperatureControllerProtocol;
  if (channel < 1 || channel > 2 ||
      sensor_model > 3 ||
      ntc_b < 100000 || ntc_b > 5000000 ||
      ntc_r0 > 9000000 ||
      pt_r0 > 10000000 ||
      pt_a < -9000000 || pt_a > 9000000 ||
      pt_b < -9000000 || pt_b > 9000000 ||
      pt_c < -90000 || pt_c > 90000)
  {
    return false;
  }
  for (size_t i = 0; i < polynomial_mantissas.size(); ++i)
  {
    if (polynomial_mantissas[i] < -99999999999999LL ||
        polynomial_mantissas[i] > 99999999999999LL ||
        polynomial_exponents[i] < -100 ||
        polynomial_exponents[i] > 100)
    {
      return false;
    }
  }

  auto writeValue = [this, channel](Register reg, const QVector<uint16_t>& values) {
    return writeAndConfirm(channel,
                           channelAddress(channel, reg),
                           std::vector<uint16_t>(values.cbegin(), values.cend()));
  };

  if (!writeValue(Register::SensorModel, encodeUInt16(sensor_model)) ||
      !writeValue(Register::NtcB, encodeUInt32(ntc_b)) ||
      !writeValue(Register::NtcR0, encodeUInt32(ntc_r0)) ||
      !writeValue(Register::PtR0, encodeUInt32(pt_r0)) ||
      !writeValue(Register::PtA, encodeInt32(pt_a)) ||
      !writeValue(Register::PtB, encodeInt32(pt_b)) ||
      !writeValue(Register::PtC, encodeInt32(pt_c)))
  {
    return false;
  }
  for (int i = 0; i < 8; ++i)
  {
    const auto mantissaRegister = static_cast<Register>(
        static_cast<quint16>(Register::PolynomialA0Mantissa) + static_cast<quint16>(i * 5));
    const auto exponentRegister = static_cast<Register>(
        static_cast<quint16>(Register::PolynomialA0Exponent) + static_cast<quint16>(i * 5));
    if (!writeValue(mantissaRegister, encodeInt64(polynomial_mantissas[static_cast<size_t>(i)])) ||
        !writeValue(exponentRegister, encodeInt16(polynomial_exponents[static_cast<size_t>(i)])))
    {
      return false;
    }
  }
  return true;
}

bool TemperatureControllerCollector::restoreFactoryDefaults()
{
  using namespace TemperatureControllerProtocol;
  const QVector<uint16_t> values = encodeUInt16(1);
  return writeRegisters(static_cast<uint16_t>(Register::FactoryReset),
                        std::vector<uint16_t>(values.cbegin(), values.cend()),
                        500);
}

void TemperatureControllerCollector::run()
{
  while (running_.load())
  {
    const auto start_time = std::chrono::steady_clock::now();
    TemperatureControllerData sample;
    if (readSnapshot(sample))
    {
      DataCallback callback;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_data_ = sample;
        callback = data_callback_;
      }
      recordDataReceived();
      if (callback && shouldEmitData())
      {
        updateLastEmitTime();
        callback();
      }
    }
    else
    {
      std::lock_guard<std::mutex> lock(mutex_);
      latest_data_.valid = false;
      latest_data_.error_message = "RD105温控器读取失败";
    }

    const int interval_ms = std::max(1, 1000 / std::max(1, getSampleRate()));
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count();
    const int remaining = interval_ms - static_cast<int>(elapsed);
    if (remaining > 0)
    {
      sleepMs(remaining);
    }
  }
}

LidarData LidarCollector::getLatestData()
{
  std::lock_guard<std::mutex> lock(mutex_);
  return latest_data_;
}

void LidarCollector::setRawFrameCallback(RawFrameCallback callback)
{
  std::lock_guard<std::mutex> lock(mutex_);
  raw_frame_callback_ = std::move(callback);
}

bool LidarCollector::initialize()
{
  serial_.setNonBlocking(true);
  active_protocol_ = LidarProtocol::Unknown;
  tfa1500_stream_started_ = false;
  return true;
}

void LidarCollector::cleanup()
{
  stopTfa1500Streaming();
  active_protocol_ = LidarProtocol::Unknown;
}

bool LidarCollector::ensureTfa1500Streaming()
{
  if (tfa1500_stream_started_)
  {
    return true;
  }

  static const uint8_t command[] = {0x55, 0xAA, 0xCB, 0xCC, 0xCC, 0xCC, 0xCC, 0xFB};
  const ssize_t written = serial_.write(command, sizeof(command));
  if (written != static_cast<ssize_t>(sizeof(command)))
  {
    log("TFA1500-L: 发送高频测距启动命令失败");
    return false;
  }

  tfa1500_stream_started_ = true;
  return true;
}

bool LidarCollector::ensureTfa1500Standby()
{
  static const uint8_t command[] = {0x55, 0x00, 0x02, 0x00, 0x00, 0x57};
  const ssize_t written = serial_.write(command, sizeof(command));
  if (written != static_cast<ssize_t>(sizeof(command)))
  {
    log("TFA1500-L: 发送待机命令失败");
    return false;
  }

  tfa1500_stream_started_ = false;
  return true;
}

bool LidarCollector::ensureTfa1500DistanceOutput()
{
  static const uint8_t command[] = {0x5A, 0x0A, 0x02, 0x02, 0x00, 0xF1};
  const ssize_t written = serial_.write(command, sizeof(command));
  if (written != static_cast<ssize_t>(sizeof(command)))
  {
    log("TFA1500-L: 发送距离输出命令失败");
    return false;
  }

  return true;
}

bool LidarCollector::ensureTfa1500LowFrequencyContinuous()
{
  static const uint8_t command[] = {0x55, 0x02, 0x02, 0x20, 0x00, 0x75};
  const ssize_t written = serial_.write(command, sizeof(command));
  if (written != static_cast<ssize_t>(sizeof(command)))
  {
    log("TFA1500-L: 发送低频连续测距命令失败");
    return false;
  }

  return true;
}

void LidarCollector::stopTfa1500Streaming()
{
  if (!tfa1500_stream_started_ || !serial_.isOpen())
  {
    return;
  }

  static const uint8_t command[] = {0x55, 0xAA, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xFC};
  serial_.write(command, sizeof(command));
  tfa1500_stream_started_ = false;
}

bool LidarCollector::setDeviceSampleRate(int hz)
{
  if (hz < 1)
  {
    hz = 1;
  }

  if (active_protocol_ == LidarProtocol::TFA1500HighFrequency || serial_config_.baudrate >= 500000)
  {
    if (!ensureTfa1500Streaming())
    {
      return false;
    }

    sample_rate_hz_.store(std::min(hz, 1000));
    log("TFA1500-L: 高频模式使用设备自适应输出；主机采样率限制已设置为 " + std::to_string(sample_rate_hz_.load()) + " Hz");
    return true;
  }

  if (active_protocol_ == LidarProtocol::TFA1500DistanceFrame)
  {
    ensureTfa1500DistanceOutput();
    sample_rate_hz_.store(std::min(hz, 100));
    log("TFA1500-L: 距离输出模式不支持设备侧频率命令；主机采样率限制已设置为 " + std::to_string(sample_rate_hz_.load()) + " Hz");
    return true;
  }

  if (active_protocol_ == LidarProtocol::TFA1500LowFrequencyFrame)
  {
    ensureTfa1500LowFrequencyContinuous();
    sample_rate_hz_.store(std::min(hz, 100));
    log("TFA1500-L: 低频模式不支持设备侧频率命令；主机采样率限制已设置为 " + std::to_string(sample_rate_hz_.load()) + " Hz");
    return true;
  }

  ensureTfa1500DistanceOutput();
  sample_rate_hz_.store(std::min(hz, 100));
  log("TFA1500-L: 未识别具体输出模式，已使用距离输出命令并将主机采样率限制设置为 " + std::to_string(sample_rate_hz_.load()) + " Hz");
  return true;
}

bool LidarCollector::parseTfa1500Frame(const uint8_t* frame, size_t size, LidarData& sample)
{
  if (!frame || size < TFA1500_FRAME_SIZE || frame[0] != TFA1500_HEADER)
  {
    return false;
  }

  uint8_t sum = 0;
  for (size_t i = 1; i < 4; ++i)
  {
    sum = static_cast<uint8_t>(sum + frame[i]);
  }
  const uint8_t checksum = static_cast<uint8_t>(~sum);
  if (checksum != frame[4])
  {
    return false;
  }

  const uint32_t distance_cm =
      static_cast<uint32_t>(frame[1]) |
      (static_cast<uint32_t>(frame[2]) << 8) |
      (static_cast<uint32_t>(frame[3]) << 16);
  const bool valid = distance_cm != 0x003FFFFFu;

  sample.distance_m = valid ? (static_cast<double>(distance_cm) / 100.0) : 0.0;
  sample.signal_strength = 0;
  sample.timestamp = std::chrono::steady_clock::now();
  sample.valid = valid && distance_cm > 0;
  sample.error_message = sample.valid ? "" : "未检测到目标或超出量程";
  return true;
}

bool LidarCollector::checkDeviceResponse()
{
  uint8_t chunk[128];
  std::vector<uint8_t> buffer;
  buffer.reserve(256);

  constexpr int max_wait_ms = 3200;
  constexpr int step_ms = 20;
  const auto start_time = std::chrono::steady_clock::now();
  int elapsed_ms = 0;
  const bool prefer_tfa1500 = serial_config_.baudrate >= 500000;
  bool tried_passive_listen = false;
  bool tried_distance_output = false;
  bool tried_distance_output_after_standby = false;
  bool tried_low_frequency = false;
  bool tried_low_frequency_after_standby = false;
  bool tried_high_frequency = false;
  bool tried_high_frequency_after_standby = false;

  while (elapsed_ms < max_wait_ms)
  {
    if (isCancelRequested())
    {
      return false;
    }

    if (!tried_passive_listen)
    {
      tried_passive_listen = true;
    }
    else if (!prefer_tfa1500 && !tried_distance_output && elapsed_ms >= 600)
    {
      ensureTfa1500DistanceOutput();
      tried_distance_output = true;
    }
    else if (!prefer_tfa1500 && !tried_distance_output_after_standby && elapsed_ms >= 1100)
    {
      ensureTfa1500Standby();
      ensureTfa1500DistanceOutput();
      tried_distance_output_after_standby = true;
    }
    else if (!prefer_tfa1500 && !tried_low_frequency && elapsed_ms >= 1400)
    {
      ensureTfa1500LowFrequencyContinuous();
      tried_low_frequency = true;
    }
    else if (!prefer_tfa1500 && !tried_low_frequency_after_standby && elapsed_ms >= 2200)
    {
      ensureTfa1500Standby();
      ensureTfa1500LowFrequencyContinuous();
      tried_low_frequency_after_standby = true;
    }
    else if (prefer_tfa1500 && !tried_high_frequency && elapsed_ms >= 200)
    {
      ensureTfa1500Streaming();
      tried_high_frequency = true;
    }
    else if (prefer_tfa1500 && !tried_high_frequency_after_standby && elapsed_ms >= 1000)
    {
      ensureTfa1500Standby();
      ensureTfa1500Streaming();
      tried_high_frequency_after_standby = true;
    }

    log(formatDetectionProgress("等待激光测距帧", 1, 1, computeRemainingSeconds(start_time, max_wait_ms)));
    ssize_t n = serial_.read(chunk, sizeof(chunk));
    if (n > 0)
    {
      buffer.insert(buffer.end(), chunk, chunk + n);
      while (!buffer.empty())
      {
        LidarData sample;
        LidarProtocol detected_protocol = LidarProtocol::Unknown;
        if (extractNextLidarSample(buffer,
                                   prefer_tfa1500 ? LidarProtocol::TFA1500HighFrequency : active_protocol_,
                                   sample,
                                   detected_protocol))
        {
          if (detected_protocol == LidarProtocol::TFA1500DistanceFrame && prefer_tfa1500)
          {
            detected_protocol = LidarProtocol::TFA1500HighFrequency;
          }
          active_protocol_ = detected_protocol;
          log(std::string("激光测距仪: 已识别协议 ") + lidarProtocolName(active_protocol_));
          return true;
        }
        break;
      }
    }

    sleepMs(step_ms);
    elapsed_ms += step_ms;
  }

  return false;
}

void LidarCollector::run()
{
  uint8_t chunk[256];
  std::vector<uint8_t> buffer;
  buffer.reserve(512);

  if (active_protocol_ == LidarProtocol::TFA1500HighFrequency)
  {
    ensureTfa1500Streaming();
  }
  else if (active_protocol_ == LidarProtocol::TFA1500DistanceFrame)
  {
    ensureTfa1500DistanceOutput();
  }
  else if (active_protocol_ == LidarProtocol::TFA1500LowFrequencyFrame)
  {
    ensureTfa1500LowFrequencyContinuous();
  }

  while (running_.load())
  {
    ssize_t n = serial_.read(chunk, sizeof(chunk));
    if (n > 0)
    {
      buffer.insert(buffer.end(), chunk, chunk + n);
      while (!buffer.empty())
      {
        LidarData sample;
        LidarProtocol detected_protocol = active_protocol_;
        std::vector<uint8_t> raw_frame;
        if (extractNextLidarSample(buffer, active_protocol_, sample, detected_protocol, &raw_frame))
        {
          active_protocol_ = detected_protocol;
          DataCallback callback;
          RawFrameCallback raw_callback;
          const uint64_t host_timestamp_us = systemTimestampUs();
          {
            std::lock_guard<std::mutex> lock(mutex_);
            latest_data_ = sample;
            callback = data_callback_;
            raw_callback = raw_frame_callback_;
          }

          recordDataReceived();

          if (raw_callback && !raw_frame.empty())
          {
            raw_callback(host_timestamp_us, active_protocol_, raw_frame.data(), raw_frame.size());
          }

          if (callback && shouldEmitData())
          {
            updateLastEmitTime();
            callback();
          }
          continue;
        }
        break;
      }
    }
    else
    {
      sleepMs(5);
    }
  }
}

}


