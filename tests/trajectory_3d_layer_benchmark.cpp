#include "geo/GeoTypes.h"
#include "map3d/Trajectory3DLayer.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{

VaporView::Geo::NavSample sample(int index)
{
    VaporView::Geo::NavSample value;
    value.recordTimestampUs = static_cast<qint64>(index + 1) * 1000000;
    value.latDeg = 39.0 + static_cast<double>(index) * 0.000001;
    value.lonDeg = 116.0 + static_cast<double>(index) * 0.000001;
    value.heightM = 50.0;
    value.nedNM = static_cast<double>(index);
    value.nedEM = static_cast<double>(index) * 0.5;
    value.nedDM = -10.0;
    value.fixQuality = VaporView::Geo::FixQuality::Fixed;
    return value;
}

void runCase(int historySize)
{
    VaporView::Map3D::Trajectory3DLayer layer;
    layer.setMaxVisibleSamples(historySize);

    std::vector<VaporView::Geo::NavSample> history;
    history.reserve(static_cast<std::size_t>(historySize));
    for (int index = 0; index < historySize; ++index)
    {
        history.push_back(sample(index));
    }
    layer.appendNavigationSamples(history);

    const int fullRebuildsBeforeSingleAppend = layer.fullRebuildCount();
    const int segmentRebuildsBeforeSingleAppend = layer.segmentGeometryRebuildCount();
    layer.appendNavigationSample(sample(historySize));
    const int singleAppendFullRebuilds =
        layer.fullRebuildCount() - fullRebuildsBeforeSingleAppend;
    const int singleAppendSegmentRebuilds =
        layer.segmentGeometryRebuildCount() - segmentRebuildsBeforeSingleAppend;

    std::vector<VaporView::Geo::NavSample> batch;
    batch.reserve(64);
    for (int index = 1; index <= 64; ++index)
    {
        batch.push_back(sample(historySize + index));
    }
    const int fullRebuildsBeforeBatchAppend = layer.fullRebuildCount();
    const int segmentRebuildsBeforeBatchAppend = layer.segmentGeometryRebuildCount();
    layer.appendNavigationSamples(batch);
    const int batchAppendFullRebuilds =
        layer.fullRebuildCount() - fullRebuildsBeforeBatchAppend;
    const int batchAppendSegmentRebuilds =
        layer.segmentGeometryRebuildCount() - segmentRebuildsBeforeBatchAppend;

    std::cout << "history=" << historySize
              << " single_full_rebuilds=" << singleAppendFullRebuilds
              << " single_segment_rebuilds=" << singleAppendSegmentRebuilds
              << " batch_full_rebuilds=" << batchAppendFullRebuilds
              << " batch_segment_rebuilds=" << batchAppendSegmentRebuilds
              << " retained_samples=" << layer.sampleCount()
              << '\n';

    if (singleAppendFullRebuilds != 0 || batchAppendFullRebuilds != 0)
    {
        std::cerr << "FAIL: live append rebuilt history at " << historySize << " samples\n";
        std::exit(1);
    }
}

void runHeatSlidingWindowCase()
{
    constexpr int kHistorySize = 200000;
    constexpr int kTailAppends = 1000;
    VaporView::Map3D::Trajectory3DLayer layer;
    layer.setMaxVisibleSamples(kHistorySize);
    std::vector<VaporView::Geo::TrajectoryRenderSample> history;
    history.reserve(kHistorySize);
    for (int index = 0; index < kHistorySize; ++index)
    {
        VaporView::Geo::TrajectoryRenderSample value;
        value.navigation = sample(index);
        value.heat.peak = static_cast<double>(index % 10000);
        history.push_back(value);
    }
    layer.appendRenderSamples(history);
    layer.resetHeatRangeInstrumentation();
    for (int index = 0; index < kTailAppends; ++index)
    {
        VaporView::Geo::TrajectoryRenderSample value;
        value.navigation = sample(kHistorySize + index);
        value.heat.peak = static_cast<double>((kHistorySize + index) % 10000);
        layer.appendRenderSample(value);
    }

    std::cout << "heat_history=" << kHistorySize
              << " heat_tail_appends=" << kTailAppends
              << " full_heat_range_scans=" << layer.heatRangeFullScanCount()
              << " incremental_heat_appends=" << layer.heatRangeIncrementalAppendCount()
              << " heat_evictions=" << layer.heatRangeEvictionCount()
              << " retained_samples=" << layer.sampleCount()
              << '\n';
    if (layer.heatRangeFullScanCount() != 0
        || layer.heatRangeIncrementalAppendCount() != kTailAppends
        || layer.heatRangeEvictionCount() != kTailAppends)
    {
        std::cerr << "FAIL: heat sliding-window append performed a full range scan\n";
        std::exit(1);
    }
}

} // namespace

int main()
{
    for (const int historySize : {1000, 10000, 100000, 200000})
    {
        runCase(historySize);
    }
    runHeatSlidingWindowCase();
    return 0;
}
