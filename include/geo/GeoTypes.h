#pragma once

#include <QtCore/QString>
#include <QtGlobal>
#include <cmath>
#include <limits>

namespace VaporView::Geo {

inline bool isPlausibleEcef(double xM, double yM, double zM)
{
    if (!std::isfinite(xM) || !std::isfinite(yM) || !std::isfinite(zM))
    {
        return false;
    }

    // VaporView tracks surface and airborne navigation. Accept a generous
    // shell around the WGS84 ellipsoid while rejecting zero-filled fields and
    // corrupted device packets that place the camera far away from Earth.
    constexpr double kMinimumRadiusM = 5'000'000.0;
    constexpr double kMaximumRadiusM = 8'000'000.0;
    const double radiusM = std::hypot(xM, yM, zM);
    return radiusM >= kMinimumRadiusM && radiusM <= kMaximumRadiusM;
}

enum class FixQuality {
    Unknown = 0,
    Invalid,
    Single,
    Dgps,
    Float,
    Fixed
};

enum class HeightReference {
    Unknown = 0,
    Wgs84Ellipsoid,
    MeanSeaLevel,
    Egm2008,
    LocalNed,

    // Compatibility aliases for older session readers and UI code.
    Ellipsoid = Wgs84Ellipsoid,
    Local = LocalNed,
    Dem = Egm2008
};

struct NavSample {
    qint64 recordTimestampUs = 0;
    qint64 deviceTimestampUs = 0;

    double latDeg = std::numeric_limits<double>::quiet_NaN();
    double lonDeg = std::numeric_limits<double>::quiet_NaN();
    double heightM = std::numeric_limits<double>::quiet_NaN();

    double ecefXM = std::numeric_limits<double>::quiet_NaN();
    double ecefYM = std::numeric_limits<double>::quiet_NaN();
    double ecefZM = std::numeric_limits<double>::quiet_NaN();

    double nedNM = std::numeric_limits<double>::quiet_NaN();
    double nedEM = std::numeric_limits<double>::quiet_NaN();
    double nedDM = std::numeric_limits<double>::quiet_NaN();

    double velNMps = std::numeric_limits<double>::quiet_NaN();
    double velEMps = std::numeric_limits<double>::quiet_NaN();
    double velDMps = std::numeric_limits<double>::quiet_NaN();

    double rollDeg = std::numeric_limits<double>::quiet_NaN();
    double pitchDeg = std::numeric_limits<double>::quiet_NaN();
    double yawDeg = std::numeric_limits<double>::quiet_NaN();

    int satellites = 0;
    double hdop = std::numeric_limits<double>::quiet_NaN();
    double vdop = std::numeric_limits<double>::quiet_NaN();
    double diffAgeS = std::numeric_limits<double>::quiet_NaN();

    FixQuality fixQuality = FixQuality::Unknown;
    HeightReference heightReference = HeightReference::Unknown;

    double quatW = std::numeric_limits<double>::quiet_NaN();
    double quatX = std::numeric_limits<double>::quiet_NaN();
    double quatY = std::numeric_limits<double>::quiet_NaN();
    double quatZ = std::numeric_limits<double>::quiet_NaN();

    bool hasLlh() const
    {
        return std::isfinite(latDeg)
            && std::isfinite(lonDeg)
            && std::isfinite(heightM)
            && latDeg >= -90.0 && latDeg <= 90.0
            && lonDeg >= -180.0 && lonDeg <= 180.0;
    }

    bool hasNed() const
    {
        return std::isfinite(nedNM)
            && std::isfinite(nedEM)
            && std::isfinite(nedDM);
    }

    bool hasEcef() const
    {
        return isPlausibleEcef(ecefXM, ecefYM, ecefZM);
    }

    bool hasQuaternion() const
    {
        if (!std::isfinite(quatW)
            || !std::isfinite(quatX)
            || !std::isfinite(quatY)
            || !std::isfinite(quatZ))
        {
            return false;
        }
        const double norm = quaternionNorm();
        return std::isfinite(norm) && norm > 1e-6;
    }

    double quaternionNorm() const
    {
        const double scale = std::fmax(std::fmax(std::fabs(quatW), std::fabs(quatX)),
                                       std::fmax(std::fabs(quatY), std::fabs(quatZ)));
        if (!std::isfinite(scale) || scale <= 0.0)
        {
            return std::numeric_limits<double>::quiet_NaN();
        }
        const double w = quatW / scale;
        const double x = quatX / scale;
        const double y = quatY / scale;
        const double z = quatZ / scale;
        const double norm = scale * std::sqrt(w * w + x * x + y * y + z * z);
        return std::isfinite(norm) ? norm : std::numeric_limits<double>::quiet_NaN();
    }
};

} // namespace VaporView::Geo
