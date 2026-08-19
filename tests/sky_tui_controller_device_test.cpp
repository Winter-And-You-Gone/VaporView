#include "SkyTuiController.h"

#include <QCoreApplication>
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

bool hasPaletteCommand(const QList<VaporView::SkyTuiCommandItem>& items, const QString& command)
{
    for (const VaporView::SkyTuiCommandItem& item : items)
    {
        if (item.command == command)
        {
            return true;
        }
    }
    return false;
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    VaporView::SkyTuiOptions options;
    VaporView::SkyTuiController controller(nullptr, options);

    VaporView::SkyDeviceId id = VaporView::SkyDeviceId::All;
    require(controller.parseDeviceNameForTest(QStringLiteral("rd105"), id) &&
                id == VaporView::SkyDeviceId::TemperatureController,
            "TUI parses RD105 device alias");
    require(controller.parseDeviceNameForTest(QStringLiteral("temperature_controller"), id) &&
                id == VaporView::SkyDeviceId::TemperatureController,
            "TUI parses temperature_controller alias");
    require(controller.parseDeviceNameForTest(QStringLiteral("激光温控"), id) &&
                id == VaporView::SkyDeviceId::TemperatureController,
            "TUI parses laser thermal display name");
    require(controller.parseDeviceNameForTest(QStringLiteral("ai8"), id) &&
                id == VaporView::SkyDeviceId::Ai8TemperatureController,
            "TUI parses AI-8 device alias");
    require(controller.parseDeviceNameForTest(QStringLiteral("ai-8288"), id) &&
                id == VaporView::SkyDeviceId::Ai8TemperatureController,
            "TUI parses AI-8288 device alias");
    require(controller.parseDeviceNameForTest(QStringLiteral("系统温控"), id) &&
                id == VaporView::SkyDeviceId::Ai8TemperatureController,
            "TUI parses system thermal display name");

    const QList<VaporView::SkyTuiCommandItem> palette = controller.commandPalette();
    require(hasPaletteCommand(palette, QStringLiteral("/connect rd105")) &&
                hasPaletteCommand(palette, QStringLiteral("/connect ai8")),
            "TUI command palette exposes formal temperature-controller devices");

    std::cout << "sky_tui_controller_device_test passed\n";
    return 0;
}
