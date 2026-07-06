#include "map3d/Aircraft3DLayer.h"

#include <osg/Geode>
#include <osg/Geometry>
#include <osg/LineWidth>
#include <osg/Math>
#include <osg/MatrixTransform>

#include <cmath>

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

osg::ref_ptr<osg::Geometry> createAircraftBody()
{
    osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
    vertices->push_back(osg::Vec3(0.0f, 22.0f, 0.0f));    // nose, yaw 0 points north/local +Y
    vertices->push_back(osg::Vec3(-13.0f, -2.0f, 0.0f));  // left wing
    vertices->push_back(osg::Vec3(0.0f, -11.0f, 0.0f));   // tail
    vertices->push_back(osg::Vec3(13.0f, -2.0f, 0.0f));   // right wing
    vertices->push_back(osg::Vec3(0.0f, -7.0f, 7.0f));    // vertical fin

    osg::ref_ptr<osg::DrawElementsUInt> triangles =
        new osg::DrawElementsUInt(osg::PrimitiveSet::TRIANGLES);
    triangles->push_back(0);
    triangles->push_back(1);
    triangles->push_back(2);
    triangles->push_back(0);
    triangles->push_back(2);
    triangles->push_back(3);
    triangles->push_back(2);
    triangles->push_back(4);
    triangles->push_back(0);

    osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
    colors->push_back(osg::Vec4(1.0f, 0.36f, 0.12f, 1.0f));

    osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
    geometry->setVertexArray(vertices.get());
    geometry->setColorArray(colors.get(), osg::Array::BIND_OVERALL);
    geometry->addPrimitiveSet(triangles.get());
    return geometry;
}

osg::ref_ptr<osg::Geometry> createAircraftOutline()
{
    osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
    vertices->push_back(osg::Vec3(0.0f, 22.0f, 1.0f));
    vertices->push_back(osg::Vec3(0.0f, -11.0f, 1.0f));
    vertices->push_back(osg::Vec3(-13.0f, -2.0f, 1.0f));
    vertices->push_back(osg::Vec3(13.0f, -2.0f, 1.0f));
    vertices->push_back(osg::Vec3(0.0f, -7.0f, 1.0f));
    vertices->push_back(osg::Vec3(0.0f, -7.0f, 8.5f));

    osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
    colors->push_back(osg::Vec4(1.0f, 0.92f, 0.72f, 1.0f));

    osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
    geometry->setVertexArray(vertices.get());
    geometry->setColorArray(colors.get(), osg::Array::BIND_OVERALL);
    geometry->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINES, 0, static_cast<GLsizei>(vertices->size())));
    geometry->getOrCreateStateSet()->setAttributeAndModes(new osg::LineWidth(2.0f), osg::StateAttribute::ON);
    return geometry;
}

osg::Matrix rotationFromSample(const VaporView::Geo::NavSample& sample)
{
    if (sample.hasQuaternion())
    {
        const double norm = std::sqrt(sample.quatW * sample.quatW
                                      + sample.quatX * sample.quatX
                                      + sample.quatY * sample.quatY
                                      + sample.quatZ * sample.quatZ);
        const osg::Quat quat(sample.quatX / norm,
                             sample.quatY / norm,
                             sample.quatZ / norm,
                             sample.quatW / norm);
        return osg::Matrix::rotate(quat);
    }

    if (std::isfinite(sample.yawDeg) || std::isfinite(sample.pitchDeg) || std::isfinite(sample.rollDeg))
    {
        const double yaw = std::isfinite(sample.yawDeg) ? osg::DegreesToRadians(sample.yawDeg) : 0.0;
        const double pitch = std::isfinite(sample.pitchDeg) ? osg::DegreesToRadians(sample.pitchDeg) : 0.0;
        const double roll = std::isfinite(sample.rollDeg) ? osg::DegreesToRadians(sample.rollDeg) : 0.0;
        return osg::Matrix::rotate(roll, osg::Vec3d(1.0, 0.0, 0.0),
                                   pitch, osg::Vec3d(0.0, 1.0, 0.0),
                                   yaw, osg::Vec3d(0.0, 0.0, 1.0));
    }

    return osg::Matrix::identity();
}

} // namespace

Aircraft3DLayer::Aircraft3DLayer()
    : transform_(new osg::MatrixTransform)
{
    installBuiltInMarker();
}

void Aircraft3DLayer::installBuiltInMarker()
{
    osg::ref_ptr<osg::Geode> geode = new osg::Geode;
    geode->addDrawable(createAircraftBody().get());
    geode->addDrawable(createAircraftOutline().get());
    built_in_marker_ = geode;
    if (transform_->getNumChildren() == 0)
    {
        transform_->addChild(built_in_marker_.get());
    }
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

    const osg::Matrix rotation = rotationFromSample(sample);
    transform_->setMatrix(rotation * osg::Matrix::translate(samplePosition(sample, use_world_coordinates_)));
    has_position_ = true;
}

void Aircraft3DLayer::setUseWorldCoordinates(bool enabled)
{
    use_world_coordinates_ = enabled;
}

void Aircraft3DLayer::setCustomModel(osg::Node* modelNode)
{
    if (!modelNode)
    {
        clearCustomModel();
        return;
    }

    if (custom_model_ == modelNode && transform_->containsNode(custom_model_.get()))
    {
        return;
    }

    transform_->removeChildren(0, transform_->getNumChildren());
    custom_model_ = modelNode;
    transform_->addChild(custom_model_.get());
}

void Aircraft3DLayer::clearCustomModel()
{
    if (!custom_model_)
    {
        return;
    }

    transform_->removeChildren(0, transform_->getNumChildren());
    custom_model_ = nullptr;
    transform_->addChild(built_in_marker_.get());
}

bool Aircraft3DLayer::hasPosition() const
{
    return has_position_;
}

bool Aircraft3DLayer::hasCustomModel() const
{
    return custom_model_.valid();
}

osg::Node* Aircraft3DLayer::node() const
{
    return transform_.get();
}

} // namespace VaporView::Map3D
