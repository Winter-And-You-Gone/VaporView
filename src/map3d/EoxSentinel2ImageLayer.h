#pragma once

#include "map3d/Map3DLoadTracker.h"
#include "MapHttp.h"
#include "OnlineMapResource.h"

#include <osgEarth/Profile>
#include <osgEarth/Progress>
#include <osgEarth/TileKey>
#include <osgEarth/XYZ>

namespace VaporView::Map3D::Detail {

inline constexpr auto kEoxSentinel2LayerName = "EOX Sentinel-2 cloudless 2024 imagery";
inline const auto kEoxSentinel2Source = QStringLiteral("online:eox_sentinel2_2024");
inline const auto kEoxSentinel2Template = QStringLiteral(
    "https://tiles.maps.eox.at/wmts/1.0.0/s2cloudless-2024_3857/default/g/{z}/{y}/{x}.jpg");
inline constexpr auto kEoxSentinel2Attribution =
    "EOxCloudless https://cloudless.eox.at by EOX IT Services GmbH (Contains modified Copernicus Sentinel data 2024)";
inline constexpr auto kEoxSentinel2UsageUrl = "https://cloudless.eox.at/license-non-commercial";
// The shared "g" matrix advertises levels through 21, but the 2024 layer serves 0–18.
inline constexpr unsigned kEoxSentinel2MaxLevel = 18u;

inline QNetworkRequest eoxSentinel2TileRequest(const osgEarth::TileKey& key)
{
    if (!key.valid() || key.getLOD() > kEoxSentinel2MaxLevel) return {};
    QNetworkRequest request{QUrl(expandTileUrl(
        kEoxSentinel2Template, key.getLOD(), key.getTileX(), key.getTileY()))};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    return request;
}

class EoxSentinel2ImageLayer final : public osgEarth::XYZImageLayer
{
public:
    EoxSentinel2ImageLayer()
    {
        setName(kEoxSentinel2LayerName);
        setURL(osgEarth::URI(kEoxSentinel2Template.toStdString()));
        setProfile(osgEarth::Profile::create(osgEarth::Profile::SPHERICAL_MERCATOR));
        setFormat("jpg");
        setAttribution(kEoxSentinel2Attribution);
        options().minLevel() = 0u;
        options().maxDataLevel() = kEoxSentinel2MaxLevel;
        // MapHttp already retains cacheable responses in the shared bounded cache.
        setCachePolicy(osgEarth::CachePolicy::NO_CACHE);
    }

    osgEarth::GeoImage createImageImplementation(const osgEarth::TileKey& key,
                                                 osgEarth::ProgressCallback* progress) const override
    {
        if (progress && progress->isCanceled()) return osgEarth::GeoImage::INVALID;
        const auto request = eoxSentinel2TileRequest(key);
        if (request.url().isEmpty()) return osgEarth::GeoImage::INVALID;
        ScopedMapLoadTask loadTask(MapLoadCategory::Imagery);
        auto image = readMapImageryTile(request, key, progress);
        if (progress && progress->isCanceled())
        {
            loadTask.cancel();
            return osgEarth::GeoImage::INVALID;
        }
        loadTask.finish();
        return image;
    }
};

} // namespace VaporView::Map3D::Detail
