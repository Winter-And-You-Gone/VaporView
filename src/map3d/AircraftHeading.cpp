#include "map3d/AircraftHeading.h"

#include <cmath>

namespace VaporView::Map3D {
namespace {

constexpr double kPi = 3.14159265358979323846;

double normalizeHeadingRad(double headingRad)
{
    while (headingRad < 0.0)
    {
        headingRad += 2.0 * kPi;
    }
    while (headingRad >= 2.0 * kPi)
    {
        headingRad -= 2.0 * kPi;
    }
    return headingRad;
}

} // namespace

double aircraftHeadingRad(const VaporView::Geo::NavSample& sample)
{
    if (sample.hasQuaternion())
    {
        const double norm = std::sqrt(sample.quatW * sample.quatW
                                      + sample.quatX * sample.quatX
                                      + sample.quatY * sample.quatY
                                      + sample.quatZ * sample.quatZ);
        const double w = sample.quatW / norm;
        const double x = sample.quatX / norm;
        const double y = sample.quatY / norm;
        const double z = sample.quatZ / norm;
        const double sinyCosp = 2.0 * (w * z + x * y);
        const double cosyCosp = 1.0 - 2.0 * (y * y + z * z);
        return normalizeHeadingRad(std::atan2(sinyCosp, cosyCosp));
    }

    if (std::isfinite(sample.yawDeg))
    {
        return normalizeHeadingRad(sample.yawDeg * kPi / 180.0);
    }

    return 0.0;
}

double aircraftHeadingDeg(const VaporView::Geo::NavSample& sample)
{
    return aircraftHeadingRad(sample) * 180.0 / kPi;
}

} // namespace VaporView::Map3D
