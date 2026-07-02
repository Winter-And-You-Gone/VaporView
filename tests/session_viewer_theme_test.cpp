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
#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QMetaObject>
#include <QPalette>
#include <QProgressBar>
#include <QSettings>
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
                return qobject_cast<SessionViewerWindow *>(
                    QApplication::activeWindow()) != nullptr;
            }),
            "data viewer opens from main window action");

    auto *viewer = qobject_cast<SessionViewerWindow *>(QApplication::activeWindow());
    require(viewer != nullptr, "active data viewer is available");
    viewer->close();
    processEventsFor(200);

    require(QMetaObject::invokeMethod(&window, "onOpenSessionViewerClicked", Qt::DirectConnection),
            "main window can reopen data viewer after close");
    require(processEventsUntil(2000, []() {
                return qobject_cast<SessionViewerWindow *>(
                    QApplication::activeWindow()) != nullptr;
            }),
            "data viewer reopens from retained main window instance");
}

void testTrajectoryViewerUsesSidebarLayout()
{
    TrajectoryViewerDialog dialog;
    dialog.resize(1080, 680);
    dialog.show();
    processEventsFor(200);

    auto *sidebar = dialog.findChild<QWidget *>(QStringLiteral("trajectoryViewerSidebar"));
    auto *mapPanel = dialog.findChild<QWidget *>(QStringLiteral("trajectoryViewerMapPanel"));
    auto *map = dialog.findChild<QWidget *>(QStringLiteral("trajectoryViewerMap"));
    auto *mapSourceCombo = dialog.findChild<QComboBox *>(QStringLiteral("trajectoryMapSourceCombo"));

    require(sidebar != nullptr, "trajectory viewer sidebar exists");
    require(mapPanel != nullptr, "trajectory viewer map panel exists");
    require(map != nullptr, "trajectory viewer map exists");
    require(mapSourceCombo != nullptr, "trajectory viewer map source control exists");
    const auto sidebarToolButtons = sidebar->findChildren<QToolButton *>(QStringLiteral("titleBarButton"));
    require(sidebarToolButtons.size() >= 4, "trajectory viewer sidebar contains map tool buttons");

    require(sidebar->isAncestorOf(mapSourceCombo), "map source control is in sidebar");
    require(mapPanel->isAncestorOf(map), "map remains in the map panel");
    require(!sidebar->isAncestorOf(map), "map is not in sidebar");

    dialog.close();
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
