#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/Material>
#include <osg/MatrixTransform>
#include <osg/PrimitiveSet>
#include <osg/StateSet>
#include <osgDB/WriteFile>
#include <osgUtil/Optimizer>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthSemiMajorM = 6378137.0;
constexpr double kEarthFlattening = 1.0 / 298.257223563;
constexpr double kBuildingBasePercentile = 0.5;
constexpr double kBuildingFoundationPercentile = 0.1;
constexpr int kBuildingGroundPaddingPixels = 1;
constexpr double kBuildingFoundationDepthM = 1.0;

struct Point2 {
    double x = 0.0;
    double y = 0.0;
};

struct BuildingGroundSample {
    double baseHeightM = 0.0;
    double foundationBottomHeightM = 0.0;
};

struct TileSummary {
    QString uri;
    double west = 0.0;
    double south = 0.0;
    double east = 0.0;
    double north = 0.0;
    double minimumHeightM = 0.0;
    double maximumHeightM = 0.0;
    int buildingCount = 0;
};

struct DatasetCloser {
    void operator()(GDALDataset* dataset) const
    {
        if (dataset)
        {
            GDALClose(dataset);
        }
    }
};

struct FeatureDeleter {
    void operator()(OGRFeature* feature) const
    {
        if (feature)
        {
            OGRFeature::DestroyFeature(feature);
        }
    }
};

using DatasetPtr = std::unique_ptr<GDALDataset, DatasetCloser>;
using FeaturePtr = std::unique_ptr<OGRFeature, FeatureDeleter>;

double degreesToRadians(double degrees)
{
    return degrees * kPi / 180.0;
}

double signedArea(const std::vector<Point2>& points)
{
    double area = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const Point2& a = points[i];
        const Point2& b = points[(i + 1) % points.size()];
        area += a.x * b.y - b.x * a.y;
    }
    return area * 0.5;
}

double cross(const Point2& a, const Point2& b, const Point2& c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool pointInTriangle(const Point2& point, const Point2& a, const Point2& b, const Point2& c)
{
    const double c1 = cross(a, b, point);
    const double c2 = cross(b, c, point);
    const double c3 = cross(c, a, point);
    constexpr double epsilon = 1e-8;
    return c1 >= -epsilon && c2 >= -epsilon && c3 >= -epsilon;
}

std::vector<std::array<unsigned int, 3>> triangulate(std::vector<Point2> points)
{
    std::vector<std::array<unsigned int, 3>> triangles;
    if (points.size() < 3)
    {
        return triangles;
    }
    if (signedArea(points) < 0.0)
    {
        std::reverse(points.begin(), points.end());
    }

    std::vector<unsigned int> remaining(points.size());
    for (unsigned int i = 0; i < remaining.size(); ++i)
    {
        remaining[i] = i;
    }

    std::size_t guard = 0;
    while (remaining.size() > 3 && guard++ < points.size() * points.size())
    {
        bool clipped = false;
        for (std::size_t i = 0; i < remaining.size(); ++i)
        {
            const unsigned int previous = remaining[(i + remaining.size() - 1) % remaining.size()];
            const unsigned int current = remaining[i];
            const unsigned int next = remaining[(i + 1) % remaining.size()];
            if (cross(points[previous], points[current], points[next]) <= 1e-8)
            {
                continue;
            }

            bool containsPoint = false;
            for (unsigned int candidate : remaining)
            {
                if (candidate == previous || candidate == current || candidate == next)
                {
                    continue;
                }
                if (pointInTriangle(points[candidate], points[previous], points[current], points[next]))
                {
                    containsPoint = true;
                    break;
                }
            }
            if (containsPoint)
            {
                continue;
            }

            triangles.push_back({previous, current, next});
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped)
        {
            break;
        }
    }

    if (remaining.size() == 3)
    {
        triangles.push_back({remaining[0], remaining[1], remaining[2]});
    }
    return triangles;
}

osg::Vec3d geodeticToEcef(double longitudeDeg, double latitudeDeg, double heightM)
{
    const double longitude = degreesToRadians(longitudeDeg);
    const double latitude = degreesToRadians(latitudeDeg);
    const double eccentricitySquared = kEarthFlattening * (2.0 - kEarthFlattening);
    const double sinLatitude = std::sin(latitude);
    const double cosLatitude = std::cos(latitude);
    const double radius = kEarthSemiMajorM / std::sqrt(1.0 - eccentricitySquared * sinLatitude * sinLatitude);
    return {
        (radius + heightM) * cosLatitude * std::cos(longitude),
        (radius + heightM) * cosLatitude * std::sin(longitude),
        (radius * (1.0 - eccentricitySquared) + heightM) * sinLatitude
    };
}

osg::Matrixd localEnuToEcef(double longitudeDeg, double latitudeDeg, double heightM)
{
    const double longitude = degreesToRadians(longitudeDeg);
    const double latitude = degreesToRadians(latitudeDeg);
    const osg::Vec3d origin = geodeticToEcef(longitudeDeg, latitudeDeg, heightM);
    const osg::Vec3d east(-std::sin(longitude), std::cos(longitude), 0.0);
    const osg::Vec3d north(-std::sin(latitude) * std::cos(longitude),
                           -std::sin(latitude) * std::sin(longitude),
                           std::cos(latitude));
    const osg::Vec3d up(std::cos(latitude) * std::cos(longitude),
                        std::cos(latitude) * std::sin(longitude),
                        std::sin(latitude));
    return osg::Matrixd(east.x(), east.y(), east.z(), 0.0,
                        north.x(), north.y(), north.z(), 0.0,
                        up.x(), up.y(), up.z(), 0.0,
                        origin.x(), origin.y(), origin.z(), 1.0);
}

bool parseBbox(const QString& text, double& west, double& south, double& east, double& north)
{
    const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.size() != 4)
    {
        return false;
    }
    bool ok[4] = {};
    west = parts[0].toDouble(&ok[0]);
    south = parts[1].toDouble(&ok[1]);
    east = parts[2].toDouble(&ok[2]);
    north = parts[3].toDouble(&ok[3]);
    return ok[0] && ok[1] && ok[2] && ok[3] && west < east && south < north;
}

std::unique_ptr<OGRGeometry> loadClipGeometry(const QString& path)
{
    if (path.trimmed().isEmpty())
    {
        return {};
    }
    DatasetPtr dataset(static_cast<GDALDataset*>(GDALOpenEx(path.toUtf8().constData(),
                                                            GDAL_OF_VECTOR | GDAL_OF_READONLY,
                                                            nullptr,
                                                            nullptr,
                                                            nullptr)));
    if (!dataset || dataset->GetLayerCount() < 1)
    {
        return {};
    }
    OGRLayer* layer = dataset->GetLayer(0);
    if (!layer)
    {
        return {};
    }
    FeaturePtr feature(layer->GetNextFeature());
    if (!feature || !feature->GetGeometryRef())
    {
        return {};
    }
    return std::unique_ptr<OGRGeometry>(feature->GetGeometryRef()->clone());
}

double sampleDem(GDALDataset* dataset, double longitudeDeg, double latitudeDeg, double fallbackM)
{
    if (!dataset || dataset->GetRasterCount() < 1)
    {
        return fallbackM;
    }
    double transform[6] = {};
    double inverse[6] = {};
    if (dataset->GetGeoTransform(transform) != CE_None || !GDALInvGeoTransform(transform, inverse))
    {
        return fallbackM;
    }
    const int pixel = static_cast<int>(std::floor(inverse[0] + inverse[1] * longitudeDeg + inverse[2] * latitudeDeg));
    const int line = static_cast<int>(std::floor(inverse[3] + inverse[4] * longitudeDeg + inverse[5] * latitudeDeg));
    if (pixel < 0 || line < 0 || pixel >= dataset->GetRasterXSize() || line >= dataset->GetRasterYSize())
    {
        return fallbackM;
    }
    float value = 0.0f;
    if (dataset->GetRasterBand(1)->RasterIO(GF_Read, pixel, line, 1, 1,
                                            &value, 1, 1, GDT_Float32,
                                            0, 0, nullptr) != CE_None)
    {
        return fallbackM;
    }
    int hasNoData = FALSE;
    const double noData = dataset->GetRasterBand(1)->GetNoDataValue(&hasNoData);
    if (!std::isfinite(value) || (hasNoData && value == noData))
    {
        return fallbackM;
    }
    return value;
}

BuildingGroundSample sampleBuildingGround(GDALDataset* dataset,
                                          const OGRGeometry* geometry,
                                          double fallbackM)
{
    if (!dataset || dataset->GetRasterCount() < 1 || !geometry)
    {
        return {fallbackM, fallbackM - kBuildingFoundationDepthM};
    }

    double transform[6] = {};
    double inverse[6] = {};
    if (dataset->GetGeoTransform(transform) != CE_None || !GDALInvGeoTransform(transform, inverse))
    {
        return {fallbackM, fallbackM - kBuildingFoundationDepthM};
    }

    OGREnvelope envelope;
    geometry->getEnvelope(&envelope);
    const std::array<std::array<double, 2>, 4> corners{{
        {envelope.MinX, envelope.MinY},
        {envelope.MinX, envelope.MaxY},
        {envelope.MaxX, envelope.MinY},
        {envelope.MaxX, envelope.MaxY},
    }};

    double minimumPixel = std::numeric_limits<double>::infinity();
    double maximumPixel = -std::numeric_limits<double>::infinity();
    double minimumLine = std::numeric_limits<double>::infinity();
    double maximumLine = -std::numeric_limits<double>::infinity();
    for (const auto& corner : corners)
    {
        const double pixel = inverse[0] + inverse[1] * corner[0] + inverse[2] * corner[1];
        const double line = inverse[3] + inverse[4] * corner[0] + inverse[5] * corner[1];
        minimumPixel = std::min(minimumPixel, pixel);
        maximumPixel = std::max(maximumPixel, pixel);
        minimumLine = std::min(minimumLine, line);
        maximumLine = std::max(maximumLine, line);
    }

    const int firstPixel = std::max(
        0, static_cast<int>(std::floor(minimumPixel)) - kBuildingGroundPaddingPixels);
    const int lastPixel = std::min(
        dataset->GetRasterXSize() - 1,
        static_cast<int>(std::floor(maximumPixel)) + kBuildingGroundPaddingPixels);
    const int firstLine = std::max(
        0, static_cast<int>(std::floor(minimumLine)) - kBuildingGroundPaddingPixels);
    const int lastLine = std::min(
        dataset->GetRasterYSize() - 1,
        static_cast<int>(std::floor(maximumLine)) + kBuildingGroundPaddingPixels);
    if (firstPixel > lastPixel || firstLine > lastLine)
    {
        return {fallbackM, fallbackM - kBuildingFoundationDepthM};
    }

    const int width = lastPixel - firstPixel + 1;
    const int height = lastLine - firstLine + 1;
    std::vector<float> rasterValues(static_cast<std::size_t>(width * height));
    GDALRasterBand* band = dataset->GetRasterBand(1);
    if (!band
        || band->RasterIO(GF_Read, firstPixel, firstLine, width, height,
                          rasterValues.data(), width, height, GDT_Float32,
                          0, 0, nullptr) != CE_None)
    {
        return {fallbackM, fallbackM - kBuildingFoundationDepthM};
    }

    int hasNoData = FALSE;
    const double noData = band->GetNoDataValue(&hasNoData);
    std::vector<float> validValues;
    validValues.reserve(rasterValues.size());
    for (const float value : rasterValues)
    {
        if (std::isfinite(value) && (!hasNoData || value != noData))
        {
            validValues.push_back(value);
        }
    }
    if (validValues.empty())
    {
        return {fallbackM, fallbackM - kBuildingFoundationDepthM};
    }

    std::sort(validValues.begin(), validValues.end());
    const auto percentileValue = [&validValues](double percentile) {
        const std::size_t index = static_cast<std::size_t>(
            std::floor(percentile * static_cast<double>(validValues.size() - 1)));
        return static_cast<double>(validValues[index]);
    };
    return {
        percentileValue(kBuildingBasePercentile),
        percentileValue(kBuildingFoundationPercentile) - kBuildingFoundationDepthM,
    };
}

void appendPolygon(const OGRPolygon* polygon,
                   double centerLongitude,
                   double centerLatitude,
                   double baseOffsetM,
                   double foundationBottomOffsetM,
                   double heightM,
                   osg::Vec3Array* vertices,
                   osg::Vec3Array* normals,
                   osg::Vec4Array* colors,
                   osg::DrawElementsUInt* indices)
{
    if (!polygon || !polygon->getExteriorRing())
    {
        return;
    }
    const OGRLinearRing* ring = polygon->getExteriorRing();
    int pointCount = ring->getNumPoints();
    if (pointCount > 1
        && ring->getX(0) == ring->getX(pointCount - 1)
        && ring->getY(0) == ring->getY(pointCount - 1))
    {
        --pointCount;
    }
    if (pointCount < 3)
    {
        return;
    }

    const double latitudeRadians = degreesToRadians(centerLatitude);
    const double eastScale = kEarthSemiMajorM * std::cos(latitudeRadians) * kPi / 180.0;
    const double northScale = kEarthSemiMajorM * kPi / 180.0;
    std::vector<Point2> footprint;
    footprint.reserve(static_cast<std::size_t>(pointCount));
    for (int i = 0; i < pointCount; ++i)
    {
        footprint.push_back({
            (ring->getX(i) - centerLongitude) * eastScale,
            (ring->getY(i) - centerLatitude) * northScale
        });
    }
    if (signedArea(footprint) < 0.0)
    {
        std::reverse(footprint.begin(), footprint.end());
    }

    const osg::Vec4 wallColor(0.58f, 0.61f, 0.65f, 1.0f);
    const osg::Vec4 roofColor(0.76f, 0.78f, 0.81f, 1.0f);
    const double roofHeightM = baseOffsetM + heightM;
    const auto roofTriangles = triangulate(footprint);
    const unsigned int roofStart = vertices->size();
    for (const Point2& point : footprint)
    {
        vertices->push_back(osg::Vec3(static_cast<float>(point.x),
                                      static_cast<float>(point.y),
                                      static_cast<float>(roofHeightM)));
        normals->push_back(osg::Vec3(0.0f, 0.0f, 1.0f));
        colors->push_back(roofColor);
    }
    for (const auto& triangle : roofTriangles)
    {
        indices->push_back(roofStart + triangle[0]);
        indices->push_back(roofStart + triangle[1]);
        indices->push_back(roofStart + triangle[2]);
    }

    for (std::size_t i = 0; i < footprint.size(); ++i)
    {
        const Point2& a = footprint[i];
        const Point2& b = footprint[(i + 1) % footprint.size()];
        const double dx = b.x - a.x;
        const double dy = b.y - a.y;
        const double length = std::hypot(dx, dy);
        if (length < 0.01)
        {
            continue;
        }
        const osg::Vec3 normal(static_cast<float>(dy / length),
                               static_cast<float>(-dx / length),
                               0.0f);
        const unsigned int start = vertices->size();
        vertices->push_back(osg::Vec3(static_cast<float>(a.x), static_cast<float>(a.y),
                                      static_cast<float>(foundationBottomOffsetM)));
        vertices->push_back(osg::Vec3(static_cast<float>(b.x), static_cast<float>(b.y),
                                      static_cast<float>(foundationBottomOffsetM)));
        vertices->push_back(osg::Vec3(static_cast<float>(b.x), static_cast<float>(b.y),
                                      static_cast<float>(roofHeightM)));
        vertices->push_back(osg::Vec3(static_cast<float>(a.x), static_cast<float>(a.y),
                                      static_cast<float>(roofHeightM)));
        for (int vertex = 0; vertex < 4; ++vertex)
        {
            normals->push_back(normal);
            colors->push_back(wallColor);
        }
        indices->push_back(start);
        indices->push_back(start + 1);
        indices->push_back(start + 2);
        indices->push_back(start);
        indices->push_back(start + 2);
        indices->push_back(start + 3);
    }
}

void appendGeometry(const OGRGeometry* geometry,
                    double centerLongitude,
                    double centerLatitude,
                    double baseOffsetM,
                    double foundationBottomOffsetM,
                    double heightM,
                    osg::Vec3Array* vertices,
                    osg::Vec3Array* normals,
                    osg::Vec4Array* colors,
                    osg::DrawElementsUInt* indices)
{
    if (!geometry)
    {
        return;
    }
    const OGRwkbGeometryType type = wkbFlatten(geometry->getGeometryType());
    if (type == wkbPolygon)
    {
        appendPolygon(geometry->toPolygon(), centerLongitude, centerLatitude,
                      baseOffsetM, foundationBottomOffsetM, heightM,
                      vertices, normals, colors, indices);
    }
    else if (type == wkbMultiPolygon)
    {
        const auto* multiPolygon = geometry->toMultiPolygon();
        for (int i = 0; i < multiPolygon->getNumGeometries(); ++i)
        {
            appendPolygon(multiPolygon->getGeometryRef(i)->toPolygon(),
                          centerLongitude, centerLatitude,
                          baseOffsetM, foundationBottomOffsetM, heightM,
                          vertices, normals, colors, indices);
        }
    }
}

QJsonArray regionArray(double west, double south, double east, double north,
                       double minimumHeightM, double maximumHeightM)
{
    return {
        degreesToRadians(west),
        degreesToRadians(south),
        degreesToRadians(east),
        degreesToRadians(north),
        minimumHeightM,
        maximumHeightM
    };
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vaporview_building_tileset"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Generate local OSG-native building tiles and a tileset.json index."));
    parser.addHelpOption();
    parser.addOption({QStringList{QStringLiteral("i"), QStringLiteral("input")},
                      QStringLiteral("Input buildings GeoPackage."), QStringLiteral("path")});
    parser.addOption({QStringList{QStringLiteral("o"), QStringLiteral("output")},
                      QStringLiteral("Output tileset directory."), QStringLiteral("path")});
    parser.addOption({QStringList{QStringLiteral("bbox")},
                      QStringLiteral("west,south,east,north in WGS84 degrees."), QStringLiteral("bbox")});
    parser.addOption({QStringList{QStringLiteral("clip")},
                      QStringLiteral("Optional GeoJSON polygon used to keep building centroids inside the AOI."),
                      QStringLiteral("path")});
    parser.addOption({QStringList{QStringLiteral("dem")},
                      QStringLiteral("Optional WGS84 DEM raster used for tile base elevation."),
                      QStringLiteral("path")});
    parser.addOption({QStringList{QStringLiteral("tile-size")},
                      QStringLiteral("Tile size in degrees."), QStringLiteral("degrees"), QStringLiteral("0.025")});
    parser.addOption({QStringList{QStringLiteral("fallback-base-height")},
                      QStringLiteral("Fallback tile base elevation in metres."), QStringLiteral("metres"), QStringLiteral("20")});
    parser.addOption({QStringList{QStringLiteral("overwrite")},
                      QStringLiteral("Replace an existing output directory.")});
    parser.process(application);

    const QString inputPath = QFileInfo(parser.value(QStringLiteral("input"))).absoluteFilePath();
    const QString outputPath = QFileInfo(parser.value(QStringLiteral("output"))).absoluteFilePath();
    if (!QFileInfo(inputPath).isFile() || outputPath.trimmed().isEmpty())
    {
        QTextStream(stderr) << "Input GeoPackage and output directory are required.\n";
        return 2;
    }

    double west = 0.0;
    double south = 0.0;
    double east = 0.0;
    double north = 0.0;
    if (!parseBbox(parser.value(QStringLiteral("bbox")), west, south, east, north))
    {
        QTextStream(stderr) << "Invalid --bbox; expected west,south,east,north.\n";
        return 2;
    }

    bool tileSizeOk = false;
    const double tileSize = parser.value(QStringLiteral("tile-size")).toDouble(&tileSizeOk);
    bool fallbackOk = false;
    const double fallbackBaseHeightM =
        parser.value(QStringLiteral("fallback-base-height")).toDouble(&fallbackOk);
    if (!tileSizeOk || tileSize <= 0.0 || !fallbackOk)
    {
        QTextStream(stderr) << "Invalid tile size or fallback base height.\n";
        return 2;
    }

    QDir outputDirectory(outputPath);
    if (outputDirectory.exists())
    {
        if (!parser.isSet(QStringLiteral("overwrite")))
        {
            QTextStream(stderr) << "Output directory exists; pass --overwrite to replace it.\n";
            return 2;
        }
        if (!outputDirectory.removeRecursively())
        {
            QTextStream(stderr) << "Failed to remove existing output directory.\n";
            return 2;
        }
    }
    if (!QDir().mkpath(QDir(outputPath).filePath(QStringLiteral("content"))))
    {
        QTextStream(stderr) << "Failed to create output directory.\n";
        return 2;
    }

    GDALAllRegister();
    DatasetPtr buildings(static_cast<GDALDataset*>(GDALOpenEx(inputPath.toUtf8().constData(),
                                                              GDAL_OF_VECTOR | GDAL_OF_READONLY,
                                                              nullptr,
                                                              nullptr,
                                                              nullptr)));
    if (!buildings)
    {
        QTextStream(stderr) << "Failed to open buildings GeoPackage.\n";
        return 2;
    }
    OGRLayer* layer = buildings->GetLayerByName("buildings");
    if (!layer)
    {
        QTextStream(stderr) << "Input GeoPackage does not contain layer 'buildings'.\n";
        return 2;
    }
    const int heightField = layer->GetLayerDefn()->GetFieldIndex("extrusion_height_m");
    if (heightField < 0)
    {
        QTextStream(stderr) << "Buildings layer does not contain extrusion_height_m.\n";
        return 2;
    }

    const QString clipPath = parser.value(QStringLiteral("clip"));
    std::unique_ptr<OGRGeometry> clipGeometry = loadClipGeometry(clipPath);
    if (!clipPath.isEmpty() && !clipGeometry)
    {
        QTextStream(stderr) << "Failed to load clip geometry: " << clipPath << '\n';
        return 2;
    }

    DatasetPtr dem;
    const QString demPath = parser.value(QStringLiteral("dem"));
    if (QFileInfo(demPath).isFile())
    {
        dem.reset(static_cast<GDALDataset*>(GDALOpenEx(demPath.toUtf8().constData(),
                                                      GDAL_OF_RASTER | GDAL_OF_READONLY,
                                                      nullptr,
                                                      nullptr,
                                                      nullptr)));
    }

    const int columns = static_cast<int>(std::ceil((east - west) / tileSize));
    const int rows = static_cast<int>(std::ceil((north - south) / tileSize));
    std::vector<TileSummary> summaries;
    int totalBuildings = 0;
    QTextStream output(stdout);

    for (int row = 0; row < rows; ++row)
    {
        const double tileSouth = south + row * tileSize;
        const double tileNorth = std::min(north, tileSouth + tileSize);
        for (int column = 0; column < columns; ++column)
        {
            const double tileWest = west + column * tileSize;
            const double tileEast = std::min(east, tileWest + tileSize);
            const double centerLongitude = (tileWest + tileEast) * 0.5;
            const double centerLatitude = (tileSouth + tileNorth) * 0.5;
            const double baseHeightM = sampleDem(dem.get(), centerLongitude, centerLatitude, fallbackBaseHeightM);

            osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
            osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array;
            osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
            osg::ref_ptr<osg::DrawElementsUInt> indices = new osg::DrawElementsUInt(GL_TRIANGLES);
            int buildingCount = 0;
            double minimumHeightM = std::numeric_limits<double>::infinity();
            double maximumHeightM = -std::numeric_limits<double>::infinity();

            layer->SetSpatialFilterRect(tileWest, tileSouth, tileEast, tileNorth);
            layer->ResetReading();
            while (FeaturePtr feature{layer->GetNextFeature()})
            {
                const OGRGeometry* geometry = feature->GetGeometryRef();
                if (!geometry)
                {
                    continue;
                }
                OGREnvelope envelope;
                geometry->getEnvelope(&envelope);
                const double featureLongitude = (envelope.MinX + envelope.MaxX) * 0.5;
                const double featureLatitude = (envelope.MinY + envelope.MaxY) * 0.5;
                const bool isLastColumn = column == columns - 1;
                const bool isLastRow = row == rows - 1;
                if (featureLongitude < tileWest
                    || featureLongitude > tileEast
                    || (!isLastColumn && featureLongitude == tileEast)
                    || featureLatitude < tileSouth
                    || featureLatitude > tileNorth
                    || (!isLastRow && featureLatitude == tileNorth))
                {
                    continue;
                }
                if (clipGeometry)
                {
                    OGRPoint center(featureLongitude, featureLatitude);
                    if (!clipGeometry->Contains(&center))
                    {
                        continue;
                    }
                }

                double heightM = feature->GetFieldAsDouble(heightField);
                if (!std::isfinite(heightM) || heightM < 3.0)
                {
                    heightM = 10.0;
                }
                heightM = std::clamp(heightM, 3.0, 300.0);
                const BuildingGroundSample buildingGround =
                    sampleBuildingGround(dem.get(), geometry, baseHeightM);
                const double baseOffsetM = buildingGround.baseHeightM - baseHeightM;
                const double foundationBottomOffsetM =
                    buildingGround.foundationBottomHeightM - baseHeightM;
                appendGeometry(geometry, centerLongitude, centerLatitude,
                               baseOffsetM, foundationBottomOffsetM, heightM,
                               vertices.get(), normals.get(), colors.get(), indices.get());
                minimumHeightM =
                    std::min(minimumHeightM, buildingGround.foundationBottomHeightM);
                maximumHeightM =
                    std::max(maximumHeightM, buildingGround.baseHeightM + heightM);
                ++buildingCount;
            }
            layer->SetSpatialFilter(nullptr);

            if (buildingCount == 0 || indices->empty())
            {
                continue;
            }

            osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
            geometry->setVertexArray(vertices.get());
            geometry->setNormalArray(normals.get(), osg::Array::BIND_PER_VERTEX);
            geometry->setColorArray(colors.get(), osg::Array::BIND_PER_VERTEX);
            geometry->addPrimitiveSet(indices.get());
            geometry->setUseDisplayList(false);
            geometry->setUseVertexBufferObjects(true);

            osg::ref_ptr<osg::Geode> geode = new osg::Geode;
            geode->setName("Xihu OSM building mesh");
            geode->addDrawable(geometry.get());
            osg::ref_ptr<osg::Material> material = new osg::Material;
            material->setColorMode(osg::Material::AMBIENT_AND_DIFFUSE);
            material->setAmbient(osg::Material::FRONT_AND_BACK, osg::Vec4(0.35f, 0.36f, 0.38f, 1.0f));
            material->setDiffuse(osg::Material::FRONT_AND_BACK, osg::Vec4(0.72f, 0.74f, 0.77f, 1.0f));
            geode->getOrCreateStateSet()->setAttributeAndModes(material.get(), osg::StateAttribute::ON);
            geode->getOrCreateStateSet()->setMode(GL_CULL_FACE, osg::StateAttribute::ON);

            osg::ref_ptr<osg::MatrixTransform> transform = new osg::MatrixTransform;
            transform->setName("Xihu building tile");
            transform->setMatrix(localEnuToEcef(centerLongitude, centerLatitude, baseHeightM));
            transform->addChild(geode.get());

            osgUtil::Optimizer optimizer;
            optimizer.optimize(transform.get(), osgUtil::Optimizer::DEFAULT_OPTIMIZATIONS);

            const QString fileName =
                QStringLiteral("buildings_r%1_c%2.osgb").arg(row, 3, 10, QLatin1Char('0')).arg(column, 3, 10, QLatin1Char('0'));
            const QString relativeUri = QStringLiteral("content/%1").arg(fileName);
            const QString absoluteFile = QDir(outputPath).filePath(relativeUri);
            if (!osgDB::writeNodeFile(*transform, QDir::fromNativeSeparators(absoluteFile).toStdString()))
            {
                QTextStream(stderr) << "Failed to write " << absoluteFile << '\n';
                return 2;
            }

            summaries.push_back({relativeUri, tileWest, tileSouth, tileEast, tileNorth,
                                 minimumHeightM, maximumHeightM, buildingCount});
            totalBuildings += buildingCount;
            output << "tile " << row << ',' << column << ": " << buildingCount
                   << " buildings -> " << relativeUri << '\n';
        }
    }

    QJsonArray children;
    for (const TileSummary& summary : summaries)
    {
        QJsonObject child;
        child.insert(QStringLiteral("boundingVolume"),
                     QJsonObject{{QStringLiteral("region"),
                                  regionArray(summary.west, summary.south, summary.east, summary.north,
                                              summary.minimumHeightM, summary.maximumHeightM)}});
        child.insert(QStringLiteral("geometricError"), 0);
        child.insert(QStringLiteral("content"), QJsonObject{{QStringLiteral("uri"), summary.uri}});
        child.insert(QStringLiteral("extras"),
                     QJsonObject{{QStringLiteral("buildingCount"), summary.buildingCount},
                                 {QStringLiteral("payloadFormat"), QStringLiteral("osg-native-osgb")}});
        children.push_back(child);
    }

    QJsonObject root;
    root.insert(QStringLiteral("boundingVolume"),
                QJsonObject{{QStringLiteral("region"),
                             regionArray(west, south, east, north, -100.0, 1000.0)}});
    root.insert(QStringLiteral("geometricError"), 1000.0);
    root.insert(QStringLiteral("refine"), QStringLiteral("ADD"));
    root.insert(QStringLiteral("children"), children);

    QJsonObject document;
    document.insert(QStringLiteral("asset"),
                    QJsonObject{{QStringLiteral("version"), QStringLiteral("1.1")},
                                {QStringLiteral("generator"), QStringLiteral("VaporView building tileset generator")},
                                {QStringLiteral("generatorVersion"), QStringLiteral("1.3")}});
    document.insert(QStringLiteral("geometricError"), 1000.0);
    document.insert(QStringLiteral("root"), root);
    document.insert(QStringLiteral("extras"),
                    QJsonObject{{QStringLiteral("format"), QStringLiteral("vaporview-osg-native-building-tiles")},
                                {QStringLiteral("groundPlacement"), QStringLiteral("per-building-dem-p50-p10-skirt")},
                                {QStringLiteral("foundationDepthM"), kBuildingFoundationDepthM},
                                {QStringLiteral("buildingCount"), totalBuildings},
                                {QStringLiteral("tileCount"), static_cast<int>(summaries.size())},
                                {QStringLiteral("clipPath"), clipPath}});

    QFile tilesetFile(QDir(outputPath).filePath(QStringLiteral("tileset.json")));
    if (!tilesetFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        QTextStream(stderr) << "Failed to write tileset.json.\n";
        return 2;
    }
    tilesetFile.write(QJsonDocument(document).toJson(QJsonDocument::Indented));
    tilesetFile.close();

    output << "Generated " << summaries.size() << " tiles containing "
           << totalBuildings << " buildings under " << outputPath << '\n';
    return summaries.empty() ? 2 : 0;
}
