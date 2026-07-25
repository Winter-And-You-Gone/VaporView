#pragma once

#include "geo/GeoTypes.h"

#include <QString>

#include <cstddef>
#include <optional>
#include <vector>

namespace VaporView::Geo
{

enum class HeatMetric
{
    Peak,
    Humidity,
    Temperature,
    Pressure
};

enum class HeatPalette
{
    Candy,
    BlueRedFast,
    SpectralReverse
};

struct HeatColor
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

struct TrajectoryHeatValues
{
    std::optional<double> peak;
    std::optional<double> humidityRh;
    std::optional<double> temperatureC;
    std::optional<double> pressureHpa;
};

struct HeatRange
{
    bool valid = false;
    double minimum = 0.0;
    double maximum = 0.0;
    std::size_t validCount = 0;
};

struct TrajectoryRenderSample
{
    NavSample navigation;
    TrajectoryHeatValues heat;
};

QString heatMetricName(HeatMetric metric, bool english);
QString heatPaletteName(HeatPalette palette, bool english);
QString heatMetricUnit(HeatMetric metric);

std::optional<double> metricValue(const TrajectoryHeatValues& values,
                                  HeatMetric metric);
std::optional<double> metricValue(const TrajectoryRenderSample& sample,
                                  HeatMetric metric);

void accumulateHeatRange(HeatRange& range, std::optional<double> value);
void accumulateHeatRange(HeatRange& range,
                         const TrajectoryHeatValues& values,
                         HeatMetric metric);
HeatRange calculateHeatRange(const std::vector<TrajectoryHeatValues>& values,
                             HeatMetric metric);
HeatRange calculateHeatRange(const std::vector<TrajectoryRenderSample>& samples,
                             HeatMetric metric);
HeatRange calculateHeatRange(const std::vector<TrajectoryRenderSample>& samples,
                             HeatMetric metric,
                             std::size_t count);

std::optional<double> normalizeHeatValue(double value, const HeatRange& range);

HeatColor neutralHeatColor();
HeatColor heatPaletteColor(double normalized, HeatPalette palette);
HeatColor heatColorForValue(double value,
                            const HeatRange& range,
                            HeatPalette palette);
HeatColor heatColorForValue(std::optional<double> value,
                            const HeatRange& range,
                            HeatPalette palette,
                            HeatColor fallback = neutralHeatColor());

} // namespace VaporView::Geo
