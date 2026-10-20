#pragma once

#include <cmath>
#include <limits>

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

}  // namespace VaporView::FpgaWave
