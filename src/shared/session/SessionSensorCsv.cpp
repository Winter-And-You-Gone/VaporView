#include "shared/session/SessionSensorCsv.h"

#include <QStringList>

#include <cmath>

namespace VaporView::SessionSensorCsv
{
namespace
{

QString boolText(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

QString optionalNumber(double value, int precision)
{
    return std::isfinite(value) ? QString::number(value, 'f', precision) : QString();
}

void appendEmptyColumns(QStringList& row, int count)
{
    for (int index = 0; index < count; ++index)
    {
        row.push_back(QString());
    }
}

}  // namespace

QString header()
{
    return QStringLiteral(
        "record_timestamp_us,"
        "epsilon_host_timestamp_us,epsilon_device_timestamp_us,epsilon_utc_unix_s,epsilon_utc_microseconds,"
        "nav_lat_deg,nav_lon_deg,nav_height_m,"
        "ecef_x_m,ecef_y_m,ecef_z_m,"
        "ned_n_m,ned_e_m,ned_d_m,"
        "vel_n_mps,vel_e_mps,vel_d_mps,"
        "body_vel_x_mps,body_vel_y_mps,body_vel_z_mps,"
        "body_acc_x_mps2,body_acc_y_mps2,body_acc_z_mps2,"
        "roll_deg,pitch_deg,yaw_deg,"
        "quat_w,quat_x,quat_y,quat_z,"
        "attitude_source_count,attitude_delta_max_deg,"
        "attitude_delta_ahrs_euler_deg,attitude_delta_ahrs_quat_deg,attitude_delta_euler_quat_deg,"
        "ang_vel_x_radps,ang_vel_y_radps,ang_vel_z_radps,"
        "imu_acc_x_mps2,imu_acc_y_mps2,imu_acc_z_mps2,"
        "imu_gyr_x_radps,imu_gyr_y_radps,imu_gyr_z_radps,"
        "mag_x_mg,mag_y_mg,mag_z_mg,"
        "gnss_fix,gnss_satellites,hdop,vdop,hacc_m,vacc_m,"
        "lat_std_m,lon_std_m,height_std_m,diff_age_s,"
        "heading_valid,system_status_bits,filter_status_bits,update_status_bits,"
        "epsilon_imu_packet_rate_hz,epsilon_ahrs_packet_rate_hz,"
        "epsilon_insgps_packet_rate_hz,epsilon_sys_state_packet_rate_hz,"
        "epsilon_raw_gnss_packet_rate_hz,epsilon_satellite_packet_rate_hz,"
        "epsilon_geodetic_packet_rate_hz,epsilon_ecef_packet_rate_hz,"
        "epsilon_valid,epsilon_error_message,"
        "hmp_temperature_c,hmp_humidity_rh,ptb_pressure_hpa,lidar_distance_m,lidar_signal_strength,lidar_valid\n");
}

QString formatRow(quint64 recordTimestampUs,
                  quint64 epsilonHostTimestampUs,
                  const EpsilonData& epsilon,
                  bool hasEpsilon,
                  const PtbData& ptb,
                  bool hasPtb,
                  const HmpData& hmp,
                  bool hasHmp,
                  const LidarData& lidar,
                  bool hasLidar)
{
    QStringList row;
    row.reserve(77);
    row << QString::number(recordTimestampUs);

    if (hasEpsilon)
    {
        row << QString::number(epsilonHostTimestampUs)
            << QString::number(epsilon.device_timestamp_us)
            << QString::number(epsilon.utc_unix_s)
            << QString::number(epsilon.utc_microseconds)
            << QString::number(epsilon.latitude_deg, 'f', 9)
            << QString::number(epsilon.longitude_deg, 'f', 9)
            << QString::number(epsilon.height_m, 'f', 6)
            << optionalNumber(epsilon.ecef_x_m, 6)
            << optionalNumber(epsilon.ecef_y_m, 6)
            << optionalNumber(epsilon.ecef_z_m, 6)
            << QString::number(epsilon.ned_n_m, 'f', 6)
            << QString::number(epsilon.ned_e_m, 'f', 6)
            << QString::number(epsilon.ned_d_m, 'f', 6)
            << QString::number(epsilon.vel_n_mps, 'f', 6)
            << QString::number(epsilon.vel_e_mps, 'f', 6)
            << QString::number(epsilon.vel_d_mps, 'f', 6)
            << QString::number(epsilon.body_vel_x_mps, 'f', 6)
            << QString::number(epsilon.body_vel_y_mps, 'f', 6)
            << QString::number(epsilon.body_vel_z_mps, 'f', 6)
            << QString::number(epsilon.body_acc_x_mps2, 'f', 6)
            << QString::number(epsilon.body_acc_y_mps2, 'f', 6)
            << QString::number(epsilon.body_acc_z_mps2, 'f', 6)
            << QString::number(epsilon.roll_deg, 'f', 6)
            << QString::number(epsilon.pitch_deg, 'f', 6)
            << QString::number(epsilon.yaw_deg, 'f', 6)
            << QString::number(epsilon.quat_w, 'f', 8)
            << QString::number(epsilon.quat_x, 'f', 8)
            << QString::number(epsilon.quat_y, 'f', 8)
            << QString::number(epsilon.quat_z, 'f', 8)
            << QString::number(epsilon.attitude_source_count)
            << optionalNumber(epsilon.attitude_delta_max_deg, 6)
            << optionalNumber(epsilon.attitude_delta_ahrs_euler_deg, 6)
            << optionalNumber(epsilon.attitude_delta_ahrs_quat_deg, 6)
            << optionalNumber(epsilon.attitude_delta_euler_quat_deg, 6)
            << QString::number(epsilon.ang_vel_x_radps, 'f', 8)
            << QString::number(epsilon.ang_vel_y_radps, 'f', 8)
            << QString::number(epsilon.ang_vel_z_radps, 'f', 8)
            << QString::number(epsilon.imu_acc_x_mps2, 'f', 6)
            << QString::number(epsilon.imu_acc_y_mps2, 'f', 6)
            << QString::number(epsilon.imu_acc_z_mps2, 'f', 6)
            << QString::number(epsilon.imu_gyr_x_radps, 'f', 8)
            << QString::number(epsilon.imu_gyr_y_radps, 'f', 8)
            << QString::number(epsilon.imu_gyr_z_radps, 'f', 8)
            << QString::number(epsilon.mag_x_mg, 'f', 6)
            << QString::number(epsilon.mag_y_mg, 'f', 6)
            << QString::number(epsilon.mag_z_mg, 'f', 6)
            << QString::fromStdString(epsilon.gnss_fix_text)
            << QString::number(epsilon.gnss_satellites)
            << QString::number(epsilon.hdop, 'f', 4)
            << QString::number(epsilon.vdop, 'f', 4)
            << QString::number(epsilon.hacc_m, 'f', 4)
            << QString::number(epsilon.vacc_m, 'f', 4)
            << QString::number(epsilon.lat_std_m, 'f', 4)
            << QString::number(epsilon.lon_std_m, 'f', 4)
            << QString::number(epsilon.height_std_m, 'f', 4)
            << optionalNumber(epsilon.diff_age_s, 4)
            << boolText(epsilon.heading_valid)
            << QString::number(epsilon.system_status_bits)
            << QString::number(epsilon.filter_status_bits)
            << QString::number(epsilon.update_status_bits)
            << QString::number(epsilon.imu_packet_rate_hz, 'f', 4)
            << QString::number(epsilon.ahrs_packet_rate_hz, 'f', 4)
            << QString::number(epsilon.insgps_packet_rate_hz, 'f', 4)
            << QString::number(epsilon.sys_state_packet_rate_hz, 'f', 4)
            << QString::number(epsilon.raw_gnss_packet_rate_hz, 'f', 4)
            << QString::number(epsilon.satellite_packet_rate_hz, 'f', 4)
            << QString::number(epsilon.geodetic_packet_rate_hz, 'f', 4)
            << QString::number(epsilon.ecef_packet_rate_hz, 'f', 4)
            << boolText(epsilon.valid)
            << QString::fromStdString(epsilon.error_message);
    }
    else
    {
        appendEmptyColumns(row, 70);
    }

    if (hasHmp)
    {
        row << QString::number(hmp.temperature, 'f', 6)
            << QString::number(hmp.humidity, 'f', 6);
    }
    else
    {
        appendEmptyColumns(row, 2);
    }

    if (hasPtb)
    {
        row << QString::number(ptb.pressure_hpa, 'f', 6);
    }
    else
    {
        appendEmptyColumns(row, 1);
    }

    if (hasLidar)
    {
        row << QString::number(lidar.distance_m, 'f', 6)
            << QString::number(lidar.signal_strength)
            << boolText(lidar.valid);
    }
    else
    {
        appendEmptyColumns(row, 3);
    }

    for (QString& value : row)
    {
        value = escape(value);
    }
    return row.join(QLatin1Char(',')) + QLatin1Char('\n');
}

QString escape(const QString& value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    if (escaped.contains(QLatin1Char(',')) || escaped.contains(QLatin1Char('"')) ||
        escaped.contains(QLatin1Char('\n')) || escaped.contains(QLatin1Char('\r')))
    {
        escaped = QStringLiteral("\"%1\"").arg(escaped);
    }
    return escaped;
}

}  // namespace VaporView::SessionSensorCsv
