#include "geo/CoordinateTransform.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace VaporView::Geo {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;
constexpr double kWgs84SemiMajorAxisM = 6378137.0;
constexpr double kWgs84Flattening = 1.0 / 298.257223563;
constexpr double kWgs84SemiMinorAxisM = kWgs84SemiMajorAxisM * (1.0 - kWgs84Flattening);
constexpr double kFirstEccentricitySq =
    kWgs84Flattening * (2.0 - kWgs84Flattening);
constexpr double kSecondEccentricitySq =
    (kWgs84SemiMajorAxisM * kWgs84SemiMajorAxisM
     - kWgs84SemiMinorAxisM * kWgs84SemiMinorAxisM)
    / (kWgs84SemiMinorAxisM * kWgs84SemiMinorAxisM);

bool finite(double value)
{
    return std::isfinite(value);
}

bool validLlh(const LlhPoint& point)
{
    return finite(point.latDeg)
        && finite(point.lonDeg)
        && finite(point.heightM)
        && point.latDeg >= -90.0
        && point.latDeg <= 90.0
        && point.lonDeg >= -180.0
        && point.lonDeg <= 180.0;
}

double clampLatitudeRad(double latRad)
{
    const double limit = 0.5 * kPi;
    return std::clamp(latRad, -limit, limit);
}

} // namespace

EcefPoint llhToEcef(const LlhPoint& point)
{
    if (!validLlh(point))
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        return {nan, nan, nan};
    }

    const double latRad = point.latDeg * kDegToRad;
    const double lonRad = point.lonDeg * kDegToRad;
    const double sinLat = std::sin(latRad);
    const double cosLat = std::cos(latRad);
    const double sinLon = std::sin(lonRad);
    const double cosLon = std::cos(lonRad);
    const double primeVerticalRadius =
        kWgs84SemiMajorAxisM / std::sqrt(1.0 - kFirstEccentricitySq * sinLat * sinLat);

    const double x = (primeVerticalRadius + point.heightM) * cosLat * cosLon;
    const double y = (primeVerticalRadius + point.heightM) * cosLat * sinLon;
    const double z =
        (primeVerticalRadius * (1.0 - kFirstEccentricitySq) + point.heightM) * sinLat;
    return {x, y, z};
}

LlhPoint ecefToLlh(const EcefPoint& point)
{
    if (!finite(point.xM) || !finite(point.yM) || !finite(point.zM))
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        return {nan, nan, nan};
    }

    const double p = std::hypot(point.xM, point.yM);
    if (p < 1.0e-12)
    {
        const double latRad = point.zM >= 0.0 ? 0.5 * kPi : -0.5 * kPi;
        const double height = std::fabs(point.zM) - kWgs84SemiMinorAxisM;
        return {latRad * kRadToDeg, 0.0, height};
    }

    const double theta =
        std::atan2(point.zM * kWgs84SemiMajorAxisM, p * kWgs84SemiMinorAxisM);
    const double sinTheta = std::sin(theta);
    const double cosTheta = std::cos(theta);
    const double lonRad = std::atan2(point.yM, point.xM);
    const double latRad = clampLatitudeRad(std::atan2(
        point.zM + kSecondEccentricitySq * kWgs84SemiMinorAxisM * sinTheta * sinTheta * sinTheta,
        p - kFirstEccentricitySq * kWgs84SemiMajorAxisM * cosTheta * cosTheta * cosTheta));
    const double sinLat = std::sin(latRad);
    const double primeVerticalRadius =
        kWgs84SemiMajorAxisM / std::sqrt(1.0 - kFirstEccentricitySq * sinLat * sinLat);
    const double height = p / std::cos(latRad) - primeVerticalRadius;

    return {latRad * kRadToDeg, lonRad * kRadToDeg, height};
}

EnuPoint nedToEnu(const NedPoint& point)
{
    return {point.eastM, point.northM, -point.downM};
}

NedPoint enuToNed(const EnuPoint& point)
{
    return {point.northM, point.eastM, -point.upM};
}

LocalTangentPlane::LocalTangentPlane(const LlhPoint& originLlh)
{
    if (!validLlh(originLlh))
    {
        return;
    }

    originLlh_ = originLlh;
    originEcef_ = llhToEcef(originLlh_);
    const double latRad = originLlh_.latDeg * kDegToRad;
    const double lonRad = originLlh_.lonDeg * kDegToRad;
    sinLat_ = std::sin(latRad);
    cosLat_ = std::cos(latRad);
    sinLon_ = std::sin(lonRad);
    cosLon_ = std::cos(lonRad);
    valid_ = finite(originEcef_.xM) && finite(originEcef_.yM) && finite(originEcef_.zM);
}

LocalTangentPlane::LocalTangentPlane(const NavSample& originSample)
    : LocalTangentPlane(LlhPoint{originSample.latDeg, originSample.lonDeg, originSample.heightM})
{
}

bool LocalTangentPlane::isValid() const
{
    return valid_;
}

LlhPoint LocalTangentPlane::originLlh() const
{
    return originLlh_;
}

EcefPoint LocalTangentPlane::originEcef() const
{
    return originEcef_;
}

EnuPoint LocalTangentPlane::ecefToEnu(const EcefPoint& point) const
{
    if (!valid_ || !finite(point.xM) || !finite(point.yM) || !finite(point.zM))
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        return {nan, nan, nan};
    }

    const double dx = point.xM - originEcef_.xM;
    const double dy = point.yM - originEcef_.yM;
    const double dz = point.zM - originEcef_.zM;

    const double east = -sinLon_ * dx + cosLon_ * dy;
    const double north = -sinLat_ * cosLon_ * dx - sinLat_ * sinLon_ * dy + cosLat_ * dz;
    const double up = cosLat_ * cosLon_ * dx + cosLat_ * sinLon_ * dy + sinLat_ * dz;
    return {east, north, up};
}

EcefPoint LocalTangentPlane::enuToEcef(const EnuPoint& point) const
{
    if (!valid_ || !finite(point.eastM) || !finite(point.northM) || !finite(point.upM))
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        return {nan, nan, nan};
    }

    const double dx =
        -sinLon_ * point.eastM
        - sinLat_ * cosLon_ * point.northM
        + cosLat_ * cosLon_ * point.upM;
    const double dy =
        cosLon_ * point.eastM
        - sinLat_ * sinLon_ * point.northM
        + cosLat_ * sinLon_ * point.upM;
    const double dz = cosLat_ * point.northM + sinLat_ * point.upM;
    return {originEcef_.xM + dx, originEcef_.yM + dy, originEcef_.zM + dz};
}

EnuPoint LocalTangentPlane::llhToEnu(const LlhPoint& point) const
{
    return ecefToEnu(llhToEcef(point));
}

LlhPoint LocalTangentPlane::enuToLlh(const EnuPoint& point) const
{
    return ecefToLlh(enuToEcef(point));
}

NedPoint LocalTangentPlane::llhToNed(const LlhPoint& point) const
{
    return enuToNed(llhToEnu(point));
}

LlhPoint LocalTangentPlane::nedToLlh(const NedPoint& point) const
{
    return enuToLlh(nedToEnu(point));
}

EnuPoint navSampleToEnu(const NavSample& sample, const LocalTangentPlane& localFrame)
{
    if (sample.hasNed())
    {
        return nedToEnu({sample.nedNM, sample.nedEM, sample.nedDM});
    }
    if (sample.hasLlh())
    {
        return localFrame.llhToEnu({sample.latDeg, sample.lonDeg, sample.heightM});
    }
    if (finite(sample.ecefXM) && finite(sample.ecefYM) && finite(sample.ecefZM))
    {
        return localFrame.ecefToEnu({sample.ecefXM, sample.ecefYM, sample.ecefZM});
    }

    const double nan = std::numeric_limits<double>::quiet_NaN();
    return {nan, nan, nan};
}

NedPoint navSampleToNed(const NavSample& sample, const LocalTangentPlane& localFrame)
{
    return enuToNed(navSampleToEnu(sample, localFrame));
}

} // namespace VaporView::Geo
