#include "ground/session/SessionTrajectoryController.h"

#include "ground/session/SessionIndex.h"
#include "ground/session/SessionTimelineModel.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace VaporView::Ground
{

void SessionTrajectoryController::clear()
{
    track_points_.clear();
    track_stats_ = SessionTrackStats();
}

void SessionTrajectoryController::setTrackData(
    QVector<SessionTrackPoint>&& points,
    const SessionTrackStats& stats)
{
    track_points_ = std::move(points);
    track_stats_ = stats;
}

void SessionTrajectoryController::attachWaveformPeaks(
    const QVector<quint64>& waveformTimestampsUs,
    const QVector<float>& waveformPeakValues)
{
    Session::SessionTimelineModel::attachWaveformPeaks(
        track_points_,
        waveformTimestampsUs,
        waveformPeakValues);
}

bool SessionTrajectoryController::hasTrack() const
{
    return !track_points_.isEmpty();
}

const QVector<SessionTrackPoint>& SessionTrajectoryController::trackPoints() const
{
    return track_points_;
}

const SessionTrackStats& SessionTrajectoryController::trackStats() const
{
    return track_stats_;
}

SessionTrajectoryFocus SessionTrajectoryController::focusForPoint(int trackPointIndex) const
{
    SessionTrajectoryFocus focus;
    if (trackPointIndex < 0 || trackPointIndex >= track_points_.size())
    {
        return focus;
    }

    const SessionTrackPoint& point = track_points_.at(trackPointIndex);
    focus.valid = true;
    focus.trackPointIndex = trackPointIndex;
    focus.csvRow = point.csv_row;
    focus.waveformFrameIndex = point.has_waveform_match ? point.waveform_frame_index : -1;
    focus.timestampUs = point.timestamp_us;
    return focus;
}

SessionTimelineRange SessionTrajectoryController::sensorRangeForWaveformRange(
    const QVector<quint64>& sensorTimestampsUs,
    const QVector<quint64>& waveformTimestampsUs,
    int startFrameIndex,
    int visibleFrameCount) const
{
    SessionTimelineRange range;
    if (sensorTimestampsUs.isEmpty() || waveformTimestampsUs.isEmpty() || visibleFrameCount <= 0)
    {
        return range;
    }

    const int totalFrames = static_cast<int>(waveformTimestampsUs.size());
    const int clampedStart = std::clamp(startFrameIndex, 0, std::max(0, totalFrames - 1));
    const int clampedEnd = std::clamp(
        clampedStart + visibleFrameCount - 1,
        clampedStart,
        totalFrames - 1);
    const int startSensorRow = Session::closestTimestampIndex(
        sensorTimestampsUs,
        waveformTimestampsUs.at(clampedStart));
    const int endSensorRow = Session::closestTimestampIndex(
        sensorTimestampsUs,
        waveformTimestampsUs.at(clampedEnd));
    if (startSensorRow < 0 || endSensorRow < 0)
    {
        return range;
    }

    range.valid = true;
    range.startIndex = std::min(startSensorRow, endSensorRow);
    range.count = std::max(1, std::abs(endSensorRow - startSensorRow) + 1);
    return range;
}

}  // namespace VaporView::Ground
