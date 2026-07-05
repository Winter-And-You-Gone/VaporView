#include "geo/GeoTypes.h"
#include "geo/SessionTrackReader.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTextStream>

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void writeCsv(const QString& sessionDir)
{
    QDir root(sessionDir);
    require(root.mkpath(QStringLiteral("sensors")), "create sensors directory");

    QFile csv(root.filePath(QStringLiteral("sensors/devices.csv")));
    require(csv.open(QIODevice::WriteOnly | QIODevice::Text), "open devices.csv");

    QTextStream out(&csv);
    out << "record_timestamp_us,nav_lat_deg,nav_lon_deg,nav_height_m,height_reference\n";
    out << "1000,31.2304,121.4737,18.0,wgs84 ellipsoid\n";
    out << "2000,31.2305,121.4738,19.0,mean sea level\n";
    out << "3000,31.2306,121.4739,20.0,EGM2008 geoid\n";
    out << "4000,31.2307,121.4740,21.0,local ned\n";
}

} // namespace

int main()
{
    using VaporView::Geo::HeightReference;

    require(HeightReference::Ellipsoid == HeightReference::Wgs84Ellipsoid,
            "legacy ellipsoid alias maps to WGS84 ellipsoid");
    require(HeightReference::Local == HeightReference::LocalNed,
            "legacy local alias maps to local NED");
    require(HeightReference::Dem == HeightReference::Egm2008,
            "legacy DEM alias maps to EGM2008 display reference");

    QTemporaryDir sessionDir;
    require(sessionDir.isValid(), "temporary session directory is valid");
    writeCsv(sessionDir.path());

    const VaporView::Geo::SessionTrackReadResult result =
        VaporView::Geo::readSessionTrack(sessionDir.path());
    require(result.ok, "session track reader accepts height reference CSV");
    require(result.samples.size() == 4, "four height reference samples parsed");
    require(result.samples[0].heightReference == HeightReference::Wgs84Ellipsoid,
            "WGS84 ellipsoid height reference parsed");
    require(result.samples[1].heightReference == HeightReference::MeanSeaLevel,
            "mean sea level height reference parsed");
    require(result.samples[2].heightReference == HeightReference::Egm2008,
            "EGM2008 height reference parsed");
    require(result.samples[3].heightReference == HeightReference::LocalNed,
            "local NED height reference parsed");

    return 0;
}
