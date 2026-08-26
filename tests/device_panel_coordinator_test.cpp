#include "ground/widgets/DevicePanelCoordinator.h"
#include "ground/widgets/EpsilonPanel.h"
#include "ground/widgets/TelemetryPanels.h"

#include <QApplication>
#include <QLabel>

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
    VaporView::PtbData ptbData;
    ptbData.valid = true;
    ptbData.pressure_hpa = 1001.25;
    VaporView::HmpData hmpData;
    hmpData.valid = true;
    hmpData.temperature = 21.5;
    hmpData.humidity = 48.0;
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

    coordinator.clearRates();
    require(epsilonRate.text().contains(QStringLiteral("-- Hz")),
            "clearing rates invalidates the EPSILON rate display");
    require(hasLabelText(ptb, QStringLiteral("rateLabel"), QStringLiteral("--")),
            "clearing rates invalidates the PTB rate display");

    std::cout << "device panel coordinator tests passed\n";
    return 0;
}
