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

// Shared full-scan reducer for live cycles and historical harmonic payloads.
// Quality bit values are the persisted HarmonicQuality v1 contract.
template<class SampleAt>
NormalizedFeature normalizedFeature(quint32 count, quint32 quality, SampleAt sampleAt)
{
    NormalizedFeature result;
    result.quality = quality;
    double sum = 0, squares = 0;
    for (quint32 i = 0; i < count; ++i) {
        const auto pair = sampleAt(i);
        const double value = normalizedSecondHarmonic(pair.first, pair.second);
        if (!std::isfinite(value)) { result.quality |= 1; continue; }
        if (!result.validPoints || value > result.peak) { result.peak = value; result.peakIndex = i; }
        if (!result.validPoints || value < result.minimum) result.minimum = value;
        sum += value; squares += value * value; ++result.validPoints;
    }
    if (!result.validPoints) result.quality |= 2;
    else { result.mean = sum / result.validPoints; result.rms = std::sqrt(squares / result.validPoints); }
    return result;
}

inline NormalizedFeature normalizedCycleFeature(const CompletedStream& stream)
{
    quint32 quality = 0;
    if (!stream.complete || stream.partial || stream.preview || stream.dliaPoints.size() != stream.totalPoints) quality |= 4;
    if (stream.overflow) quality |= 8;
    if (stream.continuityError) quality |= 16;
    return normalizedFeature(quality ? 0 : stream.totalPoints, quality, [&stream](quint32 i) {
        const auto& p = stream.dliaPoints[i];
        return std::pair<double, double>{
            stream.format == 1 ? std::hypot(double(p.i1), double(p.q1)) : double(p.h1),
            stream.format == 1 ? std::hypot(double(p.i2), double(p.q2)) : double(p.h2)};
    });
}

inline double normalizedCyclePeak(const CompletedStream& stream)
{
    if (!stream.complete || stream.partial || stream.overflow || stream.continuityError ||
        stream.preview || stream.dliaPoints.size() != stream.totalPoints) return std::numeric_limits<double>::quiet_NaN();
    return stream.normalizedFeature ? stream.normalizedFeature->peak : normalizedCycleFeature(stream).peak;
}

}  // namespace VaporView::FpgaWave
