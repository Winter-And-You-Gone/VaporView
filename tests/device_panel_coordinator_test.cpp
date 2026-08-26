#include "ground/widgets/DevicePanelCoordinator.h"
#include "ground/widgets/EpsilonPanel.h"
#include "ground/widgets/TelemetryPanels.h"

#include <QApplication>
#include <QLabel>

#include <chrono>
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

bool hasLabelText(const QWidget& panel, const QString& objectName, const QString& text)
{
    const auto labels = panel.findChildren<QLabel *>(objectName);
    for (const QLabel *label : labels)
    {
        if (label->text().contains(text))
        {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QLabel epsilonRate;
    VaporView::Ground::Widgets::EpsilonPanel epsilon(&epsilonRate);
    PtbPanel ptb;
    HmpPanel hmp;
    LidarPanel lidar;

    VaporView::Ground::Widgets::DevicePanelBindings bindings;
    bindings.epsilon = &epsilon;
    bindings.ptb = &ptb;
    bindings.hmp = &hmp;
    bindings.lidar = &lidar;
    VaporView::Ground::Widgets::DevicePanelCoordinator coordinator(bindings);

    VaporView::Ground::Widgets::DevicePanelRates rates;
    rates.epsilonHz = 100.0;
    rates.ptbHz = 12.5;
    rates.hmpHz = 8.0;
    rates.lidarHz = 20.0;
    coordinator.updateRates(rates);

    require(epsilonRate.text().contains(QStringLiteral("100.0 Hz")),
            "EPSILON rate is presented through the coordinator");
    require(hasLabelText(ptb, QStringLiteral("rateLabel"), QStringLiteral("12.5")),
            "PTB rate is presented through the coordinator");
    require(hasLabelText(hmp, QStringLiteral("rateLabel"), QStringLiteral("8.0")),
            "HMP rate is presented through the coordinator");
    require(hasLabelText(lidar, QStringLiteral("rateLabel"), QStringLiteral("20.0")),
            "Lidar rate is presented through the coordinator");

    VaporView::EpsilonData epsilonData;
    const auto baseTimestamp = std::chrono::steady_clock::now();
    VaporView::PtbData ptbData;
    ptbData.valid = true;
    ptbData.pressure_hpa = 1001.25;
    ptbData.timestamp = baseTimestamp;
    VaporView::HmpData hmpData;
    hmpData.valid = true;
    hmpData.temperature = 21.5;
    hmpData.humidity = 48.0;
    hmpData.timestamp = baseTimestamp;
    VaporView::LidarData lidarData;
    lidarData.valid = true;
    lidarData.distance_m = 12.75;
    lidarData.signal_strength = 321;
    coordinator.updateEnvironmentData(epsilonData, ptbData, hmpData, lidarData);

    require(hasLabelText(ptb, QStringLiteral("highlightedValue"), QStringLiteral("1001.25")),
            "PTB data is presented through the coordinator");
    require(hasLabelText(hmp, QStringLiteral("highlightedValue"), QStringLiteral("21.5")),
            "HMP data is presented through the coordinator");
    require(hasLabelText(lidar, QStringLiteral("highlightedValue"), QStringLiteral("12.75")),
            "Lidar data is presented through the coordinator");
    auto *temperatureTrend = hmp.findChild<QWidget *>(QStringLiteral("environmentTemperatureTrendPlot"));
    auto *humidityTrend = hmp.findChild<QWidget *>(QStringLiteral("environmentHumidityTrendPlot"));
    auto *pressureTrend = ptb.findChild<QWidget *>(QStringLiteral("environmentPressureTrendPlot"));
    require(temperatureTrend && humidityTrend && pressureTrend,
            "environment trend plots are embedded under their data panels");
    require(temperatureTrend->property("sampleCount").toInt() == 1 &&
                humidityTrend->property("sampleCount").toInt() == 1 &&
                pressureTrend->property("sampleCount").toInt() == 1,
            "environment trend plots receive temperature, humidity, and pressure samples");
    require(temperatureTrend->property("yAxisUnitLabel").toString() == QStringLiteral("°C") &&
                humidityTrend->property("yAxisUnitLabel").toString() == QStringLiteral("%RH") &&
                pressureTrend->property("yAxisUnitLabel").toString() == QStringLiteral("hPa"),
            "environment trend plots expose y-axis unit labels");
    require(temperatureTrend->property("yAxisBottomLabel").toString() == QStringLiteral("20.5") &&
                temperatureTrend->property("yAxisMiddleLabel").toString() == QStringLiteral("21.5") &&
                temperatureTrend->property("yAxisTopLabel").toString() == QStringLiteral("22.5"),
            "temperature trend plot exposes numeric y-axis tick labels around the current value");
    require(temperatureTrend->property("xAxisLabelText").toString().isEmpty() &&
                temperatureTrend->property("xAxisLeftLabel").toString().contains(QLatin1Char(':')) &&
                temperatureTrend->property("xAxisRightLabel").toString().contains(QLatin1Char(':')) &&
                humidityTrend->property("xAxisLeftLabel").toString().contains(QLatin1Char(':')) &&
                pressureTrend->property("xAxisLeftLabel").toString().contains(QLatin1Char(':')),
            "environment trend plots expose clock-time x-axis tick labels instead of a title word");

    temperatureTrend->resize(420, 64);
    humidityTrend->resize(420, 64);
    pressureTrend->resize(420, 64);
    for (int i = 1; i <= 8; ++i)
    {
        hmpData.timestamp = baseTimestamp + std::chrono::seconds(i);
        hmpData.temperature = 21.5 + i * 0.1;
        hmpData.humidity = 48.0 + i * 0.2;
        ptbData.timestamp = hmpData.timestamp;
        ptbData.pressure_hpa = 1001.25 + i * 0.05;
        coordinator.updateEnvironmentData(epsilonData, ptbData, hmpData, lidarData);
    }
    const QStringList temperatureXAxisTicks =
        temperatureTrend->property("xAxisTickLabels").toStringList();
    require(temperatureTrend->property("xAxisTickCount").toInt() ==
                    temperatureXAxisTicks.size() &&
                temperatureXAxisTicks.size() > 2 &&
                temperatureXAxisTicks.first().contains(QLatin1Char(':')) &&
                temperatureXAxisTicks.last().contains(QLatin1Char(':')),
            "environment trend x-axis adapts to render more than endpoint time ticks when width permits");
    const int compactXAxisTickCount = temperatureXAxisTicks.size();
    require(std::abs(temperatureTrend->property("xAxisTimeSpanSeconds").toDouble() -
                     static_cast<double>(compactXAxisTickCount - 1)) < 1e-6,
            "environment trend x-axis uses one-second time intervals like the temperature trend plot");

    temperatureTrend->resize(840, 64);
    humidityTrend->resize(840, 64);
    pressureTrend->resize(840, 64);
    hmpData.timestamp = baseTimestamp + std::chrono::seconds(9);
    hmpData.temperature = 22.5;
    hmpData.humidity = 50.0;
    ptbData.timestamp = hmpData.timestamp;
    ptbData.pressure_hpa = 1002.0;
    coordinator.updateEnvironmentData(epsilonData, ptbData, hmpData, lidarData);
    const QStringList wideTemperatureXAxisTicks =
        temperatureTrend->property("xAxisTickLabels").toStringList();
    require(wideTemperatureXAxisTicks.size() > compactXAxisTickCount &&
                temperatureTrend->property("xAxisTickCount").toInt() ==
                    wideTemperatureXAxisTicks.size() &&
                std::abs(temperatureTrend->property("xAxisTimeSpanSeconds").toDouble() -
                         static_cast<double>(wideTemperatureXAxisTicks.size() - 1)) < 1e-6,
            "environment trend x-axis packs additional clock ticks as plot width expands");

    hmp.setEnglish(true);
    ptb.setEnglish(true);
    require(temperatureTrend->property("xAxisLeftLabel").toString().contains(QLatin1Char(':')) &&
                humidityTrend->property("xAxisLeftLabel").toString().contains(QLatin1Char(':')) &&
                pressureTrend->property("xAxisLeftLabel").toString().contains(QLatin1Char(':')),
            "environment trend plot x-axis clock labels survive language switching");

    coordinator.clearRates();
    require(epsilonRate.text().contains(QStringLiteral("-- Hz")),
            "clearing rates invalidates the EPSILON rate display");
    require(hasLabelText(ptb, QStringLiteral("rateLabel"), QStringLiteral("--")),
            "clearing rates invalidates the PTB rate display");

    std::cout << "device panel coordinator tests passed\n";
    return 0;
}
