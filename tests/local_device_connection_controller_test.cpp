#include "ground/devices/LocalDeviceConnectionController.h"

#include <cstdlib>
#include <iostream>
#include <mutex>
#include <vector>

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

}  // namespace

int main()
{
    using namespace VaporView::Ground::Devices;

    LocalDeviceConnectionController controller;
    std::mutex mutex;
    std::vector<LocalConnectionLogEntry> logs;
    bool finished = false;
    bool connected = true;
    LocalConnectionCallbacks callbacks;
    callbacks.log = [&](const LocalConnectionLogEntry& entry) {
        std::lock_guard<std::mutex> lock(mutex);
        logs.push_back(entry);
    };
    callbacks.finished = [&](bool result) {
        connected = result;
        finished = true;
    };
    controller.setCallbacks(std::move(callbacks));

    LocalConnectionRequest request;
    request.english = false;
    request.selectText = QStringLiteral("未选择");
    request.epsilon.port = request.selectText;
    request.ptb.port = request.selectText;
    request.hmp.port = request.selectText;
    request.lidar.port = request.selectText;
    request.temperatureController.port = request.selectText;
    require(controller.connectAsync(request), "start asynchronous connection attempt");
    controller.wait();

    require(finished, "finished callback");
    require(!connected, "no selected devices is not connected");
    require(!controller.connectionInProgress(), "controller returns to idle");
    require(!controller.anyCollectorRunning(), "no collectors running");
    {
        std::lock_guard<std::mutex> lock(mutex);
        bool foundSummary = false;
        bool foundLidarSkip = false;
        for (const LocalConnectionLogEntry& entry : logs)
        {
            foundSummary = foundSummary ||
                (entry.event == QStringLiteral("local_serial_devices_not_connected") &&
                 entry.level == VaporView::LogLevel::Warning &&
                 entry.fields.value(QStringLiteral("reason_code")).toString() == QStringLiteral("NO_DEVICE_CONNECTED"));
            foundLidarSkip = foundLidarSkip ||
                (entry.event == QStringLiteral("local_device_connection_skipped") &&
                 entry.fields.value(QStringLiteral("device")).toString() == QStringLiteral("TFA1005-L") &&
                 entry.fields.value(QStringLiteral("reason_code")).toString() == QStringLiteral("PORT_NOT_SELECTED"));
        }
        require(foundSummary, "no-port result logged as structured warning");
        require(foundLidarSkip, "lidar skip log uses structured device field");
    }

    controller.disconnect();

    LocalSampleRateConfiguration rateConfiguration;
    const LocalSampleRateApplyResult allRateResult =
        controller.applyRunningSampleRates(rateConfiguration);
    require(!allRateResult.epsilonDeviceRateAttempted,
            "epsilon device rate is not reported as attempted without a running collector");
    const LocalSampleRateApplyResult epsilonRateResult =
        controller.setEpsilonSampleRate(100, {{0x40u, 100}}, true);
    require(!epsilonRateResult.epsilonDeviceRateAttempted,
            "single epsilon rate change is not reported as applied without a running collector");

    LocalTemperatureConnectionRequest temperatureRequest;
    temperatureRequest.english = true;
    temperatureRequest.port = QStringLiteral("__invalid_vaporview_port__");
    temperatureRequest.baudText = QStringLiteral("38400");
    temperatureRequest.baudRate = 38400;
    temperatureRequest.sampleRateHz = 2;
    bool temperatureFinished = false;
    bool temperatureConnected = true;
    QString temperatureResult;
    require(
        controller.connectTemperatureAsync(
            temperatureRequest,
            [&](bool result, const QString& message) {
                temperatureConnected = result;
                temperatureResult = message;
                temperatureFinished = true;
            }),
        "start single temperature-controller connection attempt");
    controller.wait();
    require(temperatureFinished, "single temperature completion callback");
    require(!temperatureConnected, "invalid temperature port is rejected");
    require(temperatureResult.contains(QStringLiteral("Failed to open")), "temperature failure has actionable detail");
    require(!controller.anyCollectorRunning(), "failed temperature connection leaves no collector running");

    std::cout << "local_device_connection_controller_test passed\n";
    return 0;
}
