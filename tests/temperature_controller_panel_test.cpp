#include "ground/widgets/TelemetryPanels.h"
#include "shared/theme/AppTheme.h"

#include <QApplication>
#include <QColor>
#include <QWidget>

#include <cmath>
#include <cstdlib>
#include <iostream>

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
    panel.updateData(data);
    QApplication::processEvents();

    data.channels[0].measured_temperature_c = 24.7;
    data.channels[1].measured_temperature_c = 26.9;
    panel.updateData(data);
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

    std::cout << "temperature controller panel tests passed\n";
    return 0;
}
