#include "SkyDeviceManager.h"

#include <QCoreApplication>
#include <QMetaObject>

#include <cmath>
#include <cstdlib>
#include <iostream>

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

void generateSimulatedData(VaporView::SkyDeviceManager& manager)
{
    require(QMetaObject::invokeMethod(&manager, "generateSimulatedData", Qt::DirectConnection),
            "invoke simulated data generation");
}

bool hasStatus(const QVector<VaporView::DeviceStatusItem>& statuses, VaporView::SkyDeviceId id)
{
    for (const VaporView::DeviceStatusItem& status : statuses)
    {
        if (status.device_id == id)
        {
            return true;
        }
    }
    return false;
}

void requireConnectedWithData(const VaporView::SkyDeviceManager& manager, VaporView::SkyDeviceId id)
{
    const VaporView::DeviceStatusItem status = manager.status(id);
    require(status.state == VaporView::DeviceState::Connected, "simulated device is connected");
    require(status.rx_count > 0, "simulated device produced data");
    require(status.last_data_time_us > 0, "simulated device last data time updated");
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    VaporView::SkyConfig config = VaporView::SkyConfig::defaults();
    config.epsilon.enabled = true;
    config.ptb = {true, QStringLiteral("/dev/test-bmp390"), 115200, 20.0};
    config.ptb.source = QStringLiteral("bmp390");
    config.hmp = {true, QStringLiteral("/dev/test-sht45"), 115200, 20.0};
    config.hmp.source = QStringLiteral("sht45");
    config.lidar.enabled = true;
    config.temperature_controller = {true, QStringLiteral("/dev/test-rd105"), 38400, 5.0, 3};
    config.ai8_temperature_controller = {true, QStringLiteral("/dev/test-ai8"), 19200, 5.0, 5};
    config.wave_tcp.enabled = true;

    VaporView::SkyDeviceManager manager;
    manager.loadConfig(config);
    manager.setSimulateData(true);
    manager.connectAll();
    generateSimulatedData(manager);

    const QVector<VaporView::DeviceStatusItem> statuses = manager.allStatuses();
    require(statuses.size() == 7, "allStatuses covers all formal Sky devices");
    for (VaporView::SkyDeviceId id : {VaporView::SkyDeviceId::Epsilon,
                                      VaporView::SkyDeviceId::Ptb,
                                      VaporView::SkyDeviceId::Hmp,
                                      VaporView::SkyDeviceId::Lidar,
                                      VaporView::SkyDeviceId::TemperatureController,
                                      VaporView::SkyDeviceId::Ai8TemperatureController,
                                      VaporView::SkyDeviceId::WaveTcp})
    {
        require(hasStatus(statuses, id), "allStatuses contains formal device");
        requireConnectedWithData(manager, id);
    }

    require(manager.latestEpsilon().valid, "simulated EPSILON data is valid");
    require(manager.latestPtb().valid && std::fabs(manager.latestPtb().pressure_hpa) > 1.0,
            "simulated pressure data is valid");
    require(manager.latestHmp().valid && std::fabs(manager.latestHmp().humidity) > 1.0,
            "simulated humidity data is valid");
    require(manager.latestLidar().valid && manager.latestLidar().distance_m > 0.0,
            "simulated lidar data is valid");
    require(manager.latestTemperatureController().valid,
            "simulated RD105 data is valid");
    require(manager.latestAi8TemperatureController().valid &&
                manager.latestAi8TemperatureController().controlStatesValid,
            "simulated AI-8 data is valid");
    require(!manager.latestWaveform().isEmpty() && !manager.latestRawWaveform().isEmpty(),
            "simulated Wave TCP data is valid");

    VaporView::CommandErrorCode error = VaporView::CommandErrorCode::Ok;
    require(manager.setTemperatureTarget(1, 31.0, &error) && error == VaporView::CommandErrorCode::Ok,
            "RD105 simulated set target succeeds");
    require(manager.setTemperatureOutputEnabled(1, true, &error) && error == VaporView::CommandErrorCode::Ok,
            "RD105 simulated output enable succeeds");
    require(!manager.setTemperatureTarget(3, 31.0, &error) &&
                error == VaporView::CommandErrorCode::InvalidPayload,
            "RD105 simulated command rejects invalid channel");
    generateSimulatedData(manager);
    const VaporView::TemperatureControllerData rd105 = manager.latestTemperatureController();
    require(std::fabs(rd105.channels[0].target_temperature_c - 31.0) < 0.000001 &&
                rd105.channels[0].output_enabled,
            "RD105 simulated telemetry reflects commands");

    require(manager.disconnectDevice(VaporView::SkyDeviceId::Ai8TemperatureController, &error) &&
                error == VaporView::CommandErrorCode::Ok,
            "AI-8 simulated disconnect succeeds");
    require(manager.status(VaporView::SkyDeviceId::Ai8TemperatureController).state ==
                VaporView::DeviceState::Disconnected,
            "AI-8 simulated status is disconnected");
    require(!manager.latestAi8TemperatureController().valid,
            "AI-8 simulated data invalidates on disconnect");
    generateSimulatedData(manager);
    require(!manager.latestAi8TemperatureController().valid,
            "AI-8 simulated data stays invalid while disconnected");

    require(manager.reconnectDevice(VaporView::SkyDeviceId::Ai8TemperatureController, &error) &&
                error == VaporView::CommandErrorCode::Ok,
            "AI-8 simulated reconnect succeeds");
    generateSimulatedData(manager);
    requireConnectedWithData(manager, VaporView::SkyDeviceId::Ai8TemperatureController);
    require(manager.latestAi8TemperatureController().valid,
            "AI-8 simulated data resumes after reconnect");

    VaporView::SkyConfig disabledAi8 = manager.config();
    disabledAi8.ai8_temperature_controller.enabled = false;
    const VaporView::ApplyConfigResult result = manager.applyConfig(disabledAi8);
    require(result.success, "simulated apply config succeeds");
    require(manager.status(VaporView::SkyDeviceId::Ai8TemperatureController).state ==
                VaporView::DeviceState::Disabled,
            "AI-8 simulated apply disable updates status");
    require(!manager.latestAi8TemperatureController().valid,
            "AI-8 simulated apply disable invalidates data");

    manager.setSimulateData(false);
    std::cout << "sky_device_manager_simulation_test passed\n";
    return 0;
}
