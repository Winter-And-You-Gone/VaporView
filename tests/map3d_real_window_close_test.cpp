#include "map3d/Map3DWindow.h"
#include "map3d/OsgEarthViewWidget.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLocale>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <functional>
#include <iostream>

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

} // namespace

int main(int argc, char** argv)
{
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

    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), QStringLiteral("temporary settings directory"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir.path());

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("VaporViewMap3DRealWindowCloseTest"));
    app.setApplicationName(QStringLiteral("map3d_real_window_close_test"));

    auto* window = new VaporView::Map3D::Map3DWindow;
    window->resize(1100, 760);
    window->show();

    auto* view = window->findChild<VaporView::Map3D::OsgEarthViewWidget*>(QStringLiteral("map3DView"));
    require(view != nullptr, QStringLiteral("real OSG/osgEarth 3D view exists"));

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
    view->setMaxVisibleSamples(5000);

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
        << "record_timestamp_us,nav_lat_deg,nav_lon_deg,nav_height_m,ecef_x_m,ecef_y_m,ecef_z_m,gnss_fix\n";
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
                      << ",9.000000000,365504425.008990,13374370.950326,58160.200631,RTK_DUAL\n";
    }
    sessionStream.flush();
    devicesCsv.close();

    window->loadSessionDirectory(sessionDir.path());
    require(waitUntil([&]() {
                return view->sampleCount() == kSessionSampleCount;
            },
            30000),
            QStringLiteral("real window loads corrupt-ECEF session through LLH fallback"));
    require(view->hasEarthMap(),
            QStringLiteral("loading corrupt-ECEF session preserves the Earth map"));
    require(view->flyToTrack(),
            QStringLiteral("corrupt-ECEF session focuses with LLH before close"));
    processEventsFor(1500);
    require(hasVisibleMapSurface(view->grabFramebuffer()),
            QStringLiteral("session auto-focus keeps the Earth surface visible instead of a black route-only frame"));

    window->close();
    processEventsFor(250);
    require(!window->isVisible(), QStringLiteral("real map window closes after session load"));
    window->show();
    processEventsFor(250);
    require(window->isVisible()
                && view->hasEarthMap()
                && view->sampleCount() == kSessionSampleCount,
            QStringLiteral("reopened real map window keeps the Earth map and session track"));
    require(view->flyToTrack(),
            QStringLiteral("reopened corrupt-ECEF session still focuses with LLH"));
    window->close();
    processEventsFor(250);
    delete window;
    processEventsFor(500);

    std::cout << "map3d_real_window_close_test passed: real window opened, rendered, and closed\n";
    return 0;
}
