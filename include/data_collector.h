#ifndef VaporView_DATA_COLLECTOR_H
#define VaporView_DATA_COLLECTOR_H

#include "data_types.h"
#include "serial_port.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>

namespace VaporView
{

enum class LidarProtocol
{
  Unknown,
  TF03,
  TFA1500DistanceFrame,
  TFA1500LowFrequencyFrame,
  TFA1500HighFrequency,
  ObservedAaB7Frame
};

class DataCollector
{
public:
  using DataCallback = std::function<void()>;
  using LogCallback = std::function<void(const std::string&)>;
  using CancelCallback = std::function<bool()>;

  DataCollector();
  virtual ~DataCollector();

  DataCollector(const DataCollector&) = delete;
  DataCollector& operator=(const DataCollector&) = delete;

  bool start(const std::string& port, const SerialConfig& config);
  bool startStreaming();
  void stop();
  bool isRunning() const;

  void setDataCallback(DataCallback callback);
  void setLogCallback(LogCallback callback);
  void setCancelCallback(CancelCallback callback);
  void setSampleRate(int hz);
  int getSampleRate() const;
  double getActualRate() const;
  std::string getLastError() const;

protected:
  virtual void run() = 0;
  virtual bool initialize();
  virtual void cleanup();
  virtual bool setDeviceSampleRate(int hz);
  virtual bool checkDeviceResponse();
  bool shouldEmitData();
  void updateLastEmitTime();
  void recordDataReceived();
  void log(const std::string& message);
  bool isCancelRequested() const;

  SerialPort serial_;
  std::atomic<bool> running_{false};
  std::thread thread_;
  mutable std::mutex mutex_;
  DataCallback data_callback_;
  LogCallback log_callback_;
  CancelCallback cancel_callback_;
  SerialConfig serial_config_;
  std::atomic<int> sample_rate_hz_{1};
  std::chrono::steady_clock::time_point last_emit_time_;
  std::chrono::steady_clock::time_point last_data_time_;
  std::chrono::steady_clock::time_point freq_calc_start_;
  int data_count_{0};
  std::atomic<double> actual_rate_{0.0};
};

class GnssCollector : public DataCollector
{
public:
  GnssData getLatestData();
  bool setDeviceSampleRate(int hz) override;
  bool checkDeviceResponse() override;

protected:
  void run() override;

private:
  GnssData latest_data_;
};

class EpsilonCollector : public DataCollector
{
public:
  using RawFrameCallback = std::function<void(uint64_t host_timestamp_us,
                                              uint8_t packet_id,
                                              uint8_t serial_number,
                                              const uint8_t* frame,
                                              size_t size)>;

  EpsilonData getLatestData();
  bool setDeviceSampleRate(int hz) override;
  bool checkDeviceResponse() override;
  void setRawFrameCallback(RawFrameCallback callback);

protected:
  void run() override;

private:
  EpsilonData latest_data_;
  RawFrameCallback raw_frame_callback_;
};

class ImuCollector : public DataCollector
{
public:
  using RawPacketCallback = std::function<void(uint64_t host_timestamp_us, uint8_t frame_tag, const uint8_t* data, size_t size)>;

  ImuData getLatestData();
  bool setDeviceSampleRate(int hz) override;
  bool checkDeviceResponse() override;
  void setRawPacketCallback(RawPacketCallback callback);
  bool setOutputMessageType(const std::string& message_type);
  std::string outputMessageType() const;
  bool sendAsciiCommand(const std::string& command, int wait_ms = 60);

protected:
  void run() override;

private:
  ImuData latest_data_;
  RawPacketCallback raw_packet_callback_;
  std::string output_message_type_ = "HI91";
};

class PtbCollector : public DataCollector
{
public:
  PtbData getLatestData();
  bool setDeviceSampleRate(int hz) override;
  bool checkDeviceResponse() override;

protected:
  void run() override;
  bool initialize() override;
  void cleanup() override;

private:
  PtbData latest_data_;
  static constexpr const char* PTB_CMD_PRESSURE = ".P\r";
  static constexpr const char* PTB_CMD_CONTINUOUS = ".BP\r";
  static constexpr const char* PTB_CMD_STOP = "\r";
};

class HmpCollector : public DataCollector
{
public:
  HmpData getLatestData();
  bool checkDeviceResponse() override;

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

class LidarCollector : public DataCollector
{
public:
  LidarData getLatestData();
  bool setDeviceSampleRate(int hz) override;
  bool checkDeviceResponse() override;

protected:
  void run() override;
  bool initialize() override;
  void cleanup() override;

private:
  LidarData latest_data_;

  static constexpr uint8_t TF03_HEADER = 0x59;
  static constexpr uint8_t TFA1500_HEADER = 0x5C;
  static constexpr size_t TF03_FRAME_SIZE = 9;
  static constexpr size_t TFA1500_FRAME_SIZE = 5;

  LidarProtocol active_protocol_ = LidarProtocol::Unknown;
  bool tfa1500_stream_started_ = false;

  bool ensureTfa1500Streaming();
  bool ensureTfa1500Standby();
  bool ensureTfa1500DistanceOutput();
  bool ensureTfa1500LowFrequencyContinuous();
  void stopTfa1500Streaming();
  static bool parseTf03Frame(const uint8_t* frame, size_t size, LidarData& sample);
  static bool parseTfa1500Frame(const uint8_t* frame, size_t size, LidarData& sample);
};

}

#endif

