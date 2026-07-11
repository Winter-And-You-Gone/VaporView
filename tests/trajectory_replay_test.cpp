#include "geo/TrajectoryReplay.h"

#include <QtCore/QString>

#include <cstdlib>
#include <chrono>
#include <iostream>
#include <utility>
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
    using ReplayDuration = VaporView::Geo::TrajectoryReplay::Duration;
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
    require(replay.sampleStorage() && replay.sampleStorage()->size() == samples.size(),
            "replay exposes shared immutable sample storage");
    require(replay.sampleCount() == 3, "loaded replay keeps sample count");
    require(replay.currentIndex() == 2, "loaded replay starts at full track");
    require(replay.visibleSamples().size() == 3, "loaded replay shows full track");
    require(replay.startTimestampUs() == 1000000, "replay reports first timestamp");
    require(replay.endTimestampUs() == 1200000, "replay reports last timestamp");
    require(replay.duration() == ReplayDuration(200000), "replay reports timestamp duration");
    require(replay.elapsed() == ReplayDuration(200000), "full-track replay reports elapsed duration");

    replay.play();
    require(replay.isPlaying(), "play starts replay");
    require(replay.currentIndex() == 0, "play rewinds from end");
    require(replay.visibleSamples().size() == 1, "play exposes first sample");
    require(replay.elapsed() == ReplayDuration::zero(), "play starts at zero elapsed time");

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
    replay.seekElapsed(ReplayDuration(150000));
    require(replay.currentIndex() == 1, "time seek picks latest sample at or before elapsed timestamp");
    require(replay.visibleSamples().size() == 2, "time seek exposes samples up to elapsed timestamp");
    replay.seekElapsed(ReplayDuration(200000));
    require(replay.currentIndex() == 2, "time seek reaches final sample at duration");
    replay.seekElapsed(ReplayDuration(-1));
    require(replay.currentIndex() == 0, "negative elapsed seek clamps to first sample");
    replay.seekElapsed(ReplayDuration(999999));
    require(replay.currentIndex() == 2, "elapsed seek beyond duration clamps to last sample");

    std::vector<VaporView::Geo::NavSample> irregularSamples;
    irregularSamples.push_back(sample(39.9, 116.3, 1000000));
    irregularSamples.push_back(sample(39.9001, 116.3002, 1050000));
    irregularSamples.push_back(sample(39.9002, 116.3004, 1300000));
    irregularSamples.push_back(sample(39.9003, 116.3006, 2000000));
    replay.setSamples(irregularSamples);
    replay.play();
    require(replay.currentIndex() == 0, "irregular replay starts at first sample");
    require(replay.stepBy(ReplayDuration(40000)), "elapsed replay accepts sub-sample delta");
    require(replay.currentIndex() == 0, "elapsed replay keeps current sample before next timestamp");
    require(replay.isPlaying(), "elapsed replay keeps playing before next timestamp");
    require(replay.stepBy(ReplayDuration(20000)), "elapsed replay crosses first irregular timestamp");
    require(replay.currentIndex() == 1, "elapsed replay advances to latest sample at elapsed time");
    require(replay.stepBy(ReplayDuration(300000)), "elapsed replay can skip across sparse timestamps");
    require(replay.currentIndex() == 2, "elapsed replay selects latest sample at or before sparse timestamp");
    require(replay.isPlaying(), "elapsed replay keeps playing before final timestamp");
    require(replay.stepBy(ReplayDuration(1000000)), "elapsed replay clamps at final timestamp");
    require(replay.currentIndex() == 3, "elapsed replay reaches final sample");
    require(!replay.isPlaying(), "elapsed replay stops at final sample");

    replay.stop();
    require(!replay.isPlaying(), "stop clears playing state");
    require(replay.currentIndex() == 0, "stop rewinds to first sample");

    replay.setSpeed(2.0);
    require(replay.speed() == 2.0, "valid replay speed is stored");
    require(replay.interval() == std::chrono::milliseconds(50), "2x replay interval is 50 ms");
    replay.setSpeed(-1.0);
    require(replay.speed() == 1.0, "invalid replay speed falls back");
    require(VaporView::Geo::TrajectoryReplay::speedFromText(QStringLiteral("5x")) == 5.0,
            "speed text parser handles x suffix");

    std::vector<VaporView::Geo::NavSample> untimedSamples;
    untimedSamples.push_back(sample(39.9, 116.3, 0));
    untimedSamples.push_back(sample(39.9001, 116.3002, 0));
    untimedSamples.push_back(sample(39.9002, 116.3004, 0));
    replay.setSamples(untimedSamples);
    require(replay.startTimestampUs() == 0, "untimed replay synthetic timeline starts at zero");
    require(replay.endTimestampUs() == 200000, "untimed replay synthetic timeline ends at fallback duration");
    require(replay.duration() == ReplayDuration(200000), "untimed replay falls back to synthetic 10 Hz timeline");
    replay.seekElapsed(ReplayDuration(100000));
    require(replay.currentIndex() == 1, "untimed elapsed seek uses synthetic timeline");

    std::vector<VaporView::Geo::NavSample> outOfOrderSamples;
    outOfOrderSamples.push_back(sample(39.9, 116.3, 1000000));
    outOfOrderSamples.push_back(sample(39.9001, 116.3002, 900000));
    outOfOrderSamples.push_back(sample(39.9002, 116.3004, 1200000));
    replay.setSamples(outOfOrderSamples);
    require(replay.startTimestampUs() == 0, "out-of-order replay synthetic timeline starts at zero");
    require(replay.endTimestampUs() == 200000, "out-of-order replay synthetic timeline ends at fallback duration");
    require(replay.duration() == ReplayDuration(200000), "out-of-order replay timestamps fall back to synthetic timeline");
    replay.seekElapsed(ReplayDuration(100000));
    require(replay.currentIndex() == 1, "out-of-order elapsed seek uses synthetic index timeline");

    std::vector<VaporView::Geo::NavSample> duplicateTimestampSamples;
    duplicateTimestampSamples.push_back(sample(39.9, 116.3, 1000000));
    duplicateTimestampSamples.push_back(sample(39.9001, 116.3002, 1000000));
    duplicateTimestampSamples.push_back(sample(39.9002, 116.3004, 1200000));
    replay.setSamples(duplicateTimestampSamples);
    require(replay.startTimestampUs() == 0, "duplicate replay synthetic timeline starts at zero");
    require(replay.endTimestampUs() == 200000, "duplicate replay synthetic timeline ends at fallback duration");
    require(replay.duration() == ReplayDuration(200000), "duplicate replay timestamps fall back to synthetic timeline");
    replay.seekElapsed(ReplayDuration(100000));
    require(replay.currentIndex() == 1, "duplicate elapsed seek uses synthetic index timeline");

    std::vector<VaporView::Geo::NavSample> largeTimeline;
    largeTimeline.reserve(100000);
    for (int index = 0; index < 100000; ++index)
    {
        largeTimeline.push_back(sample(39.9, 116.3, 1000000 + static_cast<qint64>(index) * 1000));
    }
    replay.setSamples(std::move(largeTimeline));
    const auto seekStart = std::chrono::steady_clock::now();
    for (int index = 0; index < 1000; ++index)
    {
        replay.seekElapsed(ReplayDuration(static_cast<qint64>(index) * 99999));
    }
    const auto seekElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - seekStart).count();
    require(seekElapsedMs < 1000, "large timestamp timeline seek remains sublinear");

    replay.clear();
    require(!replay.hasSamples(), "clear removes samples");
    require(replay.currentIndex() == -1, "clear resets current index");

    return 0;
}
