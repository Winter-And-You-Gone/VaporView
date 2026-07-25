#pragma once

#include "geo/GeoTypes.h"
#include "geo/TrajectoryHeatmap.h"

#include <cstddef>
#include <vector>

namespace VaporView::Map3D {

std::vector<VaporView::Geo::NavSample> uniformlySampleTrack(
    const std::vector<VaporView::Geo::NavSample>& samples,
    int maxSamples);
std::vector<VaporView::Geo::NavSample> uniformlySampleTrack(
    const std::vector<VaporView::Geo::NavSample>& samples,
    std::size_t sourceCount,
    int maxSamples);
std::vector<VaporView::Geo::TrajectoryRenderSample> uniformlySampleTrack(
    const std::vector<VaporView::Geo::TrajectoryRenderSample>& samples,
    int maxSamples);
std::vector<VaporView::Geo::TrajectoryRenderSample> uniformlySampleTrack(
    const std::vector<VaporView::Geo::TrajectoryRenderSample>& samples,
    std::size_t sourceCount,
    int maxSamples);
double trackFocusRangeM(double trackRadiusM, bool earthMap);

} // namespace VaporView::Map3D
