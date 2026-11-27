#pragma once

#include <QString>

class QNetworkRequest;

namespace osgEarth { class Map; }
namespace VaporView::Map3D::Detail {

struct OnlineMapResource;
QNetworkRequest cesiumEndpointRequest(const OnlineMapResource& resource);
bool addCesiumTerrainFallback(osgEarth::Map* map);
QString cesiumTerrainCredit(const osgEarth::Map* map, bool collapsible);

} // namespace VaporView::Map3D::Detail
