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
    std::vector<QString> logs;
    const auto outcome = SerialPortDetectionService::detect(
        request,
        []() { return false; },
        [&](const QString& message) { logs.push_back(message); });

    require(outcome.detections.isEmpty(), "no ports produce no detections");
    require(!outcome.canceled, "empty scan is not canceled");
    require(logs.size() == 1, "empty scan emits one result log");
    require(logs.front().contains(QStringLiteral("no serial ports")),
            "empty scan explains missing ports");

    std::cout << "serial_port_detection_service_test passed\n";
    return 0;
}
