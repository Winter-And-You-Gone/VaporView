#include "map3d/Trajectory3DLayer.h"

#include "geo/TrajectoryQuality.h"
#include "geo/TrajectoryHeatmap.h"

#include <osg/Geometry>
#include <osg/Geode>
#include <osg/LineWidth>
#include <osg/Image>
#include <osg/Point>
#include <osg/Program>
#include <osg/PrimitiveSet>
#include <osg/StateSet>
#include <osg/Texture1D>
#include <osg/Uniform>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace VaporView::Map3D {
namespace {

constexpr int kSegmentSize = 4096;
constexpr int kMaxSolidSphereMarkers = 12000;
constexpr int kIcosphereVertexCount = 42;
constexpr int kIcosphereIndexCount = 240;
constexpr double kTrajectorySphereRadiusM = 0.5;
constexpr double kSelectedTrajectorySphereRadiusM = 1.0;
constexpr unsigned int kHeatValueAttribute = 6;
constexpr unsigned int kHeatValidAttribute = 7;

const char* kHeatVertexShader = R"glsl(
attribute float vaporviewHeatValue;
attribute float vaporviewHeatValid;
varying float vvHeatValue;
varying float vvHeatValid;
void main()
{
    gl_Position = ftransform();
    gl_FrontColor = gl_Color;
    vvHeatValue = vaporviewHeatValue;
    vvHeatValid = vaporviewHeatValid;
}
)glsl";

const char* kHeatFragmentShader = R"glsl(
uniform sampler1D vaporviewHeatPalette;
uniform float vaporviewHeatMinimum;
uniform float vaporviewHeatMaximum;
varying float vvHeatValue;
varying float vvHeatValid;
void main()
{
    if (vvHeatValid < 0.5)
    {
        gl_FragColor = gl_Color;
        return;
    }
    float span = vaporviewHeatMaximum - vaporviewHeatMinimum;
    float normalized = span > 0.000001
        ? clamp((vvHeatValue - vaporviewHeatMinimum) / span, 0.0, 1.0)
        : 0.5;
    gl_FragColor = texture1D(vaporviewHeatPalette, normalized);
}
)glsl";

bool hasWorldPosition(const VaporView::Geo::NavSample& sample)
{
    return sample.hasEcef();
}

osg::Vec3d samplePosition(const VaporView::Geo::NavSample& sample,
                          bool useWorldCoordinates,
                          bool hasWorldOrigin,
                          const osg::Vec3d& worldOrigin)
{
    if (useWorldCoordinates && hasWorldPosition(sample))
    {
        const osg::Vec3d world(sample.ecefXM, sample.ecefYM, sample.ecefZM);
        return hasWorldOrigin ? world - worldOrigin : world;
    }
    if (sample.hasNed())
    {
        return osg::Vec3d(sample.nedEM, sample.nedNM, -sample.nedDM);
    }
    return osg::Vec3d(sample.lonDeg * 100000.0, sample.latDeg * 100000.0, sample.heightM);
}

osg::Vec4 qualityColor(const VaporView::Geo::NavSample& sample)
{
    if (!VaporView::Geo::isUsableForDisplay(sample))
    {
        return osg::Vec4(0.9f, 0.1f, 0.1f, 1.0f);
    }
    switch (sample.fixQuality)
    {
    case VaporView::Geo::FixQuality::Fixed:
        return osg::Vec4(0.05f, 1.0f, 0.35f, 1.0f);
    case VaporView::Geo::FixQuality::Float:
        return osg::Vec4(1.0f, 0.82f, 0.05f, 1.0f);
    case VaporView::Geo::FixQuality::Dgps:
        return osg::Vec4(0.05f, 0.65f, 1.0f, 1.0f);
    case VaporView::Geo::FixQuality::Single:
        return osg::Vec4(1.0f, 0.95f, 0.25f, 1.0f);
    case VaporView::Geo::FixQuality::Unknown:
        return osg::Vec4(0.05f, 0.95f, 1.0f, 1.0f);
    case VaporView::Geo::FixQuality::Invalid:
        break;
    }
    return osg::Vec4(0.95f, 0.05f, 0.05f, 1.0f);
}

osg::Vec4 markerColor()
{
    return osg::Vec4(0.95f, 0.05f, 0.05f, 1.0f);
}

osg::Vec4 heatColorToVec4(const VaporView::Geo::HeatColor& color)
{
    return osg::Vec4(color.r, color.g, color.b, color.a);
}

VaporView::Geo::TrajectoryRenderSample renderSampleFromNavSample(
    const VaporView::Geo::NavSample& sample)
{
    VaporView::Geo::TrajectoryRenderSample renderSample;
    renderSample.navigation = sample;
    return renderSample;
}

osg::Vec4 selectedMarkerColor()
{
    return osg::Vec4(1.0f, 0.48f, 0.02f, 1.0f);
}

bool finiteVec3(const osg::Vec3d& value)
{
    return std::isfinite(value.x()) && std::isfinite(value.y()) && std::isfinite(value.z());
}

struct UnitIcosphereMesh {
    std::vector<osg::Vec3d> vertices;
    std::vector<unsigned int> indices;
};

UnitIcosphereMesh unitIcosphereMesh()
{
    constexpr double kPhi = 1.6180339887498948482;
    UnitIcosphereMesh mesh;
    mesh.vertices = {
        {-1.0, kPhi, 0.0}, {1.0, kPhi, 0.0}, {-1.0, -kPhi, 0.0}, {1.0, -kPhi, 0.0},
        {0.0, -1.0, kPhi}, {0.0, 1.0, kPhi}, {0.0, -1.0, -kPhi}, {0.0, 1.0, -kPhi},
        {kPhi, 0.0, -1.0}, {kPhi, 0.0, 1.0}, {-kPhi, 0.0, -1.0}, {-kPhi, 0.0, 1.0},
    };
    for (osg::Vec3d& vertex : mesh.vertices)
    {
        vertex.normalize();
    }

    const std::vector<unsigned int> faces = {
        0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11,
        1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
        3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9,
        4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1,
    };

    std::map<std::pair<unsigned int, unsigned int>, unsigned int> midpointIndices;
    auto midpointIndex = [&](unsigned int first, unsigned int second) {
        const auto edge = (std::minmax)(first, second);
        const auto key = std::make_pair(edge.first, edge.second);
        const auto existing = midpointIndices.find(key);
        if (existing != midpointIndices.end())
        {
            return existing->second;
        }
        osg::Vec3d midpoint = mesh.vertices[first] + mesh.vertices[second];
        midpoint.normalize();
        const unsigned int index = static_cast<unsigned int>(mesh.vertices.size());
        mesh.vertices.push_back(midpoint);
        midpointIndices.emplace(key, index);
        return index;
    };

    mesh.indices.reserve(faces.size() * 4);
    for (std::size_t face = 0; face < faces.size(); face += 3)
    {
        const unsigned int a = faces[face];
        const unsigned int b = faces[face + 1];
        const unsigned int c = faces[face + 2];
        const unsigned int ab = midpointIndex(a, b);
        const unsigned int bc = midpointIndex(b, c);
        const unsigned int ca = midpointIndex(c, a);
        const unsigned int subdividedFaces[] = {
            a, ab, ca,
            b, bc, ab,
            c, ca, bc,
            ab, bc, ca,
        };
        mesh.indices.insert(mesh.indices.end(),
                            std::begin(subdividedFaces),
                            std::end(subdividedFaces));
    }
    return mesh;
}

void appendIcosphere(osg::Vec3dArray& vertices,
                     osg::Vec4Array& colors,
                     osg::DrawElementsUInt& indices,
                     const osg::Vec3d& center,
                     double radiusM,
                     const osg::Vec4& color)
{
    static const UnitIcosphereMesh mesh = unitIcosphereMesh();
    const unsigned int base = static_cast<unsigned int>(vertices.size());
    for (const osg::Vec3d& vertex : mesh.vertices)
    {
        vertices.push_back(center + vertex * radiusM);
        colors.push_back(color);
    }
    for (unsigned int index : mesh.indices)
    {
        indices.push_back(base + index);
    }
}

} // namespace

Trajectory3DLayer::Trajectory3DLayer()
    : geode_(new osg::Geode)
    , selected_marker_geometry_(new osg::Geometry)
{
}

void Trajectory3DLayer::clear()
{
    samples_.clear();
    sample_sequences_.clear();
    line_sample_flags_.clear();
    last_line_sample_index_ = -1;
    selected_sample_index_ = -1;
    sphere_marker_stride_ = 1;
    segments_.clear();
    quality_stats_ = {};
    heat_rendering_enabled_ = false;
    heat_range_override_.reset();
    resetHeatStatistics();
    next_sample_sequence_ = 0;
    resetHeatRangeInstrumentation();
    geode_->removeDrawables(0, geode_->getNumDrawables());
    updateSelectedMarkerGeometry();
}

void Trajectory3DLayer::appendNavigationSample(const VaporView::Geo::NavSample& sample)
{
    appendSampleInternal(renderSampleFromNavSample(sample), false);
}

void Trajectory3DLayer::appendRenderSample(const VaporView::Geo::TrajectoryRenderSample& sample)
{
    appendSampleInternal(sample, true);
}

void Trajectory3DLayer::appendSampleInternal(const VaporView::Geo::TrajectoryRenderSample& sample,
                                              bool enableHeatRendering)
{
    const int previousSphereMarkerStride = sphere_marker_stride_;
    const bool wasHeatRenderingEnabled = heat_rendering_enabled_;
    if (enableHeatRendering)
    {
        heat_rendering_enabled_ = true;
    }
    sample_sequences_.push_back(next_sample_sequence_++);
    samples_.push_back(sample);
    if (enableHeatRendering)
    {
        appendHeatStatistics(sample, sample_sequences_.back());
        updateHeatRenderingState();
    }
    const int sampleIndex = sampleCount() - 1;
    const bool lineSample = shouldUseAsLineSample(sampleIndex);
    line_sample_flags_.push_back(lineSample ? 1 : 0);
    if (lineSample)
    {
        last_line_sample_index_ = sampleIndex;
    }
    adjustQualityStats(sampleCount() - 1, 1);
    const bool needsNewSegment = segments_.empty() || segments_.back().sampleCount >= kSegmentSize;
    if (needsNewSegment)
    {
        appendSegment();
    }
    TrajectorySegment& segment = segments_.back();
    ++segment.sampleCount;
    segment.hasLineDiscontinuity = segment.hasLineDiscontinuity || !lineSample;
    if (heat_rendering_enabled_)
    {
        trimToVisibleLimit();
        sphere_marker_stride_ = sphereMarkerStride();
        if (!wasHeatRenderingEnabled)
        {
            rebuildSegments();
            return;
        }
        const VaporView::Geo::HeatRange heatRange = resolvedHeatRange();
        rebuildSegmentGeometry(segment, heatRange);
    }
    else
    {
        const int sampleIndex = sampleCount() - 1;
        const bool lineUpdated =
            !needsNewSegment && appendLineSampleGeometry(segment, sampleIndex);
        const bool sphereUpdated =
            !needsNewSegment
            && (!shouldRenderSphereMarker(sampleIndex)
                || appendSphereMarkerGeometry(segment, sampleIndex));
        if (needsNewSegment || !lineUpdated || !sphereUpdated)
        {
            const VaporView::Geo::HeatRange heatRange = resolvedHeatRange();
            rebuildSegmentGeometry(segment, heatRange);
        }
    }
    trimToVisibleLimit();
    sphere_marker_stride_ = sphereMarkerStride();
    if (sphere_marker_stride_ != previousSphereMarkerStride)
    {
        rebuildSegments();
        return;
    }
    applySegmentVisibility();
}

void Trajectory3DLayer::appendNavigationSamples(const std::vector<VaporView::Geo::NavSample>& samples)
{
    for (const VaporView::Geo::NavSample& sample : samples)
    {
        appendSampleInternal(renderSampleFromNavSample(sample), false);
    }
}

void Trajectory3DLayer::appendRenderSamples(
    const std::vector<VaporView::Geo::TrajectoryRenderSample>& samples)
{
    appendSamplesInternal(samples, true);
}

void Trajectory3DLayer::appendSamplesInternal(
    const std::vector<VaporView::Geo::TrajectoryRenderSample>& samples,
    bool enableHeatRendering)
{
    if (samples.empty())
    {
        return;
    }
    for (const VaporView::Geo::TrajectoryRenderSample& sample : samples)
    {
        appendSampleInternal(sample, enableHeatRendering);
    }
}

void Trajectory3DLayer::setUseWorldCoordinates(bool enabled)
{
    if (use_world_coordinates_ == enabled)
    {
        return;
    }
    use_world_coordinates_ = enabled;
    rebuildSegments();
}

void Trajectory3DLayer::setWorldOrigin(const osg::Vec3d& origin)
{
    if (has_world_origin_ && world_origin_ == origin)
    {
        return;
    }
    has_world_origin_ = true;
    world_origin_ = origin;
    if (use_world_coordinates_)
    {
        rebuildSegments();
    }
}

void Trajectory3DLayer::clearWorldOrigin()
{
    if (!has_world_origin_)
    {
        return;
    }
    has_world_origin_ = false;
    world_origin_.set(0.0, 0.0, 0.0);
    if (use_world_coordinates_)
    {
        rebuildSegments();
    }
}

void Trajectory3DLayer::setMaxVisibleSamples(int maxVisibleSamples)
{
    const int sanitized = (std::max)(1000, maxVisibleSamples);
    if (max_visible_samples_ == sanitized)
    {
        return;
    }
    max_visible_samples_ = sanitized;
    trimToVisibleLimit();
    sphere_marker_stride_ = sphereMarkerStride();
    rebuildLineSampleFlags();
    rebuildQualityStats();
    rebuildSegments();
}

void Trajectory3DLayer::setHeatMetric(VaporView::Geo::HeatMetric metric)
{
    if (heat_metric_ == metric)
    {
        return;
    }
    heat_metric_ = metric;
    invalidateHeatRange();
    if (heat_rendering_enabled_)
    {
        recomputeHeatRange();
        // The metric changes the per-vertex scalar, so this is a deliberate
        // user-driven geometry refresh rather than a live append cost.
        updateHeatRenderingState();
        rebuildSegments();
    }
}

VaporView::Geo::HeatMetric Trajectory3DLayer::heatMetric() const
{
    return heat_metric_;
}

void Trajectory3DLayer::setHeatPalette(VaporView::Geo::HeatPalette palette)
{
    if (heat_palette_ == palette)
    {
        return;
    }
    heat_palette_ = palette;
    heat_palette_texture_dirty_ = true;
    // Existing vertices retain raw heat values; the palette texture updates all
    // heat geometry without rebuilding the historical trajectory.
    updateHeatRenderingState();
}

VaporView::Geo::HeatPalette Trajectory3DLayer::heatPalette() const
{
    return heat_palette_;
}

void Trajectory3DLayer::setHeatRange(const VaporView::Geo::HeatRange& range)
{
    heat_range_override_ = range;
    updateHeatRenderingState();
}

void Trajectory3DLayer::clearHeatRangeOverride()
{
    if (!heat_range_override_.has_value())
    {
        return;
    }
    heat_range_override_.reset();
    invalidateHeatRange();
    if (heat_rendering_enabled_)
    {
        recomputeHeatRange();
    }
    updateHeatRenderingState();
}

VaporView::Geo::HeatRange Trajectory3DLayer::heatRange() const
{
    return resolvedHeatRange();
}

void Trajectory3DLayer::setTrackLineVisible(bool visible)
{
    if (track_line_visible_ == visible)
    {
        return;
    }
    track_line_visible_ = visible;
    applySegmentVisibility();
}

bool Trajectory3DLayer::trackLineVisible() const
{
    return track_line_visible_;
}

void Trajectory3DLayer::setTrackPointsVisible(bool visible)
{
    if (track_points_visible_ == visible)
    {
        return;
    }
    track_points_visible_ = visible;
    applySegmentVisibility();
}

bool Trajectory3DLayer::trackPointsVisible() const
{
    return track_points_visible_;
}

void Trajectory3DLayer::setTrackLineWidth(float width)
{
    const float sanitized = std::clamp(width, 1.0f, 20.0f);
    if (std::abs(track_line_width_ - sanitized) <= 0.001f)
    {
        return;
    }
    track_line_width_ = sanitized;
    rebuildSegments();
}

float Trajectory3DLayer::trackLineWidth() const
{
    return track_line_width_;
}

void Trajectory3DLayer::setTrackPointSize(float size)
{
    const float sanitized = std::clamp(size, 1.0f, 32.0f);
    if (std::abs(track_point_size_ - sanitized) <= 0.001f)
    {
        return;
    }
    track_point_size_ = sanitized;
    rebuildSegments();
}

float Trajectory3DLayer::trackPointSize() const
{
    return track_point_size_;
}

int Trajectory3DLayer::sampleCount() const
{
    return static_cast<int>(samples_.size());
}

int Trajectory3DLayer::visibleSampleCount() const
{
    return (std::min)(sampleCount(), max_visible_samples_);
}

int Trajectory3DLayer::maxVisibleSamples() const
{
    return max_visible_samples_;
}

int Trajectory3DLayer::segmentCount() const
{
    return static_cast<int>(segments_.size());
}

int Trajectory3DLayer::segmentSize() const
{
    return kSegmentSize;
}

int Trajectory3DLayer::fullRebuildCount() const
{
    return full_rebuild_count_;
}

int Trajectory3DLayer::segmentGeometryRebuildCount() const
{
    return segment_geometry_rebuild_count_;
}

int Trajectory3DLayer::heatRangeFullScanCount() const
{
    return heat_range_full_scan_count_;
}

int Trajectory3DLayer::heatRangeIncrementalAppendCount() const
{
    return heat_range_incremental_append_count_;
}

int Trajectory3DLayer::heatRangeEvictionCount() const
{
    return heat_range_eviction_count_;
}

void Trajectory3DLayer::resetHeatRangeInstrumentation()
{
    heat_range_full_scan_count_ = 0;
    heat_range_incremental_append_count_ = 0;
    heat_range_eviction_count_ = 0;
}

bool Trajectory3DLayer::heatRenderingEnabled() const
{
    return heat_rendering_enabled_;
}

int Trajectory3DLayer::sphereMarkerCount() const
{
    int count = 0;
    for (const TrajectorySegment& segment : segments_)
    {
        count += segment.sphereMarkerCount;
    }
    return count;
}

int Trajectory3DLayer::selectedSampleIndex() const
{
    return selected_sample_index_;
}

TrajectoryQualityStats Trajectory3DLayer::qualityStats() const
{
    return quality_stats_;
}

bool Trajectory3DLayer::displayPositionForSample(int sampleIndex, osg::Vec3d& position) const
{
    if (sampleIndex < 0 || sampleIndex >= sampleCount())
    {
        return false;
    }
    position = samplePosition(samples_[static_cast<std::size_t>(sampleIndex)].navigation,
                              use_world_coordinates_,
                              has_world_origin_,
                              world_origin_);
    return finiteVec3(position);
}

std::optional<TrajectoryPickResult> Trajectory3DLayer::pickNearestSample(
    const osg::Matrixd& localToWindow,
    double screenX,
    double screenY,
    double maxDistancePx) const
{
    if (samples_.empty() || maxDistancePx <= 0.0)
    {
        return std::nullopt;
    }

    const double maxDistanceSq = maxDistancePx * maxDistancePx;
    double bestDistanceSq = maxDistanceSq;
    int bestIndex = -1;
    const int first = firstVisibleIndex();
    const int end = sampleCount();
    for (int index = first; index < end; ++index)
    {
        osg::Vec3d localPosition;
        if (!displayPositionForSample(index, localPosition))
        {
            continue;
        }
        const osg::Vec3d projected = localPosition * localToWindow;
        if (!finiteVec3(projected) || projected.z() < 0.0 || projected.z() > 1.0)
        {
            continue;
        }
        const double dx = projected.x() - screenX;
        const double dy = projected.y() - screenY;
        const double distanceSq = dx * dx + dy * dy;
        if (distanceSq <= bestDistanceSq)
        {
            bestDistanceSq = distanceSq;
            bestIndex = index;
        }
    }

    if (bestIndex < 0)
    {
        return std::nullopt;
    }

    TrajectoryPickResult result;
    result.sampleIndex = bestIndex;
    result.sample = samples_[static_cast<std::size_t>(bestIndex)].navigation;
    result.screenDistancePx = std::sqrt(bestDistanceSq);
    return result;
}

void Trajectory3DLayer::setSelectedSampleIndex(int sampleIndex)
{
    const int sanitized = (sampleIndex >= 0 && sampleIndex < sampleCount()) ? sampleIndex : -1;
    if (selected_sample_index_ == sanitized)
    {
        return;
    }
    selected_sample_index_ = sanitized;
    updateSelectedMarkerGeometry();
}

osg::Node* Trajectory3DLayer::node()
{
    return geode_.get();
}

const osg::Node* Trajectory3DLayer::node() const
{
    return geode_.get();
}

void Trajectory3DLayer::rebuildSegments()
{
    ++full_rebuild_count_;
    geode_->removeDrawables(0, geode_->getNumDrawables());
    sphere_marker_stride_ = sphereMarkerStride();
    if (samples_.empty())
    {
        segments_.clear();
        updateSelectedMarkerGeometry();
        return;
    }

    segments_.clear();
    const VaporView::Geo::HeatRange heatRange = resolvedHeatRange();
    for (int firstSample = 0; firstSample < sampleCount(); firstSample += kSegmentSize)
    {
        TrajectorySegment segment;
        segment.firstSampleIndex = firstSample;
        segment.sampleCount = (std::min)(kSegmentSize, sampleCount() - firstSample);
        for (int index = firstSample;
             index < firstSample + segment.sampleCount;
             ++index)
        {
            segment.hasLineDiscontinuity = segment.hasLineDiscontinuity || !isLineSample(index);
        }
        rebuildSegmentGeometry(segment, heatRange);
        geode_->addDrawable(segment.geometry.get());
        geode_->addDrawable(segment.sphereGeometry.get());
        segments_.push_back(segment);
    }
    updateSelectedMarkerGeometry();
    applySegmentVisibility();
}

void Trajectory3DLayer::rebuildSegmentGeometry(
    TrajectorySegment& segment,
    const VaporView::Geo::HeatRange& heatRange)
{
    ++segment_geometry_rebuild_count_;
    osg::ref_ptr<osg::Geometry> geometry = segment.geometry;
    if (!geometry.valid())
    {
        geometry = new osg::Geometry;
    }
    geometry->removePrimitiveSet(0, geometry->getNumPrimitiveSets());

    osg::ref_ptr<osg::Vec3dArray> vertices = new osg::Vec3dArray;
    osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
    osg::ref_ptr<osg::FloatArray> heatValues = new osg::FloatArray;
    osg::ref_ptr<osg::FloatArray> heatValidity = new osg::FloatArray;
    const int first = (std::max)(segment.firstSampleIndex, firstVisibleIndex());
    const int end = (std::min)(sampleCount(), segment.firstSampleIndex + segment.sampleCount);
    const int vertexCount = (std::max)(0, end - first);
    vertices->reserve(static_cast<std::size_t>(heat_rendering_enabled_ ? vertexCount * 2 : vertexCount));
    colors->reserve(vertices->capacity());

    auto heatColorForIndex = [&](int index) {
        const VaporView::Geo::TrajectoryRenderSample& sample = samples_[static_cast<std::size_t>(index)];
        return heatColorToVec4(VaporView::Geo::heatColorForValue(
            VaporView::Geo::metricValue(sample, heat_metric_),
            heatRange,
            heat_palette_));
    };
    auto appendHeatAttribute = [&](int index) {
        const auto value = VaporView::Geo::metricValue(
            samples_[static_cast<std::size_t>(index)], heat_metric_);
        heatValues->push_back(static_cast<float>(value.value_or(0.0)));
        heatValidity->push_back(value.has_value() ? 1.0f : 0.0f);
    };

    if (heat_rendering_enabled_)
    {
        const int segmentStartVertex = static_cast<int>(vertices->size());
        int lineStart = first + 1;
        if (first == segment.firstSampleIndex
            && first > 0
            && first > firstVisibleIndex())
        {
            lineStart = first;
        }
        for (int index = lineStart; index < end; ++index)
        {
            if (!isLineSample(index) || !isLineSample(index - 1))
            {
                continue;
            }
            osg::Vec3d previousPosition;
            osg::Vec3d currentPosition;
            if (!displayPositionForSample(index - 1, previousPosition)
                || !displayPositionForSample(index, currentPosition))
            {
                continue;
            }
            const std::optional<double> previousValue =
                VaporView::Geo::metricValue(samples_[static_cast<std::size_t>(index - 1)], heat_metric_);
            const std::optional<double> currentValue =
                VaporView::Geo::metricValue(samples_[static_cast<std::size_t>(index)], heat_metric_);
            osg::Vec4 previousColor = heatColorToVec4(VaporView::Geo::neutralHeatColor());
            osg::Vec4 currentColor = previousColor;
            if (previousValue.has_value() && currentValue.has_value())
            {
                previousColor = heatColorForIndex(index - 1);
                currentColor = heatColorForIndex(index);
            }
            else if (previousValue.has_value())
            {
                previousColor = heatColorForIndex(index - 1);
                currentColor = previousColor;
            }
            else if (currentValue.has_value())
            {
                currentColor = heatColorForIndex(index);
                previousColor = currentColor;
            }
            vertices->push_back(previousPosition);
            colors->push_back(previousColor);
            appendHeatAttribute(index - 1);
            vertices->push_back(currentPosition);
            colors->push_back(currentColor);
            appendHeatAttribute(index);
        }
        const int heatLineVertexCount = static_cast<int>(vertices->size()) - segmentStartVertex;
        if (heatLineVertexCount >= 2)
        {
            geometry->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINES,
                                                          segmentStartVertex,
                                                          static_cast<GLsizei>(heatLineVertexCount)));
        }
    }
    else
    {
        int runStartVertex = -1;
        int runVertexCount = 0;
        auto flushLineRun = [&]() {
            if (runVertexCount >= 2)
            {
                geometry->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINE_STRIP,
                                                              runStartVertex,
                                                              static_cast<GLsizei>(runVertexCount)));
            }
            runStartVertex = -1;
            runVertexCount = 0;
        };

        if (first == segment.firstSampleIndex
            && first > 0
            && isLineSample(first - 1)
            && isLineSample(first))
        {
            const VaporView::Geo::NavSample& previous = samples_[static_cast<std::size_t>(first - 1)].navigation;
            vertices->push_back(samplePosition(previous,
                                               use_world_coordinates_,
                                               has_world_origin_,
                                               world_origin_));
            colors->push_back(qualityColor(previous));
            runStartVertex = 0;
            runVertexCount = 1;
        }

        for (int index = first; index < end; ++index)
        {
            const VaporView::Geo::NavSample& sample = samples_[static_cast<std::size_t>(index)].navigation;
            const osg::Vec3d position =
                samplePosition(sample, use_world_coordinates_, has_world_origin_, world_origin_);
            if (isLineSample(index))
            {
                if (runStartVertex < 0)
                {
                    runStartVertex = static_cast<int>(vertices->size());
                }
                vertices->push_back(position);
                colors->push_back(qualityColor(sample));
                ++runVertexCount;
                continue;
            }

            flushLineRun();
        }
        flushLineRun();
    }

    geometry->setVertexArray(vertices.get());
    geometry->setColorArray(colors.get(), osg::Array::BIND_PER_VERTEX);
    if (heat_rendering_enabled_)
    {
        geometry->setVertexAttribArray(kHeatValueAttribute,
                                       heatValues.get(),
                                       osg::Array::BIND_PER_VERTEX);
        geometry->setVertexAttribArray(kHeatValidAttribute,
                                       heatValidity.get(),
                                       osg::Array::BIND_PER_VERTEX);
    }
    configureGeometryState(*geometry);
    configureHeatRenderingState(*geometry);
    geometry->dirtyBound();
    segment.geometry = geometry;

    osg::ref_ptr<osg::Geometry> sphereGeometry = segment.sphereGeometry;
    if (!sphereGeometry.valid())
    {
        sphereGeometry = new osg::Geometry;
    }
    sphereGeometry->removePrimitiveSet(0, sphereGeometry->getNumPrimitiveSets());
    osg::ref_ptr<osg::Vec3dArray> pointVertices = new osg::Vec3dArray;
    osg::ref_ptr<osg::Vec4Array> pointColors = new osg::Vec4Array;
    osg::ref_ptr<osg::FloatArray> pointHeatValues = new osg::FloatArray;
    osg::ref_ptr<osg::FloatArray> pointHeatValidity = new osg::FloatArray;
    osg::ref_ptr<osg::DrawElementsUInt> sphereIndices =
        new osg::DrawElementsUInt(osg::PrimitiveSet::TRIANGLES);
    if (!heat_rendering_enabled_)
    {
        std::size_t markerCount = 0;
        for (int index = first; index < end; ++index)
        {
            if (shouldRenderSphereMarker(index))
            {
                ++markerCount;
            }
        }
        pointVertices->reserve(markerCount * kIcosphereVertexCount);
        pointColors->reserve(markerCount * kIcosphereVertexCount);
        sphereIndices->reserve(markerCount * kIcosphereIndexCount);
    }
    else
    {
        pointVertices->reserve(static_cast<std::size_t>(vertexCount));
        pointColors->reserve(static_cast<std::size_t>(vertexCount));
    }
    segment.sphereMarkerCount = 0;
    for (int index = first; index < end; ++index)
    {
        if (!heat_rendering_enabled_ && !shouldRenderSphereMarker(index))
        {
            continue;
        }
        osg::Vec3d position;
        if (!displayPositionForSample(index, position))
        {
            continue;
        }
        if (heat_rendering_enabled_)
        {
            pointVertices->push_back(position);
            pointColors->push_back(heatColorForIndex(index));
            const auto value = VaporView::Geo::metricValue(
                samples_[static_cast<std::size_t>(index)], heat_metric_);
            pointHeatValues->push_back(static_cast<float>(value.value_or(0.0)));
            pointHeatValidity->push_back(value.has_value() ? 1.0f : 0.0f);
        }
        else
        {
            const VaporView::Geo::NavSample& sample = samples_[static_cast<std::size_t>(index)].navigation;
            appendIcosphere(*pointVertices,
                            *pointColors,
                            *sphereIndices,
                            position,
                            kTrajectorySphereRadiusM,
                            qualityColor(sample));
        }
        ++segment.sphereMarkerCount;
    }
    sphereGeometry->setVertexArray(pointVertices.get());
    sphereGeometry->setColorArray(pointColors.get(), osg::Array::BIND_PER_VERTEX);
    if (heat_rendering_enabled_)
    {
        sphereGeometry->setVertexAttribArray(kHeatValueAttribute,
                                             pointHeatValues.get(),
                                             osg::Array::BIND_PER_VERTEX);
        sphereGeometry->setVertexAttribArray(kHeatValidAttribute,
                                             pointHeatValidity.get(),
                                             osg::Array::BIND_PER_VERTEX);
    }
    if (!pointVertices->empty())
    {
        if (heat_rendering_enabled_)
        {
            sphereGeometry->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::POINTS,
                                                                0,
                                                                static_cast<GLsizei>(pointVertices->size())));
        }
        else if (!sphereIndices->empty())
        {
            sphereGeometry->addPrimitiveSet(sphereIndices.get());
        }
    }
    configureSphereMarkerState(*sphereGeometry);
    configureHeatRenderingState(*sphereGeometry);
    sphereGeometry->dirtyBound();
    segment.sphereGeometry = sphereGeometry;
}

bool Trajectory3DLayer::appendLineSampleGeometry(TrajectorySegment& segment, int sampleIndex)
{
    if (!segment.geometry.valid()
        || segment.hasLineDiscontinuity
        || !isLineSample(sampleIndex)
        || (sampleIndex > segment.firstSampleIndex && !isLineSample(sampleIndex - 1)))
    {
        return false;
    }

    auto* vertices = dynamic_cast<osg::Vec3dArray*>(segment.geometry->getVertexArray());
    auto* colors = dynamic_cast<osg::Vec4Array*>(segment.geometry->getColorArray());
    if (!vertices || !colors)
    {
        return false;
    }
    for (unsigned int primitiveIndex = 0;
         primitiveIndex < segment.geometry->getNumPrimitiveSets();
         ++primitiveIndex)
    {
        if (segment.geometry->getPrimitiveSet(primitiveIndex)->getMode() == osg::PrimitiveSet::POINTS)
        {
            return false;
        }
    }

    const VaporView::Geo::NavSample& sample = samples_[static_cast<std::size_t>(sampleIndex)].navigation;
    vertices->push_back(samplePosition(sample,
                                       use_world_coordinates_,
                                       has_world_origin_,
                                       world_origin_));
    colors->push_back(qualityColor(sample));
    if (segment.geometry->getNumPrimitiveSets() == 0 && vertices->size() >= 2)
    {
        segment.geometry->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINE_STRIP,
                                                               0,
                                                               static_cast<GLsizei>(vertices->size())));
    }
    else if (segment.geometry->getNumPrimitiveSets() == 1)
    {
        auto* line = dynamic_cast<osg::DrawArrays*>(segment.geometry->getPrimitiveSet(0));
        if (!line || line->getMode() != osg::PrimitiveSet::LINE_STRIP)
        {
            return false;
        }
        line->setCount(static_cast<GLsizei>(vertices->size()));
        line->dirty();
    }
    vertices->dirty();
    colors->dirty();
    segment.geometry->dirtyBound();
    return true;
}

bool Trajectory3DLayer::appendSphereMarkerGeometry(TrajectorySegment& segment, int sampleIndex)
{
    if (!segment.sphereGeometry.valid() || !shouldRenderSphereMarker(sampleIndex))
    {
        return false;
    }
    auto* vertices = dynamic_cast<osg::Vec3dArray*>(segment.sphereGeometry->getVertexArray());
    auto* colors = dynamic_cast<osg::Vec4Array*>(segment.sphereGeometry->getColorArray());
    osg::DrawElementsUInt* indices = nullptr;
    if (segment.sphereGeometry->getNumPrimitiveSets() > 0)
    {
        indices = dynamic_cast<osg::DrawElementsUInt*>(segment.sphereGeometry->getPrimitiveSet(0));
    }
    if (!vertices || !colors)
    {
        return false;
    }
    if (!indices)
    {
        osg::ref_ptr<osg::DrawElementsUInt> created =
            new osg::DrawElementsUInt(osg::PrimitiveSet::TRIANGLES);
        segment.sphereGeometry->addPrimitiveSet(created.get());
        indices = created.get();
    }

    osg::Vec3d position;
    if (!displayPositionForSample(sampleIndex, position))
    {
        return false;
    }
    const VaporView::Geo::NavSample& sample = samples_[static_cast<std::size_t>(sampleIndex)].navigation;
    appendIcosphere(*vertices,
                    *colors,
                    *indices,
                    position,
                    kTrajectorySphereRadiusM,
                    qualityColor(sample));
    ++segment.sphereMarkerCount;
    vertices->dirty();
    colors->dirty();
    indices->dirty();
    segment.sphereGeometry->dirtyBound();
    return true;
}

void Trajectory3DLayer::configureGeometryState(osg::Geometry& geometry)
{
    osg::StateSet* stateSet = geometry.getOrCreateStateSet();
    osg::ref_ptr<osg::LineWidth> lineWidth = new osg::LineWidth(track_line_width_);
    stateSet->setAttributeAndModes(lineWidth.get(), osg::StateAttribute::ON);
    stateSet->setMode(GL_LIGHTING,
                      osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
    stateSet->setMode(GL_LINE_SMOOTH,
                      osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
    stateSet->setMode(GL_BLEND, osg::StateAttribute::ON);
    stateSet->setMode(GL_DEPTH_TEST,
                      osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
    stateSet->setRenderBinDetails(1000, "RenderBin");
}

void Trajectory3DLayer::configureSphereMarkerState(osg::Geometry& geometry)
{
    osg::StateSet* stateSet = geometry.getOrCreateStateSet();
    osg::ref_ptr<osg::Point> pointSize = new osg::Point(track_point_size_);
    stateSet->setAttributeAndModes(pointSize.get(), osg::StateAttribute::ON);
    stateSet->setMode(GL_LIGHTING,
                      osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
    stateSet->setMode(GL_BLEND, osg::StateAttribute::ON);
    stateSet->setMode(GL_DEPTH_TEST,
                      osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
    stateSet->setRenderBinDetails(1001, "RenderBin");
}

void Trajectory3DLayer::updateSelectedMarkerGeometry()
{
    if (!selected_marker_geometry_.valid())
    {
        selected_marker_geometry_ = new osg::Geometry;
    }

    selected_marker_geometry_->removePrimitiveSet(0, selected_marker_geometry_->getNumPrimitiveSets());
    osg::ref_ptr<osg::Vec3dArray> vertices = new osg::Vec3dArray;
    osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
    osg::ref_ptr<osg::DrawElementsUInt> indices =
        new osg::DrawElementsUInt(osg::PrimitiveSet::TRIANGLES);

    osg::Vec3d position;
    if (displayPositionForSample(selected_sample_index_, position))
    {
        appendIcosphere(*vertices,
                        *colors,
                        *indices,
                        position,
                        kSelectedTrajectorySphereRadiusM,
                        selectedMarkerColor());
        selected_marker_geometry_->setVertexArray(vertices.get());
        selected_marker_geometry_->setColorArray(colors.get(), osg::Array::BIND_PER_VERTEX);
        selected_marker_geometry_->addPrimitiveSet(indices.get());
        configureSphereMarkerState(*selected_marker_geometry_);
        if (geode_->getDrawableIndex(selected_marker_geometry_.get()) == geode_->getNumDrawables())
        {
            geode_->addDrawable(selected_marker_geometry_.get());
        }
    }
    else
    {
        selected_marker_geometry_->setVertexArray(vertices.get());
        selected_marker_geometry_->setColorArray(colors.get(), osg::Array::BIND_PER_VERTEX);
        geode_->removeDrawable(selected_marker_geometry_.get());
    }
    selected_marker_geometry_->dirtyBound();
}

void Trajectory3DLayer::trimToVisibleLimit()
{
    bool removed = false;
    while (sampleCount() > max_visible_samples_)
    {
        removeOldestSample();
        removed = true;
    }
    if (removed)
    {
        updateHeatRenderingState();
    }
}

void Trajectory3DLayer::removeOldestSample()
{
    if (samples_.empty())
    {
        return;
    }
    const std::uint64_t sequence = sample_sequences_.front();
    adjustQualityStats(0, -1);
    if (heat_rendering_enabled_)
    {
        evictHeatStatistics(
            sequence,
            VaporView::Geo::metricValue(samples_.front(), heat_metric_).has_value());
    }
    samples_.pop_front();
    sample_sequences_.pop_front();
    line_sample_flags_.pop_front();
    if (last_line_sample_index_ >= 0)
    {
        --last_line_sample_index_;
    }
    if (selected_sample_index_ == 0)
    {
        selected_sample_index_ = -1;
    }
    else if (selected_sample_index_ > 0)
    {
        --selected_sample_index_;
    }
    if (segments_.empty())
    {
        updateSelectedMarkerGeometry();
        return;
    }

    --segments_.front().sampleCount;
    for (std::size_t index = 1; index < segments_.size(); ++index)
    {
        --segments_[index].firstSampleIndex;
    }
    if (segments_.front().sampleCount <= 0)
    {
        geode_->removeDrawable(segments_.front().geometry.get());
        geode_->removeDrawable(segments_.front().sphereGeometry.get());
        segments_.erase(segments_.begin());
    }
    else
    {
        const VaporView::Geo::HeatRange heatRange = resolvedHeatRange();
        rebuildSegmentGeometry(segments_.front(), heatRange);
    }
    updateSelectedMarkerGeometry();
}

void Trajectory3DLayer::appendSegment()
{
    TrajectorySegment segment;
    segment.firstSampleIndex = sampleCount() - 1;
    segment.sampleCount = 0;
    segment.geometry = new osg::Geometry;
    segment.sphereGeometry = new osg::Geometry;
    geode_->addDrawable(segment.geometry.get());
    geode_->addDrawable(segment.sphereGeometry.get());
    segments_.push_back(segment);
}

int Trajectory3DLayer::firstVisibleIndex() const
{
    return (std::max)(0, sampleCount() - visibleSampleCount());
}

int Trajectory3DLayer::sphereMarkerStride() const
{
    const int visible = visibleSampleCount();
    if (visible <= kMaxSolidSphereMarkers)
    {
        return 1;
    }
    return (std::max)(1, (visible + kMaxSolidSphereMarkers - 1) / kMaxSolidSphereMarkers);
}

bool Trajectory3DLayer::shouldUseAsLineSample(int index) const
{
    const VaporView::Geo::NavSample& sample = samples_[static_cast<std::size_t>(index)].navigation;
    if (!VaporView::Geo::isUsableForDisplay(sample))
    {
        return false;
    }

    const int previousIndex = last_line_sample_index_;
    if (previousIndex < 0)
    {
        return true;
    }
    return !VaporView::Geo::isLikelyJump(samples_[static_cast<std::size_t>(previousIndex)].navigation, sample);
}

bool Trajectory3DLayer::isLineSample(int index) const
{
    return index >= 0
        && index < static_cast<int>(line_sample_flags_.size())
        && line_sample_flags_[static_cast<std::size_t>(index)] != 0;
}

bool Trajectory3DLayer::shouldRenderSphereMarker(int index) const
{
    if (index < firstVisibleIndex() || index >= sampleCount())
    {
        return false;
    }
    if (index == selected_sample_index_
        || index == firstVisibleIndex()
        || !isLineSample(index))
    {
        return true;
    }
    const int stride = (std::max)(1, sphere_marker_stride_);
    return ((index - firstVisibleIndex()) % stride) == 0;
}

void Trajectory3DLayer::rebuildLineSampleFlags()
{
    line_sample_flags_.clear();
    last_line_sample_index_ = -1;
    for (int index = 0; index < sampleCount(); ++index)
    {
        const VaporView::Geo::NavSample& sample = samples_[static_cast<std::size_t>(index)].navigation;
        bool line = VaporView::Geo::isUsableForDisplay(sample);
        if (line && last_line_sample_index_ >= 0)
        {
            line = !VaporView::Geo::isLikelyJump(
                samples_[static_cast<std::size_t>(last_line_sample_index_)].navigation, sample);
        }
        line_sample_flags_.push_back(line ? 1 : 0);
        if (line)
        {
            last_line_sample_index_ = index;
        }
    }
}

void Trajectory3DLayer::rebuildQualityStats()
{
    quality_stats_ = {};
    for (int index = 0; index < sampleCount(); ++index)
    {
        adjustQualityStats(index, 1);
    }
}

void Trajectory3DLayer::adjustQualityStats(int index, int delta)
{
    const VaporView::Geo::NavSample& sample = samples_[static_cast<std::size_t>(index)].navigation;
    const bool usable = VaporView::Geo::isUsableForDisplay(sample);
    const bool line = isLineSample(index);
    if (!usable)
    {
        quality_stats_.invalidSamples += delta;
        quality_stats_.markerSamples += delta;
        return;
    }
    if (!line)
    {
        quality_stats_.jumpSamples += delta;
        quality_stats_.markerSamples += delta;
        return;
    }

    quality_stats_.lineSamples += delta;
    switch (sample.fixQuality)
    {
    case VaporView::Geo::FixQuality::Fixed:
        quality_stats_.fixedSamples += delta;
        break;
    case VaporView::Geo::FixQuality::Float:
        quality_stats_.floatSamples += delta;
        break;
    case VaporView::Geo::FixQuality::Dgps:
        quality_stats_.dgpsSamples += delta;
        break;
    case VaporView::Geo::FixQuality::Single:
        quality_stats_.singleSamples += delta;
        break;
    case VaporView::Geo::FixQuality::Unknown:
        quality_stats_.unknownSamples += delta;
        break;
    case VaporView::Geo::FixQuality::Invalid:
        break;
    }
}

bool Trajectory3DLayer::segmentIsVisible(const TrajectorySegment& segment) const
{
    const int visibleFirst = firstVisibleIndex();
    const int segmentEnd = segment.firstSampleIndex + segment.sampleCount;
    return segmentEnd > visibleFirst;
}

void Trajectory3DLayer::applySegmentVisibility()
{
    for (TrajectorySegment& segment : segments_)
    {
        const bool visible = segmentIsVisible(segment);
        if (segment.geometry.valid())
        {
            segment.geometry->setNodeMask(visible && track_line_visible_ ? ~0u : 0u);
        }
        if (segment.sphereGeometry.valid())
        {
            segment.sphereGeometry->setNodeMask(visible && track_points_visible_ ? ~0u : 0u);
        }
    }
}

VaporView::Geo::HeatRange Trajectory3DLayer::resolvedHeatRange() const
{
    if (heat_range_override_.has_value())
    {
        return heat_range_override_.value();
    }
    if (!heat_rendering_enabled_)
    {
        return {};
    }
    if (!heat_range_cache_valid_)
    {
        const_cast<Trajectory3DLayer*>(this)->recomputeHeatRange();
    }
    return heat_range_cache_;
}

void Trajectory3DLayer::invalidateHeatRange()
{
    resetHeatStatistics();
}

void Trajectory3DLayer::recomputeHeatRange()
{
    const int incrementalAppendCount = heat_range_incremental_append_count_;
    resetHeatStatistics();
    ++heat_range_full_scan_count_;
    for (std::size_t index = 0; index < samples_.size(); ++index)
    {
        appendHeatStatistics(samples_[index], sample_sequences_[index]);
    }
    heat_range_incremental_append_count_ = incrementalAppendCount;
    heat_range_cache_valid_ = true;
}

void Trajectory3DLayer::resetHeatStatistics()
{
    heat_range_cache_ = {};
    heat_range_cache_valid_ = false;
    heat_min_deque_.clear();
    heat_max_deque_.clear();
    heat_valid_count_ = 0;
}

void Trajectory3DLayer::updateHeatRangeCacheFromExtrema()
{
    heat_range_cache_ = {};
    if (!heat_min_deque_.empty() && !heat_max_deque_.empty())
    {
        heat_range_cache_.valid = true;
        heat_range_cache_.minimum = heat_min_deque_.front().value;
        heat_range_cache_.maximum = heat_max_deque_.front().value;
        heat_range_cache_.validCount = heat_valid_count_;
    }
    heat_range_cache_valid_ = true;
}

void Trajectory3DLayer::appendHeatStatistics(
    const VaporView::Geo::TrajectoryRenderSample& sample,
    std::uint64_t sequence)
{
    const std::optional<double> value = VaporView::Geo::metricValue(sample, heat_metric_);
    if (value.has_value())
    {
        const HeatExtremaEntry entry{sequence, value.value()};
        while (!heat_min_deque_.empty() && heat_min_deque_.back().value >= entry.value)
        {
            heat_min_deque_.pop_back();
        }
        while (!heat_max_deque_.empty() && heat_max_deque_.back().value <= entry.value)
        {
            heat_max_deque_.pop_back();
        }
        heat_min_deque_.push_back(entry);
        heat_max_deque_.push_back(entry);
        ++heat_valid_count_;
    }
    ++heat_range_incremental_append_count_;
    updateHeatRangeCacheFromExtrema();
}

void Trajectory3DLayer::evictHeatStatistics(std::uint64_t sequence, bool hasValidValue)
{
    if (!heat_min_deque_.empty() && heat_min_deque_.front().sequence == sequence)
    {
        heat_min_deque_.pop_front();
    }
    if (!heat_max_deque_.empty() && heat_max_deque_.front().sequence == sequence)
    {
        heat_max_deque_.pop_front();
    }
    if (hasValidValue && heat_valid_count_ > 0)
    {
        --heat_valid_count_;
    }
    ++heat_range_eviction_count_;
    updateHeatRangeCacheFromExtrema();
}

void Trajectory3DLayer::updateHeatRenderingState()
{
    if (!heat_rendering_enabled_)
    {
        return;
    }

    if (!heat_program_.valid())
    {
        heat_program_ = new osg::Program;
        heat_program_->addShader(new osg::Shader(osg::Shader::VERTEX, kHeatVertexShader));
        heat_program_->addShader(new osg::Shader(osg::Shader::FRAGMENT, kHeatFragmentShader));
        heat_program_->addBindAttribLocation("vaporviewHeatValue", kHeatValueAttribute);
        heat_program_->addBindAttribLocation("vaporviewHeatValid", kHeatValidAttribute);
        heat_palette_texture_ = new osg::Texture1D;
        heat_palette_texture_->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
        heat_palette_texture_->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
        heat_palette_texture_->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
        heat_minimum_uniform_ = new osg::Uniform("vaporviewHeatMinimum", 0.0f);
        heat_maximum_uniform_ = new osg::Uniform("vaporviewHeatMaximum", 1.0f);
        heat_palette_uniform_ = new osg::Uniform(osg::Uniform::SAMPLER_1D,
                                                  "vaporviewHeatPalette");
        heat_palette_uniform_->set(0);
    }

    const VaporView::Geo::HeatRange range = resolvedHeatRange();
    heat_minimum_uniform_->set(static_cast<float>(range.valid ? range.minimum : 0.0));
    heat_maximum_uniform_->set(static_cast<float>(range.valid ? range.maximum : 1.0));
    if (!heat_palette_texture_dirty_)
    {
        return;
    }

    osg::ref_ptr<osg::Image> image = new osg::Image;
    constexpr int paletteSamples = 256;
    image->allocateImage(paletteSamples, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE);
    for (int index = 0; index < paletteSamples; ++index)
    {
        const auto color = VaporView::Geo::heatPaletteColor(
            static_cast<double>(index) / static_cast<double>(paletteSamples - 1),
            heat_palette_);
        unsigned char* pixel = image->data(index, 0);
        pixel[0] = static_cast<unsigned char>(std::lround(color.r * 255.0f));
        pixel[1] = static_cast<unsigned char>(std::lround(color.g * 255.0f));
        pixel[2] = static_cast<unsigned char>(std::lround(color.b * 255.0f));
        pixel[3] = static_cast<unsigned char>(std::lround(color.a * 255.0f));
    }
    heat_palette_texture_->setImage(image.get());
    heat_palette_texture_dirty_ = false;
}

void Trajectory3DLayer::configureHeatRenderingState(osg::Geometry& geometry)
{
    if (!heat_rendering_enabled_
        || !geometry.getVertexAttribArray(kHeatValueAttribute)
        || !heat_program_.valid())
    {
        return;
    }
    osg::StateSet* stateSet = geometry.getOrCreateStateSet();
    stateSet->setAttributeAndModes(heat_program_.get(), osg::StateAttribute::ON);
    stateSet->setTextureAttributeAndModes(0,
                                          heat_palette_texture_.get(),
                                          osg::StateAttribute::ON);
    stateSet->addUniform(heat_minimum_uniform_.get());
    stateSet->addUniform(heat_maximum_uniform_.get());
    stateSet->addUniform(heat_palette_uniform_.get());
}

} // namespace VaporView::Map3D
