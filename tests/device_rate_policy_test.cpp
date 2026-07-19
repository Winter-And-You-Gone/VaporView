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

    const std::map<uint8_t, int> grouped = groupedEpsilonPacketRates(100);
    require(grouped.at(0x40) == 100, "high-rate EPSILON packet follows grouped rate");
    require(grouped.at(0x53) == 20, "status EPSILON packet uses low-rate group");
    require(epsilonPacketCallbackRate(grouped, 10) == 100,
            "callback rate follows fastest configured packet");
    const EpsilonSerialBandwidth groupedBandwidth = epsilonSerialBandwidth(grouped);
    require(groupedBandwidth.required_bits_per_second == 390600,
            "grouped 100 Hz profile includes FDILink and 8N1 overhead");
    require(groupedBandwidth.fits(), "grouped 100 Hz profile fits 921600 baud with headroom");

    const EpsilonSerialBandwidth defaultBandwidth = epsilonSerialBandwidth(defaultEpsilonPacketRates());
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
    settings.setValue(QStringLiteral("epsilon_custom_packet_rates_enabled"), true);
    settings.setValue(QStringLiteral("epsilon_custom_packet_rates_user_saved"), true);
    settings.setValue(epsilonPacketRateSettingsKey(0x40), 250);
    bool custom = false;
    const std::map<uint8_t, int> effective = effectiveEpsilonPacketRates(settings, 100, &custom);
    require(custom, "saved custom EPSILON profile is selected");
    require(effective.at(0x40) == 250, "custom EPSILON packet rate is loaded");
    require(!epsilonPacketRatesSignature(effective).isEmpty(), "packet-rate signature is stable");

    std::cout << "device rate policy tests passed\n";
    return 0;
}
