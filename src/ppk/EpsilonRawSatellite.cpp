#include "EpsilonRawSatellite.h"

#include <cmath>
#include <cstring>
#include <set>
#include <tuple>
#include <utility>

namespace VaporView::Ppk
{
namespace
{
template <typename T> T little(const std::uint8_t *p)
{
    std::uint8_t bytes[sizeof(T)];
    const std::uint16_t endian = 1;
    const bool nativeLittle = *reinterpret_cast<const std::uint8_t *>(&endian) == 1;
    for (std::size_t i = 0; i < sizeof(T); ++i)
        bytes[i] = p[nativeLittle ? i : sizeof(T) - 1 - i];
    T value;
    std::memcpy(&value, bytes, sizeof(T));
    return value;
}

std::uint8_t crc8(const std::uint8_t *p, std::size_t n)
{
    std::uint8_t crc = 0;
    while (n--)
    {
        crc ^= *p++;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1) ? (crc >> 1) ^ 0x8C : crc >> 1;
    }
    return crc;
}

std::uint16_t crc16(const std::uint8_t *p, std::size_t n)
{
    std::uint16_t crc = 0;
    while (n--)
    {
        crc ^= static_cast<std::uint16_t>(*p++) << 8;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
}

RawSatelliteObservation observation(const std::uint8_t *sat, const std::uint8_t *freq)
{
    RawSatelliteObservation o;
    o.system = sat[0];
    o.prn = sat[1];
    o.elevationDeg = sat[2];
    o.azimuthDeg = little<std::uint16_t>(sat + 3);
    o.frequency = freq[0];
    o.trackingStatus = freq[1];
    o.carrierPhaseCycles = little<double>(freq + 2);
    o.pseudoRangeM = little<double>(freq + 10);
    o.dopplerHz = little<float>(freq + 18);
    o.snrDbHz = little<float>(freq + 22);
    return o;
}
} // namespace

std::uint64_t RawSatelliteEpoch::utcNanoseconds() const
{
    return static_cast<std::uint64_t>(unixSeconds) * 1000000000ULL + nanoseconds;
}

bool decodeRawSatellitePayload(const std::uint8_t *p, std::size_t n, RawSatellitePacket &result, std::string &reason)
{
    auto fail = [&](const char *code)
    {
        reason = code;
        return false;
    };
    if (!p || n < 16)
        return fail("TRUNCATED_HEADER");
    RawSatellitePacket next;
    next.epoch.unixSeconds = little<std::uint32_t>(p);
    next.epoch.nanoseconds = little<std::uint32_t>(p + 4);
    next.epoch.receiverClockOffsetUs = little<std::int32_t>(p + 8);
    next.epoch.receiver = p[12];
    next.packetNumber = p[13];
    next.totalPackets = p[14];
    if (next.epoch.nanoseconds >= 1000000000 || next.epoch.unixSeconds == 0)
        return fail("INVALID_UTC");
    if (next.totalPackets == 0 || next.packetNumber > next.totalPackets)
        return fail("INVALID_PACKET_NUMBER");

    // The vendor packed C fixture repeats a 32-byte satellite/frequency record.
    // The manual's frequency count also permits a satellite header followed by
    // multiple 26-byte frequency records. Accept only an exactly sized layout.
    std::size_t offset = 16;
    if (n == 16 + static_cast<std::size_t>(p[15]) * 32)
    {
        for (int i = 0; i < p[15]; ++i, offset += 32)
        {
            if (p[offset + 5] == 0)
                return fail("INVALID_FREQUENCY_COUNT");
            next.epoch.observations.push_back(observation(p + offset, p + offset + 6));
        }
    }
    else
    {
        for (int i = 0; i < p[15]; ++i)
        {
            if (offset + 6 > n)
                return fail("TRUNCATED_SATELLITE");
            const auto *sat = p + offset;
            const int count = sat[5];
            if (count == 0)
                return fail("INVALID_FREQUENCY_COUNT");
            offset += 6;
            for (int j = 0; j < count; ++j, offset += 26)
            {
                if (offset + 26 > n)
                    return fail("TRUNCATED_FREQUENCY");
                next.epoch.observations.push_back(observation(sat, p + offset));
            }
        }
    }
    if (offset != n)
        return fail("PAYLOAD_SIZE_MISMATCH");
    std::set<std::tuple<int, int, int>> keys;
    for (const auto &o : next.epoch.observations)
    {
        if (!std::isfinite(o.carrierPhaseCycles) || !std::isfinite(o.pseudoRangeM) || !std::isfinite(o.dopplerHz) ||
            !std::isfinite(o.snrDbHz))
            return fail("NONFINITE_OBSERVATION");
        if (!keys.emplace(o.system, o.prn, o.frequency).second)
            return fail("DUPLICATE_OBSERVATION");
    }
    result = std::move(next);
    reason.clear();
    return true;
}

bool decodeRawSatelliteFrame(const std::uint8_t *p, std::size_t n, RawSatellitePacket &result, std::string &reason)
{
    if (!p || n < 24 || p[0] != 0xFC || p[1] != kMsgRawSatellite || n != static_cast<std::size_t>(p[2]) + 8 ||
        p[n - 1] != 0xFD)
    {
        reason = "INVALID_FRAME";
        return false;
    }
    if (crc8(p, 4) != p[4] || crc16(p + 7, p[2]) != (p[5] << 8 | p[6]))
    {
        reason = "CRC_MISMATCH";
        return false;
    }
    return decodeRawSatellitePayload(p + 7, p[2], result, reason);
}

RawSatelliteAssembler::RawSatelliteAssembler(EventCallback events) : events_(std::move(events))
{
}

std::vector<std::uint8_t> encodeRawSatelliteFrame(const RawSatellitePacket &packet, std::uint8_t serial)
{
    if (packet.epoch.observations.size() > 7)
        return {};
    const auto size = 16 + 32 * packet.epoch.observations.size();
    std::vector<std::uint8_t> frame(size + 8, 0);
    frame[0] = 0xFC;
    frame[1] = kMsgRawSatellite;
    frame[2] = static_cast<std::uint8_t>(size);
    frame[3] = serial;
    frame.back() = 0xFD;
    auto put = [&](std::size_t offset, auto value)
    {
        std::uint8_t bytes[sizeof(value)];
        std::memcpy(bytes, &value, sizeof(value));
        const std::uint16_t endian = 1;
        const bool le = *reinterpret_cast<const std::uint8_t *>(&endian) == 1;
        for (std::size_t i = 0; i < sizeof(value); ++i)
            frame[7 + offset + i] = bytes[le ? i : sizeof(value) - 1 - i];
    };
    put(0, packet.epoch.unixSeconds);
    put(4, packet.epoch.nanoseconds);
    put(8, packet.epoch.receiverClockOffsetUs);
    frame[19] = packet.epoch.receiver;
    frame[20] = packet.packetNumber;
    frame[21] = packet.totalPackets;
    frame[22] = static_cast<std::uint8_t>(packet.epoch.observations.size());
    std::size_t offset = 16;
    for (const auto &observation : packet.epoch.observations)
    {
        frame[7 + offset] = observation.system;
        frame[8 + offset] = observation.prn;
        frame[9 + offset] = observation.elevationDeg;
        put(offset + 3, observation.azimuthDeg);
        frame[12 + offset] = 1;
        frame[13 + offset] = observation.frequency;
        frame[14 + offset] = observation.trackingStatus;
        put(offset + 8, observation.carrierPhaseCycles);
        put(offset + 16, observation.pseudoRangeM);
        put(offset + 24, observation.dopplerHz);
        put(offset + 28, observation.snrDbHz);
        offset += 32;
    }
    frame[4] = crc8(frame.data(), 4);
    const auto checksum = crc16(frame.data() + 7, size);
    frame[5] = checksum >> 8;
    frame[6] = checksum & 255;
    return frame;
}

std::vector<std::uint8_t> encodeFdilinkFrame(std::uint8_t id, const std::vector<std::uint8_t> &payload,
                                             std::uint8_t serial)
{
    if (payload.size() > 255)
        return {};
    std::vector<std::uint8_t> frame{0xFC, id, static_cast<std::uint8_t>(payload.size()), serial, 0, 0, 0};
    frame.insert(frame.end(), payload.begin(), payload.end());
    frame.push_back(0xFD);
    frame[4] = crc8(frame.data(), 4);
    const auto checksum = crc16(payload.data(), payload.size());
    frame[5] = checksum >> 8;
    frame[6] = checksum & 255;
    return frame;
}

void RawSatelliteAssembler::reportEpochEvent(const char *name, const char *reason, const Pending &p) const
{
    if (events_)
        events_(name, reason, p.epoch.receiver, p.epoch.utcNanoseconds(), static_cast<int>(p.packets.size()), p.total);
}

std::optional<RawSatelliteEpoch> RawSatelliteAssembler::accept(RawSatellitePacket packet)
{
    auto &p = pending_[packet.epoch.receiver];
    const auto utc = packet.epoch.utcNanoseconds();
    if (p.total && utc < p.epoch.utcNanoseconds())
    {
        reportEpochEvent("epsilon_raw_satellite_time_regression", "UTC_REGRESSION", p);
        return {};
    }
    if (!p.total || utc != p.epoch.utcNanoseconds())
    {
        if (p.total && !p.completed)
            reportEpochEvent("epsilon_raw_satellite_epoch_incomplete", "MISSING_PACKETS", p);
        p = Pending{};
        p.epoch = packet.epoch;
        p.epoch.observations.clear();
        p.total = packet.totalPackets;
    }
    if (p.completed || p.packets.count(packet.packetNumber))
    {
        reportEpochEvent("epsilon_raw_satellite_packet_duplicate", "DUPLICATE_PACKET", p);
        return {};
    }
    if (p.total != packet.totalPackets || p.epoch.receiverClockOffsetUs != packet.epoch.receiverClockOffsetUs)
    {
        reportEpochEvent("epsilon_raw_satellite_epoch_incomplete", "HEADER_CONFLICT", p);
        p.completed = true; // A conflicted epoch cannot later become trustworthy.
        return {};
    }
    p.packets.emplace(packet.packetNumber, std::move(packet));
    if (p.packets.size() != p.total)
        return {};
    // Both numbering conventions are accepted only once an endpoint proves it:
    // 0..N-1 or 1..N. Interior packets alone never imply a complete epoch.
    const int first = p.packets.count(0) ? 0 : 1;
    for (int i = first; i < first + p.total; ++i)
        if (!p.packets.count(static_cast<std::uint8_t>(i)))
            return {};
    std::set<std::tuple<int, int, int>> keys;
    for (const auto &entry : p.packets)
        for (const auto &o : entry.second.epoch.observations)
        {
            if (!keys.emplace(o.system, o.prn, o.frequency).second)
            {
                reportEpochEvent("epsilon_raw_satellite_epoch_incomplete", "OBSERVATION_CONFLICT", p);
                p.completed = true;
                return {};
            }
            p.epoch.observations.push_back(o);
        }
    p.completed = true;
    reportEpochEvent("epsilon_raw_satellite_epoch_completed", "COMPLETE", p);
    return p.epoch;
}

void RawSatelliteAssembler::flush()
{
    for (const auto &entry : pending_)
        if (!entry.second.completed)
            reportEpochEvent("epsilon_raw_satellite_epoch_incomplete", "STREAM_ENDED", entry.second);
    pending_.clear();
}
} // namespace VaporView::Ppk
