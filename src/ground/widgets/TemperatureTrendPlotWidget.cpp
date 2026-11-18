#include "ground/widgets/TemperatureTrendPlotWidget.h"

#include "shared/theme/AppTheme.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>
#include <QPolygonF>
#include <QSizePolicy>
#include <QStringList>
#include <QTimer>
#include <QShowEvent>
#include <QHideEvent>

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
constexpr qreal kTimeXAxisLabelGap = 4.0;
constexpr qreal kXAxisLabelRightInset = 2.0;
constexpr qreal kXAxisTickLength = 4.0;
constexpr qreal kXAxisLabelGapAfterTick = 2.0;
constexpr qreal kTargetTemperatureGuideLineWidth = 1.0;
constexpr double kTimeAxisSecondsPerInterval = 1.0;

double localClockSeconds()
{
    return static_cast<double>(QDateTime::currentMSecsSinceEpoch()) / 1000.0;
}

qreal timeAxisLabelWidth(const QFontMetrics& metrics, bool abbreviated = false)
{
    if (abbreviated)
        return metrics.horizontalAdvance(QStringLiteral("00:00"));
    return std::max<qreal>(36.0, metrics.horizontalAdvance(QStringLiteral("00:00:00")));
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
    clock_timer_ = new QTimer(this);
    clock_timer_->setInterval(33);
    clock_timer_->setTimerType(Qt::PreciseTimer);
    connect(clock_timer_, &QTimer::timeout, this, [this] { update(); });
}

void TemperatureTrendPlotWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (time_axis_enabled_)
        clock_timer_->start();
}

void TemperatureTrendPlotWidget::hideEvent(QHideEvent *event)
{
    clock_timer_->stop();
    QWidget::hideEvent(event);
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
    setProperty("latestSampleTimeSeconds", sample_times_.isEmpty()
        ? std::numeric_limits<double>::quiet_NaN() : sample_times_.last());
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
    if (enabled && isVisible()) clock_timer_->start();
    else clock_timer_->stop();
    updateSampleProperties();
    update();
}

void TemperatureTrendPlotWidget::setOverviewTimeAxis(bool enabled)
{
    overview_time_axis_ = enabled;
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

void TemperatureTrendPlotWidget::setXAxisVisible(bool visible)
{
    x_axis_visible_ = visible;
    updateSampleProperties();
    update();
}

QRectF TemperatureTrendPlotWidget::plotAreaRect() const
{
    QFont axisFont = font();
    axisFont.setPointSize(std::max(8, axisFont.pointSize() - 2));
    const QFontMetrics axisFm(axisFont);
    const qreal leftAxisWidth = axisFm.horizontalAdvance(QStringLiteral("999")) + 6.0;
    const qreal kBottomAxisHeight = x_axis_visible_ ? 24.0
        : std::max(8.0, std::ceil(axisFm.height() / 2.0) + 1.0);
    const qreal topAxisHeight = compact_mode_ && height() < axisFm.height() * 8
        ? std::max(4.0, std::ceil(axisFm.height() / 2.0)) : 4.0;
    const QRectF basePlotRect = rect().adjusted(leftAxisWidth, topAxisHeight, -4.0, -kBottomAxisHeight);
    QRectF plotRect = basePlotRect;
    if (time_axis_enabled_)
    {
        const qreal labelHalfWidth = timeAxisLabelWidth(axisFm, overview_time_axis_) / 2.0;
        const qreal firstLabelInset = overview_time_axis_
            ? timeAxisLabelWidth(axisFm) - labelHalfWidth : labelHalfWidth;
        const qreal timeAxisLeft = std::max(basePlotRect.left(), firstLabelInset);
        const qreal timeAxisRight = std::min(basePlotRect.right(),
                                             width() - labelHalfWidth - kXAxisLabelRightInset);
        if (timeAxisRight > timeAxisLeft + 1.0)
        {
            plotRect.setLeft(timeAxisLeft);
            plotRect.setRight(timeAxisRight);
        }
    }
    return plotRect;
}

int TemperatureTrendPlotWidget::xAxisTickCount(qreal plotWidth, const QFontMetrics& metrics) const
{
    if (!time_axis_enabled_ || property("compactTimeTicks").toBool())
        return kDefaultXAxisLabelCount;

    const qreal spacing = timeAxisLabelWidth(metrics, overview_time_axis_) + kTimeXAxisLabelGap;
    const int intervals = static_cast<int>(std::floor(std::max<qreal>(0.0, plotWidth) / spacing));
    return std::max(kMinTimeXAxisLabelCount, intervals + 1);
}

int TemperatureTrendPlotWidget::yAxisIntervals() const
{
    if (!compact_mode_) return 6;
    QFont axisFont = font();
    axisFont.setPointSize(std::max(8, axisFont.pointSize() - 2));
    return std::clamp(static_cast<int>(plotAreaRect().height() /
        (QFontMetrics(axisFont).height() + 4)), 2, 6);
}

void TemperatureTrendPlotWidget::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    setProperty("paintCount", property("paintCount").toInt() + 1);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const bool dark = VaporView::isDarkThemeEnabled();
    const bool forceWhiteBackground = property("forceWhiteBackground").toBool() && !dark;
    const QColor background = forceWhiteBackground
        ? QColor(Qt::white)
        : appThemeColor(AppThemeColor::SurfaceRaised, dark);
    const QColor grid = VaporView::appThemeColor(VaporView::AppThemeColor::PlotGrid, dark);
    const QColor border = VaporView::appThemeColor(VaporView::AppThemeColor::PlotBorder, dark);
    const QColor text = VaporView::appThemeColor(VaporView::AppThemeColor::PlotText, dark);
    const QColor muted = VaporView::appThemeColor(VaporView::AppThemeColor::PlotMutedText, dark);
    const QColor line = VaporView::appThemeColor(VaporView::AppThemeColor::PlotSeriesTemperature, dark);
    const QColor targetGuideLine = VaporView::appThemeColor(VaporView::AppThemeColor::ToolbarGreen, dark);

    painter.fillRect(rect(), background);
    QFont axisFont = font();
    axisFont.setPointSize(std::max(8, axisFont.pointSize() - 2));
    const QFontMetrics fm = painter.fontMetrics();
    const QFontMetrics axisFm(axisFont);
    painter.setPen(text);
    const int kYAxisTicks = yAxisIntervals();
    const QRectF plotRect = plotAreaRect();
    setProperty("plotAreaLeft", plotRect.left());
    setProperty("plotAreaTop", plotRect.top());
    setProperty("plotAreaRight", plotRect.right());
    setProperty("plotAreaBottom", plotRect.bottom());
    setProperty("yAxisTickCount", kYAxisTicks + 1);
    setProperty("latestSampleX", std::numeric_limits<double>::quiet_NaN());
    const qreal timeAxisMinimumTickSpacing = timeAxisLabelWidth(axisFm, overview_time_axis_) + kTimeXAxisLabelGap;
    const bool abbreviateTimes = overview_time_axis_ || property("compactTimeTicks").toBool();
    const int xAxisLabelCount = xAxisTickCount(plotRect.width(), axisFm);
    const int xAxisIntervals = std::max(1, xAxisLabelCount - 1);
    setProperty("xAxisTickCount", xAxisLabelCount);
    setProperty("xAxisTimeMinimumTickSpacing",
                time_axis_enabled_ ? timeAxisMinimumTickSpacing : 0.0);
    const double now = localClockSeconds();
    auto xAxisRange = [this, now, xAxisIntervals](int sampleCount) {
        if (!time_axis_enabled_)
        {
            return std::pair<double, double>{0.0, static_cast<double>(std::max(1, sampleCount - 1))};
        }
        return visibleTimeRange(now, xAxisIntervals);
    };
    const auto [minValue, maxValue] = visibleTemperatureAxisRange(now);
    setProperty("yAxisMinC", minValue);
    setProperty("yAxisMaxC", maxValue);
    auto drawGridAndAxes = [&](double minValue,
                               double maxValue,
                               int sampleCount) {
        const auto [xMin, xMax] = xAxisRange(sampleCount);
        const qreal xAxisLabelTop = plotRect.bottom() +
            kXAxisTickLength + kXAxisLabelGapAfterTick;
        setProperty("xAxisPlotBottomY", plotRect.bottom());
        setProperty("xAxisTickLength", kXAxisTickLength);
        setProperty("xAxisLabelTopY", xAxisLabelTop);
        setProperty("xAxisLabelGapFromPlot", xAxisLabelTop - plotRect.bottom());
        if (time_axis_enabled_)
        {
            setProperty("xAxisTimeMinSeconds", xMin);
            setProperty("xAxisTimeMaxSeconds", xMax);
            setProperty("xAxisTimeSpanSeconds", xMax - xMin);
            const QString rightLabel = timeAxisTickLabel(xMax);
            setProperty("xAxisTimeRightLabel", overview_time_axis_ ? rightLabel.mid(3) : rightLabel);
            setProperty("xAxisTimeFirstTickX", plotRect.left());
            setProperty("xAxisTimeLastTickX", plotRect.right());
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
        for (int i = 0; x_axis_visible_ && i < xAxisLabelCount; ++i)
        {
            const qreal x = plotRect.left() + plotRect.width() * i / static_cast<qreal>(xAxisIntervals);
            painter.drawLine(QPointF(x, plotRect.bottom()),
                             QPointF(x, plotRect.bottom() + kXAxisTickLength));
        }

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
        QStringList timeLabels;
        for (int i = 0; x_axis_visible_ && i < xAxisLabelCount; ++i)
        {
            const qreal x = plotRect.left() + plotRect.width() * i / static_cast<qreal>(xAxisIntervals);
            const double xValue = xMin + (xMax - xMin) * i / static_cast<double>(xAxisIntervals);
            const QString label = time_axis_enabled_
                ? (abbreviateTimes && i > 0 ? timeAxisTickLabel(xValue).mid(3) : timeAxisTickLabel(xValue))
                : QString::number(sampleCount <= 1
                                      ? 0
                                      : qRound((sampleCount - 1) * i / static_cast<double>(xAxisIntervals)));
            const qreal textWidth = axisFm.horizontalAdvance(label);
            const qreal labelWidth = time_axis_enabled_ && overview_time_axis_
                ? textWidth : std::max<qreal>(36.0, textWidth + (time_axis_enabled_ ? 0.0 : 8.0));
            // Align the minute/second separator of the first time with its tick.
            const qreal anchorOffset = time_axis_enabled_ && overview_time_axis_ && i == 0
                ? textWidth - axisFm.horizontalAdvance(label.right(5)) / 2.0 : labelWidth / 2.0;
            const qreal labelLeft = time_axis_enabled_
                ? std::clamp(x - anchorOffset,
                             0.0,
                             std::max<qreal>(0.0, width() - labelWidth - kXAxisLabelRightInset))
                : std::clamp(x - labelWidth / 2.0,
                             plotRect.left(),
                             std::max(plotRect.left(), width() - labelWidth - kXAxisLabelRightInset));
            const QRectF labelRect(labelLeft,
                                   xAxisLabelTop,
                                   labelWidth,
                                   axisFm.height());
            painter.drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop, label);
            if (time_axis_enabled_)
            {
                timeLabels.append(label);
                if (i == 0)
                    setProperty("xAxisTimeFirstLabelAnchorX", labelLeft + anchorOffset);
                else if (i == xAxisLabelCount - 1)
                    setProperty("xAxisTimeLastLabelCenterX", labelRect.center().x());
            }
        }
        if (time_axis_enabled_)
            setProperty("xAxisTimeTickLabels", timeLabels);
        painter.setFont(font());
    };
    auto drawTargetTemperatureGuide = [&](double minValue,
                                          double maxValue) {
        const bool visible = std::isfinite(target_temperature_c_) &&
            maxValue > minValue &&
            target_temperature_c_ >= minValue &&
            target_temperature_c_ <= maxValue;
        setProperty("targetGuideLineVisible", visible);
        setProperty("targetGuideLineColor", visible
                        ? targetGuideLine.name(QColor::HexRgb)
                        : QString());
        setProperty("targetGuideLineWidth", kTargetTemperatureGuideLineWidth);
        if (!visible)
        {
            setProperty("targetGuideLineY", std::numeric_limits<double>::quiet_NaN());
            return;
        }

        const double normalized = (target_temperature_c_ - minValue) /
            std::max(1e-6, maxValue - minValue);
        const qreal y = plotRect.bottom() -
            normalized * plotRect.height();
        setProperty("targetGuideLineY", y);

        painter.save();
        painter.setClipRect(plotRect);
        painter.setPen(QPen(targetGuideLine,
                            kTargetTemperatureGuideLineWidth,
                            Qt::SolidLine,
                            Qt::FlatCap));
        painter.drawLine(QPointF(plotRect.left(), y),
                         QPointF(plotRect.right(), y));
        painter.restore();
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
        drawGridAndAxes(minValue, maxValue, 0);
        drawTargetTemperatureGuide(minValue, maxValue);
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

    drawGridAndAxes(minValue, maxValue, finiteSamples.size());
    drawTargetTemperatureGuide(minValue, maxValue);

    QPolygonF polyline;
    polyline.reserve(finiteSamples.size());
    const int count = finiteSamples.size();
    const auto [xMin, xMax] = xAxisRange(count);
    const bool useTimeCoordinates = time_axis_enabled_ &&
        (finiteSampleTimes.size() > 1 || (overview_time_axis_ && sample_times_.size() == samples_.size())) &&
        xMax - xMin > 1e-6;
    const double xSpan = useTimeCoordinates
        ? xMax - xMin
        : static_cast<double>(std::max(1, count - 1));
    for (int i = 0; i < count; ++i)
    {
        const double xValue = useTimeCoordinates
            ? finiteSampleTimes.at(i)
            : static_cast<double>(i);
        const double ratio = (xValue - (useTimeCoordinates ? xMin : 0.0)) / xSpan;
        const double normalized = (finiteSamples.at(i) - minValue) / std::max(1e-6, maxValue - minValue);
        polyline.append(QPointF(plotRect.left() + ratio * plotRect.width(),
                                plotRect.bottom() - normalized * plotRect.height()));
    }

    painter.save();
    painter.setClipRect(plotRect);
    painter.setPen(QPen(line, 1.6));
    painter.drawPolyline(polyline);
    if (overview_time_axis_ && plotRect.contains(polyline.last()))
        painter.setClipping(false);
    painter.setBrush(line);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(polyline.last(), 3.0, 3.0);
    setProperty("latestSampleX", polyline.last().x());
    painter.restore();
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

    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(std::floor(value)))
        .toLocalTime()
        .toString(QStringLiteral("hh:mm:ss"));
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

std::pair<double, double> TemperatureTrendPlotWidget::visibleTimeRange(double now, int intervals) const
{
    double right = now;
    if (overview_time_axis_ && sample_times_.size() == samples_.size() && !sample_times_.isEmpty() &&
        std::isfinite(sample_times_.last()))
        right = sample_times_.last();
    return {right - intervals * kTimeAxisSecondsPerInterval, right};
}

std::pair<double, double> TemperatureTrendPlotWidget::visibleTemperatureAxisRange(double now) const
{
    QVector<double> finiteSamples;
    finiteSamples.reserve(samples_.size());
    QFont axisFont = font();
    axisFont.setPointSize(std::max(8, axisFont.pointSize() - 2));
    const int intervals = xAxisTickCount(plotAreaRect().width(), QFontMetrics(axisFont)) - 1;
    const auto [xMin, xMax] = visibleTimeRange(now, intervals);
    const bool useTimes = time_axis_enabled_ && sample_times_.size() == samples_.size();
    int previous = -1;
    for (int index = 0; index < samples_.size(); ++index)
    {
        const double value = samples_.at(index);
        if (!std::isfinite(value))
            continue;
        if (!useTimes)
        {
            finiteSamples.append(value);
            continue;
        }
        const double time = sample_times_.at(index);
        if (!std::isfinite(time))
            continue;
        if (time >= xMin && time <= xMax)
            finiteSamples.append(value);
        // Include only the visible part of a line crossing either window edge.
        if (previous >= 0)
        {
            const double previousTime = sample_times_.at(previous);
            for (double edge : {xMin, xMax})
            {
                if (previousTime < edge && time > edge)
                {
                    const double ratio = (edge - previousTime) / (time - previousTime);
                    finiteSamples.append(samples_.at(previous) +
                        ratio * (value - samples_.at(previous)));
                }
            }
        }
        previous = index;
    }
    return temperatureAxisRange(finiteSamples, target_temperature_c_);
}

void TemperatureTrendPlotWidget::updateSampleProperties()
{
    const auto [minValue, maxValue] = visibleTemperatureAxisRange(localClockSeconds());
    setProperty("sampleCount", static_cast<int>(std::count_if(samples_.cbegin(), samples_.cend(),
        [](double value) { return std::isfinite(value); })));
    setProperty("yAxisMinC", minValue);
    setProperty("yAxisMaxC", maxValue);
    setProperty("axisLabelsVisible", true);
    setProperty("yAxisTickCount", yAxisIntervals() + 1);
    setProperty("xAxisLabelsVisible", x_axis_visible_);
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
