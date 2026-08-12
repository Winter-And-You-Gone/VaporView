#include "ground/widgets/TemperatureTrendPlotWidget.h"

#include "shared/theme/AppTheme.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>
#include <QPolygonF>
#include <QSizePolicy>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

using VaporView::AppThemeColor;
using VaporView::appThemeColor;

namespace
{
constexpr int kTemperatureControllerPlotWidth = 260;
constexpr int kTemperatureControllerPlotMinHeight = 190;
constexpr int kDefaultXAxisLabelCount = 5;
constexpr int kMinTimeXAxisLabelCount = 2;
constexpr int kMaxTimeXAxisLabelCount = 8;
constexpr qreal kXAxisLabelGap = 8.0;
constexpr double kTimeAxisSecondsPerInterval = 1.0;

int xAxisLabelCountForWidth(qreal plotWidth,
                            const QFontMetrics& axisFontMetrics,
                            bool timeAxisEnabled)
{
    if (!timeAxisEnabled)
    {
        return kDefaultXAxisLabelCount;
    }

    const qreal labelWidth = std::max<qreal>(
        36.0,
        axisFontMetrics.horizontalAdvance(QStringLiteral("00:00:00")) + 8.0);
    const int count = static_cast<int>(std::floor(
        (std::max<qreal>(0.0, plotWidth) + kXAxisLabelGap) /
        (labelWidth + kXAxisLabelGap)));
    return std::clamp(count, kMinTimeXAxisLabelCount, kMaxTimeXAxisLabelCount);
}

QFont numericFontFrom(const QFont& base)
{
    QFont font(base);
    font.setFamilies({QStringLiteral("Consolas"),
                      QStringLiteral("Monaco"),
                      QStringLiteral("Courier New")});
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    return font;
}
} // namespace

TemperatureTrendPlotWidget::TemperatureTrendPlotWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("temperatureTrendPlot"));
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setFont(numericFontFrom(font()));
    applyPlotSizing();
    updateSampleProperties();
}

void TemperatureTrendPlotWidget::setCompactMode(bool compact)
{
    if (compact_mode_ == compact)
    {
        return;
    }
    compact_mode_ = compact;
    applyPlotSizing();
    updateGeometry();
    update();
}

void TemperatureTrendPlotWidget::setEnglish(bool english)
{
    is_english_ = english;
    update();
}

void TemperatureTrendPlotWidget::setChannelIndex(int channelIndex)
{
    channel_index_ = std::max(0, channelIndex);
    update();
}

void TemperatureTrendPlotWidget::setSamples(const QVector<double>& samples)
{
    samples_ = samples;
    updateSampleProperties();
    update();
}

void TemperatureTrendPlotWidget::setSampleTimes(const QVector<double>& sampleTimes)
{
    sample_times_ = sampleTimes;
    updateSampleProperties();
    update();
}

void TemperatureTrendPlotWidget::setTimeAxisEnabled(bool enabled)
{
    if (time_axis_enabled_ == enabled)
    {
        return;
    }
    time_axis_enabled_ = enabled;
    updateSampleProperties();
    update();
}

void TemperatureTrendPlotWidget::setTargetTemperature(double celsius)
{
    target_temperature_c_ = std::isfinite(celsius)
        ? celsius
        : std::numeric_limits<double>::quiet_NaN();
    updateSampleProperties();
    update();
}

void TemperatureTrendPlotWidget::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const bool dark = VaporView::isDarkThemeEnabled();
    const QColor background = property("forceWhiteBackground").toBool()
        ? QColor(Qt::white)
        : appThemeColor(AppThemeColor::SurfaceRaised, dark);
    const QColor grid = VaporView::appThemeColor(VaporView::AppThemeColor::PlotGrid, dark);
    const QColor border = VaporView::appThemeColor(VaporView::AppThemeColor::PlotBorder, dark);
    const QColor text = VaporView::appThemeColor(VaporView::AppThemeColor::PlotText, dark);
    const QColor muted = VaporView::appThemeColor(VaporView::AppThemeColor::PlotMutedText, dark);
    const QColor line = VaporView::appThemeColor(VaporView::AppThemeColor::PlotSeriesTemperature, dark);

    painter.fillRect(rect(), background);
    QFont axisFont = font();
    axisFont.setPointSize(std::max(8, axisFont.pointSize() - 2));
    const QFontMetrics fm = painter.fontMetrics();
    const QFontMetrics axisFm(axisFont);
    painter.setPen(text);
    constexpr int kYAxisTicks = 6;
    const qreal leftAxisWidth = axisFm.horizontalAdvance(QStringLiteral("999")) + 6.0;
    constexpr qreal kBottomAxisHeight = 18.0;
    const QRectF plotRect = rect().adjusted(leftAxisWidth, 4.0, -4.0, -kBottomAxisHeight);
    const int xAxisLabelCount = xAxisLabelCountForWidth(plotRect.width(), axisFm, time_axis_enabled_);
    const int xAxisIntervals = std::max(1, xAxisLabelCount - 1);
    setProperty("xAxisTickCount", xAxisLabelCount);
    auto xAxisRange = [this, xAxisIntervals](const QVector<double>& xValues, int sampleCount) {
        if (!time_axis_enabled_)
        {
            return std::pair<double, double>{0.0, static_cast<double>(std::max(1, sampleCount - 1))};
        }
        const double visibleSpan = static_cast<double>(xAxisIntervals) * kTimeAxisSecondsPerInterval;
        if (xValues.isEmpty())
        {
            return std::pair<double, double>{0.0, visibleSpan};
        }
        const auto maxIt = std::max_element(xValues.cbegin(), xValues.cend());
        if (maxIt == xValues.cend() || !std::isfinite(*maxIt))
        {
            return std::pair<double, double>{0.0, visibleSpan};
        }
        const double xMax = std::max(visibleSpan, std::max(0.0, *maxIt));
        return std::pair<double, double>{xMax - visibleSpan, xMax};
    };
    auto drawGridAndAxes = [&](double minValue,
                               double maxValue,
                               const QVector<double>& xValues,
                               int sampleCount) {
        const auto [xMin, xMax] = xAxisRange(xValues, sampleCount);
        if (time_axis_enabled_)
        {
            setProperty("xAxisTimeMinSeconds", xMin);
            setProperty("xAxisTimeMaxSeconds", xMax);
            setProperty("xAxisTimeSpanSeconds", xMax - xMin);
        }
        painter.setPen(QPen(grid, 1));
        for (int i = 0; i < xAxisLabelCount; ++i)
        {
            const qreal x = plotRect.left() + plotRect.width() * i / static_cast<qreal>(xAxisIntervals);
            painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
        }
        for (int i = 0; i <= kYAxisTicks; ++i)
        {
            const qreal y = plotRect.top() + plotRect.height() * i / static_cast<qreal>(kYAxisTicks);
            painter.drawLine(QPointF(plotRect.left(), y), QPointF(plotRect.right(), y));
        }
        painter.setPen(QPen(border, 1));
        painter.drawRect(plotRect);

        painter.setFont(axisFont);
        painter.setPen(muted);
        for (int i = 0; i <= kYAxisTicks; ++i)
        {
            const double value = maxValue - (maxValue - minValue) * i / static_cast<double>(kYAxisTicks);
            const qreal y = plotRect.top() + plotRect.height() * i / static_cast<qreal>(kYAxisTicks);
            const QString label = axisTickLabel(value);
            const QRectF labelRect(0.0,
                                   y - axisFm.height() / 2.0,
                                   plotRect.left() - 4.0,
                                   axisFm.height());
            painter.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, label);
        }
        for (int i = 0; i < xAxisLabelCount; ++i)
        {
            const qreal x = plotRect.left() + plotRect.width() * i / static_cast<qreal>(xAxisIntervals);
            const double xValue = xMin + (xMax - xMin) * i / static_cast<double>(xAxisIntervals);
            const QString label = time_axis_enabled_
                ? timeAxisTickLabel(xValue)
                : QString::number(sampleCount <= 1
                                      ? 0
                                      : qRound((sampleCount - 1) * i / static_cast<double>(xAxisIntervals)));
            const qreal labelWidth = std::max<qreal>(36.0, axisFm.horizontalAdvance(label) + 8.0);
            const qreal labelLeft = std::clamp(x - labelWidth / 2.0,
                                               plotRect.left(),
                                               std::max(plotRect.left(), width() - labelWidth - 2.0));
            const QRectF labelRect(labelLeft,
                                   plotRect.bottom() + 2.0,
                                   labelWidth,
                                   axisFm.height());
            painter.drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop, label);
        }
        painter.setFont(font());
    };

    QVector<double> finiteSamples;
    QVector<double> finiteSampleTimes;
    finiteSamples.reserve(samples_.size());
    finiteSampleTimes.reserve(samples_.size());
    for (int index = 0; index < samples_.size(); ++index)
    {
        const double value = samples_.at(index);
        if (std::isfinite(value))
        {
            finiteSamples.append(value);
            const double sampleTime = time_axis_enabled_ && index < sample_times_.size()
                ? sample_times_.at(index)
                : static_cast<double>(index);
            finiteSampleTimes.append(std::isfinite(sampleTime)
                                         ? sampleTime
                                         : static_cast<double>(index));
        }
    }

    if (finiteSamples.isEmpty() || plotRect.width() <= 1.0 || plotRect.height() <= 1.0)
    {
        const auto [minValue, maxValue] = temperatureAxisRange(QVector<double>(), target_temperature_c_);
        drawGridAndAxes(minValue, maxValue, QVector<double>(), 0);
        painter.setPen(muted);
        QRectF visiblePlotRect = plotRect.intersected(QRectF(visibleRegion().boundingRect()));
        if (!visiblePlotRect.isValid() || visiblePlotRect.width() <= 1.0 || visiblePlotRect.height() <= 1.0)
        {
            visiblePlotRect = plotRect;
        }
        const QRectF textRect = visiblePlotRect.adjusted(4, 0, -4, 0);
        QString emptyText = compact_mode_
            ? (is_english_ ? QStringLiteral("No data") : QStringLiteral("暂无数据"))
            : (is_english_ ? QStringLiteral("No measured data") : QStringLiteral("暂无实际温度数据"));
        emptyText = fm.elidedText(emptyText, Qt::ElideRight,
                                  std::max(0, static_cast<int>(textRect.width())));
        if (!emptyText.isEmpty())
        {
            painter.drawText(textRect, Qt::AlignCenter, emptyText);
        }
        return;
    }

    const auto [minValue, maxValue] = temperatureAxisRange(finiteSamples, target_temperature_c_);
    drawGridAndAxes(minValue, maxValue, finiteSampleTimes, finiteSamples.size());

    QPolygonF polyline;
    polyline.reserve(finiteSamples.size());
    const int count = finiteSamples.size();
    const auto [xMin, xMax] = xAxisRange(finiteSampleTimes, count);
    const bool useTimeCoordinates = time_axis_enabled_ &&
        finiteSampleTimes.size() > 1 &&
        xMax - xMin > 1e-6;
    const double xSpan = useTimeCoordinates
        ? xMax - xMin
        : static_cast<double>(std::max(1, count - 1));
    for (int i = 0; i < count; ++i)
    {
        const double xValue = useTimeCoordinates
            ? finiteSampleTimes.at(i)
            : static_cast<double>(i);
        const double ratio = count == 1
            ? 0.0
            : (xValue - (useTimeCoordinates ? xMin : 0.0)) / xSpan;
        const double normalized = (finiteSamples.at(i) - minValue) / std::max(1e-6, maxValue - minValue);
        polyline.append(QPointF(plotRect.left() + ratio * plotRect.width(),
                                plotRect.bottom() - normalized * plotRect.height()));
    }

    painter.setPen(QPen(line, 1.6));
    painter.drawPolyline(polyline);
    painter.setBrush(line);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(polyline.last(), 3.0, 3.0);
}

QString TemperatureTrendPlotWidget::axisTickLabel(double value)
{
    return std::abs(value - std::round(value)) < 0.05
        ? QString::number(qRound(value))
        : QString::number(value, 'f', 1);
}

QString TemperatureTrendPlotWidget::timeAxisTickLabel(double value)
{
    if (!std::isfinite(value))
    {
        return QStringLiteral("---");
    }

    const qint64 totalSeconds = std::max<qint64>(0, static_cast<qint64>(std::llround(value)));
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

std::pair<double, double> TemperatureTrendPlotWidget::temperatureAxisRange(
    const QVector<double>& finiteSamples,
    double targetTemperature)
{
    double minValue = std::isfinite(targetTemperature) ? targetTemperature - 1.0 : 20.0;
    double maxValue = std::isfinite(targetTemperature) ? targetTemperature + 1.0 : 25.0;
    if (finiteSamples.isEmpty())
    {
        return {minValue, maxValue};
    }

    auto [minIt, maxIt] = std::minmax_element(finiteSamples.cbegin(), finiteSamples.cend());
    if (*minIt < minValue)
    {
        minValue = std::floor(*minIt) - 1.0;
    }
    if (*maxIt > maxValue)
    {
        maxValue = std::ceil(*maxIt) + 1.0;
    }
    return {minValue, maxValue};
}

void TemperatureTrendPlotWidget::updateSampleProperties()
{
    QVector<double> finiteSamples;
    finiteSamples.reserve(samples_.size());
    for (double value : samples_)
    {
        if (std::isfinite(value))
        {
            finiteSamples.append(value);
        }
    }

    const auto [minValue, maxValue] = temperatureAxisRange(finiteSamples, target_temperature_c_);
    setProperty("sampleCount", finiteSamples.size());
    setProperty("yAxisMinC", minValue);
    setProperty("yAxisMaxC", maxValue);
    setProperty("axisLabelsVisible", true);
    setProperty("yAxisTickCount", 7);
    if (!time_axis_enabled_)
    {
        setProperty("xAxisTickCount", kDefaultXAxisLabelCount);
    }
    setProperty("xAxisTimeMode", time_axis_enabled_);
    setProperty("xAxisTimeSampleCount", time_axis_enabled_ ? sample_times_.size() : 0);
    setProperty("xAxisTimeLabelFormat",
                time_axis_enabled_ ? QStringLiteral("hh:mm:ss") : QString());
}

void TemperatureTrendPlotWidget::applyPlotSizing()
{
    if (compact_mode_)
    {
        setMinimumSize(160, 144);
        setMaximumHeight(QWIDGETSIZE_MAX);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        return;
    }

    setMinimumSize(kTemperatureControllerPlotWidth, kTemperatureControllerPlotMinHeight);
    setMaximumHeight(QWIDGETSIZE_MAX);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
}
