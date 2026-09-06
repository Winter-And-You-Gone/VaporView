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
    selectedProbeRequest.ai8TemperatureController = {
        QStringLiteral("VAPORVIEW_TEST_SELECTED_PORT"), QStringLiteral("256000")};
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
    require(selectedStarted->fields.value(QStringLiteral("baud")).toString() ==
                QStringLiteral("19200"),
            "AI-8288 selected probe falls back to the documented default baud");
    const auto rejectedAi8 = std::find_if(logs.cbegin(), logs.cend(), [](const auto& entry) {
        return entry.event == QStringLiteral("serial_port_detection_rejected_unsupported_baud") &&
               entry.fields.value(QStringLiteral("device_key")).toString() == QStringLiteral("ai8") &&
               entry.fields.value(QStringLiteral("configured_baud")).toString() == QStringLiteral("256000") &&
               entry.fields.value(QStringLiteral("fallback_baud")).toString() == QStringLiteral("19200") &&
               entry.fields.value(QStringLiteral("reason_code")).toString() ==
                   QStringLiteral("UNSUPPORTED_BAUD_RATE");
    });
    require(rejectedAi8 != logs.cend(),
            "AI-8288 auto-detect reports an unsupported configured baud before probing");

    SerialPortDetectionRequest invalidLidarRequest;
    invalidLidarRequest.english = false;
    invalidLidarRequest.lidar = {
        QStringLiteral("VAPORVIEW_TEST_LIDAR_PORT"), QStringLiteral("9600")};
    logs.clear();
    SerialPortDetectionService::detect(
        invalidLidarRequest,
        []() { return false; },
        [&](const SerialPortDetectionService::LogEntry& entry) { logs.push_back(entry); });
    const auto rejectedLidar = std::find_if(logs.cbegin(), logs.cend(), [](const auto& entry) {
        return entry.event == QStringLiteral("serial_port_detection_rejected_unsupported_baud") &&
               entry.fields.value(QStringLiteral("device_key")).toString() == QStringLiteral("lidar") &&
               entry.fields.value(QStringLiteral("configured_baud")).toString() == QStringLiteral("9600") &&
               entry.fields.value(QStringLiteral("fallback_baud")).toString() == QStringLiteral("500000") &&
               entry.fields.value(QStringLiteral("reason_code")).toString() ==
                   QStringLiteral("UNSUPPORTED_BAUD_RATE");
    });
    const auto lidarFallbackStarted = std::find_if(logs.cbegin(), logs.cend(), [](const auto& entry) {
        return entry.event == QStringLiteral("serial_port_detection_probe_started") &&
               entry.fields.value(QStringLiteral("probe_phase")).toString() == QStringLiteral("selected") &&
               entry.fields.value(QStringLiteral("device_key")).toString() == QStringLiteral("lidar") &&
               entry.fields.value(QStringLiteral("baud")).toString() == QStringLiteral("500000");
    });
    require(rejectedLidar != logs.cend() && lidarFallbackStarted != logs.cend(),
            "TFA1500-L auto-detect rejects 9600 and probes the legal 500000 default");

    SerialPortDetectionRequest customLidarRequest;
    customLidarRequest.english = false;
    customLidarRequest.lidar = {
        QStringLiteral("VAPORVIEW_TEST_LIDAR_PORT"), QStringLiteral("750000")};
    logs.clear();
    SerialPortDetectionService::detect(
        customLidarRequest,
        []() { return false; },
        [&](const SerialPortDetectionService::LogEntry& entry) { logs.push_back(entry); });
    const auto customLidarStarted = std::find_if(logs.cbegin(), logs.cend(), [](const auto& entry) {
        return entry.event == QStringLiteral("serial_port_detection_probe_started") &&
               entry.fields.value(QStringLiteral("probe_phase")).toString() == QStringLiteral("selected") &&
               entry.fields.value(QStringLiteral("device_key")).toString() == QStringLiteral("lidar") &&
               entry.fields.value(QStringLiteral("baud")).toString() == QStringLiteral("750000");
    });
    require(customLidarStarted != logs.cend(),
            "TFA1500-L auto-detect preserves a legal non-preset 750000 baud");

    SerialPortDetectionRequest customProbeRequest;
    customProbeRequest.english = false;
    customProbeRequest.hmp = {
        QStringLiteral("VAPORVIEW_TEST_CUSTOM_PORT"), QStringLiteral("256000")};
    logs.clear();
    SerialPortDetectionService::detect(
        customProbeRequest,
        []() { return false; },
        [&](const SerialPortDetectionService::LogEntry& entry) { logs.push_back(entry); });
    const auto customStarted = std::find_if(logs.cbegin(), logs.cend(), [](const auto& entry) {
        return entry.event == QStringLiteral("serial_port_detection_probe_started") &&
               entry.fields.value(QStringLiteral("probe_phase")).toString() == QStringLiteral("selected") &&
               entry.fields.value(QStringLiteral("device_key")).toString() == QStringLiteral("hmp");
    });
    require(customStarted != logs.cend() &&
                customStarted->fields.value(QStringLiteral("baud")).toString() ==
                    QStringLiteral("256000"),
            "custom-capable HMP host link retains a selected custom baud during auto-detect");

    SerialPortDetectionRequest sourceAwareRequest;
    sourceAwareRequest.ptb = {
        QStringLiteral("VAPORVIEW_TEST_BMP_PORT"), QStringLiteral("256000")};
    sourceAwareRequest.hmp = {
        QStringLiteral("VAPORVIEW_TEST_SHT45_PORT"), QStringLiteral("256000")};
    sourceAwareRequest.pressureProtocol = VaporView::PressureSensorProtocol::Bmp390Serial;
    sourceAwareRequest.humidityProtocol = VaporView::HumiditySensorProtocol::Sht45Serial;
    logs.clear();
    SerialPortDetectionService::detect(
        sourceAwareRequest,
        []() { return false; },
        [&](const SerialPortDetectionService::LogEntry& entry) { logs.push_back(entry); });
    const auto bmpStarted = std::find_if(logs.cbegin(), logs.cend(), [](const auto& entry) {
        return entry.event == QStringLiteral("serial_port_detection_probe_started") &&
               entry.fields.value(QStringLiteral("probe_phase")).toString() == QStringLiteral("selected") &&
               entry.fields.value(QStringLiteral("device_key")).toString() == QStringLiteral("ptb");
    });
    const auto shtStarted = std::find_if(logs.cbegin(), logs.cend(), [](const auto& entry) {
        return entry.event == QStringLiteral("serial_port_detection_probe_started") &&
               entry.fields.value(QStringLiteral("probe_phase")).toString() == QStringLiteral("selected") &&
               entry.fields.value(QStringLiteral("device_key")).toString() == QStringLiteral("hmp");
    });
    require(bmpStarted != logs.cend() &&
                bmpStarted->fields.value(QStringLiteral("device")).toString() ==
                    QStringLiteral("BMP390") &&
                bmpStarted->fields.value(QStringLiteral("baud")).toString() ==
                    QStringLiteral("256000"),
            "BMP390 source keeps its custom adapter baud during auto-detect");
    require(shtStarted != logs.cend() &&
                shtStarted->fields.value(QStringLiteral("device")).toString() ==
                    QStringLiteral("SHT45") &&
                shtStarted->fields.value(QStringLiteral("baud")).toString() ==
                    QStringLiteral("256000"),
            "SHT45 source keeps its custom adapter baud during auto-detect");

    std::cout << "serial_port_detection_service_test passed\n";
    return 0;
}
