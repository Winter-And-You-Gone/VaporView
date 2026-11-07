#include "test_settings_sandbox.h"
#include "ground/main/MainWindow.h"
#include "ground/map/Map3DController.h"
#include "map3d/OsgEarthViewWidget.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QWidget>

#include <osgEarth/Threading>

#include <cstdlib>
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

QAction* findActionByText(QWidget* root, const QStringList& expectedTexts)
{
    const QList<QAction*> actions = root->findChildren<QAction*>();
    for (QAction* action : actions)
    {
        if (action && expectedTexts.contains(action->text()))
        {
            return action;
        }
    }
    return nullptr;
}

QWidget* findMap3DWindow()
{
    const QWidgetList topLevelWidgets = QApplication::topLevelWidgets();
    for (QWidget* widget : topLevelWidgets)
    {
        if (widget && widget->objectName() == QStringLiteral("map3DWindow"))
        {
            return widget;
        }
    }
    return nullptr;
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
    qputenv("OSGEARTH_CACHE_PATH", settingsDir.filePath(QStringLiteral("map-cache")).toUtf8());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir.path());

    QElapsedTimer lifecycleClock;
    lifecycleClock.start();
    struct ApplicationExitMarker
    {
        QElapsedTimer* clock;
        ~ApplicationExitMarker()
        {
            std::cout << "MAPCLOSE_PHASE elapsed_ms=" << clock->elapsed()
                      << " phase=application_destructor_end" << std::endl;
        }
    } applicationExitMarker{&lifecycleClock};

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("VaporViewMainWindowMap3DRealCloseTest"));
    app.setApplicationName(QStringLiteral("main_window_map3d_real_close_test"));

    const auto mark = [&](const char* phase, const QString& detail = QString()) {
        std::cout << "MAPCLOSE_PHASE elapsed_ms=" << lifecycleClock.elapsed()
                  << " pid=" << QApplication::applicationPid()
                  << " tid=0x" << std::hex << reinterpret_cast<quintptr>(QThread::currentThreadId())
                  << std::dec << " phase=" << phase;
        if (!detail.isEmpty()) std::cout << " " << detail.toStdString();
        std::cout << std::endl;
    };

    mark("main_window_create_begin");
    auto* window = new MainWindow;
    mark("main_window_create_end");
    window->resize(1000, 700);
    window->show();
    mark("main_window_shown");
    processEventsFor(250);

    const QString scenario = qEnvironmentVariable("VAPORVIEW_MAPCLOSE_SCENARIO", "loaded");
    mark("scenario", scenario);
    const auto finish = [&]() {
        mark("main_window_close_begin");
        window->close();
        require(!window->isVisible(), QStringLiteral("MainWindow accepts close"));
        mark("main_window_close_end");
        processEventsFor(250);
        mark("main_window_delete_begin");
        delete window;
        mark("main_window_delete_end");
        processEventsFor(500);
        mark("osgearth_jobs_shutdown_begin");
        VaporView::Ground::Map3DController::shutdownBackgroundJobs();
        mark("osgearth_jobs_shutdown_end");
        require(jobs::get_metrics()->total_running() == 0,
                QStringLiteral("osgEarth jobs are joined before GDAL process teardown"));
        mark("test_passed");
        return 0;
    };
    if (scenario == QStringLiteral("no-map")) return finish();

    QAction* mapAction = findActionByText(window,
                                          {QStringLiteral("三维地图"),
                                           QStringLiteral("3D Map")});
    require(mapAction != nullptr, QStringLiteral("3D map action exists"));
    mapAction->trigger();
    mark("map_action_triggered");
    if (scenario == QStringLiteral("opening")) return finish();
    processEventsFor(250);

    QWidget* mapWindow = findMap3DWindow();
    mark("map_window_lookup", QStringLiteral("found=%1").arg(mapWindow != nullptr));
    require(mapWindow != nullptr && mapWindow->isVisible(), QStringLiteral("3D map window opens from MainWindow"));


    const qint64 startupDeadline = QDateTime::currentMSecsSinceEpoch() + 10000;
    auto* view = mapWindow->findChild<VaporView::Map3D::OsgEarthViewWidget*>(QStringLiteral("map3DView"));
    // The placeholder paint queues renderer creation; wait for that real
    // readiness condition within the existing startup deadline.
    while (!view && QDateTime::currentMSecsSinceEpoch() < startupDeadline)
    {
        processEventsFor(20);
        view = mapWindow->findChild<VaporView::Map3D::OsgEarthViewWidget*>(QStringLiteral("map3DView"));
    }
    mark("map_view_lookup", QStringLiteral("found=%1").arg(view != nullptr));
    require(view != nullptr, QStringLiteral("real OSG/osgEarth 3D view exists in MainWindow path"));
    if (scenario == QStringLiteral("loading"))
    {
        view->loadEarthFileAsync(earthPath, [](bool) {});
        require(view->earthLoadDiagnostics().attempted && !view->earthLoadDiagnostics().loaded,
                QStringLiteral("Earth load is pending when MainWindow closes"));
        return finish();
    }
    while (!view->hasEarthMap() && QDateTime::currentMSecsSinceEpoch() < startupDeadline)
        processEventsFor(20);
    require(view->hasEarthMap(), QStringLiteral("lightweight startup map loads before selecting local detail"));
    mark("startup_earth_loaded");
    auto* buildings = mapWindow->findChild<QAction*>(QStringLiteral("map3DLayer_buildings3D"));
    require(buildings && !buildings->isChecked(), QStringLiteral("building layer starts disabled"));
    buildings->trigger();
    mark("buildings_enabled");
    auto* reload = mapWindow->findChild<QAction*>(QStringLiteral("map3DReloadBestMapAction"));
    require(reload != nullptr, QStringLiteral("local map reload action exists"));
    reload->trigger();
    mark("local_detail_reload_triggered");
    if (scenario == QStringLiteral("async")) return finish();

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

    require(loaded, QStringLiteral("MainWindow path loads the Xihu earth map and building overlay"));
    const VaporView::Map3D::Local3DTilesLoadDiagnostics tileDiagnostics =
        view->local3DTilesLoadDiagnostics();
    require(tileDiagnostics.loadedPayloadCount == 55,
            QStringLiteral("MainWindow path loads all 55 building payloads, got %1")
                .arg(tileDiagnostics.loadedPayloadCount));
    mark("local_detail_loaded", QStringLiteral("payloads=%1").arg(tileDiagnostics.loadedPayloadCount));

    mark("pre_close_drain_begin");
    processEventsFor(3000);
    const auto closeStats = view->performanceStats();
    mark("pre_close_drain_end", QStringLiteral("pending_jobs=%1 running_jobs=%2")
        .arg(closeStats.pendingJobs).arg(closeStats.runningJobs));
    if (scenario == QStringLiteral("rex"))
        require(closeStats.runningJobs > 0, QStringLiteral("REX jobs are running at close"));
    if (scenario == QStringLiteral("layers"))
    {
        const QString imagery = sourceRoot.filePath(QStringLiteral("resources/maps/natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.vrt"));
        require(view->applyLocalSatelliteImagery(imagery) && view->applyLocalSatelliteImagery(imagery),
                QStringLiteral("satellite layer operation succeeds before close"));
    }
    if (scenario == QStringLiteral("standalone") || scenario == QStringLiteral("reopen"))
    {
        mapWindow->close();
        require(!mapWindow->isVisible(), QStringLiteral("map independently closes"));
        if (scenario == QStringLiteral("reopen"))
        {
            mapAction->trigger();
            processEventsFor(250);
            require(findMap3DWindow() && findMap3DWindow()->isVisible(), QStringLiteral("map reopens"));
        }
    }
    mark("main_window_close_begin");
    window->close();
    mark("main_window_close_end");
    processEventsFor(250);
    mark("post_close_event_pump_end");
    mark("main_window_delete_begin");
    delete window;
    mark("main_window_delete_end");
    processEventsFor(500);
    mark("post_destroy_event_pump_end", QStringLiteral("pending_jobs=%1 running_jobs=%2")
        .arg(jobs::get_metrics()->total_pending()).arg(jobs::get_metrics()->total_running()));

    mark("osgearth_jobs_shutdown_begin");
    const qint64 jobsShutdownStarted = lifecycleClock.elapsed();
    VaporView::Ground::Map3DController::shutdownBackgroundJobs();
    mark("osgearth_jobs_shutdown_end", QStringLiteral("duration_ms=%1 pending_jobs=%2 running_jobs=%3")
        .arg(lifecycleClock.elapsed() - jobsShutdownStarted)
        .arg(jobs::get_metrics()->total_pending())
        .arg(jobs::get_metrics()->total_running()));
    require(jobs::get_metrics()->total_running() == 0,
            QStringLiteral("osgEarth jobs are joined before GDAL process teardown"));

    mark("test_passed");
    std::cout << "main_window_map3d_real_close_test passed: MainWindow opened and closed real 3D map\n";
    return 0;
}
