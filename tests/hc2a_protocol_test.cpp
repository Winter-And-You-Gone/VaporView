#include "Hc2aProtocol.h"
#include <cstdlib>
#include <iostream>

using namespace VaporView::Hc2aProtocol;
static void require(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
static std::string reply(const std::string& rh = " 47.80", const std::string& unit = "%rh",
                         const std::string& alarm = "000") {
    std::string bytes = "{F00rdd 001;" + rh + ";" + unit + ";000;=; 21.76;\xB0" "C;000;=;nc;---.--;\xB0" "C;000; ;020;V1.2-1;0000000002;HC2A;" + alarm + ";";
    unsigned sum = 0;
    for (unsigned char c : bytes) sum += c;
    bytes += char((sum & 0x3f) + 0x20); bytes += '\r';
    return bytes;
}
int main() {
    Reading r;
    require(request(99) == "{F99RDD}\r" && request(4) == "{F04RDD}\r", "read-only RDD requests");
    require(request(65).empty() && request(240).empty(), "reject invalid HC2A addresses");
    const auto frame = reply();
    require(decode(frame, 99, r) && r.address == 0 && r.humidity == 47.80
        && r.temperature == 21.76 && !r.alarms, "decode actual lower-case RH and Latin-1 Celsius units");
    require(decode(reply("100.00", "%RH"), 0, r) && r.humidity == 100, "upper-case unit and RH boundary");
    require(!decode(frame, 4, r), "refuse another probe's address");
    require(!decode(reply("---.--"), 99, r) && !decode(reply("101.00"), 99, r), "invalid PV is never a temperature sample");
    require(!decode(reply("47.8", "%Rh"), 99, r), "unknown RH unit rejected");
    require(decode(reply("47.80", "%rh", "002"), 99, r) && r.alarms == 2, "retain Rotronic alarm");
    require(!decode(reply("47.80", "%rh", "256"), 99, r), "reject oversized alarm");
    auto bad = frame; bad[15] ^= 1;
    require(!decode(bad, 99, r), "checksum failure rejected");
    std::string buffer = "line noise" + frame.substr(0, 30);
    require(!take(buffer, 99, r), "retain split response");
    buffer += frame.substr(30) + frame;
    std::string raw;
    require(take(buffer, 99, r, &raw) && raw == frame && take(buffer, 0, r) && buffer.empty(), "multiple frames preserve exact raw bytes");
    buffer = bad + "{Fbroken" + frame;
    require(take(buffer, 99, r), "resynchronize after bad checksum and truncated response");
    buffer = "{F" + std::string(2000, 'x');
    require(!take(buffer, 99, r) && buffer.empty(), "bounded incomplete response");
    std::cout << "HC2A_PROTOCOL_PASS\n";
}
