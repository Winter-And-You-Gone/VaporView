#include "ground/session/SessionTimelineModel.h"

#include "ground/session/SessionIndex.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace VaporView::Ground::Session
{
namespace
{

quint64 midpointTimestamp(quint64 first, quint64 second)
{
    return first <= second
        ? first + (second - first) / 2ULL
        : second + (first - second) / 2ULL;
}

quint64 addClampedUs(quint64 timestampUs, quint64 deltaUs)
{
    const quint64 maximum = std::numeric_limits<quint64>::max();
    return deltaUs > maximum - timestampUs ? maximum : timestampUs + deltaUs;
}

void resetWaveformMatch(SessionTrackPoint& point)
{
    point.peak_value = 0.0f;
    point.has_peak_value = false;
    point.waveform_frame_index = -1;
    point.waveform_timestamp_us = 0;
    point.waveform_delta_us = 0;
    point.has_waveform_match = false;
}

}  // namespace

void SessionTimelineModel::attachWaveformPeaks(
    QVector<SessionTrackPoint>& trackPoints,
    const QVector<quint64>& waveformTimestampsUs,
    const QVector<float>& waveformPeakValues)
{
    QVector<quint64> trackTimestampsUs;
    trackTimestampsUs.reserve(trackPoints.size());
    for (const SessionTrackPoint& point : std::as_const(trackPoints))
    {
        trackTimestampsUs.push_back(point.timestamp_us);
    }

    const double waveformRateHz = measuredRateHz(waveformTimestampsUs);
    const double trackRateHz = measuredRateHz(trackTimestampsUs);
    const bool averageHighRatePeaks =
        waveformRateHz > 0.0 &&
        trackRateHz > 0.0 &&
        waveformRateHz > trackRateHz * 1.05;

    QVector<int> previousTrackTimestampIndex(trackPoints.size(), -1);
    QVector<int> nextTrackTimestampIndex(trackPoints.size(), -1);
    if (averageHighRatePeaks)
    {
        int previousIndex = -1;
        for (int index = 0; index < trackPoints.size(); ++index)
        {
            previousTrackTimestampIndex[index] = previousIndex;
            if (trackPoints.at(index).timestamp_us > 0)
            {
                previousIndex = index;
            }
        }

        int nextIndex = -1;
        for (int index = trackPoints.size() - 1; index >= 0; --index)
        {
            nextTrackTimestampIndex[index] = nextIndex;
            if (trackPoints.at(index).timestamp_us > 0)
            {
                nextIndex = index;
            }
        }
    }

    for (int trackIndex = 0; trackIndex < trackPoints.size(); ++trackIndex)
    {
        SessionTrackPoint& point = trackPoints[trackIndex];
        resetWaveformMatch(point);
        if (point.timestamp_us == 0)
        {
            continue;
        }

        const int peakIndex = closestTimestampIndex(waveformTimestampsUs, point.timestamp_us);
        if (peakIndex < 0 || peakIndex >= waveformPeakValues.size())
        {
            continue;
        }

        point.waveform_frame_index = peakIndex;
        if (peakIndex < waveformTimestampsUs.size())
        {
            point.waveform_timestamp_us = waveformTimestampsUs.at(peakIndex);
            point.waveform_delta_us = point.waveform_timestamp_us >= point.timestamp_us
                ? point.waveform_timestamp_us - point.timestamp_us
                : point.timestamp_us - point.waveform_timestamp_us;
            point.has_waveform_match = true;
        }

        float peakValue = waveformPeakValues.at(peakIndex);
        if (averageHighRatePeaks)
        {
            const int previousIndex = previousTrackTimestampIndex.at(trackIndex);
            const int nextIndex = nextTrackTimestampIndex.at(trackIndex);
            quint64 lowerBoundUs = 0;
            quint64 upperBoundUs = 0;
            bool hasLowerBound = false;
            bool hasUpperBound = false;

            if (previousIndex >= 0)
            {
                const quint64 previousUs = trackPoints.at(previousIndex).timestamp_us;
                if (previousUs > 0 && previousUs < point.timestamp_us)
                {
                    lowerBoundUs = midpointTimestamp(previousUs, point.timestamp_us);
                    hasLowerBound = true;
                }
            }
            if (!hasLowerBound && nextIndex >= 0)
            {
                const quint64 nextUs = trackPoints.at(nextIndex).timestamp_us;
                if (nextUs > point.timestamp_us)
                {
                    const quint64 halfInterval = (nextUs - point.timestamp_us) / 2ULL;
                    lowerBoundUs = point.timestamp_us > halfInterval
                        ? point.timestamp_us - halfInterval
                        : 0;
                    hasLowerBound = true;
                }
            }

            if (nextIndex >= 0)
            {
                const quint64 nextUs = trackPoints.at(nextIndex).timestamp_us;
                if (nextUs > point.timestamp_us)
                {
                    upperBoundUs = midpointTimestamp(point.timestamp_us, nextUs);
                    hasUpperBound = true;
                }
            }
            if (!hasUpperBound && previousIndex >= 0)
            {
                const quint64 previousUs = trackPoints.at(previousIndex).timestamp_us;
                if (previousUs > 0 && previousUs < point.timestamp_us)
                {
                    const quint64 halfInterval = (point.timestamp_us - previousUs) / 2ULL;
                    upperBoundUs = addClampedUs(point.timestamp_us, halfInterval);
                    hasUpperBound = true;
                }
            }

            if (hasLowerBound && hasUpperBound && lowerBoundUs < upperBoundUs)
            {
                const auto firstIt = std::lower_bound(
                    waveformTimestampsUs.cbegin(), waveformTimestampsUs.cend(), lowerBoundUs);
                const auto lastIt = std::lower_bound(
                    waveformTimestampsUs.cbegin(), waveformTimestampsUs.cend(), upperBoundUs);
                const int firstPeakIndex = static_cast<int>(
                    std::distance(waveformTimestampsUs.cbegin(), firstIt));
                const int lastPeakIndex = std::min(
                    static_cast<int>(std::distance(waveformTimestampsUs.cbegin(), lastIt)),
                    static_cast<int>(waveformPeakValues.size()));
                double sum = 0.0;
                int count = 0;
                for (int valueIndex = firstPeakIndex; valueIndex < lastPeakIndex; ++valueIndex)
                {
                    const float candidate = waveformPeakValues.at(valueIndex);
                    if (std::isfinite(candidate))
                    {
                        sum += static_cast<double>(candidate);
                        ++count;
                    }
                }
                if (count > 0)
                {
                    peakValue = static_cast<float>(sum / static_cast<double>(count));
                }
            }
        }

        if (std::isfinite(peakValue))
        {
            point.peak_value = peakValue;
            point.has_peak_value = true;
        }
    }
}

}  // namespace VaporView::Ground::Session
