#pragma once

#include "map3d/OsgEarthViewWidget.h"

#include <osg/Group>
#include <osg/Node>
#include <osg/ref_ptr>

namespace osgEarth {
class MapNode;
}

namespace VaporView::Map3D::Detail {

struct EarthAssetLoadResult {
    EarthLoadDiagnostics diagnostics;
    osg::ref_ptr<osg::Node> node;
    osgEarth::MapNode* mapNode = nullptr;
    bool useXihuInitialView = false;
};

struct Local3DTilesAssetLoadResult {
    Local3DTilesLoadDiagnostics diagnostics;
    osg::ref_ptr<osg::Group> node;
};

struct AircraftAssetLoadResult {
    AircraftModelDiagnostics diagnostics;
    osg::ref_ptr<osg::Node> node;
};

EarthAssetLoadResult loadEarthAsset(const QString& earthPath);
Local3DTilesAssetLoadResult loadLocal3DTilesAsset(const QString& tilesetPath);
AircraftAssetLoadResult loadAircraftAsset(const QString& modelPath,
                                          const QString& reasonPrefix);

} // namespace VaporView::Map3D::Detail
