#include "map3d/Trajectory3DLayer.h"

#include "geo/TrajectoryQuality.h"

#include <osg/Geometry>
#include <osg/Geode>
#include <osg/LineWidth>
#include <osg/Point>

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

osg::Vec3d samplePosition(const VaporView::Geo::NavSample& sample, bool useWorldCoordinates)
{
    if (useWorldCoordinates && hasWorldPosition(sample))
    {
        return osg::Vec3d(sample.ecefXM, sample.ecefYM, sample.ecefZM);
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
        return osg::Vec4(0.1f, 0.85f, 0.25f, 1.0f);
    case VaporView::Geo::FixQuality::Float:
        return osg::Vec4(1.0f, 0.75f, 0.1f, 1.0f);
    case VaporView::Geo::FixQuality::Dgps:
        return osg::Vec4(0.2f, 0.55f, 1.0f, 1.0f);
    default:
        return osg::Vec4(0.85f, 0.85f, 0.85f, 1.0f);
    }
}

bool isJumpSample(const std::vector<VaporView::Geo::NavSample>& samples, int index)
{
    if (index <= 0)
    {
        return false;
    }
    return VaporView::Geo::isLikelyJump(samples[static_cast<std::size_t>(index - 1)],
                                        samples[static_cast<std::size_t>(index)]);
}

bool isLineSample(const std::vector<VaporView::Geo::NavSample>& samples, int index)
{
    const VaporView::Geo::NavSample& sample = samples[static_cast<std::size_t>(index)];
    return VaporView::Geo::isUsableForDisplay(sample) && !isJumpSample(samples, index);
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
    segments_.clear();
    geode_->removeDrawables(0, geode_->getNumDrawables());
}

void Trajectory3DLayer::appendSample(const VaporView::Geo::NavSample& sample)
{
    const int previousFirstVisibleIndex = firstVisibleIndex();
    samples_.push_back(sample);
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

    osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
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
        const osg::Vec3d position = samplePosition(sample, use_world_coordinates_);
        if (isLineSample(samples_, index))
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

    osg::ref_ptr<osg::LineWidth> lineWidth = new osg::LineWidth(2.5f);
    geometry->getOrCreateStateSet()->setAttributeAndModes(lineWidth.get(), osg::StateAttribute::ON);
    osg::ref_ptr<osg::Point> pointSize = new osg::Point(7.0f);
    geometry->getOrCreateStateSet()->setAttributeAndModes(pointSize.get(), osg::StateAttribute::ON);

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
