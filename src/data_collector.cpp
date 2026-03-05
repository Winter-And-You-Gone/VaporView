#include "data_collector.h"
#include "hipnuc_dec.h"
#include "pvtsln_data.hpp"
#include <cstring>
#include <unistd.h>

namespace VaproView
{

DataCollector::DataCollector()
{
}

DataCollector::~DataCollector()
{
  stop();
}

bool DataCollector::start(const std::string& port, const SerialConfig& config)
{
  if (running_.load())
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

void DataCollector::setDataCallback(DataCallback callback)
{
  std::lock_guard<std::mutex> lock(mutex_);
  data_callback_ = std::move(callback);
}

std::string DataCollector::getLastError() const
{
  return serial_.lastError();
}

void DataCollector::setSampleRate(int hz)
{
  if (hz < 1) hz = 1;
  if (hz > 20) hz = 20;
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

        if (line.find("PVTSLN") == std::string::npos)
        {
          continue;
        }

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
          latest_data_.heading = pvt_data.heading;
          latest_data_.heading_pitch = pvt_data.heading_pitch;
          latest_data_.position_status = pvt_data.position_status;
          latest_data_.num_satellites_used = pvt_data.num_satellites_used;
          latest_data_.num_satellites_tracked = pvt_data.num_satellites_tracked;
          latest_data_.gdop = pvt_data.gdop;
          latest_data_.pdop = pvt_data.pdop;
          latest_data_.hdop = pvt_data.hdop;
          latest_data_.htdop = pvt_data.htdop;
          latest_data_.tdop = pvt_data.tdop;
          latest_data_.diff_age = pvt_data.bestpos_diff_age;
          latest_data_.timestamp = std::chrono::steady_clock::now();
          latest_data_.valid = true;
          latest_data_.raw_sentence = line;
          latest_data_.error_message.clear();

          if (data_callback_ && shouldEmitData())
          {
            updateLastEmitTime();
            data_callback_();
          }
        }
        else
        {
          std::lock_guard<std::mutex> lock(mutex_);
          latest_data_.valid = false;
          latest_data_.error_message = error;
          latest_data_.raw_sentence = line;
        }
      }
    }
    else
    {
      usleep(5000);
    }
  }
}

ImuData ImuCollector::getLatestData()
{
  std::lock_guard<std::mutex> lock(mutex_);
  return latest_data_;
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
      usleep(5000);
    }
  }
}

PtbData PtbCollector::getLatestData()
{
  std::lock_guard<std::mutex> lock(mutex_);
  return latest_data_;
}

bool PtbCollector::initialize()
{
  return true;
}

void PtbCollector::run()
{
  char response[256];

  while (running_.load())
  {
    auto start_time = std::chrono::steady_clock::now();
    
    serial_.write(PTB_CMD_PRESSURE, std::strlen(PTB_CMD_PRESSURE));
    usleep(100000);

    ssize_t n = serial_.read(response, sizeof(response));
    if (n > 0)
    {
      std::string resp(response, static_cast<size_t>(n));
      while (!resp.empty() && (resp.back() == '\r' || resp.back() == '\n' || resp.back() == ' '))
      {
        resp.pop_back();
      }

      try
      {
        double pressure = std::stod(resp);
        std::lock_guard<std::mutex> lock(mutex_);
        latest_data_.pressure_hpa = pressure;
        latest_data_.valid = true;
        latest_data_.timestamp = std::chrono::steady_clock::now();
        latest_data_.error_message.clear();

        if (data_callback_ && shouldEmitData())
        {
          updateLastEmitTime();
          data_callback_();
        }
      }
      catch (const std::exception& e)
      {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_data_.valid = false;
        latest_data_.error_message = "Parse error: " + resp;
      }
    }

    int interval_ms = 1000 / sample_rate_hz_;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();
    int remaining = interval_ms - static_cast<int>(elapsed) - 100;
    if (remaining > 0)
    {
      usleep(static_cast<useconds_t>(remaining * 1000));
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
    usleep(100000);

    ssize_t n = serial_.read(response, sizeof(response));

    if (n >= 13 && response[0] == HMP3_SLAVE_ADDR && response[1] == 0x03)
    {
      uint16_t recv_crc = response[n - 2] | (response[n - 1] << 8);
      uint16_t calc_crc = modbusCrc16(response, static_cast<size_t>(n - 2));

      if (recv_crc == calc_crc)
      {
        uint16_t reg0 = (response[3] << 8) | response[4];
        uint16_t reg1 = (response[5] << 8) | response[6];
        uint16_t reg2 = (response[7] << 8) | response[8];
        uint16_t reg3 = (response[9] << 8) | response[10];

        float humidity = decodeFloatLE(reg0, reg1);
        float temperature = decodeFloatLE(reg2, reg3);

        std::lock_guard<std::mutex> lock(mutex_);
        latest_data_.humidity = humidity;
        latest_data_.temperature = temperature;
        latest_data_.valid = true;
        latest_data_.timestamp = std::chrono::steady_clock::now();
        latest_data_.error_message.clear();

        if (data_callback_ && shouldEmitData())
        {
          updateLastEmitTime();
          data_callback_();
        }
      }
      else
      {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_data_.valid = false;
        latest_data_.error_message = "CRC error";
      }
    }
    else if (n > 0 && response[1] == 0x83)
    {
      std::lock_guard<std::mutex> lock(mutex_);
      latest_data_.valid = false;
      latest_data_.error_message = "Modbus error: " + std::to_string(response[2]);
    }

    int interval_ms = 1000 / sample_rate_hz_;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();
    int remaining = interval_ms - static_cast<int>(elapsed) - 100;
    if (remaining > 0)
    {
      usleep(static_cast<useconds_t>(remaining * 1000));
    }
  }
}

}
