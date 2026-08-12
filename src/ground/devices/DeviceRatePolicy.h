#pragma once

#include <QString>
#include <QtGlobal>

#include <cstdint>
#include <map>
#include <vector>

class QSettings;

namespace VaporView::Ground::DeviceRates
{

inline constexpr int kPtbMinSampleRateHz = 1;
inline constexpr int kPtbMaxSampleRateHz = 70;
inline constexpr int kDefaultEpsilonSampleRateHz = 100;
inline constexpr int kDefaultPtbSampleRateHz = 20;
inline constexpr int kDefaultHmpSampleRateHz = 20;
inline constexpr int kDefaultLidarSampleRateHz = 100;
inline constexpr int kDefaultTemperatureSampleRateHz = 5;
inline constexpr int kMaxTemperatureSampleRateHz = 20;
inline constexpr int kDefaultEpsilonSerialBaud = 921600;
inline constexpr int kEpsilonSerialUtilizationLimitPercent = 80;

struct EpsilonPacketConfigOption
{
    quint8 packet_id = 0;
    const char *message_name = nullptr;
    const char *title_zh = nullptr;
    const char *title_en = nullptr;
    int payload_size_bytes = 0;
    std::vector<int> supported_rates_hz;
};

struct EpsilonSerialBandwidth
{
    qint64 required_bits_per_second = 0;
    qint64 limit_bits_per_second = 0;
    int baud_rate = 0;

    bool fits() const
    {
        return baud_rate > 0 && required_bits_per_second <= limit_bits_per_second;
    }
};

int parseRate(const QString& text);
bool isRateUnspecified(const QString& text);
int effectiveRateOrDefault(const QString& text, int defaultRate, int maxRate = 1000);
int clampPtbSampleRate(int hz);

const std::vector<EpsilonPacketConfigOption>& epsilonPacketConfigOptions();
QString epsilonPacketRateSettingsKey(quint8 packetId);
int nearestSupportedEpsilonPacketRate(const EpsilonPacketConfigOption& option, int desiredRateHz);
std::map<uint8_t, int> defaultEpsilonPacketRates();
bool epsilonPacketRateSupported(const EpsilonPacketConfigOption& option, int rateHz);
std::map<uint8_t, int> loadCustomEpsilonPacketRates(QSettings& settings);
std::map<uint8_t, int> effectiveEpsilonPacketRates(QSettings& settings);
QString epsilonPacketRatesSignature(const std::map<uint8_t, int>& packetRates);
QString epsilonPacketRatesSummary(const std::map<uint8_t, int>& packetRates);
int epsilonPacketCallbackRate(const std::map<uint8_t, int>& packetRates, int fallbackRateHz);
EpsilonSerialBandwidth epsilonSerialBandwidth(
    const std::map<uint8_t, int>& packetRates,
    int baudRate = kDefaultEpsilonSerialBaud);
QString epsilonPacketDialogRowLabel(const EpsilonPacketConfigOption& option, bool english);
QString epsilonPacketRateDisplayText(int rateHz, bool english);

} // namespace VaporView::Ground::DeviceRates
