#include "geo/SessionTrackReader.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTextStream>
#include <iostream>

namespace {

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main()
{
    QTemporaryDir sessionDir;
    require(sessionDir.isValid(), "temporary session directory");

    QDir dir(sessionDir.path());
    require(dir.mkpath(QStringLiteral("sensors")), "create sensors directory");

    QFile csv(dir.filePath(QStringLiteral("sensors/devices.csv")));
    require(csv.open(QIODevice::WriteOnly | QIODevice::Text), "open devices.csv for writing");
    QTextStream out(&csv);
    out << "record_timestamp_us,device_timestamp_us,nav_lat_deg,nav_lon_deg,nav_height_m,gnss_satellites,hdop,fix_quality\n";
    out << "1000,900,31.230400001,121.473700001,18.2,12,0.7,fixed\n";
    out << "2000,1900,31.230500001,121.473800001,18.4,11,0.8,float\n";
    csv.close();

    const VaporView::Geo::SessionTrackReadResult result =
        VaporView::Geo::readSessionTrack(sessionDir.path());

    require(result.ok, "readSessionTrack ok");
    require(result.samples.size() == 2, "read two samples");
    require(result.samples.front().recordTimestampUs == 1000, "record timestamp parsed");
    require(result.samples.front().satellites == 12, "satellites parsed");
    require(result.samples.front().fixQuality == VaporView::Geo::FixQuality::Fixed, "fix quality parsed");

    return 0;
}
