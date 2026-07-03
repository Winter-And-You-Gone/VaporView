#include "AppTheme.h"
#include "MainWindow.h"
#include "RawDataParserWindow.h"
#include "SessionViewerWindow.h"
#include "TrajectoryViewerDialog.h"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QMargins>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QWidget>
#include <cstdlib>
#include <functional>
#include <iostream>

namespace
{

constexpr char kTestRawMagic[8] = {'V', 'V', 'R', 'A', 'W', 'D', 'A', 'T'};
constexpr quint32 kTestRawRecordMarker = 0x44525756u;
constexpr quint16 kTestRawSourceEpsilon = 1u;
constexpr quint16 kTestRawHeaderSize = 20u;
constexpr quint16 kTestRawRecordHeaderSize = 36u;

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool processEventsUntil(int timeoutMs, const std::function<bool()>& condition)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        if (condition())
        {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return condition();
}

void processEventsFor(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

void clickWidgetCenterThroughWindow(QWidget *widget, int waitMs = 250)
{
    require(widget != nullptr, "click target exists");
    const QPoint globalCenter = widget->mapToGlobal(widget->rect().center());
    QWidget *target = QApplication::widgetAt(globalCenter);
    if (!target)
    {
        target = widget;
    }
    const QPoint localPos = target->mapFromGlobal(globalCenter);
    QMouseEvent press(QEvent::MouseButtonPress,
                      localPos,
                      globalCenter,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(target, &press);
    QMouseEvent release(QEvent::MouseButtonRelease,
                        localPos,
                        globalCenter,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    QCoreApplication::sendEvent(target, &release);
    if (waitMs > 0)
    {
        processEventsFor(waitMs);
    }
}

SessionViewerWindow *visibleSessionViewerWindow()
{
    for (QWidget *widget : QApplication::topLevelWidgets())
    {
        auto *viewer = qobject_cast<SessionViewerWindow *>(widget);
        if (viewer && viewer->isVisible() && !viewer->isMinimized())
        {
            return viewer;
        }
    }
    return nullptr;
}

int countRedDominantPixels(const QImage& image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            const QColor color = image.pixelColor(x, y);
            if (color.red() > color.green() + 24 && color.red() > color.blue() + 24)
            {
                ++count;
            }
        }
    }
    return count;
}

void writeUnifiedRawFile(const QString& path, int recordCount)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "temporary raw file can be written");

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData(kTestRawMagic, sizeof(kTestRawMagic));
    stream << quint32(2) << quint32(kTestRawHeaderSize) << quint16(kTestRawSourceEpsilon) << quint16(0);

    const QByteArray payload("TEST");
    for (int i = 0; i < recordCount; ++i)
    {
        stream << quint32(kTestRawRecordMarker)
               << quint32(kTestRawRecordHeaderSize)
               << quint64(1'700'000'000'000'000ULL + static_cast<quint64>(i))
               << quint32(payload.size())
               << quint16(kTestRawSourceEpsilon)
               << quint16(0x40)
               << quint32(0)
               << quint64(i);
        stream.writeRawData(payload.constData(), payload.size());
    }
}

void writeMinimalTrajectorySession(const QString& sessionPath)
{
    QDir dir(sessionPath);
    require(dir.mkpath(QStringLiteral("sensors")), "temporary trajectory sensors directory can be created");

    QFile metadata(dir.filePath(QStringLiteral("session.json")));
    require(metadata.open(QIODevice::WriteOnly | QIODevice::Text), "temporary trajectory metadata can be written");
    metadata.write(R"json({
  "session_name": "trajectory_test_session",
  "start_time_utc": "2026-06-26T06:33:55.573Z",
  "end_time_utc": "2026-06-26T06:33:58.573Z",
  "sensor_rows": "4",
  "sensor_export_rate_hz": 1,
  "waveform_frames": "0",
  "waveform_points_per_frame": 16,
  "waveform_export_rate_hz": 0,
  "waveform_export_mode": "per_frame",
  "paths": {
    "devices_csv": "sensors/devices.csv",
    "waveform_directory": "waveform",
    "waveform_index": "waveform_index.csv"
  },
  "raw_files": {
    "tcp_wave": {
      "path": "raw/tcp_wave.dat"
    }
  }
})json");
    metadata.close();

    QFile sensors(dir.filePath(QStringLiteral("sensors/devices.csv")));
    require(sensors.open(QIODevice::WriteOnly | QIODevice::Text), "temporary trajectory CSV can be written");
    QTextStream stream(&sensors);
    stream << "record_timestamp_us,epsilon_valid,gnss_fix,nav_lat_deg,nav_lon_deg,nav_height_m\n";
    stream << "1782446035573000,true,RTK_FIXED,30.13698120,120.06938175,9.606\n";
    stream << "1782446036573000,true,RTK_FIXED,30.13712000,120.06952000,9.806\n";
    stream << "1782446037573000,true,RTK_FIXED,30.13736000,120.06972000,10.106\n";
    stream << "1782446038573000,true,RTK_FIXED,30.13762000,120.06990710,11.190\n";
}

void testRawDataParserOpenIsNonBlocking()
{
    QTemporaryDir sessionDir;
    require(sessionDir.isValid(), "temporary raw parser session directory");
    QDir dir(sessionDir.path());
    require(dir.mkpath(QStringLiteral("raw")), "temporary raw directory can be created");
    writeUnifiedRawFile(dir.filePath(QStringLiteral("raw/epsilon.dat")), 300000);

    RawDataParserWindow parser;
    parser.setEnglish(false);
    parser.show();
    processEventsFor(50);

    QElapsedTimer elapsed;
    elapsed.start();
    require(parser.openSessionPath(sessionDir.path()), "raw parser accepts temporary session directory");
    require(elapsed.elapsed() < 250, "raw parser starts indexing without blocking the UI thread");

    auto *progressBar = parser.findChild<QProgressBar *>(QStringLiteral("rawDataParserProgressBar"));
    require(progressBar != nullptr, "raw parser exposes an indexing progress bar");
    require(progressBar->isVisible(), "raw parser shows indexing progress while loading");

    auto *statusLabel = parser.findChild<QLabel *>(QStringLiteral("rawDataParserStatusLabel"));
    require(statusLabel != nullptr, "raw parser exposes a status label");
    require(processEventsUntil(10000, [statusLabel]() {
                const QString text = statusLabel->text();
                return text.contains(QStringLiteral("建立 300000 条")) ||
                       text.contains(QStringLiteral("Indexed 300000"));
            }),
            "raw parser background indexing completes");

    parser.close();
    processEventsFor(100);
}

void testCsvViewportUsesNeutralBackground(SessionViewerWindow& viewer)
{
    auto *table = viewer.findChild<QTableWidget *>(QStringLiteral("sessionViewerCsvTable"));
    require(table != nullptr, "session viewer CSV table exists");
    require(table->viewport() != nullptr, "session viewer CSV viewport exists");

    const QColor base = table->viewport()->palette().color(QPalette::Base);
    require(base.lightness() > 180 || VaporView::isDarkThemeEnabled(), "CSV empty viewport uses a light neutral background");
    require(base.saturation() < 24 || VaporView::isDarkThemeEnabled(), "CSV empty viewport is not tinted blue");
}

void testCsvSelectionUsesThemeAccent(SessionViewerWindow& viewer, bool dark)
{
    auto *table = viewer.findChild<QTableWidget *>(QStringLiteral("sessionViewerCsvTable"));
    require(table != nullptr, "session viewer CSV table exists for selection theme");

    const QColor actualHighlight = table->palette().color(QPalette::Highlight);
    const QColor expectedHighlight = VaporView::appThemeColor(VaporView::AppThemeColor::Primary, dark);
    require(actualHighlight.name() == expectedHighlight.name(),
            dark ? "CSV dark selection uses orange theme accent"
                 : "CSV light selection uses blue theme accent");

    const QColor actualText = table->palette().color(QPalette::HighlightedText);
    const QColor expectedText = dark
        ? VaporView::appThemeColor(VaporView::AppThemeColor::TableText, false)
        : VaporView::appThemeColor(VaporView::AppThemeColor::TextInverse, false);
    require(actualText.name() == expectedText.name(), "CSV selection text keeps contrast against accent background");

    if (dark)
    {
        require(actualHighlight.red() > actualHighlight.blue() + 24,
                "CSV dark selection accent is orange-tinted");
    }
    else
    {
        require(actualHighlight.blue() > actualHighlight.red() + 48,
                "CSV light selection accent is blue-tinted");
    }
}

void testWaveformEmptyPlotIsNotRed(SessionViewerWindow& viewer)
{
    auto *plot = viewer.findChild<QWidget *>(QStringLiteral("sessionViewerWaveformPlot"));
    require(plot != nullptr, "session viewer waveform plot exists");
    require(plot->width() > 0 && plot->height() > 0, "session viewer waveform plot has size");

    const QImage image = plot->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    require(!image.isNull(), "session viewer waveform plot renders");
    const int redDominantPixels = countRedDominantPixels(image);
    const int totalPixels = image.width() * image.height();
    require(redDominantPixels < totalPixels / 100, "session viewer waveform grid is not red-dominant");
}

void requireSessionViewerTitleBarWindowButtonsWork(SessionViewerWindow& viewer)
{
    auto *minimizeButton = viewer.findChild<QToolButton *>(QStringLiteral("windowMinimizeButton"));
    auto *maximizeButton = viewer.findChild<QToolButton *>(QStringLiteral("windowMaximizeButton"));
    require(minimizeButton != nullptr, "data viewer minimize button exists");
    require(maximizeButton != nullptr, "data viewer maximize button exists");
    require(minimizeButton->isEnabled(), "data viewer minimize button is enabled");
    require(maximizeButton->isEnabled(), "data viewer maximize button is enabled");

    clickWidgetCenterThroughWindow(maximizeButton);
    require(processEventsUntil(1000, [&viewer]() {
                return viewer.isMaximized() ||
                       viewer.windowState().testFlag(Qt::WindowMaximized);
            }),
            "data viewer maximize button maximizes window");

    clickWidgetCenterThroughWindow(maximizeButton);
    require(processEventsUntil(1000, [&viewer]() {
                return !viewer.isMaximized() &&
                       !viewer.windowState().testFlag(Qt::WindowMaximized);
            }),
            "data viewer maximize button restores window");

    clickWidgetCenterThroughWindow(minimizeButton);
    require(processEventsUntil(1000, [&viewer]() {
                return viewer.isMinimized() ||
                       viewer.windowState().testFlag(Qt::WindowMinimized);
            }),
            "data viewer minimize button minimizes window");

    viewer.showNormal();
    processEventsFor(200);
}

void testMainWindowDataViewerOpenCanReopen()
{
    QTemporaryDir sessionDir;
    require(sessionDir.isValid(), "temporary session directory for data viewer startup");

    {
        QSettings settings(QStringLiteral("VaporView"), QStringLiteral("SessionViewer"));
        settings.setValue(QStringLiteral("last_session_directory"), sessionDir.path());
    }

    MainWindow window;
    window.resize(1280, 800);
    window.show();
    processEventsFor(300);

    require(QMetaObject::invokeMethod(&window, "onOpenSessionViewerClicked", Qt::DirectConnection),
            "main window can invoke data viewer action");
    require(processEventsUntil(2000, []() {
                return visibleSessionViewerWindow() != nullptr;
            }),
            "data viewer opens from main window action");

    auto *viewer = visibleSessionViewerWindow();
    require(viewer != nullptr, "active data viewer is available");
    requireSessionViewerTitleBarWindowButtonsWork(*viewer);
    auto *minimizeButton = viewer->findChild<QToolButton *>(QStringLiteral("windowMinimizeButton"));
    require(minimizeButton != nullptr, "data viewer minimize button exists before reopen");
    clickWidgetCenterThroughWindow(minimizeButton);
    require(processEventsUntil(1000, [viewer]() {
                return viewer->isMinimized() ||
                       viewer->windowState().testFlag(Qt::WindowMinimized);
            }),
            "data viewer is minimized before reopen action");
    require(QMetaObject::invokeMethod(&window, "onOpenSessionViewerClicked", Qt::DirectConnection),
            "main window can invoke data viewer action while viewer is minimized");
    require(processEventsUntil(2000, [viewer]() {
                return viewer->isVisible() &&
                       !viewer->isMinimized() &&
                       !viewer->windowState().testFlag(Qt::WindowMinimized);
            }),
            "data viewer action restores minimized retained window");
    viewer->close();
    processEventsFor(200);

    require(QMetaObject::invokeMethod(&window, "onOpenSessionViewerClicked", Qt::DirectConnection),
            "main window can reopen data viewer after close");
    require(processEventsUntil(2000, []() {
                return visibleSessionViewerWindow() != nullptr;
            }),
            "data viewer reopens from retained main window instance");
}

void testSessionViewerTrajectoryActionLifetime()
{
    QTemporaryDir sessionDir;
    require(sessionDir.isValid(), "temporary trajectory session directory");
    writeMinimalTrajectorySession(sessionDir.path());

    SessionViewerWindow viewer;
    viewer.resize(1280, 800);
    viewer.show();
    processEventsFor(300);

    require(viewer.openSessionPath(sessionDir.path()), "session viewer loads temporary trajectory session");
    processEventsFor(300);
    require(QMetaObject::invokeMethod(&viewer, "onViewTrajectoryClicked", Qt::DirectConnection),
            "session viewer invokes trajectory viewer action");

    TrajectoryViewerDialog *dialog = nullptr;
    require(processEventsUntil(2000, [&dialog]() {
                for (QWidget *widget : QApplication::topLevelWidgets())
                {
                    dialog = qobject_cast<TrajectoryViewerDialog *>(widget);
                    if (dialog && dialog->isVisible())
                    {
                        return true;
                    }
                }
                return false;
            }),
            "trajectory viewer opens from loaded data viewer session");

    dialog->close();
    processEventsFor(700);
    viewer.close();
    processEventsFor(100);
}

void testTrajectoryViewerUsesSidebarLayout()
{
    TrajectoryViewerDialog dialog;
    dialog.resize(1080, 680);
    dialog.show();
    processEventsFor(200);

    require(dialog.objectName() == QStringLiteral("trajectoryViewerDialog"),
            "trajectory viewer dialog has scoped style object name");
    auto *sidebar = dialog.findChild<QWidget *>(QStringLiteral("trajectoryViewerSidebar"));
    auto *sidebarCard = dialog.findChild<QFrame *>(QStringLiteral("trajectoryViewerSidebarCard"));
    auto *sidebarTitleBar = sidebarCard
        ? sidebarCard->findChild<QWidget *>(QStringLiteral("sectionTitleBar"))
        : nullptr;
    auto *sidebarIcon = sidebarTitleBar
        ? sidebarTitleBar->findChild<QLabel *>(QStringLiteral("trajectorySidebarTitleIcon"))
        : nullptr;
    auto *sidebarTitle = sidebarTitleBar
        ? sidebarTitleBar->findChild<QLabel *>(QStringLiteral("sectionTitleLabel"))
        : nullptr;
    auto *sidebarContent = dialog.findChild<QWidget *>(QStringLiteral("trajectoryViewerSidebarContent"));
    auto *mapPanel = dialog.findChild<QFrame *>(QStringLiteral("trajectoryViewerMapPanel"));
    auto *map = dialog.findChild<QWidget *>(QStringLiteral("trajectoryViewerMap"));
    auto *mapSourceCombo = dialog.findChild<QComboBox *>(QStringLiteral("trajectoryMapSourceCombo"));
    auto *heatLegendCard = dialog.findChild<QFrame *>(QStringLiteral("trajectoryHeatLegendCard"));
    auto *mapToolsCard = dialog.findChild<QFrame *>(QStringLiteral("trajectoryMapToolsCard"));
    auto *heatLegendTitle = dialog.findChild<QLabel *>(QStringLiteral("trajectoryHeatLegendTitle"));
    auto *heatGradientBar = dialog.findChild<QWidget *>(QStringLiteral("trajectoryHeatGradientBar"));
    auto *heatPaletteCombo = dialog.findChild<QComboBox *>(QStringLiteral("trajectoryHeatPaletteCombo"));
    auto *trackWidthSlider = dialog.findChild<QSlider *>(QStringLiteral("trajectoryTrackWidthSlider"));
    auto *pointSizeSlider = dialog.findChild<QSlider *>(QStringLiteral("trajectoryPointSizeSlider"));
    const auto visibilityToggles = dialog.findChildren<QPushButton *>(QStringLiteral("trajectoryVisibilityToggle"));
    QPushButton *showRouteButton = nullptr;
    QPushButton *showPointsButton = nullptr;
    for (QPushButton *toggle : visibilityToggles)
    {
        if (toggle->property("visibilityRole").toString() == QStringLiteral("route"))
        {
            showRouteButton = toggle;
        }
        else if (toggle->property("visibilityRole").toString() == QStringLiteral("points"))
        {
            showPointsButton = toggle;
        }
    }
    auto *summaryLabel = dialog.findChild<QLabel *>(QStringLiteral("trajectorySidebarSummaryLabel"));
    auto *detailLabel = dialog.findChild<QLabel *>(QStringLiteral("trajectorySidebarDetailLabel"));

    require(sidebarCard != nullptr, "trajectory viewer sidebar card exists");
    require(sidebarTitleBar != nullptr, "trajectory viewer sidebar title bar exists");
    require(sidebarIcon != nullptr, "trajectory viewer sidebar title icon exists");
    require(sidebarTitle != nullptr, "trajectory viewer sidebar title label exists");
    require(sidebarContent != nullptr, "trajectory viewer sidebar body exists");
    require(sidebar != nullptr, "trajectory viewer sidebar exists");
    require(mapPanel != nullptr, "trajectory viewer map panel exists");
    require(map != nullptr, "trajectory viewer map exists");
    require(mapSourceCombo != nullptr, "trajectory viewer map source control exists");
    require(heatLegendCard != nullptr, "trajectory viewer floating heat legend card exists");
    require(mapToolsCard != nullptr, "trajectory viewer floating map tools card exists");
    require(heatLegendTitle != nullptr, "trajectory viewer floating heat legend title exists");
    require(heatGradientBar != nullptr, "trajectory viewer floating heat gradient bar exists");
    require(heatPaletteCombo != nullptr, "trajectory viewer heat palette control exists");
    require(trackWidthSlider != nullptr, "trajectory viewer track width control exists");
    require(pointSizeSlider != nullptr, "trajectory viewer point size control exists");
    require(showRouteButton != nullptr, "trajectory viewer route visibility toggle exists");
    require(showPointsButton != nullptr, "trajectory viewer point visibility toggle exists");
    require(summaryLabel != nullptr, "trajectory viewer summary label exists");
    require(detailLabel != nullptr, "trajectory viewer detail label exists");
    const auto sidebarToolButtons = sidebar->findChildren<QToolButton *>(QStringLiteral("titleBarButton"));
    require(sidebarToolButtons.size() >= 4, "trajectory viewer sidebar contains map tool buttons");
    const auto sidebarActionButtons = sidebar->findChildren<QPushButton *>(QStringLiteral("trajectorySidebarActionButton"));
    require(sidebarActionButtons.size() == 3, "trajectory viewer sidebar contains styled action buttons");

    require(sidebarCard->isAncestorOf(sidebarTitleBar), "sidebar title bar is inside card");
    require(sidebarCard->isAncestorOf(sidebar), "sidebar body scroll area is inside card");
    require(sidebar->isAncestorOf(sidebarContent), "sidebar content is inside scroll body");
    require(sidebarCard->layout() != nullptr, "sidebar card layout exists");
    require(sidebarCard->layout()->contentsMargins() == QMargins(1, 1, 1, 1),
            "sidebar card preserves visible rounded border");
    require(sidebarTitleBar->minimumHeight() == sidebarTitleBar->maximumHeight()
                && sidebarTitleBar->minimumHeight() >= 40,
            "sidebar title bar keeps home card fixed height");
    require(sidebarContent->layout() != nullptr, "sidebar content layout exists");
    require(sidebarContent->layout()->contentsMargins() == QMargins(12, 12, 12, 12),
            "sidebar body uses home card interior padding");
    require(mapSourceCombo->minimumWidth() == 160, "map source combo keeps readable minimum width");
    require(mapSourceCombo->sizePolicy().horizontalPolicy() == QSizePolicy::Expanding,
            "map source combo expands within sidebar controls");
    require(sidebar->isAncestorOf(mapSourceCombo), "map source control is in sidebar");
    require(heatPaletteCombo->minimumWidth() >= 132 && heatPaletteCombo->maximumWidth() <= 156,
            "heat palette combo keeps compact floating-card width");
    require(heatPaletteCombo->sizePolicy().horizontalPolicy() == QSizePolicy::Fixed,
            "heat palette combo stays compact inside floating heat legend");
    require(heatPaletteCombo->count() >= 5, "heat palette combo exposes multiple vivid ramps");
    require(map->isAncestorOf(heatLegendCard), "heat legend card floats inside the map");
    require(heatLegendCard->isAncestorOf(heatPaletteCombo), "heat palette control is inside floating heat legend");
    require(!sidebar->isAncestorOf(heatPaletteCombo), "heat palette control is no longer in sidebar");
    require(trackWidthSlider->minimum() == 10 && trackWidthSlider->maximum() == 80,
            "track width slider exposes a bounded visual range");
    require(pointSizeSlider->minimum() == 20 && pointSizeSlider->maximum() == 120,
            "point size slider exposes a bounded visual range");
    require(map->isAncestorOf(mapToolsCard), "map tools card floats inside the map");
    require(mapToolsCard->isAncestorOf(trackWidthSlider), "track width control is in floating map tools card");
    require(mapToolsCard->isAncestorOf(pointSizeSlider), "point size control is in floating map tools card");
    require(mapToolsCard->isAncestorOf(showRouteButton), "route visibility toggle is in floating map tools card");
    require(mapToolsCard->isAncestorOf(showPointsButton), "point visibility toggle is in floating map tools card");
    require(!sidebar->isAncestorOf(trackWidthSlider), "track width control is no longer in sidebar");
    require(!sidebar->isAncestorOf(pointSizeSlider), "point size control is no longer in sidebar");
    require(showRouteButton->isCheckable() && showRouteButton->isChecked(),
            "route visibility is enabled by default");
    require(showPointsButton->isCheckable() && showPointsButton->isChecked(),
            "point visibility is enabled by default");
    require(showRouteButton->text().isEmpty() && showPointsButton->text().isEmpty(),
            "visibility toggles use icon-only buttons");
    require(!showRouteButton->icon().isNull() && !showPointsButton->icon().isNull(),
            "visibility toggles use lucide icons");
    require(mapPanel->isAncestorOf(map), "map remains in the map panel");
    require(!sidebar->isAncestorOf(map), "map is not in sidebar");
    require(mapPanel->layout() != nullptr, "map panel layout exists");
    require(mapPanel->layout()->contentsMargins() == QMargins(0, 0, 0, 0),
            "map panel lets the map fill the rounded card");
    require(mapPanel->layout()->spacing() == 0,
            "map panel has no fixed legend spacing above the map");
    require(sidebarCard->geometry().left() < mapPanel->geometry().left(),
            "trajectory sidebar card is positioned to the left of the map panel");
    require(sidebarCard->geometry().top() == mapPanel->geometry().top(),
            "trajectory sidebar card and map panel share the same row");
    require(map->height() >= 300, "trajectory map keeps usable vertical space");
    const QRect mapPanelContents = mapPanel->contentsRect();
    require(std::abs(map->geometry().left() - mapPanelContents.left()) <= 2
                && std::abs(map->geometry().top() - mapPanelContents.top()) <= 2,
            "trajectory map starts at the map panel content origin");
    require(std::abs(map->width() - mapPanelContents.width()) <= 2
                && std::abs(map->height() - mapPanelContents.height()) <= 2,
            "trajectory map fills the whole rounded map panel");
    const QString styleSheet = dialog.styleSheet();
    require(styleSheet.contains(QStringLiteral("QDialog#trajectoryViewerDialog")),
            "trajectory viewer stylesheet is scoped to dialog");
    require(styleSheet.contains(QStringLiteral("QFrame#trajectoryViewerSidebarCard")),
            "trajectory viewer stylesheet includes sidebar card styling");
    require(styleSheet.contains(QStringLiteral("QWidget#sectionTitleBar")),
            "trajectory viewer stylesheet includes home-style section title bar");
    require(styleSheet.contains(QStringLiteral("QPushButton#trajectorySidebarActionButton")),
            "trajectory viewer stylesheet includes sidebar action button styling");
    require(styleSheet.contains(QStringLiteral("QLabel#trajectorySidebarTitleIcon")),
            "trajectory viewer stylesheet includes sidebar title icon styling");
    require(styleSheet.contains(QStringLiteral("QLabel#trajectoryControlLabel")),
            "trajectory viewer stylesheet includes route control label styling");
    require(styleSheet.contains(QStringLiteral("QFrame#trajectoryHeatLegendCard")),
            "trajectory viewer stylesheet includes floating heat legend styling");
    require(styleSheet.contains(QStringLiteral("QFrame#trajectoryMapToolsCard")),
            "trajectory viewer stylesheet includes floating map tools styling");
    require(styleSheet.contains(QStringLiteral("QPushButton#trajectoryVisibilityToggle")),
            "trajectory viewer stylesheet includes visibility toggle styling");
    require(styleSheet.contains(QStringLiteral("QFrame#trajectoryViewerMapPanel")),
            "trajectory viewer stylesheet includes rounded map panel styling");

    RtkTrackStats stats;
    stats.scanned_rows = 3;
    stats.accepted_points = 2;
    stats.rejected_invalid_nav = 1;
    RtkTrackPoint firstPoint;
    firstPoint.latitude = 30.13698120;
    firstPoint.longitude = 120.06938175;
    firstPoint.height_m = 9.606;
    firstPoint.cumulative_distance_m = 0.0;
    firstPoint.speed_mps = 3.2;
    firstPoint.timestamp_us = 1782446035573000ULL;
    firstPoint.peak_value = 0.380270f;
    firstPoint.csv_row = 0;
    firstPoint.waveform_frame_index = 0;
    firstPoint.waveform_delta_us = 13929;
    firstPoint.has_height = true;
    firstPoint.has_speed = true;
    firstPoint.has_peak_value = true;
    firstPoint.has_waveform_match = true;
    RtkTrackPoint secondPoint = firstPoint;
    secondPoint.latitude = 30.1376200;
    secondPoint.longitude = 120.0699071;
    secondPoint.height_m = 11.190;
    secondPoint.cumulative_distance_m = 448.82;
    secondPoint.speed_mps = 4.1;
    secondPoint.csv_row = 1;
    secondPoint.peak_value = 0.420000f;
    dialog.setTrackStats(stats);
    dialog.setTrackPoints({firstPoint, secondPoint});
    processEventsFor(200);

    require(heatLegendCard->isVisible(), "heat legend card is visible when peak samples exist");
    require(heatGradientBar->isVisible(), "heat gradient bar is visible when peak samples exist");
    require(heatLegendCard->geometry().left() < mapToolsCard->geometry().left(),
            "floating heat legend stays to the left of map tools card");
    require(std::abs(heatLegendCard->geometry().top() - mapToolsCard->geometry().top()) <= 2,
            "floating heat legend and map tools card share the same top row");

    require(summaryLabel->textFormat() == Qt::RichText, "trajectory summary uses rich text");
    require(detailLabel->textFormat() == Qt::RichText, "trajectory detail uses rich text");
    require(summaryLabel->text().contains(QStringLiteral("<table")),
            "trajectory summary fields are formatted as a table");
    require(detailLabel->text().contains(QStringLiteral("<table")),
            "trajectory detail fields are formatted as a table");
    require(!detailLabel->text().contains(QStringLiteral(" | ")),
            "trajectory detail no longer uses a dense pipe-delimited sentence");

    dialog.close();
    processEventsFor(100);
}

void testSessionViewerTitleBarWindowButtons()
{
    SessionViewerWindow viewer;
    viewer.resize(1280, 800);
    viewer.show();
    processEventsFor(300);

    requireSessionViewerTitleBarWindowButtonsWork(viewer);
    viewer.close();
    processEventsFor(100);
}

}  // namespace

int main(int argc, char **argv)
{
    QTemporaryDir settingsDir;
    require(settingsDir.isValid(), "temporary settings directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("VaporViewSessionViewerThemeTest"));
    app.setApplicationName(QStringLiteral("session_viewer_theme_test"));
    app.setProperty(VaporView::kAppDarkThemeProperty, false);
    app.setPalette(VaporView::appThemePalette(false));

    testRawDataParserOpenIsNonBlocking();
    testMainWindowDataViewerOpenCanReopen();
    testSessionViewerTitleBarWindowButtons();
    testSessionViewerTrajectoryActionLifetime();
    testTrajectoryViewerUsesSidebarLayout();

    {
        SessionViewerWindow viewer;
        viewer.resize(1280, 800);
        viewer.show();
        processEventsFor(300);

        testCsvViewportUsesNeutralBackground(viewer);
        testCsvSelectionUsesThemeAccent(viewer, false);
        testWaveformEmptyPlotIsNotRed(viewer);

        viewer.close();
        processEventsFor(100);
    }

    app.setProperty(VaporView::kAppDarkThemeProperty, true);
    app.setPalette(VaporView::appThemePalette(true));
    {
        SessionViewerWindow viewer;
        viewer.resize(1280, 800);
        viewer.show();
        processEventsFor(300);

        testCsvSelectionUsesThemeAccent(viewer, true);

        viewer.close();
        processEventsFor(100);
    }

    std::cout << "session_viewer_theme_test passed\n";
    return 0;
}
