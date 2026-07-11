#include "map3d/Aircraft3DLayer.h"

#include <osg/Geode>
#include <osg/Group>
#include <osg/MatrixTransform>
#include <osg/Node>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool nearlyEqual(double lhs, double rhs, double tolerance = 1e-6)
{
    return std::abs(lhs - rhs) <= tolerance;
}

VaporView::Geo::NavSample localSample(double yawDeg)
{
    VaporView::Geo::NavSample sample;
    sample.latDeg = 39.9;
    sample.lonDeg = 116.3;
    sample.heightM = 45.0;
    sample.nedNM = 20.0;
    sample.nedEM = 10.0;
    sample.nedDM = -5.0;
    sample.yawDeg = yawDeg;
    sample.pitchDeg = 0.0;
    sample.rollDeg = 0.0;
    sample.fixQuality = VaporView::Geo::FixQuality::Fixed;
    return sample;
}

osg::Vec3d transformedOrigin(const osg::Matrix& matrix)
{
    return osg::Vec3d(0.0, 0.0, 0.0) * matrix;
}

osg::Vec3d transformedForward(const osg::Matrix& matrix)
{
    return osg::Vec3d(0.0, 1.0, 0.0) * matrix - transformedOrigin(matrix);
}

osg::Vec3d transformedRight(const osg::Matrix& matrix)
{
    return osg::Vec3d(1.0, 0.0, 0.0) * matrix - transformedOrigin(matrix);
}

osg::Vec3d transformedUp(const osg::Matrix& matrix)
{
    return osg::Vec3d(0.0, 0.0, 1.0) * matrix - transformedOrigin(matrix);
}

osg::Vec3d localEast(double lonDeg)
{
    const double lonRad = osg::DegreesToRadians(lonDeg);
    return osg::Vec3d(-std::sin(lonRad), std::cos(lonRad), 0.0);
}

osg::Vec3d localNorth(double latDeg, double lonDeg)
{
    const double latRad = osg::DegreesToRadians(latDeg);
    const double lonRad = osg::DegreesToRadians(lonDeg);
    return osg::Vec3d(-std::sin(latRad) * std::cos(lonRad),
                      -std::sin(latRad) * std::sin(lonRad),
                      std::cos(latRad));
}

osg::Vec3d localUp(double latDeg, double lonDeg)
{
    const double latRad = osg::DegreesToRadians(latDeg);
    const double lonRad = osg::DegreesToRadians(lonDeg);
    return osg::Vec3d(std::cos(latRad) * std::cos(lonRad),
                      std::cos(latRad) * std::sin(lonRad),
                      std::sin(latRad));
}

} // namespace

int main()
{
    VaporView::Map3D::Aircraft3DLayer layer;
    require(!layer.hasPosition(), "new aircraft layer has no position");

    auto* transform = dynamic_cast<osg::MatrixTransform*>(layer.node());
    require(transform != nullptr, "aircraft node is a matrix transform");
    require(transform->getNumChildren() == 1, "aircraft transform has one geode child");

    auto* geode = dynamic_cast<osg::Geode*>(transform->getChild(0));
    require(geode != nullptr, "aircraft child is a geode");
    require(geode->getNumDrawables() >= 2, "aircraft marker has body and outline drawables");
    require(!layer.hasCustomModel(), "aircraft starts with built-in marker");

    osg::ref_ptr<osg::Group> customModel = new osg::Group;
    customModel->setName("test-aircraft-model");
    layer.setCustomModel(customModel.get());
    require(layer.hasCustomModel(), "custom aircraft model can be installed");
    require(transform->getNumChildren() == 1, "custom aircraft model replaces built-in marker child");
    require(transform->getChild(0) == customModel.get(), "custom aircraft model is attached to transform");

    layer.clearCustomModel();
    require(!layer.hasCustomModel(), "custom aircraft model can be cleared");
    require(transform->getNumChildren() == 1, "built-in marker is restored after clearing custom model");
    geode = dynamic_cast<osg::Geode*>(transform->getChild(0));
    require(geode != nullptr, "built-in aircraft child is restored as a geode");

    layer.updateSample(localSample(0.0));
    require(layer.hasPosition(), "valid local sample sets aircraft position");
    const osg::Matrix yaw0 = transform->getMatrix();
    const osg::Vec3d position0 = transformedOrigin(yaw0);
    require(nearlyEqual(position0.x(), 10.0), "local sample uses NED east as X");
    require(nearlyEqual(position0.y(), 20.0), "local sample uses NED north as Y");
    require(nearlyEqual(position0.z(), 5.0), "local sample uses negative NED down as Z");
    require(transformedForward(yaw0).y() > 0.9, "yaw 0 marker points along local north");

    layer.updateSample(localSample(90.0));
    const osg::Vec3d yaw90Forward = transformedForward(transform->getMatrix());
    require(std::abs(yaw90Forward.x()) > 0.9, "yaw 90 rotates visible aircraft nose toward east/west axis");
    require(std::abs(yaw90Forward.y()) < 0.1, "yaw 90 no longer points north");

    VaporView::Geo::NavSample pitchSample = localSample(0.0);
    pitchSample.pitchDeg = 30.0;
    layer.updateSample(pitchSample);
    require(transformedForward(transform->getMatrix()).z() > 0.49,
            "positive EPSILON pitch raises the model nose");

    VaporView::Geo::NavSample rollSample = localSample(0.0);
    rollSample.rollDeg = 30.0;
    layer.updateSample(rollSample);
    require(transformedRight(transform->getMatrix()).z() < -0.49,
            "positive EPSILON roll lowers the model right wing around its forward axis");
    require(transformedForward(transform->getMatrix()).y() > 0.99,
            "roll rotates around the model forward axis without changing heading");

    VaporView::Geo::NavSample quaternionSample = localSample(0.0);
    quaternionSample.quatW = std::cos(osg::DegreesToRadians(45.0));
    quaternionSample.quatX = 0.0;
    quaternionSample.quatY = 0.0;
    quaternionSample.quatZ = std::sin(osg::DegreesToRadians(45.0));
    layer.updateSample(quaternionSample);
    const osg::Vec3d quaternionForward = transformedForward(transform->getMatrix());
    require(std::abs(quaternionForward.x()) > 0.9,
            "quaternion orientation takes precedence over yaw fallback");
    require(std::abs(quaternionForward.y()) < 0.1,
            "quaternion yaw rotates aircraft nose away from north");

    VaporView::Geo::NavSample worldSample = localSample(0.0);
    worldSample.ecefXM = 1.0;
    worldSample.ecefYM = 2.0;
    worldSample.ecefZM = 3.0;
    layer.setUseWorldCoordinates(true);
    layer.setWorldOrigin(osg::Vec3d(1.0, 2.0, 3.0));
    layer.updateSample(worldSample);
    const osg::Vec3d worldPosition = transformedOrigin(transform->getMatrix());
    require(nearlyEqual(worldPosition.x(), 0.0), "world mode uses local offset X");
    require(nearlyEqual(worldPosition.y(), 0.0), "world mode uses local offset Y");
    require(nearlyEqual(worldPosition.z(), 0.0), "world mode uses local offset Z");
    const osg::Vec3d expectedNorth = localNorth(worldSample.latDeg, worldSample.lonDeg);
    const osg::Vec3d expectedEast = localEast(worldSample.lonDeg);
    const osg::Vec3d expectedUp = localUp(worldSample.latDeg, worldSample.lonDeg);
    require(transformedForward(transform->getMatrix()) * expectedNorth > 0.999,
            "world yaw zero follows the local ECEF north tangent");
    require(transformedRight(transform->getMatrix()) * expectedEast > 0.999,
            "world model right axis follows the local ECEF east tangent");
    require(transformedUp(transform->getMatrix()) * expectedUp > 0.999,
            "world model up axis follows the local ECEF surface normal");

    worldSample.yawDeg = 90.0;
    layer.updateSample(worldSample);
    require(transformedForward(transform->getMatrix()) * expectedEast > 0.999,
            "world yaw 90 follows the local ECEF east tangent");

    worldSample.ecefXM = 4.0;
    worldSample.ecefYM = 6.0;
    worldSample.ecefZM = 8.0;
    layer.updateSample(worldSample);
    const osg::Vec3d offsetWorldPosition = transformedOrigin(transform->getMatrix());
    require(nearlyEqual(offsetWorldPosition.x(), 3.0), "world mode preserves local ECEF X offset");
    require(nearlyEqual(offsetWorldPosition.y(), 4.0), "world mode preserves local ECEF Y offset");
    require(nearlyEqual(offsetWorldPosition.z(), 5.0), "world mode preserves local ECEF Z offset");

    layer.clear();
    require(!layer.hasPosition(), "clear removes aircraft position");
    require(transform->getMatrix() == osg::Matrix::identity(), "clear resets transform");

    return 0;
}
