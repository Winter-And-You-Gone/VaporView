#pragma once

#include "geo/GeoTypes.h"

#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Matrixd>
#include <osg/ref_ptr>
#include <deque>
#include <optional>
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

struct TrajectoryPickResult {
    int sampleIndex = -1;
    VaporView::Geo::NavSample sample;
    double screenDistancePx = 0.0;
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
    int sphereMarkerCount() const;
    int selectedSampleIndex() const;
    TrajectoryQualityStats qualityStats() const;
    bool displayPositionForSample(int sampleIndex, osg::Vec3d& position) const;
    std::optional<TrajectoryPickResult> pickNearestSample(
        const osg::Matrixd& localToWindow,
        double screenX,
        double screenY,
        double maxDistancePx) const;
    void setSelectedSampleIndex(int sampleIndex);
    osg::Node* node();
    const osg::Node* node() const;

private:
    struct TrajectorySegment {
        int firstSampleIndex = 0;
        int sampleCount = 0;
        int sphereMarkerCount = 0;
        osg::ref_ptr<osg::Geometry> geometry;
        osg::ref_ptr<osg::Geometry> sphereGeometry;
    };

    void rebuildSegments();
    void rebuildSegmentGeometry(TrajectorySegment& segment);
    bool appendLineSampleGeometry(TrajectorySegment& segment, int sampleIndex);
    bool appendSphereMarkerGeometry(TrajectorySegment& segment, int sampleIndex);
    void configureGeometryState(osg::Geometry& geometry);
    void configureSphereMarkerState(osg::Geometry& geometry);
    void updateSelectedMarkerGeometry();
    void appendSegment();
    void trimToVisibleLimit();
    void removeOldestSample();
    void adjustQualityStats(int index, int delta);
    int firstVisibleIndex() const;
    int sphereMarkerStride() const;
    bool shouldUseAsLineSample(int index) const;
    bool isLineSample(int index) const;
    bool shouldRenderSphereMarker(int index) const;
    void rebuildLineSampleFlags();
    void rebuildQualityStats();
    bool segmentIsVisible(const TrajectorySegment& segment) const;
    void applySegmentVisibility();

    osg::ref_ptr<osg::Geode> geode_;
    osg::ref_ptr<osg::Geometry> selected_marker_geometry_;
    std::deque<VaporView::Geo::NavSample> samples_;
    std::deque<char> line_sample_flags_;
    int last_line_sample_index_ = -1;
    int selected_sample_index_ = -1;
    int sphere_marker_stride_ = 1;
    std::vector<TrajectorySegment> segments_;
    TrajectoryQualityStats quality_stats_;
    bool use_world_coordinates_ = false;
    bool has_world_origin_ = false;
    osg::Vec3d world_origin_;
    int max_visible_samples_ = 200000;
};

} // namespace VaporView::Map3D
