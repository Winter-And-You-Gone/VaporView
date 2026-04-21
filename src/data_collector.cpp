#include "data_collector.h"
#include "hipnuc_dec.h"
#include "pvtsln_data.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <regex>
#include <sstream>
#include <vector>

namespace VaporView
{
namespace
{
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
constexpr uint8_t kMsgRawGnss = 0x59;
constexpr uint8_t kMsgSatellites = 0x5A;
constexpr uint8_t kMsgGeodeticPos = 0x5C;
constexpr uint8_t kMsgEcefPos = 0x5D;
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

bool isSupportedEpsilonPacketRate(uint8_t packet_id, int hz)
{
  static const std::vector<int> kCommonRates = {0, 1, 2, 5, 10, 20, 50, 100, 250, 500};
  static const std::vector<int> kImuRates = {0, 1, 2, 5, 10, 20, 50, 100, 200, 250, 500, 1000};

  const std::vector<int>* supported_rates = &kCommonRates;
  if (packet_id == kMsgImu)
  {
    supported_rates = &kImuRates;
  }
  return std::find(supported_rates->cbegin(), supported_rates->cend(), hz) != supported_rates->cend();
}

std::map<uint8_t, int> desiredEpsilonPacketRates(int hz)
{
  const int navLowRate = std::min(hz, 20);
  return {
      {kMsgImu, hz},
      {kMsgAhrs, hz},
      {kMsgInsGps, hz},
      {kMsgSystemState, hz},
      {kMsgRawGnss, navLowRate},
      {kMsgSatellites, navLowRate},
      {kMsgGeodeticPos, navLowRate},
      {kMsgEcefPos, navLowRate},
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

std::string readPrintableSerialResponse(SerialPort& serial, int totalWaitMs, bool stopOnAck)
{
  std::string filtered;
  const auto start = std::chrono::steady_clock::now();
  auto lastDataTime = start;
  bool sawAck = false;
  uint8_t chunk[256];

  while (true)
  {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
    const auto idleMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastDataTime).count();
    if (elapsedMs >= totalWaitMs && (!stopOnAck || !sawAck || idleMs >= 120))
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
      if (stopOnAck && containsEpsilonAsciiAck(filtered))
      {
        sawAck = true;
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
                                          const std::string& command,
                                          int waitMs)
{
  logFn("[EPSILON TX] " + trimAscii(command));
  const ssize_t written = serial.write(command.c_str(), command.size());
  if (written != static_cast<ssize_t>(command.size()))
  {
    logFn("EPSILON: failed to send command: " + trimAscii(command));
    return std::string();
  }
  if (waitMs > 0)
  {
    sleepMs(waitMs);
  }
  const std::string response = readLoggedEpsilonAsciiResponse(serial, logFn, std::max(180, waitMs));
  if (command != "y\r\n" && !containsEpsilonAsciiAck(response))
  {
    logFn("EPSILON: no explicit ASCII acknowledgement for command: " + trimAscii(command));
  }
  return response;
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
  case LidarProtocol::TF03:
    return "TF03";
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

constexpr uint8_t kTf03Header = 0x59;
constexpr size_t kTf03FrameSize = 9;
constexpr uint8_t kTfa1500Header = 0x5C;
constexpr size_t kTfa1500FrameSize = 5;
constexpr uint8_t kTfa1500LowFrequencyHeader = 0x55;
constexpr size_t kTfa1500LowFrequencyMinFrameSize = 6;
constexpr uint8_t kObservedAaHeader = 0xAA;
constexpr uint8_t kObservedB7Type = 0xB7;
constexpr uint8_t kObservedBbTail = 0xBB;
constexpr size_t kObservedAaB7FrameSize = 10;

bool parseTf03FrameLocal(const uint8_t* frame, size_t size, LidarData& sample)
{
  if (!frame || size < kTf03FrameSize || frame[0] != kTf03Header || frame[1] != kTf03Header)
  {
    return false;
  }

  uint8_t checksum = 0;
  for (size_t i = 0; i < 8; ++i)
  {
    checksum = static_cast<uint8_t>(checksum + frame[i]);
  }

  if (checksum != frame[8])
  {
    return false;
  }

  const uint16_t distance_cm = static_cast<uint16_t>(frame[2]) | (static_cast<uint16_t>(frame[3]) << 8);
  const uint16_t strength = static_cast<uint16_t>(frame[4]) | (static_cast<uint16_t>(frame[5]) << 8);

  sample.distance_m = distance_cm / 100.0;
  sample.signal_strength = strength;
  sample.timestamp = std::chrono::steady_clock::now();
  sample.valid = strength >= 40 && distance_cm > 0;
  sample.error_message = sample.valid ? "" : "信号弱或超出量程";
  return true;
}

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

bool extractNextTf03Sample(std::vector<uint8_t>& buffer, LidarData& sample)
{
  while (buffer.size() >= kTf03FrameSize)
  {
    auto header = std::find(buffer.begin(), buffer.end(), kTf03Header);
    if (header == buffer.end())
    {
      buffer.clear();
      return false;
    }
    if (std::distance(header, buffer.end()) < 2)
    {
      buffer.erase(buffer.begin(), header);
      return false;
    }
    if (*(header + 1) != kTf03Header)
    {
      buffer.erase(buffer.begin(), header + 1);
      continue;
    }
    if (std::distance(header, buffer.end()) < static_cast<std::ptrdiff_t>(kTf03FrameSize))
    {
      buffer.erase(buffer.begin(), header);
      return false;
    }
    if (parseTf03FrameLocal(&(*header), kTf03FrameSize, sample))
    {
      buffer.erase(buffer.begin(), header + kTf03FrameSize);
      return true;
    }
    buffer.erase(buffer.begin(), header + 1);
  }
  return false;
}

bool extractNextTfa1500Sample(std::vector<uint8_t>& buffer, LidarData& sample)
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
      buffer.erase(buffer.begin(), header + kTfa1500FrameSize);
      return true;
    }
    buffer.erase(buffer.begin(), header + 1);
  }
  return false;
}

bool extractNextTfa1500LowFrequencySample(std::vector<uint8_t>& buffer, LidarData& sample)
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

    size_t consumed_size = 0;
    if (parseTfa1500LowFrequencyFrameLocal(&(*header), static_cast<size_t>(remaining), sample, consumed_size))
    {
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

bool extractNextObservedAaB7Sample(std::vector<uint8_t>& buffer, LidarData& sample)
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
      buffer.erase(buffer.begin(), header + kObservedAaB7FrameSize);
      return true;
    }

    buffer.erase(buffer.begin(), header + 1);
  }
  return false;
}

bool extractNextLidarSample(std::vector<uint8_t>& buffer, LidarProtocol protocol_hint, LidarData& sample, LidarProtocol& detected_protocol)
{
  if (protocol_hint == LidarProtocol::TF03)
  {
    if (extractNextTf03Sample(buffer, sample))
    {
      detected_protocol = LidarProtocol::TF03;
      return true;
    }
    return false;
  }

  if (protocol_hint == LidarProtocol::TFA1500HighFrequency)
  {
    if (extractNextTfa1500Sample(buffer, sample))
    {
      detected_protocol = LidarProtocol::TFA1500HighFrequency;
      return true;
    }
    return false;
  }

  if (protocol_hint == LidarProtocol::TFA1500DistanceFrame)
  {
    if (extractNextTfa1500Sample(buffer, sample))
    {
      detected_protocol = LidarProtocol::TFA1500DistanceFrame;
      return true;
    }
    return false;
  }

  if (protocol_hint == LidarProtocol::TFA1500LowFrequencyFrame)
  {
    if (extractNextTfa1500LowFrequencySample(buffer, sample))
    {
      detected_protocol = LidarProtocol::TFA1500LowFrequencyFrame;
      return true;
    }
    return false;
  }

  if (protocol_hint == LidarProtocol::ObservedAaB7Frame)
  {
    if (extractNextObservedAaB7Sample(buffer, sample))
    {
      detected_protocol = LidarProtocol::ObservedAaB7Frame;
      return true;
    }
    return false;
  }

  while (!buffer.empty())
  {
    auto tf03 = std::find(buffer.begin(), buffer.end(), kTf03Header);
    auto tfa = std::find(buffer.begin(), buffer.end(), kTfa1500Header);
    auto tfa_low = std::find(buffer.begin(), buffer.end(), kTfa1500LowFrequencyHeader);
    auto aa_b7 = std::find(buffer.begin(), buffer.end(), kObservedAaHeader);

    if (tf03 == buffer.end() && tfa == buffer.end() && tfa_low == buffer.end() && aa_b7 == buffer.end())
    {
      buffer.clear();
      return false;
    }

    auto earliest = buffer.end();
    if (tf03 != buffer.end() && (earliest == buffer.end() || tf03 < earliest)) earliest = tf03;
    if (tfa != buffer.end() && (earliest == buffer.end() || tfa < earliest)) earliest = tfa;
    if (tfa_low != buffer.end() && (earliest == buffer.end() || tfa_low < earliest)) earliest = tfa_low;
    if (aa_b7 != buffer.end() && (earliest == buffer.end() || aa_b7 < earliest)) earliest = aa_b7;

    if (earliest == tf03)
    {
      std::vector<uint8_t> slice(tf03, buffer.end());
      if (extractNextTf03Sample(slice, sample))
      {
        detected_protocol = LidarProtocol::TF03;
        buffer.assign(slice.begin(), slice.end());
        return true;
      }

      if (tf03 == buffer.begin())
      {
        return false;
      }
      buffer.erase(buffer.begin(), tf03);
      continue;
    }

    if (earliest == tfa)
    {
      std::vector<uint8_t> slice(tfa, buffer.end());
      if (extractNextTfa1500Sample(slice, sample))
      {
        detected_protocol = LidarProtocol::TFA1500DistanceFrame;
        buffer.assign(slice.begin(), slice.end());
        return true;
      }

      if (tfa == buffer.begin())
      {
        return false;
      }
      buffer.erase(buffer.begin(), tfa);
      continue;
    }

    if (earliest == tfa_low)
    {
      std::vector<uint8_t> slice(tfa_low, buffer.end());
      if (extractNextTfa1500LowFrequencySample(slice, sample))
      {
        detected_protocol = LidarProtocol::TFA1500LowFrequencyFrame;
        buffer.assign(slice.begin(), slice.end());
        return true;
      }

      if (tfa_low == buffer.begin())
      {
        return false;
      }
      buffer.erase(buffer.begin(), tfa_low);
      continue;
    }

    if (earliest == aa_b7)
    {
      std::vector<uint8_t> slice(aa_b7, buffer.end());
      if (extractNextObservedAaB7Sample(slice, sample))
      {
        detected_protocol = LidarProtocol::ObservedAaB7Frame;
        buffer.assign(slice.begin(), slice.end());
        return true;
      }

      if (aa_b7 == buffer.begin())
      {
        return false;
      }
      buffer.erase(buffer.begin(), aa_b7);
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

HmpParseResult parseHmpResponse(const uint8_t* buffer, size_t size, float& humidity, float& temperature, uint8_t& exception_code, uint8_t function_code)
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
  char chunk[1024];

  while (running_.load())
  {
    ssize_t n = serial_.read(chunk, sizeof(chunk));
    if (n > 0)
    {
      buffer.append(chunk, static_cast<size_t>(n));

      size_t pos = 0;
      while ((pos = buffer.find('\n')) != std::string::npos)
      {
        std::string line = buffer.substr(0, pos);
        buffer.erase(0, pos + 1);

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
  std::vector<uint8_t> buffer;
  std::vector<uint8_t> frame;
  uint8_t packetId = 0;
  if (!readValidFdilinkFrame(serial_, buffer, &frame, 2500, &packetId, nullptr))
  {
    log("EPSILON: no navigation frame detected, trying command-mode handshake");

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
      log("EPSILON: recovered navigation stream with FDILink frame " + std::to_string(packetId));
      return true;
    }

    log("EPSILON: device responds to commands, but no navigation frame was restored yet");
    return true;
  }

  log("EPSILON: detected FDILink frame " + std::to_string(packetId));
  return true;
}

bool EpsilonCollector::setDeviceSampleRate(int hz)
{
  if (!isSupportedEpsilonRate(hz))
  {
    log("EPSILON: unsupported output rate " + std::to_string(hz) + " Hz");
    return false;
  }
  if (!setOutputPacketRates(desiredEpsilonPacketRates(hz)))
  {
    return false;
  }
  sample_rate_hz_.store(hz);
  return true;
}

bool EpsilonCollector::setOutputPacketRates(const std::map<uint8_t, int>& packetRates)
{
  if (packetRates.empty())
  {
    log("EPSILON: no packet rates were provided for configuration");
    return false;
  }
  if (!serial_.isOpen())
  {
    log("EPSILON: serial port is not open");
    return false;
  }
  for (const auto& entry : packetRates)
  {
    if (!isSupportedEpsilonPacketRate(entry.first, entry.second))
    {
      std::ostringstream oss;
      oss << "EPSILON: unsupported packet rate " << entry.second << " Hz for packet 0x"
          << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
          << static_cast<int>(entry.first);
      log(oss.str());
      return false;
    }
  }

  const EpsilonLogFn logFn = [this](const std::string& message) { log(message); };

  constexpr int kConfigCommandWaitMs = 1500;
  serial_.flush();
  sleepMs(80);

  sendLoggedEpsilonAsciiCommand(serial_, logFn, "#fconfig\r\n", kConfigCommandWaitMs);
  const std::string fmsgResponse = sendLoggedEpsilonAsciiCommand(serial_, logFn, "#fmsg\r\n", kConfigCommandWaitMs);
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

  if (needsReconfigure)
  {
    for (const auto& entry : packetRates)
    {
      char command[32];
      std::snprintf(command, sizeof(command), "#fmsg %02X %d\r\n", entry.first, entry.second);
      sendLoggedEpsilonAsciiCommand(serial_, logFn, command, kConfigCommandWaitMs);
    }
    sendLoggedEpsilonAsciiCommand(serial_, logFn, "#fsave\r\n", kConfigCommandWaitMs);
    sendLoggedEpsilonAsciiCommand(serial_, logFn, "#fdeconfig\r\n", kConfigCommandWaitMs);

    waitForEpsilonNavigationStreamRestore(serial_,
                                          logFn,
                                          6000,
                                          "EPSILON: output configuration updated, saved, and navigation stream restored",
                                          "EPSILON: configuration saved, but no FDILink frame was observed after leaving config mode");
  }
  else
  {
    log("EPSILON: output configuration already matches requested packet rates");
    sendLoggedEpsilonAsciiCommand(serial_, logFn, "#fdeconfig\r\n", kConfigCommandWaitMs);
    waitForEpsilonNavigationStreamRestore(serial_,
                                          logFn,
                                          3000,
                                          "EPSILON: returned to navigation mode with FDILink frame %u",
                                          "EPSILON: configuration query completed, but navigation stream is still silent");
  }
  return true;
}

bool EpsilonCollector::configureRtcmPort(int portIndex, int baudRate)
{
  if (portIndex < 1 || portIndex > 5)
  {
    log("EPSILON: invalid communication port index for RTCM configuration");
    return false;
  }
  if (!serial_.isOpen())
  {
    log("EPSILON: serial port is not open");
    return false;
  }

  const int baudParamValue = epsilonSerialBaudToParamValue(baudRate);
  if (baudParamValue == 0)
  {
    log("EPSILON: unsupported RTCM serial baud rate " + std::to_string(baudRate));
    return false;
  }

  const EpsilonLogFn logFn = [this](const std::string& message) { log(message); };
  constexpr int kConfigCommandWaitMs = 1500;

  serial_.flush();
  sleepMs(80);

  sendLoggedEpsilonAsciiCommand(serial_, logFn, "#fconfig\r\n", kConfigCommandWaitMs);

  char command[64];
  std::snprintf(command, sizeof(command), "#fparam set COMM_STREAM_TYP%d 3\r\n", portIndex);
  sendLoggedEpsilonAsciiCommand(serial_, logFn, command, kConfigCommandWaitMs);

  std::snprintf(command, sizeof(command), "#fparam set COMM_BAUD%d %d\r\n", portIndex, baudParamValue);
  sendLoggedEpsilonAsciiCommand(serial_, logFn, command, kConfigCommandWaitMs);

  sendLoggedEpsilonAsciiCommand(serial_, logFn, "#fsave\r\n", kConfigCommandWaitMs);
  sendLoggedEpsilonAsciiCommand(serial_, logFn, "#fdeconfig\r\n", kConfigCommandWaitMs);

  waitForEpsilonNavigationStreamRestore(serial_,
                                        logFn,
                                        6000,
                                        "EPSILON: RTCM port configuration saved and navigation stream restored",
                                        "EPSILON: RTCM port configuration was sent, but no FDILink frame was observed after leaving config mode");
  return true;
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
                       &ecefRateTracker](const std::vector<uint8_t>& frame, uint64_t hostTimestampUs) {
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
        latest_data_.imu_temp_c = readFloatLE(payload + 36);
        latest_data_.pressure_pa = readFloatLE(payload + 40);
        latest_data_.pressure_temp_c = readFloatLE(payload + 44);
        latest_data_.device_timestamp_us = static_cast<uint64_t>(readI64LE(payload + 48));
      }
      else if (packetId == kMsgAhrs && payloadSize >= 48)
      {
        latest_data_.ang_vel_x_radps = readFloatLE(payload + 0);
        latest_data_.ang_vel_y_radps = readFloatLE(payload + 4);
        latest_data_.ang_vel_z_radps = readFloatLE(payload + 8);
        latest_data_.roll_deg = radToDeg(readFloatLE(payload + 12));
        latest_data_.pitch_deg = radToDeg(readFloatLE(payload + 16));
        latest_data_.yaw_deg = radToDeg(readFloatLE(payload + 20));
        latest_data_.quat_w = readFloatLE(payload + 24);
        latest_data_.quat_x = readFloatLE(payload + 28);
        latest_data_.quat_y = readFloatLE(payload + 32);
        latest_data_.quat_z = readFloatLE(payload + 36);
        latest_data_.device_timestamp_us = static_cast<uint64_t>(readI64LE(payload + 40));
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
        latest_data_.pressure_altitude_m = readFloatLE(payload + 60);
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
      else if (packetId == kMsgFormattedTime && payloadSize >= 15)
      {
        sysStateRateTracker.record();
        const uint32_t microseconds = readU32LE(payload + 0);
        uint64_t utcSeconds = 0;
        uint32_t utcMicroseconds = 0;
        if (utcPartsToUnix(
                static_cast<int>(readU16LE(payload + 4)),
                static_cast<int>(payload[8]),
                static_cast<int>(payload[9]),
                static_cast<int>(payload[12]),
                static_cast<int>(payload[13]),
                static_cast<double>(payload[14]) + microseconds / 1000000.0,
                utcSeconds,
                utcMicroseconds))
        {
          latest_data_.utc_unix_s = utcSeconds;
          latest_data_.utc_microseconds = utcMicroseconds;
        }
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
        latest_data_.heading_valid = ((rawGnssStatus >> 8) & 0x01u) != 0;
      }
      else if (packetId == kMsgSatellites && payloadSize >= 13)
      {
        latest_data_.hdop = readFloatLE(payload + 0);
        latest_data_.vdop = readFloatLE(payload + 4);
        latest_data_.gnss_satellites = static_cast<int>(payload[8]) +
            static_cast<int>(payload[9]) +
            static_cast<int>(payload[10]) +
            static_cast<int>(payload[11]) +
            static_cast<int>(payload[12]);
      }
      else if (packetId == kMsgGeodeticPos && payloadSize >= 32)
      {
        latest_data_.latitude_deg = radToDeg(readDoubleLE(payload + 0));
        latest_data_.longitude_deg = radToDeg(readDoubleLE(payload + 8));
        latest_data_.height_m = readDoubleLE(payload + 16);
        latest_data_.hacc_m = readFloatLE(payload + 24);
        latest_data_.vacc_m = readFloatLE(payload + 28);
      }
      else if (packetId == kMsgEcefPos && payloadSize >= 24)
      {
        latest_data_.ecef_x_m = readDoubleLE(payload + 0);
        latest_data_.ecef_y_m = readDoubleLE(payload + 8);
        latest_data_.ecef_z_m = readDoubleLE(payload + 16);
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
                latest_data_.imu_temp_c = values[12];
                latest_data_.pressure_pa = values[19];
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
            sample.temperature = raw.hi83.temperature;
            sample.air_pressure = raw.hi83.air_pressure;
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
            sample.temperature = static_cast<double>(raw.hi91.temp);
            sample.air_pressure = raw.hi91.air_pressure;
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
            sample.temperature = static_cast<double>(raw.hi92.temp);
            sample.air_pressure = 100000.0 + static_cast<double>(raw.hi92.air_pressure);
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
            sample.temperature = static_cast<double>(raw.hi81.temperature);
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

bool PtbCollector::setDeviceSampleRate(int hz)
{
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

  serial_.write(PTB_CMD_STOP, std::strlen(PTB_CMD_STOP));
  sleepMs(50);
  serial_.flush();
}

bool PtbCollector::checkDeviceResponse()
{
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
  char chunk[256];
  std::string buffer;
  buffer.reserve(512);

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

  auto processLine = [this](std::string line) -> bool {
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
      {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_data_.pressure_hpa = pressure;
        latest_data_.valid = true;
        latest_data_.timestamp = std::chrono::steady_clock::now();
        latest_data_.error_message.clear();
        callback = data_callback_;
      }

      recordDataReceived();

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

      size_t line_end = std::string::npos;
      while ((line_end = buffer.find_first_of("\r\n")) != std::string::npos)
      {
        const std::string line = buffer.substr(0, line_end);
        buffer.erase(0, line_end + 1);
        while (!buffer.empty() && (buffer.front() == '\r' || buffer.front() == '\n'))
        {
          buffer.erase(0, 1);
        }

        if (processLine(line))
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

bool HmpCollector::initialize()
{
  serial_.setNonBlocking(true);
  return true;
}

bool HmpCollector::checkDeviceResponse()
{
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
    size_t total = 0;
    int elapsed_wait_ms = 0;
    constexpr int step_ms = 25;

    while (running_.load() && elapsed_wait_ms < response_wait_ms && total < sizeof(response))
    {
      ssize_t chunk = serial_.read(response + total, sizeof(response) - total);
      if (chunk > 0)
      {
        total += static_cast<size_t>(chunk);
        parsed = parseHmpResponse(response, total, humidity, temperature, exception_code, 0x03);
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
      parsed = parseHmpResponse(response, total, humidity, temperature, exception_code, 0x03);
    }

    if (parsed == HmpParseResult::Data)
    {
      DataCallback callback;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_data_.humidity = humidity;
        latest_data_.temperature = temperature;
        latest_data_.valid = true;
        latest_data_.timestamp = std::chrono::steady_clock::now();
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

LidarData LidarCollector::getLatestData()
{
  std::lock_guard<std::mutex> lock(mutex_);
  return latest_data_;
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

  if (hz < 1 || hz > 100)
  {
    log("TF03: 不支持的帧率: " + std::to_string(hz) + " Hz（有效范围: 1-100 Hz）");
    return false;
  }

  uint8_t command[6];
  command[0] = 0x5A;
  command[1] = 0x06;
  command[2] = 0x03;
  command[3] = static_cast<uint8_t>(hz & 0xFF);
  command[4] = static_cast<uint8_t>((hz >> 8) & 0xFF);
  command[5] = static_cast<uint8_t>((command[0] + command[1] + command[2] + command[3] + command[4]) & 0xFF);

  const ssize_t written = serial_.write(command, sizeof(command));
  if (written != static_cast<ssize_t>(sizeof(command)))
  {
    log("TF03: 发送帧率命令失败");
    return false;
  }

  sample_rate_hz_.store(hz);
  log("TF03: 已将帧率设置为 " + std::to_string(hz) + " Hz");
  return true;
}

bool LidarCollector::parseTf03Frame(const uint8_t* frame, size_t size, LidarData& sample)
{
  if (!frame || size < 9 || frame[0] != TF03_HEADER || frame[1] != TF03_HEADER)
  {
    return false;
  }

  uint8_t checksum = 0;
  for (size_t i = 0; i < 8; ++i)
  {
    checksum = static_cast<uint8_t>(checksum + frame[i]);
  }

  if (checksum != frame[8])
  {
    return false;
  }

  const uint16_t distance_cm = static_cast<uint16_t>(frame[2]) | (static_cast<uint16_t>(frame[3]) << 8);
  const uint16_t strength = static_cast<uint16_t>(frame[4]) | (static_cast<uint16_t>(frame[5]) << 8);

  sample.distance_m = distance_cm / 100.0;
  sample.signal_strength = strength;
  sample.timestamp = std::chrono::steady_clock::now();
  sample.valid = strength >= 40 && distance_cm > 0;
  sample.error_message = sample.valid ? "" : "信号弱或超出量程";
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
        if (extractNextLidarSample(buffer, active_protocol_, sample, detected_protocol))
        {
          active_protocol_ = detected_protocol;
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


