#include "geo/GeoTypes.h"
#include "map3d/Map3DWindow.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>

#include <cstdlib>
#include <iostream>
#include <vector>

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
    qputenv("VAPORVIEW_MAP3D_HEADLESS_TEST", "1");

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("VaporViewTest"));
    QCoreApplication::setApplicationName(QStringLiteral("map3d_window_smoke_test"));

    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), "temporary settings directory is valid");
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);

    VaporView::Map3D::Map3DWindow window;
    QCoreApplication::processEvents();

    QLabel* label = statusLabel(window);
    require(label->text().contains(QStringLiteral("Points: 0")), "initial status has zero points");
    require(label->text().contains(QStringLiteral("Source none")), "initial status reports no track source");
    QAction* replayAction = actionByName(window, QStringLiteral("map3DReplayAction"));
    QAction* replayStopAction = actionByName(window, QStringLiteral("map3DReplayStopAction"));
    QAction* reloadBestMapAction = actionByName(window, QStringLiteral("map3DReloadBestMapAction"));
    QAction* flyToAircraftAction = actionByName(window, QStringLiteral("map3DFlyToAircraftAction"));
    QAction* flyToTrackAction = actionByName(window, QStringLiteral("map3DFlyToTrackAction"));
    QAction* resetViewAction = actionByName(window, QStringLiteral("map3DResetViewAction"));
    QAction* diagnosticsAction = actionByName(window, QStringLiteral("map3DDiagnosticsAction"));
    QAction* localImageryAction = actionByName(window, QStringLiteral("map3DLocalImageryAction"));
    auto* replaySpeedCombo = window.findChild<QComboBox*>(QStringLiteral("map3DReplaySpeedCombo"));
    auto* replaySlider = window.findChild<QSlider*>(QStringLiteral("map3DReplaySlider"));
    auto* maxVisibleSamplesSpin = window.findChild<QSpinBox*>(QStringLiteral("map3DMaxVisibleSamplesSpin"));
    require(reloadBestMapAction->isEnabled(), "reload best local map action exists");
    require(flyToAircraftAction->isEnabled(), "fly to aircraft action exists");
    require(flyToTrackAction->isEnabled(), "fly to track action exists");
    require(resetViewAction->isEnabled(), "reset view action exists");
    require(diagnosticsAction->isEnabled(), "diagnostics action exists");
    require(localImageryAction->menu() != nullptr, "local imagery action has a menu");
    require(!localImageryAction->isEnabled(), "local imagery action starts disabled without local imagery VRTs");
    require(replaySpeedCombo != nullptr, "replay speed combo exists");
    require(replaySlider != nullptr, "replay slider exists");
    require(maxVisibleSamplesSpin != nullptr, "max visible samples spin box exists");
    require(maxVisibleSamplesSpin->minimum() == 1000, "max visible samples lower bound is 1000");
    require(maxVisibleSamplesSpin->maximum() == 1000000, "max visible samples upper bound is 1000000");
    require(!replayAction->isEnabled(), "replay disabled before session load");
    require(!replayStopAction->isEnabled(), "replay stop disabled before session load");
    require(!replaySlider->isEnabled(), "replay slider disabled before session load");

    maxVisibleSamplesSpin->setValue(1000);
    std::vector<VaporView::Geo::NavSample> manySamples(1100);
    for (int i = 0; i < static_cast<int>(manySamples.size()); ++i)
    {
        manySamples[i].recordTimestampUs = static_cast<qint64>(i + 1) * 1000000;
        manySamples[i].latDeg = 39.9 + static_cast<double>(i) * 0.000001;
        manySamples[i].lonDeg = 116.3;
        manySamples[i].heightM = 45.0;
        manySamples[i].nedNM = static_cast<double>(i);
        manySamples[i].nedEM = 0.0;
        manySamples[i].nedDM = -45.0;
        manySamples[i].fixQuality = VaporView::Geo::FixQuality::Fixed;
        manySamples[i].satellites = 12;
        manySamples[i].hdop = 0.9;
    }
    window.appendSamples(manySamples);
    QCoreApplication::processEvents();
    require(label->text().contains(QStringLiteral("Points: 1000/1100")),
            "max visible samples caps the visible status count");
    require(label->text().contains(QStringLiteral("Q Fixed 1000")),
            "status includes visible fixed quality count");
    require(label->text().contains(QStringLiteral("Source Live")), "live append batch reports live source");
    window.clearTrack();
    QCoreApplication::processEvents();

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
    require(label->text().contains(QStringLiteral("Q Fixed 1")),
            "appendSample status includes quality summary");
    require(label->text().contains(QStringLiteral("Source Live")), "appendSample reports live source");
    require(label->text().contains(QStringLiteral("rec 1000000")), "status includes latest record timestamp");
    require(label->text().contains(QStringLiteral("dev 900000")), "status includes latest device timestamp");
    require(label->text().contains(QStringLiteral("Sats 12")), "status includes latest sample satellite count");
    require(label->text().contains(QStringLiteral("HDOP 0.90")), "status includes latest sample HDOP");
    require(label->text().contains(QStringLiteral("Att none")), "status reports absent attitude source");

    window.clearTrack();
    QCoreApplication::processEvents();

    VaporView::Geo::NavSample eulerSample = sample;
    eulerSample.yawDeg = 91.0;
    window.appendSample(eulerSample);
    QCoreApplication::processEvents();
    require(label->text().contains(QStringLiteral("Att Euler")), "status reports Euler attitude source");

    window.clearTrack();
    QCoreApplication::processEvents();

    VaporView::Geo::NavSample quaternionSample = sample;
    quaternionSample.quatW = 1.0;
    quaternionSample.quatX = 0.0;
    quaternionSample.quatY = 0.0;
    quaternionSample.quatZ = 0.0;
    window.appendSample(quaternionSample);
    QCoreApplication::processEvents();
    require(label->text().contains(QStringLiteral("Att Quaternion")), "status reports quaternion attitude source");

    VaporView::Geo::NavSample throttledSample = sample;
    throttledSample.recordTimestampUs = 2000000;
    throttledSample.deviceTimestampUs = 1900000;
    throttledSample.latDeg = 39.91;
    window.appendSample(throttledSample);
    QCoreApplication::processEvents();
    require(!label->text().contains(QStringLiteral("rec 2000000")),
            "live status updates are throttled below 5 Hz");
    QThread::msleep(220);
    QCoreApplication::processEvents();
    throttledSample.recordTimestampUs = 3000000;
    throttledSample.deviceTimestampUs = 2900000;
    throttledSample.latDeg = 39.92;
    window.appendSample(throttledSample);
    QCoreApplication::processEvents();
    require(label->text().contains(QStringLiteral("rec 3000000")),
            "live status updates refresh after the 5 Hz throttle interval");

    window.clearTrack();
    QCoreApplication::processEvents();

    require(label->text().contains(QStringLiteral("Points: 0")), "clearTrack resets status");
    require(label->text().contains(QStringLiteral("Source none")), "clearTrack resets track source");

    QTemporaryDir sessionDir;
    writeSessionTrack(sessionDir);
    window.loadSessionDirectory(sessionDir.path());
    QCoreApplication::processEvents();
    require(label->text().contains(QStringLiteral("Points: 2")), "session load appends track samples");
    require(label->text().contains(QStringLiteral("Source Session")), "session load reports session source");
    require(label->text().contains(QStringLiteral("Camera Track auto")),
            "session load automatically focuses the complete track");
    require(replayAction->isEnabled(), "replay enabled after session load");
    require(replayStopAction->isEnabled(), "replay stop enabled after session load");
    require(replaySlider->isEnabled(), "replay slider enabled after session load");
    require(replaySlider->maximum() == 50, "replay slider spans session time in milliseconds");
    require(label->text().contains(QStringLiteral("Replay 2/2")), "session status includes replay position");
    require(label->text().contains(QStringLiteral("t 0.050/0.050 s")), "session status includes replay time progress");

    replaySlider->setSliderDown(true);
    replaySlider->setValue(0);
    QCoreApplication::processEvents();
    replaySlider->setSliderDown(false);
    require(label->text().contains(QStringLiteral("Points: 1")), "slider previews replay sample");
    require(label->text().contains(QStringLiteral("Source Replay")), "slider preview reports replay source");
    require(label->text().contains(QStringLiteral("Replay 1/2")), "slider updates replay position");
    require(label->text().contains(QStringLiteral("t 0.000/0.050 s")), "slider updates replay elapsed time");

    diagnosticsAction->trigger();
    QCoreApplication::processEvents();
    auto* diagnosticsText = window.findChild<QPlainTextEdit*>();
    require(diagnosticsText != nullptr, "diagnostics text view exists");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Track data:")),
            "diagnostics include track data section");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Earth load:")),
            "diagnostics include earth runtime load section");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Mode:")),
            "diagnostics include selected map mode");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Earth file:")),
            "diagnostics include selected earth file");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Natural Earth texture:")),
            "diagnostics include Natural Earth texture path");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Copernicus DEM VRT:")),
            "diagnostics include Copernicus DEM VRT path");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("SRTM VRT:")),
            "diagnostics include SRTM VRT path");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("OSM layer contract:")),
            "diagnostics include expected OSM layer contract section");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("layer roads")),
            "diagnostics include expected OSM roads GeoPackage layer name");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("TiledFeatureModel OSM place labels")),
            "diagnostics include OSM place label render contract");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("OSG_LIBRARY_PATH:")),
            "diagnostics include OSG plugin environment");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("GDAL_DATA:")),
            "diagnostics include GDAL data environment");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("PROJ_DATA:")),
            "diagnostics include PROJ data environment");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Attempted:")),
            "diagnostics include earth load attempt state");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Render performance:")),
            "diagnostics include render performance section");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Local imagery menu:")),
            "diagnostics include local imagery menu availability");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Local 3D Tiles contract:")),
            "diagnostics include local 3D Tiles contract status");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Trajectory quality:")),
            "diagnostics include trajectory quality section");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Fixed: 1")),
            "diagnostics include fixed quality count");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Visible line samples: 1")),
            "diagnostics include visible line sample count");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Samples:")),
            "diagnostics include visible and total sample counts");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Source: Replay")),
            "diagnostics include latest track source");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Attitude source: none")),
            "diagnostics include attitude source");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Camera: Track auto")),
            "diagnostics include latest automatic camera action");

    flyToAircraftAction->trigger();
    QCoreApplication::processEvents();
    require(label->text().contains(QStringLiteral("Camera Aircraft")),
            "fly to aircraft updates persistent camera status");
    require(label->text().contains(QStringLiteral("Lat 39.9000000")),
            "camera action preserves latest sample status details");

    flyToTrackAction->trigger();
    QCoreApplication::processEvents();
    require(label->text().contains(QStringLiteral("Camera Track")),
            "fly to track updates persistent camera status");

    resetViewAction->trigger();
    QCoreApplication::processEvents();
    require(label->text().contains(QStringLiteral("Camera Reset")),
            "reset view updates persistent camera status");

    window.noteLiveSampleDrop(QStringLiteral("Live"), QStringLiteral("missing LLH"), 123456);
    QCoreApplication::processEvents();
    require(label->text().contains(QStringLiteral("Last drop Live: missing LLH")),
            "status includes latest live sample drop reason");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Last drop reason: missing LLH")),
            "diagnostics include latest live sample drop reason");

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
