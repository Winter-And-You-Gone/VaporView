#include "map3d/Trajectory3DLayer.h"

#include "geo/TrajectoryQuality.h"

#include <osg/Geometry>
#include <osg/Geode>
#include <osg/LineWidth>
#include <osg/Point>
#include <osg/StateSet>

#include <algorithm>
#include <cmath>

namespace VaporView::Map3D {
namespace {

constexpr int kSegmentSize = 4096;

bool hasWorldPosition(const VaporView::Geo::NavSample& sample)
{
    return std::isfinite(sample.ecefXM)
        && std::isfinite(sample.ecefYM)
        && std::isfinite(sample.ecefZM);
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

} // namespace

Trajectory3DLayer::Trajectory3DLayer()
    : geode_(new osg::Geode)
{
}

void Trajectory3DLayer::clear()
{
    samples_.clear();
    line_sample_flags_.clear();
    segments_.clear();
    geode_->removeDrawables(0, geode_->getNumDrawables());
}

void Trajectory3DLayer::appendSample(const VaporView::Geo::NavSample& sample)
{
    const int previousFirstVisibleIndex = firstVisibleIndex();
    samples_.push_back(sample);
    line_sample_flags_.push_back(shouldUseAsLineSample(sampleCount() - 1) ? 1 : 0);
    if (segments_.empty() || segments_.back().sampleCount >= kSegmentSize)
    {
        appendSegment();
    }
    TrajectorySegment& segment = segments_.back();
    ++segment.sampleCount;
    rebuildSegmentGeometry(segment);
    if (previousFirstVisibleIndex != firstVisibleIndex())
    {
        rebuildVisibilityBoundarySegment();
    }
    applySegmentVisibility();
}

void Trajectory3DLayer::appendSamples(const std::vector<VaporView::Geo::NavSample>& samples)
{
    if (samples.empty())
    {
        return;
    }
    samples_.insert(samples_.end(), samples.cbegin(), samples.cend());
    rebuildLineSampleFlags();
    rebuildSegments();
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
    const int sanitized = std::max(1000, maxVisibleSamples);
    if (max_visible_samples_ == sanitized)
    {
        return;
    }
    max_visible_samples_ = sanitized;
    rebuildSegments();
}

int Trajectory3DLayer::sampleCount() const
{
    return static_cast<int>(samples_.size());
}

int Trajectory3DLayer::visibleSampleCount() const
{
    return std::min(sampleCount(), max_visible_samples_);
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

TrajectoryQualityStats Trajectory3DLayer::qualityStats() const
{
    TrajectoryQualityStats stats;
    const int first = firstVisibleIndex();
    for (int index = first; index < sampleCount(); ++index)
    {
        const VaporView::Geo::NavSample& sample = samples_[static_cast<std::size_t>(index)];
        const bool usable = VaporView::Geo::isUsableForDisplay(sample);
        const bool line = isLineSample(index);
        if (!usable)
        {
            ++stats.invalidSamples;
            ++stats.markerSamples;
            continue;
        }
        if (!line)
        {
            ++stats.jumpSamples;
            ++stats.markerSamples;
            continue;
        }

        ++stats.lineSamples;
        switch (sample.fixQuality)
        {
        case VaporView::Geo::FixQuality::Fixed:
            ++stats.fixedSamples;
            break;
        case VaporView::Geo::FixQuality::Float:
            ++stats.floatSamples;
            break;
        case VaporView::Geo::FixQuality::Dgps:
            ++stats.dgpsSamples;
            break;
        case VaporView::Geo::FixQuality::Single:
            ++stats.singleSamples;
            break;
        case VaporView::Geo::FixQuality::Invalid:
            ++stats.invalidSamples;
            ++stats.markerSamples;
            --stats.lineSamples;
            break;
        case VaporView::Geo::FixQuality::Unknown:
            ++stats.unknownSamples;
            break;
        }
    }
    return stats;
}

osg::Node* Trajectory3DLayer::node() const
{
    return geode_.get();
}

void Trajectory3DLayer::rebuildSegments()
{
    geode_->removeDrawables(0, geode_->getNumDrawables());
    if (samples_.empty())
    {
        segments_.clear();
        return;
    }

    segments_.clear();
    for (int firstSample = 0; firstSample < sampleCount(); firstSample += kSegmentSize)
    {
        TrajectorySegment segment;
        segment.firstSampleIndex = firstSample;
        segment.sampleCount = std::min(kSegmentSize, sampleCount() - firstSample);
        rebuildSegmentGeometry(segment);
        geode_->addDrawable(segment.geometry.get());
        segments_.push_back(segment);
    }
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
    const int first = std::max(segment.firstSampleIndex, firstVisibleIndex());
    const int end = std::min(sampleCount(), segment.firstSampleIndex + segment.sampleCount);
    const int vertexCount = std::max(0, end - first);
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

    osg::StateSet* stateSet = geometry->getOrCreateStateSet();
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

    segment.geometry = geometry;
}

void Trajectory3DLayer::rebuildVisibilityBoundarySegment()
{
    if (segments_.empty())
    {
        return;
    }

    const int visibleFirst = firstVisibleIndex();
    for (TrajectorySegment& segment : segments_)
    {
        const int segmentEnd = segment.firstSampleIndex + segment.sampleCount;
        if (segment.firstSampleIndex <= visibleFirst && segmentEnd > visibleFirst)
        {
            rebuildSegmentGeometry(segment);
            return;
        }
    }
}

void Trajectory3DLayer::appendSegment()
{
    TrajectorySegment segment;
    segment.firstSampleIndex = sampleCount() - 1;
    segment.sampleCount = 0;
    segment.geometry = new osg::Geometry;
    geode_->addDrawable(segment.geometry.get());
    segments_.push_back(segment);
}

int Trajectory3DLayer::firstVisibleIndex() const
{
    return std::max(0, sampleCount() - visibleSampleCount());
}

int Trajectory3DLayer::previousLineSampleIndex(int index) const
{
    for (int previousIndex = index - 1; previousIndex >= 0; --previousIndex)
    {
        if (isLineSample(previousIndex))
        {
            return previousIndex;
        }
    }
    return -1;
}

bool Trajectory3DLayer::shouldUseAsLineSample(int index) const
{
    const VaporView::Geo::NavSample& sample = samples_[static_cast<std::size_t>(index)];
    if (!VaporView::Geo::isUsableForDisplay(sample))
    {
        return false;
    }

    const int previousIndex = previousLineSampleIndex(index);
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

void Trajectory3DLayer::rebuildLineSampleFlags()
{
    line_sample_flags_.clear();
    line_sample_flags_.reserve(samples_.size());
    for (int index = 0; index < sampleCount(); ++index)
    {
        line_sample_flags_.push_back(shouldUseAsLineSample(index) ? 1 : 0);
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
        if (!segment.geometry.valid())
        {
            continue;
        }
        segment.geometry->setNodeMask(segmentIsVisible(segment) ? ~0u : 0u);
    }
}

} // namespace VaporView::Map3D
