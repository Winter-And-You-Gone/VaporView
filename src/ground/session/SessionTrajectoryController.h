#pragma once

#include "ground/SessionData.h"

#include <QVector>

namespace VaporView::Ground
{

struct SessionTrajectoryFocus
{
    bool valid = false;
    int trackPointIndex = -1;
    int csvRow = -1;
    int waveformFrameIndex = -1;
    quint64 timestampUs = 0;
};

struct SessionTimelineRange
{
    bool valid = false;
    int startIndex = 0;
    int count = 0;
};

class SessionTrajectoryController final
{
public:
    void clear();
    void setTrackData(QVector<SessionTrackPoint>&& points, const SessionTrackStats& stats);
    void attachWaveformPeaks(const QVector<quint64>& waveformTimestampsUs,
                             const QVector<float>& waveformPeakValues);

    bool hasTrack() const;
    const QVector<SessionTrackPoint>& trackPoints() const;
    const SessionTrackStats& trackStats() const;
    SessionTrajectoryFocus focusForPoint(int trackPointIndex) const;
    SessionTimelineRange sensorRangeForWaveformRange(
        const QVector<quint64>& sensorTimestampsUs,
        const QVector<quint64>& waveformTimestampsUs,
        int startFrameIndex,
        int visibleFrameCount) const;

private:
    QVector<SessionTrackPoint> track_points_;
    SessionTrackStats track_stats_;
};

}  // namespace VaporView::Ground
