#include "geo/GeoTypes.h"
#include "map3d/Map3DWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
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

    return 0;
}
