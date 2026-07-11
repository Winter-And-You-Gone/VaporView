#pragma once

#include "geo/GeoTypes.h"

#include <QtCore/QString>

#include <vector>

namespace VaporView::Geo {

class TrajectoryReplay {
public:
    void clear();
    void setSamples(std::vector<NavSample> samples);

    bool hasSamples() const;
    int sampleCount() const;
    int currentIndex() const;
    bool isPlaying() const;
    qint64 startTimestampUs() const;
    qint64 endTimestampUs() const;
    qint64 durationUs() const;
    qint64 elapsedUs() const;

    double speed() const;
    void setSpeed(double speed);

    void play();
    void pause();
    void stop();
    bool seek(int index);
    bool seekElapsedUs(qint64 elapsedUs);
    bool stepForward();
    bool stepByElapsedUs(qint64 deltaUs);

    const NavSample* currentSample() const;
    std::vector<NavSample> visibleSamples() const;
    const std::vector<NavSample>& samples() const;

    int intervalMs() const;
    static int intervalMsForSpeed(double speed);
    static double speedFromText(const QString& text, double fallback = 1.0);

private:
    static qint64 timestampUsForSample(const NavSample& sample);
    void rebuildTimelineCache();
    bool hasTimestampTimeline() const;
    qint64 fallbackDurationUs() const;
    qint64 sampleTimestampUs(int index) const;

    std::vector<NavSample> samples_;
    bool has_timestamp_timeline_ = false;
    qint64 start_timestamp_us_ = 0;
    qint64 end_timestamp_us_ = 0;
    qint64 duration_us_ = 0;
    int current_index_ = -1;
    qint64 current_elapsed_us_ = 0;
    double speed_ = 1.0;
    bool playing_ = false;
};

} // namespace VaporView::Geo
