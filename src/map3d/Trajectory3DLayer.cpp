#include "map3d/Trajectory3DLayer.h"

#include "geo/TrajectoryQuality.h"

#include <osg/Geometry>
#include <osg/Geode>
#include <osg/LineWidth>
#include <osg/Point>
#include <osg/PrimitiveSet>
#include <osg/StateSet>

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
constexpr double kTrajectorySphereRadiusM = 0.5;
constexpr double kSelectedTrajectorySphereRadiusM = 1.0;

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
    line_sample_flags_.clear();
    last_line_sample_index_ = -1;
    selected_sample_index_ = -1;
    sphere_marker_stride_ = 1;
    segments_.clear();
    quality_stats_ = {};
    geode_->removeDrawables(0, geode_->getNumDrawables());
    updateSelectedMarkerGeometry();
}

void Trajectory3DLayer::appendSample(const VaporView::Geo::NavSample& sample)
{
    const int previousSphereMarkerStride = sphere_marker_stride_;
    samples_.push_back(sample);
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
    const bool lineUpdated = !needsNewSegment && appendLineSampleGeometry(segment, sampleCount() - 1);
    const bool sphereUpdated =
        !needsNewSegment
        && (!shouldRenderSphereMarker(sampleCount() - 1)
            || appendSphereMarkerGeometry(segment, sampleCount() - 1));
    if (needsNewSegment || !lineUpdated || !sphereUpdated)
    {
        rebuildSegmentGeometry(segment);
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

void Trajectory3DLayer::appendSamples(const std::vector<VaporView::Geo::NavSample>& samples)
{
    if (samples.empty())
    {
        return;
    }
    for (const VaporView::Geo::NavSample& sample : samples)
    {
        appendSample(sample);
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
    position = samplePosition(samples_[static_cast<std::size_t>(sampleIndex)],
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
    result.sample = samples_[static_cast<std::size_t>(bestIndex)];
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
    geode_->removeDrawables(0, geode_->getNumDrawables());
    sphere_marker_stride_ = sphereMarkerStride();
    if (samples_.empty())
    {
        segments_.clear();
        updateSelectedMarkerGeometry();
        return;
    }

    segments_.clear();
    for (int firstSample = 0; firstSample < sampleCount(); firstSample += kSegmentSize)
    {
        TrajectorySegment segment;
        segment.firstSampleIndex = firstSample;
        segment.sampleCount = (std::min)(kSegmentSize, sampleCount() - firstSample);
        rebuildSegmentGeometry(segment);
        geode_->addDrawable(segment.geometry.get());
        geode_->addDrawable(segment.sphereGeometry.get());
        segments_.push_back(segment);
    }
    updateSelectedMarkerGeometry();
    applySegmentVisibility();
}

void Trajectory3DLayer::rebuildSegmentGeometry(TrajectorySegment& segment)
{
    osg::ref_ptr<osg::Geometry> geometry = segment.geometry;
    if (!geometry.valid())
    {
        geometry = new osg::Geometry;
    }
    geometry->removePrimitiveSet(0, geometry->getNumPrimitiveSets());

    osg::ref_ptr<osg::Vec3dArray> vertices = new osg::Vec3dArray;
    osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
    std::vector<osg::Vec3d> markerPositions;
    const int first = (std::max)(segment.firstSampleIndex, firstVisibleIndex());
    const int end = (std::min)(sampleCount(), segment.firstSampleIndex + segment.sampleCount);
    const int vertexCount = (std::max)(0, end - first);
    vertices->reserve(static_cast<std::size_t>(vertexCount));
    colors->reserve(static_cast<std::size_t>(vertexCount));

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
        const VaporView::Geo::NavSample& previous = samples_[static_cast<std::size_t>(first - 1)];
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
        const VaporView::Geo::NavSample& sample = samples_[static_cast<std::size_t>(index)];
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
        markerPositions.push_back(position);
    }
    flushLineRun();

    if (!markerPositions.empty())
    {
        const int markerStartVertex = static_cast<int>(vertices->size());
        for (const osg::Vec3d& position : markerPositions)
        {
            vertices->push_back(position);
            colors->push_back(markerColor());
        }
        geometry->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::POINTS,
                                                      markerStartVertex,
                                                      static_cast<GLsizei>(markerPositions.size())));
    }

    geometry->setVertexArray(vertices.get());
    geometry->setColorArray(colors.get(), osg::Array::BIND_PER_VERTEX);

    configureGeometryState(*geometry);

    segment.geometry = geometry;

    osg::ref_ptr<osg::Geometry> sphereGeometry = segment.sphereGeometry;
    if (!sphereGeometry.valid())
    {
        sphereGeometry = new osg::Geometry;
    }
    sphereGeometry->removePrimitiveSet(0, sphereGeometry->getNumPrimitiveSets());
    osg::ref_ptr<osg::Vec3dArray> sphereVertices = new osg::Vec3dArray;
    osg::ref_ptr<osg::Vec4Array> sphereColors = new osg::Vec4Array;
    osg::ref_ptr<osg::DrawElementsUInt> sphereIndices =
        new osg::DrawElementsUInt(osg::PrimitiveSet::TRIANGLES);
    segment.sphereMarkerCount = 0;
    for (int index = first; index < end; ++index)
    {
        if (!shouldRenderSphereMarker(index))
        {
            continue;
        }
        const VaporView::Geo::NavSample& sample = samples_[static_cast<std::size_t>(index)];
        osg::Vec3d position;
        if (!displayPositionForSample(index, position))
        {
            continue;
        }
        appendIcosphere(*sphereVertices,
                        *sphereColors,
                        *sphereIndices,
                        position,
                        kTrajectorySphereRadiusM,
                        qualityColor(sample));
        ++segment.sphereMarkerCount;
    }
    if (!sphereVertices->empty())
    {
        sphereGeometry->setVertexArray(sphereVertices.get());
        sphereGeometry->setColorArray(sphereColors.get(), osg::Array::BIND_PER_VERTEX);
        sphereGeometry->addPrimitiveSet(sphereIndices.get());
    }
    else
    {
        sphereGeometry->setVertexArray(sphereVertices.get());
        sphereGeometry->setColorArray(sphereColors.get(), osg::Array::BIND_PER_VERTEX);
    }
    configureSphereMarkerState(*sphereGeometry);
    sphereGeometry->dirtyBound();
    segment.sphereGeometry = sphereGeometry;
}

bool Trajectory3DLayer::appendLineSampleGeometry(TrajectorySegment& segment, int sampleIndex)
{
    if (!segment.geometry.valid()
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

    const VaporView::Geo::NavSample& sample = samples_[static_cast<std::size_t>(sampleIndex)];
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
    const VaporView::Geo::NavSample& sample = samples_[static_cast<std::size_t>(sampleIndex)];
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
    if (stateSet->getAttribute(osg::StateAttribute::LINEWIDTH))
    {
        return;
    }
    osg::ref_ptr<osg::LineWidth> lineWidth = new osg::LineWidth(5.0f);
    stateSet->setAttributeAndModes(lineWidth.get(), osg::StateAttribute::ON);
    osg::ref_ptr<osg::Point> pointSize = new osg::Point(7.0f);
    stateSet->setAttributeAndModes(pointSize.get(), osg::StateAttribute::ON);
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
    while (sampleCount() > max_visible_samples_)
    {
        removeOldestSample();
    }
}

void Trajectory3DLayer::removeOldestSample()
{
    if (samples_.empty())
    {
        return;
    }
    adjustQualityStats(0, -1);
    samples_.pop_front();
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
        rebuildSegmentGeometry(segments_.front());
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
    const VaporView::Geo::NavSample& sample = samples_[static_cast<std::size_t>(index)];
    if (!VaporView::Geo::isUsableForDisplay(sample))
    {
        return false;
    }

    const int previousIndex = last_line_sample_index_;
    if (previousIndex < 0)
    {
        return true;
    }
    return !VaporView::Geo::isLikelyJump(samples_[static_cast<std::size_t>(previousIndex)], sample);
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
        const VaporView::Geo::NavSample& sample = samples_[static_cast<std::size_t>(index)];
        bool line = VaporView::Geo::isUsableForDisplay(sample);
        if (line && last_line_sample_index_ >= 0)
        {
            line = !VaporView::Geo::isLikelyJump(
                samples_[static_cast<std::size_t>(last_line_sample_index_)], sample);
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
    const VaporView::Geo::NavSample& sample = samples_[static_cast<std::size_t>(index)];
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
        const unsigned int mask = segmentIsVisible(segment) ? ~0u : 0u;
        if (segment.geometry.valid())
        {
            segment.geometry->setNodeMask(mask);
        }
        if (segment.sphereGeometry.valid())
        {
            segment.sphereGeometry->setNodeMask(mask);
        }
    }
}

} // namespace VaporView::Map3D
