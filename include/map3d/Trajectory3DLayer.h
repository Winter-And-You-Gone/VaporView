#pragma once

#include "geo/GeoTypes.h"

#include <osg/ref_ptr>
#include <vector>

namespace osg {
class Geode;
class Node;
}

namespace VaporView::Map3D {

class Trajectory3DLayer {
public:
    Trajectory3DLayer();

    void clear();
    void appendSample(const VaporView::Geo::NavSample& sample);
    void appendSamples(const std::vector<VaporView::Geo::NavSample>& samples);

    int sampleCount() const;
    osg::Node* node() const;

private:
    void rebuildGeometry();

    osg::ref_ptr<osg::Geode> geode_;
    std::vector<VaporView::Geo::NavSample> samples_;
};

} // namespace VaporView::Map3D
