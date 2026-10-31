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
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QThread>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <functional>
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

    if (!QFileInfo::exists(earthPath) || !QFileInfo::exists(tilesetPath))
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


    auto* view = window->findChild<VaporView::Map3D::OsgEarthViewWidget*>(QStringLiteral("map3DView"));
    require(view != nullptr, QStringLiteral("real OSG/osgEarth 3D view exists"));
    const bool startupLoaded = waitUntil([&]() { return view->hasEarthMap(); }, 10000);
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
        require(imagery && imagery->isEnabled(), QStringLiteral("local Sentinel-2 action is available"));
        imagery->trigger();
        require(waitUntil([&]() {
                    const auto diagnostics = view->earthLoadDiagnostics();
                    return diagnostics.loaded && QFileInfo(diagnostics.requestedPath) == QFileInfo(imageryEarthPath);
                }, 30000),
                QStringLiteral("switching to local imagery completes"));
        require(view->earthLoadDiagnostics().openElevationLayerName == QStringLiteral("Copernicus DEM GLO-30")
                    && view->layerVisible(VaporView::Map3D::Map3DLayer::DigitalElevation),
                QStringLiteral("switching imagery keeps the actual DEM open and visible"));
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
    processEventsFor(1500);
    require(view->earthCameraRangeM() < 20000.0,
            QStringLiteral("cold session open automatically focuses after both loads finish"));
    require(hasVisibleMapSurface(view->grabFramebuffer()),
            QStringLiteral("cold session open renders the Earth surface"));
    window->close();
    delete window;
    processEventsFor(500);

    std::cout << "map3d_real_window_close_test passed: real window opened, rendered, and closed\n";
    return 0;
}
