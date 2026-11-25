#pragma once

#include "Map3DRuntime.h"
#include "OnlineTerrainLayer.h"
#include <osgEarth/Cache>
#include <osgEarth/ElevationLayer>
#include <osgEarth/Progress>
#include <osgEarth/Registry>
#include <osgEarth/Threading>

#include <cpl_string.h>
#include <gdal_priv.h>
#include <gdal_utils.h>
#include <QCache>
#include <QDateTime>
#include <QMutex>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace VaporView::Map3D::Detail {

class OnlineCopernicusLayer final : public OnlineTerrainLayer
{
public:
    static constexpr auto name = "Online Copernicus GLO-30";

    OnlineCopernicusLayer()
        : OnlineTerrainLayer(&options_, &originalOptions_)
    {
        ElevationLayer::init();
        setName(name);
        setAttribution("Terrain adapted from the Copernicus DEM. © DLR e.V. 2010–2014 and © Airbus Defence and Space GmbH 2014–2018, provided under COPERNICUS by the European Union and ESA");
        setProfile(osgEarth::Profile::create(osgEarth::Profile::GLOBAL_GEODETIC));
        setTileSize(65);
        setMinLevel(kTerrainMinLevel);
        setMaxDataLevel(14);
        setCachePolicy(osgEarth::CachePolicy::NO_CACHE);
        if (auto* cache = osgEarth::Registry::instance()->getDefaultCache())
            cache_ = cache->addBin("vaporview-copernicus-glo30-v1");
    }

private:
    osgEarth::Status openImplementation() override
    {
        auto status = ElevationLayer::openImplementation();
        if (status.isOK())
            addDataExtent(osgEarth::DataExtent(getProfile()->getExtent(), getMinLevel(), getMaxDataLevel()));
        return status;
    }

    GDALDatasetUniquePtr openCell(int lat, int lon) const
    {
        const QString cell = QStringLiteral("Copernicus_DSM_COG_10_%1%2_00_%3%4_00_DEM")
            .arg(lat < 0 ? 'S' : 'N').arg(std::abs(lat), 2, 10, QLatin1Char('0'))
            .arg(lon < 0 ? 'W' : 'E').arg(std::abs(lon), 3, 10, QLatin1Char('0'));
        {
            QMutexLocker lock(&retryMutex_);
            const auto* retry = retryAfter_.object(cell);
            if (retry && *retry > QDateTime::currentSecsSinceEpoch()) return {};
        }
        const QString url = QStringLiteral("https://copernicus-dem-30m.s3.eu-central-1.amazonaws.com/%1/%1.tif").arg(cell);
        const QByteArray path = "/vsicurl?empty_dir=yes&connecttimeout=4&timeout=10&url=" + QUrl::toPercentEncoding(url);
        GDALDatasetUniquePtr dataset(GDALDataset::Open(path.constData(), GDAL_OF_RASTER | GDAL_OF_READONLY));
        if (!dataset)
        {
            QMutexLocker lock(&retryMutex_);
            retryAfter_.insert(cell, new qint64(QDateTime::currentSecsSinceEpoch() + 30));
        }
        return dataset;
    }

    osgEarth::GeoHeightField createHeightFieldImplementation(const osgEarth::TileKey& key,
        osgEarth::ProgressCallback* progress) const override
    {
        using namespace osgEarth;
        const auto& extent = key.getExtent();
        const auto cacheKey = Cache::makeCacheKey(key.str());
        static Threading::Gate<std::string> tileRequests;
        Threading::ScopedGate<std::string> lock(tileRequests, cacheKey);
        if (progress && progress->isCanceled()) return {};
        if (cache_)
        {
            const auto cached = cache_->readObject(cacheKey, getReadOptions());
            if (auto* field = cached.get<osg::HeightField>())
                return GeoHeightField(field, extent);
        }

        if (shouldDeferDownload()) return {};

        // Copernicus registers heights at pixel centres. The first sample of
        // each COG is on its north-west corner. Include the half-pixel overlap.
        constexpr double latMargin = 0.5 / 3600.0;
        constexpr double lonMargin = 5.0 / 3600.0;
        const int south = std::max(-90, int(std::floor(extent.yMin() - latMargin)));
        const int east = std::min(179, int(std::floor(extent.xMax() + lonMargin)));
        std::vector<GDALDatasetUniquePtr> datasets;
        std::vector<GDALDatasetH> handles;
        bool complete = true;
        for (int lat = south; lat <= std::min(89, int(std::floor(extent.yMax()))); ++lat)
        {
            for (int lon = int(std::floor(extent.xMin())); lon <= east; ++lon)
            {
                if (progress && progress->isCanceled()) return {};
                auto dataset = openCell(lat, lon);
                if (dataset)
                {
                    handles.push_back(GDALDataset::ToHandle(dataset.get()));
                    datasets.push_back(std::move(dataset));
                }
                else complete = false;
            }
        }
        if (handles.empty()) return {};

        // The outer height samples sit on the tile edges; GDAL takes pixel-edge bounds.
        const int size = int(getTileSize());
        const double halfPixel = extent.width() / (2.0 * (size - 1));
        CPLStringList args;
        for (const char* arg : {"-of", "MEM", "-ot", "Float32", "-r", "bilinear", "-te"}) args.AddString(arg);
        for (double bound : {extent.xMin() - halfPixel, extent.yMin() - halfPixel,
                             extent.xMax() + halfPixel, extent.yMax() + halfPixel})
            args.AddString(CPLSPrintf("%.15g", bound));
        args.AddString("-ts");
        args.AddString(CPLSPrintf("%d", size));
        args.AddString(CPLSPrintf("%d", size));
        args.AddString("-dstnodata");
        args.AddString(CPLSPrintf("%.9g", double(NO_DATA_VALUE)));
        std::unique_ptr<GDALWarpAppOptions, decltype(&GDALWarpAppOptionsFree)> warp(
            GDALWarpAppOptionsNew(args.List(), nullptr), GDALWarpAppOptionsFree);
        if (!warp) return {};
        GDALWarpAppOptionsSetProgress(warp.get(), [](double, const char*, void* data) {
            auto* callback = static_cast<ProgressCallback*>(data);
            return callback && callback->isCanceled() ? FALSE : TRUE;
        }, progress);
        GDALDatasetUniquePtr tile(GDALDataset::FromHandle(
            GDALWarp("", nullptr, int(handles.size()), handles.data(), warp.get(), nullptr)));
        if (!tile || (progress && progress->isCanceled())) return {};

        osg::ref_ptr<osg::HeightField> field = new osg::HeightField;
        field->allocate(size, size);
        auto& heights = field->getHeightList();
        // GDAL rows start in the north; osgEarth height fields start in the south.
        if (tile->GetRasterBand(1)->RasterIO(GF_Read, 0, 0, size, size,
                heights.data() + size * (size - 1), size, size, GDT_Float32,
                sizeof(float), -static_cast<GSpacing>(size * sizeof(float)), nullptr) != CE_None) return {};
        if (std::all_of(heights.begin(), heights.end(), [](float h) { return h == NO_DATA_VALUE; })) return {};
        if (complete && cache_) cache_->write(cacheKey, field.get(), getReadOptions());
        return GeoHeightField(field.get(), extent);
    }

    Options options_;
    const Options originalOptions_;
    osg::ref_ptr<osgEarth::CacheBin> cache_;
    mutable QMutex retryMutex_;
    mutable QCache<QString, qint64> retryAfter_{256};
};

} // namespace VaporView::Map3D::Detail
