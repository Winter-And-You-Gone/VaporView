#pragma once

#include "geo/GeoTypes.h"

#include <deque>
#include <vector>

namespace VaporView::Geo {

class TrajectoryBuffer {
public:
    explicit TrajectoryBuffer(int maxSamples = 200000);

    void clear();
    void append(const NavSample& sample);
    void appendBatch(const std::vector<NavSample>& samples);

    std::vector<NavSample> snapshot() const;
    std::vector<NavSample> recent(int maxCount) const;

    int size() const;
    void setMaxSamples(int maxSamples);

private:
    void trimToLimit();

    int maxSamples_;
    std::deque<NavSample> samples_;
};

} // namespace VaporView::Geo
