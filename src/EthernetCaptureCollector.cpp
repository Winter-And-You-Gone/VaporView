#include "EthernetCaptureCollector.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <thread>

#if defined(_WIN32) && defined(ENABLE_TDLAS_CAPTURE) && ENABLE_TDLAS_CAPTURE
#include <pcap.h>
#include <windows.h>
#endif

namespace VaporView
{
namespace
{
constexpr int kCaptureSize = 65536;
constexpr int kReadTimeoutMs = 250;
constexpr size_t kPayloadHexBytes = 64;
constexpr size_t kTrendSampleHistory = 24;

std::string jsonEscape(const std::string& value)
{
  std::string escaped;
  escaped.reserve(value.size() + 8);
  for (char ch : value)
  {
    switch (ch)
    {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped.push_back(ch);
        break;
    }
  }
  return escaped;
}

std::string summarizePayloadHex(const uint8_t* payload, size_t length)
{
  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  const size_t limit = (std::min)(length, kPayloadHexBytes);
  for (size_t i = 0; i < limit; ++i)
  {
    if (i > 0)
    {
      stream << ' ';
    }
    stream << std::setw(2) << static_cast<int>(payload[i]);
  }
  if (length > limit)
  {
    stream << " ...";
  }
  return stream.str();
}

std::string summarizeRawHex(const uint8_t* data, size_t length)
{
  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (size_t i = 0; i < length; ++i)
  {
    if (i > 0)
    {
      stream << ' ';
    }
    stream << std::setw(2) << static_cast<int>(data[i]);
  }
  return stream.str();
}

uint16_t readU16BE(const uint8_t* data)
{
  return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | static_cast<uint16_t>(data[1]));
}

uint32_t readU32BE(const uint8_t* data)
{
  return (static_cast<uint32_t>(data[0]) << 24) |
         (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) |
         static_cast<uint32_t>(data[3]);
}

uint16_t readU16LE(const uint8_t* data)
{
  return static_cast<uint16_t>((static_cast<uint16_t>(data[1]) << 8) | static_cast<uint16_t>(data[0]));
}

float readF32LE(const uint8_t* data)
{
  uint32_t raw = static_cast<uint32_t>(data[0]) |
                 (static_cast<uint32_t>(data[1]) << 8) |
                 (static_cast<uint32_t>(data[2]) << 16) |
                 (static_cast<uint32_t>(data[3]) << 24);
  float value = 0.0f;
  std::memcpy(&value, &raw, sizeof(value));
  return value;
}

bool isFiniteMetric(float value)
{
  return std::isfinite(value) && std::fabs(value) < 1.0e9f;
}

std::string macToString(const uint8_t* data)
{
  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (int i = 0; i < 6; ++i)
  {
    if (i > 0)
    {
      stream << ':';
    }
    stream << std::setw(2) << static_cast<int>(data[i]);
  }
  return stream.str();
}

std::string ipv4ToString(const uint8_t* data)
{
  std::ostringstream stream;
  stream << static_cast<int>(data[0]) << '.'
         << static_cast<int>(data[1]) << '.'
         << static_cast<int>(data[2]) << '.'
         << static_cast<int>(data[3]);
  return stream.str();
}

std::string formatUtcNow()
{
  using namespace std::chrono;
  const auto now = system_clock::now();
  const std::time_t tt = system_clock::to_time_t(now);
  std::tm tm_utc{};
#if defined(_WIN32)
  gmtime_s(&tm_utc, &tt);
#else
  gmtime_r(&tt, &tm_utc);
#endif
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
  return std::string(buffer);
}

std::string metricsToJson(const std::vector<TdlasMetric>& metrics)
{
  std::ostringstream stream;
  stream << '[';
  for (size_t i = 0; i < metrics.size(); ++i)
  {
    const TdlasMetric& metric = metrics[i];
    if (i > 0)
    {
      stream << ',';
    }
    stream << '{'
           << "\"key\":\"" << jsonEscape(metric.key) << "\","
           << "\"label_zh\":\"" << jsonEscape(metric.label_zh) << "\","
           << "\"label_en\":\"" << jsonEscape(metric.label_en) << "\","
           << "\"unit\":\"" << jsonEscape(metric.unit) << "\","
           << "\"wire_type\":\"" << jsonEscape(metric.wire_type) << "\","
           << "\"raw_hex\":\"" << jsonEscape(metric.raw_hex) << "\","
           << "\"value\":" << metric.value << ','
           << "\"offset\":" << metric.offset << ','
           << "\"valid\":" << (metric.valid ? "true" : "false") << ','
           << "\"confidence\":\"" << jsonEscape(metric.confidence) << "\""
           << '}';
  }
  stream << ']';
  return stream.str();
}

std::vector<TdlasMetric> parseBusinessMetrics(const uint8_t* payload, size_t length, bool* any_valid)
{
  struct CandidateMetric
  {
    const char* key;
    const char* label_zh;
    const char* label_en;
    const char* unit;
    size_t offset;
    enum class Type
    {
      UInt16,
      Float32
    } type;
  };

  constexpr std::array<CandidateMetric, 7> kCandidates = {{
      {"avg_concentration", "平均浓度", "Average Concentration", "", 0, CandidateMetric::Type::UInt16},
      {"input_signal_range", "输入信号范围", "Input Signal Range", "", 2, CandidateMetric::Type::UInt16},
      {"harmonic_max", "谐波检测最大值", "Harmonic Peak Max", "V", 4, CandidateMetric::Type::Float32},
      {"harmonic_min", "谐波检测最小值", "Harmonic Peak Min", "V", 8, CandidateMetric::Type::Float32},
      {"normal_frequency", "正常步进频率", "Normal Sweep Frequency", "Hz", 12, CandidateMetric::Type::Float32},
      {"normal_value", "正常浓度值", "Normal Concentration", "", 16, CandidateMetric::Type::Float32},
      {"ch1_result", "CH1检测浓度结果", "CH1 Concentration Result", "", 20, CandidateMetric::Type::Float32},
  }};

  std::vector<TdlasMetric> metrics;
  metrics.reserve(kCandidates.size());
  bool valid_found = false;
  for (const CandidateMetric& candidate : kCandidates)
  {
    TdlasMetric metric;
    metric.key = candidate.key;
    metric.label_zh = candidate.label_zh;
    metric.label_en = candidate.label_en;
    metric.unit = candidate.unit;
    metric.offset = static_cast<uint32_t>(candidate.offset);
    metric.confidence = "unverified";

    if (candidate.type == CandidateMetric::Type::UInt16)
    {
      metric.wire_type = "u16le";
      if (candidate.offset + 2 <= length)
      {
        metric.raw_hex = summarizeRawHex(payload + candidate.offset, 2);
        metric.value = static_cast<double>(readU16LE(payload + candidate.offset));
        metric.valid = true;
        valid_found = true;
      }
    }
    else if (candidate.offset + 4 <= length)
    {
      metric.wire_type = "f32le";
      metric.raw_hex = summarizeRawHex(payload + candidate.offset, 4);
      const float value = readF32LE(payload + candidate.offset);
      if (isFiniteMetric(value))
      {
        metric.value = static_cast<double>(value);
        metric.valid = true;
        valid_found = true;
      }
    }

    metrics.push_back(metric);
  }

  if (any_valid)
  {
    *any_valid = valid_found;
  }
  return metrics;
}

std::string metricsSummary(const std::vector<TdlasMetric>& metrics, bool english)
{
  std::ostringstream stream;
  bool first = true;
  for (const TdlasMetric& metric : metrics)
  {
    if (!metric.valid)
    {
      continue;
    }
    if (!first)
    {
      stream << '\n';
    }
    first = false;
    stream << (english ? metric.label_en : metric.label_zh) << ": " << metric.value;
    if (!metric.unit.empty())
    {
      stream << ' ' << metric.unit;
    }
    stream << " [" << metric.confidence << ']';
  }
  return stream.str();
}
}

#if defined(_WIN32) && defined(ENABLE_TDLAS_CAPTURE) && ENABLE_TDLAS_CAPTURE
namespace
{
class PcapRuntime
{
public:
  using FindAllDevsFn = int (*)(pcap_if_t**, char*);
  using FreeAllDevsFn = void (*)(pcap_if_t*);
  using OpenLiveFn = pcap_t* (*)(const char*, int, int, int, char*);
  using CloseFn = void (*)(pcap_t*);
  using NextExFn = int (*)(pcap_t*, struct pcap_pkthdr**, const u_char**);
  using LibVersionFn = const char* (*)();

  static PcapRuntime& instance()
  {
    static PcapRuntime runtime;
    return runtime;
  }

  bool isLoaded(std::string* error_message = nullptr)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_)
    {
      load();
    }
    if (!loaded_ && error_message)
    {
      *error_message = error_message_;
    }
    return loaded_;
  }

  FindAllDevsFn findalldevs() const { return findalldevs_; }
  FreeAllDevsFn freealldevs() const { return freealldevs_; }
  OpenLiveFn openlive() const { return openlive_; }
  CloseFn close() const { return close_; }
  NextExFn nextex() const { return nextex_; }
  LibVersionFn libversion() const { return libversion_; }

private:
  void load()
  {
    initialized_ = true;
    wpcap_module_ = ::LoadLibraryA("wpcap.dll");
    if (!wpcap_module_)
    {
      error_message_ = "Npcap runtime not found: wpcap.dll is missing";
      return;
    }

    findalldevs_ = reinterpret_cast<FindAllDevsFn>(::GetProcAddress(wpcap_module_, "pcap_findalldevs"));
    freealldevs_ = reinterpret_cast<FreeAllDevsFn>(::GetProcAddress(wpcap_module_, "pcap_freealldevs"));
    openlive_ = reinterpret_cast<OpenLiveFn>(::GetProcAddress(wpcap_module_, "pcap_open_live"));
    close_ = reinterpret_cast<CloseFn>(::GetProcAddress(wpcap_module_, "pcap_close"));
    nextex_ = reinterpret_cast<NextExFn>(::GetProcAddress(wpcap_module_, "pcap_next_ex"));
    libversion_ = reinterpret_cast<LibVersionFn>(::GetProcAddress(wpcap_module_, "pcap_lib_version"));

    if (!findalldevs_ || !freealldevs_ || !openlive_ || !close_ || !nextex_)
    {
      error_message_ = "Npcap runtime is incomplete: required pcap entry points are missing";
      return;
    }

    loaded_ = true;
  }

  std::mutex mutex_;
  bool initialized_ = false;
  bool loaded_ = false;
  std::string error_message_;
  HMODULE wpcap_module_ = nullptr;
  FindAllDevsFn findalldevs_ = nullptr;
  FreeAllDevsFn freealldevs_ = nullptr;
  OpenLiveFn openlive_ = nullptr;
  CloseFn close_ = nullptr;
  NextExFn nextex_ = nullptr;
  LibVersionFn libversion_ = nullptr;
};

struct ParsedPacket
{
  PacketHeaders headers;
  const uint8_t* payload = nullptr;
  size_t payload_length = 0;
  bool is_ipv4 = false;
  bool is_udp = false;
};

bool parsePacket(const uint8_t* data, size_t length, ParsedPacket* parsed)
{
  if (!parsed || !data || length < 14)
  {
    return false;
  }

  ParsedPacket packet;
  packet.headers.ethernet.destination.mac = macToString(data + 0);
  packet.headers.ethernet.source.mac = macToString(data + 6);
  packet.headers.ethernet.destination.valid = true;
  packet.headers.ethernet.source.valid = true;
  packet.headers.ethernet.ether_type = readU16BE(data + 12);
  packet.headers.ethernet.valid = true;

  if (packet.headers.ethernet.ether_type != 0x0800)
  {
    *parsed = packet;
    return true;
  }

  packet.is_ipv4 = true;
  if (length < 14 + 20)
  {
    return false;
  }

  const uint8_t* ip = data + 14;
  const uint8_t ihl = static_cast<uint8_t>((ip[0] & 0x0F) * 4);
  if (ihl < 20 || length < 14 + ihl)
  {
    return false;
  }

  packet.headers.ipv4.version = static_cast<uint8_t>((ip[0] >> 4) & 0x0F);
  packet.headers.ipv4.ihl = ihl;
  packet.headers.ipv4.type_of_service = ip[1];
  packet.headers.ipv4.total_length = readU16BE(ip + 2);
  packet.headers.ipv4.identification = readU16BE(ip + 4);
  const uint16_t flags_and_offset = readU16BE(ip + 6);
  packet.headers.ipv4.flags = static_cast<uint16_t>((flags_and_offset >> 13) & 0x07);
  packet.headers.ipv4.fragment_offset = static_cast<uint16_t>(flags_and_offset & 0x1FFF);
  packet.headers.ipv4.ttl = ip[8];
  packet.headers.ipv4.protocol = ip[9];
  packet.headers.ipv4.header_checksum = readU16BE(ip + 10);
  packet.headers.ipv4.source.ip = ipv4ToString(ip + 12);
  packet.headers.ipv4.destination.ip = ipv4ToString(ip + 16);
  packet.headers.ipv4.source.valid = true;
  packet.headers.ipv4.destination.valid = true;
  packet.headers.ipv4.valid = true;

  if (packet.headers.ipv4.protocol != 17)
  {
    *parsed = packet;
    return true;
  }

  packet.is_udp = true;
  const size_t udp_offset = 14 + ihl;
  if (length < udp_offset + 8)
  {
    return false;
  }

  const uint8_t* udp = data + udp_offset;
  packet.headers.udp.source_port = readU16BE(udp + 0);
  packet.headers.udp.destination_port = readU16BE(udp + 2);
  packet.headers.udp.length = readU16BE(udp + 4);
  packet.headers.udp.checksum = readU16BE(udp + 6);
  packet.headers.udp.valid = true;
  packet.headers.ipv4.source.port = packet.headers.udp.source_port;
  packet.headers.ipv4.destination.port = packet.headers.udp.destination_port;

  packet.payload = udp + 8;
  packet.payload_length = length - (udp_offset + 8);
  *parsed = packet;
  return true;
}

bool packetMatchesConfig(const ParsedPacket& packet, const TdlasCaptureConfig& config)
{
  if (!packet.is_udp)
  {
    return false;
  }

  auto incomingMatches = [&]() {
    const bool ipMatch = config.remote_ip.empty() || packet.headers.ipv4.source.ip == config.remote_ip;
    const bool remotePortMatch = config.remote_port == 0 || packet.headers.udp.source_port == config.remote_port;
    const bool localPortMatch = config.local_port == 0 || packet.headers.udp.destination_port == config.local_port;
    return ipMatch && remotePortMatch && localPortMatch;
  };
  auto outgoingMatches = [&]() {
    const bool ipMatch = config.remote_ip.empty() || packet.headers.ipv4.destination.ip == config.remote_ip;
    const bool remotePortMatch = config.remote_port == 0 || packet.headers.udp.destination_port == config.remote_port;
    const bool localPortMatch = config.local_port == 0 || packet.headers.udp.source_port == config.local_port;
    return ipMatch && remotePortMatch && localPortMatch;
  };

  return incomingMatches() || outgoingMatches();
}
}
#endif

EthernetCaptureCollector::EthernetCaptureCollector() = default;

EthernetCaptureCollector::~EthernetCaptureCollector()
{
  stop();
}

bool EthernetCaptureCollector::start(const TdlasCaptureConfig& config)
{
  stop();
  config_ = config;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    recent_metric_samples_.clear();
    latest_data_ = TdlasData{};
    latest_data_.adapter_name = config.adapter_name;
    latest_data_.error_message = "Waiting for matching traffic";
  }
  last_error_.clear();

  if (config.adapter_name.empty())
  {
    last_error_ = "No capture adapter selected";
    std::lock_guard<std::mutex> lock(mutex_);
    latest_data_.error_message = last_error_;
    return false;
  }

#if defined(_WIN32) && defined(ENABLE_TDLAS_CAPTURE) && ENABLE_TDLAS_CAPTURE
  std::string runtime_error;
  if (!isRuntimeAvailable(&runtime_error))
  {
    last_error_ = runtime_error;
    std::lock_guard<std::mutex> lock(mutex_);
    latest_data_.error_message = last_error_;
    return false;
  }

  running_.store(true);
  thread_ = std::thread([this]() { run(); });
  return true;
#else
  last_error_ = "TDLAS Ethernet capture was not compiled into this build";
  std::lock_guard<std::mutex> lock(mutex_);
  latest_data_.error_message = last_error_;
  return false;
#endif
}

void EthernetCaptureCollector::stop()
{
  running_.store(false);
  if (thread_.joinable())
  {
    thread_.join();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  recent_metric_samples_.clear();
  latest_data_.capture_session_active = false;
  if (latest_data_.error_message.empty())
  {
    latest_data_.error_message = "Disconnected";
  }
}

bool EthernetCaptureCollector::isRunning() const
{
  return running_.load();
}

void EthernetCaptureCollector::setDataCallback(DataCallback callback)
{
  std::lock_guard<std::mutex> lock(mutex_);
  data_callback_ = std::move(callback);
}

void EthernetCaptureCollector::setLogCallback(LogCallback callback)
{
  std::lock_guard<std::mutex> lock(mutex_);
  log_callback_ = std::move(callback);
}

void EthernetCaptureCollector::setCancelCallback(CancelCallback callback)
{
  std::lock_guard<std::mutex> lock(mutex_);
  cancel_callback_ = std::move(callback);
}

void EthernetCaptureCollector::setSampleRate(int hz)
{
  sample_rate_hz_.store(std::clamp(hz, 1, 500));
}

int EthernetCaptureCollector::getSampleRate() const
{
  return sample_rate_hz_.load();
}

double EthernetCaptureCollector::getActualRate() const
{
  return actual_rate_hz_.load();
}

std::string EthernetCaptureCollector::getLastError() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return last_error_;
}

TdlasData EthernetCaptureCollector::getLatestData() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return latest_data_;
}

std::vector<TdlasAdapterInfo> EthernetCaptureCollector::listAvailableAdapters()
{
  std::vector<TdlasAdapterInfo> adapters;
#if defined(_WIN32) && defined(ENABLE_TDLAS_CAPTURE) && ENABLE_TDLAS_CAPTURE
  std::string runtime_error;
  if (!isRuntimeAvailable(&runtime_error))
  {
    return adapters;
  }

  PcapRuntime& runtime = PcapRuntime::instance();
  char errbuf[PCAP_ERRBUF_SIZE] = {0};
  pcap_if_t* devices = nullptr;
  if (runtime.findalldevs()(&devices, errbuf) == -1)
  {
    return adapters;
  }

  for (pcap_if_t* dev = devices; dev != nullptr; dev = dev->next)
  {
    TdlasAdapterInfo info;
    info.name = dev->name ? dev->name : "";
    info.description = dev->description ? dev->description : "";
    info.display_name = info.description.empty() ? info.name : (info.description + " (" + info.name + ")");
    adapters.push_back(info);
  }
  runtime.freealldevs()(devices);
#endif
  return adapters;
}

bool EthernetCaptureCollector::isRuntimeAvailable(std::string* error_message)
{
#if defined(_WIN32) && defined(ENABLE_TDLAS_CAPTURE) && ENABLE_TDLAS_CAPTURE
  return PcapRuntime::instance().isLoaded(error_message);
#else
  if (error_message)
  {
    *error_message = "TDLAS Ethernet capture was not compiled into this build";
  }
  return false;
#endif
}

bool EthernetCaptureCollector::isFeatureCompiled()
{
#if defined(_WIN32) && defined(ENABLE_TDLAS_CAPTURE) && ENABLE_TDLAS_CAPTURE
  return true;
#else
  return false;
#endif
}

void EthernetCaptureCollector::log(const std::string& message) const
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

bool EthernetCaptureCollector::shouldEmitData() const
{
  const int rate = (std::max)(1, sample_rate_hz_.load());
  const auto interval = std::chrono::milliseconds((std::max)(2, 1000 / rate));
  return std::chrono::steady_clock::now() - last_emit_time_ >= interval;
}

void EthernetCaptureCollector::updateLastEmitTime()
{
  last_emit_time_ = std::chrono::steady_clock::now();
}

void EthernetCaptureCollector::run()
{
#if defined(_WIN32) && defined(ENABLE_TDLAS_CAPTURE) && ENABLE_TDLAS_CAPTURE
  PcapRuntime& runtime = PcapRuntime::instance();
  char errbuf[PCAP_ERRBUF_SIZE] = {0};
  pcap_t* handle = runtime.openlive()(config_.adapter_name.c_str(), kCaptureSize, 1, kReadTimeoutMs, errbuf);
  if (!handle)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    last_error_ = errbuf[0] ? errbuf : "Failed to open capture adapter";
    latest_data_.capture_session_active = false;
    latest_data_.error_message = last_error_;
    running_.store(false);
    return;
  }

  log("TDLAS capture started on adapter: " + config_.adapter_name);

  uint64_t total_packets = 0;
  uint64_t matched_packets = 0;
  uint64_t non_ipv4_packets = 0;
  uint64_t non_udp_packets = 0;
  uint64_t mismatch_packets = 0;
  uint64_t parse_success_count = 0;
  uint64_t parse_failure_count = 0;
  uint64_t total_window_packets = 0;
  uint64_t matched_window_packets = 0;
  auto rate_window_start = std::chrono::steady_clock::now();

  while (running_.load())
  {
    CancelCallback cancel_callback;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      cancel_callback = cancel_callback_;
    }
    if (cancel_callback && cancel_callback())
    {
      break;
    }

    struct pcap_pkthdr* header = nullptr;
    const u_char* packet_data = nullptr;
    const int result = runtime.nextex()(handle, &header, &packet_data);

    const auto now = std::chrono::steady_clock::now();
    const auto window_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - rate_window_start).count();
    if (window_ms >= 1000)
    {
      const double elapsed = (std::max)(0.001, window_ms / 1000.0);
      actual_rate_hz_.store(matched_window_packets / elapsed);
      const double total_rate = total_window_packets / elapsed;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_data_.total_rate_hz = total_rate;
        latest_data_.matched_rate_hz = actual_rate_hz_.load();
      }
      total_window_packets = 0;
      matched_window_packets = 0;
      rate_window_start = now;
    }

    if (result == 0)
    {
      if (shouldEmitData())
      {
        DataCallback callback;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          latest_data_.capture_session_active = true;
          latest_data_.matched = false;
          latest_data_.valid = false;
          if (latest_data_.matched_packets == 0)
          {
            latest_data_.error_message = "Waiting for matching traffic";
          }
          latest_data_.timestamp = now;
          callback = data_callback_;
        }
        updateLastEmitTime();
        if (callback)
        {
          callback();
        }
      }
      continue;
    }

    if (result < 0 || !header || !packet_data)
    {
      std::lock_guard<std::mutex> lock(mutex_);
      last_error_ = "Packet capture read failed";
      latest_data_.capture_session_active = false;
      latest_data_.valid = false;
      latest_data_.error_message = last_error_;
      break;
    }

    ++total_packets;
    ++total_window_packets;

    ParsedPacket parsed;
    if (!parsePacket(packet_data, header->caplen, &parsed))
    {
      ++parse_failure_count;
      continue;
    }

    if (!parsed.is_ipv4)
    {
      ++non_ipv4_packets;
    }
    else if (!parsed.is_udp)
    {
      ++non_udp_packets;
    }
    else if (!packetMatchesConfig(parsed, config_))
    {
      ++mismatch_packets;
    }
    else
    {
      ++matched_packets;
      ++matched_window_packets;

      bool any_metrics = false;
      std::vector<TdlasMetric> metrics = parseBusinessMetrics(parsed.payload, parsed.payload_length, &any_metrics);
      if (any_metrics)
      {
        ++parse_success_count;
      }
      else
      {
        ++parse_failure_count;
      }

      TdlasData sample;
      sample.adapter_name = config_.adapter_name;
      sample.headers = parsed.headers;
      sample.payload_hex = summarizePayloadHex(parsed.payload, parsed.payload_length);
      sample.metrics = std::move(metrics);
      sample.metrics_json = metricsToJson(sample.metrics);
      sample.packet_length = static_cast<uint32_t>(header->caplen);
      sample.total_packets = total_packets;
      sample.matched_packets = matched_packets;
      sample.non_ipv4_packets = non_ipv4_packets;
      sample.non_udp_packets = non_udp_packets;
      sample.filter_mismatch_packets = mismatch_packets;
      sample.parse_success_count = parse_success_count;
      sample.parse_failure_count = parse_failure_count;
      sample.total_rate_hz = latest_data_.total_rate_hz;
      sample.matched_rate_hz = actual_rate_hz_.load();
      sample.capture_session_active = true;
      sample.matched = true;
      sample.valid = any_metrics;
      sample.mapping_unverified = true;
      sample.timestamp = now;
      sample.last_match_time_utc = formatUtcNow();
      sample.error_message = any_metrics ? std::string() : "Payload observed but business mapping is still unverified";
      TdlasMetricSample trendSample;
      trendSample.timestamp_utc = sample.last_match_time_utc;
      trendSample.metrics = sample.metrics;

      DataCallback callback;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        recent_metric_samples_.push_back(std::move(trendSample));
        while (recent_metric_samples_.size() > kTrendSampleHistory)
        {
          recent_metric_samples_.pop_front();
        }
        sample.recent_metric_samples.assign(recent_metric_samples_.begin(), recent_metric_samples_.end());
        latest_data_ = sample;
        callback = data_callback_;
      }

      if (shouldEmitData())
      {
        updateLastEmitTime();
        if (callback)
        {
          callback();
        }
      }
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      latest_data_.total_packets = total_packets;
      latest_data_.matched_packets = matched_packets;
      latest_data_.non_ipv4_packets = non_ipv4_packets;
      latest_data_.non_udp_packets = non_udp_packets;
      latest_data_.filter_mismatch_packets = mismatch_packets;
      latest_data_.parse_success_count = parse_success_count;
      latest_data_.parse_failure_count = parse_failure_count;
      latest_data_.capture_session_active = true;
      latest_data_.timestamp = now;
    }
  }

  runtime.close()(handle);
  running_.store(false);
  std::lock_guard<std::mutex> lock(mutex_);
  latest_data_.capture_session_active = false;
  if (latest_data_.error_message.empty())
  {
    latest_data_.error_message = "Disconnected";
  }
#endif
}

}
