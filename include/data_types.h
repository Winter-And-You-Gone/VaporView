#ifndef VaporView_DATA_TYPES_H
#define VaporView_DATA_TYPES_H

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

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

struct PacketEndpoint
{
  std::string mac;
  std::string ip;
  uint16_t port = 0;
  bool valid = false;
};

struct EthernetHeaderData
{
  PacketEndpoint source;
  PacketEndpoint destination;
  uint16_t ether_type = 0;
  bool valid = false;
};

struct IPv4HeaderData
{
  uint8_t version = 0;
  uint8_t ihl = 0;
  uint8_t type_of_service = 0;
  uint16_t total_length = 0;
  uint16_t identification = 0;
  uint16_t flags = 0;
  uint16_t fragment_offset = 0;
  uint8_t ttl = 0;
  uint8_t protocol = 0;
  uint16_t header_checksum = 0;
  PacketEndpoint source;
  PacketEndpoint destination;
  bool valid = false;
};

struct UdpHeaderData
{
  uint16_t source_port = 0;
  uint16_t destination_port = 0;
  uint16_t length = 0;
  uint16_t checksum = 0;
  bool valid = false;
};

struct PacketHeaders
{
  EthernetHeaderData ethernet;
  IPv4HeaderData ipv4;
  UdpHeaderData udp;
};

struct TdlasMetric
{
  std::string key;
  std::string label_zh;
  std::string label_en;
  std::string unit;
  std::string wire_type;
  std::string raw_hex;
  double value = 0.0;
  uint32_t offset = 0;
  bool valid = false;
  std::string confidence = "unverified";
};

struct TdlasMetricSample
{
  std::string timestamp_utc;
  std::vector<TdlasMetric> metrics;
};

struct TdlasWordStat
{
  uint32_t word_index = 0;
  uint32_t offset = 0;
  std::string raw_hex;
  uint16_t latest_value = 0;
  uint16_t min_value = 0;
  uint16_t max_value = 0;
  uint32_t unique_count = 0;
  bool stable = false;
};

struct TdlasData
{
  std::string adapter_name;
  PacketHeaders headers;
  std::string payload_hex;
  std::string payload_signature;
  std::string payload_variation_summary;
  std::string metrics_json;
  std::string error_message;
  std::string last_match_time_utc;
  std::vector<TdlasMetric> metrics;
  std::vector<TdlasWordStat> word_stats;
  std::vector<TdlasMetricSample> recent_metric_samples;

  uint32_t packet_length = 0;
  uint64_t total_packets = 0;
  uint64_t matched_packets = 0;
  uint64_t non_ipv4_packets = 0;
  uint64_t non_udp_packets = 0;
  uint64_t filter_mismatch_packets = 0;
  uint64_t parse_success_count = 0;
  uint64_t parse_failure_count = 0;
  double total_rate_hz = 0.0;
  double matched_rate_hz = 0.0;

  std::chrono::steady_clock::time_point timestamp{};
  bool capture_session_active = false;
  bool matched = false;
  bool valid = false;
  bool mapping_unverified = true;
};

}

#endif

