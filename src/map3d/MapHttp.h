#pragma once

#include <QNetworkRequest>
#include <osgEarth/GeoData>

namespace osgEarth { class ProgressCallback; class TileKey; }

namespace VaporView::Map3D::Detail {

enum class OnlineMapProvider;
struct OnlineMapResource;

struct MapHttpResult {
    QByteArray body;
    int status = 0;
    bool fromCache = false;
};

MapHttpResult mapHttpGet(QNetworkRequest request, osgEarth::ProgressCallback* progress);
osgEarth::GeoImage readMapImageryTile(const QNetworkRequest& request, const osgEarth::TileKey& key,
                                     osgEarth::ProgressCallback* progress);
QNetworkRequest credentialedImageryRequest(OnlineMapProvider provider, const OnlineMapResource& resource,
                                          unsigned z, unsigned x, unsigned y);
osgEarth::GeoImage readCredentialedImageryTile(OnlineMapProvider provider, const OnlineMapResource& resource,
                                              const osgEarth::TileKey& key, osgEarth::ProgressCallback* progress);

} // namespace VaporView::Map3D::Detail
