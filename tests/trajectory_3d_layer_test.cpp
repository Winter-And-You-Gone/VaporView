#include "map3d/Trajectory3DLayer.h"

#include <osg/Geometry>
#include <osg/Geode>
#include <osg/PrimitiveSet>

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
    value.satellites = 12;
    value.hdop = 0.9;
    return value;
}

struct PrimitiveStats {
    int lineStrips = 0;
    int lineVertices = 0;
    int pointSets = 0;
    int pointVertices = 0;
};

PrimitiveStats primitiveStats(const osg::Geometry& geometry)
{
    PrimitiveStats stats;
    for (unsigned int index = 0; index < geometry.getNumPrimitiveSets(); ++index)
    {
        const osg::PrimitiveSet* primitive = geometry.getPrimitiveSet(index);
        const auto* drawArrays = dynamic_cast<const osg::DrawArrays*>(primitive);
        if (!primitive || !drawArrays)
        {
            continue;
        }
        if (primitive->getMode() == osg::PrimitiveSet::LINE_STRIP)
        {
            ++stats.lineStrips;
            stats.lineVertices += static_cast<int>(drawArrays->getCount());
        }
        else if (primitive->getMode() == osg::PrimitiveSet::POINTS)
        {
            ++stats.pointSets;
            stats.pointVertices += static_cast<int>(drawArrays->getCount());
        }
    }
    return stats;
}

int visibleDrawableCount(const osg::Geode& geode)
{
    int count = 0;
    for (unsigned int index = 0; index < geode.getNumDrawables(); ++index)
    {
        const osg::Drawable* drawable = geode.getDrawable(index);
        if (drawable && drawable->getNodeMask() != 0u)
        {
            ++count;
        }
    }
    return count;
}

} // namespace

int main()
{
    VaporView::Map3D::Trajectory3DLayer layer;
    require(layer.sampleCount() == 0, "new layer has no samples");
    require(layer.segmentSize() == 4096, "segment size is fixed at 4096 samples");

    std::vector<VaporView::Geo::NavSample> samples;
    samples.reserve(9000);
    for (int index = 0; index < 9000; ++index)
    {
        samples.push_back(sample(index));
    }

    layer.appendSamples(samples);
    require(layer.sampleCount() == 9000, "layer keeps total sample count");
    require(layer.visibleSampleCount() == 9000, "all samples visible by default");
    require(layer.segmentCount() == 3, "9000 samples are split into three segments");

    auto* geode = dynamic_cast<osg::Geode*>(layer.node());
    require(geode != nullptr, "trajectory node is a geode");
    require(geode->getNumDrawables() == 3, "geode has one drawable per segment");

    layer.setMaxVisibleSamples(5000);
    require(layer.visibleSampleCount() == 5000, "visible sample count is capped");
    require(geode->getDrawable(0)->getNodeMask() != 0u, "boundary segment remains visible");
    require(geode->getDrawable(1)->getNodeMask() != 0u, "middle segment remains visible");
    require(geode->getDrawable(2)->getNodeMask() != 0u, "current segment remains visible");
    auto* boundaryGeometry = dynamic_cast<osg::Geometry*>(geode->getDrawable(0));
    require(boundaryGeometry != nullptr, "boundary segment drawable is geometry");
    require(boundaryGeometry->getVertexArray() != nullptr, "boundary segment has vertex array");
    require(boundaryGeometry->getVertexArray()->getNumElements() == 96,
            "boundary segment is clipped to exactly visible samples");

    layer.appendSample(sample(9000));
    require(layer.sampleCount() == 9001, "appendSample increments total count");
    require(layer.segmentCount() == 3, "appendSample reuses current segment");
    require(boundaryGeometry->getVertexArray()->getNumElements() == 95,
            "boundary segment clipping advances after append");

    layer.setMaxVisibleSamples(9000);
    require(layer.visibleSampleCount() == 9000, "expanded visible sample count is applied");
    auto* restoredFirstGeometry = dynamic_cast<osg::Geometry*>(geode->getDrawable(0));
    require(restoredFirstGeometry != nullptr, "restored first segment drawable is geometry");
    require(restoredFirstGeometry->getVertexArray() != nullptr, "restored first segment has vertex array");
    require(restoredFirstGeometry->getVertexArray()->getNumElements() == 4095,
            "expanded visibility restores previously clipped segment");

    VaporView::Map3D::Trajectory3DLayer longLayer;
    std::vector<VaporView::Geo::NavSample> longSamples;
    constexpr int kLongTrackSampleCount = 100000;
    longSamples.reserve(kLongTrackSampleCount);
    for (int index = 0; index < kLongTrackSampleCount; ++index)
    {
        longSamples.push_back(sample(index));
    }

    longLayer.appendSamples(longSamples);
    require(longLayer.sampleCount() == kLongTrackSampleCount, "long layer keeps 100000 samples");
    require(longLayer.visibleSampleCount() == kLongTrackSampleCount, "long layer shows all samples under default cap");
    const int expectedLongSegments =
        (kLongTrackSampleCount + longLayer.segmentSize() - 1) / longLayer.segmentSize();
    require(longLayer.segmentCount() == expectedLongSegments, "long layer splits 100000 samples into segments");

    auto* longGeode = dynamic_cast<osg::Geode*>(longLayer.node());
    require(longGeode != nullptr, "long trajectory node is a geode");
    require(static_cast<int>(longGeode->getNumDrawables()) == expectedLongSegments,
            "long trajectory has one drawable per segment");

    longLayer.setMaxVisibleSamples(10000);
    require(longLayer.sampleCount() == kLongTrackSampleCount,
            "long layer max-visible cap does not discard source samples");
    require(longLayer.visibleSampleCount() == 10000,
            "long layer max-visible cap limits rendered samples");
    require(longGeode->getDrawable(0)->getNodeMask() == 0u,
            "long layer hides old segments outside the visible window");
    const int firstVisibleIndex = kLongTrackSampleCount - longLayer.visibleSampleCount();
    const int firstVisibleSegment = firstVisibleIndex / longLayer.segmentSize();
    require(visibleDrawableCount(*longGeode) == expectedLongSegments - firstVisibleSegment,
            "long layer keeps only boundary and current visible segments active");

    auto* longBoundaryGeometry =
        dynamic_cast<osg::Geometry*>(longGeode->getDrawable(static_cast<unsigned int>(firstVisibleSegment)));
    require(longBoundaryGeometry != nullptr, "long layer boundary segment drawable is geometry");
    require(longBoundaryGeometry->getVertexArray() != nullptr,
            "long layer boundary segment has vertex array");
    const int expectedBoundaryVertices =
        ((firstVisibleSegment + 1) * longLayer.segmentSize()) - firstVisibleIndex;
    require(static_cast<int>(longBoundaryGeometry->getVertexArray()->getNumElements()) == expectedBoundaryVertices,
            "long layer clips the boundary segment to the visible window");

    VaporView::Map3D::Trajectory3DLayer qualityLayer;
    std::vector<VaporView::Geo::NavSample> qualitySamples;
    for (int index = 0; index < 8; ++index)
    {
        qualitySamples.push_back(sample(index));
    }
    qualitySamples[2].fixQuality = VaporView::Geo::FixQuality::Invalid;
    qualitySamples[5].recordTimestampUs = qualitySamples[4].recordTimestampUs - 1;

    qualityLayer.appendSamples(qualitySamples);
    auto* qualityGeode = dynamic_cast<osg::Geode*>(qualityLayer.node());
    require(qualityGeode != nullptr, "quality trajectory node is a geode");
    require(qualityGeode->getNumDrawables() == 1, "quality trajectory fits in one segment");
    auto* qualityGeometry = dynamic_cast<osg::Geometry*>(qualityGeode->getDrawable(0));
    require(qualityGeometry != nullptr, "quality trajectory drawable is geometry");
    const PrimitiveStats stats = primitiveStats(*qualityGeometry);
    require(stats.lineStrips == 3, "invalid and jump samples split the continuous trajectory line");
    require(stats.lineVertices == 6, "only usable non-jump samples participate in line strips");
    require(stats.pointSets == 1, "invalid and jump samples share one red point marker primitive");
    require(stats.pointVertices == 2, "invalid and jump samples are rendered as two point markers");
    const VaporView::Map3D::TrajectoryQualityStats qualityStats = qualityLayer.qualityStats();
    require(qualityStats.fixedSamples == 6, "quality stats count fixed line samples");
    require(qualityStats.invalidSamples == 1, "quality stats count invalid marker samples");
    require(qualityStats.jumpSamples == 1, "quality stats count jump marker samples");
    require(qualityStats.lineSamples == 6, "quality stats count line samples");
    require(qualityStats.markerSamples == 2, "quality stats count marker samples");

    layer.clear();
    require(layer.sampleCount() == 0, "clear removes samples");
    require(layer.segmentCount() == 0, "clear removes segments");
    require(geode->getNumDrawables() == 0, "clear removes drawables");

    return 0;
}
