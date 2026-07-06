#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QWidget>

#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
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

VaporView::EpsilonData makeSample(double latitudeDeg)
{
    VaporView::EpsilonData sample;
    sample.valid = true;
    sample.latitude_deg = latitudeDeg;
    sample.longitude_deg = 116.3;
    sample.height_m = 45.0;
    sample.gnss_fix_code = 6;
    sample.gnss_satellites = 12;
    sample.hdop = 0.9;
    sample.vdop = 1.1;
    return sample;
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("VAPORVIEW_MAP3D_HEADLESS_TEST", "1");

    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), "temporary settings directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("VaporViewMap3DLiveTest"));
    app.setApplicationName(QStringLiteral("main_window_map3d_live_test"));

    MainWindow window;
    window.resize(1000, 700);
    window.show();
    processEventsFor(200);

    QAction* mapAction = findActionByText(&window,
                                          {QStringLiteral("三维地图..."),
                                           QStringLiteral("3D Map...")});
    require(mapAction != nullptr, "3D map action exists");
    mapAction->trigger();
    processEventsFor(100);

    QWidget* mapWindow = window.findChild<QWidget*>(QStringLiteral("map3DWindow"));
    require(mapWindow != nullptr && mapWindow->isVisible(), "3D map window opens");

    window.testMaybeForwardMap3DSampleForMap3D(makeSample(39.900001), 1000000);
    window.testMaybeForwardMap3DSampleForMap3D(makeSample(39.900002), 1005000);
    window.testMaybeForwardMap3DSampleForMap3D(makeSample(39.900003), 1010000);

    require(window.testPendingMap3DSampleCount() == 1,
            "3D map live forwarding keeps only the latest pending sample per flush interval");
    require(window.testLatestPendingMap3DRecordTimestampUs() == 1010000,
            "3D map live forwarding retains the newest pending sample timestamp");
    require(window.testMap3DFlushTimerActive(),
            "3D map flush timer starts after a valid live sample");

    VaporView::EpsilonData invalidSample = makeSample(39.900004);
    invalidSample.valid = false;
    window.testMaybeForwardMap3DSampleForMap3D(invalidSample, 1015000);
    require(window.testPendingMap3DSampleCount() == 0,
            "invalid live sample clears pending 3D map samples");
    require(!window.testMap3DFlushTimerActive(),
            "invalid live sample stops the 3D map flush timer");

    window.testMaybeForwardMap3DSampleForMap3D(makeSample(39.900005), 1020000);
    require(window.testPendingMap3DSampleCount() == 1,
            "valid live sample queues after invalid sample reset");

    mapWindow->hide();
    window.testMaybeForwardMap3DSampleForMap3D(makeSample(39.900006), 1025000);
    require(window.testPendingMap3DSampleCount() == 0,
            "hidden 3D map window clears pending live samples");
    require(!window.testMap3DFlushTimerActive(),
            "hidden 3D map window stops live sample flush timer");

    window.close();
    processEventsFor(50);
    std::cout << "main_window_map3d_live_test passed\n";
    return 0;
}
