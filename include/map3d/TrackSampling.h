#pragma once

#include "geo/GeoTypes.h"

#include <vector>

namespace VaporView::Map3D {

std::vector<VaporView::Geo::NavSample> uniformlySampleTrack(
    const std::vector<VaporView::Geo::NavSample>& samples,
    int maxSamples);

} // namespace VaporView::Map3D
