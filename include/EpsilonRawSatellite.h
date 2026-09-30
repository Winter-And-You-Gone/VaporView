#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace VaporView::Ppk
{

inline constexpr std::uint8_t kMsgRawSatellite = 0x77;
inline constexpr int kDefaultObservationRateHz = 5;

struct RawSatelliteObservation
{
    std::uint8_t system = 0;
    std::uint8_t prn = 0;
    std::uint8_t elevationDeg = 0;
    std::uint16_t azimuthDeg = 0;
    std::uint8_t frequency = 0;
    std::uint8_t trackingStatus = 0;
    double carrierPhaseCycles = 0.0;
    double pseudoRangeM = 0.0;
    float dopplerHz = 0.0f;
    float snrDbHz = 0.0f;
};

struct RawSatelliteEpoch
{
    std::uint32_t unixSeconds = 0;
    std::uint32_t nanoseconds = 0;
    std::int32_t receiverClockOffsetUs = 0;
    std::uint8_t receiver = 0;
    std::uint64_t hostTimestampUs = 0;
    std::vector<RawSatelliteObservation> observations;
    std::uint64_t utcNanoseconds() const;
};

struct RawSatellitePacket
{
    RawSatelliteEpoch epoch;
    std::uint8_t packetNumber = 0;
    std::uint8_t totalPackets = 0;
};

// FDILink payloads are little endian; frames use CRC8(header) and CRC16(payload).
bool decodeRawSatellitePayload(const std::uint8_t *payload, std::size_t size, RawSatellitePacket &result,
                               std::string &reason);
bool decodeRawSatelliteFrame(const std::uint8_t *frame, std::size_t size, RawSatellitePacket &result,
                             std::string &reason);
// Vendor packed records, also used by the simulated device and replay fixtures.
std::vector<std::uint8_t> encodeRawSatelliteFrame(const RawSatellitePacket &packet, std::uint8_t serial);
std::vector<std::uint8_t> encodeFdilinkFrame(std::uint8_t packetId, const std::vector<std::uint8_t> &payload,
                                             std::uint8_t serial);

class RawSatelliteAssembler final
{
  public:
    using EventCallback = std::function<void(const std::string &event, const std::string &reason, std::uint8_t receiver,
                                             std::uint64_t utcNs, int received, int expected)>;
    explicit RawSatelliteAssembler(EventCallback events = {});
    std::optional<RawSatelliteEpoch> accept(RawSatellitePacket packet);
    void flush();

  private:
    struct Pending
    {
        RawSatelliteEpoch epoch;
        std::uint8_t total = 0;
        bool completed = false;
        std::map<std::uint8_t, RawSatellitePacket> packets;
    };
    void reportEpochEvent(const char *name, const char *reason, const Pending &pending) const;
    std::map<std::uint8_t, Pending> pending_;
    EventCallback events_;
};

} // namespace VaporView::Ppk
