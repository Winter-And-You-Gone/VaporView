#include "ground/devices/SerialPortDetectionService.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
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

    SerialPortDetectionRequest emptyRequest;
    emptyRequest.english = true;
    std::vector<SerialPortDetectionService::LogEntry> logs;
    const auto outcome = SerialPortDetectionService::detect(
        emptyRequest,
        []() { return false; },
        [&](const SerialPortDetectionService::LogEntry& entry) { logs.push_back(entry); });

    require(outcome.detections.isEmpty(), "no ports produce no detections");
    require(!outcome.canceled, "empty scan is not canceled");
    require(logs.size() == 1, "empty scan emits one result log");
    require(logs.front().level == VaporView::LogLevel::Warning,
            "empty scan uses warning level");
    require(logs.front().event == QStringLiteral("serial_port_detection_no_ports"),
            "empty scan event is explicit");
    require(logs.front().fields.value(QStringLiteral("reason_code")).toString() ==
                QStringLiteral("NO_SERIAL_PORTS"),
            "empty scan reason_code is explicit");

    SerialPortDetectionRequest visibleProbeRequest;
    visibleProbeRequest.english = false;
    visibleProbeRequest.availablePorts = {QStringLiteral("VAPORVIEW_TEST_MISSING_PORT")};
    logs.clear();
    SerialPortDetectionService::detect(
        visibleProbeRequest,
        []() { return false; },
        [&](const SerialPortDetectionService::LogEntry& entry) { logs.push_back(entry); });
    const auto started = std::find_if(logs.cbegin(), logs.cend(), [](const auto& entry) {
        return entry.event == QStringLiteral("serial_port_detection_probe_started") &&
               entry.fields.value(QStringLiteral("probe_phase")).toString() == QStringLiteral("default");
    });
    require(started != logs.cend(), "default pass emits per-probe progress logs");
    require(started->level == VaporView::LogLevel::Info, "per-probe progress is visible Info");
    require(started->fields.value(QStringLiteral("ui_visibility")).toString() ==
                QStringLiteral("details"),
            "per-probe progress enters the details log view");
    require(started->fields.value(QStringLiteral("ui_message")).toString().contains(QStringLiteral("正在探测")),
            "per-probe progress has a localized UI message");

    SerialPortDetectionRequest selectedProbeRequest;
    selectedProbeRequest.english = false;
    selectedProbeRequest.epsilon = {QStringLiteral("VAPORVIEW_TEST_SELECTED_PORT"), QStringLiteral("921600")};
    logs.clear();
    SerialPortDetectionService::detect(
        selectedProbeRequest,
        []() { return false; },
        [&](const SerialPortDetectionService::LogEntry& entry) { logs.push_back(entry); });
    const auto selectedStarted = std::find_if(logs.cbegin(), logs.cend(), [](const auto& entry) {
        return entry.event == QStringLiteral("serial_port_detection_probe_started") &&
               entry.fields.value(QStringLiteral("probe_phase")).toString() == QStringLiteral("selected");
    });
    require(selectedStarted != logs.cend(), "selected pass emits per-probe progress logs");
    require(selectedStarted->level == VaporView::LogLevel::Info,
            "selected per-probe progress is visible Info");
    require(selectedStarted->fields.value(QStringLiteral("ui_visibility")).toString() ==
                QStringLiteral("details"),
            "selected per-probe progress enters the details log view");

    std::cout << "serial_port_detection_service_test passed\n";
    return 0;
}
