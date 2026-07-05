#include "map3d/Trajectory3DLayer.h"

#include <osg/Geometry>
#include <osg/Geode>

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

    layer.clear();
    require(layer.sampleCount() == 0, "clear removes samples");
    require(layer.segmentCount() == 0, "clear removes segments");
    require(geode->getNumDrawables() == 0, "clear removes drawables");

    return 0;
}
