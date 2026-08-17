#include "ground/devices/DeviceRatePolicy.h"

#include <QSettings>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main()
{
    using namespace VaporView::Ground::DeviceRates;

    require(parseRate(QStringLiteral("50")) == 50, "numeric rate parsed");
    require(parseRate(QStringLiteral("0")) == 20, "out-of-range rate uses fallback");
    require(isRateUnspecified(QStringLiteral("No Set")), "English unset rate recognized");
    require(isRateUnspecified(QStringLiteral("不设定")), "Chinese unset rate recognized");
    require(effectiveRateOrDefault(QStringLiteral("No Set"), 100, 200) == 100,
            "unset rate uses bounded default");
    require(effectiveRateOrDefault(QStringLiteral("500"), 100, 200) == 200,
            "effective rate respects maximum");
    require(clampPtbSampleRate(200) == kPtbMaxSampleRateHz, "PTB rate is capped");

    const std::map<uint8_t, int> defaultRates = defaultEpsilonPacketRates();
    require(defaultRates.at(0x40) == 250 && defaultRates.at(0x5A) == 1,
            "recommended EPSILON packet-rate defaults are stable");
    require(epsilonPacketCallbackRate(defaultRates, 10) == 250,
            "callback rate follows fastest configured packet");

    const EpsilonSerialBandwidth defaultBandwidth = epsilonSerialBandwidth(defaultRates);
    require(defaultBandwidth.required_bits_per_second == 427570,
            "recommended profile bandwidth matches documented packet sizes");
    require(defaultBandwidth.fits(), "recommended profile fits 921600 baud with headroom");

    std::map<uint8_t, int> maximumRates;
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        maximumRates[option.packet_id] = option.supported_rates_hz.back();
    }
    const EpsilonSerialBandwidth maximumBandwidth = epsilonSerialBandwidth(maximumRates);
    require(maximumBandwidth.required_bits_per_second == 3005000,
            "maximum selectable profile bandwidth is calculated correctly");
    require(!maximumBandwidth.fits(), "maximum selectable profile is rejected at 921600 baud");

    QTemporaryDir temporaryDir;
    require(temporaryDir.isValid(), "temporary settings directory created");
    QSettings settings(temporaryDir.filePath(QStringLiteral("rates.ini")), QSettings::IniFormat);
    require(effectiveEpsilonPacketRates(settings) == defaultRates,
            "missing EPSILON packet-rate settings fall back to recommended defaults");
    settings.setValue(epsilonPacketRateSettingsKey(0x40), 500);
    const std::map<uint8_t, int> effective = effectiveEpsilonPacketRates(settings);
    require(effective.at(0x40) == 500 && effective.at(0x41) == defaultRates.at(0x41),
            "saved EPSILON packet rates override individual recommended defaults");
    require(!epsilonPacketRatesSignature(effective).isEmpty(), "packet-rate signature is stable");

    std::cout << "device rate policy tests passed\n";
    return 0;
}
