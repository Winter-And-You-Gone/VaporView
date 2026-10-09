#include "FpgaVlp1.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace VaporView::FpgaVlp1 {
namespace {
void put16(std::vector<std::uint8_t>& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
}
void put32(std::vector<std::uint8_t>& b, std::uint32_t v) { for (int i=0;i<4;++i) b.push_back(static_cast<std::uint8_t>(v >> (8*i))); }
void put64(std::vector<std::uint8_t>& b, std::uint64_t v) { for (int i=0;i<8;++i) b.push_back(static_cast<std::uint8_t>(v >> (8*i))); }
std::uint16_t get16(const std::uint8_t *p) { return std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8); }
std::uint32_t get32(const std::uint8_t *p) { std::uint32_t v=0; for(int i=0;i<4;++i) v |= std::uint32_t(p[i]) << (8*i); return v; }
std::uint64_t get64(const std::uint8_t *p) { std::uint64_t v=0; for(int i=0;i<8;++i) v |= std::uint64_t(p[i]) << (8*i); return v; }
void checkReg(std::uint32_t address, std::size_t count) {
    if (address > 0xffffu || (address & 3u) || count == 0 || count > 0xffffu ||
        std::uint64_t(address) + 4u * count > 0x10000u) throw std::invalid_argument("invalid register range");
}
}

std::uint32_t crc32(const std::uint8_t *data, std::size_t size) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t n=0;n<size;++n) { crc ^= data[n]; for(int i=0;i<8;++i) crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u); }
    return crc ^ 0xffffffffu;
}

std::vector<std::uint8_t> buildFrame(FrameType type, std::uint32_t sequence, std::uint16_t source,
    std::uint16_t message, const std::vector<std::uint8_t>& payload, bool wordPadding,
    std::uint32_t flags, std::uint64_t timestamp, std::uint32_t cycleId) {
    if (payload.size() > kMaxPayload) throw std::invalid_argument("VLP payload too large");
    const auto total = kFrameOverhead + payload.size();
    if (type == FrameType::Command && total > kMaxCommandFrame) throw std::invalid_argument("command frame too large");
    std::vector<std::uint8_t> out; out.reserve(wordPadding ? ((total+3)&~std::size_t(3)) : total);
    out.insert(out.end(), {'V','L','P','1'}); out.push_back(1); out.push_back(0); out.push_back(static_cast<std::uint8_t>(type)); out.push_back(10);
    put32(out, static_cast<std::uint32_t>(total)); put32(out, sequence); put16(out, source); put16(out, message);
    put32(out, flags); put64(out, timestamp); put32(out, cycleId); put32(out, static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end()); put32(out, crc32(out));
    if (wordPadding) while (out.size() & 3u) out.push_back(0);
    return out;
}
std::vector<std::uint8_t> buildPing(std::uint32_t s, const std::vector<std::uint8_t>& e, std::uint16_t src) { return buildFrame(FrameType::Command,s,src,0xfe,e); }
std::vector<std::uint8_t> buildGetCapabilities(std::uint32_t s, std::uint16_t src) { return buildFrame(FrameType::Command,s,src,1,{}); }
std::vector<std::uint8_t> buildReadReg(std::uint32_t s, std::uint32_t a, std::uint16_t c, std::uint16_t src) {
    checkReg(a,c); if (c > 1021) throw std::invalid_argument("READ_REG count too large");
    std::vector<std::uint8_t> p; put32(p,a); put16(p,c); put16(p,0); return buildFrame(FrameType::Command,s,src,2,p);
}
std::vector<std::uint8_t> buildWriteReg(std::uint32_t s, std::uint32_t a, const std::vector<std::uint32_t>& v, std::uint16_t f, std::uint16_t src) {
    checkReg(a,v.size()); if (v.size() > 1011 || (f & ~1u)) throw std::invalid_argument("invalid WRITE_REG");
    std::vector<std::uint8_t> p; put32(p,a); put16(p,static_cast<std::uint16_t>(v.size())); put16(p,f); for(auto x:v) put32(p,x); return buildFrame(FrameType::Command,s,src,3,p);
}

StreamParser::StreamParser(std::size_t maxFrameBytes, std::size_t maxResyncBytes) : maxFrameBytes_(maxFrameBytes), maxResyncBytes_(maxResyncBytes) {
    if (maxFrameBytes_ < kFrameOverhead || maxFrameBytes_ > kMaxFrame || maxResyncBytes_ == 0) throw std::invalid_argument("invalid parser limits");
}
void StreamParser::discard(std::size_t count) {
    count = std::min(count, buffer_.size()); buffer_.erase(buffer_.begin(), buffer_.begin()+count); offset_ += count; discardedBytes_ += count; consecutiveDiscarded_ += count;
    if (consecutiveDiscarded_ > maxResyncBytes_) throw std::runtime_error("VLP resynchronization limit exceeded");
}
std::vector<Frame> StreamParser::drain(bool eof) {
    std::vector<Frame> result;
    for (;;) {
        if (buffer_.size() < 4) { if(eof && !buffer_.empty()) discard(buffer_.size()); break; }
        if (std::memcmp(buffer_.data(), "VLP1", 4) != 0) { auto it=std::search(buffer_.begin()+1,buffer_.end(),"VLP1","VLP1"+4); discard(it==buffer_.end() ? buffer_.size()-3 : std::size_t(it-buffer_.begin())); continue; }
        if (buffer_.size() < kHeaderSize) { if(eof){discard(1); continue;} break; }
        const auto *p=buffer_.data(); const auto total=get32(p+8), payloadLen=get32(p+36);
        if (p[4]!=1 || p[5]!=0 || p[7]!=10 || total < kFrameOverhead || total > maxFrameBytes_ || total != payloadLen+kFrameOverhead || payloadLen > kMaxPayload) { discard(1); continue; }
        const std::size_t wire=(total+3u)&~std::size_t(3); if(buffer_.size()<wire){if(eof){discard(1);continue;}break;}
        if (crc32(p,total-4) != get32(p+total-4)) { discard(1); continue; }
        if (std::any_of(buffer_.begin()+total,buffer_.begin()+wire,[](std::uint8_t x){return x!=0;})) { discard(1); continue; }
        Frame f; f.streamOffset=offset_; f.bytes.assign(buffer_.begin(),buffer_.begin()+total); f.payload.assign(buffer_.begin()+40,buffer_.begin()+40+payloadLen);
        f.header.versionMajor=p[4]; f.header.versionMinor=p[5]; f.header.frameType=static_cast<FrameType>(p[6]); f.header.totalLength=total; f.header.sequence=get32(p+12); f.header.source=get16(p+16); f.header.message=get16(p+18); f.header.flags=get32(p+20); f.header.timestamp=get64(p+24); f.header.cycleId=get32(p+32); f.header.payloadLength=payloadLen;
        result.push_back(std::move(f)); buffer_.erase(buffer_.begin(),buffer_.begin()+wire); offset_+=wire; ++frameCount_; consecutiveDiscarded_=0;
    } return result;
}
std::vector<Frame> StreamParser::feed(const std::uint8_t *data, std::size_t size) { if(size) buffer_.insert(buffer_.end(),data,data+size); return drain(false); }
std::vector<Frame> StreamParser::finish() { return drain(true); }
std::vector<Frame> StreamParser::expirePartial() { if(!buffer_.empty()) discard(1); return drain(false); }

bool matchesResponse(const Frame& f, const ResponseMatch& e) { return (f.header.frameType==FrameType::Response || f.header.frameType==FrameType::ProtocolError) && f.header.sequence==e.sequence && f.header.source==e.source && f.header.message==e.message; }
bool responseSucceeded(const Frame& f, std::uint32_t *status) { if(f.payload.size()<4 || f.header.frameType!=FrameType::Response) return false; const auto s=get32(f.payload.data()); if(status)*status=s; return s==0; }
}
