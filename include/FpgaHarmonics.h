#pragma once

#include <cmath>
#include <limits>
#include "FpgaWaveformAssembler.h"

namespace VaporView::FpgaWave
{

// H1 is a magnitude reference. Invalid references leave a gap, never a zero peak.
inline double normalizedSecondHarmonic(double h1, double h2)
{
    if (!std::isfinite(h1) || h1 <= 0.0 || !std::isfinite(h2))
        return std::numeric_limits<double>::quiet_NaN();
    const double ratio = h2 / h1;
    return std::isfinite(ratio) ? ratio : std::numeric_limits<double>::quiet_NaN();
}

inline double normalizedCyclePeak(const CompletedStream& stream)
{
    double peak = std::numeric_limits<double>::quiet_NaN();
    if (!stream.complete || stream.partial || stream.overflow || stream.continuityError ||
        stream.preview || stream.dliaPoints.size() != stream.totalPoints) return peak;
    for (const auto& p : stream.dliaPoints) {
        const double h1 = p.hasHarmonics ? double(p.h1) : std::hypot(double(p.i1), double(p.q1));
        const double h2 = p.hasHarmonics ? double(p.h2) : std::hypot(double(p.i2), double(p.q2));
        const double value = normalizedSecondHarmonic(h1, h2);
        if (std::isfinite(value) && (!std::isfinite(peak) || value > peak)) peak = value;
    }
    return peak;
}

}  // namespace VaporView::FpgaWave
