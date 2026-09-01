#include "ground/devices/LocalDeviceConnectionController.h"

#include <algorithm>
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

    LocalDeviceConfig config;
    config.epsilon.port = QStringLiteral("COM42");
    config.epsilon.baudText = QStringLiteral("256000");
    config.epsilon.enabled = true;
    const LocalSerialDeviceSettings requestSettings =
        makeLocalConnectionSettings(config.epsilon, true, 100, false);
    require(requestSettings.requested && requestSettings.enabled,
            "enabled model entry remains requested in connection settings");
    require(requestSettings.port == QStringLiteral("COM42") &&
                requestSettings.baudText == QStringLiteral("256000"),
            "connection settings retain a custom host baud from the non-UI model");
    require(requestSettings.sampleRateHz == 100,
            "connection settings use the effective runtime sample rate");
    const std::vector<std::pair<const char *, LocalSerialDeviceSettings *>> allHostSerialSettings = {
        {"epsilon", &config.epsilon},
        {"ptb", &config.ptb},
        {"hmp", &config.hmp},
        {"lidar", &config.lidar},
        {"temperature", &config.temperatureController},
        {"ai8", &config.ai8TemperatureController},
    };
    for (const auto& item : allHostSerialSettings)
    {
        item.second->port = QStringLiteral("COM42");
        item.second->baudText = QStringLiteral("256000");
        const LocalSerialDeviceSettings custom =
            makeLocalConnectionSettings(*item.second, true, 7, false);
        require(custom.requested && custom.enabled &&
                    custom.port == QStringLiteral("COM42") &&
                    custom.baudText == QStringLiteral("256000") &&
                    custom.sampleRateHz == 7,
                "every local host serial device retains a custom baud in its non-UI model");
    }
    const LocalSerialDeviceSettings skippedSettings =
        makeLocalConnectionSettings(config.epsilon, false, 100, false);
    require(!skippedSettings.requested && !skippedSettings.enabled,
            "non-requested device is disabled without changing the stored model");

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
                 entry.fields.value(QStringLiteral("device")).toString() == QStringLiteral("TFA1500-L") &&
                 entry.fields.value(QStringLiteral("reason_code")).toString() == QStringLiteral("PORT_NOT_SELECTED"));
        }
        require(foundSummary, "no-port result logged as structured warning");
        require(foundLidarSkip, "lidar skip log uses structured device field");
    }

    controller.disconnect();

    {
        std::lock_guard<std::mutex> lock(mutex);
        logs.clear();
    }
    finished = false;
    connected = true;
    LocalConnectionRequest singleDeviceRequest;
    singleDeviceRequest.english = false;
    singleDeviceRequest.selectText = QStringLiteral("未选择");
    singleDeviceRequest.epsilon.port = QStringLiteral("__invalid_vaporview_port__");
    singleDeviceRequest.epsilon.baudText = QStringLiteral("256000");
    singleDeviceRequest.ptb.requested = false;
    singleDeviceRequest.hmp.requested = false;
    singleDeviceRequest.lidar.requested = false;
    singleDeviceRequest.temperatureController.requested = false;
    singleDeviceRequest.ai8TemperatureController.requested = false;
    require(controller.connectAsync(singleDeviceRequest),
            "start single requested-device connection attempt");
    controller.wait();
    require(finished && !connected,
            "single requested-device failure completes without connected devices");
    {
        std::lock_guard<std::mutex> lock(mutex);
        bool foundEpsilonAttempt = false;
        bool foundCustomEpsilonBaud = false;
        bool foundUnexpectedSkippedDevice = false;
        for (const LocalConnectionLogEntry& entry : logs)
        {
            foundEpsilonAttempt = foundEpsilonAttempt ||
                (entry.event == QStringLiteral("local_device_connection_started") &&
                 entry.fields.value(QStringLiteral("device")).toString() == QStringLiteral("EPSILON"));
            foundCustomEpsilonBaud = foundCustomEpsilonBaud ||
                (entry.event == QStringLiteral("local_device_connection_started") &&
                 entry.fields.value(QStringLiteral("device")).toString() == QStringLiteral("EPSILON") &&
                 entry.fields.value(QStringLiteral("baud")).toString() == QStringLiteral("256000"));
            foundUnexpectedSkippedDevice = foundUnexpectedSkippedDevice ||
                (entry.event == QStringLiteral("local_device_connection_skipped") &&
                 entry.fields.value(QStringLiteral("device")).toString() != QStringLiteral("EPSILON"));
        }
        require(foundEpsilonAttempt, "single requested-device attempt logs the target device");
        require(foundCustomEpsilonBaud,
                "single requested-device attempt passes the custom baud into the serial backend");
        require(!foundUnexpectedSkippedDevice,
                "non-requested devices do not emit skipped-device noise");
    }

    controller.disconnect();
    {
        std::lock_guard<std::mutex> lock(mutex);
        logs.clear();
    }
    finished = false;
    connected = true;
    LocalConnectionRequest invalidBaudRequest = singleDeviceRequest;
    invalidBaudRequest.epsilon.baudText = QStringLiteral("0");
    require(controller.connectAsync(invalidBaudRequest),
            "start invalid-baud connection attempt");
    controller.wait();
    require(finished && !connected,
            "invalid host baud completes without connecting a device");
    {
        std::lock_guard<std::mutex> lock(mutex);
        const bool rejectedInvalidBaud = std::any_of(
            logs.cbegin(), logs.cend(), [](const LocalConnectionLogEntry& entry) {
                return entry.event == QStringLiteral("local_device_connection_rejected_invalid_baud") &&
                    entry.fields.value(QStringLiteral("device")).toString() == QStringLiteral("EPSILON") &&
                    entry.fields.value(QStringLiteral("configured_baud")).toString() == QStringLiteral("0");
            });
        require(rejectedInvalidBaud,
                "connection controller rejects a non-positive host baud before opening the port");
    }

    LocalSampleRateConfiguration rateConfiguration;
    const LocalSampleRateApplyResult allRateResult =
        controller.applyRunningSampleRates(rateConfiguration);
    require(!allRateResult.epsilonDeviceRateAttempted,
            "epsilon device rate is not reported as attempted without a running collector");
    const LocalSampleRateApplyResult epsilonRateResult =
        controller.setEpsilonSampleRate(100, {{0x40u, 100}});
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
