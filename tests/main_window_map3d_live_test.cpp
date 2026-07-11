#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QLabel>
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
    sample.yaw_deg = 91.0;
    return sample;
}

VaporView::TelemetryBasic makeRemoteTelemetry(double latitudeDeg, quint64 hostTimeUs)
{
    VaporView::TelemetryBasic telemetry;
    telemetry.host_time_us = hostTimeUs;
    telemetry.epsilon_time_us = hostTimeUs - 2000;
    telemetry.validity_flags = VaporView::BasicHasEpsilonTime |
                               VaporView::BasicHasPosition |
                               VaporView::BasicHasGnssQuality |
                               VaporView::BasicHasAttitude;
    telemetry.latitude_deg = latitudeDeg;
    telemetry.longitude_deg = 116.4;
    telemetry.height_m = 50.0;
    telemetry.gnss_fix_code = 6;
    telemetry.gnss_satellites = 14;
    telemetry.hdop = 0.8f;
    telemetry.vdop = 1.0f;
    telemetry.roll_deg = 1.5f;
    telemetry.pitch_deg = -2.0f;
    telemetry.yaw_deg = 93.0f;
    return telemetry;
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
    QLabel* mapStatusLabel = mapWindow->findChild<QLabel*>(QStringLiteral("map3DStatusLabel"));
    require(mapStatusLabel != nullptr, "3D map status label exists");

    window.testMaybeForwardMap3DSampleForMap3D(makeSample(39.900001), 1000000);
    window.testMaybeForwardMap3DSampleForMap3D(makeSample(39.900002), 1005000);
    window.testMaybeForwardMap3DSampleForMap3D(makeSample(39.900003), 1010000);

    require(window.testPendingMap3DSampleCount() == 1,
            "3D map live forwarding keeps only the latest pending sample per flush interval");
    require(window.testLatestPendingMap3DRecordTimestampUs() == 1010000,
            "3D map live forwarding retains the newest pending sample timestamp");
    require(window.testMap3DFlushTimerActive(),
            "3D map flush timer starts after a valid live sample");

    VaporView::TelemetryBasic remoteTelemetry = makeRemoteTelemetry(39.910001, 2010000);
    window.testOnRemoteBasicTelemetryUpdatedForMap3D(remoteTelemetry);
    require(window.testPendingMap3DSampleCount() == 1,
            "remote sky telemetry forwards one pending 3D map sample");
    require(window.testLatestPendingMap3DRecordTimestampUs() == 2010000,
            "remote sky telemetry uses host timestamp for the pending 3D map sample");
    processEventsFor(80);
    require(mapStatusLabel->text().contains(QStringLiteral("Att Euler")),
            "flushed remote sky telemetry reports Euler aircraft attitude source");

    VaporView::EpsilonData invalidSample = makeSample(39.900004);
    invalidSample.valid = false;
    window.testMaybeForwardMap3DSampleForMap3D(invalidSample, 1015000);
    require(window.testPendingMap3DSampleCount() == 0,
            "invalid live sample clears pending 3D map samples");
    require(!window.testMap3DFlushTimerActive(),
            "invalid live sample stops the 3D map flush timer");
    require(window.testLastMap3DDropReason() == QStringLiteral("epsilon invalid"),
            "invalid live sample records a 3D map drop reason");
    require(mapStatusLabel->text().contains(QStringLiteral("Last drop Live: epsilon invalid")),
            "3D map status reports invalid live sample drop reason");

    VaporView::EpsilonData invalidFixSample = makeSample(39.900005);
    invalidFixSample.gnss_fix_code = 0;
    window.testMaybeForwardMap3DSampleForMap3D(invalidFixSample, 1018000);
    require(window.testPendingMap3DSampleCount() == 1,
            "live sample with invalid GNSS fix but valid LLH queues for 3D marker display");
    require(window.testLatestPendingMap3DRecordTimestampUs() == 1018000,
            "invalid-fix live sample retains its timestamp while queued");
    require(window.testMap3DFlushTimerActive(),
            "invalid-fix live sample starts the 3D map flush timer");
    require(window.testLastMap3DDropReason().isEmpty(),
            "invalid-fix live sample is not treated as a dropped 3D map sample");
    processEventsFor(80);
    require(mapStatusLabel->text().contains(QStringLiteral("Fix Invalid")),
            "3D map status reports the forwarded invalid GNSS fix");
    require(mapStatusLabel->text().contains(QStringLiteral("Invalid 1")),
            "3D map status counts invalid GNSS fixes as marker samples");

    window.testMaybeForwardMap3DSampleForMap3D(makeSample(39.900005), 1020000);
    require(window.testPendingMap3DSampleCount() == 1,
            "valid live sample queues after invalid sample reset");

    VaporView::TelemetryBasic ecefOnlyRemote = makeRemoteTelemetry(39.920001, 2020000);
    ecefOnlyRemote.validity_flags = VaporView::BasicHasEpsilonTime |
                                    VaporView::BasicHasEcef |
                                    VaporView::BasicHasGnssQuality;
    ecefOnlyRemote.latitude_deg = 0.0;
    ecefOnlyRemote.longitude_deg = 0.0;
    ecefOnlyRemote.height_m = 0.0;
    ecefOnlyRemote.ecef_x_m = -2170000.0;
    ecefOnlyRemote.ecef_y_m = 4380000.0;
    ecefOnlyRemote.ecef_z_m = 4070000.0;
    window.testOnRemoteBasicTelemetryUpdatedForMap3D(ecefOnlyRemote);
    require(window.testPendingMap3DSampleCount() == 0,
            "remote telemetry without BasicHasPosition clears pending samples and does not inject a 0/0/0 3D map point");
    require(!window.testMap3DFlushTimerActive(),
            "remote telemetry without position leaves the 3D map flush timer stopped");
    require(window.testLastMap3DDropReason() == QStringLiteral("missing BasicHasPosition"),
            "remote telemetry without position records a 3D map drop reason");
    require(mapStatusLabel->text().contains(QStringLiteral("Last drop Remote: missing BasicHasPosition")),
            "3D map status reports remote telemetry drop reason");

    mapWindow->hide();
    window.testMaybeForwardMap3DSampleForMap3D(makeSample(39.900006), 1025000);
    require(window.testPendingMap3DSampleCount() == 0,
            "hidden 3D map window clears pending live samples");
    require(!window.testMap3DFlushTimerActive(),
            "hidden 3D map window stops live sample flush timer");

    mapWindow->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    processEventsFor(200);
    window.close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    processEventsFor(200);
    std::cout << "main_window_map3d_live_test passed\n";
    return 0;
}
