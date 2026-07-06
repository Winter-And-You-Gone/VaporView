#pragma once

#include <QtCore/QString>
#include <QtGlobal>
#include <cmath>
#include <limits>

namespace VaporView::Geo {

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

    bool hasQuaternion() const
    {
        if (!std::isfinite(quatW)
            || !std::isfinite(quatX)
            || !std::isfinite(quatY)
            || !std::isfinite(quatZ))
        {
            return false;
        }
        const double norm2 = quatW * quatW + quatX * quatX + quatY * quatY + quatZ * quatZ;
        return norm2 > 1e-12;
    }
};

} // namespace VaporView::Geo
