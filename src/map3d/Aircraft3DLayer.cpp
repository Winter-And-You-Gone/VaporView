#include "map3d/Aircraft3DLayer.h"

#include <osg/Geode>
#include <osg/Math>
#include <osg/MatrixTransform>
#include <osg/ShapeDrawable>

#include <cmath>

namespace VaporView::Map3D {
namespace {

osg::Vec3d samplePosition(const VaporView::Geo::NavSample& sample)
{
    if (sample.hasNed())
    {
        return osg::Vec3d(sample.nedEM, sample.nedNM, -sample.nedDM);
    }
    return osg::Vec3d(sample.lonDeg * 100000.0, sample.latDeg * 100000.0, sample.heightM);
}

} // namespace

Aircraft3DLayer::Aircraft3DLayer()
    : transform_(new osg::MatrixTransform)
{
    osg::ref_ptr<osg::Geode> geode = new osg::Geode;
    osg::ref_ptr<osg::ShapeDrawable> body = new osg::ShapeDrawable(new osg::Sphere(osg::Vec3(), 8.0));
    body->setColor(osg::Vec4(1.0f, 0.35f, 0.12f, 1.0f));
    geode->addDrawable(body.get());
    transform_->addChild(geode.get());
}

void Aircraft3DLayer::clear()
{
    has_position_ = false;
    transform_->setMatrix(osg::Matrix::identity());
}

void Aircraft3DLayer::updateSample(const VaporView::Geo::NavSample& sample)
{
    if (!sample.hasLlh() && !sample.hasNed())
    {
        return;
    }

    osg::Matrix rotation = osg::Matrix::identity();
    if (std::isfinite(sample.yawDeg) || std::isfinite(sample.pitchDeg) || std::isfinite(sample.rollDeg))
    {
        const double yaw = std::isfinite(sample.yawDeg) ? osg::DegreesToRadians(sample.yawDeg) : 0.0;
        const double pitch = std::isfinite(sample.pitchDeg) ? osg::DegreesToRadians(sample.pitchDeg) : 0.0;
        const double roll = std::isfinite(sample.rollDeg) ? osg::DegreesToRadians(sample.rollDeg) : 0.0;
        rotation = osg::Matrix::rotate(roll, osg::Vec3d(1.0, 0.0, 0.0),
                                       pitch, osg::Vec3d(0.0, 1.0, 0.0),
                                       yaw, osg::Vec3d(0.0, 0.0, 1.0));
    }
    transform_->setMatrix(rotation * osg::Matrix::translate(samplePosition(sample)));
    has_position_ = true;
}

bool Aircraft3DLayer::hasPosition() const
{
    return has_position_;
}

osg::Node* Aircraft3DLayer::node() const
{
    return transform_.get();
}

} // namespace VaporView::Map3D
