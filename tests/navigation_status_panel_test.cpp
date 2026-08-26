#include "ground/navigation/NavigationStatusPanel.h"
#include "shared/theme/AppTheme.h"
#include "shared/theme/TopLevelCardStyle.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QSet>
#include <QSize>

#include <cstdlib>
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>

namespace
{
using VaporView::Ground::Navigation::NavigationStatusPanel;
using VaporView::Ground::Navigation::NavigationStatusSnapshot;

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void processEventsFor(int milliseconds)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

NavigationStatusSnapshot reliableSnapshot()
{
    NavigationStatusSnapshot snapshot;
    snapshot.epsilonOnline = true;
    snapshot.epsilonDataFresh = true;
    snapshot.epsilonDataAgeMs = 32;
    snapshot.navigationDataAvailable = true;
    snapshot.gnssFixText = QStringLiteral("RTK_FIXED");
    snapshot.filterStatusAvailable = true;
    snapshot.filterStatusBits = 0x0062;
    snapshot.updateStatusBits = 0x000c;
    snapshot.gnssQualityAvailable = true;
    snapshot.satelliteCount = 24;
    snapshot.horizontalAccuracyM = 0.015;
    snapshot.positionAvailable = true;
    snapshot.longitudeDeg = 120.14530;
    snapshot.latitudeDeg = 30.24620;
    snapshot.heightM = 42.5;
    snapshot.speedAvailable = true;
    snapshot.speedMps = 1.8;
    snapshot.attitudeAvailable = true;
    snapshot.rollDeg = 1.25;
    snapshot.pitchDeg = -0.75;
    snapshot.headingDeg = 128.5;
    snapshot.rtkServiceRunning = true;
    return snapshot;
}

QLabel *label(NavigationStatusPanel& panel, const char *objectName)
{
    return panel.findChild<QLabel *>(QString::fromLatin1(objectName));
}

QFrame *card(NavigationStatusPanel& panel, const char *objectName)
{
    return panel.findChild<QFrame *>(QString::fromLatin1(objectName));
}

void applyTheme(QApplication& app, bool dark)
{
    app.setProperty(VaporView::kAppDarkThemeProperty, dark);
    app.setPalette(VaporView::appThemePalette(dark));
    processEventsFor(30);
}

void requireHealthyStatusColor(NavigationStatusPanel& panel, bool dark)
{
    const QString expectedColor =
        VaporView::appThemeColorName(VaporView::AppThemeColor::HomeDeviceSuccess, dark);
    require(panel.styleSheet().contains(
                QStringLiteral("QLabel[navigationStatusKind=\"healthy\"] { color: %1; }")
                    .arg(expectedColor)),
            "status overview healthy values use the bright success color for the active theme");
}

void requireGrabHasContent(const QPixmap& pixmap, const char *message)
{
    require(!pixmap.isNull() && pixmap.width() > 0 && pixmap.height() > 0, message);
    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    QSet<QRgb> colors;
    const int xStep = std::max(1, image.width() / 80);
    const int yStep = std::max(1, image.height() / 60);
    for (int y = 0; y < image.height(); y += yStep)
    {
        for (int x = 0; x < image.width(); x += xStep)
        {
            colors.insert(image.pixel(x, y));
        }
    }
    require(colors.size() >= 8, message);
}

void saveOptionalGrab(const QPixmap& pixmap, const QString& fileName)
{
    const QString outputDirectory = qEnvironmentVariable("VAPORVIEW_NAV_STATUS_GRAB_DIR");
    if (outputDirectory.isEmpty())
    {
        return;
    }
    require(QDir().mkpath(outputDirectory), "grab output directory can be created");
    require(pixmap.save(QDir(outputDirectory).filePath(fileName)), "grab image can be saved");
}

void testStateVisibilityAndFormatting()
{
    NavigationStatusPanel panel;
    panel.resize(1100, 680);
    panel.show();
    processEventsFor(60);
    requireHealthyStatusColor(panel, false);

    QFrame *summaryCard = card(panel, "navigationStatusSummaryCard");
    QFrame *positionCard = card(panel, "navigationStatusPositionCard");
    QFrame *attitudeCard = card(panel, "navigationStatusAttitudeCard");
    QFrame *gnssCard = card(panel, "navigationStatusGnssCard");
    QFrame *differentialCard = card(panel, "navigationStatusDifferentialCard");
    QFrame *emptyState = card(panel, "navigationStatusEmptyState");
    require(summaryCard && positionCard && attitudeCard && gnssCard && differentialCard,
            "status dashboard exposes stable semantic card names");
    require(summaryCard->property(VaporView::kTopLevelCardProperty).toBool() &&
                positionCard->property(VaporView::kTopLevelCardProperty).toBool() &&
                attitudeCard->property(VaporView::kTopLevelCardProperty).toBool() &&
                gnssCard->property(VaporView::kTopLevelCardProperty).toBool() &&
                differentialCard->property(VaporView::kTopLevelCardProperty).toBool(),
            "status dashboard uses the shared top-level card system");
    require(positionCard->isVisible() && attitudeCard->isVisible() &&
                gnssCard->isVisible() && differentialCard->isVisible(),
            "no-data state keeps the four dashboard regions visible with unavailable values");
    require(emptyState != nullptr && emptyState->isVisible(),
            "no-data state provides an explicit compact trust boundary instead of a blank canvas");
    require(label(panel, "navigationStatusFixValue")->text() == QStringLiteral("--") &&
                label(panel, "navigationStatusNtripValue")->text() == QStringLiteral("--") &&
                label(panel, "navigationStatusRtcmValue")->text() == QStringLiteral("--"),
            "no-data state uses a single unavailable representation");
    require(panel.findChildren<QAbstractButton *>().isEmpty(),
            "read-only dashboard does not introduce interactive controls");

    const NavigationStatusSnapshot reliable = reliableSnapshot();
    panel.setSnapshot(reliable);
    processEventsFor(60);
    require(positionCard->isVisible() && attitudeCard->isVisible() && gnssCard->isVisible(),
            "reliable fields reveal their corresponding detail cards");
    require(emptyState->isHidden(),
            "reliable detail data removes the no-data state");
    require(differentialCard->isVisible() &&
                label(panel, "navigationStatusNtripValue")->text() == QStringLiteral("--") &&
                label(panel, "navigationStatusRtcmValue")->text() == QStringLiteral("--"),
            "differential card stays visible without inferring NTRIP or RTCM health");
    require(label(panel, "navigationStatusLongitudeValue")->text().endsWith(QStringLiteral("°")) &&
                label(panel, "navigationStatusHeightValue")->text().endsWith(QStringLiteral(" m")) &&
                label(panel, "navigationStatusSpeedValue")->text().endsWith(QStringLiteral(" m/s")) &&
                label(panel, "navigationStatusHorizontalAccuracyValue")->text().endsWith(QStringLiteral(" m")),
            "navigation values use stable units and precision formatting");
    require(label(panel, "navigationStatusFreshnessValue")->text().contains(QStringLiteral("32 ms")) &&
                label(panel, "navigationStatusGnssValue")->text() == QStringLiteral("RTK_FIXED") &&
                label(panel, "navigationStatusFixValue")->text() == QStringLiteral("定位融合中") &&
                label(panel, "navigationStatusGnssFixValue")->text() == QStringLiteral("RTK_FIXED") &&
                label(panel, "navigationStatusSatellitesValue")->text() == QStringLiteral("24"),
            "overview separates positioning status from the explicit GNSS fix state");

    NavigationStatusSnapshot stale = reliable;
    stale.epsilonDataFresh = false;
    panel.setSnapshot(stale);
    processEventsFor(30);
    require(label(panel, "navigationStatusEpsilonValue")->text() == QStringLiteral("○ 离线") &&
                label(panel, "navigationStatusFreshnessValue")->text() == QStringLiteral("--") &&
                label(panel, "navigationStatusGnssValue")->text() == QStringLiteral("--") &&
                label(panel, "navigationStatusLongitudeValue")->text() == QStringLiteral("--") &&
                label(panel, "navigationStatusRollValue")->text() == QStringLiteral("--") &&
                label(panel, "navigationStatusGnssFixValue")->text() == QStringLiteral("--") &&
                label(panel, "navigationStatusSatellitesValue")->text() == QStringLiteral("--") &&
                positionCard->isVisible() && attitudeCard->isVisible() && gnssCard->isVisible(),
            "stale snapshots never render cached navigation values");

    NavigationStatusSnapshot partial;
    partial.epsilonOnline = true;
    partial.epsilonDataFresh = true;
    partial.navigationDataAvailable = true;
    partial.gnssFixText = QStringLiteral("3D");
    panel.setSnapshot(partial);
    processEventsFor(30);
    require(gnssCard->isVisible() &&
                label(panel, "navigationStatusGnssValue")->text() == QStringLiteral("3D") &&
                label(panel, "navigationStatusFixValue")->text() == QStringLiteral("GNSS可用") &&
                label(panel, "navigationStatusGnssFixValue")->text() == QStringLiteral("3D") &&
                label(panel, "navigationStatusSatellitesValue")->text() == QStringLiteral("--") &&
                label(panel, "navigationStatusHorizontalAccuracyValue")->text() == QStringLiteral("--"),
            "partial GNSS data renders explicit fix state without inferring quality fields");

    NavigationStatusSnapshot invalid = reliable;
    invalid.positionAvailable = true;
    invalid.longitudeDeg = std::numeric_limits<double>::quiet_NaN();
    invalid.attitudeAvailable = true;
    invalid.rollDeg = std::numeric_limits<double>::infinity();
    invalid.speedAvailable = true;
    invalid.speedMps = -1.0;
    panel.setSnapshot(invalid);
    processEventsFor(30);
    require(positionCard->isVisible() && attitudeCard->isVisible() &&
                label(panel, "navigationStatusLongitudeValue")->text() == QStringLiteral("--") &&
                label(panel, "navigationStatusRollValue")->text() == QStringLiteral("--"),
            "invalid and non-finite navigation groups remain explicit unavailable placeholders");
    for (QLabel *value : panel.findChildren<QLabel *>())
    {
        if (!value->objectName().startsWith(QStringLiteral("navigationStatus")))
        {
            continue;
        }
        const QString lower = value->text().toLower();
        require(!lower.contains(QStringLiteral("nan")) &&
                    !lower.contains(QStringLiteral("inf")),
                "non-finite values never leak into dashboard text");
    }

    NavigationStatusSnapshot differential;
    differential.differentialAvailable = true;
    differential.ntripStatusText = QStringLiteral("Connected");
    panel.setSnapshot(differential);
    processEventsFor(30);
    require(differentialCard->isVisible() &&
                label(panel, "navigationStatusNtripValue")->text() == QStringLiteral("Connected") &&
                label(panel, "navigationStatusRtcmValue")->text() == QStringLiteral("--") &&
                label(panel, "navigationStatusDifferentialAgeValue")->text() == QStringLiteral("--"),
            "differential card supports partial explicit state without inferring missing health");
}

void testResponsiveGeometry()
{
    NavigationStatusPanel panel;
    panel.setSnapshot(reliableSnapshot());
    panel.resize(1100, 720);
    panel.show();
    processEventsFor(80);
    QFrame *positionCard = card(panel, "navigationStatusPositionCard");
    QFrame *attitudeCard = card(panel, "navigationStatusAttitudeCard");
    QFrame *gnssCard = card(panel, "navigationStatusGnssCard");
    require(positionCard->geometry().top() == attitudeCard->geometry().top(),
            "wide dashboard keeps position and attitude side by side");

    panel.resize(620, 820);
    processEventsFor(80);
    require(attitudeCard->geometry().top() > positionCard->geometry().bottom() &&
                gnssCard->width() >= 500,
            "narrow dashboard stacks detail cards without clipping their content");
}

void testGrabs(QApplication& app)
{
    const std::array<bool, 2> themes{{false, true}};
    for (bool dark : themes)
    {
        applyTheme(app, dark);
        for (bool withData : themes)
        {
            NavigationStatusPanel panel;
            requireHealthyStatusColor(panel, dark);
            panel.setSnapshot(withData ? reliableSnapshot() : NavigationStatusSnapshot{});
            panel.resize(withData ? QSize(1180, 700) : QSize(820, 560));
            panel.show();
            processEventsFor(100);
            const QPixmap pixmap = panel.grab();
            requireGrabHasContent(pixmap, "QWidget grab contains rendered dashboard pixels");
            saveOptionalGrab(
                pixmap,
                QStringLiteral("navigation-status-%1-%2.png")
                    .arg(dark ? QStringLiteral("dark") : QStringLiteral("light"),
                         withData ? QStringLiteral("data") : QStringLiteral("no-data")));
        }
    }
    applyTheme(app, false);
}

} // namespace

int main(int argc, char **argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("VaporViewNavigationStatusPanelTest"));
    app.setApplicationName(QStringLiteral("navigation_status_panel_test"));
    applyTheme(app, false);

    testStateVisibilityAndFormatting();
    testResponsiveGeometry();
    testGrabs(app);

    std::cout << "navigation_status_panel_test passed\n";
    return 0;
}
