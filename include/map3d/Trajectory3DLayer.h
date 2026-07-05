#pragma once

#include "geo/GeoTypes.h"

#include <osg/Geode>
#include <osg/Geometry>
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
    void setMaxVisibleSamples(int maxVisibleSamples);

    int sampleCount() const;
    int visibleSampleCount() const;
    int maxVisibleSamples() const;
    int segmentCount() const;
    int segmentSize() const;
    osg::Node* node() const;

private:
    struct TrajectorySegment {
        int firstSampleIndex = 0;
        int sampleCount = 0;
        osg::ref_ptr<osg::Geometry> geometry;
    };

    void rebuildSegments();
    void rebuildSegmentGeometry(TrajectorySegment& segment);
    void rebuildVisibilityBoundarySegment();
    void appendSegment();
    int firstVisibleIndex() const;
    bool segmentIsVisible(const TrajectorySegment& segment) const;
    void applySegmentVisibility();

    osg::ref_ptr<osg::Geode> geode_;
    std::vector<VaporView::Geo::NavSample> samples_;
    std::vector<TrajectorySegment> segments_;
    bool use_world_coordinates_ = false;
    int max_visible_samples_ = 200000;
};

} // namespace VaporView::Map3D
