#ifndef VAPROVIEW_DATA_COLLECTOR_H
#define VAPROVIEW_DATA_COLLECTOR_H

#include "data_types.h"
#include "serial_port.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>

namespace VaproView
{

class DataCollector
{
public:
  using DataCallback = std::function<void()>;

  DataCollector();
  virtual ~DataCollector();

  DataCollector(const DataCollector&) = delete;
  DataCollector& operator=(const DataCollector&) = delete;

  bool start(const std::string& port, const SerialConfig& config);
  void stop();
  bool isRunning() const;

  void setDataCallback(DataCallback callback);
  std::string getLastError() const;

protected:
  virtual void run() = 0;
  virtual bool initialize();
  virtual void cleanup();

  SerialPort serial_;
  std::atomic<bool> running_{false};
  std::thread thread_;
  mutable std::mutex mutex_;
  DataCallback data_callback_;
};

class GnssCollector : public DataCollector
{
public:
  GnssData getLatestData();

protected:
  void run() override;

private:
  GnssData latest_data_;
};

class ImuCollector : public DataCollector
{
public:
  ImuData getLatestData();

protected:
  void run() override;

private:
  ImuData latest_data_;
};

class PtbCollector : public DataCollector
{
public:
  PtbData getLatestData();

protected:
  void run() override;
  bool initialize() override;

private:
  PtbData latest_data_;
  static constexpr const char* PTB_CMD_PRESSURE = ".P\r";
};

class HmpCollector : public DataCollector
{
public:
  HmpData getLatestData();

protected:
  void run() override;
  bool initialize() override;

private:
  HmpData latest_data_;

  static constexpr uint8_t HMP3_SLAVE_ADDR = 240;
  static constexpr uint16_t HMP3_REG_HUMIDITY = 0x0000;
  static constexpr uint16_t HMP3_REG_TEMPERATURE = 0x0002;

  static uint16_t modbusCrc16(const uint8_t* data, size_t len);
  static float decodeFloatLE(uint16_t reg0, uint16_t reg1);
};

}

#endif
