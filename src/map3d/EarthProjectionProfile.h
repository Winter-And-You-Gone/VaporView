#pragma once

#include <algorithm>
#include <cmath>

namespace VaporView::Map3D {

inline constexpr double kEarthMaxInteractivePitchDeg = -4.0;
inline constexpr double kEarthProjectionNearPlaneM = 1.0;
inline constexpr double kEarthProjectionDefaultFarPlaneM = 50000000.0;
inline constexpr double kEarthProjectionLocalRangeLimitM = 1000000.0;
inline constexpr double kEarthProjectionLocalFarMultiplier = 18.0;
inline constexpr double kEarthProjectionLowAngleFarMultiplier = 6.0;
inline constexpr double kEarthProjectionLocalMinFarPlaneM = 20000.0;
inline constexpr double kEarthProjectionLowAngleMinFarPlaneM = 8000.0;
inline constexpr double kEarthProjectionLocalMaxFarPlaneM = 850000.0;
inline constexpr double kEarthProjectionLowAngleMaxFarPlaneM = 120000.0;
inline constexpr float kEarthSmallFeatureCullPixels = 3.0f;
inline constexpr float kEarthLowAngleSmallFeatureCullPixels = 9.0f;
inline constexpr float kEarthCullLODScale = 1.0f;
inline constexpr float kEarthLowAngleCullLODScale = 2.5f;
inline constexpr double kEarthLowAngleStartPitchDeg = -35.0;

struct EarthProjectionProfile {
    double farPlaneM = kEarthProjectionDefaultFarPlaneM;
    float smallFeatureCullPixels = kEarthSmallFeatureCullPixels;
    float lodScale = kEarthCullLODScale;
};

inline double blend(double lowAngle0, double lowAngle1, double factor)
{
    return lowAngle0 + (lowAngle1 - lowAngle0) * factor;
}

inline double lowAngleFactor(double pitchDeg)
{
    if (!std::isfinite(pitchDeg))
    {
        return 0.0;
    }
    return std::clamp((pitchDeg - kEarthLowAngleStartPitchDeg)
                          / (kEarthMaxInteractivePitchDeg - kEarthLowAngleStartPitchDeg),
                      0.0,
                      1.0);
}

inline EarthProjectionProfile earthProjectionProfile(double cameraRangeM, double pitchDeg)
{
    if (!std::isfinite(cameraRangeM) || cameraRangeM <= 0.0)
    {
        return {};
    }

    // Keep the entire globe within the frustum at orbital distances.
    if (cameraRangeM > kEarthProjectionLocalRangeLimitM)
    {
        EarthProjectionProfile profile;
        profile.farPlaneM = cameraRangeM + 2.0 * 6378137.0;
        return profile;
    }

    const double lowAngle = lowAngleFactor(pitchDeg);
    const double farMultiplier =
        blend(kEarthProjectionLocalFarMultiplier,
              kEarthProjectionLowAngleFarMultiplier,
              lowAngle);
    const double minFarPlaneM =
        blend(kEarthProjectionLocalMinFarPlaneM,
              kEarthProjectionLowAngleMinFarPlaneM,
              lowAngle);
    const double maxFarPlaneM =
        blend(kEarthProjectionLocalMaxFarPlaneM,
              kEarthProjectionLowAngleMaxFarPlaneM,
              lowAngle);

    EarthProjectionProfile profile;
    // Detail limits must never clip the focal point as the camera zooms out.
    profile.farPlaneM = (std::max)(cameraRangeM * 1.25,
        std::clamp(cameraRangeM * farMultiplier, minFarPlaneM, maxFarPlaneM));
    profile.smallFeatureCullPixels =
        static_cast<float>(blend(static_cast<double>(kEarthSmallFeatureCullPixels),
                                 static_cast<double>(kEarthLowAngleSmallFeatureCullPixels),
                                 lowAngle));
    profile.lodScale =
        static_cast<float>(blend(static_cast<double>(kEarthCullLODScale),
                                 static_cast<double>(kEarthLowAngleCullLODScale),
                                 lowAngle));
    return profile;
}

} // namespace VaporView::Map3D
