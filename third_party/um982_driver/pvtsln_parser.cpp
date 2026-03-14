#include "pvtsln_data.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace unicore_um982_driver
{
  namespace
  {

    std::string trim(const std::string &input)
    {
      const auto first = input.find_first_not_of(" \t\r\n");
      if (first == std::string::npos)
      {
        return {};
      }
      const auto last = input.find_last_not_of(" \t\r\n");
      return input.substr(first, last - first + 1);
    }

    std::vector<std::string> splitString(const std::string &str, char delimiter)
    {
      std::vector<std::string> tokens;
      std::stringstream ss(str);
      std::string token;

      while (std::getline(ss, token, delimiter))
      {
        tokens.push_back(trim(token));
      }

      return tokens;
    }

    double parseDouble(const std::string &value)
    {
      const std::string trimmed = trim(value);
      if (trimmed.empty())
      {
        return 0.0;
      }
      return std::stod(trimmed);
    }

    int32_t parseInt32(const std::string &value)
    {
      const std::string trimmed = trim(value);
      if (trimmed.empty())
      {
        return 0;
      }
      return static_cast<int32_t>(std::stol(trimmed, nullptr, 10));
    }

    uint32_t parseUInt32(const std::string &value)
    {
      std::string trimmed = trim(value);
      if (trimmed.empty())
      {
        return 0U;
      }

      int base = 10;
      if (trimmed.rfind("0x", 0) == 0 || trimmed.rfind("0X", 0) == 0)
      {
        trimmed = trimmed.substr(2);
        base = 16;
      }
      else if (trimmed.find_first_of("ABCDEFabcdef") != std::string::npos)
      {
        base = 16;
      }

      return static_cast<uint32_t>(std::stoul(trimmed, nullptr, base));
    }

    uint16_t parseUInt16(const std::string &value)
    {
      const std::string trimmed = trim(value);
      if (trimmed.empty())
      {
        return 0U;
      }
      return static_cast<uint16_t>(std::stoul(trimmed, nullptr, 10));
    }

    uint8_t parseUInt8(const std::string &value)
    {
      const std::string trimmed = trim(value);
      if (trimmed.empty())
      {
        return 0U;
      }
      return static_cast<uint8_t>(std::stoul(trimmed, nullptr, 10));
    }

    constexpr size_t kBaseBodyFields = 34;                      // Fields 2-35 in the table before PRN count
    constexpr size_t kMaxPrnEntries = 41;                       // Table shows PRN_list[41]
    constexpr size_t kRequiredBodyFields = kBaseBodyFields + 1; // base fields + PRN count

    bool isNumeric(const std::string &text)
    {
      return !text.empty() &&
             std::all_of(text.begin(), text.end(), [](unsigned char ch)
                         { return std::isdigit(ch); });
    }

    std::string mapPositionVelocityType(const std::string &raw)
    {
      const std::string trimmed = trim(raw);
      if (!isNumeric(trimmed))
      {
        return trimmed;
      }

      int code = std::stoi(trimmed);
      switch (code)
      {
      case 0:
        return "NONE";
      case 1:
        return "FIXEDPOS";
      case 2:
        return "FIXEDHEIGHT";
      case 8:
        return "DOPPLER_VELOCITY";
      case 16:
        return "SINGLE";
      case 17:
        return "PSRDIFF";
      case 18:
        return "SBAS";
      case 32:
        return "L1_FLOAT";
      case 33:
        return "IONOFREE_FLOAT";
      case 34:
        return "NARROW_FLOAT";
      case 48:
        return "L1_INT";
      case 49:
        return "WIDE_INT";
      case 50:
        return "NARROW_INT";
      case 52:
        return "INS";
      case 53:
        return "INS_PSRSP";
      case 54:
        return "INS_PSRDIFF";
      case 55:
        return "INS_RTKFLOAT";
      case 56:
        return "INS_RTKFIXED";
      case 68:
        return "PPP_CONVERGING";
      case 69:
        return "PPP";
      default:
        return trimmed; // Unknown code, keep numeric string
      }
    }

    std::string mapSolutionStatus(const std::string &raw)
    {
      const std::string trimmed = trim(raw);
      if (!isNumeric(trimmed))
      {
        return trimmed;
      }

      int code = std::stoi(trimmed);
      switch (code)
      {
      case 0:
        return "SOL_COMPUTED";
      case 1:
        return "INSUFFICIENT_OBS";
      case 2:
        return "NO_CONVERGENCE";
      case 4:
        return "COV_TRACE";
      default:
        return trimmed;
      }
    }

  } // namespace

  std::ostream &operator<<(std::ostream &os, const PVTSLNData &data)
  {
    os << "PVTSLNData{"
       << "message_id=" << data.message_id
       << ", sequence_number=" << data.sequence_number
       << ", cpu_idle=" << static_cast<unsigned>(data.cpu_idle)
       << ", port_id=" << data.port_id
       << ", time_ref=" << data.time_ref
       << ", time_status=" << data.time_status
       << ", week=" << data.week
       << ", time_of_week=" << data.time_of_week
       << ", format_version=" << data.format_version
       << ", receiver_status=" << data.receiver_status
       << ", reserved=" << data.reserved
       << ", solution_status=" << data.solution_status
       << ", position_type=" << data.position_type
       << ", leap_seconds=" << data.leap_seconds
       << ", output_delay=" << data.output_delay
       << ", bestpos_type=" << data.bestpos_type
       << ", bestpos_height=" << data.bestpos_height
       << ", bestpos_latitude=" << data.bestpos_latitude
       << ", bestpos_longitude=" << data.bestpos_longitude
       << ", bestpos_height_std=" << data.bestpos_height_std
       << ", bestpos_latitude_std=" << data.bestpos_latitude_std
       << ", bestpos_longitude_std=" << data.bestpos_longitude_std
       << ", bestpos_diff_age=" << data.bestpos_diff_age
       << ", age_of_corrections=" << data.age_of_corrections
       << ", psrpos_type=" << data.psrpos_type
       << ", psrpos_height=" << data.psrpos_height
       << ", psrpos_latitude=" << data.psrpos_latitude
       << ", psrpos_longitude=" << data.psrpos_longitude
       << ", undulation=" << data.undulation
       << ", bestpos_svs=" << static_cast<unsigned>(data.bestpos_svs)
       << ", bestpos_solnsvs=" << static_cast<unsigned>(data.bestpos_solnsvs)
       << ", psrpos_svs=" << static_cast<unsigned>(data.psrpos_svs)
       << ", psrpos_solnsvs=" << static_cast<unsigned>(data.psrpos_solnsvs)
       << ", psrvel_north=" << data.psrvel_north
       << ", psrvel_east=" << data.psrvel_east
       << ", psrvel_ground=" << data.psrvel_ground
       << ", heading_type=" << data.heading_type
       << ", heading_length=" << data.heading_length
       << ", heading_degree=" << data.heading_degree
       << ", heading_pitch=" << data.heading_pitch
       << ", heading_trackedsvs=" << static_cast<unsigned>(data.heading_trackedsvs)
       << ", heading_solnsvs=" << static_cast<unsigned>(data.heading_solnsvs)
       << ", heading_ggl1=" << static_cast<unsigned>(data.heading_ggl1)
       << ", heading_ggl1l2=" << static_cast<unsigned>(data.heading_ggl1l2)
       << ", gdop=" << data.gdop
       << ", pdop=" << data.pdop
       << ", hdop=" << data.hdop
       << ", htdop=" << data.htdop
       << ", tdop=" << data.tdop
       << ", elevation_cutoff=" << data.elevation_cutoff
       << ", prn_count=" << data.prn_count
       << ", prn_list=[";

    for (size_t i = 0; i < data.prn_list.size(); ++i)
    {
      if (i > 0)
      {
        os << ",";
      }
      os << data.prn_list[i];
    }

    os << "], crc=" << data.crc
       << ", position_status=" << data.position_status
       << ", latitude=" << data.latitude
       << ", longitude=" << data.longitude
       << ", altitude=" << data.altitude
       << ", num_satellites_tracked=" << data.num_satellites_tracked
       << ", num_satellites_used=" << data.num_satellites_used
       << ", sigma_latitude=" << data.sigma_latitude
       << ", sigma_longitude=" << data.sigma_longitude
       << ", sigma_altitude=" << data.sigma_altitude
       << ", heading=" << data.heading
       << ", velocity_north=" << data.velocity_north
       << ", velocity_east=" << data.velocity_east
       << ", velocity_up=" << data.velocity_up
       << ", timestamp=" << data.timestamp
       << ", is_valid=" << std::boolalpha << data.is_valid
       << "}";
    return os;
  }

  bool parsePVTSLN(const std::string &line, PVTSLNData &data, std::string *error_message)
  {
    data = PVTSLNData();
    data.is_valid = false;

    auto setError = [&](const std::string &message)
    {
      if (error_message)
      {
        *error_message = message;
      }
      std::cerr << message << std::endl;
    };

    if (line.find("PVTSLN") == std::string::npos)
    {
      return false;
    }

    try
    {
      std::string msg = trim(line);

      // Remove checksum if present and store it
      size_t checksum_pos = msg.find('*');
      if (checksum_pos != std::string::npos)
      {
        std::string checksum_str = msg.substr(checksum_pos + 1);
        data.crc = parseUInt32(checksum_str);
        msg = msg.substr(0, checksum_pos);
      }
      else
      {
        data.crc = 0U;
      }

      // Remove leading # if present
      if (!msg.empty() && msg.front() == '#')
      {
        msg.erase(0, 1);
      }

      // Split the message into header and body parts
      size_t semicolon_pos = msg.find(';');
      if (semicolon_pos == std::string::npos)
      {
        setError("No semicolon found in PVTSLN message");
        return false;
      }

      std::string header_part = msg.substr(0, semicolon_pos);
      std::string body_part = msg.substr(semicolon_pos + 1);

      // Parse header part (comma-separated)
      std::vector<std::string> header_fields = splitString(header_part, ',');
      if (header_fields.size() < 10)
      {
        setError("Insufficient header fields in PVTSLN message");
        return false;
      }

      data.message_id = header_fields[0];

      // Table 7-50 ASCII header layout:
      // 0: Message, 1: CPUIdle, 2: TimeRef, 3: TimeStatus, 4: Wn, 5: Ms,
      // 6: Version, 7: Reserved, 8: Leap sec, 9: Output Delay
      // Fall back to legacy layout if more fields are provided.
      data.cpu_idle = parseUInt8(header_fields[1]);
      data.sequence_number = data.cpu_idle; // legacy compatibility
      data.time_ref = header_fields[2];
      data.port_id = data.time_ref; // legacy compatibility
      data.time_status = header_fields[3];
      data.week = parseInt32(header_fields[4]);
      data.time_of_week = parseDouble(header_fields[5]);
      data.format_version = parseUInt32(header_fields[6]);
      data.receiver_status = data.format_version; // legacy compatibility
      data.reserved = parseUInt32(header_fields[7]);
      data.leap_seconds = parseInt32(header_fields[8]);
      data.solution_status = data.leap_seconds; // legacy compatibility

      data.output_delay = parseUInt16(header_fields[9]);
      data.position_type = static_cast<int32_t>(data.output_delay); // legacy compatibility

      // Parse body part (comma-separated)
      std::vector<std::string> body_fields = splitString(body_part, ',');
      if (body_fields.size() < kRequiredBodyFields)
      {
        setError("Insufficient body fields in PVTSLN message, got " + std::to_string(body_fields.size()));
        return false;
      }

      size_t field_idx = 0;

      data.bestpos_type = mapPositionVelocityType(body_fields[field_idx++]); // 2
      data.bestpos_height = parseDouble(body_fields[field_idx++]);           // 3
      data.bestpos_latitude = parseDouble(body_fields[field_idx++]);         // 4
      data.bestpos_longitude = parseDouble(body_fields[field_idx++]);        // 5
      data.bestpos_height_std = parseDouble(body_fields[field_idx++]);       // 6
      data.bestpos_latitude_std = parseDouble(body_fields[field_idx++]);     // 7
      data.bestpos_longitude_std = parseDouble(body_fields[field_idx++]);    // 8
      data.bestpos_diff_age = parseDouble(body_fields[field_idx++]);         // 9
      data.age_of_corrections = data.bestpos_diff_age;

      data.psrpos_type = mapPositionVelocityType(body_fields[field_idx++]); // 10
      data.psrpos_height = parseDouble(body_fields[field_idx++]);           // 11
      data.psrpos_latitude = parseDouble(body_fields[field_idx++]);         // 12
      data.psrpos_longitude = parseDouble(body_fields[field_idx++]);        // 13

      data.undulation = parseDouble(body_fields[field_idx++]); // 14

      data.bestpos_svs = parseUInt8(body_fields[field_idx++]);     // 15
      data.bestpos_solnsvs = parseUInt8(body_fields[field_idx++]); // 16
      data.psrpos_svs = parseUInt8(body_fields[field_idx++]);      // 17
      data.psrpos_solnsvs = parseUInt8(body_fields[field_idx++]);  // 18

      data.psrvel_north = parseDouble(body_fields[field_idx++]);  // 19
      data.psrvel_east = parseDouble(body_fields[field_idx++]);   // 20
      data.psrvel_ground = parseDouble(body_fields[field_idx++]); // 21

      data.heading_type = mapSolutionStatus(body_fields[field_idx++]); // 22
      data.heading_length = parseDouble(body_fields[field_idx++]);     // 23
      data.heading_degree = parseDouble(body_fields[field_idx++]);     // 24
      data.heading_pitch = parseDouble(body_fields[field_idx++]);      // 25

      data.heading_trackedsvs = parseUInt8(body_fields[field_idx++]); // 26
      data.heading_solnsvs = parseUInt8(body_fields[field_idx++]);    // 27
      data.heading_ggl1 = parseUInt8(body_fields[field_idx++]);       // 28
      data.heading_ggl1l2 = parseUInt8(body_fields[field_idx++]);     // 29

      data.gdop = parseDouble(body_fields[field_idx++]);             // 30
      data.pdop = parseDouble(body_fields[field_idx++]);             // 31
      data.hdop = parseDouble(body_fields[field_idx++]);             // 32
      data.htdop = parseDouble(body_fields[field_idx++]);            // 33
      data.tdop = parseDouble(body_fields[field_idx++]);             // 34
      data.elevation_cutoff = parseDouble(body_fields[field_idx++]); // 35

      data.prn_count = parseUInt16(body_fields[field_idx++]); // 36

      const size_t expected_prn_fields = std::min(kMaxPrnEntries,
                                                  static_cast<size_t>(data.prn_count));
      if (body_fields.size() < kRequiredBodyFields + expected_prn_fields)
      {
        setError("PVTSLN body missing PRN entries, expected at least " +
                 std::to_string(expected_prn_fields) + " got " +
                 std::to_string(body_fields.size() - kRequiredBodyFields));
        return false;
      }

      data.prn_list.clear();
      const size_t remaining_fields = body_fields.size() - field_idx;
      const size_t prn_to_read = std::min({static_cast<size_t>(data.prn_count),
                                           remaining_fields,
                                           kMaxPrnEntries});
      data.prn_list.reserve(prn_to_read);
      for (size_t i = 0; i < prn_to_read; ++i)
      {
        data.prn_list.push_back(parseUInt16(body_fields[field_idx++]));
      }

      // Populate convenience mirrors used by rest of the driver
      data.position_status = data.bestpos_type;
      data.latitude = data.bestpos_latitude;
      data.longitude = data.bestpos_longitude;
      data.altitude = data.bestpos_height;
      data.num_satellites_tracked = data.bestpos_svs;
      data.num_satellites_used = data.bestpos_solnsvs;
      data.sigma_latitude = data.bestpos_latitude_std;
      data.sigma_longitude = data.bestpos_longitude_std;
      data.sigma_altitude = data.bestpos_height_std;
      data.heading = data.heading_degree;
      data.velocity_north = data.psrvel_north;
      data.velocity_east = data.psrvel_east;
      data.velocity_up = 0.0; // PVTSLN does not report vertical velocity
      data.timestamp = data.time_of_week;

      data.is_valid = true;
      if (error_message)
      {
        error_message->clear();
      }
      // std::cout << data << std::endl;
      return true;
    }
    catch (const std::exception &e)
    {
      setError(std::string("Error parsing PVTSLN message: ") + e.what());
      data.is_valid = false;
      return false;
    }
  }

} // namespace unicore_um982_driver
