#include "geo/TrajectoryReplay.h"

#include <QtCore/QString>

#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

VaporView::Geo::NavSample sample(double lat, double lon, qint64 timestampUs)
{
    VaporView::Geo::NavSample value;
    value.recordTimestampUs = timestampUs;
    value.latDeg = lat;
    value.lonDeg = lon;
    value.heightM = 42.0;
    return value;
}

} // namespace

int main()
{
    VaporView::Geo::TrajectoryReplay replay;
    require(!replay.hasSamples(), "empty replay has no samples");
    require(replay.currentIndex() == -1, "empty replay has no current index");

    replay.play();
    require(!replay.isPlaying(), "empty replay does not start");

    std::vector<VaporView::Geo::NavSample> samples;
    samples.push_back(sample(39.9, 116.3, 1000000));
    samples.push_back(sample(39.9001, 116.3002, 1100000));
    samples.push_back(sample(39.9002, 116.3004, 1200000));
    replay.setSamples(samples);

    require(replay.hasSamples(), "loaded replay has samples");
    require(replay.sampleCount() == 3, "loaded replay keeps sample count");
    require(replay.currentIndex() == 2, "loaded replay starts at full track");
    require(replay.visibleSamples().size() == 3, "loaded replay shows full track");

    replay.play();
    require(replay.isPlaying(), "play starts replay");
    require(replay.currentIndex() == 0, "play rewinds from end");
    require(replay.visibleSamples().size() == 1, "play exposes first sample");

    require(replay.stepForward(), "first replay step advances");
    require(replay.currentIndex() == 1, "first replay step reaches second sample");
    require(replay.isPlaying(), "middle sample keeps replay playing");

    require(replay.stepForward(), "second replay step advances");
    require(replay.currentIndex() == 2, "second replay step reaches final sample");
    require(!replay.isPlaying(), "final sample stops replay");

    replay.seek(-20);
    require(replay.currentIndex() == 0, "negative seek clamps to first sample");
    replay.seek(20);
    require(replay.currentIndex() == 2, "large seek clamps to last sample");

    replay.stop();
    require(!replay.isPlaying(), "stop clears playing state");
    require(replay.currentIndex() == 0, "stop rewinds to first sample");

    replay.setSpeed(2.0);
    require(replay.speed() == 2.0, "valid replay speed is stored");
    require(replay.intervalMs() == 50, "2x replay interval is 50 ms");
    replay.setSpeed(-1.0);
    require(replay.speed() == 1.0, "invalid replay speed falls back");
    require(VaporView::Geo::TrajectoryReplay::speedFromText(QStringLiteral("5x")) == 5.0,
            "speed text parser handles x suffix");

    replay.clear();
    require(!replay.hasSamples(), "clear removes samples");
    require(replay.currentIndex() == -1, "clear resets current index");

    return 0;
}
