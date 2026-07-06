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

struct TrajectoryQualityStats {
    int fixedSamples = 0;
    int floatSamples = 0;
    int dgpsSamples = 0;
    int singleSamples = 0;
    int unknownSamples = 0;
    int invalidSamples = 0;
    int jumpSamples = 0;
    int lineSamples = 0;
    int markerSamples = 0;
};

class Trajectory3DLayer {
public:
    Trajectory3DLayer();

    void clear();
    void appendSample(const VaporView::Geo::NavSample& sample);
    void appendSamples(const std::vector<VaporView::Geo::NavSample>& samples);
    void setUseWorldCoordinates(bool enabled);
    void setWorldOrigin(const osg::Vec3d& origin);
    void clearWorldOrigin();
    void setMaxVisibleSamples(int maxVisibleSamples);

    int sampleCount() const;
    int visibleSampleCount() const;
    int maxVisibleSamples() const;
    int segmentCount() const;
    int segmentSize() const;
    TrajectoryQualityStats qualityStats() const;
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
    int previousLineSampleIndex(int index) const;
    bool shouldUseAsLineSample(int index) const;
    bool isLineSample(int index) const;
    void rebuildLineSampleFlags();
    bool segmentIsVisible(const TrajectorySegment& segment) const;
    void applySegmentVisibility();

    osg::ref_ptr<osg::Geode> geode_;
    std::vector<VaporView::Geo::NavSample> samples_;
    std::vector<char> line_sample_flags_;
    std::vector<TrajectorySegment> segments_;
    bool use_world_coordinates_ = false;
    bool has_world_origin_ = false;
    osg::Vec3d world_origin_;
    int max_visible_samples_ = 200000;
};

} // namespace VaporView::Map3D
