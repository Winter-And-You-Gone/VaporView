#include "test_settings_sandbox.h"
#include "map3d/Map3DWindow.h"
#include "map3d/OsgEarthViewWidget.h"

#include <QApplication>
#include <QCoreApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDialog>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLocale>
#include <QLabel>
#include <QMenu>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <future>
#include <iostream>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <imm.h>
#endif

namespace {

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

bool hasVisibleMapSurface(const QImage& frame)
{
    const QImage rgb = frame.convertToFormat(QImage::Format_RGB32);
    if (rgb.isNull())
    {
        return false;
    }

    qsizetype visiblePixelCount = 0;
    const qsizetype sampledPixelCount =
        static_cast<qsizetype>((rgb.width() + 3) / 4) * ((rgb.height() + 3) / 4);
    for (int y = 0; y < rgb.height(); y += 4)
    {
        const QRgb* row = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        for (int x = 0; x < rgb.width(); x += 4)
        {
            const QRgb pixel = row[x];
            if (qRed(pixel) > 24 && qGreen(pixel) > 24 && qBlue(pixel) > 24)
            {
                ++visiblePixelCount;
            }
        }
    }
    return visiblePixelCount * 2 > sampledPixelCount;
}

int countColor(const QImage& frame, const QColor& expected)
{
    const QImage rgb = frame.convertToFormat(QImage::Format_RGB32);
    int count = 0;
    for (int y = 0; y < rgb.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        for (int x = 0; x < rgb.width(); ++x)
            if (std::abs(qRed(row[x]) - expected.red()) < 28 &&
                std::abs(qGreen(row[x]) - expected.green()) < 28 &&
                std::abs(qBlue(row[x]) - expected.blue()) < 28)
                ++count;
    }
    return count;
}

void testCancelledAssets()
{
    auto *pool=QThreadPool::globalInstance();const int oldMaximum=pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    for(int asset=0;asset<3;++asset) {
        std::promise<void> started,release;auto released=release.get_future().share();
        auto blocker=QtConcurrent::run([&]{started.set_value();released.wait();});started.get_future().wait();
        VaporView::Map3D::OsgEarthViewWidget view(nullptr,true);int callbacks=0;
        const auto finished=[&](bool loaded){require(!loaded,QStringLiteral("cancelled asset cannot be applied"));++callbacks;};
        if(asset==0)view.loadEarthFileAsync(QStringLiteral("missing.earth"),finished);
        if(asset==1)view.loadLocal3DTilesPreviewAsync(QStringLiteral("missing-tileset.json"),finished);
        if(asset==2)view.loadAircraftModelAsync(QStringLiteral("missing.osgb"),finished);
        const auto watchers=view.findChildren<QFutureWatcherBase*>();
        require(watchers.size()==1,QStringLiteral("one queued asset watcher exists"));
        watchers.first()->cancel();release.set_value();blocker.waitForFinished();
        require(waitUntil([&]{return callbacks==1;},5000),QStringLiteral("cancelled asset returns failure without reading a nonexistent result"));
        view.shutdown();
    }
    {
        std::promise<void> started,release;auto released=release.get_future().share();
        auto blocker=QtConcurrent::run([&]{started.set_value();released.wait();});started.get_future().wait();
        int callbacks=0;auto *view=new VaporView::Map3D::OsgEarthViewWidget(nullptr,true);
        view->loadLocal3DTilesPreviewAsync(QStringLiteral("missing-tileset.json"),[&](bool){++callbacks;});
        view->shutdown();delete view;release.set_value();blocker.waitForFinished();processEventsFor(100);
        require(callbacks==0,QStringLiteral("destroying a view cancels queued work and disconnects all late callbacks"));
    }
    pool->setMaxThreadCount(oldMaximum);
}

} // namespace

int main(int argc, char** argv)
{
    const QDir sourceRoot(QStringLiteral(VAPORVIEW_SOURCE_DIR));
    const QString earthPath =
        sourceRoot.filePath(QStringLiteral("resources/maps/vaporview_real3d_local.earth"));
    const QString tilesetPath =
        sourceRoot.filePath(QStringLiteral("resources/maps/tiles3d/local/tileset.json"));
    const QString imageryEarthPath =
        sourceRoot.filePath(QStringLiteral("resources/maps/vaporview_with_sentinel2_imagery.earth"));
    const QString imageryVrtPath =
        sourceRoot.filePath(QStringLiteral("resources/maps/imagery/sentinel2/sentinel2.vrt"));
    const QString demVrtPath =
        sourceRoot.filePath(QStringLiteral("resources/maps/terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt"));

    const bool cancellationsOnly=argc>1 && QString::fromLocal8Bit(argv[1])==QStringLiteral("--cancelled-assets-only");
    if (!cancellationsOnly && (!QFileInfo::exists(earthPath) || !QFileInfo::exists(tilesetPath)))
    {
        std::cout << "SKIP: Hangzhou Xihu real-3D local data is not installed\n";
        return 77;
    }

    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), QStringLiteral("temporary settings directory"));
    qputenv("OSGEARTH_CACHE_PATH", settingsDir.filePath(QStringLiteral("map-cache")).toUtf8());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir.path());

#ifdef Q_OS_WIN
    // This rendering/lifecycle test never exercises text input. Disable the
    // current thread's IME before Qt creates any native windows, so third-party
    // input method hooks cannot affect the window teardown regression.
    require(ImmDisableIME(GetCurrentThreadId()) != FALSE,
            QStringLiteral("isolate rendering test from native input method hooks"));
#endif
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("VaporViewMap3DRealWindowCloseTest"));
    app.setApplicationName(QStringLiteral("map3d_real_window_close_test"));
    testCancelledAssets();
    if(cancellationsOnly)return 0;

    const bool hasImageryAndDem = QFileInfo::exists(imageryEarthPath)
        && QFileInfo::exists(imageryVrtPath) && QFileInfo::exists(demVrtPath);
    if (hasImageryAndDem)
    {
        QSettings mapSettings(QSettings::defaultFormat(), QSettings::UserScope,
                              QStringLiteral("VaporView"), QStringLiteral("Map3D"));
        mapSettings.setValue(QStringLiteral("lastEarthFile"), imageryEarthPath);
        mapSettings.sync();
    }

    auto* window = new VaporView::Map3D::Map3DWindow;
    window->resize(1100, 760);
    window->show();


    VaporView::Map3D::OsgEarthViewWidget* view = nullptr;
    const bool startupLoaded = waitUntil([&]() {
        view = window->findChild<VaporView::Map3D::OsgEarthViewWidget*>(QStringLiteral("map3DView"));
        return view && view->hasEarthMap();
    }, 10000);
    require(view != nullptr, QStringLiteral("real OSG/osgEarth 3D view exists"));
    const auto startupDiagnostics = view->earthLoadDiagnostics();
    require(startupLoaded,
            QStringLiteral("lightweight startup map loads before selecting local detail: %1: %2")
                .arg(startupDiagnostics.requestedPath, startupDiagnostics.failureReason));
    if (hasImageryAndDem)
    {
        require(QFileInfo(startupDiagnostics.requestedPath) == QFileInfo(imageryEarthPath)
                    && view->layerAvailable(VaporView::Map3D::Map3DLayer::DigitalElevation)
                    && startupDiagnostics.openElevationLayerName == QStringLiteral("Copernicus DEM GLO-30"),
                QStringLiteral("restoring a local imagery scene also loads the available DEM"));
        auto* status = window->findChild<QLabel*>(QStringLiteral("map3DStatusLabel"));
        require(status && status->text().contains(QStringLiteral("Copernicus DEM GLO-30")),
                QStringLiteral("the displayed map status reports the loaded imagery terrain"));
    }
    auto* displayAction = window->findChild<QAction*>(QStringLiteral("map3DDisplayAction"));
    require(displayAction != nullptr, QStringLiteral("display settings action exists"));
    displayAction->trigger();
    auto* displayPanel = window->findChild<QDialog*>(QStringLiteral("map3DDisplayPanel"));
    require(displayPanel != nullptr, QStringLiteral("display settings panel exists"));
    auto* lineCheck = displayPanel->findChild<QCheckBox*>(QStringLiteral("map3DTrackLineVisibleCheckBox"));
    auto* pointCheck = displayPanel->findChild<QCheckBox*>(QStringLiteral("map3DTrackPointsVisibleCheckBox"));
    auto* metricCombo = displayPanel->findChild<QComboBox*>(QStringLiteral("map3DHeatMetricCombo"));
    auto* paletteCombo = displayPanel->findChild<QComboBox*>(QStringLiteral("map3DHeatPaletteCombo"));
    auto* samplesSpin = displayPanel->findChild<QSpinBox*>(QStringLiteral("map3DMaxVisibleSamplesSpin"));
    auto* lineSpin = displayPanel->findChild<QSpinBox*>(QStringLiteral("map3DTrackLineWidthSpin"));
    auto* pointSpin = displayPanel->findChild<QSpinBox*>(QStringLiteral("map3DTrackPointSizeSpin"));
    auto* refreshStatus = displayPanel->findChild<QLabel*>(QStringLiteral("map3DDisplayRefreshStatus"));
    require(lineCheck && pointCheck && metricCombo && paletteCombo && samplesSpin && lineSpin && pointSpin && refreshStatus,
            QStringLiteral("display settings controls are present"));
    const bool initialLine = lineCheck->isChecked(), initialPoints = pointCheck->isChecked();
    const int initialMetric = metricCombo->currentIndex(), initialPalette = paletteCombo->currentIndex();
    const int initialSamples = samplesSpin->value(), initialLineWidth = lineSpin->value(), initialPointSize = pointSpin->value();
    lineCheck->setChecked(!initialLine);
    pointCheck->setChecked(!initialPoints);
    metricCombo->setCurrentIndex(1);
    paletteCombo->setCurrentIndex(2);
    samplesSpin->setValue(10000);
    lineSpin->setValue(9);
    pointSpin->setValue(96);
    require(view->trackLineVisible() == !initialLine && view->trackPointsVisible() == !initialPoints &&
                view->heatMetric() == VaporView::Geo::HeatMetric::Humidity &&
                view->heatPalette() == VaporView::Geo::HeatPalette::SpectralReverse &&
                view->maxVisibleSamples() == 10000 && view->trackLineWidth() == 9 && view->trackPointSize() == 96 &&
                refreshStatus->text() == QStringLiteral("已刷新"),
            QStringLiteral("display settings immediately update the real map view and report the refresh"));
    lineCheck->setChecked(initialLine);
    pointCheck->setChecked(initialPoints);
    metricCombo->setCurrentIndex(initialMetric);
    paletteCombo->setCurrentIndex(initialPalette);
    samplesSpin->setValue(initialSamples);
    lineSpin->setValue(initialLineWidth);
    pointSpin->setValue(initialPointSize);
    displayPanel->hide();
    VaporView::Geo::NavSample pointProbe;
    pointProbe.latDeg = 30.25;
    pointProbe.lonDeg = 120.15;
    pointProbe.heightM = 200.0;
    pointProbe.heightReference = VaporView::Geo::HeightReference::Wgs84Ellipsoid;
    pointProbe.recordTimestampUs = 1000000;
    pointProbe.fixQuality = VaporView::Geo::FixQuality::Fixed;
    pointProbe.satellites = 12;
    pointProbe.hdop = 0.9;
    auto pointProbe2 = pointProbe;
    pointProbe2.lonDeg += 0.002;
    pointProbe2.recordTimestampUs += 10000000;
    view->setTrackLineVisible(false);
    view->setTrackPointsVisible(true);
    view->setSamples(std::vector<VaporView::Geo::NavSample>{pointProbe, pointProbe2});
    require(view->flyToTrack(), QStringLiteral("focus on the ordinary trajectory point"));
    processEventsFor(3000);
    pointSpin->setValue(7);
    processEventsFor(400);
    const QImage smallPointFrame = view->grabFramebuffer();
    pointSpin->setValue(96);
    processEventsFor(400);
    const QImage largePointFrame = view->grabFramebuffer();
    const int smallGreen = countColor(smallPointFrame, QColor(13, 255, 89));
    const int largeGreen = countColor(largePointFrame, QColor(13, 255, 89));
    require(largeGreen > smallGreen + 1000,
            QStringLiteral("ordinary trajectory pixels visibly grow when the point size increases"));
    const auto initialHeatMetric = view->heatMetric();
    const auto initialHeatPalette = view->heatPalette();
    view->setHeatMetric(VaporView::Geo::HeatMetric::Peak);
    view->setHeatPalette(VaporView::Geo::HeatPalette::BlueRedFast);
    VaporView::Geo::TrajectoryRenderSample heatProbe;
    heatProbe.navigation = pointProbe;
    heatProbe.heat.peak = 5.0;
    auto heatProbe2 = heatProbe;
    heatProbe2.navigation = pointProbe2;
    pointSpin->setValue(7);
    view->setSamples(std::vector<VaporView::Geo::TrajectoryRenderSample>{heatProbe, heatProbe2});
    processEventsFor(500);
    const QImage smallHeatFrame = view->grabFramebuffer();
    pointSpin->setValue(96);
    processEventsFor(500);
    const QImage largeHeatFrame = view->grabFramebuffer();
    const int smallHeat = countColor(smallHeatFrame, QColor(66, 255, 56));
    const int largeHeat = countColor(largeHeatFrame, QColor(66, 255, 56));
    require(largeHeat > smallHeat + 1000,
            QStringLiteral("heat trajectory pixels visibly grow when the point size increases"));
    view->setHeatMetric(initialHeatMetric);
    view->setHeatPalette(initialHeatPalette);
    view->clearTrack();
    view->setTrackLineVisible(initialLine);
    view->setTrackPointsVisible(initialPoints);
    pointSpin->setValue(initialPointSize);
    require(!view->hasLocal3DTilesPreview(), QStringLiteral("buildings are not loaded at startup"));
    auto* buildings = window->findChild<QAction*>(QStringLiteral("map3DLayer_buildings3D"));
    require(buildings && !buildings->isChecked(), QStringLiteral("building layer starts disabled"));
    buildings->trigger();
    auto* reload = window->findChild<QAction*>(QStringLiteral("map3DReloadBestMapAction"));
    require(reload != nullptr, QStringLiteral("local map reload action exists"));
    reload->trigger();

    bool loaded = false;
    const qint64 loadDeadline = QDateTime::currentMSecsSinceEpoch() + 20000;
    while (QDateTime::currentMSecsSinceEpoch() < loadDeadline)
    {
        processEventsFor(100);
        const VaporView::Map3D::Local3DTilesLoadDiagnostics tileDiagnostics =
            view->local3DTilesLoadDiagnostics();
        if (view->hasEarthMap() && tileDiagnostics.loaded)
        {
            loaded = true;
            break;
        }
    }

    require(loaded, QStringLiteral("real window loads the Xihu earth map and building overlay"));
    const VaporView::Map3D::Local3DTilesLoadDiagnostics tileDiagnostics =
        view->local3DTilesLoadDiagnostics();
    require(tileDiagnostics.loadedPayloadCount == 55,
            QStringLiteral("real window loads all 55 building payloads, got %1")
                .arg(tileDiagnostics.loadedPayloadCount));
    if (hasImageryAndDem)
    {
        auto* imagery = window->findChild<QAction*>(QStringLiteral("map3DLocalImagery_sentinel2"));
        auto* online = window->findChild<QAction*>(QStringLiteral("map3DOnlineImagery_tianditu"));
        auto* openAerialMap = window->findChild<QAction*>(QStringLiteral("map3DOnlineImagery_openaerialmap"));
        auto* esriImagery = window->findChild<QAction*>(QStringLiteral("map3DOnlineImagery_esri"));
        auto* imageryMenu = window->findChild<QMenu*>(QStringLiteral("map3DLocalImageryMenu"));
        require(imagery && imagery->isEnabled(), QStringLiteral("local Sentinel-2 action is available"));
        require(online && imageryMenu && !online->isEnabled(), QStringLiteral("online imagery initially needs a key"));
        require(openAerialMap && openAerialMap->isEnabled(), QStringLiteral("OpenAerialMap needs no key"));
        require(esriImagery && !esriImagery->isEnabled(), QStringLiteral("Esri imagery needs a key"));
        view->setSamples(std::vector<VaporView::Geo::NavSample>{pointProbe, pointProbe2});
        const QString previousEarth = view->earthLoadDiagnostics().requestedPath;
        const QString previousDem = view->earthLoadDiagnostics().openElevationLayerName;
        const int previousSamples = view->sampleCount();
        const double previousRange = view->earthCameraRangeM();
        view->hide(); // Keep the fake test key away from network requests.
        openAerialMap->trigger();
        require(view->activeSatelliteImagerySource() == QStringLiteral("online:openaerialmap")
                    && openAerialMap->isChecked(), QStringLiteral("OpenAerialMap can be selected without a key"));
        {
            QSettings mapSettings(QSettings::defaultFormat(), QSettings::UserScope,
                                  QStringLiteral("VaporView"), QStringLiteral("Map3D"));
            require(mapSettings.value(QStringLiteral("satelliteImagerySource")).toString()
                        == QStringLiteral("online:openaerialmap"),
                    QStringLiteral("the OpenAerialMap choice is persisted"));
        }
        {
            QSettings mapSettings(QSettings::defaultFormat(), QSettings::UserScope,
                                  QStringLiteral("VaporView"), QStringLiteral("Map3D"));
            mapSettings.setValue(QStringLiteral("esriApiKey"), QStringLiteral("test-key-not-sent"));
            mapSettings.sync();
        }
        QMetaObject::invokeMethod(imageryMenu, "aboutToShow", Qt::DirectConnection);
        require(esriImagery->isEnabled(), QStringLiteral("Esri becomes selectable when a key is configured"));
        esriImagery->trigger();
        require(view->activeSatelliteImagerySource() == QStringLiteral("online:esri")
                    && esriImagery->isChecked() && !openAerialMap->isChecked(),
                QStringLiteral("selecting Esri replaces OpenAerialMap"));
        auto* mapStatus = window->findChild<QLabel*>(QStringLiteral("map3DStatusLabel"));
        require(mapStatus && mapStatus->text().startsWith(QStringLiteral("Esri, Vantor, Earthstar Geographics")),
                QStringLiteral("Esri attribution is shown in the map status"));
        {
            QSettings mapSettings(QSettings::defaultFormat(), QSettings::UserScope,
                                  QStringLiteral("VaporView"), QStringLiteral("Map3D"));
            require(mapSettings.value(QStringLiteral("satelliteImagerySource")).toString()
                        == QStringLiteral("online:esri"),
                    QStringLiteral("the Esri choice is persisted"));
        }
        {
            QSettings config(qEnvironmentVariable("VAPORVIEW_CONFIG_FILE"), QSettings::IniFormat);
            config.setValue(QStringLiteral("TrajectoryViewer/map/tianditu_key"), QStringLiteral("test-key-not-sent"));
            config.sync();
        }
        QMetaObject::invokeMethod(imageryMenu, "aboutToShow", Qt::DirectConnection);
        require(online->isEnabled(), QStringLiteral("online option becomes available when a key is configured"));
        online->trigger();
        require(view->activeSatelliteImagerySource() == QStringLiteral("online:tianditu") && online->isChecked()
                    && !openAerialMap->isChecked() && !esriImagery->isChecked(),
                QStringLiteral("selecting online activates the Tianditu layer"));
        require(!mapStatus->text().startsWith(QStringLiteral("Esri, Vantor")),
                QStringLiteral("Esri attribution is removed when another source is selected"));
        imagery->trigger();
        require(view->activeSatelliteImagerySource().startsWith(QStringLiteral("local:")) && imagery->isChecked()
                    && !online->isChecked(), QStringLiteral("selecting local imagery replaces online imagery"));
        require(QFileInfo(view->earthLoadDiagnostics().requestedPath) == QFileInfo(previousEarth)
                    && view->sampleCount() == previousSamples
                    && std::abs(view->earthCameraRangeM() - previousRange) < 1.0,
                QStringLiteral("switching imagery preserves the scene, track and camera"));
        require(previousDem.contains(QStringLiteral("Copernicus DEM GLO-30"))
                    && view->earthLoadDiagnostics().openElevationLayerName == previousDem
                    && view->layerAvailable(VaporView::Map3D::Map3DLayer::DigitalElevation)
                    && view->layerVisible(VaporView::Map3D::Map3DLayer::DigitalElevation),
                QStringLiteral("switching imagery keeps the actual DEM open and visible"));
        QSettings mapSettings(QSettings::defaultFormat(), QSettings::UserScope,
                              QStringLiteral("VaporView"), QStringLiteral("Map3D"));
        require(mapSettings.value(QStringLiteral("satelliteImagerySource")).toString()
                    == QStringLiteral("local:sentinel2"), QStringLiteral("selected source is persisted"));
        {
            QSettings config(qEnvironmentVariable("VAPORVIEW_CONFIG_FILE"), QSettings::IniFormat);
            config.remove(QStringLiteral("TrajectoryViewer/map/tianditu_key"));
            config.sync();
        }
        view->show();
        view->clearTrack();
    }
    view->setMaxVisibleSamples(10000);

    QTemporaryDir failedLoadDir;
    require(failedLoadDir.isValid(), QStringLiteral("temporary failed-load directory"));
    const QString invalidEarthPath = QDir(failedLoadDir.path()).filePath(QStringLiteral("not-an-earth.osg"));
    QFile invalidEarth(invalidEarthPath);
    require(invalidEarth.open(QIODevice::WriteOnly | QIODevice::Text),
            QStringLiteral("create non-MapNode OSG file"));
    invalidEarth.write("Group { }\n");
    invalidEarth.close();
    require(!view->loadEarthFile(invalidEarthPath),
            QStringLiteral("non-MapNode Earth candidate is rejected"));
    require(view->hasEarthMap(),
            QStringLiteral("failed Earth candidate preserves the active Earth scene"));

    const QString incompleteTilesetPath =
        QDir(failedLoadDir.path()).filePath(QStringLiteral("tileset.json"));
    QFile incompleteTileset(incompleteTilesetPath);
    require(incompleteTileset.open(QIODevice::WriteOnly | QIODevice::Text),
            QStringLiteral("create incomplete native building tileset"));
    incompleteTileset.write(R"JSON({
  "asset": {"version": "1.1"},
  "extras": {"format": "vaporview-osg-native-building-tiles"},
  "geometricError": 1,
  "root": {
    "boundingVolume": {"region": [0, 0, 0.1, 0.1, 0, 10]},
    "content": {"uri": "missing.osgb"}
  }
})JSON");
    incompleteTileset.close();
    require(!view->loadLocal3DTilesPreview(incompleteTilesetPath),
            QStringLiteral("incomplete native building tileset is rejected atomically"));
    require(view->hasLocal3DTilesPreview(),
            QStringLiteral("failed building tileset preserves the active building overlay"));

    QTemporaryDir sessionDir;
    require(sessionDir.isValid(), QStringLiteral("temporary corrupt-ECEF session directory"));
    require(QDir(sessionDir.path()).mkpath(QStringLiteral("sensors")),
            QStringLiteral("create corrupt-ECEF session sensors directory"));
    QFile devicesCsv(QDir(sessionDir.path()).filePath(QStringLiteral("sensors/devices.csv")));
    require(devicesCsv.open(QIODevice::WriteOnly | QIODevice::Text),
            QStringLiteral("create corrupt-ECEF session CSV"));
    QTextStream sessionStream(&devicesCsv);
    sessionStream.setLocale(QLocale::c());
    sessionStream.setRealNumberNotation(QTextStream::FixedNotation);
    sessionStream.setRealNumberPrecision(9);
    sessionStream
        << "record_timestamp_us,nav_lat_deg,nav_lon_deg,nav_height_m,ecef_x_m,ecef_y_m,ecef_z_m,gnss_fix,hmp_humidity_rh\n";
    constexpr int kSessionSampleCount = 57911;
    const std::array<double, 5> latitudes = {
        30.130669989, 30.124458407, 30.128837606, 30.132797391, 30.130669989};
    const std::array<double, 5> longitudes = {
        120.074031181, 120.076469197, 120.080873490, 120.076249142, 120.074031181};
    for (int i = 0; i < kSessionSampleCount; ++i)
    {
        const double routePosition =
            static_cast<double>(i) * 4.0 / static_cast<double>(kSessionSampleCount - 1);
        const int segment = (std::min)(3, static_cast<int>(routePosition));
        const double fraction = routePosition - static_cast<double>(segment);
        const double latitude =
            latitudes[segment] + (latitudes[segment + 1] - latitudes[segment]) * fraction;
        const double longitude =
            longitudes[segment] + (longitudes[segment + 1] - longitudes[segment]) * fraction;
        sessionStream << (i + 1) * 1000LL << ',' << latitude << ',' << longitude
                      << ",9.000000000,365504425.008990,13374370.950326,58160.200631,RTK_DUAL,"
                      << 40.0 + (i % 100) * 0.1 << '\n';
    }
    sessionStream.flush();
    devicesCsv.close();

    VaporView::Geo::NavSample liveSample;
    liveSample.latDeg = 30.13;
    liveSample.lonDeg = 120.075;
    liveSample.heightM = 50.0;
    liveSample.recordTimestampUs = 1000000;
    window->appendSample(liveSample);
    QTimer liveTimer;
    QObject::connect(&liveTimer, &QTimer::timeout, window, [&]() {
        liveSample.recordTimestampUs += 50000;
        window->appendSamples({liveSample});
    });
    liveTimer.start(50);
    metricCombo->setCurrentIndex(1); // Exercise heat geometry as well as the full Session track.
    QElapsedTimer guiPulseClock;
    guiPulseClock.start();
    qint64 longestGuiGapMs = 0;
    int guiPulses = 0;
    QTimer guiPulse;
    QObject::connect(&guiPulse, &QTimer::timeout, window, [&]() {
        longestGuiGapMs = (std::max)(longestGuiGapMs, guiPulseClock.restart());
        ++guiPulses;
    });
    guiPulse.start(50);
    window->loadSessionDirectory(sessionDir.path());
    require(waitUntil([&]() {
                return view->sampleCount() == kSessionSampleCount;
            },
            30000),
            QStringLiteral("real window loads corrupt-ECEF session through LLH fallback"));
    longestGuiGapMs = (std::max)(longestGuiGapMs, guiPulseClock.elapsed());
    guiPulse.stop();
    std::cout << "Session load: " << guiPulses << " GUI pulses; longest gap "
              << longestGuiGapMs << " ms\n";
    require(guiPulses > 0 && longestGuiGapMs < 5000,
            QStringLiteral("large Session loading keeps the GUI responsive"));
    require(view->heatRange().valid,
            QStringLiteral("large Session retains its humidity heat data"));
    require(view->hasEarthMap(),
            QStringLiteral("loading corrupt-ECEF session preserves the Earth map"));
    require(view->earthCameraRangeM() < 20000.0,
            QStringLiteral("corrupt-ECEF session automatically focuses with LLH before close"));
    processEventsFor(1500);
    require(hasVisibleMapSurface(view->grabFramebuffer()),
            QStringLiteral("session auto-focus keeps the Earth surface visible instead of a black route-only frame"));

    auto* modeButton = window->findChild<QToolButton*>(QStringLiteral("map3DModeButton"));
    require(modeButton != nullptr, QStringLiteral("real map provides a title-bar mode switch"));
    modeButton->click();
    require(view->sampleCount() > 0 && view->sampleCount() < kSessionSampleCount,
            QStringLiteral("live map restores only live positions, without Session samples"));
    processEventsFor(200);
    modeButton->click();
    require(view->sampleCount() == kSessionSampleCount,
            QStringLiteral("Session track survives ongoing live input and mode switches"));
    liveTimer.stop();

    window->close();
    processEventsFor(250);
    require(!window->isVisible(), QStringLiteral("real map window closes after session load"));
    window->show();
    processEventsFor(250);
    require(window->isVisible()
                && view->hasEarthMap()
                && view->sampleCount() < kSessionSampleCount
                && !modeButton->isEnabled(),
            QStringLiteral("reopened real map keeps the Earth map and releases the Session cache"));
    require(view->flyToTrack(),
            QStringLiteral("reopened live track still focuses with LLH"));
    window->close();
    processEventsFor(250);
    delete window;
    processEventsFor(500);

    window = new VaporView::Map3D::Map3DWindow;
    window->resize(1100, 760);
    window->show();
    window->loadSessionDirectory(sessionDir.path());
    view = window->findChild<VaporView::Map3D::OsgEarthViewWidget*>(QStringLiteral("map3DView"));
    require(view != nullptr, QStringLiteral("opening a session starts the renderer automatically"));
    require(waitUntil([&]() {
                return view->hasEarthMap() && view->sampleCount() == kSessionSampleCount;
            }, 30000),
            QStringLiteral("cold session open loads both Earth and track"));
    if (hasImageryAndDem)
    {
        require(waitUntil([&]() {
                    return view->activeSatelliteImagerySource().startsWith(QStringLiteral("local:"));
                }, 10000), QStringLiteral("the selected local source survives a cold Session open"));
        require(view->layerAvailable(VaporView::Map3D::Map3DLayer::DigitalElevation)
                    && view->layerVisible(VaporView::Map3D::Map3DLayer::DigitalElevation),
                QStringLiteral("the restored imagery keeps the DEM visible"));
    }
    processEventsFor(1500);
    require(view->earthCameraRangeM() < 20000.0,
            QStringLiteral("cold session open automatically focuses after both loads finish"));
    require(hasVisibleMapSurface(view->grabFramebuffer()),
            QStringLiteral("cold session open renders the Earth surface"));
    if (hasImageryAndDem)
    {
        view->hide(); // Keep the reload deterministic without relying on the network.
        auto* oam = window->findChild<QAction*>(QStringLiteral("map3DOnlineImagery_openaerialmap"));
        auto* esri = window->findChild<QAction*>(QStringLiteral("map3DOnlineImagery_esri"));
        auto* reloadMap = window->findChild<QAction*>(QStringLiteral("map3DReloadBestMapAction"));
        require(oam && esri && reloadMap, QStringLiteral("online imagery and map reload actions are available"));
        oam->trigger();
        reloadMap->trigger();
        require(waitUntil([&]() {
                    return view->earthLoadDiagnostics().loaded
                        && view->activeSatelliteImagerySource() == QStringLiteral("online:openaerialmap");
                }, 30000), QStringLiteral("map reload restores the selected OpenAerialMap source"));
        require(view->sampleCount() == kSessionSampleCount
                    && view->layerAvailable(VaporView::Map3D::Map3DLayer::DigitalElevation)
                    && view->layerVisible(VaporView::Map3D::Map3DLayer::DigitalElevation),
                QStringLiteral("OpenAerialMap reload preserves the Session track and visible terrain"));
        esri->trigger();
        reloadMap->trigger();
        require(waitUntil([&]() {
                    return view->earthLoadDiagnostics().loaded
                        && view->activeSatelliteImagerySource() == QStringLiteral("online:esri");
                }, 30000), QStringLiteral("map reload restores the selected Esri source"));
        require(view->sampleCount() == kSessionSampleCount
                    && view->layerAvailable(VaporView::Map3D::Map3DLayer::DigitalElevation)
                    && view->layerVisible(VaporView::Map3D::Map3DLayer::DigitalElevation),
                QStringLiteral("Esri reload preserves the Session track and visible terrain"));
    }
    window->close();
    delete window;
    processEventsFor(500);

    if (hasImageryAndDem)
    {
        VaporView::Map3D::Map3DWindow restored;
        const auto* esri = restored.findChild<QAction*>(QStringLiteral("map3DOnlineImagery_esri"));
        require(esri && esri->isChecked(), QStringLiteral("a new window remembers the Esri choice"));
        QSettings mapSettings(QSettings::defaultFormat(), QSettings::UserScope,
                              QStringLiteral("VaporView"), QStringLiteral("Map3D"));
        mapSettings.remove(QStringLiteral("esriApiKey"));
        mapSettings.sync();
    }

    std::cout << "map3d_real_window_close_test passed: real window opened, rendered, and closed\n";
    return 0;
}
