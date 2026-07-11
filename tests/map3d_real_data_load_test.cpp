#include "map3d/OsgEarthViewWidget.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QThread>

#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const QString& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message.toStdString() << '\n';
        std::exit(1);
    }
}

void processEventsFor(int timeoutMs)
{
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    const QDir sourceRoot(QStringLiteral(VAPORVIEW_SOURCE_DIR));
    const QString earthPath =
        sourceRoot.filePath(QStringLiteral("resources/maps/vaporview_real3d_local.earth"));
    const QString tilesetPath =
        sourceRoot.filePath(QStringLiteral("resources/maps/tiles3d/local/tileset.json"));

    if (!QFileInfo::exists(earthPath) || !QFileInfo::exists(tilesetPath))
    {
        std::cout << "SKIP: Hangzhou Xihu real-3D local data is not installed\n";
        return 77;
    }

    VaporView::Map3D::OsgEarthViewWidget view;
    view.resize(800, 600);
    view.show();
    processEventsFor(250);

    require(view.loadEarthFile(earthPath),
            QStringLiteral("load Hangzhou Xihu real-3D earth file"));

    const VaporView::Map3D::EarthLoadDiagnostics earthDiagnostics =
        view.earthLoadDiagnostics();
    require(earthDiagnostics.loaded,
            QStringLiteral("earth diagnostics report successful load"));
    require(earthDiagnostics.foundMapNode,
            QStringLiteral("earth file contains an osgEarth MapNode"));

    VaporView::Geo::NavSample unresolvedMslSample;
    unresolvedMslSample.latDeg = 30.25;
    unresolvedMslSample.lonDeg = 120.15;
    unresolvedMslSample.heightM = 25.0;
    unresolvedMslSample.heightReference = VaporView::Geo::HeightReference::MeanSeaLevel;
    unresolvedMslSample.fixQuality = VaporView::Geo::FixQuality::Fixed;
    view.setSamples({unresolvedMslSample});
    VaporView::Map3D::Map3DPerformanceStats heightStats = view.performanceStats();
    require(heightStats.qualityStats.invalidSamples == 1,
            QStringLiteral("MSL sample without ECEF is omitted instead of treated as ellipsoid height"));
    require(heightStats.heightReferenceStatus.contains(QStringLiteral("omitted")),
            QStringLiteral("unresolved height datum is reported explicitly"));

    VaporView::Geo::NavSample recordedEcefSample = unresolvedMslSample;
    recordedEcefSample.ecefXM = -2764490.0;
    recordedEcefSample.ecefYM = 4787610.0;
    recordedEcefSample.ecefZM = 3170380.0;
    view.setSamples({recordedEcefSample});
    heightStats = view.performanceStats();
    require(heightStats.qualityStats.lineSamples == 1,
            QStringLiteral("recorded ECEF keeps non-ellipsoid samples renderable"));
    require(heightStats.heightReferenceStatus.contains(QStringLiteral("recorded ECEF")),
            QStringLiteral("recorded ECEF height handling is reported explicitly"));
    view.clearTrack();

    require(view.loadLocal3DTilesPreview(tilesetPath),
            QStringLiteral("load Hangzhou Xihu building tileset"));

    QFile tilesetFile(tilesetPath);
    require(tilesetFile.open(QIODevice::ReadOnly),
            QStringLiteral("open Hangzhou Xihu building tileset index"));
    const QJsonDocument tilesetDocument = QJsonDocument::fromJson(tilesetFile.readAll());
    require(tilesetDocument.isObject(),
            QStringLiteral("building tileset index is valid JSON"));
    const QJsonObject tileset = tilesetDocument.object();
    require(tileset.value(QStringLiteral("extras")).toObject()
                .value(QStringLiteral("groundPlacement")).toString()
                == QStringLiteral("per-building-dem-p50-p10-skirt"),
            QStringLiteral("building tileset uses per-building DEM ground placement"));

    int terrainAwareTileCount = 0;
    const QJsonArray children =
        tileset.value(QStringLiteral("root")).toObject()
            .value(QStringLiteral("children")).toArray();
    for (const QJsonValue& childValue : children)
    {
        const QJsonArray region =
            childValue.toObject()
                .value(QStringLiteral("boundingVolume")).toObject()
                .value(QStringLiteral("region")).toArray();
        if (region.size() >= 6 && region.at(5).toDouble() - region.at(4).toDouble() > 100.0)
        {
            ++terrainAwareTileCount;
        }
    }
    require(terrainAwareTileCount > 0,
            QStringLiteral("building tileset bounding volumes include per-building terrain variation"));

    const VaporView::Map3D::Local3DTilesLoadDiagnostics tileDiagnostics =
        view.local3DTilesLoadDiagnostics();
    require(tileDiagnostics.loaded,
            QStringLiteral("building diagnostics report successful load"));
    require(tileDiagnostics.payloadCount == 55,
            QStringLiteral("tileset contains 55 building payloads, got %1")
                .arg(tileDiagnostics.payloadCount));
    require(tileDiagnostics.loadedPayloadCount == 55,
            QStringLiteral("all 55 building payloads load successfully, got %1")
                .arg(tileDiagnostics.loadedPayloadCount));
    require(tileDiagnostics.warnings.isEmpty(),
            QStringLiteral("building tiles load without warnings: %1")
                .arg(tileDiagnostics.warnings.join(QStringLiteral(" | "))));

    view.shutdown();
    view.close();
    processEventsFor(250);
    std::cout << "map3d_real_data_load_test passed: 55/55 building tiles loaded\n";
    return 0;
}
