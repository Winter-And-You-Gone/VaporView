#include "map3d/OsgEarthViewWidget.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QThread>
#include <QThreadPool>

#include <cstdlib>
#include <iostream>
#include <functional>

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

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return predicate();
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

    bool earthFinished = false;
    bool earthLoaded = false;
    QElapsedTimer asyncReturnTimer;
    asyncReturnTimer.start();
    view.loadEarthFileAsync(earthPath, [&](bool loaded) {
        earthLoaded = loaded;
        earthFinished = true;
    });
    require(asyncReturnTimer.elapsed() < 250,
            QStringLiteral("earth async API returns without blocking the GUI thread"));
    require(waitUntil([&]() { return earthFinished; }, 30000) && earthLoaded,
            QStringLiteral("load Hangzhou Xihu real-3D earth file asynchronously"));

    const VaporView::Map3D::EarthLoadDiagnostics earthDiagnostics =
        view.earthLoadDiagnostics();
    require(earthDiagnostics.loaded,
            QStringLiteral("earth diagnostics report successful load"));
    require(earthDiagnostics.foundMapNode,
            QStringLiteral("earth file contains an osgEarth MapNode"));
    require(earthDiagnostics.layerSummaries.join(QStringLiteral(" | "))
                .contains(QStringLiteral("visible-first screen-space LOD")),
            QStringLiteral("terrain prioritizes imagery for the visible camera region"));
    require(earthDiagnostics.layerSummaries.join(QStringLiteral(" | "))
                .contains(QStringLiteral("low-angle pitch guard")),
            QStringLiteral("earth camera avoids near-horizontal views that overdraw distant terrain"));

    VaporView::Geo::NavSample unresolvedMslSample;
    unresolvedMslSample.latDeg = 30.25;
    unresolvedMslSample.lonDeg = 120.15;
    unresolvedMslSample.heightM = 25.0;
    unresolvedMslSample.ecefXM = 0.0;
    unresolvedMslSample.ecefYM = 0.0;
    unresolvedMslSample.ecefZM = 0.0;
    unresolvedMslSample.heightReference = VaporView::Geo::HeightReference::MeanSeaLevel;
    unresolvedMslSample.fixQuality = VaporView::Geo::FixQuality::Fixed;
    view.setSamples({unresolvedMslSample});
    VaporView::Map3D::Map3DPerformanceStats heightStats = view.performanceStats();
    require(heightStats.qualityStats.invalidSamples == 1,
            QStringLiteral("MSL sample without ECEF is omitted instead of treated as ellipsoid height"));
    require(heightStats.heightReferenceStatus.contains(QStringLiteral("omitted")),
            QStringLiteral("unresolved height datum is reported explicitly"));
    require(view.flyToTrack(),
            QStringLiteral("Earth fly-to ignores unusable zero ECEF values and keeps a valid map camera target"));

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

    VaporView::Geo::NavSample corruptRecordedEcefSample;
    corruptRecordedEcefSample.latDeg = 30.136981202;
    corruptRecordedEcefSample.lonDeg = 120.069381752;
    corruptRecordedEcefSample.heightM = 9.605644;
    corruptRecordedEcefSample.ecefXM = 365504425.008990;
    corruptRecordedEcefSample.ecefYM = 13374370.950326;
    corruptRecordedEcefSample.ecefZM = 58160.200631;
    corruptRecordedEcefSample.heightReference = VaporView::Geo::HeightReference::Wgs84Ellipsoid;
    corruptRecordedEcefSample.fixQuality = VaporView::Geo::FixQuality::Fixed;
    require(!corruptRecordedEcefSample.hasEcef(),
            QStringLiteral("session ECEF far outside the WGS84 shell is rejected"));
    view.setSamples({corruptRecordedEcefSample});
    heightStats = view.performanceStats();
    require(heightStats.qualityStats.lineSamples == 1,
            QStringLiteral("valid LLH remains renderable when recorded ECEF is corrupt"));
    require(heightStats.heightReferenceStatus.contains(QStringLiteral("WGS84 ellipsoid")),
            QStringLiteral("corrupt recorded ECEF falls back to WGS84 LLH conversion"));
    require(view.flyToTrack(),
            QStringLiteral("track focus uses LLH instead of corrupt recorded ECEF"));
    view.clearTrack();

    bool tilesFinished = false;
    bool tilesLoaded = false;
    asyncReturnTimer.restart();
    view.loadLocal3DTilesPreviewAsync(tilesetPath, [&](bool loaded) {
        tilesLoaded = loaded;
        tilesFinished = true;
    });
    require(asyncReturnTimer.elapsed() < 250,
            QStringLiteral("building async API returns without blocking the GUI thread"));
    require(waitUntil([&]() { return tilesFinished; }, 30000) && tilesLoaded,
            QStringLiteral("load Hangzhou Xihu building tileset asynchronously"));

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

    bool staleLoadCallbackCalled = false;
    view.loadLocal3DTilesPreviewAsync(tilesetPath, [&](bool) {
        staleLoadCallbackCalled = true;
    });
    view.clearLocal3DTilesPreview();
    require(QThreadPool::globalInstance()->waitForDone(30000),
            QStringLiteral("superseded building load worker finishes"));
    processEventsFor(100);
    require(!view.hasLocal3DTilesPreview(),
            QStringLiteral("clearing buildings invalidates an in-flight async load"));
    require(!staleLoadCallbackCalled,
            QStringLiteral("superseded async load does not invoke a stale UI callback"));

    view.shutdown();
    view.close();
    processEventsFor(250);
    std::cout << "map3d_real_data_load_test passed: 55/55 building tiles loaded\n";
    return 0;
}
