#pragma once

#include "geo/GeoTypes.h"

#include <osg/MatrixTransform>
#include <osg/ref_ptr>

namespace osg {
class Node;
}

namespace VaporView::Map3D {

class Aircraft3DLayer {
public:
    Aircraft3DLayer();

    void clear();
    void updateSample(const VaporView::Geo::NavSample& sample);

    bool hasPosition() const;
    osg::Node* node() const;

private:
    osg::ref_ptr<osg::MatrixTransform> transform_;
    bool has_position_ = false;
};

} // namespace VaporView::Map3D
