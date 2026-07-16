#include "ground/devices/ImuConfigurationService.h"

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

    require(ImuConfigurationService::isSupported(QStringLiteral("HI91"), 200), "HI91 at 200 Hz is supported");
    require(ImuConfigurationService::isSupported(QStringLiteral(" hi92 "), 1000), "format normalization is supported");
    require(!ImuConfigurationService::isSupported(QStringLiteral("HI81"), 200), "unsupported output format rejected");
    require(!ImuConfigurationService::isSupported(QStringLiteral("HI91"), 333), "unsupported sample rate rejected");

    ImuProfileRequest request;
    request.english = true;
    request.port = QStringLiteral("__invalid_vaporview_port__");
    request.outputFormat = QStringLiteral("HI91");
    request.currentBaud = 921600;
    request.targetBaud = 921600;
    request.targetRateHz = 200;
    std::vector<QString> logs;
    require(
        ImuConfigurationService::apply(
            request,
            {},
            [&](const QString& message) { logs.push_back(message); }),
        "offline profile remains saved when the direct port cannot be opened");
    require(!logs.empty() && logs.back().contains(QStringLiteral("saved for next connection")),
            "offline profile result is explained");

    std::cout << "imu_configuration_service_test passed\n";
    return 0;
}
