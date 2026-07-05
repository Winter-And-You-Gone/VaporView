#pragma once

#include "geo/GeoTypes.h"

#include <osg/Geode>
#include <osg/ref_ptr>
#include <vector>

namespace osg {
class Node;
}

namespace VaporView::Map3D {

class Trajectory3DLayer {
public:
    Trajectory3DLayer();

    void clear();
    void appendSample(const VaporView::Geo::NavSample& sample);
    void appendSamples(const std::vector<VaporView::Geo::NavSample>& samples);
    void setUseWorldCoordinates(bool enabled);

    int sampleCount() const;
    osg::Node* node() const;

private:
    void rebuildGeometry();

    osg::ref_ptr<osg::Geode> geode_;
    std::vector<VaporView::Geo::NavSample> samples_;
    bool use_world_coordinates_ = false;
};

} // namespace VaporView::Map3D
