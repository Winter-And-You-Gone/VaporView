#include "map3d/Trajectory3DLayer.h"

#include "geo/TrajectoryQuality.h"

#include <osg/Geometry>
#include <osg/Geode>
#include <osg/LineWidth>

#include <cmath>
#include <algorithm>

namespace VaporView::Map3D {
namespace {

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

} // namespace

Trajectory3DLayer::Trajectory3DLayer()
    : geode_(new osg::Geode)
{
}

void Trajectory3DLayer::clear()
{
    samples_.clear();
    rebuildGeometry();
}

void Trajectory3DLayer::appendSample(const VaporView::Geo::NavSample& sample)
{
    samples_.push_back(sample);
    rebuildGeometry();
}

void Trajectory3DLayer::appendSamples(const std::vector<VaporView::Geo::NavSample>& samples)
{
    samples_.insert(samples_.end(), samples.cbegin(), samples.cend());
    rebuildGeometry();
}

void Trajectory3DLayer::setUseWorldCoordinates(bool enabled)
{
    if (use_world_coordinates_ == enabled)
    {
        return;
    }
    use_world_coordinates_ = enabled;
    rebuildGeometry();
}

void Trajectory3DLayer::setMaxVisibleSamples(int maxVisibleSamples)
{
    const int sanitized = std::max(1000, maxVisibleSamples);
    if (max_visible_samples_ == sanitized)
    {
        return;
    }
    max_visible_samples_ = sanitized;
    rebuildGeometry();
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

osg::Node* Trajectory3DLayer::node() const
{
    return geode_.get();
}

void Trajectory3DLayer::rebuildGeometry()
{
    geode_->removeDrawables(0, geode_->getNumDrawables());
    if (samples_.empty())
    {
        return;
    }

    osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
    osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
    osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
    const int visibleCount = visibleSampleCount();
    const int firstVisibleIndex = std::max(0, sampleCount() - visibleCount);
    vertices->reserve(static_cast<std::size_t>(visibleCount));
    colors->reserve(static_cast<std::size_t>(visibleCount));

    for (auto it = samples_.cbegin() + firstVisibleIndex; it != samples_.cend(); ++it)
    {
        const VaporView::Geo::NavSample& sample = *it;
        vertices->push_back(samplePosition(sample, use_world_coordinates_));
        colors->push_back(qualityColor(sample));
    }

    geometry->setVertexArray(vertices.get());
    geometry->setColorArray(colors.get(), osg::Array::BIND_PER_VERTEX);
    geometry->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINE_STRIP, 0, static_cast<GLsizei>(vertices->size())));

    osg::ref_ptr<osg::LineWidth> lineWidth = new osg::LineWidth(2.5f);
    geometry->getOrCreateStateSet()->setAttributeAndModes(lineWidth.get(), osg::StateAttribute::ON);

    geode_->addDrawable(geometry.get());
}

} // namespace VaporView::Map3D
