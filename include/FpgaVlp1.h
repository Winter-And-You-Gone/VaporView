#ifndef VAPORVIEW_FPGA_VLP1_H
#define VAPORVIEW_FPGA_VLP1_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace VaporView::FpgaVlp1 {

constexpr std::size_t kHeaderSize = 40;
constexpr std::size_t kFrameOverhead = 44;
constexpr std::size_t kMaxCommandFrame = 4096;
constexpr std::size_t kMaxCommandPayload = 4052;
constexpr std::size_t kMaxFrame = 8236;
constexpr std::size_t kMaxPayload = 8192;

enum class FrameType : std::uint8_t { Command = 0x01, Response = 0x02,
    Data = 0x10, Event = 0x11, Status = 0x12, ProtocolError = 0x7f };
enum class Message : std::uint16_t { GetCapabilities = 0x0001, ReadReg = 0x0002,
    WriteReg = 0x0003, Ping = 0x00fe };

struct Header {
    std::uint8_t versionMajor = 1, versionMinor = 0;
    FrameType frameType = FrameType::Command;
    std::uint32_t totalLength = 0, sequence = 0;
    std::uint16_t source = 0, message = 0;
    std::uint32_t flags = 0, cycleId = 0, payloadLength = 0;
    std::uint64_t timestamp = 0;
};

struct Frame {
    std::size_t streamOffset = 0;
    Header header;
    std::vector<std::uint8_t> bytes; // header + payload + CRC, excluding padding
    std::vector<std::uint8_t> payload;
};

std::uint32_t crc32(const std::uint8_t *data, std::size_t size);
inline std::uint32_t crc32(const std::vector<std::uint8_t>& data) { return crc32(data.data(), data.size()); }

std::vector<std::uint8_t> buildFrame(FrameType type, std::uint32_t sequence,
    std::uint16_t source, std::uint16_t message, const std::vector<std::uint8_t>& payload,
    bool wordPadding = true, std::uint32_t flags = 0, std::uint64_t timestamp = 0,
    std::uint32_t cycleId = 0);
std::vector<std::uint8_t> buildPing(std::uint32_t sequence, const std::vector<std::uint8_t>& echo = {}, std::uint16_t source = 1);
std::vector<std::uint8_t> buildGetCapabilities(std::uint32_t sequence, std::uint16_t source = 1);
std::vector<std::uint8_t> buildReadReg(std::uint32_t sequence, std::uint32_t address, std::uint16_t count, std::uint16_t source = 1);
std::vector<std::uint8_t> buildWriteReg(std::uint32_t sequence, std::uint32_t address, const std::vector<std::uint32_t>& values, std::uint16_t flags = 0, std::uint16_t source = 1);

class StreamParser {
public:
    explicit StreamParser(std::size_t maxFrameBytes = kMaxFrame, std::size_t maxResyncBytes = (1u << 20));
    std::vector<Frame> feed(const std::uint8_t *data, std::size_t size);
    std::vector<Frame> feed(const std::vector<std::uint8_t>& data) { return feed(data.data(), data.size()); }
    std::vector<Frame> finish();
    std::vector<Frame> expirePartial();
    std::size_t discardedBytes() const { return discardedBytes_; }
    std::size_t frameCount() const { return frameCount_; }
    std::size_t pendingBytes() const { return buffer_.size(); }
private:
    std::vector<Frame> drain(bool eof);
    void discard(std::size_t count);
    std::vector<std::uint8_t> buffer_;
    std::size_t offset_ = 0, discardedBytes_ = 0, frameCount_ = 0, consecutiveDiscarded_ = 0;
    std::size_t maxFrameBytes_, maxResyncBytes_;
};

struct ResponseMatch {
    std::uint32_t sequence;
    std::uint16_t source;
    std::uint16_t message;
};
bool matchesResponse(const Frame& frame, const ResponseMatch& expected);
bool responseSucceeded(const Frame& frame, std::uint32_t *status = nullptr);

} // namespace VaporView::FpgaVlp1
#endif
