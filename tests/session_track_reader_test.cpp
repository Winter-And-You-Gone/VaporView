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
                 QStringLiteral("host_time_us,epsilon_device_timestamp_us,epsilon_latitude_deg,epsilon_longitude_deg,epsilon_height_m,epsilon_ned_n_m,epsilon_ned_e_m,epsilon_ned_d_m,epsilon_yaw_deg,epsilon_quat_w,epsilon_quat_x,epsilon_quat_y,epsilon_quat_z,epsilon_gnss_satellites,epsilon_hdop,epsilon_gnss_fix_text\n"
                                "3000,2900,31.230600001,121.473900001,19.0,1.0,2.0,-3.0,45.5,0.9238795,0.0,0.0,0.3826834,14,0.6,RTK fixed\n"
                                "4000,3900,,,20.0,4.0,5.0,-6.0,46.0,1.0,0.0,0.0,0.0,14,0.6,RTK float\n"));

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
        require(sample.hasQuaternion(), "epsilon quaternion parsed");
        require(std::fabs(sample.quatW - 0.9238795) < 0.0000001, "epsilon quaternion W parsed");
        require(std::fabs(sample.quatZ - 0.3826834) < 0.0000001, "epsilon quaternion Z parsed");
        require(sample.fixQuality == VaporView::Geo::FixQuality::Fixed, "epsilon fix text parsed");
    }

    {
        QTemporaryDir sessionDir;
        require(sessionDir.isValid(), "temporary recorded GNSS session directory");

        QDir dir(sessionDir.path());
        require(dir.mkpath(QStringLiteral("sensors")), "create recorded GNSS sensors directory");

        writeCsv(dir.filePath(QStringLiteral("sensors/devices.csv")),
                 QStringLiteral("record_timestamp_us,nav_lat_deg,nav_lon_deg,nav_height_m,gnss_fix\n"
                                "4100,30.250000001,120.150000001,31.0,RTK_DUAL\n"
                                "4200,30.250100001,120.150100001,31.2,3D\n"));

        const VaporView::Geo::SessionTrackReadResult result =
            VaporView::Geo::readSessionTrack(sessionDir.path());

        require(result.ok, "recorded GNSS devices.csv read ok");
        require(result.samples.size() == 2, "recorded GNSS rows parsed");
        require(result.samples.front().fixQuality == VaporView::Geo::FixQuality::Fixed,
                "RTK_DUAL fix quality parsed as fixed");
        require(result.samples.back().fixQuality == VaporView::Geo::FixQuality::Single,
                "3D fix quality parsed as single");
    }

    {
        QTemporaryDir sessionDir;
        require(sessionDir.isValid(), "temporary legacy RTK session directory");

        QDir dir(sessionDir.path());
        require(dir.mkpath(QStringLiteral("sensors")), "create legacy sensors directory");

        writeCsv(dir.filePath(QStringLiteral("sensors/devices.csv")),
                 QStringLiteral("record_timestamp_us,rtk_timestamp_us,rtk_lat,rtk_lon,rtk_alt,rtk_fix,rtk_sat,rtk_heading,rtk_pitch,rtk_vel_n,rtk_vel_e,rtk_vel_d,imu_roll,imu_yaw\n"
                                "5000,4900,31.231000001,121.474000001,22.5,4,10,88.5,1.5,0.1,0.2,-0.3,-2.0,89.5\n"
                                "6000,5900,31.231100001,121.474100001,22.7,5,9,90.0,1.0,0.4,0.5,-0.6,-1.0,91.0\n"));

        const VaporView::Geo::SessionTrackReadResult result =
            VaporView::Geo::readSessionTrack(sessionDir.path());

        require(result.ok, "legacy RTK devices.csv read ok");
        require(result.samples.size() == 2, "legacy RTK rows parsed");
        const auto& first = result.samples.front();
        require(first.recordTimestampUs == 5000, "legacy record timestamp parsed");
        require(first.deviceTimestampUs == 4900, "legacy RTK timestamp parsed");
        require(std::fabs(first.latDeg - 31.231000001) < 0.000000001, "legacy RTK latitude parsed");
        require(std::fabs(first.lonDeg - 121.474000001) < 0.000000001, "legacy RTK longitude parsed");
        require(std::fabs(first.heightM - 22.5) < 0.000001, "legacy RTK altitude parsed");
        require(first.fixQuality == VaporView::Geo::FixQuality::Fixed, "legacy numeric RTK fixed parsed");
        require(first.satellites == 10, "legacy RTK satellite count parsed");
        require(std::fabs(first.yawDeg - 88.5) < 0.000001, "legacy RTK heading preferred as yaw");
        require(std::fabs(first.pitchDeg - 1.5) < 0.000001, "legacy RTK pitch parsed");
        require(std::fabs(first.rollDeg + 2.0) < 0.000001, "legacy IMU roll parsed");
        require(std::fabs(first.velNMps - 0.1) < 0.000001, "legacy RTK north velocity parsed");
        require(std::fabs(first.velEMps - 0.2) < 0.000001, "legacy RTK east velocity parsed");
        require(std::fabs(first.velDMps + 0.3) < 0.000001, "legacy RTK down velocity parsed");
        require(result.samples.back().fixQuality == VaporView::Geo::FixQuality::Float,
                "legacy numeric RTK float parsed");
    }

    return 0;
}
