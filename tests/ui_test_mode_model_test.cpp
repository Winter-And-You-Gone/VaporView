#include "ground/devices/UiTestDataModel.h"
#include "shared/config/SettingsWriteBarrier.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    using namespace VaporView;
    using namespace VaporView::Ground::Devices;

    UiTestDataModel first;
    UiTestDataModel second;
    const UiTestSnapshot firstSnapshot = first.snapshot(2500);
    const UiTestSnapshot secondSnapshot = second.snapshot(2500);
    require(firstSnapshot.epsilon.valid, "normal scenario supplies EPSILON data");
    require(firstSnapshot.rawWaveform.size() == 512, "normal scenario supplies raw waveform");
    require(firstSnapshot.harmonicWaveform.size() == 512, "normal scenario supplies harmonic waveform");
    require(firstSnapshot.ai8Temperature.valid, "normal scenario supplies AI-8 temperature data");
    require(firstSnapshot.epsilon.utc_unix_s > 0 &&
                firstSnapshot.epsilon.device_timestamp_us > 0,
            "normal scenario supplies EPSILON UTC and device timestamps");
    require(firstSnapshot.epsilon.filter_status_bits != 0 &&
                firstSnapshot.epsilon.update_status_bits != 0,
            "normal scenario supplies EPSILON filter/update status bits");
    require(firstSnapshot.epsilon.gnss_fix_code == 6 &&
                firstSnapshot.epsilon.gnss_fix_text == "RTK_FIXED",
            "UI-test GNSS cycle starts from the RTK fixed state");
    require(firstSnapshot.epsilon.quat_orien_valid &&
                std::isfinite(firstSnapshot.epsilon.attitude_delta_max_deg) &&
                std::isfinite(firstSnapshot.epsilon.attitude_delta_ahrs_euler_deg) &&
                std::isfinite(firstSnapshot.epsilon.attitude_delta_ahrs_quat_deg) &&
                std::isfinite(firstSnapshot.epsilon.attitude_delta_euler_quat_deg),
            "normal scenario supplies all EPSILON attitude consistency fields");
    require(firstSnapshot.epsilon.raw_gnss_packet_rate_hz > 0.0 &&
                firstSnapshot.epsilon.satellite_packet_rate_hz > 0.0 &&
                firstSnapshot.epsilon.geodetic_packet_rate_hz > 0.0 &&
                firstSnapshot.epsilon.ecef_packet_rate_hz > 0.0,
            "normal scenario supplies detailed EPSILON packet-rate fields");
    require(!firstSnapshot.gnss.heading_type.empty() &&
                firstSnapshot.gnss.num_satellites_tracked > firstSnapshot.gnss.num_satellites_used &&
                firstSnapshot.gnss.vel_ground > 0.0 &&
                firstSnapshot.gnss.sigma_lat > 0.0 &&
                firstSnapshot.gnss.sigma_lon > 0.0 &&
                firstSnapshot.gnss.sigma_alt > 0.0 &&
                firstSnapshot.gnss.gdop > 0.0 &&
                firstSnapshot.gnss.pdop > 0.0 &&
                firstSnapshot.gnss.htdop > 0.0 &&
                firstSnapshot.gnss.tdop > 0.0,
            "normal scenario supplies representative GNSS detail fields");
    require(firstSnapshot.imu.system_time_ms > 0 &&
                std::abs(firstSnapshot.imu.quaternion[0]) > 0.1 &&
                std::isfinite(firstSnapshot.imu.air_pressure),
            "normal scenario supplies representative IMU time/quaternion/environment fields");
    require(firstSnapshot.lidar.valid &&
                firstSnapshot.lidar.distance_m >= 100.0 &&
                firstSnapshot.lidar.distance_m < 1000.0,
            "UI-test lidar distance uses a three-digit meter range");
    const UiTestSnapshot beforeWideDistance = first.snapshot(2999);
    const UiTestSnapshot wideDistance = first.snapshot(3000);
    const UiTestSnapshot wideDistanceEnd = first.snapshot(4999);
    const UiTestSnapshot afterWideDistance = first.snapshot(5000);
    const UiTestSnapshot repeatedWideDistance = first.snapshot(6000);
    require(beforeWideDistance.lidar.distance_m >= 100.0 &&
                beforeWideDistance.lidar.distance_m < 1000.0 &&
                wideDistance.lidar.distance_m >= 1000.0 &&
                wideDistance.lidar.distance_m < 10000.0 &&
                wideDistanceEnd.lidar.distance_m >= 1000.0 &&
                wideDistanceEnd.lidar.distance_m < 10000.0 &&
                afterWideDistance.lidar.distance_m >= 100.0 &&
                afterWideDistance.lidar.distance_m < 1000.0 &&
                repeatedWideDistance.lidar.distance_m >= 1000.0 &&
                repeatedWideDistance.lidar.distance_m < 10000.0,
            "UI-test lidar distance alternates between three and four integer digits on the requested cycle");
    require(firstSnapshot.epsilon.latitude_deg == secondSnapshot.epsilon.latitude_deg,
            "same elapsed time produces deterministic navigation data");
    require(firstSnapshot.rawWaveform == secondSnapshot.rawWaveform,
            "same elapsed time produces deterministic waveform data");
    require(firstSnapshot.ai8Temperature.measuredC == secondSnapshot.ai8Temperature.measuredC,
            "same elapsed time produces deterministic AI-8 temperature data");
    require(firstSnapshot.receiveBitsPerSecond == secondSnapshot.receiveBitsPerSecond &&
                firstSnapshot.waveCaptureRateHz == secondSnapshot.waveCaptureRateHz,
            "same elapsed time produces deterministic UI-test telemetry capsule values");
    UiTestDataModel pidModel;
    constexpr double pidTargetTemperature = 25.0;
    const double initialPidTemperature =
        pidModel.snapshot(0).temperature.channels[0].measured_temperature_c;
    const double positivePidOvershoot =
        pidModel.snapshot(2327).temperature.channels[0].measured_temperature_c;
    const double negativePidOvershoot =
        pidModel.snapshot(4654).temperature.channels[0].measured_temperature_c;
    const double settledPidTemperature =
        pidModel.snapshot(9308).temperature.channels[0].measured_temperature_c;
    require(initialPidTemperature < pidTargetTemperature &&
                positivePidOvershoot > pidTargetTemperature &&
                negativePidOvershoot < pidTargetTemperature &&
                std::abs(negativePidOvershoot - pidTargetTemperature) <
                    std::abs(initialPidTemperature - pidTargetTemperature) &&
                std::abs(settledPidTemperature - pidTargetTemperature) <
                    std::abs(negativePidOvershoot - pidTargetTemperature),
            "enabled UI-test temperature simulates damped PID overshoot toward the target");
    TemperatureControllerCommand disableOutput;
    disableOutput.channel = 1;
    disableOutput.output_enabled = false;
    pidModel.applyTemperatureCommand(CommandId::SetTemperatureOutputEnabled, disableOutput);
    const double disabledTemperature =
        pidModel.snapshot(4654).temperature.channels[0].measured_temperature_c;
    require(!pidModel.snapshot(4654).temperature.channels[0].output_enabled &&
                std::abs(disabledTemperature - pidTargetTemperature) <
                    std::abs(negativePidOvershoot - pidTargetTemperature),
            "disabling UI-test temperature output removes the PID overshoot response");
    const UiTestSnapshot laterSnapshot = first.snapshot(3900);
    require(firstSnapshot.waveformFeatureRateHz != laterSnapshot.waveformFeatureRateHz ||
                firstSnapshot.rawWaveformRateHz != laterSnapshot.rawWaveformRateHz ||
                firstSnapshot.receiveBitsPerSecond != laterSnapshot.receiveBitsPerSecond,
            "different elapsed times animate UI-test telemetry capsule values");
    require(firstSnapshot.epsilonRateHz >= 100.0 &&
                firstSnapshot.waveformFeatureRateHz >= 100.0 &&
                firstSnapshot.telemetryStatusRateHz >= 100.0 &&
                firstSnapshot.rawWaveformRateHz >= 100.0 &&
                firstSnapshot.harmonicWaveformRateHz >= 100.0 &&
                firstSnapshot.waveCaptureRateHz >= 100.0,
            "UI-test home telemetry rates cover three-digit Hz values");
    require(laterSnapshot.receiveBitsPerSecond >= 100'000'000.0 &&
                laterSnapshot.transmitBitsPerSecond >= 100'000'000.0 &&
                laterSnapshot.receiveBitsPerSecond + laterSnapshot.transmitBitsPerSecond < 1'000'000'000.0,
            "UI-test link-rate values cover three-digit one-decimal Mbps without leaving the compact reserve");

    constexpr std::array<int, 10> expectedFixCodes{{6, 5, 9, 4, 3, 2, 8, 7, 1, 0}};
    constexpr std::array<const char *, 10> expectedFixTexts{{
        "RTK_FIXED", "RTK_FLOAT", "RTK_DUAL", "DGPS", "3D",
        "2D", "PPP", "STATIC", "NO_FIX", "NO_GPS"}};
    for (std::size_t index = 0; index < expectedFixCodes.size(); ++index)
    {
        const UiTestSnapshot cycleSnapshot = first.snapshot(static_cast<qint64>(index) * 3000 + 100);
        require(cycleSnapshot.epsilon.gnss_fix_code == expectedFixCodes[index] &&
                    cycleSnapshot.epsilon.gnss_fix_text == expectedFixTexts[index],
                "UI-test GNSS data cycles through every EPSILON fix state every three seconds");
    }

    first.setScenario(UiTestScenario::PartialFailure, 2500);
    const UiTestSnapshot partial = first.snapshot(2600);
    require(!partial.ptb.valid, "partial-failure scenario invalidates PTB");
    require(!partial.hmp.valid, "partial-failure scenario disconnects HMP");
    require(partial.temperature.error_code != 0, "partial-failure scenario reports RD105 alarm");
    require(partial.waveformFeature.quality_flags != 0, "partial-failure scenario marks waveform anomaly");
    require(partial.ai8Temperature.valid, "partial-failure scenario keeps AI-8 data available");

    first.setScenario(UiTestScenario::DataStalled, 3000);
    require(first.snapshot(5900).epsilon.valid, "data remains fresh before stall timeout");
    require(first.snapshot(6100).dataStalled, "data-stalled scenario becomes stale after timeout");
    require(!first.snapshot(6100).epsilon.valid, "stalled navigation data becomes invalid");
    require(!first.snapshot(6100).ai8Temperature.valid, "stalled AI-8 data becomes invalid");
    require(first.snapshot(6100).epsilon.timestamp == first.snapshot(9000).epsilon.timestamp,
            "stalled scenario freezes its sample timestamp");
    first.setScenario(UiTestScenario::Normal, 6200);
    require(first.snapshot(6200).epsilon.valid, "normal scenario immediately restores data");

    first.setAllDevicesConnected(false);
    require(first.deviceState(SkyDeviceId::WaveTcp) == DeviceState::Disconnected,
            "disconnect transition is retained in memory");
    first.setDeviceState(SkyDeviceId::WaveTcp, DeviceState::Connecting);
    require(first.deviceState(SkyDeviceId::WaveTcp) == DeviceState::Connecting,
            "connecting transition is retained in memory");
    first.setDeviceState(SkyDeviceId::WaveTcp, DeviceState::Connected);

    TemperatureControllerCommand target;
    target.channel = 2;
    target.target_temperature_c = 41.25;
    first.applyTemperatureCommand(CommandId::SetTemperatureTarget, target);
    require(std::abs(first.snapshot(7000).temperature.channels[1].target_temperature_c - 41.25) < 1e-9,
            "temperature command updates only the simulated RD105 state");

    QTemporaryDir temporaryDirectory;
    require(temporaryDirectory.isValid(), "temporary settings directory created");
    const QString settingsPath = temporaryDirectory.filePath(QStringLiteral("settings.ini"));
    QSettings settings(settingsPath, QSettings::IniFormat);
    settings.setValue(QStringLiteral("serial/port"), QStringLiteral("COM7"));
    settings.setValue(QStringLiteral("theme/dark"), false);
    settings.sync();
    const QStringList keysBefore = settings.allKeys();
    const QVariant portBefore = settings.value(QStringLiteral("serial/port"));

    setSettingsWritesSuspended(true);
    setPersistentSetting(settings, QStringLiteral("serial/port"), QStringLiteral("UI-TEST-EPSILON"));
    setPersistentSetting(settings, QStringLiteral("map/lastEarthFile"), QStringLiteral("ui-test.earth"));
    removePersistentSetting(settings, QStringLiteral("theme/dark"));
    settings.sync();
    require(settings.allKeys() == keysBefore, "write barrier preserves the complete settings key set");
    require(settings.value(QStringLiteral("serial/port")) == portBefore,
            "write barrier preserves existing settings values");
    setSettingsWritesSuspended(false);
    setPersistentSetting(settings, QStringLiteral("serial/port"), QStringLiteral("COM8"));
    settings.sync();
    require(settings.value(QStringLiteral("serial/port")).toString() == QStringLiteral("COM8"),
            "writes resume after the barrier is released");

    std::cout << "ui_test_mode_model_test passed\n";
    return 0;
}
