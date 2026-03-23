#ifndef VaporView_ETHERNET_CAPTURE_COLLECTOR_H
#define VaporView_ETHERNET_CAPTURE_COLLECTOR_H

#include "data_types.h"
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace VaporView
{

struct TdlasAdapterInfo
{
  std::string name;
  std::string description;
  std::string display_name;
};

struct TdlasCaptureConfig
{
  std::string adapter_name;
  std::string remote_ip;
  uint16_t remote_port = 0;
  uint16_t local_port = 0;
};

class EthernetCaptureCollector
{
public:
  using DataCallback = std::function<void()>;
  using LogCallback = std::function<void(const std::string&)>;
  using CancelCallback = std::function<bool()>;

  EthernetCaptureCollector();
  ~EthernetCaptureCollector();

  EthernetCaptureCollector(const EthernetCaptureCollector&) = delete;
  EthernetCaptureCollector& operator=(const EthernetCaptureCollector&) = delete;

  bool start(const TdlasCaptureConfig& config);
  void stop();
  bool isRunning() const;

  void setDataCallback(DataCallback callback);
  void setLogCallback(LogCallback callback);
  void setCancelCallback(CancelCallback callback);
  void setSampleRate(int hz);
  int getSampleRate() const;
  double getActualRate() const;
  std::string getLastError() const;
  TdlasData getLatestData() const;

  static std::vector<TdlasAdapterInfo> listAvailableAdapters();
  static bool isRuntimeAvailable(std::string* error_message = nullptr);
  static bool isFeatureCompiled();

private:
  void run();
  void log(const std::string& message) const;
  bool shouldEmitData() const;
  void updateLastEmitTime();

  mutable std::mutex mutex_;
  TdlasCaptureConfig config_;
  TdlasData latest_data_;
  DataCallback data_callback_;
  LogCallback log_callback_;
  CancelCallback cancel_callback_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<int> sample_rate_hz_{20};
  std::atomic<double> actual_rate_hz_{0.0};
  std::string last_error_;
  std::chrono::steady_clock::time_point last_emit_time_{};
  std::deque<TdlasMetricSample> recent_metric_samples_;
  std::deque<std::vector<uint8_t>> recent_payload_samples_;
};

}

#endif
