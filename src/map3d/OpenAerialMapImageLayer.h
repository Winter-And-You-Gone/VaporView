#pragma once

#include <osgEarth/ImageUtils>
#include <osgEarth/Profile>
#include <osgEarth/XYZ>

#include <algorithm>

namespace VaporView::Map3D::Detail {

inline osgEarth::GeoImage maskOpenAerialMapNoData(osgEarth::GeoImage image)
{
    if (!image.valid()) return image;
    const osg::Image* source = image.getImage();
    if (!source || !source->data() || source->getDataType() != GL_UNSIGNED_BYTE || source->isCompressed())
        return image;

    osg::ref_ptr<osg::Image> masked = osgEarth::ImageUtils::convertToRGBA8(source);
    if (!masked) return image;
    bool changed = false;
    bool hasVisiblePixel = false;
    bool allVisiblePixelsGrayscale = true;
    unsigned minGray = 255;
    unsigned maxGray = 0;
    // OAM's older imagery uses RGB 0 as no-data, including within partly covered tiles.
    for (int y = 0; y < masked->t(); ++y)
        for (int x = 0; x < masked->s(); ++x)
        {
            unsigned char* pixel = masked->data(x, y);
            if (pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] != 0)
            {
                pixel[3] = 0;
                changed = true;
            }
            if (pixel[3] != 0)
            {
                hasVisiblePixel = true;
                if (pixel[0] == pixel[1] && pixel[1] == pixel[2])
                {
                    minGray = (std::min)(minGray, static_cast<unsigned>(pixel[0]));
                    maxGray = (std::max)(maxGray, static_cast<unsigned>(pixel[0]));
                }
                else
                {
                    allVisiblePixelsGrayscale = false;
                }
            }
        }
    if (!hasVisiblePixel) return osgEarth::GeoImage::INVALID;
    // Very coarse OAM rasters can produce almost uniform gray tiles that hide
    // the basemap at detailed zooms. Keep textured grayscale and color imagery.
    if (allVisiblePixelsGrayscale && maxGray - minGray <= 4u)
        return osgEarth::GeoImage::INVALID;
    return changed ? osgEarth::GeoImage(masked.get(), image.getExtent()) : image;
}

class OpenAerialMapImageLayer final : public osgEarth::XYZImageLayer
{
public:
    OpenAerialMapImageLayer()
    {
        setName("OpenAerialMap Satellite imagery");
        setURL(osgEarth::URI("https://global.imagery.hotosm.org/{z}/{x}/{y}.png"));
        setProfile(osgEarth::Profile::create(osgEarth::Profile::SPHERICAL_MERCATOR));
        setFormat("png");
        // The service serves coverage grids rather than imagery below zoom 14.
        options().minLevel() = 14u;
        options().maxLevel() = 22u;
        options().maxDataLevel() = 22u;
    }

    osgEarth::GeoImage createImageImplementation(const osgEarth::TileKey& key,
                                                 osgEarth::ProgressCallback* progress) const override
    {
        return maskOpenAerialMapNoData(XYZImageLayer::createImageImplementation(key, progress));
    }

    void postCreateImageImplementation(osgEarth::GeoImage& image, const osgEarth::TileKey&,
                                       osgEarth::ProgressCallback*) const override
    {
        image = maskOpenAerialMapNoData(image);
    }
};

} // namespace VaporView::Map3D::Detail
