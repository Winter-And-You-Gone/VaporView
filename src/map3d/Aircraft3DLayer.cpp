#include "map3d/Aircraft3DLayer.h"

#include <osg/Geode>
#include <osg/Geometry>
#include <osg/LineWidth>
#include <osg/Math>
#include <osg/MatrixTransform>

#include <algorithm>
#include <cmath>

namespace VaporView::Map3D {
namespace {

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

struct EulerAttitude {
    double rollRad = 0.0;
    double pitchRad = 0.0;
    double yawRad = 0.0;
};

EulerAttitude attitudeFromSample(const VaporView::Geo::NavSample& sample)
{
    if (sample.hasQuaternion())
    {
        const double norm = sample.quaternionNorm();
        const double w = sample.quatW / norm;
        const double x = sample.quatX / norm;
        const double y = sample.quatY / norm;
        const double z = sample.quatZ / norm;
        EulerAttitude attitude;
        attitude.rollRad = std::atan2(2.0 * (w * x + y * z),
                                      1.0 - 2.0 * (x * x + y * y));
        attitude.pitchRad = std::asin(std::clamp(2.0 * (w * y - z * x), -1.0, 1.0));
        attitude.yawRad = std::atan2(2.0 * (w * z + x * y),
                                     1.0 - 2.0 * (y * y + z * z));
        return attitude;
    }

    EulerAttitude attitude;
    attitude.rollRad = std::isfinite(sample.rollDeg) ? osg::DegreesToRadians(sample.rollDeg) : 0.0;
    attitude.pitchRad = std::isfinite(sample.pitchDeg) ? osg::DegreesToRadians(sample.pitchDeg) : 0.0;
    attitude.yawRad = std::isfinite(sample.yawDeg) ? osg::DegreesToRadians(sample.yawDeg) : 0.0;
    return attitude;
}

osg::Matrix basisMatrix(const osg::Vec3d& modelRight,
                        const osg::Vec3d& modelForward,
                        const osg::Vec3d& modelUp)
{
    return osg::Matrix(modelRight.x(), modelRight.y(), modelRight.z(), 0.0,
                       modelForward.x(), modelForward.y(), modelForward.z(), 0.0,
                       modelUp.x(), modelUp.y(), modelUp.z(), 0.0,
                       0.0, 0.0, 0.0, 1.0);
}

osg::Vec3d nedVectorToEnu(const osg::Vec3d& vector)
{
    return osg::Vec3d(vector.y(), vector.x(), -vector.z());
}

osg::Matrix modelToEnuRotation(const VaporView::Geo::NavSample& sample)
{
    const EulerAttitude attitude = attitudeFromSample(sample);
    const double cr = std::cos(attitude.rollRad);
    const double sr = std::sin(attitude.rollRad);
    const double cp = std::cos(attitude.pitchRad);
    const double sp = std::sin(attitude.pitchRad);
    const double cy = std::cos(attitude.yawRad);
    const double sy = std::sin(attitude.yawRad);

    const osg::Vec3d bodyForwardNed(cp * cy, cp * sy, -sp);
    const osg::Vec3d bodyRightNed(sr * sp * cy - cr * sy,
                                  sr * sp * sy + cr * cy,
                                  sr * cp);
    const osg::Vec3d bodyDownNed(cr * sp * cy + sr * sy,
                                 cr * sp * sy - sr * cy,
                                 cr * cp);
    return basisMatrix(nedVectorToEnu(bodyRightNed),
                       nedVectorToEnu(bodyForwardNed),
                       -nedVectorToEnu(bodyDownNed));
}

osg::Matrix enuToEcefRotation(double latDeg, double lonDeg)
{
    const double latRad = osg::DegreesToRadians(latDeg);
    const double lonRad = osg::DegreesToRadians(lonDeg);
    const osg::Vec3d east(-std::sin(lonRad), std::cos(lonRad), 0.0);
    const osg::Vec3d north(-std::sin(latRad) * std::cos(lonRad),
                           -std::sin(latRad) * std::sin(lonRad),
                           std::cos(latRad));
    const osg::Vec3d up(std::cos(latRad) * std::cos(lonRad),
                        std::cos(latRad) * std::sin(lonRad),
                        std::sin(latRad));
    return basisMatrix(east, north, up);
}

osg::Matrix rotationFromSample(const VaporView::Geo::NavSample& sample, bool useWorldCoordinates)
{
    const osg::Matrix modelToEnu = modelToEnuRotation(sample);
    return useWorldCoordinates && sample.hasLlh()
        ? modelToEnu * enuToEcefRotation(sample.latDeg, sample.lonDeg)
        : modelToEnu;
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
    if (!sample.hasLlh() && !sample.hasNed() && !hasWorldPosition(sample))
    {
        return;
    }

    const osg::Matrix rotation = rotationFromSample(sample, use_world_coordinates_);
    transform_->setMatrix(rotation * osg::Matrix::translate(
        samplePosition(sample, use_world_coordinates_, has_world_origin_, world_origin_)));
    has_position_ = true;
}

void Aircraft3DLayer::setUseWorldCoordinates(bool enabled)
{
    use_world_coordinates_ = enabled;
}

void Aircraft3DLayer::setWorldOrigin(const osg::Vec3d& origin)
{
    has_world_origin_ = true;
    world_origin_ = origin;
}

void Aircraft3DLayer::clearWorldOrigin()
{
    has_world_origin_ = false;
    world_origin_.set(0.0, 0.0, 0.0);
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

osg::Node* Aircraft3DLayer::node()
{
    return transform_.get();
}

const osg::Node* Aircraft3DLayer::node() const
{
    return transform_.get();
}

} // namespace VaporView::Map3D
