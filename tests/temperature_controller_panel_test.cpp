#include "test_settings_sandbox.h"
#include "ground/widgets/TelemetryPanels.h"
#include "ground/widgets/TemperatureControllerWidgets.h"
#include "ground/widgets/TemperatureTrendPlotWidget.h"
#include "ground/widgets/DevicePanelCoordinator.h"
#include "shared/theme/AppTheme.h"

#include <QApplication>
#include <QColor>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QToolButton>
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

void testTec103PlotViews()
{
    TemperatureControllerPanel panel;
    panel.resize(900, 420);
    panel.show();
    QApplication::processEvents();
    auto *plot = findConfigTemperaturePlot(panel);
    VaporView::TemperatureControllerData data;
    data.valid = true;
    data.channels[0].measured_temperature_c = 24.7;
    data.timestamp = std::chrono::steady_clock::now();
    auto *viewToggle = panel.findChild<QToolButton*>("tec103PlotViewToggle");
    auto *device = panel.findChild<QAbstractButton*>("tec103DeviceSwitch");
    auto *remotePlot = panel.findChild<QWidget*>("tec103RemoteTemperaturePlot");
    require(viewToggle && device && remotePlot && viewToggle->isHidden() && remotePlot->isHidden(),
            "RD105 hides the TEC103-specific controls");
    panel.setDualTec103(true);
    panel.clearTemperatureHistory();
    data.channels[1].measured_temperature_c = std::numeric_limits<double>::quiet_NaN();
    panel.updateData(data, 10.0);
    data.timestamp += std::chrono::seconds(1);
    data.channels[1].measured_temperature_c = 26.9;
    panel.updateData(data, 11.0);
    QApplication::processEvents();
    require(viewToggle->isVisible() && !viewToggle->icon().isNull() && remotePlot->isHidden() &&
                plot->property("sampleCount").toInt() == 2,
            "TEC103 defaults to the in situ device plot");
    const QRect singleBounds = plot->geometry();
    device->click();
    require(plot->property("sampleCount").toInt() == 1,
            "TEC103 single view follows the selected device");
    viewToggle->click();
    QApplication::processEvents();
    require(remotePlot->isVisible() && plot->geometry().right() < remotePlot->geometry().left() &&
                std::abs(plot->width() - remotePlot->width()) <= 1 &&
                plot->geometry().top() == remotePlot->geometry().top() &&
                plot->height() == remotePlot->height() &&
                plot->geometry().top() > singleBounds.top() && plot->geometry().bottom() == singleBounds.bottom() &&
                plot->property("sampleCount").toInt() == 2 && remotePlot->property("sampleCount").toInt() == 1,
            "TEC103 shows independent in situ (left) and remote (right) histories");
    viewToggle->click();
    QApplication::processEvents();
    require(remotePlot->isHidden() && plot->geometry() == singleBounds &&
                plot->property("sampleCount").toInt() == 1,
            "Returning to single view restores the selected TEC103 and full width");
    panel.setDualTec103(false);
    require(viewToggle->isHidden() && remotePlot->isHidden(), "RD105 restores its single-plot layout");

    std::cout << "TEC103 plot view checks passed" << std::endl;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    testTec103PlotViews();

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
            "RD105 configuration trend plot uses timestamped samples");

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
    require(overview->sizeHint().height() == 164 && overview->height() == 164,
            "Both RD105 channels fit the enlarged output controls and side-by-side plots");
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
    auto *errorStatus = overview->titleStatusWidget()->findChild<QLabel *>(QStringLiteral("temperatureOverviewErrorStatusLabel"));
    require(errorStatus && errorStatus->text() == QStringLiteral("正常"), "Home RD105 header shows confirmed normal status");
    data.error_code = 0x12;
    coordinator.updateTemperatureData(data);
    require(errorStatus->text().contains(QStringLiteral("0X0012")), "Home RD105 header exposes controller faults");
    data.valid = false;
    coordinator.updateTemperatureData(data);
    require(errorStatus->text().contains(QStringLiteral("0X0012")), "A controller fault remains visible when its measurement is invalid");
    data.error_code = 0;
    coordinator.updateTemperatureData(data);
    require(errorStatus->text().contains(QStringLiteral("—")), "Missing RD105 data does not report normal status");
    data.valid = true;
    coordinator.updateTemperatureData(data);
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
    const double latestSampleTime = plot->property("latestSampleTimeSeconds").toDouble();
    for (int refresh = 0; refresh < 500; ++refresh)
        coordinator.updateTemperatureData(data);
    plot->grab();
    require(plot->property("sampleCount").toInt() == 2 &&
                overviewPlots[0]->property("sampleCount").toInt() == 2 &&
                overviewPlots[1]->property("sampleCount").toInt() == 2 &&
                plot->property("latestSampleTimeSeconds").toDouble() == latestSampleTime,
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

    // Use a recorded 1 Hz stream to verify that older samples remain visible.
    overview->clearTemperatureHistory();
    data.timestamp = std::chrono::steady_clock::now();
    constexpr double recordedStart = 1672531200.0;
    constexpr int recordedIntervals = 20;
    for (int i = 0; i <= recordedIntervals; ++i)
    {
        data.timestamp += std::chrono::seconds(1);
        data.channels[0].measured_temperature_c = 24.0 + i / 10.0;
        data.channels[1].measured_temperature_c = 26.0 + i / 10.0;
        overview->updateData(data, recordedStart + i);
    }
    overview->resize(470, 148);
    QApplication::processEvents();
    TemperatureTrendPlotWidget fullClockPlot;
    fullClockPlot.setCompactMode(true);
    fullClockPlot.setTimeAxisEnabled(true);
    for (auto *homePlot : overviewPlots)
    {
        homePlot->grab();
        fullClockPlot.setFont(homePlot->font());
        fullClockPlot.resize(homePlot->size());
        fullClockPlot.grab();
        const auto ticks = homePlot->property("xAxisTimeTickLabels").toStringList();
        const double timeMin = homePlot->property("xAxisTimeMinSeconds").toDouble();
        const double timeMax = homePlot->property("xAxisTimeMaxSeconds").toDouble();
        require(ticks.size() > fullClockPlot.property("xAxisTickCount").toInt() &&
                    ticks.first().count(QLatin1Char(':')) == 2 &&
                    ticks.last().count(QLatin1Char(':')) == 1,
                "At the same compact width, the overview fits more ticks by abbreviating times after the first");
        require(timeMax == recordedStart + recordedIntervals && timeMin >= recordedStart &&
                    timeMax - timeMin == ticks.size() - 1 &&
                    std::abs(homePlot->property("latestSampleX").toDouble() -
                             homePlot->property("xAxisTimeLastTickX").toDouble()) < 0.001,
                "Both overview plots show one-second ticks ending on the latest recorded temperature sample");
    }
    const int compactTickCount = overviewPlot->property("xAxisTickCount").toInt();
    overview->resize(760, 148);
    QApplication::processEvents();
    overviewPlot->grab();
    require(overviewPlot->property("xAxisTickCount").toInt() > compactTickCount,
            "Widening the overview displays a longer span of actual temperature history");

    std::cout << "temperature controller panel tests passed\n";
    return 0;
}
