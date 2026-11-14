#include "Sdfm300Protocol.h"
#include "data_collector.h"
#include "shared/session/UnifiedRawDat.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
void require(bool value, const char* message)
{
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
std::array<uint8_t, 4> frame(uint16_t cm)
{
    const auto low = static_cast<uint8_t>(cm);
    const auto high = static_cast<uint8_t>(cm >> 8);
    return {0x5c, low, high, static_cast<uint8_t>(255 - ((low + high) & 255))};
}
}

int main()
{
    using namespace VaporView;
    LidarData sample;
    const std::array<uint8_t, 4> manualExample{0x5c, 0x02, 0x11, 0xec};
    require(Sdfm300::decodeFrame(manualExample.data(), 4, sample) && sample.valid &&
                std::abs(sample.distance_m - 43.54) < 1e-9, "manual example decodes to 43.54 m");
    const std::array<uint8_t, 4> noTarget{0x5c, 0xff, 0xff, 0x01};
    require(Sdfm300::decodeFrame(noTarget.data(), 4, sample) && !sample.valid &&
                sample.distance_m == 0.0, "FFFF is an invalid measurement in a valid frame");
    for (uint32_t cm = 0; cm <= 0xffff; ++cm)
    {
        const auto bytes = frame(static_cast<uint16_t>(cm));
        require(Sdfm300::decodeFrame(bytes.data(), 4, sample), "all checksummed LE16 frames are recognized");
        require(sample.valid == (cm >= 5 && cm <= 30000), "physical range is 0.05 through 300 m");
        if (sample.valid) require(std::abs(sample.distance_m - cm / 100.0) < 1e-9, "centimeters convert once");
    }
    require(!Sdfm300::decodeFrame(nullptr, 4, sample), "null input rejected");
    for (size_t length = 0; length < 4; ++length)
        require(!Sdfm300::decodeFrame(manualExample.data(), length, sample), "truncated frame rejected");
    auto corrupt = manualExample;
    corrupt[3] ^= 1;
    require(!Sdfm300::decodeFrame(corrupt.data(), 4, sample), "bad checksum rejected");
    const uint8_t oldFrame[]{0x5c, 0x02, 0x11, 0x00, 0xec};
    require(!Sdfm300::decodeFrame(oldFrame, 5, sample), "old five-byte protocol is not acquired");

    std::vector<uint8_t> buffer{0x00, 0xaa, 0x5c, 0x00, 0x00, 0x00, 0x5c, 0x02};
    require(!Sdfm300::extractSample(buffer, sample) && buffer == std::vector<uint8_t>({0x5c, 0x02}),
            "noise and corrupt frames are discarded while split header and payload survive");
    buffer.insert(buffer.end(), {0x11, 0xec, 0x5c, 0xff, 0xff, 0x01});
    std::vector<uint8_t> raw;
    require(Sdfm300::extractSample(buffer, sample, &raw) && sample.valid &&
                raw == std::vector<uint8_t>(manualExample.begin(), manualExample.end()),
            "split sample reconstructs exact raw bytes");
    require(Sdfm300::extractSample(buffer, sample, &raw) && !sample.valid && buffer.empty(),
            "concatenated no-target frame is retained for recording and clears validity");
    const auto embeddedHeader = frame(0x5c5c);
    buffer.assign(embeddedHeader.begin(), embeddedHeader.end());
    require(Sdfm300::extractSample(buffer, sample) && sample.valid, "header bytes inside distance are data");
    require(SessionRawDat::isValidRecordType(SessionRawDat::kSourceDistance, Sdfm300::kRecordType) &&
                SessionRawDat::isValidRecordType(SessionRawDat::kSourceDistance, 2) &&
                SessionRawDat::isValidRecordType(SessionRawDat::kSourceDistance, 5) &&
                !SessionRawDat::isValidRecordType(SessionRawDat::kSourceDistance, 7),
            "new raw type is distinct and historical types remain readable");

    LidarCollector collector;
    require(collector.setDeviceSampleRate(1000) && collector.getSampleRate() == 1000 &&
                collector.getLastError().empty(), "host rate needs no open serial port or device command");
    require(collector.setDeviceSampleRate(2000) && collector.getSampleRate() == 1000,
            "host refresh limit cannot exceed the module maximum");
    std::cout << "SDFM300 protocol tests passed\n";
}
