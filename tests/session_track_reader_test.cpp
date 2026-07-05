#include "geo/SessionTrackReader.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTextStream>
#include <iostream>
#include <cmath>

namespace {

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void writeCsv(const QString& path, const QString& contents)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Text), "open devices.csv for writing");
    QTextStream out(&file);
    out << contents;
}

} // namespace

int main()
{
    {
        QTemporaryDir sessionDir;
        require(sessionDir.isValid(), "temporary session directory");

        QDir dir(sessionDir.path());
        require(dir.mkpath(QStringLiteral("sensors")), "create sensors directory");

        writeCsv(dir.filePath(QStringLiteral("sensors/devices.csv")),
                 QStringLiteral("record_timestamp_us,device_timestamp_us,nav_lat_deg,nav_lon_deg,nav_height_m,gnss_satellites,hdop,fix_quality\n"
                                "1000,900,31.230400001,121.473700001,18.2,12,0.7,fixed\n"
                                "2000,1900,31.230500001,121.473800001,18.4,11,0.8,float\n"));

        const VaporView::Geo::SessionTrackReadResult result =
            VaporView::Geo::readSessionTrack(sessionDir.path());

        require(result.ok, "readSessionTrack ok");
        require(result.samples.size() == 2, "read two samples");
        require(result.samples.front().recordTimestampUs == 1000, "record timestamp parsed");
        require(result.samples.front().satellites == 12, "satellites parsed");
        require(result.samples.front().fixQuality == VaporView::Geo::FixQuality::Fixed, "fix quality parsed");
    }

    {
        QTemporaryDir sessionDir;
        require(sessionDir.isValid(), "temporary epsilon session directory");

        QDir dir(sessionDir.path());
        require(dir.mkpath(QStringLiteral("nested/session/sensors")), "create nested sensors directory");

        writeCsv(dir.filePath(QStringLiteral("nested/session/sensors/devices.csv")),
                 QStringLiteral("host_time_us,epsilon_device_timestamp_us,epsilon_latitude_deg,epsilon_longitude_deg,epsilon_height_m,epsilon_ned_n_m,epsilon_ned_e_m,epsilon_ned_d_m,epsilon_yaw_deg,epsilon_gnss_satellites,epsilon_hdop,epsilon_gnss_fix_text\n"
                                "3000,2900,31.230600001,121.473900001,19.0,1.0,2.0,-3.0,45.5,14,0.6,RTK fixed\n"
                                "4000,3900,,,20.0,4.0,5.0,-6.0,46.0,14,0.6,RTK float\n"));

        const VaporView::Geo::SessionTrackReadResult result =
            VaporView::Geo::readSessionTrack(sessionDir.path());

        require(result.ok, "fallback devices.csv read ok");
        require(result.sourceCsvPath.endsWith(QStringLiteral("devices.csv")), "fallback source csv set");
        require(result.samples.size() == 1, "invalid LLH row skipped");
        const auto& sample = result.samples.front();
        require(sample.recordTimestampUs == 3000, "host_time_us parsed");
        require(sample.deviceTimestampUs == 2900, "epsilon device timestamp parsed");
        require(std::fabs(sample.latDeg - 31.230600001) < 0.000000001, "epsilon latitude parsed");
        require(std::fabs(sample.lonDeg - 121.473900001) < 0.000000001, "epsilon longitude parsed");
        require(std::fabs(sample.nedNM - 1.0) < 0.000001, "epsilon NED north parsed");
        require(std::fabs(sample.nedEM - 2.0) < 0.000001, "epsilon NED east parsed");
        require(std::fabs(sample.nedDM + 3.0) < 0.000001, "epsilon NED down parsed");
        require(std::fabs(sample.yawDeg - 45.5) < 0.000001, "epsilon yaw parsed");
        require(sample.fixQuality == VaporView::Geo::FixQuality::Fixed, "epsilon fix text parsed");
    }

    return 0;
}
