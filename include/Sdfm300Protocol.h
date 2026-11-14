#pragma once

#include "data_types.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace VaporView::Sdfm300
{
inline constexpr int kDefaultBaudRate = 460800;
inline constexpr uint8_t kHeader = 0x5c;
inline constexpr size_t kFrameSize = 4;
inline constexpr uint16_t kMinDistanceCm = 5;
inline constexpr uint16_t kMaxDistanceCm = 30000;
inline constexpr uint16_t kNoTarget = 0xffff;
inline constexpr uint16_t kRecordType = 6;

inline bool decodeFrame(const uint8_t* frame, size_t size, LidarData& sample)
{
    if (!frame || size != kFrameSize || frame[0] != kHeader ||
        frame[3] != static_cast<uint8_t>(~static_cast<uint8_t>(frame[1] + frame[2])))
    {
        return false;
    }
    const uint16_t distanceCm = uint16_t(frame[1]) | (uint16_t(frame[2]) << 8);
    sample = LidarData{};
    sample.timestamp = std::chrono::steady_clock::now();
    sample.valid = distanceCm >= kMinDistanceCm && distanceCm <= kMaxDistanceCm;
    sample.distance_m = sample.valid ? distanceCm / 100.0 : 0.0;
    sample.error_message = sample.valid ? "" : "SDFM300 未检测到目标或超出 0.05–300 m 量程";
    return true;
}

inline bool extractSample(std::vector<uint8_t>& buffer, LidarData& sample,
                          std::vector<uint8_t>* rawFrame = nullptr)
{
    while (!buffer.empty())
    {
        const auto header = std::find(buffer.begin(), buffer.end(), kHeader);
        buffer.erase(buffer.begin(), header);
        if (buffer.size() < kFrameSize)
        {
            return false;
        }
        if (decodeFrame(buffer.data(), kFrameSize, sample))
        {
            if (rawFrame) rawFrame->assign(buffer.begin(), buffer.begin() + kFrameSize);
            buffer.erase(buffer.begin(), buffer.begin() + kFrameSize);
            return true;
        }
        buffer.erase(buffer.begin());
    }
    return false;
}
}
