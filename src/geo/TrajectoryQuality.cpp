#include "geo/TrajectoryQuality.h"

#include <algorithm>
#include <cmath>

namespace VaporView::Geo {

bool isUsableForDisplay(const NavSample& sample, const QualityPolicy& policy)
{
    if (!sample.hasLlh())
    {
        return false;
    }
    if (sample.fixQuality == FixQuality::Invalid)
    {
        return false;
    }
    if (sample.satellites > 0 && sample.satellites < policy.minSatellites)
    {
        return false;
    }
    if (std::isfinite(sample.hdop) && sample.hdop > policy.maxHdop)
    {
        return false;
    }
    return true;
}

bool isLikelyJump(const NavSample& previous, const NavSample& current, const QualityPolicy& policy)
{
    const qint64 previousTimeUs = previous.deviceTimestampUs > 0 ? previous.deviceTimestampUs : previous.recordTimestampUs;
    const qint64 currentTimeUs = current.deviceTimestampUs > 0 ? current.deviceTimestampUs : current.recordTimestampUs;
    const qint64 deltaUs = currentTimeUs - previousTimeUs;
    if (deltaUs <= 0)
    {
        return true;
    }

    if (!previous.hasNed() || !current.hasNed())
    {
        return false;
    }

    const double deltaN = current.nedNM - previous.nedNM;
    const double deltaE = current.nedEM - previous.nedEM;
    const double deltaD = current.nedDM - previous.nedDM;
    const double distanceM = std::sqrt(deltaN * deltaN + deltaE * deltaE + deltaD * deltaD);
    const double deltaS = static_cast<double>(deltaUs) / 1000000.0;
    if (!(deltaS > 0.0) || !std::isfinite(distanceM))
    {
        return true;
    }

    return distanceM / deltaS > policy.maxReasonableSpeedMps;
}

} // namespace VaporView::Geo
