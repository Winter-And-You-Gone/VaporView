#include "test_settings_sandbox.h"
#include "map3d/OsgEarthViewWidget.h"
#include "../src/map3d/Map3DAssetLoader.h"
#include "../src/map3d/CesiumTerrain.h"
#include "../src/map3d/MapHttp.h"
#include "../src/map3d/Map3DRuntime.h"
#include "../src/map3d/OpenFreeMapBuildings.h"

#include <osgEarth/ElevationLayer>
#include <osgEarth/ElevationPool>
#include <osgEarth/Map>
#include <osgEarth/MapNode>
#include <osgEarth/SpatialReference>
#include <osgEarth/Threading>

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
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QWheelEvent>
#include <QString>
#include <QThread>
#include <QThreadPool>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <functional>
#include <future>
#include <limits>

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

void checkHttpCache()
{
    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost), QStringLiteral("HTTP cache fixture listens"));
    int requests = 0;
    bool conditional = false;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        auto* socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
            if (!socket->peek(socket->bytesAvailable()).contains("\r\n\r\n")) return;
            const QByteArray headers = socket->readAll().toLower();
            conditional = headers.contains("if-none-match: \"one\"");
            ++requests;
            const bool fresh = headers.startsWith("get /fresh");
            const bool unchanged = !fresh && requests == 3 && conditional;
            QByteArray response = unchanged ? "HTTP/1.1 304 Not Modified\r\n" : "HTTP/1.1 200 OK\r\n";
            response += fresh ? "Cache-Control: public, max-age=3600\r\n"
                              : "Cache-Control: public\r\nExpires: Mon, 01 Jan 2001 00:00:00 GMT\r\n";
            response += "Date: " + QLocale::c().toString(QDateTime::currentDateTimeUtc(),
                QStringLiteral("ddd, dd MMM yyyy HH:mm:ss 'GMT'")).toLatin1() + "\r\n";
            const QByteArray body = unchanged ? "" : requests <= 2 ? "first" : "updated";
            response += requests <= 3 ? "ETag: \"one\"\r\n" : "ETag: \"two\"\r\n";
            response += "Connection: close\r\nContent-Length: " + QByteArray::number(body.size());
            socket->write(response + "\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
    });
    const auto get = [&](const QString& path) {
        QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/%2").arg(server.serverPort()).arg(path)));
        auto future = std::async(std::launch::async, [request] {
            return VaporView::Map3D::Detail::mapHttpGet(request, nullptr);
        });
        require(waitUntil([&] { return future.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }, 10000),
                QStringLiteral("HTTP map request completed"));
        return future.get();
    };
    require(get("fresh").body == "first" && get("fresh").fromCache && requests == 1,
            QStringLiteral("fresh tile reused without an HTTP request"));
    require(get("expired").body == "first", QStringLiteral("expired tile initially downloaded"));
    require(get("expired").body == "first" && requests == 3 && conditional,
            QStringLiteral("expired tile conditionally validated and reused after 304"));
    require(get("expired").body == "updated" && requests == 4 && conditional,
            QStringLiteral("changed tile replaced after revalidation"));
}

void checkTerrainLevels(osgEarth::Map* map)
{
    osgEarth::ElevationLayerVector layers;
    map->getLayers(layers);
    require(!layers.empty(), QStringLiteral("built-in map has elevation layers"));
    const auto distantKey = map->getProfile()->createTileKey(120.07, 30.2, 7);
    for (const auto& layer : layers)
    {
        require(layer->getMinLevel() == 8 && !layer->getBestAvailableTileKey(distantKey).valid(),
                QStringLiteral("DEM starts at level 8: %1").arg(QString::fromStdString(layer->getName())));
    }
    osg::ref_ptr<osg::HeightField> field = new osg::HeightField;
    field->allocate(257, 257); // Match the terrain renderer's elevation grid.
    require(!layers.populateHeightField(field, nullptr, distantKey, nullptr, osgEarth::INTERP_BILINEAR, nullptr),
            QStringLiteral("level 7 skips the entire elevation stack"));
}

void checkLiveBuildings(const QString& tilesetPath)
{
    using namespace VaporView::Map3D;
    using namespace osgEarth;
    initializeMap3DRuntime();
    osg::ref_ptr<Map> map = new Map;
    osg::ref_ptr<Detail::OpenFreeMapBuildings> online = new Detail::OpenFreeMapBuildings;
    map->addLayer(online.get());
    const TileKey xihu(14, 13658, 6745, online->getProfile());
    const TileKey downtown(14, 13663, 6746, online->getProfile());
    require(!online->createTile(xihu.createParentKey(), nullptr), QStringLiteral("distant views skip buildings"));
    const auto tile = online->createTile(xihu, nullptr);
    require(tile && tile->getBound().valid(), QStringLiteral("live Xihu tile produces a 3D mesh"));
    if (QFileInfo::exists(tilesetPath))
    {
        const auto local = Detail::loadLocal3DTilesAsset(tilesetPath);
        const auto* native = dynamic_cast<Detail::LocalBuildingTiles*>(local.node.get());
        require(native && native->coverage.contains(QPointF(120.1218, 30.2639)),
                QStringLiteral("native coverage includes Xihu sample"));
        osg::ref_ptr<Detail::OpenFreeMapBuildings> fallback = new Detail::OpenFreeMapBuildings(native->coverage);
        map->addLayer(fallback.get());
        require(!fallback->createTile(xihu, nullptr), QStringLiteral("native Xihu buildings suppress the online tile"));
        const auto outside = fallback->createTile(downtown, nullptr);
        require(outside && outside->getBound().valid(), QStringLiteral("Hangzhou city centre streams outside local coverage"));
    }

    // Isolate the building layer so a frame change measures real paged drawables.
    OsgEarthViewWidget view;
    view.resize(820, 580);
    view.show();
    view.startRendering();
    for (size_t i = 0; i < kMap3DLayerCount; ++i)
        view.setLayerVisible(static_cast<Map3DLayer>(i), false);
    view.setFollowAircraft(false);
    require(view.loadOnlineGlobe(), QStringLiteral("online globe opens without a local map"));
    VaporView::Geo::NavSample west;
    west.latDeg = 30.246;
    west.lonDeg = 120.210;
    west.heightM = 0;
    west.heightReference = VaporView::Geo::HeightReference::Wgs84Ellipsoid;
    auto east = west;
    east.lonDeg += 0.008;
    view.setSamples(std::vector<VaporView::Geo::NavSample>{west, east});
    require(view.flyToTrack(), QStringLiteral("camera focuses on the Hangzhou city centre"));
    processEventsFor(2000);
    const QImage background = view.grabFramebuffer();
    require(!background.isNull(), QStringLiteral("globe framebuffer is available"));
    view.setLayerVisible(Map3DLayer::Buildings3D, true);
    require(waitUntil([&] { return view.grabFramebuffer() != background; }, 30000),
            QStringLiteral("online building meshes appear in the visible street-level view"));
    view.setLayerVisible(Map3DLayer::Buildings3D, false);
    require(waitUntil([&] { return view.grabFramebuffer() == background; }, 5000),
            QStringLiteral("disabling buildings restores the original frame"));
}

void checkLiveTerrain(const QString& earth, bool blockCopernicus)
{
    // Only GDAL's remote file reader is affected. The local DEM and ion's Qt
    // HTTP transport remain available for the full elevation-stack test.
    if (blockCopernicus) qputenv("CPL_VSIL_CURL_ALLOWED_EXTENSIONS", ".disabled");
    const QUrl proxy(qEnvironmentVariable("HTTPS_PROXY"));
    if (!proxy.host().isEmpty())
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy, proxy.host(), proxy.port(8080)));
    auto scene = VaporView::Map3D::Detail::loadEarthAsset(earth);
    require(scene.mapNode && scene.diagnostics.cesiumTerrainConfigured, QStringLiteral("configured terrain stack"));
    auto* map = scene.mapNode->getMap();
    checkTerrainLevels(map);
    osgEarth::ElevationLayerVector layers;
    map->getLayers(layers);
    require(layers.size() >= 3 && layers.front()->getName() == "Cesium World Terrain"
                && layers[1]->getName() == "Online Copernicus GLO-30",
            QStringLiteral("sampling order is local, online Copernicus, then Cesium"));
    // An entire tile inside the Xihu DEM must resolve without contacting ion.
    const auto sample = [&](double lon, double lat, unsigned level) {
        const auto key = map->getProfile()->createTileKey(lon, lat, level);
        osg::ref_ptr<osg::HeightField> hf = new osg::HeightField;
        hf->allocate(65, 65);
        std::fill(hf->getHeightList().begin(), hf->getHeightList().end(), NO_DATA_VALUE);
        require(layers.populateHeightField(hf, nullptr, key, nullptr, osgEarth::INTERP_BILINEAR, nullptr),
                QStringLiteral("terrain tile has data"));
        return hf;
    };
    const float xihu = sample(120.07, 30.2, 12)->getHeight(32, 32);
    require(xihu > 0 && VaporView::Map3D::Detail::cesiumTerrainCredit(map, false).isEmpty(),
            QStringLiteral("local DEM used before ion authentication"));
    // This Himalayan tile straddles the boundary between two one-degree COGs.
    require(sample(86.925, 28.0, 8)->getHeight(32, 32) > 3000,
            QStringLiteral("Himalayan terrain begins at level 8"));
    const auto mountainTile = sample(86.925, 28.0, 12);
    const auto& heights = mountainTile->getHeightList();
    const float mountain = mountainTile->getHeight(32, 32);
    const bool usedCesium = !VaporView::Map3D::Detail::cesiumTerrainCredit(map, false).isEmpty();
    std::cout << "Local and online tile center heights: " << xihu << ", " << mountain
              << " m; tile minimum " << *std::min_element(heights.begin(), heights.end())
              << " m; Cesium used " << usedCesium << std::endl;
    require(*std::min_element(heights.begin(), heights.end()) > 4000 && usedCesium == blockCopernicus,
            blockCopernicus ? QStringLiteral("Cesium supplies terrain when online GLO-30 is unavailable")
                            : QStringLiteral("online GLO-30 supplies Himalayan terrain before Cesium"));
}

} // namespace

int main(int argc, char** argv)
{
    QTemporaryDir cacheDir;
    require(cacheDir.isValid(), QStringLiteral("isolated map cache directory"));
    const QByteArray mode = argc == 2 ? QByteArray(argv[1]) : QByteArray();
    const bool blockCopernicus = mode == "--cesium-live";
    const bool liveTerrain = blockCopernicus || mode == "--copernicus-live";
    if (mode != "--copernicus-live" || !qEnvironmentVariableIsSet("OSGEARTH_CACHE_PATH"))
        qputenv("OSGEARTH_CACHE_PATH", cacheDir.path().toUtf8());
    QApplication app(argc, argv);

    const QDir sourceRoot(QStringLiteral(VAPORVIEW_SOURCE_DIR));
    const QString earthPath =
        sourceRoot.filePath(QStringLiteral("resources/maps/vaporview_real3d_local.earth"));
    const QString tilesetPath =
        sourceRoot.filePath(QStringLiteral("resources/maps/tiles3d/local/tileset.json"));

    if (mode == "--buildings-live")
    {
        checkLiveBuildings(tilesetPath);
        return 0;
    }
    if (liveTerrain)
    {
        checkLiveTerrain(earthPath, blockCopernicus);
        return 0;
    }
    checkHttpCache();

    if (!QFileInfo::exists(earthPath) || !QFileInfo::exists(tilesetPath))
    {
        std::cout << "SKIP: Hangzhou Xihu real-3D local data is not installed\n";
        return 77;
    }

    const QString imageryEarth = sourceRoot.filePath(
        QStringLiteral("resources/maps/vaporview_with_sentinel2_imagery.earth"));
    const QString demVrt = sourceRoot.filePath(
        QStringLiteral("resources/maps/terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt"));
    if (QFileInfo::exists(imageryEarth) && QFileInfo::exists(demVrt))
    {
        auto asset = VaporView::Map3D::Detail::loadEarthAsset(imageryEarth);
        require(asset.mapNode && asset.mapNode->getMap()
                    && asset.diagnostics.openElevationLayerName == QStringLiteral("Copernicus DEM GLO-30"),
                QStringLiteral("local imagery opens the real DEM data source"));
        checkTerrainLevels(asset.mapNode->getMap());
        auto* pool = asset.mapNode->getMap()->getElevationPool();
        const auto* wgs84 = osgEarth::SpatialReference::get("wgs84");
        require(pool && wgs84, QStringLiteral("imagery scene exposes an elevation sampler"));
        int validSamples = 0;
        double minHeight = (std::numeric_limits<double>::max)();
        double maxHeight = (std::numeric_limits<double>::lowest)();
        for (double lat : {30.10, 30.13, 30.16, 30.20})
        {
            for (double lon : {120.02, 120.05, 120.08, 120.11})
            {
                const auto sample = pool->getSample(osgEarth::GeoPoint(wgs84, lon, lat), nullptr);
                if (!sample.hasData()) continue;
                const double height = sample.elevation().getValue();
                minHeight = (std::min)(minHeight, height);
                maxHeight = (std::max)(maxHeight, height);
                ++validSamples;
            }
        }
        std::cout << "Local imagery terrain: " << validSamples << " elevations, "
                  << minHeight << " to " << maxHeight << " m\n";
        require(validSamples >= 4 && maxHeight - minHeight > 10.0,
                QStringLiteral("local imagery scene returns varying real terrain heights"));
    }

    int presentedFrames = 0;
    int performanceUpdates = 0;
    VaporView::Map3D::OsgEarthViewWidget view;
    QObject::connect(&view, &QOpenGLWidget::frameSwapped, [&]() { ++presentedFrames; });
    QObject::connect(&view, &VaporView::Map3D::OsgEarthViewWidget::performanceUpdated,
                     [&]() { ++performanceUpdates; });
    view.resize(800, 600);
    view.show();
    require(waitUntil([&]() { return presentedFrames >= 3 && performanceUpdates > 0; }, 5000),
            QStringLiteral("presentation-driven rendering continues without user input"));
    require(view.performanceStats().frameIntervalP95Ms > 0.0,
            QStringLiteral("performance reports measured presentation intervals"));
    view.hide();
    processEventsFor(100);
    const int hiddenFrames = presentedFrames;
    processEventsFor(200);
    require(presentedFrames == hiddenFrames, QStringLiteral("hidden views stop presenting frames"));
    view.show();
    require(waitUntil([&]() { return presentedFrames >= hiddenFrames + 3; }, 5000),
            QStringLiteral("showing a hidden view restarts presentation scheduling"));

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
    require(view.earthCameraRangeM() > 10000000.0,
            QStringLiteral("opening the Xihu earth scene starts with the whole globe in view"));

    const VaporView::Map3D::EarthLoadDiagnostics earthDiagnostics =
        view.earthLoadDiagnostics();
    require(earthDiagnostics.loaded,
            QStringLiteral("earth diagnostics report successful load"));
    require(earthDiagnostics.foundMapNode,
            QStringLiteral("earth file contains an osgEarth MapNode"));
    require(earthDiagnostics.layerSummaries.join(QStringLiteral(" | "))
                .contains(QStringLiteral("可见区域优先的屏幕空间 LOD")),
            QStringLiteral("terrain prioritizes imagery for the visible camera region"));
    require(earthDiagnostics.layerSummaries.join(QStringLiteral(" | "))
                .contains(QStringLiteral("低视角自适应裁剪和细节级别")),
            QStringLiteral("earth camera keeps near-horizontal views responsive with adaptive distance/detail limits"));
    // No frame is rendered with the test key, so this checks configuration/reuse without network I/O.
    view.hide();
    require(view.applyTiandituSatelliteImagery(QStringLiteral("test-key-not-sent")),
            QStringLiteral("satellite layer opens with an explicit tile profile"));
    const auto satelliteDiagnostics = view.earthLoadDiagnostics();
    require(satelliteDiagnostics.layerSummaries.join(QStringLiteral(" | "))
                .contains(QStringLiteral("天地图源级别：1–")),
            QStringLiteral("satellite requests exclude the level-zero placeholder"));
    require(view.applyTiandituSatelliteImagery(QStringLiteral("test-key-not-sent"))
                && view.earthLoadDiagnostics().layerSummaries == satelliteDiagnostics.layerSummaries,
            QStringLiteral("reapplying identical satellite settings reuses the loaded layer"));
    view.applyTiandituSatelliteImagery(QString());
    view.show();

    require(view.layerAvailable(VaporView::Map3D::Map3DLayer::BaseMap),
            QStringLiteral("real scene exposes the base geography layer"));
    require(view.layerAvailable(VaporView::Map3D::Map3DLayer::SatelliteImagery),
            QStringLiteral("real scene exposes the satellite imagery layer"));
    require(view.layerAvailable(VaporView::Map3D::Map3DLayer::DigitalElevation),
            QStringLiteral("real scene exposes the DEM terrain layer"));
    require(view.layerAvailable(VaporView::Map3D::Map3DLayer::Hydrography),
            QStringLiteral("real scene exposes the hydrography layer"));
    require(view.layerAvailable(VaporView::Map3D::Map3DLayer::RoadNetwork),
            QStringLiteral("real scene exposes the road network layer"));
    require(!view.hasLocal3DTilesPreview() && view.layerAvailable(VaporView::Map3D::Map3DLayer::Buildings3D),
            QStringLiteral("built-in map offers online buildings before any local tileset is loaded"));
    view.setLayerVisible(VaporView::Map3D::Map3DLayer::Hydrography, false);
    require(!view.layerVisible(VaporView::Map3D::Map3DLayer::Hydrography),
            QStringLiteral("hydrography visibility can be disabled independently"));
    view.setLayerVisible(VaporView::Map3D::Map3DLayer::Hydrography, true);
    require(view.layerVisible(VaporView::Map3D::Map3DLayer::Hydrography),
            QStringLiteral("hydrography visibility can be restored independently"));

    std::vector<VaporView::Geo::TrajectoryRenderSample> heatTrack;
    for (int index = 0; index < 64; ++index)
    {
        VaporView::Geo::TrajectoryRenderSample sample;
        sample.navigation.latDeg = 30.25 + index * 0.00001;
        sample.navigation.lonDeg = 120.15 + index * 0.00001;
        sample.navigation.heightM = 25.0;
        sample.navigation.heightReference = VaporView::Geo::HeightReference::Wgs84Ellipsoid;
        sample.navigation.fixQuality = VaporView::Geo::FixQuality::Fixed;
        sample.heat.peak = static_cast<double>(index);
        heatTrack.push_back(sample);
    }
    view.setSamples(heatTrack);
    require(view.flyToTrack(), QStringLiteral("heat track can be focused"));
    require(view.loadEarthFile(earthPath), QStringLiteral("late Earth load succeeds after track installation"));
    require(view.earthCameraRangeM() < 10000.0,
            QStringLiteral("late Earth load focuses the existing track instead of resetting to the globe"));
    for (int step = 0; step < 12; ++step)
    {
        QWheelEvent wheel(QPointF(400, 300), QPointF(view.mapToGlobal(QPoint(400, 300))),
                          QPoint(), QPoint(0, step < 6 ? 120 : -120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&view, &wheel);
        processEventsFor(100);
    }
    view.clearTrack();

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
    require(view.flyToTrack(),
            QStringLiteral("selected-point smoke test centers the recorded ECEF sample"));
    processEventsFor(250);
    bool sampleSelected = false;
    bool selectionCleared = false;
    QObject::connect(&view,
                     &VaporView::Map3D::OsgEarthViewWidget::trajectorySampleSelected,
                     &view,
                     [&](int sampleIndex, const VaporView::Geo::NavSample&) {
                         sampleSelected = sampleIndex == 0;
                     });
    QObject::connect(&view,
                     &VaporView::Map3D::OsgEarthViewWidget::trajectorySampleSelectionCleared,
                     &view,
                     [&]() { selectionCleared = true; });
    const QPointF viewCenter(view.rect().center());
    const QPointF globalCenter(view.mapToGlobal(viewCenter.toPoint()));
    QMouseEvent pressEvent(QEvent::MouseButtonPress,
                           viewCenter,
                           globalCenter,
                           Qt::LeftButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &pressEvent);
    QMouseEvent releaseEvent(QEvent::MouseButtonRelease,
                             viewCenter,
                             globalCenter,
                             Qt::LeftButton,
                             Qt::NoButton,
                             Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &releaseEvent);
    processEventsFor(100);
    require(sampleSelected,
            QStringLiteral("clicking the centered trajectory point selects the sample"));
    QWidget* infoCard = view.findChild<QWidget*>(QStringLiteral("map3DTrajectoryInfoCard"));
    QLabel* infoDetails = view.findChild<QLabel*>(QStringLiteral("map3DTrajectoryInfoDetails"));
    require(infoCard && !infoCard->isHidden(),
            QStringLiteral("selected trajectory point opens its map information card"));
    require(infoCard->property("billboardMode").toString() == QStringLiteral("screen-space"),
            QStringLiteral("trajectory information card stays camera-facing in screen space"));
    require(infoDetails && infoDetails->text().contains(QStringLiteral("30.2500000")),
            QStringLiteral("trajectory information card includes detailed sample coordinates"));
    QKeyEvent escapeEvent(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &escapeEvent);
    processEventsFor(50);
    require(selectionCleared && infoCard->isHidden(),
            QStringLiteral("Escape clears the selected point and closes its information card"));

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
    require(heightStats.heightReferenceStatus.contains(QStringLiteral("WGS84 椭球高")),
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
    require(view.layerAvailable(VaporView::Map3D::Map3DLayer::Buildings3D),
            QStringLiteral("loaded native OSG buildings are exposed as a 3D building layer"));
    view.setLayerVisible(VaporView::Map3D::Map3DLayer::Buildings3D, false);
    require(!view.layerVisible(VaporView::Map3D::Map3DLayer::Buildings3D),
            QStringLiteral("3D building visibility can be disabled independently"));
    view.setLayerVisible(VaporView::Map3D::Map3DLayer::Buildings3D, true);

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
    // Match application shutdown: drain osgEarth workers before GDAL/PROJ
    // global teardown, which may otherwise race still-open map datasets.
    jobs::shutdown();
    require(jobs::get_metrics()->total_running() == 0,
            QStringLiteral("osgEarth workers stop before GDAL process teardown"));
    std::cout << "map3d_real_data_load_test passed: 55/55 building tiles loaded\n";
    return 0;
}
