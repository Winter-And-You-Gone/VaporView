#pragma once

#include "geo/GeoTypes.h"

#include <QtCore/QString>

#include <chrono>
#include <memory>
#include <vector>

namespace VaporView::Geo {

class TrajectoryReplay {
public:
    using Duration = std::chrono::microseconds;

    void clear();
    void setSamples(std::vector<NavSample> samples);
    void setSamples(std::shared_ptr<const std::vector<NavSample>> samples);

    bool hasSamples() const;
    int sampleCount() const;
    int currentIndex() const;
    bool isPlaying() const;
    qint64 startTimestampUs() const;
    qint64 endTimestampUs() const;
    Duration duration() const;
    Duration elapsed() const;

    double speed() const;
    void setSpeed(double speed);

    void play();
    void pause();
    void stop();
    bool seek(int index);
    bool seekElapsed(Duration elapsed);
    bool stepForward();
    bool stepBy(Duration delta);

    const NavSample* currentSample() const;
    std::vector<NavSample> visibleSamples() const;
    const std::vector<NavSample>& samples() const;
    std::shared_ptr<const std::vector<NavSample>> sampleStorage() const;

    std::chrono::milliseconds interval() const;
    static std::chrono::milliseconds intervalForSpeed(double speed);
    static double speedFromText(const QString& text, double fallback = 1.0);

private:
    static qint64 timestampUsForSample(const NavSample& sample);
    void rebuildTimelineCache();
    bool hasTimestampTimeline() const;
    Duration fallbackDuration() const;
    qint64 sampleTimestampUs(int index) const;

    std::shared_ptr<const std::vector<NavSample>> samples_;
    bool has_timestamp_timeline_ = false;
    qint64 start_timestamp_us_ = 0;
    qint64 end_timestamp_us_ = 0;
    Duration duration_{};
    int current_index_ = -1;
    Duration current_elapsed_{};
    double speed_ = 1.0;
    bool playing_ = false;
};

} // namespace VaporView::Geo
