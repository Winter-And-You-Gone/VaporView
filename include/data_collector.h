#ifndef VaporView_DATA_COLLECTOR_H
#define VaporView_DATA_COLLECTOR_H

#include "data_types.h"
#include "serial_port.h"
#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace VaporView
{

enum class PressureSensorProtocol
{
  Ptb210,
  Bmp390Serial
};

enum class HumiditySensorProtocol
{
  Hmp3Modbus,
  Sht45Serial
};

struct EnvironmentSerialValues
{
  bool has_temperature = false;
  bool has_humidity = false;
  bool has_pressure = false;
  double temperature_c = 0.0;
  double humidity_rh = 0.0;
  double pressure_hpa = 0.0;
};

bool parseEnvironmentSerialLine(const std::string& line, EnvironmentSerialValues& values);

enum class LidarProtocol
{
  Unknown = 0,
  TFA1500DistanceFrame = 2,
  TFA1500LowFrequencyFrame = 3,
  TFA1500HighFrequency = 4,
  ObservedAaB7Frame = 5
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
  void setEnglish(bool english);
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
  bool isEnglishLog() const;
  bool isCancelRequested() const;

  SerialPort serial_;
  std::string port_name_;
  std::atomic<bool> running_{false};
  std::thread thread_;
  mutable std::mutex mutex_;
  DataCallback data_callback_;
  LogCallback log_callback_;
  CancelCallback cancel_callback_;
  SerialConfig serial_config_;
  std::atomic<bool> log_english_{false};
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
  bool setOutputPacketRates(const std::map<uint8_t, int>& packet_rates, bool force_apply = false);
  bool configureRtcmPort(int port_index, int baud_rate);
  bool configureMainAntennaLeverArm(double x_m, double y_m, double z_m);
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
  using RawResponseCallback = std::function<void(uint64_t host_timestamp_us, const uint8_t* data, size_t size)>;

  PtbData getLatestData();
  bool setDeviceSampleRate(int hz) override;
  bool checkDeviceResponse() override;
  void setRawResponseCallback(RawResponseCallback callback);
  void setProtocol(PressureSensorProtocol protocol);
  PressureSensorProtocol protocol() const;

protected:
  void run() override;
  bool initialize() override;
  void cleanup() override;

private:
  PtbData latest_data_;
  RawResponseCallback raw_response_callback_;
  PressureSensorProtocol protocol_ = PressureSensorProtocol::Ptb210;
  static constexpr const char* PTB_CMD_PRESSURE = ".P\r";
  static constexpr const char* PTB_CMD_CONTINUOUS = ".BP\r";
  static constexpr const char* PTB_CMD_STOP = "\r";
};

class HmpCollector : public DataCollector
{
public:
  using RawResponseCallback = std::function<void(uint64_t host_timestamp_us, const uint8_t* data, size_t size)>;

  HmpData getLatestData();
  bool checkDeviceResponse() override;
  void setRawResponseCallback(RawResponseCallback callback);
  void setProtocol(HumiditySensorProtocol protocol);
  HumiditySensorProtocol protocol() const;

protected:
  void run() override;
  bool initialize() override;

private:
  HmpData latest_data_;
  RawResponseCallback raw_response_callback_;
  HumiditySensorProtocol protocol_ = HumiditySensorProtocol::Hmp3Modbus;

  static constexpr uint8_t HMP3_SLAVE_ADDR = 240;
  static constexpr uint16_t HMP3_REG_HUMIDITY = 0x0000;
  static constexpr uint16_t HMP3_REG_TEMPERATURE = 0x0002;

  static uint16_t modbusCrc16(const uint8_t* data, size_t len);
  static float decodeFloatLE(uint16_t reg0, uint16_t reg1);
};

class LidarCollector : public DataCollector
{
public:
  using RawFrameCallback = std::function<void(uint64_t host_timestamp_us,
                                             LidarProtocol protocol,
                                             const uint8_t* frame,
                                             size_t size)>;

  LidarData getLatestData();
  bool setDeviceSampleRate(int hz) override;
  bool checkDeviceResponse() override;
  void setRawFrameCallback(RawFrameCallback callback);

protected:
  void run() override;
  bool initialize() override;
  void cleanup() override;

private:
  LidarData latest_data_;
  RawFrameCallback raw_frame_callback_;

  static constexpr uint8_t TFA1500_HEADER = 0x5C;
  static constexpr size_t TFA1500_FRAME_SIZE = 5;

  LidarProtocol active_protocol_ = LidarProtocol::Unknown;
  bool tfa1500_stream_started_ = false;

  bool ensureTfa1500Streaming();
  bool ensureTfa1500Standby();
  bool ensureTfa1500DistanceOutput();
  bool ensureTfa1500LowFrequencyContinuous();
  void stopTfa1500Streaming();
  static bool parseTfa1500Frame(const uint8_t* frame, size_t size, LidarData& sample);
};

class TemperatureControllerCollector : public DataCollector
{
public:
  using RawFrameCallback = std::function<void(uint64_t host_timestamp_us, const uint8_t* frame, size_t size)>;

  TemperatureControllerData getLatestData();
  bool checkDeviceResponse() override;
  void setRawFrameCallback(RawFrameCallback callback);
  void setSlaveAddress(uint8_t slave_address);
  uint8_t slaveAddress() const;
  bool setTargetTemperature(uint8_t channel, double celsius);
  bool setOutputEnabled(uint8_t channel, bool enabled);
  bool setOutputMode(uint8_t channel, uint16_t mode);
  bool setMaxOutputPercent(uint8_t channel, uint16_t percent);
  bool setPid(uint8_t channel, uint32_t kp, uint32_t ki, uint32_t kd);
  bool setAutoPid(uint8_t channel, uint16_t mode);
  bool setControllerMode(uint16_t mode);
  bool setDeviceAddress(uint16_t address);
  bool setRs485BaudIndex(uint16_t baud_index);
  bool setOvertempOutputMode(uint16_t mode);
  bool setSensorConfig(uint8_t channel,
                       uint16_t sensor_model,
                       uint32_t ntc_b,
                       uint32_t ntc_r0,
                       uint32_t pt_r0,
                       int32_t pt_a,
                       int32_t pt_b,
                       int32_t pt_c,
                       const std::array<int64_t, 8>& polynomial_mantissas,
                       const std::array<int16_t, 8>& polynomial_exponents);
  bool restoreFactoryDefaults();

protected:
  void run() override;
  bool initialize() override;

private:
  TemperatureControllerData latest_data_;
  RawFrameCallback raw_frame_callback_;
  uint8_t slave_address_ = 1;
  int channel_count_ = 1;
  bool polynomial_exponents_supported_ = true;
  std::mutex modbus_mutex_;

  bool readSnapshot(TemperatureControllerData& sample);
  bool readChannel(uint8_t channel, TemperatureControllerChannelData& channel_data);
  bool readRegisters(uint16_t address, uint16_t count, std::vector<uint16_t>& registers, int wait_ms = 200);
  bool readRegistersUnlocked(uint16_t address, uint16_t count, std::vector<uint16_t>& registers, int wait_ms);
  bool queryAscii(const std::string& command, std::string& response, int wait_ms = 1200);
  bool writeRegisters(uint16_t address, const std::vector<uint16_t>& registers, int wait_ms = 200);
  bool writeRegistersUnlocked(uint16_t address, const std::vector<uint16_t>& registers, int wait_ms);
  bool writeAndConfirm(uint8_t channel, uint16_t address, const std::vector<uint16_t>& registers);
  bool readResponseFrame(uint8_t function_code, std::vector<uint8_t>& frame, int wait_ms);
  void publishRawFrame(const std::vector<uint8_t>& frame);
};

}

#endif

