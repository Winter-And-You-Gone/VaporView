#include "map3d/AircraftHeading.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

constexpr double kPi = 3.14159265358979323846;

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool near(double actual, double expected, double tolerance = 1e-6)
{
    return std::abs(actual - expected) <= tolerance;
}

VaporView::Geo::NavSample sampleWithYaw(double yawDeg)
{
    VaporView::Geo::NavSample sample;
    sample.yawDeg = yawDeg;
    return sample;
}

VaporView::Geo::NavSample sampleWithHeadingQuaternion(double headingDeg)
{
    VaporView::Geo::NavSample sample;
    const double halfHeadingRad = headingDeg * kPi / 360.0;
    sample.quatW = std::cos(halfHeadingRad);
    sample.quatX = 0.0;
    sample.quatY = 0.0;
    sample.quatZ = std::sin(halfHeadingRad);
    return sample;
}

} // namespace

int main()
{
    require(near(VaporView::Map3D::aircraftHeadingDeg(sampleWithYaw(90.0)), 90.0),
            "Euler yaw is used when no quaternion is available");
    require(near(VaporView::Map3D::aircraftHeadingDeg(sampleWithYaw(-90.0)), 270.0),
            "Euler yaw is normalized into the 0..360 range");

    VaporView::Geo::NavSample quaternionSample = sampleWithHeadingQuaternion(90.0);
    quaternionSample.yawDeg = 0.0;
    require(near(VaporView::Map3D::aircraftHeadingDeg(quaternionSample), 90.0),
            "quaternion heading takes precedence over stale Euler yaw");

    require(near(VaporView::Map3D::aircraftHeadingDeg(sampleWithHeadingQuaternion(450.0)), 90.0),
            "quaternion heading is normalized into the 0..360 range");

    VaporView::Geo::NavSample emptySample;
    require(near(VaporView::Map3D::aircraftHeadingDeg(emptySample), 0.0),
            "missing attitude defaults to north-up heading");

    return 0;
}
