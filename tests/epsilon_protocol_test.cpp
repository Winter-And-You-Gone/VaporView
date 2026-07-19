#include "EpsilonProtocol.h"
#include "geo/CoordinateTransform.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

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

void requireNear(double actual, double expected, double tolerance, const char *message)
{
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}

template<typename Value>
void writeValue(std::vector<std::uint8_t>& payload, std::size_t offset, Value value)
{
    require(offset + sizeof(Value) <= payload.size(), "test payload write stays in bounds");
    std::memcpy(payload.data() + offset, &value, sizeof(Value));
}
}

int main()
{
    VaporView::EpsilonData data;

    std::vector<std::uint8_t> imu(56, 0);
    writeValue<float>(imu, 36, 42.5f);
    writeValue<float>(imu, 40, 100125.0f);
    writeValue<float>(imu, 44, 36.75f);
    writeValue<std::int64_t>(imu, 48, 123456789);
    require(VaporView::EpsilonProtocol::decodeCorePacket(data, 0x40, imu.data(), imu.size()),
            "decode MSG_IMU");
    requireNear(data.imu_temp_c, 42.5, 1.0e-6, "MSG_IMU temperature offset");
    requireNear(data.pressure_pa, 100125.0, 1.0e-6, "MSG_IMU pressure offset");
    requireNear(data.pressure_temp_c, 36.75, 1.0e-6, "MSG_IMU pressure temperature offset");
    require(data.device_timestamp_us == 123456789ULL, "MSG_IMU timestamp offset");

    std::vector<std::uint8_t> insGps(72, 0);
    writeValue<float>(insGps, 48, 1.25f);
    writeValue<float>(insGps, 52, -2.5f);
    writeValue<float>(insGps, 56, 3.75f);
    writeValue<float>(insGps, 60, 88.0f);
    require(VaporView::EpsilonProtocol::decodeCorePacket(data, 0x42, insGps.data(), insGps.size()),
            "decode MSG_INSGPS");
    requireNear(data.ned_acc_n_mps2, 1.25, 1.0e-6, "MSG_INSGPS north acceleration offset");
    requireNear(data.ned_acc_e_mps2, -2.5, 1.0e-6, "MSG_INSGPS east acceleration offset");
    requireNear(data.ned_acc_d_mps2, 3.75, 1.0e-6, "MSG_INSGPS down acceleration offset");
    requireNear(data.pressure_altitude_m, 88.0, 1.0e-6, "MSG_INSGPS pressure altitude offset");

    std::vector<std::uint8_t> rawGnss(74, 0);
    constexpr double kDegToRad = 0.01745329251994329576923690768489;
    writeValue<double>(rawGnss, 8, 30.25 * kDegToRad);
    writeValue<double>(rawGnss, 16, 120.15 * kDegToRad);
    writeValue<double>(rawGnss, 24, 42.5);
    writeValue<float>(rawGnss, 32, 4.0f);
    writeValue<float>(rawGnss, 36, 5.0f);
    writeValue<float>(rawGnss, 40, -1.0f);
    writeValue<float>(rawGnss, 56, 123.5f);
    writeValue<float>(rawGnss, 60, 8.25f);
    writeValue<float>(rawGnss, 64, 0.75f);
    writeValue<std::uint16_t>(rawGnss, 72, static_cast<std::uint16_t>(0x03F4u));
    require(VaporView::EpsilonProtocol::decodeCorePacket(data, 0x59, rawGnss.data(), rawGnss.size()),
            "decode MSG_RAW_GNSS");
    requireNear(data.latitude_deg, 30.25, 1.0e-9, "MSG_RAW_GNSS latitude offset");
    requireNear(data.longitude_deg, 120.15, 1.0e-9, "MSG_RAW_GNSS longitude offset");
    requireNear(data.gnss_course_deg, 123.5, 1.0e-6, "MSG_RAW_GNSS course offset");
    requireNear(data.geoid_separation_m, 8.25, 1.0e-6, "MSG_RAW_GNSS geoid separation offset");
    require(data.gnss_fix_code == 4 && data.gnss_velocity_valid && data.gnss_time_valid &&
                data.external_gnss && data.gnss_tilt_valid && data.heading_valid &&
                data.floating_ambiguity_heading,
            "MSG_RAW_GNSS status bits 4 through 9");
    require(VaporView::Geo::isPlausibleEcef(data.ecef_x_m, data.ecef_y_m, data.ecef_z_m),
            "MSG_RAW_GNSS position derives plausible ECEF");

    const auto now = std::chrono::steady_clock::now();
    VaporView::EpsilonData attitude;
    attitude.system_state_attitude_valid = true;
    attitude.system_state_attitude_timestamp = now;
    attitude.system_state_roll_deg = 1.0;
    attitude.system_state_pitch_deg = 2.0;
    attitude.system_state_yaw_deg = 3.0;
    VaporView::EpsilonProtocol::resolveAttitudeState(attitude, now);
    requireNear(attitude.roll_deg, 1.0, 1.0e-9, "MSG_SYS_STATE is the attitude fallback");

    attitude.ahrs_attitude_valid = true;
    attitude.ahrs_attitude_timestamp = now;
    attitude.ahrs_roll_deg = 4.0;
    attitude.ahrs_pitch_deg = 5.0;
    attitude.ahrs_yaw_deg = 6.0;
    VaporView::EpsilonProtocol::resolveAttitudeState(attitude, now);
    requireNear(attitude.roll_deg, 4.0, 1.0e-9, "MSG_AHRS outranks MSG_SYS_STATE");

    attitude.euler_orien_valid = true;
    attitude.euler_orien_timestamp = now;
    attitude.euler_orien_roll_deg = 7.0;
    attitude.euler_orien_pitch_deg = 8.0;
    attitude.euler_orien_yaw_deg = 9.0;
    VaporView::EpsilonProtocol::resolveAttitudeState(attitude, now);
    requireNear(attitude.roll_deg, 7.0, 1.0e-9, "MSG_EULER_ORIEN has canonical priority");
    require(attitude.attitude_source_count == 2, "independent attitude source count excludes fallback");

    attitude.ahrs_attitude_timestamp = now - std::chrono::seconds(4);
    attitude.euler_orien_timestamp = now - std::chrono::seconds(4);
    VaporView::EpsilonProtocol::resolveAttitudeState(attitude, now);
    require(!attitude.ahrs_attitude_valid && !attitude.euler_orien_valid,
            "stale attitude sources expire after three seconds");
    requireNear(attitude.roll_deg, 1.0, 1.0e-9, "fresh MSG_SYS_STATE resumes as fallback");

    std::cout << "epsilon_protocol_test passed\n";
    return 0;
}
