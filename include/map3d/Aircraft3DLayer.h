#pragma once

#include "geo/GeoTypes.h"

#include <osg/MatrixTransform>
#include <osg/Node>
#include <osg/ref_ptr>

namespace VaporView::Map3D {

class Aircraft3DLayer {
public:
    Aircraft3DLayer();

    void clear();
    void updateSample(const VaporView::Geo::NavSample& sample);
    void setUseWorldCoordinates(bool enabled);
    void setCustomModel(osg::Node* modelNode);
    void clearCustomModel();

    bool hasPosition() const;
    bool hasCustomModel() const;
    osg::Node* node() const;

private:
    void installBuiltInMarker();

    osg::ref_ptr<osg::MatrixTransform> transform_;
    osg::ref_ptr<osg::Node> built_in_marker_;
    osg::ref_ptr<osg::Node> custom_model_;
    bool has_position_ = false;
    bool use_world_coordinates_ = false;
};

} // namespace VaporView::Map3D
