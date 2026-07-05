#include "geo/TrajectoryQuality.h"

#include <iostream>

namespace {

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

VaporView::Geo::NavSample validSample()
{
    VaporView::Geo::NavSample sample;
    sample.recordTimestampUs = 1000000;
    sample.latDeg = 31.2304;
    sample.lonDeg = 121.4737;
    sample.heightM = 18.0;
    sample.satellites = 12;
    sample.hdop = 0.8;
    sample.fixQuality = VaporView::Geo::FixQuality::Fixed;
    return sample;
}

} // namespace

int main()
{
    auto sample = validSample();
    require(VaporView::Geo::isUsableForDisplay(sample), "valid sample is displayable");

    sample = validSample();
    sample.latDeg = std::numeric_limits<double>::quiet_NaN();
    require(!VaporView::Geo::isUsableForDisplay(sample), "NaN latitude is rejected");

    sample = validSample();
    sample.fixQuality = VaporView::Geo::FixQuality::Invalid;
    require(!VaporView::Geo::isUsableForDisplay(sample), "invalid fix is rejected");

    sample = validSample();
    sample.hdop = 10.0;
    require(!VaporView::Geo::isUsableForDisplay(sample), "large hdop is rejected");

    auto previous = validSample();
    previous.recordTimestampUs = 2000000;
    sample = validSample();
    sample.recordTimestampUs = 1000000;
    require(VaporView::Geo::isLikelyJump(previous, sample), "non-positive dt is jump");

    previous = validSample();
    previous.recordTimestampUs = 1000000;
    previous.nedNM = 0.0;
    previous.nedEM = 0.0;
    previous.nedDM = 0.0;
    sample = validSample();
    sample.recordTimestampUs = 2000000;
    sample.nedNM = 500.0;
    sample.nedEM = 0.0;
    sample.nedDM = 0.0;
    require(VaporView::Geo::isLikelyJump(previous, sample), "unreasonable NED speed is jump");

    previous = validSample();
    previous.recordTimestampUs = 1000000;
    sample = validSample();
    sample.recordTimestampUs = 2000000;
    sample.latDeg = 45.0;
    sample.lonDeg = 90.0;
    require(!VaporView::Geo::isLikelyJump(previous, sample), "missing NED does not reject LLH-only samples");

    return 0;
}
