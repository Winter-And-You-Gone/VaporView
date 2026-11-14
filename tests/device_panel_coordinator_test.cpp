#include "test_settings_sandbox.h"
#include "ground/widgets/DevicePanelCoordinator.h"
#include "ground/widgets/EpsilonPanel.h"
#include "ground/widgets/TelemetryPanels.h"

#include <QApplication>
#include <QDateTime>
#include <QFontMetrics>
#include <QLabel>
#include <QLayout>
#include <QPoint>
#include <QPixmap>

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

int labelTextCount(const QWidget& panel, const QString& objectName, const QString& text)
{
    int count = 0;
    const auto labels = panel.findChildren<QLabel *>(objectName);
    for (const QLabel *label : labels)
    {
        if (label->text().contains(text))
        {
            ++count;
        }
    }
    return count;
}

QLabel *firstLabelContaining(QWidget& panel, const QString& objectName, const QString& text)
{
    const auto labels = panel.findChildren<QLabel *>(objectName);
    for (QLabel *label : labels)
    {
        if (label->text().contains(text))
        {
            return label;
        }
    }
    return nullptr;
}

QLabel *firstLabelExact(QWidget& panel, const QString& objectName, const QString& text)
{
    const auto labels = panel.findChildren<QLabel *>(objectName);
    for (QLabel *label : labels)
    {
        if (label->text() == text)
        {
            return label;
        }
    }
    return nullptr;
}

int labelCenterYInPanel(const QLabel *label, QWidget& panel)
{
    return label->mapTo(&panel, QPoint(0, label->height() / 2)).y();
}

int visibleTextLeftInPanel(const QLabel *label, QWidget& panel)
{
    const QString text = label->text();
    int leadingSpaces = 0;
    while (leadingSpaces < text.size() && text.at(leadingSpaces).isSpace())
    {
        ++leadingSpaces;
    }
    return label->mapTo(&panel,
                        QPoint(label->fontMetrics().horizontalAdvance(text.left(leadingSpaces)),
                               0)).x();
}

int visibleTextRightInPanel(const QLabel *label, QWidget& panel)
{
    return label->mapTo(&panel,
                        QPoint(label->fontMetrics().horizontalAdvance(label->text().trimmed()),
                               0)).x();
}

void requireCompactFieldValueGap(QWidget& panel,
                                 const QLabel *fieldLabel,
                                 const QLabel *valueLabel,
                                 const char *message)
{
    require(fieldLabel != nullptr && valueLabel != nullptr, message);
    const int gap = visibleTextLeftInPanel(valueLabel, panel) -
                    visibleTextRightInPanel(fieldLabel, panel);
    require(gap >= 0 && gap <= 16, message);
}

void requireReadableLabelGap(QWidget& panel,
                             const QLabel *leftLabel,
                             const QLabel *rightLabel,
                             int minimumGap,
                             int maximumGap,
                             const char *message)
{
    require(leftLabel != nullptr && rightLabel != nullptr, message);
    // Short readings leave room inside the numeric field for the longest signed value.
    const int gap = visibleTextLeftInPanel(rightLabel, panel) -
                    leftLabel->mapTo(&panel, QPoint(leftLabel->width(), 0)).x();
    require(gap >= minimumGap && gap <= maximumGap, message);
}

void requireValueCenteredBetween(QWidget& panel,
                                 const QLabel *leftLabel,
                                 const QLabel *valueLabel,
                                 const QLabel *rightLabel,
                                 const char *message)
{
    require(leftLabel != nullptr && valueLabel != nullptr && rightLabel != nullptr,
            message);
    const int leftEdge = leftLabel->mapTo(&panel, QPoint(leftLabel->width(), 0)).x();
    const int rightEdge = rightLabel->mapTo(&panel, QPoint(0, 0)).x();
    const int expectedCenter = (leftEdge + rightEdge) / 2;
    const int valueCenter = valueLabel->mapTo(&panel, QPoint(valueLabel->width() / 2, 0)).x();
    require(std::abs(valueCenter - expectedCenter) <= 2, message);
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QLabel epsilonRate;
    VaporView::Ground::Widgets::EpsilonPanel epsilon(&epsilonRate);
    PtbPanel ptb;
    Hc2aPanel hc2a;
    LidarPanel lidar;

    VaporView::Ground::Widgets::DevicePanelBindings bindings;
    bindings.epsilon = &epsilon;
    bindings.ptb = &ptb;
    bindings.hc2a = &hc2a;
    bindings.lidar = &lidar;
    VaporView::Ground::Widgets::DevicePanelCoordinator coordinator(bindings);

    VaporView::Ground::Widgets::DevicePanelRates rates;
    rates.epsilonHz = 100.0;
    rates.ptbHz = 12.5;
    rates.hc2aHz = 8.0;
    rates.lidarHz = 20.0;
    coordinator.updateRates(rates);

    require(epsilonRate.text().contains(QStringLiteral("100.0 Hz")),
            "EPSILON rate is presented through the coordinator");
    require(hasLabelText(ptb, QStringLiteral("rateLabel"), QStringLiteral("12.5")),
            "PTB rate is presented through the coordinator");
    require(hasLabelText(hc2a, QStringLiteral("rateLabel"), QStringLiteral("8.0")),
            "HC2A rate is presented through the coordinator");
    require(labelTextCount(hc2a, QStringLiteral("rateLabel"), QStringLiteral("8.0")) == 2,
            "HC2A rate is shown on both the temperature and humidity rows");
    require(hasLabelText(lidar, QStringLiteral("rateLabel"), QStringLiteral("20.0")),
            "Lidar rate is presented through the coordinator");

    VaporView::EpsilonData epsilonData;
    const auto baseTimestamp = std::chrono::steady_clock::now();
    VaporView::PtbData ptbData;
    ptbData.valid = true;
    ptbData.pressure_hpa = 1001.25;
    ptbData.timestamp = baseTimestamp;
    VaporView::Hc2aData hc2aData;
    hc2aData.valid = true;
    hc2aData.temperature = 21.5;
    hc2aData.humidity = 48.0;
    hc2aData.timestamp = baseTimestamp;
    VaporView::LidarData lidarData;
    lidarData.valid = true;
    lidarData.distance_m = 120.75;
    lidarData.signal_strength = 321;
    coordinator.updateEnvironmentData(epsilonData, ptbData, hc2aData, lidarData);
    for (QWidget *panel : {static_cast<QWidget *>(&ptb),
                           static_cast<QWidget *>(&hc2a),
                           static_cast<QWidget *>(&lidar)})
    {
        panel->resize(560, std::max(1, panel->sizeHint().height()));
        if (panel->layout())
        {
            panel->layout()->activate();
        }
    }

    require(hasLabelText(ptb, QStringLiteral("highlightedValue"), QStringLiteral("1001.25")),
            "PTB data is presented through the coordinator");
    require(hasLabelText(hc2a, QStringLiteral("highlightedValue"), QStringLiteral("21.5")),
            "HC2A data is presented through the coordinator");
    require(hasLabelText(lidar, QStringLiteral("highlightedValue"), QStringLiteral("120.75")),
            "Lidar data is presented through the coordinator");
    auto *lidarDistanceValue =
        firstLabelContaining(lidar, QStringLiteral("highlightedValue"), QStringLiteral("120.75"));
    auto *lidarStrengthValue =
        firstLabelContaining(lidar, QStringLiteral("highlightedValue"), QStringLiteral("321"));
    auto *lidarRateValue =
        firstLabelContaining(lidar, QStringLiteral("rateLabel"), QStringLiteral("20.0"));
    require(lidarDistanceValue && lidarStrengthValue && lidarRateValue,
            "Lidar row exposes distance, strength, and rate labels");
    require(std::abs(labelCenterYInPanel(lidarDistanceValue, lidar) -
                     labelCenterYInPanel(lidarStrengthValue, lidar)) <= 2 &&
                std::abs(labelCenterYInPanel(lidarDistanceValue, lidar) -
                         labelCenterYInPanel(lidarRateValue, lidar)) <= 2,
            "Lidar distance, strength, and rate are presented on the same row");
    auto *pressureField =
        firstLabelExact(ptb, QStringLiteral("fieldLabel"), QStringLiteral("环境气压:"));
    auto *pressureValue =
        firstLabelContaining(ptb, QStringLiteral("highlightedValue"), QStringLiteral("1001.25"));
    auto *pressureRateValue =
        firstLabelContaining(ptb, QStringLiteral("rateLabel"), QStringLiteral("12.5"));
    requireValueCenteredBetween(ptb,
                                pressureField,
                                pressureValue,
                                pressureRateValue,
                                "pressure value is centered between its field label and rate");
    auto *distanceField =
        firstLabelExact(lidar, QStringLiteral("fieldLabel"), QStringLiteral("距离:"));
    auto *strengthField =
        firstLabelExact(lidar, QStringLiteral("fieldLabel"), QStringLiteral("强度:"));
    requireCompactFieldValueGap(lidar,
                                distanceField,
                                lidarDistanceValue,
                                "distance field and value keep a compact readable gap");
    requireReadableLabelGap(lidar,
                            lidarDistanceValue,
                            strengthField,
                            8,
                            24,
                            "strength field keeps a slightly wider gap after distance");
    requireCompactFieldValueGap(lidar,
                                strengthField,
                                lidarStrengthValue,
                                "strength field and value keep a compact readable gap");
    auto *temperatureTrend = hc2a.findChild<QWidget *>(QStringLiteral("environmentTemperatureTrendPlot"));
    auto *humidityTrend = hc2a.findChild<QWidget *>(QStringLiteral("environmentHumidityTrendPlot"));
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
        hc2aData.timestamp = baseTimestamp + std::chrono::seconds(i);
        hc2aData.temperature = 21.5 + i * 0.1;
        hc2aData.humidity = 48.0 + i * 0.2;
        ptbData.timestamp = hc2aData.timestamp;
        ptbData.pressure_hpa = 1001.25 + i * 0.05;
        coordinator.updateEnvironmentData(epsilonData, ptbData, hc2aData, lidarData);
    }
    const QPixmap compactTemperatureTrendSnapshot = temperatureTrend->grab();
    require(!compactTemperatureTrendSnapshot.isNull(),
            "environment trend plot renders before compact time-label geometry is checked");
    require(std::abs(temperatureTrend->property("xAxisFirstMinuteSecondSeparatorX").toDouble() -
                         temperatureTrend->property("xAxisFirstTickX").toDouble()) <= 0.75 &&
                temperatureTrend->property("xAxisFirstLabelLeftX").toDouble() <
                    temperatureTrend->property("xAxisFirstTickX").toDouble() &&
                temperatureTrend->property("xAxisSecondLabelLeftX").toDouble() -
                        temperatureTrend->property("xAxisFirstLabelRightX").toDouble() >= 2.0,
            "environment trend first time label aligns its minute-second separator and stays clear of the next label");
    const QStringList temperatureXAxisTicks =
        temperatureTrend->property("xAxisTickLabels").toStringList();
    require(temperatureTrend->property("xAxisTickCount").toInt() ==
                    temperatureXAxisTicks.size() &&
                temperatureXAxisTicks.size() > 10 &&
                temperatureXAxisTicks.first().contains(QLatin1Char(':')) &&
                temperatureXAxisTicks.last().contains(QLatin1Char(':')),
            "environment trend x-axis packs additional ticks using the compact minute-second label width");
    const int compactXAxisTickCount = temperatureXAxisTicks.size();
    require(std::abs(temperatureTrend->property("xAxisTimeSpanSeconds").toDouble() - 8.0) < 1e-6,
            "environment trend x-axis fits the available eight seconds of samples");
    const auto requireTimeLabelsMatchRange = [](QWidget *plot) {
        const QStringList labels = plot->property("xAxisTickLabels").toStringList();
        const qint64 start = plot->property("xAxisTimeMinMsecs").toLongLong();
        const qint64 end = plot->property("xAxisTimeMaxMsecs").toLongLong();
        require(labels.size() >= 2 && end > start, "environment time range is nonempty");
        for (int i = 0; i < labels.size(); ++i)
        {
            const qint64 time = start + qRound64(static_cast<double>(end - start) * i / (labels.size() - 1));
            require(labels.at(i) == QDateTime::fromMSecsSinceEpoch(time).toLocalTime().toString(
                        i == 0 ? QStringLiteral("H:mm:ss") : QStringLiteral("mm:ss")),
                    "environment tick labels match the plotted timestamps across the entire axis");
        }
    };
    for (QWidget *plot : {temperatureTrend, humidityTrend, pressureTrend})
        requireTimeLabelsMatchRange(plot);

    temperatureTrend->resize(840, 64);
    humidityTrend->resize(840, 64);
    pressureTrend->resize(840, 64);
    hc2aData.timestamp = baseTimestamp + std::chrono::seconds(9);
    hc2aData.temperature = 22.5;
    hc2aData.humidity = 50.0;
    ptbData.timestamp = hc2aData.timestamp;
    ptbData.pressure_hpa = 1002.0;
    coordinator.updateEnvironmentData(epsilonData, ptbData, hc2aData, lidarData);
    const QStringList wideTemperatureXAxisTicks =
        temperatureTrend->property("xAxisTickLabels").toStringList();
    require(wideTemperatureXAxisTicks.size() > compactXAxisTickCount &&
                temperatureTrend->property("xAxisTickCount").toInt() ==
                    wideTemperatureXAxisTicks.size() &&
                std::abs(temperatureTrend->property("xAxisTimeSpanSeconds").toDouble() -
                         9.0) < 1e-6,
            "wider environment plots add ticks without extending the available sample time range");
    for (QWidget *plot : {temperatureTrend, humidityTrend, pressureTrend})
        requireTimeLabelsMatchRange(plot);

    hc2aData.timestamp = baseTimestamp + std::chrono::seconds(120);
    ptbData.timestamp = hc2aData.timestamp;
    coordinator.updateEnvironmentData(epsilonData, ptbData, hc2aData, lidarData);
    require(std::abs(temperatureTrend->property("xAxisTimeSpanSeconds").toDouble() -
                     static_cast<double>(wideTemperatureXAxisTicks.size() - 1)) < 1e-6,
            "long-running environment plots retain the fixed one-second tick window");
    for (QWidget *plot : {temperatureTrend, humidityTrend, pressureTrend})
        requireTimeLabelsMatchRange(plot);

    const auto requireCenteredRange = [](QWidget *plot, double center, double dataMin, double dataMax) {
        const double lower = plot->property("yAxisMin").toDouble();
        const double upper = plot->property("yAxisMax").toDouble();
        require(std::abs((lower + upper) / 2.0 - center) < 1e-6,
                "environment y axis centers the visible data range");
        require(upper - lower >= 2.0 - 1e-6 && lower < dataMin && upper > dataMax,
                "environment values retain a minimum span and clearance from both edges");
    };
    Hc2aPanel steadyHc2a;
    PtbPanel steadyPressure;
    auto *steadyTemperature = steadyHc2a.findChild<QWidget *>(QStringLiteral("environmentTemperatureTrendPlot"));
    auto *steadyHumidity = steadyHc2a.findChild<QWidget *>(QStringLiteral("environmentHumidityTrendPlot"));
    auto *steadyPressurePlot = steadyPressure.findChild<QWidget *>(QStringLiteral("environmentPressureTrendPlot"));
    require(steadyTemperature && steadyHumidity && steadyPressurePlot,
            "steady environment test exposes all three trend plots");
    for (QWidget *plot : {steadyTemperature, steadyHumidity, steadyPressurePlot})
        plot->setFixedSize(420, 64);
    auto steadyData = hc2aData;
    auto steadyPtbData = ptbData;
    for (int i = 0; i < 2; ++i)
    {
        steadyData.timestamp = baseTimestamp + std::chrono::seconds(i);
        steadyData.temperature = 22.48 + i * 0.02;
        steadyData.humidity = 72.19 + i * 0.02;
        steadyPtbData.timestamp = steadyData.timestamp;
        steadyPtbData.pressure_hpa = 1011.65 + i * 0.02;
        steadyHc2a.updateData(steadyData);
        steadyPressure.updateData(steadyPtbData);
    }
    requireCenteredRange(steadyTemperature, 22.49, 22.48, 22.50);
    requireCenteredRange(steadyHumidity, 72.20, 72.19, 72.21);
    requireCenteredRange(steadyPressurePlot, 1011.66, 1011.65, 1011.67);

    Hc2aPanel windowedHc2a;
    auto *windowedTemperature = windowedHc2a.findChild<QWidget *>(QStringLiteral("environmentTemperatureTrendPlot"));
    auto *windowedHumidity = windowedHc2a.findChild<QWidget *>(QStringLiteral("environmentHumidityTrendPlot"));
    windowedTemperature->setFixedSize(240, 64);
    windowedHumidity->setFixedSize(240, 64);
    for (int seconds : {0, 1, 20})
    {
        steadyData.timestamp = baseTimestamp + std::chrono::seconds(seconds);
        steadyData.temperature = seconds == 0 ? 0.0 : 25.0;
        steadyData.humidity = seconds == 0 ? 100.0 : 72.2;
        windowedHc2a.updateData(steadyData);
    }
    requireCenteredRange(windowedTemperature, 25.0, 25.0, 25.0);
    requireCenteredRange(windowedHumidity, 72.2, 72.2, 72.2);
    require(windowedTemperature->property("sampleCount").toInt() == 3,
            "offscreen extrema remain in history while no longer affecting the y axis");
    for (QWidget *plot : {windowedTemperature, windowedHumidity})
    {
        plot->setFixedSize(1400, 64);
        require(!plot->grab().isNull(), "resized environment plot renders");
    }
    requireCenteredRange(windowedTemperature, 12.5, 0.0, 25.0);
    requireCenteredRange(windowedHumidity, 86.1, 72.2, 100.0);
    for (QWidget *plot : {windowedTemperature, windowedHumidity})
    {
        plot->setFixedSize(240, 64);
        plot->grab();
    }
    requireCenteredRange(windowedTemperature, 25.0, 25.0, 25.0);
    requireCenteredRange(windowedHumidity, 72.2, 72.2, 72.2);

    Hc2aPanel crossingHc2a;
    auto *crossingTemperature = crossingHc2a.findChild<QWidget *>(QStringLiteral("environmentTemperatureTrendPlot"));
    crossingTemperature->setFixedSize(420, 64);
    for (int seconds : {0, 119, 120})
    {
        steadyData.timestamp = baseTimestamp + std::chrono::seconds(seconds);
        steadyData.temperature = seconds == 0 ? 0.0 : seconds == 119 ? 25.0 : 25.1;
        crossingHc2a.updateData(steadyData);
    }
    crossingTemperature->grab();
    const qint64 crossingWindowStart = crossingTemperature->property("xAxisTimeMinMsecs").toLongLong();
    const qint64 crossingWindowEnd = crossingTemperature->property("xAxisTimeMaxMsecs").toLongLong();
    const double crossingValue = 25.0 *
        static_cast<double>(crossingWindowStart - (crossingWindowEnd - 120000)) / 119000.0;
    require(crossingValue > 0.0 && crossingValue < 25.0,
            "test line crosses the left time-window boundary");
    requireCenteredRange(crossingTemperature, (crossingValue + 25.1) / 2.0, crossingValue, 25.1);
    require(crossingTemperature->property("yAxisMin").toDouble() > 0.0,
            "a crossing segment contributes only its visible boundary value, not its old endpoint");

    hc2a.setEnglish(true);
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
    require(labelTextCount(hc2a, QStringLiteral("rateLabel"), QStringLiteral("--")) == 2,
            "clearing rates invalidates both HC2A temperature and humidity rate displays");

    std::cout << "device panel coordinator tests passed\n";
    return 0;
}
