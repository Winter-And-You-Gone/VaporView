#pragma once

#include "EarthProjectionProfile.h"
#include <osg/NodeCallback>
#include <osgEarth/Ellipsoid>
#include <memory>

namespace VaporView::Map3D::Detail {

// Updated and queried on the single-threaded viewer's render thread.
struct GroundDetailEllipse
{
    static constexpr double halfWidthM = 30000.0;
    osg::Vec3d eyeGround, targetGround, up;
    double majorAxisM = 0.0;
    double requiredFarPlaneM = 0.0;

    void update(const osgEarth::Ellipsoid& earth, const osg::Matrixd& cameraToWorld,
                double rangeM, double pitchDeg)
    {
        *this = {};
        if (!(pitchDeg > kEarthLowAngleStartPitchDeg && rangeM > 0.0 &&
              rangeM <= kEarthProjectionLocalRangeLimitM)) return;
        const osg::Vec3d eye = cameraToWorld.getTrans();
        osg::Vec3d eyeLLA = earth.geocentricToGeodetic(eye);
        osg::Vec3d targetLLA = earth.geocentricToGeodetic(osg::Vec3d(0, 0, -rangeM) * cameraToWorld);
        if (!eyeLLA.valid() || !targetLLA.valid() || eyeLLA.z() - targetLLA.z() > halfWidthM) return;
        eyeLLA.z() = targetLLA.z() = 0.0;
        eyeGround = earth.geodeticToGeocentric(eyeLLA);
        targetGround = earth.geodeticToGeocentric(targetLLA);
        up = earth.geocentricToUpVector((eyeGround + targetGround) * 0.5);
        const double separation = horizontalDistance(eyeGround - targetGround);
        majorAxisM = std::hypot(separation, 2.0 * halfWidthM);
        requiredFarPlaneM = (eye - eyeGround).length() + 0.5 * (majorAxisM + separation);
    }

    bool intersects(const osg::BoundingSphere& tile) const
    {
        if (majorAxisM == 0.0 || !tile.valid()) return true;
        const osg::Vec3d center = tile.center();
        // Each focus distance can shrink by up to one radius anywhere inside the tile.
        return horizontalDistance(center - eyeGround) + horizontalDistance(center - targetGround)
            <= majorAxisM + 2.0 * tile.radius();
    }

private:
    double horizontalDistance(const osg::Vec3d& delta) const
    {
        return (delta - up * (delta * up)).length();
    }
};

class BuildingEllipseCull final : public osg::NodeCallback
{
public:
    explicit BuildingEllipseCull(std::shared_ptr<const GroundDetailEllipse> ellipse)
        : ellipse_(std::move(ellipse)) {}

    void operator()(osg::Node* node, osg::NodeVisitor* visitor) override
    {
        if (ellipse_->intersects(node->getBound())) traverse(node, visitor);
    }

private:
    std::shared_ptr<const GroundDetailEllipse> ellipse_;
};

} // namespace VaporView::Map3D::Detail
