#include "ground/devices/UiTestDataModel.h"
#include "shared/config/SettingsWriteBarrier.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

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
    require(firstSnapshot.epsilon.latitude_deg == secondSnapshot.epsilon.latitude_deg,
            "same elapsed time produces deterministic navigation data");
    require(firstSnapshot.rawWaveform == secondSnapshot.rawWaveform,
            "same elapsed time produces deterministic waveform data");
    require(firstSnapshot.ai8Temperature.measuredC == secondSnapshot.ai8Temperature.measuredC,
            "same elapsed time produces deterministic AI-8 temperature data");
    require(firstSnapshot.receiveBitsPerSecond == secondSnapshot.receiveBitsPerSecond &&
                firstSnapshot.waveCaptureRateHz == secondSnapshot.waveCaptureRateHz,
            "same elapsed time produces deterministic UI-test telemetry capsule values");
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
