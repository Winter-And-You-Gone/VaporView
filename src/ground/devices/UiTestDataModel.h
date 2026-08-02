#pragma once

#include "TelemetryTypes.h"
#include "data_types.h"

#include <QVector>

#include <array>

namespace VaporView::Ground::Devices
{

enum class UiTestScenario
{
    Normal,
    PartialFailure,
    DataStalled,
};

struct UiTestSnapshot
{
    EpsilonData epsilon;
    GnssData gnss;
    ImuData imu;
    PtbData ptb;
    HmpData hmp;
    LidarData lidar;
    TemperatureControllerData temperature;
    QVector<float> rawWaveform;
    QVector<float> harmonicWaveform;
    WaveformFeature waveformFeature;
    double epsilonRateHz = 0.0;
    double gnssRateHz = 0.0;
    double imuRateHz = 0.0;
    double ptbRateHz = 0.0;
    double hmpRateHz = 0.0;
    double lidarRateHz = 0.0;
    double temperatureRateHz = 0.0;
    bool dataStalled = false;
};

class UiTestDataModel
{
public:
    UiTestDataModel();

    void reset(qint64 elapsedMs = 0);
    void setScenario(UiTestScenario scenario, qint64 elapsedMs);
    UiTestScenario scenario() const;

    void setAllDevicesConnected(bool connected);
    void setDeviceState(SkyDeviceId device, DeviceState state);
    DeviceState deviceState(SkyDeviceId device) const;

    void applyTemperatureCommand(CommandId command,
                                 const TemperatureControllerCommand& payload);
    UiTestSnapshot snapshot(qint64 elapsedMs) const;

private:
    static int deviceIndex(SkyDeviceId device);
    void resetTemperatureState();

    UiTestScenario scenario_ = UiTestScenario::Normal;
    qint64 scenario_started_ms_ = 0;
    std::array<DeviceState, 7> device_states_{};
    TemperatureControllerData temperature_state_;
};

} // namespace VaporView::Ground::Devices
