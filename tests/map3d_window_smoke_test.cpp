#include "shared/theme/AppTheme.h"
#include "geo/GeoTypes.h"
#include "map3d/Map3DWindow.h"
#include "map3d/Map3DRuntime.h"
#include "shared/theme/SingleLevelPopupComboBox.h"
#include "shared/theme/SingleLevelPopupMenu.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QToolButton>
#include <QWidgetAction>

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
    require(combo->view()->hasMouseTracking() &&
                combo->view()->testAttribute(Qt::WA_Hover) &&
                combo->view()->viewport() &&
                combo->view()->viewport()->hasMouseTracking() &&
                combo->view()->viewport()->testAttribute(Qt::WA_Hover),
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
                popupStyle.contains(QStringLiteral("::item:hover,")) &&
                popupStyle.contains(QStringLiteral("::item:selected:hover")) &&
                popupStyle.contains(QStringLiteral("::item:selected:active:hover")) &&
                popupStyle.contains(QStringLiteral("::item:selected:!active:hover { background-color: %1").arg(hoverColor)) &&
                !popupStyle.contains(QStringLiteral("padding: 12px 4px")),
            message);
}

void requireSingleLevelComboPopup(QComboBox *combo, const char *message)
{
    require(combo != nullptr, message);
    auto *singleLevelCombo = dynamic_cast<VaporView::SingleLevelPopupComboBox *>(combo);
    require(singleLevelCombo != nullptr, message);
    require(combo->property("usesSingleLevelPopupMenu").toBool(), message);
    VaporView::SingleLevelPopupMenu *popupMenu = singleLevelCombo->popupMenu();
    require(popupMenu != nullptr, message);

    singleLevelCombo->showPopup();
    QCoreApplication::processEvents();
    require(popupMenu->isVisible(), message);
    const QList<VaporView::SingleLevelPopupMenuRow *> rows = popupMenu->rows();
    require(rows.size() == combo->count(), message);

    int previousBottom = -1;
    for (int i = 0; i < rows.size(); ++i)
    {
        const auto *row = rows.at(i);
        require(row != nullptr, message);
        require(row->text() == combo->itemText(i), message);
        require(row->height() >= 32, message);
        if (previousBottom >= 0)
        {
            require(row->y() >= previousBottom, message);
        }
        previousBottom = row->y() + row->height();
    }

    singleLevelCombo->hidePopup();
    QCoreApplication::processEvents();
    require(!popupMenu->isVisible(), message);
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
    out << "2000000,1900000,39.9000100,116.3000200,46.0,12,0.9,RTK_FIXED\n";
}

bool waitForText(QLabel* label, const QString& text, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        QCoreApplication::processEvents();
        if (label && label->text().contains(text))
        {
            return true;
        }
        QThread::msleep(10);
    }
    return label && label->text().contains(text);
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

QSettings map3DTestSettings()
{
    return QSettings(QSettings::defaultFormat(),
                     QSettings::UserScope,
                     QStringLiteral("VaporView"),
                     QStringLiteral("Map3D"));
}

bool waitForSessionDirectory(const QString& path, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    const QString expected = QFileInfo(path).absoluteFilePath();
    while (timer.elapsed() < timeoutMs)
    {
        QCoreApplication::processEvents();
        if (map3DTestSettings().value(QStringLiteral("lastSessionDir")).toString() == expected)
        {
            return true;
        }
        QThread::msleep(10);
    }
    return map3DTestSettings().value(QStringLiteral("lastSessionDir")).toString() == expected;
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("VAPORVIEW_MAP3D_HEADLESS_TEST", "1");

    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), "temporary settings directory is valid");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir.path());

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("VaporViewTest"));
    QCoreApplication::setApplicationName(QStringLiteral("map3d_window_smoke_test"));

    const QString expectedProjectData = QDir::cleanPath(
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../../data")));
    require(VaporView::Map3D::map3DProjectDataDirectory() == expectedProjectData,
            "session chooser defaults to the project-root data directory");
    require(VaporView::Map3D::map3DProjectDataDirectory()
                != QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("data")),
            "session chooser does not default to the release data directory");

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
    QAction* layersAction = actionByName(window, QStringLiteral("map3DLayersAction"));
    QAction* localImageryAction = actionByName(window, QStringLiteral("map3DLocalImageryAction"));
    QAction* local3DTilesAction = actionByName(window, QStringLiteral("map3DLocal3DTilesAction"));
    QAction* clearLocal3DTilesAction = actionByName(window, QStringLiteral("map3DClearLocal3DTilesAction"));
    QAction* loadAircraftModelAction = actionByName(window, QStringLiteral("map3DLoadAircraftModelAction"));
    QAction* resetAircraftModelAction = actionByName(window, QStringLiteral("map3DResetAircraftModelAction"));
    auto* replaySpeedCombo = window.findChild<QComboBox*>(QStringLiteral("map3DReplaySpeedCombo"));
    auto* heatMetricCombo = window.findChild<QComboBox*>(QStringLiteral("map3DHeatMetricCombo"));
    auto* heatPaletteCombo = window.findChild<QComboBox*>(QStringLiteral("map3DHeatPaletteCombo"));
    auto* replaySlider = window.findChild<QSlider*>(QStringLiteral("map3DReplaySlider"));
    auto* maxVisibleSamplesSpin = window.findChild<QSpinBox*>(QStringLiteral("map3DMaxVisibleSamplesSpin"));
    auto* trackLineWidthSpin = window.findChild<QSpinBox*>(QStringLiteral("map3DTrackLineWidthSpin"));
    auto* trackPointSizeSpin = window.findChild<QSpinBox*>(QStringLiteral("map3DTrackPointSizeSpin"));
    auto* heatLegendLabel = window.findChild<QLabel*>(QStringLiteral("map3DHeatLegendLabel"));
    QAction* trackLineVisibleAction =
        actionByName(window, QStringLiteral("map3DTrackLineVisibleAction"));
    QAction* trackPointsVisibleAction =
        actionByName(window, QStringLiteral("map3DTrackPointsVisibleAction"));
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
    require(!layersAction->icon().isNull(), "layers action uses a visible layers icon");
    require(layersAction->toolTip() == QStringLiteral("地图图层"),
            "layers action has a concise accessible label");
    auto* layersButton = window.findChild<QToolButton*>(QStringLiteral("map3DLayersButton"));
    require(layersButton != nullptr
                && layersButton->popupMode() == QToolButton::InstantPopup
                && layersButton->toolButtonStyle() == Qt::ToolButtonIconOnly,
            "clicking the layers icon opens the layer menu directly");
    require(layersButton->accessibleName() == QStringLiteral("地图图层"),
            "layers icon has an accessibility name");
    auto* layersMenu = qobject_cast<VaporView::SingleLevelPopupMenu*>(layersAction->menu());
    require(layersMenu != nullptr, "layers action uses the shared single-level popup menu");
    require(layersMenu->objectName() == QStringLiteral("map3DLayersMenu"),
            "layers menu has a stable object name");
    const QStringList expectedLayerLabels = {
        QStringLiteral("基础地理底图"),
        QStringLiteral("卫星遥感影像"),
        QStringLiteral("数字高程模型（DEM）"),
        QStringLiteral("水系"),
        QStringLiteral("道路网络"),
        QStringLiteral("三维建筑模型"),
        QStringLiteral("飞行任务要素（轨迹与飞行器）")
    };
    require(layersMenu->actions().size() == expectedLayerLabels.size(),
            "layers menu exposes the seven professional map layer groups");
    for (int index = 0; index < expectedLayerLabels.size(); ++index)
    {
        QAction* layerAction = layersMenu->actions().at(index);
        auto* widgetAction = qobject_cast<QWidgetAction*>(layerAction);
        auto* row = widgetAction
            ? qobject_cast<VaporView::SingleLevelPopupMenuRow*>(widgetAction->defaultWidget())
            : nullptr;
        require(layerAction->text() == expectedLayerLabels.at(index),
                "layer action uses the expected professional name");
        require(layerAction->isCheckable() && layerAction->isChecked(),
                "layer action starts visible and is independently checkable");
        require(row && row->isChecked() && !row->closeOnClick(),
                "layer row mirrors its check state and keeps the menu open for multi-selection");
    }

    window.resize(760, 520);
    window.show();
    QCoreApplication::processEvents();
    const QRect windowClientRect(window.mapToGlobal(window.rect().topLeft()), window.rect().size());
    const QRect expectedPopupBounds = windowClientRect.adjusted(8, 8, -8, -8);
    layersMenu->popup(QPoint(windowClientRect.right() - 2, windowClientRect.bottom() - 2));
    QCoreApplication::processEvents();
    require(expectedPopupBounds.contains(layersMenu->geometry()),
            "layers menu stays inside the 3D map window in compact window mode");
    layersMenu->hide();

    QAction* satelliteLayerAction =
        actionByName(window, QStringLiteral("map3DLayer_satelliteImagery"));
    satelliteLayerAction->trigger();
    QCoreApplication::processEvents();
    {
        QSettings settings = map3DTestSettings();
        require(!settings.value(QStringLiteral("layers/satelliteImageryVisible"), true).toBool(),
                "satellite layer visibility is persisted when hidden");
    }
    satelliteLayerAction->trigger();
    QCoreApplication::processEvents();
    require(satelliteLayerAction->isChecked(),
            "satellite layer can be restored without closing the layer menu");
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
    require(heatMetricCombo != nullptr && heatMetricCombo->count() == 4,
            "heat metric combo exposes peak, humidity, temperature and pressure");
    require(heatPaletteCombo != nullptr && heatPaletteCombo->count() == 3
                && heatPaletteCombo->itemText(2) == QStringLiteral("SpectralReverse"),
            "heat palette combo exposes the three shared palette names");
    requireSingleLevelComboPopup(heatMetricCombo,
                                 "heat metric combo uses the shared single-level popup without overlapping rows");
    requireSingleLevelComboPopup(heatPaletteCombo,
                                 "heat palette combo uses the shared single-level popup without overlapping rows");
    require(trackLineVisibleAction->isCheckable() && trackLineVisibleAction->isChecked(),
            "track line visibility action starts enabled");
    require(trackPointsVisibleAction->isCheckable() && trackPointsVisibleAction->isChecked(),
            "track point visibility action starts enabled");
    require(trackLineWidthSpin != nullptr && trackLineWidthSpin->value() == 5,
            "track line width control starts at the default");
    require(trackPointSizeSpin != nullptr && trackPointSizeSpin->value() == 7,
            "track point size control starts at the default");
    require(heatLegendLabel != nullptr
                && heatLegendLabel->text().contains(QStringLiteral("峰值"))
                && heatLegendLabel->text().contains(QStringLiteral("无有效数据")),
            "heat legend reports no valid data instead of a fake zero range");
    require(replaySlider != nullptr, "replay slider exists");
    require(maxVisibleSamplesSpin != nullptr, "max visible samples spin box exists");
    require(followAction->isCheckable(), "follow aircraft action is checkable");
    require(maxVisibleSamplesSpin->minimum() == 1000, "max visible samples lower bound is 1000");
    require(maxVisibleSamplesSpin->maximum() == 1000000, "max visible samples upper bound is 1000000");
    require(!replayAction->isEnabled(), "replay disabled before session load");
    require(!replayStopAction->isEnabled(), "replay stop disabled before session load");
    require(!replaySlider->isEnabled(), "replay slider disabled before session load");

    heatMetricCombo->setCurrentText(QStringLiteral("温度"));
    heatPaletteCombo->setCurrentText(QStringLiteral("BlueRedFast"));
    trackLineVisibleAction->setChecked(false);
    trackPointsVisibleAction->setChecked(false);
    trackLineWidthSpin->setValue(9);
    trackPointSizeSpin->setValue(11);
    QCoreApplication::processEvents();
    {
        QSettings settings = map3DTestSettings();
        require(settings.value(QStringLiteral("heatMetric")).toInt() == 2,
                "heat metric setting is persisted");
        require(settings.value(QStringLiteral("heatPalette")).toInt() == 1,
                "heat palette setting is persisted");
        require(!settings.value(QStringLiteral("trackLineVisible"), true).toBool(),
                "track line visibility setting is persisted");
        require(!settings.value(QStringLiteral("trackPointsVisible"), true).toBool(),
                "track point visibility setting is persisted");
        require(settings.value(QStringLiteral("trackLineWidth")).toInt() == 9,
                "track line width setting is persisted");
        require(settings.value(QStringLiteral("trackPointSize")).toInt() == 11,
                "track point size setting is persisted");
    }
    require(heatLegendLabel->text().contains(QStringLiteral("温度")),
            "heat legend follows the selected metric");

    maxVisibleSamplesSpin->setValue(1000);
    {
        QSettings settings = map3DTestSettings();
        require(settings.value(QStringLiteral("maxVisibleSamples")).toInt() == 1000,
                "max visible sample setting is persisted");
    }

    followAction->setChecked(true);
    QCoreApplication::processEvents();
    {
        QSettings settings = map3DTestSettings();
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
    require(label->text().contains(QStringLiteral("Height ref assumed WGS84")),
            "status reports the explicit fallback for an unspecified height reference");
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
    require(waitForText(label, QStringLiteral("Source Session"), 3000),
            "asynchronous session load completes without blocking the window");
    {
        QSettings settings = map3DTestSettings();
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
    require(replaySlider->maximum() == 1000, "replay slider spans session time in milliseconds");
    require(label->text().contains(QStringLiteral("Replay paused 2/2")), "session status includes paused replay position");
    require(label->text().contains(QStringLiteral("t 1.000/1.000 s")), "session status includes replay time progress");

    replaySlider->setSliderDown(true);
    replaySlider->setValue(0);
    QCoreApplication::processEvents();
    replaySlider->setSliderDown(false);
    require(label->text().contains(QStringLiteral("Points: 1")), "slider previews replay sample");
    require(label->text().contains(QStringLiteral("Source Replay")), "slider preview reports replay source");
    require(label->text().contains(QStringLiteral("Replay stopped 1/2")), "slider updates replay state and position");
    require(label->text().contains(QStringLiteral("t 0.000/1.000 s")), "slider updates replay elapsed time");

    diagnosticsAction->trigger();
    QCoreApplication::processEvents();
    auto* diagnosticsText = window.findChild<QPlainTextEdit*>();
    require(diagnosticsText != nullptr, "diagnostics text view exists");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Track data:")),
            "diagnostics include track data section");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Earth load:")),
            "diagnostics include earth runtime load section");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Local native OSG building load:")),
            "diagnostics include local native OSG building load section");
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
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Native OSG building tiles contract:")),
            "diagnostics include native OSG building tile contract status");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Native OSG building tileset:")),
            "diagnostics include native OSG building tileset path");
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
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Replay time: t 0.000/1.000 s")),
            "diagnostics include replay elapsed time");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Attitude source: none")),
            "diagnostics include attitude source");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Follow aircraft: off")),
            "diagnostics include follow camera state");
    require(diagnosticsText->toPlainText().contains(QStringLiteral("Height safety note: vertical reference applied for display")),
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
        QSettings settings = map3DTestSettings();
        require(settings.value(QStringLiteral("replaySpeed")).toDouble() == 2.0,
                "replay speed setting is persisted");
    }

    replayAction->trigger();
    QCoreApplication::processEvents();
    require(replayAction->isChecked(), "replay action toggles into playing state");
    require(replayAction->text() == QStringLiteral("暂停"), "replay action text changes to pause");
    require(label->text().contains(QStringLiteral("Replay playing 1/2")),
            "status reports explicit playing replay state");
    require(waitForText(label, QStringLiteral("Replay paused 2/2"), 1500),
            "window replay crosses a sparse one-second sample gap without resetting elapsed time");

    replayStopAction->trigger();
    QCoreApplication::processEvents();
    require(!replayAction->isChecked(), "stop clears replay playing state");
    require(label->text().contains(QStringLiteral("Replay stopped 1/2")), "stop rewinds replay to first sample and reports stopped state");

    QTemporaryDir supersededSessionDir;
    QTemporaryDir latestSessionDir;
    writeSessionTrack(supersededSessionDir);
    writeSessionTrack(latestSessionDir);
    window.loadSessionDirectory(supersededSessionDir.path());
    window.loadSessionDirectory(latestSessionDir.path());
    require(waitForSessionDirectory(latestSessionDir.path(), 3000),
            "the latest asynchronous session request wins over an older completed request");
    require(waitForText(label, QStringLiteral("Source Session"), 1000),
            "the newest asynchronous session request applies its track on the GUI thread");

    auto* closingWindow = new VaporView::Map3D::Map3DWindow;
    closingWindow->loadSessionDirectory(latestSessionDir.path());
    delete closingWindow;
    QCoreApplication::processEvents();

    return 0;
}
