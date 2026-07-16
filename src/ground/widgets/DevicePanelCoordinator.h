#pragma once

#include "TelemetryTypes.h"
#include "data_types.h"

#include <QtGlobal>

class GnssPanel;
class HmpPanel;
class ImuPanel;
class LidarPanel;
class PtbPanel;
class TemperatureControllerPanel;

namespace VaporView::Ground::Widgets
{

class EpsilonPanel;
class TemperatureControllerOverviewPanel;

struct DevicePanelBindings
{
    EpsilonPanel *epsilon = nullptr;
    GnssPanel *gnss = nullptr;
    ImuPanel *imu = nullptr;
    PtbPanel *ptb = nullptr;
    HmpPanel *hmp = nullptr;
    LidarPanel *lidar = nullptr;
    TemperatureControllerPanel *temperature = nullptr;
    TemperatureControllerOverviewPanel *temperatureOverview = nullptr;
};

struct DevicePanelRates
{
    double epsilonHz = 0.0;
    double gnssHz = 0.0;
    double imuHz = 0.0;
    double ptbHz = 0.0;
    double hmpHz = 0.0;
    double lidarHz = 0.0;
    double temperatureHz = 0.0;
};

// Keeps QWidget-specific presentation fan-out out of MainWindow's connection
// and telemetry paths. It does not own device data or panel lifetimes.
class DevicePanelCoordinator final
{
public:
    explicit DevicePanelCoordinator(DevicePanelBindings bindings);

    void updateAllData(const VaporView::EpsilonData& epsilon,
                       const VaporView::GnssData& gnss,
                       quint64 gnssTimestampUs,
                       const VaporView::ImuData& imu,
                       quint64 imuTimestampUs,
                       const VaporView::PtbData& ptb,
                       const VaporView::HmpData& hmp,
                       const VaporView::LidarData& lidar,
                       const VaporView::TemperatureControllerData& temperature);
    void updateEnvironmentData(const VaporView::EpsilonData& epsilon,
                               const VaporView::PtbData& ptb,
                               const VaporView::HmpData& hmp,
                               const VaporView::LidarData& lidar);
    void updateTemperatureData(const VaporView::TemperatureControllerData& temperature);
    void updateTemperatureRate(double rateHz);
    void updateRates(const DevicePanelRates& rates);
    void clearRates();

private:
    DevicePanelBindings bindings_;
};

} // namespace VaporView::Ground::Widgets
