#include "geo/TrajectoryBuffer.h"

#include <iostream>

namespace {

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main()
{
    VaporView::Geo::TrajectoryBuffer buffer(3);
    for (int i = 0; i < 5; ++i)
    {
        VaporView::Geo::NavSample sample;
        sample.recordTimestampUs = i;
        sample.latDeg = 31.0 + i;
        sample.lonDeg = 121.0;
        sample.heightM = 10.0;
        buffer.append(sample);
    }

    require(buffer.size() == 3, "buffer trims to max samples");
    const auto snapshot = buffer.snapshot();
    require(snapshot.size() == 3, "snapshot size");
    require(snapshot.front().recordTimestampUs == 2, "oldest samples are dropped");

    const auto recent = buffer.recent(2);
    require(recent.size() == 2, "recent count");
    require(recent.front().recordTimestampUs == 3, "recent starts at expected sample");

    buffer.setMaxSamples(2);
    require(buffer.size() == 2, "setMaxSamples trims existing samples");
    require(buffer.snapshot().front().recordTimestampUs == 3, "setMaxSamples keeps newest samples");

    buffer.setMaxSamples(0);
    require(buffer.size() == 0, "zero max samples clears buffer");

    VaporView::Geo::NavSample dropped;
    dropped.recordTimestampUs = 99;
    buffer.append(dropped);
    require(buffer.size() == 0, "zero max samples drops appended samples");

    buffer.clear();
    require(buffer.size() == 0, "clear empties buffer");

    return 0;
}
