#pragma once

#include "geo/GeoTypes.h"

namespace VaporView::Geo {

struct QualityPolicy {
    int minSatellites = 5;
    double maxHdop = 5.0;
    double maxReasonableSpeedMps = 200.0;
};

bool isUsableForDisplay(const NavSample& sample, const QualityPolicy& policy = {});
bool isLikelyJump(const NavSample& previous, const NavSample& current, const QualityPolicy& policy = {});

} // namespace VaporView::Geo
