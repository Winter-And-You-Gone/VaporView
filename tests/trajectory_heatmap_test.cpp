#include "geo/TrajectoryHeatmap.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace
{

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void requireNear(double actual, double expected, const char* message)
{
    require(std::abs(actual - expected) <= 1.0e-9, message);
}

void requireColor(VaporView::Geo::HeatColor color,
                  int red,
                  int green,
                  int blue,
                  const char* message)
{
    const auto channel = [](float value) {
        return static_cast<int>(std::lround(static_cast<double>(value) * 255.0));
    };
    require(channel(color.r) == red
                && channel(color.g) == green
                && channel(color.b) == blue
                && channel(color.a) == 255,
            message);
}

} // namespace

int main()
{
    using namespace VaporView::Geo;

    require(heatMetricName(HeatMetric::Peak, true) == QStringLiteral("Peak")
                && heatMetricName(HeatMetric::Peak, false) == QStringLiteral("峰值"),
            "peak label");
    require(heatMetricName(HeatMetric::Humidity, false) == QStringLiteral("湿度"),
            "humidity label");
    require(heatMetricName(HeatMetric::Temperature, false) == QStringLiteral("温度"),
            "temperature label");
    require(heatMetricName(HeatMetric::Pressure, false) == QStringLiteral("气压"),
            "pressure label");
    require(heatPaletteName(HeatPalette::Candy, true) == QStringLiteral("Candy")
                && heatPaletteName(HeatPalette::Candy, false) == QStringLiteral("糖果"),
            "candy label");
    require(heatPaletteName(HeatPalette::BlueRedFast, true) == QStringLiteral("Fast blue-red")
                && heatPaletteName(HeatPalette::BlueRedFast, false) == QStringLiteral("蓝红急变"),
            "blue-red label");
    require(heatPaletteName(HeatPalette::SpectralReverse, true) == QStringLiteral("SpectralReverse")
                && heatPaletteName(HeatPalette::SpectralReverse, false) == QStringLiteral("反向光谱"),
            "spectral label");
    require(heatMetricUnit(HeatMetric::Peak).isEmpty(), "peak is unitless");
    require(heatMetricUnit(HeatMetric::Humidity) == QStringLiteral("%RH"), "humidity unit");
    require(heatMetricUnit(HeatMetric::Temperature) == QStringLiteral("°C"), "temperature unit");
    require(heatMetricUnit(HeatMetric::Pressure) == QStringLiteral("hPa"), "pressure unit");

    TrajectoryHeatValues values;
    values.peak = 4.5;
    values.humidityRh = 62.0;
    values.temperatureC = 23.25;
    values.pressureHpa = 1008.5;
    require(metricValue(values, HeatMetric::Peak) == values.peak, "peak value");
    require(metricValue(values, HeatMetric::Humidity) == values.humidityRh, "humidity value");
    require(metricValue(values, HeatMetric::Temperature) == values.temperatureC, "temperature value");
    require(metricValue(values, HeatMetric::Pressure) == values.pressureHpa, "pressure value");
    TrajectoryRenderSample sample;
    sample.heat = values;
    require(metricValue(sample, HeatMetric::Temperature) == values.temperatureC,
            "render sample value");

    const TrajectoryHeatValues missing;
    require(!metricValue(missing, HeatMetric::Peak).has_value()
                && !metricValue(missing, HeatMetric::Humidity).has_value()
                && !metricValue(missing, HeatMetric::Temperature).has_value()
                && !metricValue(missing, HeatMetric::Pressure).has_value(),
            "missing fields are not zero");

    TrajectoryHeatValues nonFinite;
    nonFinite.peak = std::numeric_limits<double>::quiet_NaN();
    nonFinite.humidityRh = std::numeric_limits<double>::infinity();
    nonFinite.temperatureC = -std::numeric_limits<double>::infinity();
    require(!metricValue(nonFinite, HeatMetric::Peak).has_value()
                && !metricValue(nonFinite, HeatMetric::Humidity).has_value()
                && !metricValue(nonFinite, HeatMetric::Temperature).has_value(),
            "non-finite values are ignored");

    TrajectoryHeatValues first;
    first.temperatureC = 10.0;
    TrajectoryHeatValues second;
    second.temperatureC = 20.0;
    TrajectoryHeatValues third;
    third.temperatureC = std::numeric_limits<double>::quiet_NaN();
    const HeatRange range = calculateHeatRange(
        std::vector<TrajectoryHeatValues>{first, missing, second, third},
        HeatMetric::Temperature);
    require(range.valid, "range is valid");
    requireNear(range.minimum, 10.0, "range min");
    requireNear(range.maximum, 20.0, "range max");
    require(range.validCount == 2, "range count");

    HeatRange accumulated;
    accumulateHeatRange(accumulated, std::nullopt);
    accumulateHeatRange(accumulated, first, HeatMetric::Temperature);
    accumulateHeatRange(accumulated, std::optional<double>(-4.0));
    requireNear(accumulated.minimum, -4.0, "accumulated min");
    requireNear(accumulated.maximum, 10.0, "accumulated max");
    require(accumulated.validCount == 2, "accumulated count");

    const HeatRange empty = calculateHeatRange(std::vector<TrajectoryHeatValues>{},
                                               HeatMetric::Pressure);
    require(!empty.valid && empty.validCount == 0, "empty range");

    TrajectoryHeatValues only;
    only.pressureHpa = 999.0;
    const HeatRange single = calculateHeatRange(std::vector<TrajectoryHeatValues>{only},
                                                HeatMetric::Pressure);
    require(single.valid && single.validCount == 1, "single-value range is valid");
    requireNear(normalizeHeatValue(999.0, single).value_or(-1.0), 0.5,
                "single-value range normalizes to middle");

    HeatRange ordinary;
    ordinary.valid = true;
    ordinary.minimum = 10.0;
    ordinary.maximum = 20.0;
    requireNear(normalizeHeatValue(5.0, ordinary).value_or(-1.0), 0.0, "clamp low");
    requireNear(normalizeHeatValue(15.0, ordinary).value_or(-1.0), 0.5, "normalize midpoint");
    requireNear(normalizeHeatValue(30.0, ordinary).value_or(-1.0), 1.0, "clamp high");
    require(!normalizeHeatValue(std::numeric_limits<double>::quiet_NaN(), ordinary).has_value(),
            "NaN has no normalized value");
    require(!normalizeHeatValue(15.0, empty).has_value(), "invalid range has no normalized value");

    HeatRange tiny;
    tiny.valid = true;
    tiny.minimum = 4.0;
    tiny.maximum = 4.0 + 0.5e-6;
    requireNear(normalizeHeatValue(4.0, tiny).value_or(-1.0), 0.5,
                "near-degenerate range maps to midpoint");

    std::vector<TrajectoryRenderSample> renderSamples(3);
    renderSamples[0].heat.peak = 1.0;
    renderSamples[1].heat.peak = 3.0;
    renderSamples[2].heat.peak = 999.0;
    const HeatRange prefix = calculateHeatRange(renderSamples, HeatMetric::Peak, 2);
    require(prefix.valid && prefix.validCount == 2, "prefix range count");
    requireNear(prefix.minimum, 1.0, "prefix min");
    requireNear(prefix.maximum, 3.0, "prefix max");

    requireColor(heatPaletteColor(0.0, HeatPalette::Candy), 0x00, 0x57, 0xFF, "Candy start");
    requireColor(heatPaletteColor(0.5, HeatPalette::Candy), 0xA2, 0xFA, 0x00, "Candy mid");
    requireColor(heatPaletteColor(1.0, HeatPalette::Candy), 0xFF, 0x00, 0xB8, "Candy end");
    requireColor(heatPaletteColor(0.0, HeatPalette::BlueRedFast), 0x00, 0x1B, 0xFF, "BlueRed start");
    requireColor(heatPaletteColor(0.5, HeatPalette::BlueRedFast), 0x42, 0xFF, 0x38, "BlueRed mid");
    requireColor(heatPaletteColor(1.0, HeatPalette::BlueRedFast), 0xD6, 0x00, 0x00, "BlueRed end");
    requireColor(heatPaletteColor(0.0, HeatPalette::SpectralReverse), 0x5E, 0x4F, 0xA2, "Spectral start");
    requireColor(heatPaletteColor(0.5, HeatPalette::SpectralReverse), 0xF3, 0xEA, 0x91, "Spectral mid");
    requireColor(heatPaletteColor(1.0, HeatPalette::SpectralReverse), 0x9E, 0x01, 0x42, "Spectral end");
    requireColor(heatPaletteColor(0.1, HeatPalette::Candy), 0x00, 0xA4, 0xFF, "interpolation");
    requireColor(heatPaletteColor(-1.0, HeatPalette::Candy), 0x00, 0x57, 0xFF, "clamp low color");
    requireColor(heatPaletteColor(2.0, HeatPalette::Candy), 0xFF, 0x00, 0xB8, "clamp high color");

    HeatRange colorRange;
    colorRange.valid = true;
    colorRange.minimum = 0.0;
    colorRange.maximum = 10.0;
    requireColor(heatColorForValue(5.0, colorRange, HeatPalette::BlueRedFast),
                 0x42, 0xFF, 0x38, "value color");
    requireColor(heatColorForValue(std::nullopt, colorRange, HeatPalette::Candy),
                 0x80, 0x80, 0x80, "neutral fallback");
    requireColor(heatPaletteColor(std::numeric_limits<double>::quiet_NaN(),
                                  HeatPalette::Candy),
                 0x80, 0x80, 0x80, "NaN normalized color uses neutral fallback");
    requireColor(heatColorForValue(std::numeric_limits<double>::infinity(),
                                   colorRange,
                                   HeatPalette::Candy),
                 0x80, 0x80, 0x80, "infinite value uses neutral fallback");
    const std::vector<TrajectoryRenderSample> legacySamples(2);
    const HeatRange legacyRange = calculateHeatRange(legacySamples, HeatMetric::Humidity);
    require(!legacyRange.valid && legacyRange.validCount == 0,
            "legacy samples without environment data stay compatible");

    return 0;
}
