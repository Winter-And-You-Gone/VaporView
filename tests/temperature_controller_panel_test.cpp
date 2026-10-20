#include "test_settings_sandbox.h"
#include "ground/widgets/TelemetryPanels.h"
#include "ground/widgets/TemperatureControllerWidgets.h"
#include "ground/widgets/DevicePanelCoordinator.h"
#include "shared/theme/AppTheme.h"

#include <QApplication>
#include <QColor>
#include <QWidget>

#include <cmath>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>

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

QWidget *findConfigTemperaturePlot(TemperatureControllerPanel& panel)
{
    for (QWidget *plot : panel.findChildren<QWidget *>(QStringLiteral("temperatureTrendPlot")))
    {
        if (plot && plot->property("temperatureConfigPlot").toBool())
        {
            return plot;
        }
    }
    return nullptr;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    TemperatureControllerPanel panel;
    std::unique_ptr<VaporView::Ground::Widgets::TemperatureControllerOverviewPanel> overview(
        VaporView::Ground::Widgets::createTemperatureControllerOverviewPanel());
    VaporView::Ground::Widgets::DevicePanelBindings bindings;
    bindings.temperature = &panel;
    bindings.temperatureOverview = overview.get();
    VaporView::Ground::Widgets::DevicePanelCoordinator coordinator(bindings);
    panel.resize(900, 420);
    panel.show();
    QApplication::processEvents();

    QWidget *plot = findConfigTemperaturePlot(panel);
    require(plot != nullptr, "RD105 temperature page exposes the configuration trend plot");
    require(plot->property("xAxisTimeMode").toBool(),
            "RD105 configuration trend plot uses the same time axis as the home overview");

    VaporView::TemperatureControllerData data;
    data.valid = true;
    data.internal_temperature_c = 25.0;
    data.channels[0].target_temperature_c = 25.0;
    data.channels[0].measured_temperature_c = 24.6;
    data.channels[1].target_temperature_c = 27.0;
    data.channels[1].measured_temperature_c = 26.8;
    data.timestamp = std::chrono::steady_clock::now();
    coordinator.updateTemperatureData(data);
    QApplication::processEvents();

    data.channels[0].measured_temperature_c = 24.7;
    data.channels[1].measured_temperature_c = 26.9;
    data.timestamp += std::chrono::seconds(1);
    coordinator.updateTemperatureData(data);
    QApplication::processEvents();
    plot->repaint();

    const double targetGuideLineY = plot->property("targetGuideLineY").toDouble();
    require(plot->property("sampleCount").toInt() >= 2 &&
                plot->property("xAxisTimeSampleCount").toInt() ==
                    plot->property("sampleCount").toInt() &&
                plot->property("targetGuideLineVisible").toBool() &&
                plot->property("targetGuideLineColor").toString() ==
                    VaporView::appThemeColor(VaporView::AppThemeColor::ToolbarGreen,
                                             VaporView::isDarkThemeEnabled()).name(QColor::HexRgb) &&
                std::abs(plot->property("targetGuideLineWidth").toDouble() - 1.0) < 0.001 &&
                std::isfinite(targetGuideLineY),
            "RD105 configuration trend plot exposes timestamped samples and the bright green target guide");

    auto *overviewPlot = overview->findChild<QWidget *>(QStringLiteral("temperatureTrendPlot"));
    require(overviewPlot != nullptr, "RD105 home overview exposes its temperature trend");
    const double latestSampleTime = plot->property("xAxisTimeMaxSeconds").toDouble();
    for (int refresh = 0; refresh < 500; ++refresh)
        coordinator.updateTemperatureData(data);
    plot->grab();
    require(plot->property("sampleCount").toInt() == 2 &&
                overviewPlot->property("sampleCount").toInt() == 2 &&
                plot->property("xAxisTimeMaxSeconds").toDouble() == latestSampleTime,
            "Cached RD105 refreshes cannot consume history or advance the sample time");

    data.timestamp += std::chrono::seconds(1);
    coordinator.updateTemperatureData(data);
    require(plot->property("sampleCount").toInt() == 3 &&
                overviewPlot->property("sampleCount").toInt() == 3,
            "A new timestamp records a sample even when the temperature is unchanged");
    data.valid = false;
    coordinator.updateTemperatureData(data);
    data.valid = true;
    coordinator.updateTemperatureData(data);
    require(plot->property("sampleCount").toInt() == 3 &&
                overviewPlot->property("sampleCount").toInt() == 3,
            "Invalid data and redisplaying the last sample preserve RD105 history");

    data.timestamp = {};
    coordinator.updateTemperatureData(data);
    coordinator.updateTemperatureData(data);
    require(plot->property("sampleCount").toInt() == 5 &&
                overviewPlot->property("sampleCount").toInt() == 5,
            "Untimestamped RD105 samples retain the local-clock fallback");

    std::cout << "temperature controller panel tests passed\n";
    return 0;
}
