#include "ground/widgets/DevicePanelCoordinator.h"

#include "ground/widgets/EpsilonPanel.h"
#include "ground/widgets/TelemetryPanels.h"
#include "ground/widgets/TemperatureControllerWidgets.h"

namespace VaporView::Ground::Widgets
{

DevicePanelCoordinator::DevicePanelCoordinator(DevicePanelBindings bindings)
    : bindings_(bindings)
{
}

void DevicePanelCoordinator::updateAllData(
    const VaporView::EpsilonData& epsilon,
    const VaporView::GnssData& gnss,
    quint64 gnssTimestampUs,
    const VaporView::ImuData& imu,
    quint64 imuTimestampUs,
    const VaporView::PtbData& ptb,
    const VaporView::HmpData& hmp,
    const VaporView::LidarData& lidar,
    const VaporView::TemperatureControllerData& temperature)
{
    updateEnvironmentData(epsilon, ptb, hmp, lidar);
    if (bindings_.gnss)
    {
        bindings_.gnss->updateData(gnss, gnssTimestampUs);
    }
    if (bindings_.imu)
    {
        bindings_.imu->updateData(imu, imuTimestampUs);
    }
    updateTemperatureData(temperature);
}

void DevicePanelCoordinator::updateEnvironmentData(
    const VaporView::EpsilonData& epsilon,
    const VaporView::PtbData& ptb,
    const VaporView::HmpData& hmp,
    const VaporView::LidarData& lidar)
{
    if (bindings_.epsilon)
    {
        bindings_.epsilon->updateData(epsilon);
    }
    if (bindings_.ptb)
    {
        bindings_.ptb->updateData(ptb);
    }
    if (bindings_.hmp)
    {
        bindings_.hmp->updateData(hmp);
    }
    if (bindings_.lidar)
    {
        bindings_.lidar->updateData(lidar);
    }
}

void DevicePanelCoordinator::updateTemperatureData(
    const VaporView::TemperatureControllerData& temperature)
{
    if (bindings_.temperature)
    {
        bindings_.temperature->updateData(temperature);
    }
    if (bindings_.temperatureOverview)
    {
        bindings_.temperatureOverview->updateData(temperature);
    }
}

void DevicePanelCoordinator::updateRates(const DevicePanelRates& rates)
{
    if (bindings_.epsilon)
    {
        bindings_.epsilon->updateRate(rates.epsilonHz);
    }
    if (bindings_.gnss)
    {
        bindings_.gnss->updateRate(rates.gnssHz);
    }
    if (bindings_.imu)
    {
        bindings_.imu->updateRate(rates.imuHz);
    }
    if (bindings_.ptb)
    {
        bindings_.ptb->updateRate(rates.ptbHz);
    }
    if (bindings_.hmp)
    {
        bindings_.hmp->updateRate(rates.hmpHz);
    }
    if (bindings_.lidar)
    {
        bindings_.lidar->updateRate(rates.lidarHz);
    }
    updateTemperatureRate(rates.temperatureHz);
}

void DevicePanelCoordinator::updateTemperatureRate(double rateHz)
{
    if (bindings_.temperature)
    {
        bindings_.temperature->updateRate(rateHz);
    }
}

void DevicePanelCoordinator::clearRates()
{
    updateRates(DevicePanelRates{});
}

} // namespace VaporView::Ground::Widgets
