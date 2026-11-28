#include "map3d/EoxSentinel2ImageLayer.h"
#include <osgEarth/Profile>
#include <osgEarth/TileKey>
#include <QCoreApplication>
#include <iostream>

namespace {
void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto profile = osgEarth::Profile::create(osgEarth::Profile::SPHERICAL_MERCATOR);
    const osgEarth::TileKey xihu(12, 3414, 1686, profile);
    const auto request = VaporView::Map3D::Detail::eoxSentinel2TileRequest(xihu);
    require(request.url().toString() == QStringLiteral(
                "https://tiles.maps.eox.at/wmts/1.0.0/s2cloudless-2024_3857/default/g/12/1686/3414.jpg"),
            "EOX WMTS expands z/y/x in row/column order");
    const osgEarth::TileKey unsupported(19, 0, 0, profile);
    require(VaporView::Map3D::Detail::eoxSentinel2TileRequest(unsupported).url().isEmpty(),
            "EOX 2024 imagery rejects levels beyond its available data");
    require(QString::fromLatin1(VaporView::Map3D::Detail::kEoxSentinel2Attribution)
                == QStringLiteral("Contains modified Copernicus Sentinel data 2024, EOXcloudless by EOX IT Services GmbH"),
            "EOX attribution names the modified 2024 Copernicus data and provider");
    require(QString::fromLatin1(VaporView::Map3D::Detail::kEoxSentinel2UsageUrl)
                == QStringLiteral("https://cloudless.eox.at/documentation/usage"),
            "EOX attribution points to the usage and license source");
    return 0;
}
