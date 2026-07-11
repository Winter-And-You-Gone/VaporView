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
    qint64 previousTimeUs = 0;
    qint64 currentTimeUs = 0;
    if (previous.deviceTimestampUs > 0 && current.deviceTimestampUs > 0)
    {
        previousTimeUs = previous.deviceTimestampUs;
        currentTimeUs = current.deviceTimestampUs;
    }
    else if (previous.recordTimestampUs > 0 && current.recordTimestampUs > 0)
    {
        previousTimeUs = previous.recordTimestampUs;
        currentTimeUs = current.recordTimestampUs;
    }
    else
    {
        return false;
    }
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
