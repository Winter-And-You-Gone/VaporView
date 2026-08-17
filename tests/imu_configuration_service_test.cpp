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
    std::vector<ImuConfigurationService::LogEntry> logs;
    require(
        ImuConfigurationService::apply(
            request,
            {},
            [&](const ImuConfigurationService::LogEntry& entry) { logs.push_back(entry); }),
        "offline profile remains saved when the direct port cannot be opened");
    require(!logs.empty(), "offline profile emits structured log");
    const ImuConfigurationService::LogEntry& entry = logs.back();
    require(entry.level == VaporView::LogLevel::Warning, "offline profile uses warning level");
    require(entry.category == QStringLiteral("device.navigation.command"), "offline profile category");
    require(entry.event == QStringLiteral("imu_profile_direct_open_failed_saved"),
            "offline profile event is explicit");
    require(entry.fields.value(QStringLiteral("reason_code")).toString() == QStringLiteral("PORT_OPEN_FAILED"),
            "offline profile reason_code");
    require(entry.fields.value(QStringLiteral("port")).toString() == request.port,
            "offline profile preserves port");

    std::cout << "imu_configuration_service_test passed\n";
    return 0;
}
