#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace VaporView::Hc2aProtocol {
struct Reading {
    unsigned address = 99;
    double humidity = 0.0, temperature = 0.0;
    unsigned alarms = 0;
};

inline std::string request(unsigned address) {
    if (address > 64 && address != 99) return {};
    std::string bytes = "{F00RDD}\r";
    bytes[2] = char('0' + address / 10);
    bytes[3] = char('0' + address % 10);
    return bytes;
}

inline std::string_view trim(std::string_view s) {
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
    return s;
}

// The probe uses fixed decimal text, never scientific notation or placeholders.
inline bool number(std::string_view text, bool decimal, double& value) {
    auto s = trim(text);
    if (s.empty()) return false;
    bool negative = false;
    if (decimal && (s.front() == '-' || s.front() == '+')) {
        negative = s.front() == '-'; s.remove_prefix(1);
    }
    unsigned digits = 0, fractional = 0;
    bool dot = false;
    double scale = 1.0;
    value = 0.0;
    for (const char c : s) {
        if (c == '.' && decimal && !dot) { dot = true; continue; }
        if (c < '0' || c > '9' || ++digits > 7) return false;
        value = value * 10.0 + (c - '0');
        if (dot) { if (++fractional > 2) return false; scale *= 10.0; }
    }
    if (!digits || (dot && !fractional)) return false;
    value = (negative ? -value : value) / scale;
    return std::isfinite(value);
}

inline bool decode(std::string_view frame, unsigned expectedAddress, Reading& result) {
    if (expectedAddress > 64 && expectedAddress != 99) return false;
    if (!frame.empty() && frame.back() == '\n') frame.remove_suffix(1);
    if (frame.empty() || frame.back() != '\r') return false;
    frame.remove_suffix(1);
    if (frame.size() < 16 || frame.size() > 160 || frame.substr(0, 2) != "{F"
        || frame[2] < '0' || frame[2] > '9' || frame[3] < '0' || frame[3] > '9'
        || (frame.substr(4, 3) != "rdd" && frame.substr(4, 3) != "RDD")) return false;
    const unsigned address = unsigned(frame[2] - '0') * 10 + unsigned(frame[3] - '0');
    if (address > 64 || (expectedAddress != 99 && address != expectedAddress)) return false;
    unsigned sum = 0;
    for (size_t i = 0; i + 1 < frame.size(); ++i) sum += static_cast<unsigned char>(frame[i]);
    if (static_cast<unsigned char>(frame.back()) != ((sum & 0x3f) + 0x20)) return false;
    std::array<std::string_view, 19> fields{};
    size_t start = 7;
    for (auto& field : fields) {
        const auto end = frame.find(';', start);
        if (end == std::string_view::npos) return false;
        field = trim(frame.substr(start, end - start)); start = end + 1;
    }
    if (start != frame.size() - 1) return false;
    double type, humidity, temperature, humidityAlarm, temperatureAlarm, alarm;
    if (!number(fields[0], false, type) || type != 1
        || !number(fields[1], true, humidity) || humidity < 0 || humidity > 100
        || (fields[2] != "%RH" && fields[2] != "%rh")
        || !number(fields[3], false, humidityAlarm) || humidityAlarm > 1
        || !number(fields[5], true, temperature) || temperature < -100 || temperature > 600
        || (fields[6] != "C" && fields[6] != "\xB0" "C" && fields[6] != "\xC2\xB0" "C")
        || !number(fields[7], false, temperatureAlarm) || temperatureAlarm > 1
        || !number(fields[18], false, alarm) || alarm > 255) return false;
    result = {address, humidity, temperature,
        unsigned(alarm) | (unsigned(humidityAlarm) << 8) | (unsigned(temperatureAlarm) << 9)};
    return true;
}

// Retain partial frames and recover at the next opening brace after line noise.
inline bool take(std::string& buffer, unsigned expectedAddress, Reading& result, std::string* raw = nullptr) {
    for (;;) {
        const auto start = buffer.find("{F");
        if (start == std::string::npos) { if (buffer.size() > 1) buffer.erase(0, buffer.size() - 1); return false; }
        if (start) buffer.erase(0, start);
        const auto end = buffer.find('\r');
        const auto next = buffer.find("{F", 2);
        if (next != std::string::npos && (end == std::string::npos || next < end)) { buffer.erase(0, next); continue; }
        if (end == std::string::npos) { if (buffer.size() > 161) buffer.clear(); return false; }
        const std::string candidate = buffer.substr(0, end + 1);
        buffer.erase(0, end + 1);
        if (decode(candidate, expectedAddress, result)) { if (raw) *raw = candidate; return true; }
    }
}
}
