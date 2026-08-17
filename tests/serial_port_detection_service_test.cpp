#include "ground/devices/SerialPortDetectionService.h"

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

    SerialPortDetectionRequest request;
    request.english = true;
    std::vector<SerialPortDetectionService::LogEntry> logs;
    const auto outcome = SerialPortDetectionService::detect(
        request,
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

    std::cout << "serial_port_detection_service_test passed\n";
    return 0;
}
