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
    config.hmp.port = QStringLiteral("COM42");
    config.hmp.baudText = QStringLiteral("256000");
    config.hmp.enabled = true;
    const LocalSerialDeviceSettings requestSettings =
        makeLocalConnectionSettings(config.hmp, true, 100, false);
    require(requestSettings.requested && requestSettings.enabled,
            "enabled model entry remains requested in connection settings");
    require(requestSettings.port == QStringLiteral("COM42") &&
                requestSettings.baudText == QStringLiteral("256000"),
            "connection settings retain a custom host-link baud from the non-UI model");
    require(requestSettings.sampleRateHz == 100,
            "connection settings use the effective runtime sample rate");
    const std::vector<std::pair<const char *, LocalSerialDeviceSettings *>> customHostSerialSettings = {
        {"hmp", &config.hmp},
        {"lidar", &config.lidar},
    };
    for (const auto& item : customHostSerialSettings)
    {
        item.second->port = QStringLiteral("COM42");
        item.second->baudText = QStringLiteral("256000");
        const LocalSerialDeviceSettings custom =
            makeLocalConnectionSettings(*item.second, true, 7, false);
        require(custom.requested && custom.enabled &&
                    custom.port == QStringLiteral("COM42") &&
                    custom.baudText == QStringLiteral("256000") &&
                    custom.sampleRateHz == 7,
                "custom-capable local host links retain a custom baud in the non-UI model");
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
    singleDeviceRequest.epsilon.requested = false;
    singleDeviceRequest.ptb.requested = false;
    singleDeviceRequest.hmp.port = QStringLiteral("__invalid_vaporview_port__");
    singleDeviceRequest.hmp.baudText = QStringLiteral("256000");
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
        bool foundHmpAttempt = false;
        bool foundCustomHmpBaud = false;
        bool foundUnexpectedSkippedDevice = false;
        for (const LocalConnectionLogEntry& entry : logs)
        {
            foundHmpAttempt = foundHmpAttempt ||
                (entry.event == QStringLiteral("local_device_connection_started") &&
                 entry.fields.value(QStringLiteral("device")).toString() == QStringLiteral("HMP3"));
            foundCustomHmpBaud = foundCustomHmpBaud ||
                (entry.event == QStringLiteral("local_device_connection_started") &&
                 entry.fields.value(QStringLiteral("device")).toString() == QStringLiteral("HMP3") &&
                 entry.fields.value(QStringLiteral("baud")).toString() == QStringLiteral("256000"));
            foundUnexpectedSkippedDevice = foundUnexpectedSkippedDevice ||
                (entry.event == QStringLiteral("local_device_connection_skipped") &&
                 entry.fields.value(QStringLiteral("device")).toString() != QStringLiteral("HMP3"));
        }
        require(foundHmpAttempt, "single requested-device attempt logs the target device");
        require(foundCustomHmpBaud,
                "single requested-device attempt passes custom host-link baud into the serial backend");
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
    invalidBaudRequest.hmp.baudText = QStringLiteral("0");
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
                    entry.fields.value(QStringLiteral("device")).toString() == QStringLiteral("HMP3") &&
                    entry.fields.value(QStringLiteral("configured_baud")).toString() == QStringLiteral("0");
            });
        require(rejectedInvalidBaud,
                "connection controller rejects a non-positive host baud before opening the port");
    }

    controller.disconnect();
    {
        std::lock_guard<std::mutex> lock(mutex);
        logs.clear();
    }
    finished = false;
    connected = true;
    LocalConnectionRequest unsupportedAi8Request;
    unsupportedAi8Request.english = false;
    unsupportedAi8Request.selectText = QStringLiteral("未选择");
    unsupportedAi8Request.epsilon.requested = false;
    unsupportedAi8Request.ptb.requested = false;
    unsupportedAi8Request.hmp.requested = false;
    unsupportedAi8Request.lidar.requested = false;
    unsupportedAi8Request.temperatureController.requested = false;
    unsupportedAi8Request.ai8TemperatureController.port = QStringLiteral("__invalid_vaporview_port__");
    unsupportedAi8Request.ai8TemperatureController.baudText = QStringLiteral("256000");
    require(controller.connectAsync(unsupportedAi8Request),
            "start unsupported AI-8288 baud connection attempt");
    controller.wait();
    require(finished && !connected,
            "unsupported AI-8288 baud completes without connecting a device");
    {
        std::lock_guard<std::mutex> lock(mutex);
        const bool rejectedUnsupportedAi8Baud = std::any_of(
            logs.cbegin(), logs.cend(), [](const LocalConnectionLogEntry& entry) {
                return entry.event == QStringLiteral("local_device_connection_rejected_unsupported_baud") &&
                    entry.fields.value(QStringLiteral("device")).toString() == QStringLiteral("AI-8288") &&
                    entry.fields.value(QStringLiteral("configured_baud")).toString() == QStringLiteral("256000") &&
                    entry.fields.value(QStringLiteral("reason_code")).toString() ==
                        QStringLiteral("UNSUPPORTED_BAUD_RATE");
            });
        const bool openedAi8Port = std::any_of(
            logs.cbegin(), logs.cend(), [](const LocalConnectionLogEntry& entry) {
                return entry.event == QStringLiteral("local_device_connection_started") &&
                    entry.fields.value(QStringLiteral("device")).toString() == QStringLiteral("AI-8288");
            });
        require(rejectedUnsupportedAi8Baud && !openedAi8Port,
                "AI-8288 unsupported baud is rejected before a serial-port open is attempted");
    }

    controller.disconnect();
    {
        std::lock_guard<std::mutex> lock(mutex);
        logs.clear();
    }
    finished = false;
    connected = true;
    LocalConnectionRequest unsupportedLidarRequest;
    unsupportedLidarRequest.english = false;
    unsupportedLidarRequest.selectText = QStringLiteral("未选择");
    unsupportedLidarRequest.epsilon.requested = false;
    unsupportedLidarRequest.ptb.requested = false;
    unsupportedLidarRequest.hmp.requested = false;
    unsupportedLidarRequest.lidar.port = QStringLiteral("__invalid_vaporview_port__");
    unsupportedLidarRequest.lidar.baudText = QStringLiteral("9600");
    unsupportedLidarRequest.lidar.requested = true;
    unsupportedLidarRequest.temperatureController.requested = false;
    unsupportedLidarRequest.ai8TemperatureController.requested = false;
    require(controller.connectAsync(unsupportedLidarRequest),
            "start unsupported TFA1500-L baud connection attempt");
    controller.wait();
    require(finished && !connected,
            "unsupported TFA1500-L baud completes without connecting a device");
    {
        std::lock_guard<std::mutex> lock(mutex);
        const bool rejectedUnsupportedLidarBaud = std::any_of(
            logs.cbegin(), logs.cend(), [](const LocalConnectionLogEntry& entry) {
                return entry.event == QStringLiteral("local_device_connection_rejected_unsupported_baud") &&
                    entry.fields.value(QStringLiteral("device")).toString() == QStringLiteral("TFA1500-L") &&
                    entry.fields.value(QStringLiteral("configured_baud")).toString() == QStringLiteral("9600") &&
                    entry.fields.value(QStringLiteral("reason_code")).toString() ==
                        QStringLiteral("UNSUPPORTED_BAUD_RATE");
            });
        const bool openedLidarPort = std::any_of(
            logs.cbegin(), logs.cend(), [](const LocalConnectionLogEntry& entry) {
                return entry.event == QStringLiteral("local_device_connection_started") &&
                    entry.fields.value(QStringLiteral("device")).toString() == QStringLiteral("TFA1500-L");
            });
        require(rejectedUnsupportedLidarBaud && !openedLidarPort,
                "TFA1500-L 9600 is rejected before a serial-port open is attempted");
    }

    controller.disconnect();
    {
        std::lock_guard<std::mutex> lock(mutex);
        logs.clear();
    }
    finished = false;
    connected = true;
    unsupportedLidarRequest.lidar.baudText = QStringLiteral("750000");
    require(controller.connectAsync(unsupportedLidarRequest),
            "start custom TFA1500-L baud connection attempt");
    controller.wait();
    require(finished && !connected,
            "custom TFA1500-L baud reaches the serial backend and then fails only on the test port");
    {
        std::lock_guard<std::mutex> lock(mutex);
        const bool attemptedCustomLidar = std::any_of(
            logs.cbegin(), logs.cend(), [](const LocalConnectionLogEntry& entry) {
                return entry.event == QStringLiteral("local_device_connection_started") &&
                    entry.fields.value(QStringLiteral("device")).toString() == QStringLiteral("TFA1500-L") &&
                    entry.fields.value(QStringLiteral("baud")).toString() == QStringLiteral("750000");
            });
        require(attemptedCustomLidar,
                "TFA1500-L accepts a non-preset 750000 baud before opening the port");
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
