#pragma once

#include "geo/GeoTypes.h"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace VaporView::Geo
{

enum class HeatMetric
{
    Peak = 0,
    Humidity = 1,
    Temperature = 2,
    Pressure = 3,
    ColumnWaterConcentration = 4, // 816 nm
    InSituWaterConcentration = 5, // 1368 nm
    LidarDistance = 6             // SDFM300
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
    double peak = std::numeric_limits<double>::quiet_NaN();
    double humidityRh = std::numeric_limits<double>::quiet_NaN();
    double temperatureC = std::numeric_limits<double>::quiet_NaN();
    double pressureHpa = std::numeric_limits<double>::quiet_NaN();
    double columnWaterConcentration = std::numeric_limits<double>::quiet_NaN();
    double inSituWaterConcentration = std::numeric_limits<double>::quiet_NaN();
    double lidarDistanceM = std::numeric_limits<double>::quiet_NaN();
    std::uint32_t calibrationEpoch = 0;
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

bool isWaterConcentrationMetric(HeatMetric metric);
QString heatMetricName(HeatMetric metric, bool english);
QString heatPaletteName(HeatPalette palette, bool english);
QString heatMetricUnit(HeatMetric metric);
QString heatMetricUnit(HeatMetric metric, const QString& waterUnitOverride);
QString formatHeatValue(double value);

std::optional<double> metricValue(const TrajectoryHeatValues& values,
                                  HeatMetric metric);
std::optional<double> metricValue(const TrajectoryRenderSample& sample,
                                  HeatMetric metric);

std::optional<double> metricValueForEpoch(const TrajectoryRenderSample& sample, HeatMetric metric,
                                          std::optional<std::uint32_t> requiredCalibrationEpoch);

void accumulateHeatRange(HeatRange& range, std::optional<double> value);
void accumulateHeatRange(HeatRange& range,
                         const TrajectoryHeatValues& values,
                         HeatMetric metric);
HeatRange calculateHeatRange(const std::vector<TrajectoryHeatValues>& values,
                             HeatMetric metric);
HeatRange calculateHeatRange(const std::vector<TrajectoryHeatValues>& values, HeatMetric metric,
                             std::optional<std::uint32_t> requiredCalibrationEpoch);
HeatRange calculateHeatRange(const std::vector<TrajectoryRenderSample>& samples,
                             HeatMetric metric);
HeatRange calculateHeatRange(const std::vector<TrajectoryRenderSample>& samples,
                             HeatMetric metric, std::size_t count);
// Pass an optional epoch explicitly: a plain integer in this position selects a prefix count.
HeatRange calculateHeatRange(const std::vector<TrajectoryRenderSample>& samples, HeatMetric metric,
                             std::optional<std::uint32_t> requiredCalibrationEpoch);
HeatRange calculateHeatRange(const std::vector<TrajectoryRenderSample>& samples, HeatMetric metric,
                             std::size_t count, std::optional<std::uint32_t> requiredCalibrationEpoch);

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
