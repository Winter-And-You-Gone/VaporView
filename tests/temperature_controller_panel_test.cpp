#include "test_settings_sandbox.h"
#include "ground/widgets/TelemetryPanels.h"
#include "ground/widgets/TemperatureControllerWidgets.h"
#include "ground/widgets/DevicePanelCoordinator.h"
#include "shared/theme/AppTheme.h"

#include <QApplication>
#include <QColor>
#include <QLabel>
#include <QPushButton>
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
    overview->resize(640, 148);
    overview->show();
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
    data.channels[0].output_percent = 32.5;
    data.channels[1].output_percent = -42.25;
    data.channels[0].output_enabled = true;
    data.channels[1].output_enabled = false;
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
    const auto overviewPlots = overview->findChildren<QWidget *>(QStringLiteral("temperatureTrendPlot"));
    const auto outputs = overview->findChildren<QPushButton *>(QStringLiteral("temperatureOverviewOutputSwitch"));
    require(overviewPlots.size() == 2 && outputs.size() == 2,
            "RD105 home overview exposes two simultaneous plots and independent output switches");
    require(!overview->findChild<QWidget *>(QStringLiteral("temperatureOverviewChannelButton")) &&
                overviewPlots[0]->geometry().right() < overviewPlots[1]->geometry().left() &&
                overviewPlots[0]->geometry().top() == overviewPlots[1]->geometry().top() &&
                std::abs(overviewPlots[0]->width() - overviewPlots[1]->width()) <= 1,
            "RD105 home overview places channel 1 left of channel 2 at equal width without a selector");
    require(overview->sizeHint().height() == 148 && overview->height() == 148,
            "Both RD105 channels fit the original single-plot overview height");
    for (auto *homePlot : overviewPlots)homePlot->grab();
    require(overviewPlots[0]->property("xAxisLabelsVisible").toBool() &&
                overviewPlots[1]->property("xAxisLabelsVisible").toBool(),
            "Both side-by-side RD105 plots display their time-axis labels");
    require(overviewPlots[0]->property("plotAreaLeft") == overviewPlots[1]->property("plotAreaLeft") &&
                std::abs(overviewPlots[0]->property("plotAreaRight").toDouble() -
                    overviewPlots[1]->property("plotAreaRight").toDouble()) <= 1.0 &&
                std::abs((overviewPlots[0]->property("plotAreaBottom").toDouble() - overviewPlots[0]->property("plotAreaTop").toDouble()) -
                    (overviewPlots[1]->property("plotAreaBottom").toDouble() - overviewPlots[1]->property("plotAreaTop").toDouble())) <= 2.0,
            "The two compact RD105 plotting areas align and retain balanced drawing heights");
    require(overviewPlots[0]->property("xAxisTimeSpanSeconds") == overviewPlots[1]->property("xAxisTimeSpanSeconds") &&
                overviewPlots[0]->height() == 144 && overviewPlots[1]->height() == 144,
            "The two plots retain the shared time range and original drawing height");
    require(outputs[0]->property("temperatureChannel").toInt() == 1 && outputs[0]->isChecked() &&
                outputs[1]->property("temperatureChannel").toInt() == 2 && !outputs[1]->isChecked(),
            "Both home output switches show their own channel's confirmed state");
    auto *percent = overview->findChild<QLabel *>(QStringLiteral("temperatureOverviewOutputPercentPill"));
    require(percent && percent->text().contains(QStringLiteral("1: 32.50%")) &&
                percent->text().contains(QStringLiteral("2: -42.25%")),
            "Both output percentages are visible with their channel numbers");
    QList<QPair<quint8, bool>> outputRequests;
    overview->setOutputEnabledCallback([&](quint8 channel, bool enabled) { outputRequests.append({channel, enabled}); });
    outputs[0]->click();
    outputs[1]->click();
    require(outputRequests == QList<QPair<quint8, bool>>{{1, false}, {2, true}} &&
                outputs[0]->isChecked() && !outputs[1]->isChecked(),
            "Home switches route each request independently and wait for confirmed readback");
    data.channels[0].output_enabled = false;
    data.channels[1].output_enabled = true;
    coordinator.updateTemperatureData(data);
    require(!outputs[0]->isChecked() && outputs[1]->isChecked(),
            "Readback updates each channel's switch independently");
    const double latestSampleTime = plot->property("xAxisTimeMaxSeconds").toDouble();
    for (int refresh = 0; refresh < 500; ++refresh)
        coordinator.updateTemperatureData(data);
    plot->grab();
    require(plot->property("sampleCount").toInt() == 2 &&
                overviewPlots[0]->property("sampleCount").toInt() == 2 &&
                overviewPlots[1]->property("sampleCount").toInt() == 2 &&
                plot->property("xAxisTimeMaxSeconds").toDouble() == latestSampleTime,
            "Cached RD105 refreshes cannot consume history or advance the sample time");

    data.timestamp += std::chrono::seconds(1);
    coordinator.updateTemperatureData(data);
    require(plot->property("sampleCount").toInt() == 3 &&
                overviewPlot->property("sampleCount").toInt() == 3,
            "A new timestamp records a sample even when the temperature is unchanged");
    data.valid = false;
    coordinator.updateTemperatureData(data);
    require(!outputs[0]->isEnabled() && !outputs[1]->isEnabled(),
            "Unavailable controller data disables both output switches");
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
