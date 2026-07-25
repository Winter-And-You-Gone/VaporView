#include "map3d/TrackSampling.h"

#include <algorithm>
#include <cstddef>

namespace VaporView::Map3D {
namespace {

template <typename Sample>
std::vector<Sample> uniformlySampleTrackImpl(
    const std::vector<Sample>& samples,
    std::size_t sourceCount,
    int maxSamples)
{
    sourceCount = (std::min)(sourceCount, samples.size());
    if (sourceCount == 0 || maxSamples <= 0)
    {
        return {};
    }

    const std::size_t targetCount =
        (std::min)(sourceCount, static_cast<std::size_t>(maxSamples));
    if (targetCount == sourceCount)
    {
        return std::vector<Sample>(samples.cbegin(), samples.cbegin() + sourceCount);
    }
    if (targetCount == 1)
    {
        return {samples.back()};
    }

    std::vector<Sample> sampled;
    sampled.reserve(targetCount);
    const std::size_t lastIndex = sourceCount - 1;
    const std::size_t intervalCount = targetCount - 1;
    for (std::size_t outputIndex = 0; outputIndex < targetCount; ++outputIndex)
    {
        const std::size_t sourceIndex = (outputIndex * lastIndex) / intervalCount;
        sampled.push_back(samples[sourceIndex]);
    }
    return sampled;
}

} // namespace

std::vector<VaporView::Geo::NavSample> uniformlySampleTrack(
    const std::vector<VaporView::Geo::NavSample>& samples,
    int maxSamples)
{
    return uniformlySampleTrack(samples, samples.size(), maxSamples);
}

std::vector<VaporView::Geo::NavSample> uniformlySampleTrack(
    const std::vector<VaporView::Geo::NavSample>& samples,
    std::size_t sourceCount,
    int maxSamples)
{
    return uniformlySampleTrackImpl(samples, sourceCount, maxSamples);
}

std::vector<VaporView::Geo::TrajectoryRenderSample> uniformlySampleTrack(
    const std::vector<VaporView::Geo::TrajectoryRenderSample>& samples,
    int maxSamples)
{
    return uniformlySampleTrack(samples, samples.size(), maxSamples);
}

std::vector<VaporView::Geo::TrajectoryRenderSample> uniformlySampleTrack(
    const std::vector<VaporView::Geo::TrajectoryRenderSample>& samples,
    std::size_t sourceCount,
    int maxSamples)
{
    return uniformlySampleTrackImpl(samples, sourceCount, maxSamples);
}

double trackFocusRangeM(double trackRadiusM, bool earthMap)
{
    const double minimumRangeM = earthMap ? 6000.0 : 300.0;
    return (std::max)(minimumRangeM, trackRadiusM * 3.0);
}

} // namespace VaporView::Map3D
