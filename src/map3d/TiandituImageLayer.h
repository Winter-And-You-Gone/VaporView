#pragma once

#include <osg/Image>
#include <osgEarth/XYZ>
#include <osgEarth/Progress>
#include <algorithm>
#include <atomic>
#include <cmath>

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

class TiandituImageLayer final : public osgEarth::XYZImageLayer
{
public:
    mutable std::atomic_uint requests{0};
    mutable std::atomic_uint failures{0};
    mutable std::atomic_bool fallbackPending{false};

    osgEarth::GeoImage createImageImplementation(const osgEarth::TileKey& key,
                                                osgEarth::ProgressCallback* progress) const override
    {
        ++requests;
        auto image = osgEarth::XYZImageLayer::createImageImplementation(key, progress);
        if (progress && progress->isCanceled()) return osgEarth::GeoImage::INVALID;
        if (!image.valid() || isTiandituNoImageryTile(image.getImage()))
        {
            ++failures;
            fallbackPending = true;
            // INVALID makes osgEarth inherit the nearest available parent tile
            // with the correct texture scale/bias; never cache warning pixels.
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
            fallbackPending = true;
            image = osgEarth::GeoImage::INVALID;
        }
    }
};

} // namespace VaporView::Map3D::Detail
