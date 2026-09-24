#pragma once

#include <osg/Image>
#include <osgEarth/XYZ>
#include <osgEarth/Progress>
#include <osgEarth/Cache>
#include <osgEarth/ImageUtils>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace VaporView::Map3D::Detail {

// Tianditu returns this beige, central warning-text tile with HTTP 200.
// Match its background, empty margins AND central ink, not just low variance:
// uniform sea, cloud, snow and desert tiles must remain valid imagery.
inline bool isTiandituNoImageryTile(const osg::Image* image)
{
    if (!image || !image->data() || image->s() != 256 || image->t() != 256
        || image->getDataType() != GL_UNSIGNED_BYTE || image->isCompressed())
        return false;

    int background = 0;
    int centralInk = 0;
    for (int y = 0; y < 256; y += 4)
    {
        for (int x = 0; x < 256; x += 4)
        {
            const auto color = image->getColor(x, y);
            const float r = color.r() * 255.0f, g = color.g() * 255.0f, b = color.b() * 255.0f;
            const bool matchesBackground = std::abs(r - 228.0f) <= 4.0f
                && std::abs(g - 227.0f) <= 4.0f && std::abs(b - 223.0f) <= 4.0f
                && color.a() > 0.95f;
            const bool margin = y < 64 || y >= 192 || x < 24 || x >= 232;
            if (margin && !matchesBackground) return false;
            background += matchesBackground;
            if (!margin && (std::max)({r, g, b}) < 215.0f
                && (std::max)({r, g, b}) - (std::min)({r, g, b}) <= 10.0f)
                ++centralInk;
        }
    }
    return background >= 3686 && centralInk >= 20 && centralInk <= 400;
}

// Cache only original native tiles; never cache region-dependent fallback mosaics.
class TiandituSourceLayer final : public osgEarth::XYZImageLayer
{
public:
    explicit TiandituSourceLayer(const Options& options) : XYZImageLayer(options) {}
    mutable std::atomic_uint requests{0};
    mutable std::atomic_uint failures{0};

    osgEarth::GeoImage createImageImplementation(const osgEarth::TileKey& key,
                                                osgEarth::ProgressCallback* progress) const override
    {
        ++requests;
        auto image = osgEarth::XYZImageLayer::createImageImplementation(key, progress);
        if (progress && progress->isCanceled()) return osgEarth::GeoImage::INVALID;
        if (!image.valid() || isTiandituNoImageryTile(image.getImage()))
        {
            ++failures;
            return osgEarth::GeoImage::INVALID;
        }
        return image;
    }

    void postCreateImageImplementation(osgEarth::GeoImage& image,
                                      const osgEarth::TileKey&,
                                      osgEarth::ProgressCallback*) const override
    {
        // Also reject warning tiles already written by earlier application versions.
        if (image.valid() && isTiandituNoImageryTile(image.getImage()))
        {
            image = osgEarth::GeoImage::INVALID;
        }
    }

protected:
    osgEarth::Status openImplementation() override
    {
        auto status = XYZImageLayer::openImplementation();
        setUpL2Cache(128u);
        return status;
    }
};

class TiandituImageLayer final : public osgEarth::XYZImageLayer
{
public:
    mutable std::atomic_bool fallbackPending{false};

    unsigned requestCount() const { return source_ ? source_->requests.load() : 0u; }
    unsigned failureCount() const { return source_ ? source_->failures.load() : 0u; }

    // Called on the GUI thread. Terrain must reload all displayed siblings in
    // these extents; a layer revision alone does not schedule that reload.
    std::vector<osgEarth::GeoExtent> refreshFallbackRegions()
    {
        std::vector<osgEarth::GeoExtent> changed;
        {
            std::lock_guard<std::mutex> lock(regions_mutex_);
            for (const auto& region : changed_regions_) changed.push_back(region.getExtent());
            changed_regions_.clear();
        }
        if (!changed.empty()) dirty();
        return changed;
    }

    osgEarth::GeoImage createImageImplementation(const osgEarth::TileKey& key,
                                                osgEarth::ProgressCallback* progress) const override
    {
        using namespace osgEarth;
        if (!source_ || (progress && progress->isCanceled())) return GeoImage::INVALID;
        if (key.getLOD() == 0u)
        {
            // Tianditu's level 0 is a warning image. Assemble the globe from
            // its four level-1 tiles instead of exposing a second map source.
            std::array<GeoImage, 4> children;
            for (unsigned quadrant = 0; quadrant < 4; ++quadrant)
            {
                children[quadrant] = createImageImplementation(key.createChildKey(quadrant), progress);
                if (progress && progress->isCanceled()) return GeoImage::INVALID;
            }
            osg::ref_ptr<osg::Image> mosaic = new osg::Image;
            const int size = static_cast<int>(getTileSize());
            mosaic->allocateImage(size, size, 1, GL_RGBA, GL_UNSIGNED_BYTE);
            ImageUtils::PixelWriter writer(mosaic);
            std::array<std::unique_ptr<GeoImagePixelReader>, 4> readers;
            for (unsigned quadrant = 0; quadrant < 4; ++quadrant)
                if (children[quadrant].valid())
                    readers[quadrant] = std::make_unique<GeoImagePixelReader>(children[quadrant]);
            const auto extent = key.getExtent();
            for (int y = 0; y < size; ++y)
            {
                const double lat = extent.yMin() + (y + 0.5) * extent.height() / size;
                for (int x = 0; x < size; ++x)
                {
                    const double lon = extent.xMin() + (x + 0.5) * extent.width() / size;
                    osg::Vec4f color(58.0f / 255.0f, 62.0f / 255.0f, 68.0f / 255.0f, 1.0f);
                    for (const auto& reader : readers)
                    {
                        if (reader && reader->readCoordWithoutClamping(color, lon, lat))
                            break;
                    }
                    writer(color, x, y);
                }
            }
            return GeoImage(mosaic, extent);
        }
        GeoImage regional;
        if (findRegion(key, regional)) return cropRegion(regional, key);

        auto image = source_->createImage(key, progress);
        if (progress && progress->isCanceled()) return GeoImage::INVALID;
        if (image.valid())
        {
            // A parallel sibling request may have found a hole during this read.
            return findRegion(key, regional) ? cropRegion(regional, key) : image;
        }

        auto parent = key.createParentKey();
        while (parent.valid() && parent.getLOD() >= source_->getMinLevel())
        {
            image = source_->createImage(parent, progress);
            if (progress && progress->isCanceled()) return GeoImage::INVALID;
            if (image.valid()) break;
            parent.makeParent();
        }

        // All descendants of the selected ancestor share this one source image.
        // With no source data, mark the immediate group, not unrelated regions.
        auto region = image.valid() ? parent : key.createParentKey();
        if (!region.valid() || region.getLOD() < source_->getMinLevel()) region = key;
        {
            std::lock_guard<std::mutex> lock(regions_mutex_);
            const auto inserted = regions_.emplace(region, image).second;
            if (inserted) changed_regions_.push_back(region);
        }
        fallbackPending = true;
        findRegion(key, regional);
        return cropRegion(regional, key);
    }

protected:
    osgEarth::Status openImplementation() override
    {
        source_ = new TiandituSourceLayer(options());
        // Separate native-source cache from older, potentially mixed mosaics.
        source_->setCacheID(osgEarth::Cache::makeCacheKey(getURL().full(), "tianditu-native-v2"));
        auto status = source_->open(getReadOptions());
        if (status.isError()) return status;
        options().minLevel() = 0u;
        setCachePolicy(osgEarth::CachePolicy::NO_CACHE);
        return XYZImageLayer::openImplementation();
    }

private:
    bool findRegion(const osgEarth::TileKey& key, osgEarth::GeoImage& image) const
    {
        std::lock_guard<std::mutex> lock(regions_mutex_);
        bool found = false;
        // The widest known region wins when failures at several LODs overlap.
        for (auto ancestor = key; ancestor.valid(); ancestor.makeParent())
        {
            const auto entry = regions_.find(ancestor);
            if (entry != regions_.end())
            {
                image = entry->second;
                found = true;
            }
        }
        return found;
    }

    osgEarth::GeoImage cropRegion(const osgEarth::GeoImage& image,
                                  const osgEarth::TileKey& key) const
    {
        if (image.valid()) return image.crop(key.getExtent(), true, getTileSize(), getTileSize());
        // Opaque neutral coverage prevents another imagery source showing through.
        osg::ref_ptr<osg::Image> missing = new osg::Image;
        missing->allocateImage(1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE);
        auto* pixel = missing->data();
        pixel[0] = 58; pixel[1] = 62; pixel[2] = 68; pixel[3] = 255;
        return osgEarth::GeoImage(missing, key.getExtent());
    }

    osg::ref_ptr<TiandituSourceLayer> source_;
    mutable std::mutex regions_mutex_;
    mutable std::map<osgEarth::TileKey, osgEarth::GeoImage> regions_;
    mutable std::vector<osgEarth::TileKey> changed_regions_;
};

} // namespace VaporView::Map3D::Detail
