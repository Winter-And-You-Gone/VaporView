#include "AppTheme.h"
#include "geo/GeoTypes.h"
#include "map3d/Map3DWindow.h"
#include "SingleLevelPopupMenu.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
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

void requireComboPopupStyled(QComboBox *combo, const char *message)
{
    require(combo != nullptr, message);
    require(combo->property("vaporViewComboPopupStyled").toBool(), message);
    require(combo->view() != nullptr, message);
    require(combo->view()->property("vaporViewComboPopupStyled").toBool(), message);
    require(!combo->view()->property("vaporViewComboPopupRoundedMaskEnabled").isValid(), message);
    require(!combo->view()->property("vaporViewComboPopupViewportMargin").isValid(), message);
    require(!combo->view()->property("vaporViewComboPopupShadowEnabled").toBool(), message);
    require(!combo->view()->property("floatingPanelChrome").toBool(), message);
    require(combo->view()->objectName() == QStringLiteral("vaporViewComboPopupView"), message);
    require(combo->view()->findChild<QWidget *>(QStringLiteral("vaporViewComboPopupBorderOverlay")) == nullptr,
            message);
    const QString popupStyle = combo->view()->styleSheet();
    const QString hoverColor = VaporView::appThemeColorName(VaporView::AppThemeColor::MenuHover,
                                                            VaporView::isDarkThemeEnabled());
    require(popupStyle.contains(QStringLiteral("border: none")) &&
                !popupStyle.contains(QStringLiteral("border: 1px solid")) &&
                !popupStyle.contains(QStringLiteral("border-bottom: 1px solid")) &&
                popupStyle.contains(QStringLiteral("border-radius: 0px")) &&
                popupStyle.contains(QStringLiteral("padding: 12px 0px")) &&
                popupStyle.contains(QStringLiteral("padding: 7px 14px")) &&
                popupStyle.contains(QStringLiteral("min-height: 30px")) &&
                popupStyle.contains(QStringLiteral("background-color: %1").arg(hoverColor)) &&
                !popupStyle.contains(QStringLiteral("padding: 12px 4px")),
            message);
}

void writeSessionTrack(QTemporaryDir& sessionDir)
{
    require(sessionDir.isValid(), "temporary session directory is valid");
    QDir root(sessionDir.path());
    require(root.mkpath(QStringLiteral("sensors")), "create sensors directory");

    QFile devicesCsv(root.filePath(QStringLiteral("sensors/devices.csv")));
    require(devicesCsv.open(QIODevice::WriteOnly | QIODevice::Text), "open devices.csv");

    QTextStream out(&devicesCsv);
    out << "record_timestamp_us,device_timestamp_us,nav_lat_deg,nav_lon_deg,nav_height_m,gnss_satellites,hdop,gnss_fix\n";
    out << "1000000,900000,39.9000000,116.3000000,45.0,12,0.9,RTK_FIXED\n";
    out << "1050000,950000,39.9000100,116.3000200,46.0,12,0.9,RTK_FIXED\n";
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
    QAction* followAction = actionByName(window, QStringLiteral("map3DFollowAction"));
    QAction* reloadBestMapAction = actionByName(window, QStringLiteral("map3DReloadBestMapAction"));
    QAction* flyToAircraftAction = actionByName(window, QStringLiteral("map3DFlyToAircraftAction"));
    QAction* flyToTrackAction = actionByName(window, QStringLiteral("map3DFlyToTrackAction"));
    QAction* resetViewAction = actionByName(window, QStringLiteral("map3DResetViewAction"));
    QAction* diagnosticsAction = actionByName(window, QStringLiteral("map3DDiagnosticsAction"));
    QAction* localImageryAction = actionByName(window, QStringLiteral("map3DLocalImageryAction"));
    QAction* local3DTilesAction = actionByName(window, QStringLiteral("map3DLocal3DTilesAction"));
    QAction* clearLocal3DTilesAction = actionByName(window, QStringLiteral("map3DClearLocal3DTilesAction"));
    QAction* loadAircraftModelAction = actionByName(window, QStringLiteral("map3DLoadAircraftModelAction"));
    QAction* resetAircraftModelAction = actionByName(window, QStringLiteral("map3DResetAircraftModelAction"));
    auto* replaySpeedCombo = window.findChild<QComboBox*>(QStringLiteral("map3DReplaySpeedCombo"));
    auto* replaySlider = window.findChild<QSlider*>(QStringLiteral("map3DReplaySlider"));
    auto* maxVisibleSamplesSpin = window.findChild<QSpinBox*>(QStringLiteral("map3DMaxVisibleSamplesSpin"));
    QWidget* mapView = window.findChild<QWidget*>(QStringLiteral("map3DView"));
    auto* osgView = window.findChild<VaporView::Map3D::OsgEarthViewWidget*>();
    require(mapView != nullptr, "embedded map view exists");
    if (osgView)
    {
        require(osgView->focusPolicy() == Qt::StrongFocus,
                "embedded osgEarth view accepts keyboard focus for native camera navigation");
    }
    require(reloadBestMapAction->isEnabled(), "reload best local map action exists");
    require(flyToAircraftAction->isEnabled(), "fly to aircraft action exists");
    require(flyToTrackAction->isEnabled(), "fly to track action exists");
    require(resetViewAction->isEnabled(), "reset view action exists");
    require(diagnosticsAction->isEnabled(), "diagnostics action exists");
    require(localImageryAction->menu() != nullptr, "local imagery action has a menu");
    require(qobject_cast<VaporView::SingleLevelPopupMenu*>(localImageryAction->menu()) != nullptr,
            "local imagery action uses the shared single-level popup menu");
    bool hasEnabledImageryEntry = false;
    for (QAction* action : localImageryAction->menu()->actions())
    {
        hasEnabledImageryEntry = hasEnabledImageryEntry || action->isEnabled();
    }
    require(localImageryAction->isEnabled() == hasEnabledImageryEntry,
            "local imagery action enabled state follows available local imagery entries");
    require(!clearLocal3DTilesAction->isEnabled(), "clear local 3D Tiles action starts disabled before preview load");
    require(loadAircraftModelAction->isEnabled(), "load aircraft model action exists");
    require(resetAircraftModelAction->isEnabled(), "reset aircraft model action exists");
    require(loadAircraftModelAction->toolTip().contains(QStringLiteral("飞机模型")),
            "load aircraft model action explains local model selection");
    require(resetAircraftModelAction->toolTip().contains(QStringLiteral("内置")),
            "reset aircraft model action explains built-in fallback");
    require(local3DTilesAction->toolTip().contains(QStringLiteral("地图诊断"))
                || local3DTilesAction->toolTip().contains(QStringLiteral("3D Tiles"))
                || local3DTilesAction->toolTip().contains(QStringLiteral("建筑瓦片")),
            "local 3D Tiles action explains its available or unavailable state");
    require(replaySpeedCombo != nullptr, "replay speed combo exists");
    requireComboPopupStyled(replaySpeedCombo,
                            "map3d replay speed combo uses the shared popup styling helper");
    require(replaySlider != nullptr, "replay slider exists");
    require(maxVisibleSamplesSpin != nullptr, "max visible samples spin box exists");
    require(followAction->isCheckable(), "follow aircraft action is checkable");
    require(maxVisibleSamplesSpin->minimum() == 1000, "max visible samples lower bound is 1000");
    require(maxVisibleSamplesSpin->maximum() == 1000000, "max visible samples upper bound is 1000000");
    require(!replayAction->isEnabled(), "replay disabled before session load");
    require(!replayStopAction->isEnabled(), "replay stop disabled before session load");
    require(!replaySlider->isEnabled(), "replay slider disabled before session load");

    maxVisibleSamplesSpin->setValue(1000);
    {
        QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
        require(settings.value(QStringLiteral("maxVisibleSamples")).toInt() == 1000,
                "max visible sample setting is persisted");
    }

    followAction->setChecked(true);
    QCoreApplication::processEvents();
    {
        QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
        require(settings.value(QStringLiteral("followAircraft")).toBool(),
                "follow aircraft setting is persisted");
    }
    require(label->text().contains(QStringLiteral("Follow On")),
            "status reports follow camera enabled state");
    followAction->setChecked(false);
    QCoreApplication::processEvents();
    require(label->text().contains(QStringLiteral("Follow Off")),
            "status reports follow camera disabled state");

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
    require(label->text().contains(QStringLiteral("Fix Fixed")),
            "status reports readable GNSS fix quality");
    require(label->text().contains(QStringLiteral("Height ref unchecked")),
            "status warns that displayed height reference is unchecked");
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
    {
        QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
        require(settings.value(QStringLiteral("lastSessionDir")).toString()
                    == QFileInfo(sessionDir.path()).absoluteFilePath(),
                "programmatic session load persists last session directory");
    }
    require(label->text().contains(QStringLiteral("Points: 2")), "session load appends track samples");
    require(label->text().contains(QStringLiteral("Q Fixed 2")),
            "session load recognizes the recorded gnss_fix quality column");
    require(label->text().contains(QStringLiteral("Source Session")), "session load reports session source");
    require(label->text().contains(QStringLiteral("Camera Track auto")),
            "session load automatically focuses the complete track");
    require(replayAction->isEnabled(), "replay enabled after session load");
    require(replayStopAction->isEnabled(), "replay stop enabled after session load");
    require(replaySlider->isEnabled(), "replay slider enabled after session load");
    require(replaySlider->maximum() == 50, "replay slider spans session time in milliseconds");
    require(label->text().contains(QStringLiteral("Replay paused 2/2")), "session status includes paused replay position");
    require(label->text().contains(QStringLiteral("t 0.050/0.050 s")), "session status includes replay time progress");

    replaySlider->setSliderDown(true);
    replaySlider->setValue(0);
    QCoreApplication::processEvents();
    replaySlider->setSliderDown(false);
    require(label->text().contains(QStringLiteral("Points: 1")), "slider previews replay sample");
    require(label->text().contains(QStringLiteral("Source Replay")), "slider preview reports replay source");
    require(label->text().contains(QStringLiteral("Replay stopped 1/2")), "slider updates replay state and position");
    require(label->text().contains(QStringLiteral("t 0.000/0.050 s")), "slider updates replay elapsed time");

    diagnosticsAction->trigger();
    QCoreApplication::processEvents();
    auto* diagnosticsText = window.findChild<QPlainTextEdit*>();
    require(diagnosticsText != nullptr, "diagnostics text view exists");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Track data:")),
            "diagnostics include track data section");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Earth load:")),
            "diagnostics include earth runtime load section");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Local 3D Tiles preview load:")),
            "diagnostics include local 3D Tiles preview load section");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Cleared previous preview:")),
            "diagnostics report whether a local 3D Tiles load removed an old preview");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Aircraft model:")),
            "diagnostics include aircraft model section");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Requested path: <none>")),
            "diagnostics report no custom aircraft model in headless smoke test");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Built-in marker: yes")),
            "diagnostics report built-in aircraft marker fallback");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Mode:")),
            "diagnostics include selected map mode");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Earth file:")),
            "diagnostics include selected earth file");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Natural Earth texture:")),
            "diagnostics include Natural Earth texture path");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Local grid fallback:")),
            "diagnostics include local grid fallback availability");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Copernicus DEM VRT:")),
            "diagnostics include Copernicus DEM VRT path");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("SRTM VRT:")),
            "diagnostics include SRTM VRT path");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Base map priority: Real 3D local > Copernicus DEM > SRTM > Natural Earth > Local grid")),
            "diagnostics include the base map selection priority");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Selected base mode:")),
            "diagnostics include selected base mode");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("OSM layer contract:")),
            "diagnostics include expected OSM layer contract section");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("layer roads")),
            "diagnostics include expected OSM roads GeoPackage layer name");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("generated data only")),
            "diagnostics include safe-default OSM generated-data contract");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("OSG_LIBRARY_PATH:")),
            "diagnostics include OSG plugin environment");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("osgEarth environment:")),
            "diagnostics include osgEarth environment variable list");
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
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Optional local imagery VRTs:")),
            "diagnostics include optional local imagery VRT count");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Optional local imagery menu-ready overlays:")),
            "diagnostics include menu-ready optional local imagery count");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Local 3D Tiles contract:")),
            "diagnostics include local 3D Tiles contract status");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Local 3D Tiles tileset:")),
            "diagnostics include local 3D Tiles tileset path");
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
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Replay state: stopped")),
            "diagnostics include explicit replay state");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Replay position: 1/2")),
            "diagnostics include replay position");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Replay speed:")),
            "diagnostics include replay speed");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Replay time: t 0.000/0.050 s")),
            "diagnostics include replay elapsed time");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Attitude source: none")),
            "diagnostics include attitude source");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Follow aircraft: off")),
            "diagnostics include follow camera state");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Height safety note: height reference unchecked")),
            "diagnostics include height reference safety note");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Fix quality: Fixed")),
            "diagnostics include readable GNSS fix quality");
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
    require(label->text().contains(QStringLiteral("Replay stopped 1/2 2x")),
            "status refreshes when replay speed changes");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Replay speed: 2x")),
            "diagnostics refresh when replay speed changes");
    {
        QSettings settings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
        require(settings.value(QStringLiteral("replaySpeed")).toDouble() == 2.0,
                "replay speed setting is persisted");
    }

    replayAction->trigger();
    QCoreApplication::processEvents();
    require(replayAction->isChecked(), "replay action toggles into playing state");
    require(replayAction->text() == QStringLiteral("暂停"), "replay action text changes to pause");
    require(label->text().contains(QStringLiteral("Replay playing 1/2")),
            "status reports explicit playing replay state");

    replayStopAction->trigger();
    QCoreApplication::processEvents();
    require(!replayAction->isChecked(), "stop clears replay playing state");
    require(label->text().contains(QStringLiteral("Replay stopped 1/2")), "stop rewinds replay to first sample and reports stopped state");

    return 0;
}
