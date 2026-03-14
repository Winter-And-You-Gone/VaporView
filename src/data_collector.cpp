#include "data_collector.h"
#include "hipnuc_dec.h"
#include "pvtsln_data.hpp"
#include <cstring>
#include <iomanip>
#include <sstream>

namespace VaproView
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

ssize_t readAccumulated(SerialPort& serial, uint8_t* buffer, size_t capacity, int total_wait_ms, int step_ms = 20)
{
  size_t total = 0;
  int elapsed = 0;
  while (elapsed < total_wait_ms && total < capacity)
  {
    ssize_t n = serial.read(buffer + total, capacity - total);
    if (n > 0)
    {
      total += static_cast<size_t>(n);
    }
    sleepMs(step_ms);
    elapsed += step_ms;
  }
  return static_cast<ssize_t>(total);
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
  actual_rate_ = 0.0;

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
  if (log_callback_)
  {
    log_callback_(message);
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
  if (hz > 500) hz = 500;
  std::lock_guard<std::mutex> lock(mutex_);
  sample_rate_hz_ = hz;
}

int DataCollector::getSampleRate() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return sample_rate_hz_;
}

bool DataCollector::shouldEmitData()
{
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - last_emit_time_).count();
  int interval_us = 1000000 / sample_rate_hz_;
  return elapsed >= interval_us;
}

void DataCollector::updateLastEmitTime()
{
  last_emit_time_ = std::chrono::steady_clock::now();
}

double DataCollector::getActualRate() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return actual_rate_;
}

void DataCollector::recordDataReceived()
{
  auto now = std::chrono::steady_clock::now();
  data_count_++;
  
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - freq_calc_start_).count();
  if (elapsed >= 1000)
  {
    actual_rate_ = data_count_ * 1000.0 / elapsed;
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
    log("[RTK TX] Failed to send command");
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
        log("[RTK RX] PVTSLNA output started at " + std::to_string(hz) + " Hz");
        break;
      }
    }
  }
  else
  {
    log("[RTK RX] No response (command may have been accepted)");
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

            recordDataReceived();

            if (data_callback_ && shouldEmitData())
            {
              updateLastEmitTime();
              data_callback_();
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

ImuData ImuCollector::getLatestData()
{
  std::lock_guard<std::mutex> lock(mutex_);
  return latest_data_;
}

bool ImuCollector::setDeviceSampleRate(int hz)
{
  double period;
  if (hz == 1) period = 1.0;
  else if (hz == 2) period = 0.5;
  else if (hz == 5) period = 0.2;
  else if (hz == 10) period = 0.1;
  else if (hz == 20) period = 0.05;
  else if (hz == 50) period = 0.02;
  else if (hz == 100) period = 0.01;
  else if (hz == 200) period = 0.005;
  else if (hz == 500) period = 0.002;
  else
  {
    log("IMU: Unsupported sample rate: " + std::to_string(hz) + " Hz");
    return false;
  }

  char cmd[64];
  snprintf(cmd, sizeof(cmd), "LOG HI91 ONTIME %.3f\r\n", period);
  
  ssize_t written = serial_.write(cmd, strlen(cmd));
  if (written < 0)
  {
    log("IMU: Failed to send sample rate command");
    return false;
  }

  log("IMU: Set sample rate to " + std::to_string(hz) + " Hz (period: " + std::to_string(period) + "s)");
  sample_rate_hz_ = hz;
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

          if (raw.hi83.tag == 0x83)
          {
            sample.valid = true;
            sample.from_hi83 = true;
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
          else if (raw.hi81.tag == 0x81)
          {
            sample.valid = true;
            sample.from_hi83 = false;
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
            std::lock_guard<std::mutex> lock(mutex_);
            latest_data_ = sample;

            recordDataReceived();

            if (data_callback_ && shouldEmitData())
            {
              updateLastEmitTime();
              data_callback_();
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
    log("PTB210: Unsupported sample rate: " + std::to_string(hz) + " Hz (valid: 1-70 Hz)");
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
    log("PTB210: Failed to send AVRG command");
    return false;
  }

  sleepMs(50);

  char cmd[32];
  snprintf(cmd, sizeof(cmd), ".MPM.%d\r", mpm);
  
  written = serial_.write(cmd, strlen(cmd));
  if (written != static_cast<ssize_t>(std::strlen(cmd)))
  {
    log("PTB210: Failed to send MPM command");
    return false;
  }

  sleepMs(100);

  const char* reset_cmd = ".RESET\r";
  written = serial_.write(reset_cmd, strlen(reset_cmd));
  if (written != static_cast<ssize_t>(std::strlen(reset_cmd)))
  {
    log("PTB210: Failed to send RESET command");
    return false;
  }

  sleepMs(500);

  if (running_.load())
  {
    written = serial_.write(PTB_CMD_CONTINUOUS, std::strlen(PTB_CMD_CONTINUOUS));
    if (written != static_cast<ssize_t>(std::strlen(PTB_CMD_CONTINUOUS)))
    {
      log("PTB210: Failed to resume continuous output");
      return false;
    }
  }

  log("PTB210: Set sample rate to " + std::to_string(hz) + " Hz (MPM: " + std::to_string(mpm) + ")");
  sample_rate_hz_ = hz;
  return true;
}

bool PtbCollector::initialize()
{
  return true;
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
  int max_attempts = 5;
  
  for (int i = 0; i < max_attempts; i++)
  {
    if (isCancelRequested())
    {
      return false;
    }
    constexpr int total_wait_ms = 500;
    const auto attempt_start = std::chrono::steady_clock::now();
    log(formatDetectionProgress("发送PTB压力查询", i + 1, max_attempts, computeRemainingSeconds(attempt_start, total_wait_ms)));
    serial_.flush();
    serial_.write(PTB_CMD_PRESSURE, std::strlen(PTB_CMD_PRESSURE));
    
    for (int j = 0; j < 10; j++)
    {
      if (isCancelRequested())
      {
        return false;
      }
      log(formatDetectionProgress("等待PTB压力返回", i + 1, max_attempts, computeRemainingSeconds(attempt_start, total_wait_ms)));
      sleepMs(50);
      ssize_t n = serial_.read(response, sizeof(response));
      if (n > 0)
      {
        std::string resp(response, static_cast<size_t>(n));
        while (!resp.empty() && (resp.back() == '\r' || resp.back() == '\n' || resp.back() == ' '))
        {
          resp.pop_back();
        }
        
        if (!resp.empty())
        {
          try
          {
            std::stod(resp);
            return true;
          }
          catch (...)
          {
          }
        }
      }
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
      log("PTB210: Failed to start continuous output");
      return false;
    }
    return true;
  };

  auto processLine = [this](std::string line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
    {
      line.pop_back();
    }

    if (line.empty())
    {
      return;
    }

    try
    {
      double pressure = std::stod(line);
      std::lock_guard<std::mutex> lock(mutex_);
      latest_data_.pressure_hpa = pressure;
      latest_data_.valid = true;
      latest_data_.timestamp = std::chrono::steady_clock::now();
      latest_data_.error_message.clear();

      recordDataReceived();

      if (data_callback_ && shouldEmitData())
      {
        updateLastEmitTime();
        data_callback_();
      }
    }
    catch (const std::exception&)
    {
      // Ignore command echoes or control lines and wait for the next numeric sample.
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

        const PtbData before = getLatestData();
        processLine(line);
        const PtbData after = getLatestData();
        if (after.valid && (!before.valid || after.timestamp != before.timestamp))
        {
          last_data_time = after.timestamp;
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
        log("PTB210: No continuous data yet, retrying .BP");
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
    const int response_wait_ms = std::min(500, std::max(50, interval_ms));
    
    request[0] = HMP3_SLAVE_ADDR;
    request[1] = 0x03;
    request[2] = 0x00;
    request[3] = 0x00;
    request[4] = 0x00;
    request[5] = 0x04;
    uint16_t crc = modbusCrc16(request, 6);
    request[6] = crc & 0xFF;
    request[7] = (crc >> 8) & 0xFF;

    serial_.flush();
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
      std::lock_guard<std::mutex> lock(mutex_);
      latest_data_.humidity = humidity;
      latest_data_.temperature = temperature;
      latest_data_.valid = true;
      latest_data_.timestamp = std::chrono::steady_clock::now();
      latest_data_.error_message.clear();

      recordDataReceived();

      if (data_callback_ && shouldEmitData())
      {
        updateLastEmitTime();
        data_callback_();
      }
    }
    else if (parsed == HmpParseResult::Exception)
    {
      std::lock_guard<std::mutex> lock(mutex_);
      latest_data_.valid = false;
      latest_data_.error_message = "Modbus error: " + std::to_string(exception_code);
    }
    else if (parsed == HmpParseResult::CrcError)
    {
      std::lock_guard<std::mutex> lock(mutex_);
      latest_data_.valid = false;
      latest_data_.error_message = "CRC error";
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

}

