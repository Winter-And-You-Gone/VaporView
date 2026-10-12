#pragma once

#include <QtGlobal>
#include <algorithm>

namespace VaporView::Ground::Widgets
{

inline constexpr int kSegmentedSwitchAnimationDurationMs = 480;
inline constexpr int kSegmentedSwitchReducedMotionDurationMs = 150;

struct SegmentedSwitchMotion
{
    qreal position;
    qreal scale_x;
    qreal scale_y;
};

// Shared launch, travel, overshoot and rebound for capsule selection thumbs.
inline SegmentedSwitchMotion sampleSegmentedSwitchMotion(
    qreal progress, qreal start, qreal target, qreal startScaleX, qreal startScaleY,
    qreal overshoot, bool reducedMotion)
{
    const auto smooth = [](qreal value) {
        const qreal t = std::clamp(value, 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    };
    const auto easeOut = [](qreal value) {
        const qreal t = 1.0 - std::clamp(value, 0.0, 1.0);
        return 1.0 - t * t * t;
    };
    const auto mix = [](qreal from, qreal to, qreal t) {
        return from + (to - from) * std::clamp(t, 0.0, 1.0);
    };
    const auto stage = [&](qreal begin, qreal end, qreal from, qreal to) {
        return mix(from, to, smooth((progress - begin) / (end - begin)));
    };
    progress = std::clamp(progress, 0.0, 1.0);
    if (reducedMotion)
        return {mix(start, target, easeOut(progress)), 1.0, 1.0};

    const qreal direction = target >= start ? 1.0 : -1.0;
    const qreal launch = start + (target - start) * 0.04;
    const qreal arrival = target + direction * overshoot;
    const qreal rebound = target - direction * overshoot * 0.24;
    SegmentedSwitchMotion result;
    if (progress < 0.14)
        result.position = mix(start, launch, smooth(progress / 0.14));
    else if (progress < 0.62)
        result.position = mix(launch, arrival, easeOut((progress - 0.14) / 0.48));
    else if (progress < 0.80)
        result.position = mix(arrival, rebound, smooth((progress - 0.62) / 0.18));
    else
        result.position = mix(rebound, target, smooth((progress - 0.80) / 0.20));

    if (progress < 0.16)
    {
        result.scale_x = stage(0.0, 0.16, startScaleX, 1.10);
        result.scale_y = stage(0.0, 0.16, startScaleY, 0.94);
    }
    else if (progress < 0.56)
    {
        result.scale_x = stage(0.16, 0.56, 1.10, 1.02);
        result.scale_y = stage(0.16, 0.56, 0.94, 0.99);
    }
    else if (progress < 0.66)
    {
        result.scale_x = stage(0.56, 0.66, 1.02, 0.96);
        result.scale_y = stage(0.56, 0.66, 0.99, 1.025);
    }
    else if (progress < 0.82)
    {
        result.scale_x = stage(0.66, 0.82, 0.96, 1.025);
        result.scale_y = stage(0.66, 0.82, 1.025, 0.985);
    }
    else
    {
        result.scale_x = stage(0.82, 1.0, 1.025, 1.0);
        result.scale_y = stage(0.82, 1.0, 0.985, 1.0);
    }
    return result;
}

} // namespace VaporView::Ground::Widgets
