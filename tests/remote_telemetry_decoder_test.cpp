#include "ground/devices/RemoteTelemetryDecoder.h"

#include "geo/CoordinateTransform.h"

#include <QCoreApplication>

#include <cmath>
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

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    VaporView::TelemetryBasic telemetry;
    telemetry.host_time_us = 1234567;
    telemetry.epsilon_time_us = 1230000;
    telemetry.validity_flags = VaporView::BasicHasEpsilonTime |
                               VaporView::BasicHasPosition |
                               VaporView::BasicHasGnssQuality |
                               VaporView::BasicHasNedVelocity |
                               VaporView::BasicHasAttitude |
                               VaporView::BasicHasEpsilonDiagnostics;
    telemetry.latitude_deg = 30.25;
    telemetry.longitude_deg = 120.15;
    telemetry.height_m = 42.0;
    telemetry.filter_status_bits = 0x60;
    telemetry.gnss_fix_code = 0;
    telemetry.gnss_satellites = 15;
    telemetry.hdop = 0.8f;
    telemetry.vel_n_mps = 1.0f;
    telemetry.vel_e_mps = 2.0f;
    telemetry.vel_d_mps = -0.5f;
    telemetry.roll_deg = 1.0f;
    telemetry.pitch_deg = -2.0f;
    telemetry.yaw_deg = 93.0f;
    telemetry.raw_frame_count = 99;

    const auto decoded = VaporView::Ground::decodeRemoteEpsilonTelemetry(
        telemetry, std::chrono::steady_clock::time_point{});
    require(decoded.available && decoded.hasPosition, "position telemetry is marked available");
    require(decoded.data.valid && decoded.data.device_timestamp_us == 1230000,
            "remote timestamps and validity are preserved");
    require(decoded.data.gnss_fix_code == 6 && decoded.data.gnss_fix_text == "RTK_FIXED",
            "zero fix code falls back to filter status bits");
    require(VaporView::Geo::isPlausibleEcef(decoded.data.ecef_x_m,
                                             decoded.data.ecef_y_m,
                                             decoded.data.ecef_z_m),
            "position-only telemetry derives plausible ECEF coordinates");
    require(decoded.data.gnss_satellites == 15 && decoded.data.vel_e_mps == 2.0 &&
                decoded.data.yaw_deg == 93.0 && decoded.data.raw_frame_count == 99,
            "quality, velocity, attitude, and diagnostics fields are copied");

    VaporView::TelemetryBasic ecefOnly;
    ecefOnly.validity_flags = VaporView::BasicHasEcef;
    ecefOnly.ecef_x_m = -2170000.0;
    ecefOnly.ecef_y_m = 4380000.0;
    ecefOnly.ecef_z_m = 4070000.0;
    const auto ecefDecoded = VaporView::Ground::decodeRemoteEpsilonTelemetry(
        ecefOnly, std::chrono::steady_clock::time_point{});
    require(ecefDecoded.available && !ecefDecoded.hasPosition &&
                ecefDecoded.data.ecef_x_m == ecefOnly.ecef_x_m,
            "ECEF-only telemetry remains available without inventing LLH");

    const auto emptyDecoded = VaporView::Ground::decodeRemoteEpsilonTelemetry(
        VaporView::TelemetryBasic{}, std::chrono::steady_clock::time_point{});
    require(!emptyDecoded.available && !emptyDecoded.data.valid,
            "empty telemetry is rejected as unavailable");

    std::cout << "remote_telemetry_decoder_test passed\n";
    return 0;
}
