#pragma once

#include "geo/GeoTypes.h"
#include "geo/TrajectoryHeatmap.h"

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
    void appendSample(const VaporView::Geo::TrajectoryRenderSample& sample);
    void appendSamples(const std::vector<VaporView::Geo::NavSample>& samples);
    void appendSamples(const std::vector<VaporView::Geo::TrajectoryRenderSample>& samples);
    void setUseWorldCoordinates(bool enabled);
    void setWorldOrigin(const osg::Vec3d& origin);
    void clearWorldOrigin();
    void setMaxVisibleSamples(int maxVisibleSamples);
    void setHeatMetric(VaporView::Geo::HeatMetric metric);
    VaporView::Geo::HeatMetric heatMetric() const;
    void setHeatPalette(VaporView::Geo::HeatPalette palette);
    VaporView::Geo::HeatPalette heatPalette() const;
    void setHeatRange(const VaporView::Geo::HeatRange& range);
    void clearHeatRangeOverride();
    VaporView::Geo::HeatRange heatRange() const;
    void setTrackLineVisible(bool visible);
    bool trackLineVisible() const;
    void setTrackPointsVisible(bool visible);
    bool trackPointsVisible() const;
    void setTrackLineWidth(float width);
    float trackLineWidth() const;
    void setTrackPointSize(float size);
    float trackPointSize() const;

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

    void appendRenderSample(const VaporView::Geo::TrajectoryRenderSample& sample,
                            bool enableHeatRendering);
    void appendRenderSamples(
        const std::vector<VaporView::Geo::TrajectoryRenderSample>& samples,
        bool enableHeatRendering);
    void rebuildSegments();
    void rebuildSegmentGeometry(TrajectorySegment& segment,
                                const VaporView::Geo::HeatRange& heatRange);
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
    VaporView::Geo::HeatRange resolvedHeatRange() const;

    osg::ref_ptr<osg::Geode> geode_;
    osg::ref_ptr<osg::Geometry> selected_marker_geometry_;
    std::deque<VaporView::Geo::TrajectoryRenderSample> samples_;
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
    bool heat_rendering_enabled_ = false;
    VaporView::Geo::HeatMetric heat_metric_ = VaporView::Geo::HeatMetric::Peak;
    VaporView::Geo::HeatPalette heat_palette_ = VaporView::Geo::HeatPalette::Candy;
    std::optional<VaporView::Geo::HeatRange> heat_range_override_;
    bool track_line_visible_ = true;
    bool track_points_visible_ = true;
    float track_line_width_ = 5.0f;
    float track_point_size_ = 7.0f;
};

} // namespace VaporView::Map3D
