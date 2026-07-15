#include "ground/devices/RemoteTelemetryDecoder.h"

#include "geo/CoordinateTransform.h"

#include <cmath>
#include <limits>

namespace VaporView::Ground
{

namespace
{

bool hasFlag(const VaporView::TelemetryBasic& telemetry, quint32 flag)
{
    return (telemetry.validity_flags & flag) != 0;
}

bool hasUsableLlh(const VaporView::EpsilonData& data)
{
    return std::isfinite(data.latitude_deg) &&
           std::isfinite(data.longitude_deg) &&
           std::isfinite(data.height_m) &&
           data.latitude_deg >= -90.0 &&
           data.latitude_deg <= 90.0 &&
           data.longitude_deg >= -180.0 &&
           data.longitude_deg <= 180.0 &&
           (std::abs(data.latitude_deg) > 1.0e-9 ||
            std::abs(data.longitude_deg) > 1.0e-9 ||
            std::abs(data.height_m) > 1.0e-9);
}

void resolveEcefFromLlh(VaporView::EpsilonData& data)
{
    if (VaporView::Geo::isPlausibleEcef(data.ecef_x_m, data.ecef_y_m, data.ecef_z_m) ||
        !hasUsableLlh(data))
    {
        return;
    }

    VaporView::Geo::EcefPoint derived;
    if (VaporView::Geo::deriveEcefFromLlh(data.latitude_deg, data.longitude_deg, data.height_m, derived))
    {
        data.ecef_x_m = derived.xM;
        data.ecef_y_m = derived.yM;
        data.ecef_z_m = derived.zM;
    }
}

std::string fixText(int fixCode)
{
    switch (fixCode)
    {
    case 0: return "NO_GPS";
    case 1: return "NO_FIX";
    case 2: return "2D";
    case 3: return "3D";
    case 4: return "DGPS";
    case 5: return "RTK_FLOAT";
    case 6: return "RTK_FIXED";
    case 7: return "STATIC";
    case 8: return "PPP";
    case 9: return "RTK_DUAL";
    default: return "UNKNOWN";
    }
}

}  // namespace

RemoteEpsilonTelemetry decodeRemoteEpsilonTelemetry(
    const VaporView::TelemetryBasic& telemetry,
    std::chrono::steady_clock::time_point timestamp)
{
    RemoteEpsilonTelemetry result;
    result.data = VaporView::EpsilonData();
    const double unknown = std::numeric_limits<double>::quiet_NaN();
    result.data.vel_n_mps = unknown;
    result.data.vel_e_mps = unknown;
    result.data.vel_d_mps = unknown;
    result.data.imu_acc_x_mps2 = unknown;
    result.data.imu_acc_y_mps2 = unknown;
    result.data.imu_acc_z_mps2 = unknown;
    result.data.imu_gyr_x_radps = unknown;
    result.data.imu_gyr_y_radps = unknown;
    result.data.imu_gyr_z_radps = unknown;
    result.data.roll_deg = unknown;
    result.data.pitch_deg = unknown;
    result.data.yaw_deg = unknown;
    result.data.hacc_m = unknown;
    result.data.vacc_m = unknown;

    result.available = hasFlag(telemetry, VaporView::BasicHasEpsilonTime) ||
                       hasFlag(telemetry, VaporView::BasicHasPosition) ||
                       hasFlag(telemetry, VaporView::BasicHasEcef);
    result.hasPosition = hasFlag(telemetry, VaporView::BasicHasPosition);
    if (!result.available)
    {
        return result;
    }

    result.data.valid = true;
    result.data.timestamp = timestamp;
    result.data.device_timestamp_us = telemetry.epsilon_time_us;
    result.data.utc_unix_s = telemetry.host_time_us / 1000000ULL;
    result.data.utc_microseconds = static_cast<quint32>(telemetry.host_time_us % 1000000ULL);

    int gnssFixCode = static_cast<int>(telemetry.gnss_fix_code);
    if (gnssFixCode == 0 && telemetry.filter_status_bits != 0)
    {
        gnssFixCode = static_cast<int>((telemetry.filter_status_bits >> 4) & 0x0F);
    }
    result.data.gnss_fix_code = gnssFixCode;
    result.data.gnss_fix_text = fixText(gnssFixCode);

    if (result.hasPosition)
    {
        result.data.latitude_deg = telemetry.latitude_deg;
        result.data.longitude_deg = telemetry.longitude_deg;
        result.data.height_m = telemetry.height_m;
    }
    if (hasFlag(telemetry, VaporView::BasicHasEcef))
    {
        result.data.ecef_x_m = telemetry.ecef_x_m;
        result.data.ecef_y_m = telemetry.ecef_y_m;
        result.data.ecef_z_m = telemetry.ecef_z_m;
    }
    if (result.hasPosition)
    {
        resolveEcefFromLlh(result.data);
    }

    result.data.system_status_bits = telemetry.status_bits;
    result.data.filter_status_bits = telemetry.filter_status_bits;
    result.data.update_status_bits = telemetry.update_status_bits;
    if (hasFlag(telemetry, VaporView::BasicHasGnssQuality))
    {
        result.data.gnss_satellites = telemetry.gnss_satellites;
        result.data.hdop = telemetry.hdop;
        result.data.vdop = telemetry.vdop;
        result.data.hacc_m = telemetry.hacc_m;
        result.data.vacc_m = telemetry.vacc_m;
        result.data.heading_valid = telemetry.heading_valid;
    }
    if (hasFlag(telemetry, VaporView::BasicHasNedVelocity))
    {
        result.data.vel_n_mps = telemetry.vel_n_mps;
        result.data.vel_e_mps = telemetry.vel_e_mps;
        result.data.vel_d_mps = telemetry.vel_d_mps;
    }
    if (hasFlag(telemetry, VaporView::BasicHasImu))
    {
        result.data.imu_acc_x_mps2 = telemetry.imu_acc_x_mps2;
        result.data.imu_acc_y_mps2 = telemetry.imu_acc_y_mps2;
        result.data.imu_acc_z_mps2 = telemetry.imu_acc_z_mps2;
        result.data.imu_gyr_x_radps = telemetry.imu_gyr_x_radps;
        result.data.imu_gyr_y_radps = telemetry.imu_gyr_y_radps;
        result.data.imu_gyr_z_radps = telemetry.imu_gyr_z_radps;
    }
    if (hasFlag(telemetry, VaporView::BasicHasAttitude))
    {
        result.data.roll_deg = telemetry.roll_deg;
        result.data.pitch_deg = telemetry.pitch_deg;
        result.data.yaw_deg = telemetry.yaw_deg;
    }
    if (hasFlag(telemetry, VaporView::BasicHasEpsilonDiagnostics))
    {
        result.data.raw_frame_count = telemetry.raw_frame_count;
        result.data.dropped_frame_count = telemetry.dropped_frame_count;
        result.data.imu_packet_rate_hz = telemetry.imu_packet_rate_hz;
        result.data.ahrs_packet_rate_hz = telemetry.ahrs_packet_rate_hz;
        result.data.insgps_packet_rate_hz = telemetry.insgps_packet_rate_hz;
        result.data.sys_state_packet_rate_hz = telemetry.sys_state_packet_rate_hz;
        result.data.raw_gnss_packet_rate_hz = telemetry.raw_gnss_packet_rate_hz;
        result.data.satellite_packet_rate_hz = telemetry.satellite_packet_rate_hz;
        result.data.geodetic_packet_rate_hz = telemetry.geodetic_packet_rate_hz;
        result.data.ecef_packet_rate_hz = telemetry.ecef_packet_rate_hz;
    }
    return result;
}

}  // namespace VaporView::Ground
