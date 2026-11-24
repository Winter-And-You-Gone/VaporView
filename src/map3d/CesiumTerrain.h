#pragma once

#include <QString>

namespace osgEarth { class Map; }
namespace VaporView::Map3D::Detail {

QByteArray configuredCesiumToken();
bool addCesiumTerrainFallback(osgEarth::Map* map);
QString cesiumTerrainCredit(const osgEarth::Map* map, bool collapsible);

} // namespace VaporView::Map3D::Detail
