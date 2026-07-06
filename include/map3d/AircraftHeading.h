#pragma once

#include "geo/GeoTypes.h"

namespace VaporView::Map3D {

double aircraftHeadingRad(const VaporView::Geo::NavSample& sample);
double aircraftHeadingDeg(const VaporView::Geo::NavSample& sample);

} // namespace VaporView::Map3D
