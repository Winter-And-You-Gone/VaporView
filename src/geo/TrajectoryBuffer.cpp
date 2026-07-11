#include "geo/TrajectoryBuffer.h"

#include <algorithm>

namespace VaporView::Geo {

TrajectoryBuffer::TrajectoryBuffer(int maxSamples)
    : maxSamples_((std::max)(0, maxSamples))
{
}

void TrajectoryBuffer::clear()
{
    samples_.clear();
}

void TrajectoryBuffer::append(const NavSample& sample)
{
    if (maxSamples_ <= 0)
    {
        return;
    }
    samples_.push_back(sample);
    trimToLimit();
}

void TrajectoryBuffer::appendBatch(const std::vector<NavSample>& samples)
{
    if (maxSamples_ <= 0)
    {
        return;
    }
    for (const NavSample& sample : samples)
    {
        samples_.push_back(sample);
    }
    trimToLimit();
}

std::vector<NavSample> TrajectoryBuffer::snapshot() const
{
    return {samples_.cbegin(), samples_.cend()};
}

std::vector<NavSample> TrajectoryBuffer::recent(int maxCount) const
{
    if (maxCount <= 0 || samples_.empty())
    {
        return {};
    }

    const auto count = static_cast<std::deque<NavSample>::difference_type>(
        std::min<int>(maxCount, static_cast<int>(samples_.size())));
    return {samples_.cend() - count, samples_.cend()};
}

int TrajectoryBuffer::size() const
{
    return static_cast<int>(samples_.size());
}

void TrajectoryBuffer::setMaxSamples(int maxSamples)
{
    maxSamples_ = (std::max)(0, maxSamples);
    trimToLimit();
}

void TrajectoryBuffer::trimToLimit()
{
    while (static_cast<int>(samples_.size()) > maxSamples_)
    {
        samples_.pop_front();
    }
}

} // namespace VaporView::Geo
