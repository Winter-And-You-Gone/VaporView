#include "geo/GeoTypes.h"
#include "map3d/Map3DWindow.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QSlider>
#include <QTemporaryDir>
#include <QTextStream>

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

void writeSessionTrack(QTemporaryDir& sessionDir)
{
    require(sessionDir.isValid(), "temporary session directory is valid");
    QDir root(sessionDir.path());
    require(root.mkpath(QStringLiteral("sensors")), "create sensors directory");

    QFile devicesCsv(root.filePath(QStringLiteral("sensors/devices.csv")));
    require(devicesCsv.open(QIODevice::WriteOnly | QIODevice::Text), "open devices.csv");

    QTextStream out(&devicesCsv);
    out << "record_timestamp_us,device_timestamp_us,nav_lat_deg,nav_lon_deg,nav_height_m,gnss_satellites,hdop,fix_quality\n";
    out << "1000000,900000,39.9000000,116.3000000,45.0,12,0.9,fixed\n";
    out << "1050000,950000,39.9000100,116.3000200,46.0,12,0.9,fixed\n";
}

QLabel* statusLabel(VaporView::Map3D::Map3DWindow& window)
{
    QLabel* label = window.findChild<QLabel*>(QStringLiteral("map3DStatusLabel"));
    require(label != nullptr, "map3d status label exists");
    return label;
}

QAction* actionByName(VaporView::Map3D::Map3DWindow& window, const QString& objectName)
{
    QAction* action = window.findChild<QAction*>(objectName);
    require(action != nullptr, "map3d action exists");
    return action;
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("VaporViewTest"));
    QCoreApplication::setApplicationName(QStringLiteral("map3d_window_smoke_test"));

    VaporView::Map3D::Map3DWindow window;
    QCoreApplication::processEvents();

    QLabel* label = statusLabel(window);
    require(label->text().contains(QStringLiteral("Points: 0")), "initial status has zero points");
    QAction* replayAction = actionByName(window, QStringLiteral("map3DReplayAction"));
    QAction* replayStopAction = actionByName(window, QStringLiteral("map3DReplayStopAction"));
    QAction* reloadBestMapAction = actionByName(window, QStringLiteral("map3DReloadBestMapAction"));
    QAction* flyToAircraftAction = actionByName(window, QStringLiteral("map3DFlyToAircraftAction"));
    QAction* flyToTrackAction = actionByName(window, QStringLiteral("map3DFlyToTrackAction"));
    QAction* resetViewAction = actionByName(window, QStringLiteral("map3DResetViewAction"));
    auto* replaySpeedCombo = window.findChild<QComboBox*>(QStringLiteral("map3DReplaySpeedCombo"));
    auto* replaySlider = window.findChild<QSlider*>(QStringLiteral("map3DReplaySlider"));
    require(reloadBestMapAction->isEnabled(), "reload best local map action exists");
    require(flyToAircraftAction->isEnabled(), "fly to aircraft action exists");
    require(flyToTrackAction->isEnabled(), "fly to track action exists");
    require(resetViewAction->isEnabled(), "reset view action exists");
    require(replaySpeedCombo != nullptr, "replay speed combo exists");
    require(replaySlider != nullptr, "replay slider exists");
    require(!replayAction->isEnabled(), "replay disabled before session load");
    require(!replayStopAction->isEnabled(), "replay stop disabled before session load");
    require(!replaySlider->isEnabled(), "replay slider disabled before session load");

    VaporView::Geo::NavSample sample;
    sample.recordTimestampUs = 1000000;
    sample.deviceTimestampUs = 900000;
    sample.latDeg = 39.9;
    sample.lonDeg = 116.3;
    sample.heightM = 45.0;
    sample.satellites = 12;
    sample.hdop = 0.9;
    sample.fixQuality = VaporView::Geo::FixQuality::Fixed;
    window.appendSample(sample);
    QCoreApplication::processEvents();
    require(label->text().contains(QStringLiteral("Points: 1")), "appendSample updates status");

    window.clearTrack();
    QCoreApplication::processEvents();
    require(label->text().contains(QStringLiteral("Points: 0")), "clearTrack resets status");

    QTemporaryDir sessionDir;
    writeSessionTrack(sessionDir);
    window.loadSessionDirectory(sessionDir.path());
    QCoreApplication::processEvents();
    require(label->text().contains(QStringLiteral("Points: 2")), "session load appends track samples");
    require(replayAction->isEnabled(), "replay enabled after session load");
    require(replayStopAction->isEnabled(), "replay stop enabled after session load");
    require(replaySlider->isEnabled(), "replay slider enabled after session load");
    require(replaySlider->maximum() == 1, "replay slider spans session samples");
    require(label->text().contains(QStringLiteral("Replay 2/2")), "session status includes replay position");

    replaySlider->setSliderDown(true);
    replaySlider->setValue(0);
    QCoreApplication::processEvents();
    replaySlider->setSliderDown(false);
    require(label->text().contains(QStringLiteral("Points: 1")), "slider previews replay sample");
    require(label->text().contains(QStringLiteral("Replay 1/2")), "slider updates replay position");

    replaySpeedCombo->setCurrentText(QStringLiteral("2x"));
    QCoreApplication::processEvents();
    require(replaySpeedCombo->currentText() == QStringLiteral("2x"), "replay speed can be changed");

    replayAction->trigger();
    QCoreApplication::processEvents();
    require(replayAction->isChecked(), "replay action toggles into playing state");
    require(replayAction->text() == QStringLiteral("暂停"), "replay action text changes to pause");

    replayStopAction->trigger();
    QCoreApplication::processEvents();
    require(!replayAction->isChecked(), "stop clears replay playing state");
    require(label->text().contains(QStringLiteral("Replay 1/2")), "stop rewinds replay to first sample");

    return 0;
}
