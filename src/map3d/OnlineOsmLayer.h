#pragma once

#include "MapTileRetention.h"

#include <osgEarth/Cache>
#include <osgEarth/FeatureRasterizer>
#include <osgEarth/ImageLayer>
#include <osgEarth/LineSymbol>
#include <osgEarth/MVT>
#include <osgEarth/PolygonSymbol>
#include <osgEarth/Progress>
#include <osgEarth/Registry>
#include <osgEarth/Threading>

#include <QDateTime>
#include "MapHttp.h"
#include "map3d/Map3DLoadTracker.h"
#include <QString>
#include <memory>

namespace VaporView::Map3D::Detail {

class OnlineOsmLayer final : public osgEarth::ImageLayer
{
public:
    static constexpr auto roadsName = "Online OSM roads";
    static constexpr auto waterName = "Online OSM water";

    OnlineOsmLayer(bool roads, osgEarth::ImageLayer* offline, bool allowOfflineFallback = true)
        : ImageLayer(&options_, &originalOptions_), roads_(roads),
          offline_(allowOfflineFallback ? offline : nullptr),
          allowOfflineFallback_(allowOfflineFallback),
          base_(qEnvironmentVariable("VAPORVIEW_OSM_VECTOR_BASE_URL",
                QStringLiteral("https://vector.openstreetmap.org/shortbread_v1")).toStdString())
    {
        using namespace osgEarth;
        ImageLayer::init();
        setName(roads ? roadsName : waterName);
        setProfile(Profile::create(Profile::SPHERICAL_MERCATOR));
        setAttribution("© OpenStreetMap contributors");
        setAsyncLoading(true);
        options().minLevel() = roads ? 10u : 6u;
        options().maxDataLevel() = 19u;
        // Only cache source vectors. A missing tile may temporarily use local data.
        setCachePolicy(CachePolicy::NO_CACHE);
        if (auto* cache = Registry::instance()->getDefaultCache())
            tiles_ = cache->addBin(osmCacheBin);
        auto* line = lineStyle_.getOrCreate<LineSymbol>();
        line->stroke()->color() = Color(roads ? "#f6df8f" : "#76b9df");
        line->stroke()->width() = Distance(1.5, Units::PIXELS);
        fillStyle_.getOrCreate<PolygonSymbol>()->fill()->color() = Color("#3f86b980");
    }

    bool allowsOfflineFallback() const { return allowOfflineFallback_; }

    osgEarth::GeoImage createImageImplementation(const osgEarth::TileKey& key,
                                                  osgEarth::ProgressCallback* progress) const override
    {
        using namespace osgEarth;
        if (progress && progress->isCanceled()) return GeoImage::INVALID;
        ScopedMapLoadTask loadTask(MapLoadCategory::Osm);
        const TileKey source = key.getLOD() > 14 ? key.createAncestorKey(14) : key;
        const std::string url = base_ + "/" + std::to_string(source.getLOD()) + "/"
            + std::to_string(source.getTileX()) + "/" + std::to_string(source.getTileY()) + ".mvt";
        FeatureList features;
        const bool loaded = readFeatures(url, source, progress, features);
        if (progress && progress->isCanceled())
        {
            loadTask.cancel();
            return GeoImage::INVALID;
        }
        if (!loaded)
        {
            auto fallback = offline_ && offline_->isOpen() ? offline_->createImage(key, progress) : GeoImage::INVALID;
            if (progress && progress->isCanceled())
            {
                loadTask.cancel();
                return GeoImage::INVALID;
            }
            loadTask.finish();
            return fallback;
        }

        FeatureList polygons, lines;
        for (const auto& feature : features)
        {
            if (!feature->getGeometry()->getBounds().intersects(key.getExtent().bounds())) continue;
            if (feature->getString("mvt_layer") == "water_polygons")
                polygons.push_back(feature);
            else
                lines.push_back(feature);
        }
        // Re-rasterize at the requested zoom so streets stay sharp beyond source zoom 14.
        Util::FeatureRasterizer raster(getTileSize(), getTileSize(), key.getExtent());
        FilterContext context;
        raster.render(polygons, fillStyle_, context);
        raster.render(lines, lineStyle_, context);
        if (progress && progress->isCanceled())
        {
            loadTask.cancel();
            return GeoImage::INVALID;
        }
        loadTask.finish();
        return raster.finalize();
    }

private:
    static osgEarth::ReadResult fetchTile(const std::string& url, const osgDB::Options* options,
                                          osgEarth::ProgressCallback* progress)
    {
        const osgEarth::URI uri(url);
        if (!uri.isRemote()) return uri.readString(options, progress);

        const auto result = mapHttpGet(QNetworkRequest(QUrl(QString::fromStdString(url))), progress);
        if (result.body.isEmpty()) return {};
        return osgEarth::ReadResult(new osgEarth::StringObject(result.body.toStdString()));
    }

    bool readFeatures(const std::string& url, const osgEarth::TileKey& source,
                      osgEarth::ProgressCallback* progress, osgEarth::FeatureList& features) const
    {
        using namespace osgEarth;
        static Threading::Gate<std::string> requests;
        Threading::ScopedGate<std::string> lock(requests, url);
        if (progress && progress->isCanceled()) return false;
        const auto key = Cache::makeCacheKey(url);
        const auto* options = getReadOptions();
        const auto cached = tiles_ ? tiles_->readString(key, options) : ReadResult();
        const auto decode = [&](const ReadResult& result) {
            static const std::vector<std::string> roads{"streets"};
            static const std::vector<std::string> water{"water_lines", "water_polygons"};
            return result.succeeded() && !result.getString().empty()
                && MVT::readTile(result.getString(), source, features, roads_ ? roads : water);
        };
        const auto useCached = [&] {
            if (!decode(cached)) return false;
            recordTileVisit(osmCacheBin, key);
            return true;
        };
        constexpr qint64 week = 7 * 24 * 60 * 60;
        if (cached.lastModifiedTime() >= QDateTime::currentSecsSinceEpoch() - week && useCached())
            return true;
        const auto result = fetchTile(url, options, progress);
        if (progress && progress->isCanceled()) return false;
        if (decode(result))
        {
            if (tiles_ && tiles_->write(key, result.getObject(), options)) recordTileVisit(osmCacheBin, key);
            return true;
        }
        return useCached();
    }

    Options options_;
    const Options originalOptions_;
    bool roads_;
    osg::ref_ptr<osgEarth::ImageLayer> offline_;
    bool allowOfflineFallback_ = true;
    std::string base_;
    osg::ref_ptr<osgEarth::CacheBin> tiles_;
    osgEarth::Style lineStyle_, fillStyle_;
};

} // namespace VaporView::Map3D::Detail
