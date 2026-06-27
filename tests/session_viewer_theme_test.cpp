#include "AppTheme.h"
#include "SessionViewerWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QPalette>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QWidget>
#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
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

void testCsvViewportUsesNeutralBackground(SessionViewerWindow& viewer)
{
    auto *table = viewer.findChild<QTableWidget *>(QStringLiteral("sessionViewerCsvTable"));
    require(table != nullptr, "session viewer CSV table exists");
    require(table->viewport() != nullptr, "session viewer CSV viewport exists");

    const QColor base = table->viewport()->palette().color(QPalette::Base);
    require(base.lightness() > 180 || VaporView::isDarkThemeEnabled(), "CSV empty viewport uses a light neutral background");
    require(base.saturation() < 24 || VaporView::isDarkThemeEnabled(), "CSV empty viewport is not tinted blue");
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

    SessionViewerWindow viewer;
    viewer.resize(1280, 800);
    viewer.show();
    processEventsFor(300);

    testCsvViewportUsesNeutralBackground(viewer);
    testWaveformEmptyPlotIsNotRed(viewer);

    viewer.close();
    processEventsFor(100);
    std::cout << "session_viewer_theme_test passed\n";
    return 0;
}
