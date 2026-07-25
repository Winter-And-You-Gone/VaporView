#include "geo/TrajectoryHeatmap.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace VaporView::Geo
{
namespace
{

struct HeatColor8
{
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a = 255;
};

struct HeatColorStop
{
    double position;
    HeatColor8 color;
};

constexpr std::array<HeatColorStop, 6> kCandyStops = {{
    {0.00, {0x00, 0x57, 0xFF}},
    {0.20, {0x00, 0xF0, 0xFF}},
    {0.40, {0x44, 0xFF, 0x00}},
    {0.60, {0xFF, 0xF5, 0x00}},
    {0.80, {0xFF, 0x7A, 0x00}},
    {1.00, {0xFF, 0x00, 0xB8}},
}};

constexpr std::array<HeatColorStop, 9> kBlueRedFastStops = {{
    {0.00, {0x00, 0x1B, 0xFF}},
    {0.18, {0x00, 0x6C, 0xFF}},
    {0.34, {0x00, 0xC8, 0xFF}},
    {0.46, {0x00, 0xF0, 0xB8}},
    {0.50, {0x42, 0xFF, 0x38}},
    {0.54, {0xDF, 0xFF, 0x00}},
    {0.66, {0xFF, 0xC0, 0x00}},
    {0.82, {0xFF, 0x4A, 0x00}},
    {1.00, {0xD6, 0x00, 0x00}},
}};

constexpr std::array<HeatColorStop, 10> kSpectralReverseStops = {{
    {0.00, {0x5E, 0x4F, 0xA2}},
    {0.11, {0x32, 0x88, 0xBD}},
    {0.22, {0x66, 0xC2, 0xA5}},
    {0.33, {0xAB, 0xDD, 0xA4}},
    {0.44, {0xE6, 0xF5, 0x98}},
    {0.55, {0xFE, 0xE0, 0x8B}},
    {0.66, {0xFD, 0xAE, 0x61}},
    {0.77, {0xF4, 0x6D, 0x43}},
    {0.88, {0xD5, 0x3E, 0x4F}},
    {1.00, {0x9E, 0x01, 0x42}},
}};

HeatColor toHeatColor(const HeatColor8& color)
{
    constexpr float kScale = 1.0f / 255.0f;
    return {
        static_cast<float>(color.r) * kScale,
        static_cast<float>(color.g) * kScale,
        static_cast<float>(color.b) * kScale,
        static_cast<float>(color.a) * kScale,
    };
}

std::uint8_t interpolateChannel(std::uint8_t first,
                                std::uint8_t second,
                                double ratio)
{
    const double clampedRatio = std::clamp(ratio, 0.0, 1.0);
    return static_cast<std::uint8_t>(std::lround(
        static_cast<double>(first)
        + (static_cast<double>(second) - static_cast<double>(first)) * clampedRatio));
}

HeatColor8 interpolateColor(const HeatColor8& first,
                            const HeatColor8& second,
                            double ratio)
{
    return {
        interpolateChannel(first.r, second.r, ratio),
        interpolateChannel(first.g, second.g, ratio),
        interpolateChannel(first.b, second.b, ratio),
        interpolateChannel(first.a, second.a, ratio),
    };
}

template <std::size_t StopCount>
HeatColor colorAtStops(double normalized,
                       const std::array<HeatColorStop, StopCount>& stops)
{
    const double clamped = std::clamp(normalized, 0.0, 1.0);
    for (std::size_t index = 1; index < stops.size(); ++index)
    {
        const HeatColorStop& previous = stops[index - 1];
        const HeatColorStop& current = stops[index];
        if (clamped <= current.position)
        {
            const double localRatio =
                (clamped - previous.position)
                / std::max(1.0e-6, current.position - previous.position);
            return toHeatColor(interpolateColor(previous.color, current.color, localRatio));
        }
    }
    return toHeatColor(stops.back().color);
}

} // namespace

QString heatMetricName(HeatMetric metric, bool english)
{
    switch (metric)
    {
    case HeatMetric::Humidity:
        return english ? QStringLiteral("Humidity") : QStringLiteral("湿度");
    case HeatMetric::Temperature:
        return english ? QStringLiteral("Temperature") : QStringLiteral("温度");
    case HeatMetric::Pressure:
        return english ? QStringLiteral("Pressure") : QStringLiteral("气压");
    case HeatMetric::Peak:
    default:
        return english ? QStringLiteral("Peak") : QStringLiteral("峰值");
    }
}

QString heatPaletteName(HeatPalette palette, bool english)
{
    switch (palette)
    {
    case HeatPalette::BlueRedFast:
        return english ? QStringLiteral("Fast blue-red") : QStringLiteral("蓝红急变");
    case HeatPalette::SpectralReverse:
        return english ? QStringLiteral("SpectralReverse") : QStringLiteral("反向光谱");
    case HeatPalette::Candy:
    default:
        return english ? QStringLiteral("Candy") : QStringLiteral("糖果");
    }
}

QString heatMetricUnit(HeatMetric metric)
{
    switch (metric)
    {
    case HeatMetric::Humidity:
        return QStringLiteral("%RH");
    case HeatMetric::Temperature:
        return QStringLiteral("°C");
    case HeatMetric::Pressure:
        return QStringLiteral("hPa");
    case HeatMetric::Peak:
    default:
        return {};
    }
}

std::optional<double> metricValue(const TrajectoryHeatValues& values,
                                  HeatMetric metric)
{
    const std::optional<double>* selected = nullptr;
    switch (metric)
    {
    case HeatMetric::Humidity:
        selected = &values.humidityRh;
        break;
    case HeatMetric::Temperature:
        selected = &values.temperatureC;
        break;
    case HeatMetric::Pressure:
        selected = &values.pressureHpa;
        break;
    case HeatMetric::Peak:
    default:
        selected = &values.peak;
        break;
    }

    if (!selected->has_value() || !std::isfinite(selected->value()))
    {
        return std::nullopt;
    }
    return selected->value();
}

std::optional<double> metricValue(const TrajectoryRenderSample& sample,
                                  HeatMetric metric)
{
    return metricValue(sample.heat, metric);
}

void accumulateHeatRange(HeatRange& range, std::optional<double> value)
{
    if (!value.has_value() || !std::isfinite(value.value()))
    {
        return;
    }

    if (!range.valid)
    {
        range.valid = true;
        range.minimum = value.value();
        range.maximum = value.value();
    }
    else
    {
        range.minimum = std::min(range.minimum, value.value());
        range.maximum = std::max(range.maximum, value.value());
    }
    ++range.validCount;
}

void accumulateHeatRange(HeatRange& range,
                         const TrajectoryHeatValues& values,
                         HeatMetric metric)
{
    accumulateHeatRange(range, metricValue(values, metric));
}

HeatRange calculateHeatRange(const std::vector<TrajectoryHeatValues>& values,
                             HeatMetric metric)
{
    HeatRange range;
    for (const TrajectoryHeatValues& sampleValues : values)
    {
        accumulateHeatRange(range, sampleValues, metric);
    }
    return range;
}

HeatRange calculateHeatRange(const std::vector<TrajectoryRenderSample>& samples,
                             HeatMetric metric)
{
    return calculateHeatRange(samples, metric, samples.size());
}

HeatRange calculateHeatRange(const std::vector<TrajectoryRenderSample>& samples,
                             HeatMetric metric,
                             std::size_t count)
{
    HeatRange range;
    const std::size_t sampleCount = std::min(count, samples.size());
    for (std::size_t index = 0; index < sampleCount; ++index)
    {
        accumulateHeatRange(range, samples[index].heat, metric);
    }
    return range;
}

std::optional<double> normalizeHeatValue(double value, const HeatRange& range)
{
    if (!range.valid
        || !std::isfinite(value)
        || !std::isfinite(range.minimum)
        || !std::isfinite(range.maximum))
    {
        return std::nullopt;
    }

    const double totalRange = range.maximum - range.minimum;
    if (!(totalRange > 1.0e-6))
    {
        return 0.5;
    }
    return std::clamp((value - range.minimum) / totalRange, 0.0, 1.0);
}

HeatColor neutralHeatColor()
{
    return toHeatColor({0x80, 0x80, 0x80});
}

HeatColor heatPaletteColor(double normalized, HeatPalette palette)
{
    if (!std::isfinite(normalized))
    {
        return neutralHeatColor();
    }

    switch (palette)
    {
    case HeatPalette::BlueRedFast:
        return colorAtStops(normalized, kBlueRedFastStops);
    case HeatPalette::SpectralReverse:
        return colorAtStops(normalized, kSpectralReverseStops);
    case HeatPalette::Candy:
    default:
        return colorAtStops(normalized, kCandyStops);
    }
}

HeatColor heatColorForValue(double value,
                            const HeatRange& range,
                            HeatPalette palette)
{
    return heatColorForValue(std::optional<double>(value),
                             range,
                             palette,
                             neutralHeatColor());
}

HeatColor heatColorForValue(std::optional<double> value,
                            const HeatRange& range,
                            HeatPalette palette,
                            HeatColor fallback)
{
    if (!value.has_value())
    {
        return fallback;
    }
    const std::optional<double> normalized = normalizeHeatValue(value.value(), range);
    return normalized.has_value()
        ? heatPaletteColor(normalized.value(), palette)
        : fallback;
}

} // namespace VaporView::Geo
