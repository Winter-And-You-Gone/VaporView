#pragma once

#include "ground/SessionData.h"

#include <QVector>

namespace VaporView::Ground::Session
{

class SessionTimelineModel final
{
public:
    static void attachWaveformPeaks(QVector<SessionTrackPoint>& trackPoints,
                                    const QVector<quint64>& waveformTimestampsUs,
                                    const QVector<float>& waveformPeakValues);
};

}  // namespace VaporView::Ground::Session
