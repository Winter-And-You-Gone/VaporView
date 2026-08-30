#include "TelemetryPanels.h"
#include "ground/widgets/VisualTextLabel.h"
#include "shared/theme/AppTheme.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QSizePolicy>
#include <QStyle>
#include <QTimeZone>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr const char *kTextWidthCandidatesProperty = "_vv_text_width_candidates";
constexpr const char *kTextWidthPaddingProperty = "_vv_text_width_padding";
constexpr const char *kNumericWidthCandidatesProperty = "_vv_numeric_width_candidates";
constexpr const char *kNumericWidthPaddingProperty = "_vv_numeric_width_padding";
constexpr quint64 kImuPpsSyncWindowUs = 2ULL * 1000ULL * 1000ULL;
constexpr int kEnvironmentTrendMaxSamples = 160;
constexpr int kEnvironmentTrendPlotHeight = 64;
constexpr qreal kEnvironmentTimeXAxisLabelGap = 4.0;
constexpr qreal kEnvironmentXAxisLabelRightInset = 2.0;
constexpr qreal kEnvironmentXAxisTickLength = 3.0;
constexpr qint64 kEnvironmentTimeAxisMsecsPerInterval = 1000;
constexpr int kEnvironmentPanelSideBySideMinimumWidth = 340;

QFont numericFontFrom(const QFont& base)
{
    QFont font(base);
    font.setFamilies({
        QStringLiteral("Consolas"),
        QStringLiteral("Monaco"),
        QStringLiteral("Courier New")
    });
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    return font;
}

int widestTextWidth(const QFont& font, const QStringList& candidates)
{
    const QFontMetrics metrics(font);
    int width = 0;
    for (const QString& candidate : candidates)
    {
        width = std::max(width, metrics.horizontalAdvance(candidate));
    }
    return width;
}

void applyFixedNumericLabelWidth(QLabel *label, const QStringList& candidates, int padding)
{
    if (!label)
    {
        return;
    }
    label->setFont(numericFontFrom(label->font()));
    QStringList widthCandidates = candidates;
    if (!label->text().isEmpty())
    {
        widthCandidates.append(label->text());
    }
    const int width = widestTextWidth(label->font(), widthCandidates) + padding;
    label->setMinimumWidth(width);
    label->setMaximumWidth(width);
    label->setSizePolicy(QSizePolicy::Fixed, label->sizePolicy().verticalPolicy());
}

void setFixedNumericLabelWidth(QLabel *label, const QStringList& candidates, int padding = 0)
{
    if (!label)
    {
        return;
    }
    label->setProperty(kNumericWidthCandidatesProperty, candidates);
    label->setProperty(kNumericWidthPaddingProperty, padding);
    applyFixedNumericLabelWidth(label, candidates, padding);
}

void setMinimumNumericLabelWidth(QLabel *label, const QStringList& candidates, int padding = 0)
{
    if (!label)
    {
        return;
    }
    label->setFont(numericFontFrom(label->font()));
    QStringList widthCandidates = candidates;
    if (!label->text().isEmpty())
    {
        widthCandidates.append(label->text());
    }
    label->setMinimumWidth(widestTextWidth(label->font(), widthCandidates) + padding);
    label->setMaximumWidth(QWIDGETSIZE_MAX);
    label->setSizePolicy(QSizePolicy::Expanding, label->sizePolicy().verticalPolicy());
}

void applyFixedTextLabelWidth(QLabel *label, const QStringList& candidates, int padding)
{
    if (!label)
    {
        return;
    }
    QStringList widthCandidates = candidates;
    if (!label->text().isEmpty())
    {
        widthCandidates.append(label->text());
    }
    const int width = widestTextWidth(label->font(), widthCandidates) + padding;
    label->setMinimumWidth(width);
    label->setMaximumWidth(width);
    label->setSizePolicy(QSizePolicy::Fixed, label->sizePolicy().verticalPolicy());
}

void setFixedTextLabelWidth(QLabel *label, const QStringList& candidates, int padding = 0)
{
    if (!label)
    {
        return;
    }
    label->setProperty(kTextWidthCandidatesProperty, candidates);
    label->setProperty(kTextWidthPaddingProperty, padding);
    applyFixedTextLabelWidth(label, candidates, padding);
}

void refreshFixedNumericLabelWidth(QLabel *label)
{
    if (!label)
    {
        return;
    }
    const QStringList widthCandidates = label->property(kNumericWidthCandidatesProperty).toStringList();
    if (widthCandidates.isEmpty())
    {
        return;
    }
    const int padding = label->property(kNumericWidthPaddingProperty).toInt();
    applyFixedNumericLabelWidth(label, widthCandidates, std::max(0, padding));
}

void polishNumericLabel(QLabel *label)
{
    if (!label)
    {
        return;
    }
    label->style()->unpolish(label);
    label->style()->polish(label);
    refreshFixedNumericLabelWidth(label);
}

QStringList localizedFieldLabelWidthCandidates(bool english,
                                               const QString& englishText,
                                               const QString& chineseText)
{
    return {english ? englishText : chineseText};
}

void setLocalizedFixedTextLabel(QLabel *label,
                                bool english,
                                const QString& englishText,
                                const QString& chineseText,
                                int padding = 2)
{
    if (!label)
    {
        return;
    }
    label->setText(english ? englishText : chineseText);
    setFixedTextLabelWidth(
        label,
        localizedFieldLabelWidthCandidates(english, englishText, chineseText),
        padding);
}

QString fixedTextField(const QString& text, int width, Qt::Alignment alignment = Qt::AlignRight)
{
    const int targetWidth = std::max(width, static_cast<int>(text.size()));
    return alignment == Qt::AlignLeft
        ? text.leftJustified(targetWidth, QLatin1Char(' '))
        : text.rightJustified(targetWidth, QLatin1Char(' '));
}

QString fixedDecimalWithUnit(double value, int decimals, int numberWidth, const QString& unit)
{
    const QString number = std::isfinite(value)
        ? QString::number(value, 'f', decimals)
        : QStringLiteral("---");
    return unit.isEmpty()
        ? fixedTextField(number, numberWidth)
        : QStringLiteral("%1 %2").arg(fixedTextField(number, numberWidth), unit);
}

} // namespace

class EnvironmentTrendSparklineWidget final : public QWidget
{
public:
    EnvironmentTrendSparklineWidget(VaporView::AppThemeColor seriesColor,
                                    const QString& unit,
                                    QWidget *parent = nullptr)
        : QWidget(parent)
        , series_color_(seriesColor)
        , unit_(unit)
    {
        setMinimumHeight(kEnvironmentTrendPlotHeight);
        setMaximumHeight(kEnvironmentTrendPlotHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAttribute(Qt::WA_OpaquePaintEvent, true);
        setProperty("sampleCount", 0);
        setProperty("yAxisUnitLabel", unit_);
        updateAxisProperties();
    }

    QSize sizeHint() const override
    {
        return QSize(180, kEnvironmentTrendPlotHeight);
    }

    QSize minimumSizeHint() const override
    {
        return QSize(120, kEnvironmentTrendPlotHeight);
    }

    void setEnglish(bool english)
    {
        is_english_ = english;
        updateAxisProperties();
        update();
    }

    void appendSample(double value,
                      std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::time_point{})
    {
        if (!std::isfinite(value))
        {
            return;
        }
        const auto effectiveTimestamp = timestamp == std::chrono::steady_clock::time_point{}
            ? std::chrono::steady_clock::now()
            : timestamp;
        if (!has_first_sample_timestamp_)
        {
            first_sample_timestamp_ = effectiveTimestamp;
            first_sample_wall_msecs_ = QDateTime::currentMSecsSinceEpoch();
            has_first_sample_timestamp_ = true;
        }
        const double elapsedSeconds =
            std::max(0.0,
                     std::chrono::duration<double>(effectiveTimestamp - first_sample_timestamp_).count());
        samples_.append(value);
        sample_wall_msecs_.append(first_sample_wall_msecs_ + qRound64(elapsedSeconds * 1000.0));
        while (samples_.size() > kEnvironmentTrendMaxSamples)
        {
            samples_.removeFirst();
            sample_wall_msecs_.removeFirst();
        }
        setProperty("sampleCount", samples_.size());
        setProperty("latestValue", value);
        updateAxisProperties();
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const bool dark = VaporView::isDarkThemeEnabled();
        const QColor background = VaporView::appThemeColor(VaporView::AppThemeColor::SurfaceRaised, dark);
        const QColor border = VaporView::appThemeColor(VaporView::AppThemeColor::PlotBorder, dark);
        const QColor grid = VaporView::appThemeColor(VaporView::AppThemeColor::PlotGrid, dark);
        const QColor muted = VaporView::appThemeColor(VaporView::AppThemeColor::PlotMutedText, dark);
        const QColor axis = VaporView::appThemeColor(VaporView::AppThemeColor::PlotAxisStrong, dark);
        const QColor line = VaporView::appThemeColor(series_color_, dark);

        painter.fillRect(rect(), background);
        const QRectF panelRect = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
        QPainterPath panelPath;
        panelPath.addRoundedRect(panelRect, 6.0, 6.0);
        painter.fillPath(panelPath, background);
        painter.setPen(QPen(border, 1.0));
        painter.drawPath(panelPath);

        QVector<double> finiteSamples;
        QVector<qint64> finiteSampleTimes;
        collectFiniteSamples(finiteSamples, finiteSampleTimes);
        const AxisState axisState = makeAxisState(finiteSamples, finiteSampleTimes);

        QFont axisFont = font();
        axisFont.setPointSize(std::max(7, axisFont.pointSize() - 3));
        const QFontMetrics axisFm(axisFont);
        const qreal yAxisWidth = yAxisWidthFor(axisState, axisFm);
        const qreal xAxisHeight = axisFm.height() + 9.0;
        const QRectF basePlotRect = panelRect.adjusted(yAxisWidth, 5.0, -7.0, -xAxisHeight);
        QRectF plotRect = basePlotRect;
        const qreal firstLabelHalfWidth = timeAxisFirstLabelWidth(axisFm) / 2.0;
        const qreal subsequentLabelHalfWidth = timeAxisSubsequentLabelWidth(axisFm) / 2.0;
        const qreal timeAxisLeft = std::max(basePlotRect.left(), firstLabelHalfWidth);
        const qreal timeAxisRight = std::min(basePlotRect.right(),
                                             width() - subsequentLabelHalfWidth -
                                                 kEnvironmentXAxisLabelRightInset);
        if (timeAxisRight > timeAxisLeft + 1.0)
        {
            plotRect.setLeft(timeAxisLeft);
            plotRect.setRight(timeAxisRight);
        }
        const XAxisState xAxisState = makeXAxisState(axisState, plotRect.width(), axisFm);
        setXAxisProperties(this, xAxisState);
        painter.setClipPath(panelPath);
        painter.setPen(QPen(grid, 1.0));
        for (int i = 1; i <= 2; ++i)
        {
            const qreal y = plotRect.top() + plotRect.height() * i / 3.0;
            painter.drawLine(QPointF(plotRect.left(), y), QPointF(plotRect.right(), y));
        }
        painter.setPen(QPen(axis, 1.0));
        painter.drawLine(QPointF(plotRect.left(), plotRect.top()),
                         QPointF(plotRect.left(), plotRect.bottom()));
        painter.drawLine(QPointF(plotRect.left(), plotRect.bottom()),
                         QPointF(plotRect.right(), plotRect.bottom()));
        painter.setFont(axisFont);
        painter.setPen(muted);
        auto drawYAxisTick = [&](const QString& label, qreal y) {
            painter.setPen(QPen(axis, 1.0));
            painter.drawLine(QPointF(plotRect.left() - 3.0, y), QPointF(plotRect.left(), y));
            painter.setPen(muted);
            const qreal labelTop = std::clamp(y - axisFm.height() / 2.0,
                                              plotRect.top(),
                                              std::max(plotRect.top(), plotRect.bottom() - axisFm.height()));
            painter.drawText(QRectF(panelRect.left() + 3.0,
                                    labelTop,
                                    yAxisWidth - 8.0,
                                    axisFm.height()),
                             Qt::AlignRight | Qt::AlignVCenter,
                             label);
        };
        drawYAxisTick(axisState.yTopLabel, plotRect.top());
        drawYAxisTick(axisState.yMiddleLabel, plotRect.center().y());
        drawYAxisTick(axisState.yBottomLabel, plotRect.bottom());
        painter.setPen(QPen(axis, 1.0));
        const qreal xAxisLabelTop = plotRect.bottom() + 3.0;
        const int xAxisTickCount = xAxisState.labels.size();
        const int xAxisIntervals = std::max(1, xAxisTickCount - 1);
        for (int i = 0; i < xAxisTickCount; ++i)
        {
            const qreal x = xAxisTickCount <= 1
                ? plotRect.center().x()
                : plotRect.left() + plotRect.width() * i / static_cast<qreal>(xAxisIntervals);
            painter.setPen(QPen(grid, 1.0));
            if (i > 0 && i + 1 < xAxisTickCount)
            {
                painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
            }
            painter.setPen(QPen(axis, 1.0));
            painter.drawLine(QPointF(x, plotRect.bottom()),
                             QPointF(x, plotRect.bottom() + kEnvironmentXAxisTickLength));
            painter.setPen(muted);
            const QString& label = xAxisState.labels.at(i);
            const qreal labelWidth = std::max<qreal>(36.0, axisFm.horizontalAdvance(label) + 6.0);
            const qreal labelLeft = std::clamp(x - labelWidth / 2.0,
                                               0.0,
                                               std::max<qreal>(
                                                   0.0,
                                                   width() - labelWidth -
                                                       kEnvironmentXAxisLabelRightInset));
            painter.drawText(QRectF(labelLeft,
                                    xAxisLabelTop,
                                    labelWidth,
                                    axisFm.height()),
                             Qt::AlignHCenter | Qt::AlignVCenter,
                             label);
        }

        if (finiteSamples.isEmpty())
        {
            painter.setClipping(false);
            painter.setPen(muted);
            painter.drawText(plotRect, Qt::AlignCenter, QStringLiteral("--"));
            return;
        }

        QPolygonF polyline;
        polyline.reserve(finiteSamples.size());
        const int count = finiteSamples.size();
        const qint64 timeSpan = xAxisState.maxWallMsecs - xAxisState.minWallMsecs;
        const bool useWallTimeForX = count > 1 && timeSpan > 0;
        for (int i = 0; i < count; ++i)
        {
            const double xRatio = count <= 1
                ? 0.5
                : useWallTimeForX
                    ? static_cast<double>(finiteSampleTimes.at(i) - xAxisState.minWallMsecs) /
                          static_cast<double>(timeSpan)
                    : static_cast<double>(i) / static_cast<double>(count - 1);
            const double yRatio = (finiteSamples.at(i) - axisState.minValue) /
                std::max(1e-6, axisState.span);
            polyline.append(QPointF(plotRect.left() + xRatio * plotRect.width(),
                                    plotRect.bottom() - yRatio * plotRect.height()));
        }

        painter.save();
        painter.setClipRect(plotRect);
        painter.setPen(QPen(line, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPolyline(polyline);
        painter.setBrush(line);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(polyline.constLast(), 2.4, 2.4);
        painter.restore();
        painter.setClipping(false);
        if (!unit_.isEmpty())
        {
            setProperty("unit", unit_);
        }
    }

private:
    struct AxisState
    {
        double minValue = std::numeric_limits<double>::quiet_NaN();
        double maxValue = std::numeric_limits<double>::quiet_NaN();
        double span = 0.0;
        qint64 minWallMsecs = 0;
        qint64 maxWallMsecs = 0;
        QString yTopLabel = QStringLiteral("--");
        QString yMiddleLabel = QStringLiteral("--");
        QString yBottomLabel = QStringLiteral("--");
        QString xLeftLabel;
        QString xRightLabel;
    };

    struct XAxisState
    {
        QStringList labels;
        qint64 minWallMsecs = 0;
        qint64 maxWallMsecs = 0;
    };

    void collectFiniteSamples(QVector<double>& finiteSamples,
                              QVector<qint64>& finiteSampleTimes) const
    {
        finiteSamples.reserve(samples_.size());
        finiteSampleTimes.reserve(samples_.size());
        for (int index = 0; index < samples_.size(); ++index)
        {
            const double sample = samples_.at(index);
            if (std::isfinite(sample))
            {
                finiteSamples.append(sample);
                finiteSampleTimes.append(index < sample_wall_msecs_.size()
                                             ? sample_wall_msecs_.at(index)
                                             : QDateTime::currentMSecsSinceEpoch());
            }
        }
    }

    static QString trimTrailingZeroes(QString text)
    {
        if (!text.contains(QLatin1Char('.')))
        {
            return text;
        }
        while (text.endsWith(QLatin1Char('0')))
        {
            text.chop(1);
        }
        if (text.endsWith(QLatin1Char('.')))
        {
            text.chop(1);
        }
        return text;
    }

    static QString formatAxisValue(double value, double span)
    {
        if (!std::isfinite(value))
        {
            return QStringLiteral("--");
        }
        const int decimals = span < 1.0 ? 2 : (span < 10.0 ? 1 : 0);
        return trimTrailingZeroes(QString::number(value, 'f', decimals));
    }

    static QString formatClockLabel(qint64 wallMsecs, bool includeHour)
    {
        if (wallMsecs <= 0)
        {
            wallMsecs = QDateTime::currentMSecsSinceEpoch();
        }
        const QString format = includeHour
            ? QStringLiteral("H:mm:ss")
            : QStringLiteral("mm:ss");
        return QDateTime::fromMSecsSinceEpoch(wallMsecs).toLocalTime().toString(format);
    }

    static qreal yAxisWidthFor(const AxisState& state, const QFontMetrics& axisFm)
    {
        qreal width = 32.0;
        for (const QString& label : {state.yTopLabel, state.yMiddleLabel, state.yBottomLabel})
        {
            width = std::max<qreal>(width, axisFm.horizontalAdvance(label) + 10.0);
        }
        return width;
    }

    qreal plotWidthFor(const AxisState& state, const QFontMetrics& axisFm) const
    {
        const qreal widgetWidth = width() > 0 ? width() : sizeHint().width();
        const qreal yAxisWidth = yAxisWidthFor(state, axisFm);
        const qreal baseLeft = 1.0 + yAxisWidth;
        const qreal baseRight = widgetWidth - 8.0;
        const qreal firstLabelHalfWidth = timeAxisFirstLabelWidth(axisFm) / 2.0;
        const qreal subsequentLabelHalfWidth = timeAxisSubsequentLabelWidth(axisFm) / 2.0;
        const qreal timeAxisLeft = std::max(baseLeft, firstLabelHalfWidth);
        const qreal timeAxisRight = std::min(baseRight,
                                             widgetWidth - subsequentLabelHalfWidth -
                                                 kEnvironmentXAxisLabelRightInset);
        if (timeAxisRight > timeAxisLeft + 1.0)
        {
            return std::max<qreal>(0.0, timeAxisRight - timeAxisLeft);
        }
        return std::max<qreal>(0.0, baseRight - baseLeft);
    }

    static qreal timeAxisFirstLabelWidth(const QFontMetrics& axisFm)
    {
        return std::max<qreal>(36.0, axisFm.horizontalAdvance(QStringLiteral("00:00:00")));
    }

    static qreal timeAxisSubsequentLabelWidth(const QFontMetrics& axisFm)
    {
        return std::max<qreal>(36.0, axisFm.horizontalAdvance(QStringLiteral("00:00")));
    }

    static int xAxisTickCountForWidth(qreal plotWidth, const QFontMetrics& axisFm)
    {
        const qreal targetSpacing = timeAxisSubsequentLabelWidth(axisFm) + kEnvironmentTimeXAxisLabelGap;
        const int intervalsByWidth = static_cast<int>(
            std::floor(std::max<qreal>(0.0, plotWidth) / targetSpacing));
        return std::max(2, intervalsByWidth + 1);
    }

    static XAxisState makeXAxisState(const AxisState& state,
                                     qreal plotWidth,
                                     const QFontMetrics& axisFm)
    {
        XAxisState xAxis;
        const int tickCount = xAxisTickCountForWidth(plotWidth, axisFm);
        const qint64 spanMsecs = std::max<qint64>(
            kEnvironmentTimeAxisMsecsPerInterval,
            static_cast<qint64>(tickCount - 1) * kEnvironmentTimeAxisMsecsPerInterval);
        xAxis.maxWallMsecs = state.maxWallMsecs > 0
            ? state.maxWallMsecs
            : QDateTime::currentMSecsSinceEpoch();
        xAxis.minWallMsecs = xAxis.maxWallMsecs - spanMsecs;

        xAxis.labels.reserve(tickCount);
        for (int i = 0; i < tickCount; ++i)
        {
            const qint64 tickMsecs = xAxis.minWallMsecs +
                qRound64(static_cast<double>(spanMsecs) * i /
                         static_cast<double>(std::max(1, tickCount - 1)));
            xAxis.labels.append(formatClockLabel(tickMsecs, i == 0));
        }
        return xAxis;
    }

    static void setXAxisProperties(QWidget *widget, const XAxisState& state)
    {
        widget->setProperty("xAxisLabelText", QString());
        widget->setProperty("xAxisTickCount", state.labels.size());
        widget->setProperty("xAxisTickLabels", state.labels);
        widget->setProperty("xAxisLeftLabel", state.labels.isEmpty() ? QString() : state.labels.first());
        widget->setProperty("xAxisRightLabel", state.labels.isEmpty() ? QString() : state.labels.last());
        widget->setProperty("xAxisSingleLabel", state.labels.size() <= 1);
        widget->setProperty("xAxisTimeMinMsecs", state.minWallMsecs);
        widget->setProperty("xAxisTimeMaxMsecs", state.maxWallMsecs);
        widget->setProperty("xAxisTimeSpanSeconds",
                            static_cast<double>(
                                std::max<qint64>(0, state.maxWallMsecs - state.minWallMsecs)) / 1000.0);
    }

    static AxisState makeAxisState(const QVector<double>& finiteSamples,
                                   const QVector<qint64>& finiteSampleTimes)
    {
        AxisState state;
        if (finiteSamples.isEmpty())
        {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            state.minWallMsecs = now;
            state.maxWallMsecs = now;
            state.xLeftLabel = formatClockLabel(now, true);
            state.xRightLabel = state.xLeftLabel;
            return state;
        }

        auto [minIt, maxIt] = std::minmax_element(finiteSamples.cbegin(), finiteSamples.cend());
        state.minValue = *minIt;
        state.maxValue = *maxIt;
        state.span = state.maxValue - state.minValue;
        if (state.span < 1e-6)
        {
            constexpr double reserve = 1.0;
            state.minValue -= reserve;
            state.maxValue += reserve;
            state.span = state.maxValue - state.minValue;
        }
        state.yTopLabel = formatAxisValue(state.maxValue, state.span);
        state.yMiddleLabel = formatAxisValue((state.minValue + state.maxValue) / 2.0, state.span);
        state.yBottomLabel = formatAxisValue(state.minValue, state.span);

        if (finiteSampleTimes.isEmpty())
        {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            state.minWallMsecs = now;
            state.maxWallMsecs = now;
        }
        else
        {
            auto [minTimeIt, maxTimeIt] = std::minmax_element(finiteSampleTimes.cbegin(),
                                                              finiteSampleTimes.cend());
            state.minWallMsecs = *minTimeIt;
            state.maxWallMsecs = *maxTimeIt;
        }
        state.xLeftLabel = formatClockLabel(state.minWallMsecs, true);
        state.xRightLabel = formatClockLabel(state.maxWallMsecs, false);
        return state;
    }

    void updateAxisProperties()
    {
        QVector<double> finiteSamples;
        QVector<qint64> finiteSampleTimes;
        collectFiniteSamples(finiteSamples, finiteSampleTimes);
        const AxisState state = makeAxisState(finiteSamples, finiteSampleTimes);
        setProperty("yAxisUnitLabel", unit_);
        setProperty("yAxisMin", state.minValue);
        setProperty("yAxisMax", state.maxValue);
        setProperty("yAxisTopLabel", state.yTopLabel);
        setProperty("yAxisMiddleLabel", state.yMiddleLabel);
        setProperty("yAxisBottomLabel", state.yBottomLabel);
        QFont axisFont = font();
        axisFont.setPointSize(std::max(7, axisFont.pointSize() - 3));
        const QFontMetrics axisFm(axisFont);
        setXAxisProperties(this, makeXAxisState(state, plotWidthFor(state, axisFm), axisFm));
    }

    VaporView::AppThemeColor series_color_;
    QString unit_;
    QVector<double> samples_;
    QVector<qint64> sample_wall_msecs_;
    std::chrono::steady_clock::time_point first_sample_timestamp_{};
    qint64 first_sample_wall_msecs_ = 0;
    bool has_first_sample_timestamp_ = false;
    bool is_english_ = false;
};

namespace
{

bool shouldAppendEnvironmentSample(const std::chrono::steady_clock::time_point& timestamp,
                                   std::chrono::steady_clock::time_point& lastTimestamp,
                                   bool& hasTimestamp)
{
    const bool hasIncomingTimestamp = timestamp != std::chrono::steady_clock::time_point{};
    if (!hasIncomingTimestamp)
    {
        return true;
    }
    if (hasTimestamp && timestamp == lastTimestamp)
    {
        return false;
    }
    lastTimestamp = timestamp;
    hasTimestamp = true;
    return true;
}
QString imuFrameTypeName(VaporView::ImuFrameType type)
{
    switch (type)
    {
    case VaporView::ImuFrameType::HI81:
        return QStringLiteral("HI81");
    case VaporView::ImuFrameType::HI83:
        return QStringLiteral("HI83");
    case VaporView::ImuFrameType::HI91:
        return QStringLiteral("HI91");
    case VaporView::ImuFrameType::HI92:
        return QStringLiteral("HI92");
    case VaporView::ImuFrameType::Unknown:
    default:
        return QStringLiteral("Unknown");
    }
}
} // namespace

GnssPanel::GnssPanel(QWidget *parent)
    : QWidget(parent)
    , rate_label_(nullptr)
    , status_label_(nullptr)
    , time_label_(nullptr)
    , lat_label_(nullptr)
    , lon_label_(nullptr)
    , alt_label_(nullptr)
    , vel_n_label_(nullptr)
    , vel_e_label_(nullptr)
    , vel_ground_label_(nullptr)
    , heading_label_(nullptr)
    , pitch_label_(nullptr)
    , heading_len_label_(nullptr)
    , heading_type_label_(nullptr)
    , heading_sats_label_(nullptr)
    , sats_label_(nullptr)
    , gdop_label_(nullptr)
    , pdop_label_(nullptr)
    , hdop_label_(nullptr)
    , htdop_label_(nullptr)
    , tdop_label_(nullptr)
    , diff_age_label_(nullptr)
    , undulation_label_(nullptr)
    , sigma_lat_label_(nullptr)
    , sigma_lon_label_(nullptr)
    , sigma_alt_label_(nullptr)
    , cutoff_label_(nullptr)
    , status_lbl_(nullptr)
    , time_lbl_(nullptr)
    , lat_lbl_(nullptr)
    , lon_lbl_(nullptr)
    , alt_lbl_(nullptr)
    , vel_n_lbl_(nullptr)
    , vel_e_lbl_(nullptr)
    , vel_ground_lbl_(nullptr)
    , heading_lbl_(nullptr)
    , pitch_lbl_(nullptr)
    , heading_type_lbl_(nullptr)
    , heading_len_lbl_(nullptr)
    , heading_sats_lbl_(nullptr)
    , sats_lbl_(nullptr)
    , diff_lbl_(nullptr)
    , gdop_lbl_(nullptr)
    , pdop_lbl_(nullptr)
    , hdop_lbl_(nullptr)
    , htdop_lbl_(nullptr)
    , tdop_lbl_(nullptr)
    , cutoff_lbl_(nullptr)
    , undulation_lbl_(nullptr)
    , sigma_lat_lbl_(nullptr)
    , sigma_lon_lbl_(nullptr)
    , sigma_alt_lbl_(nullptr)
    , is_english_(false)
{
    setupUi();
}

void GnssPanel::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(4);
    mainLayout->setContentsMargins(6, 2, 6, 6);

    rate_label_ = new VaporView::VisualTextLabel(this);
    rate_label_->setObjectName("rateLabel");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setFixedNumericLabelWidth(rate_label_, {QStringLiteral("-999.9 Hz"), QStringLiteral("999.9 Hz"), QStringLiteral("-- Hz")}, 4);
    mainLayout->addWidget(rate_label_, 0, Qt::AlignRight);

    auto *colsLayout = new QHBoxLayout();
    colsLayout->setSpacing(12);

    auto *leftLayout = new QGridLayout();
    leftLayout->setVerticalSpacing(4);
    leftLayout->setHorizontalSpacing(1);

    auto *midLayout = new QGridLayout();
    midLayout->setVerticalSpacing(4);
    midLayout->setHorizontalSpacing(1);

    auto *rightLayout = new QGridLayout();
    rightLayout->setVerticalSpacing(4);
    rightLayout->setHorizontalSpacing(1);

    auto createRow = [](QGridLayout* grid, int row, QLabel*& lbl, QLabel*& valueLabel, QWidget* parent) {
        lbl = new QLabel(parent);
        lbl->setObjectName("fieldLabel");
        lbl->setMinimumHeight(22);
        valueLabel = new QLabel("---", parent);
        valueLabel->setObjectName("valueLabel");
        valueLabel->setMinimumHeight(22);
        grid->addWidget(lbl, row, 0);
        grid->addWidget(valueLabel, row, 1);
    };

    createRow(leftLayout, 0, status_lbl_, status_label_, this);
    createRow(leftLayout, 1, time_lbl_, time_label_, this);
    createRow(leftLayout, 2, lat_lbl_, lat_label_, this);
    createRow(leftLayout, 3, lon_lbl_, lon_label_, this);
    createRow(leftLayout, 4, alt_lbl_, alt_label_, this);
    createRow(leftLayout, 5, sigma_lat_lbl_, sigma_lat_label_, this);
    createRow(leftLayout, 6, sigma_lon_lbl_, sigma_lon_label_, this);
    createRow(leftLayout, 7, sigma_alt_lbl_, sigma_alt_label_, this);
    createRow(leftLayout, 8, undulation_lbl_, undulation_label_, this);

    createRow(midLayout, 0, vel_n_lbl_, vel_n_label_, this);
    createRow(midLayout, 1, vel_e_lbl_, vel_e_label_, this);
    createRow(midLayout, 2, vel_ground_lbl_, vel_ground_label_, this);
    createRow(midLayout, 3, heading_lbl_, heading_label_, this);
    createRow(midLayout, 4, pitch_lbl_, pitch_label_, this);
    createRow(midLayout, 5, heading_type_lbl_, heading_type_label_, this);
    createRow(midLayout, 6, heading_len_lbl_, heading_len_label_, this);
    createRow(midLayout, 7, heading_sats_lbl_, heading_sats_label_, this);
    createRow(midLayout, 8, sats_lbl_, sats_label_, this);
    createRow(midLayout, 9, diff_lbl_, diff_age_label_, this);

    createRow(rightLayout, 0, gdop_lbl_, gdop_label_, this);
    createRow(rightLayout, 1, pdop_lbl_, pdop_label_, this);
    createRow(rightLayout, 2, hdop_lbl_, hdop_label_, this);
    createRow(rightLayout, 3, htdop_lbl_, htdop_label_, this);
    createRow(rightLayout, 4, tdop_lbl_, tdop_label_, this);
    createRow(rightLayout, 5, cutoff_lbl_, cutoff_label_, this);

    if (time_label_)
    {
        time_label_->setWordWrap(true);
        time_label_->setMinimumHeight(40);
        time_label_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    }

    leftLayout->setColumnStretch(1, 1);
    midLayout->setColumnStretch(1, 1);
    rightLayout->setColumnStretch(1, 1);

    colsLayout->addLayout(leftLayout, 1);
    colsLayout->addLayout(midLayout, 1);
    colsLayout->addLayout(rightLayout, 1);

    mainLayout->addLayout(colsLayout);
    mainLayout->addStretch();
    setEnglish(false);
}

void GnssPanel::updateRate(double hz)
{
    if (rate_label_)
    {
        rate_label_->setText(std::isfinite(hz)
            ? fixedDecimalWithUnit(hz, 1, 6, QStringLiteral("Hz"))
            : QStringLiteral("%1 Hz").arg(fixedTextField(QStringLiteral("--"), 6)));
    }
}
void GnssPanel::setEnglish(bool english)
{
    is_english_ = english;
    if (english)
    {
        status_lbl_->setText("Status:");
        time_lbl_->setText("Time:");
        lat_lbl_->setText("Lat:");
        lon_lbl_->setText("Lon:");
        alt_lbl_->setText("Alt:");
        sigma_lat_lbl_->setText("σ Lat:");
        sigma_lon_lbl_->setText("σ Lon:");
        sigma_alt_lbl_->setText("σ Alt:");
        undulation_lbl_->setText("Undul:");
        vel_n_lbl_->setText("Vel N:");
        vel_e_lbl_->setText("Vel E:");
        vel_ground_lbl_->setText("Vel Gnd:");
        heading_lbl_->setText("Heading:");
        pitch_lbl_->setText("Pitch:");
        heading_type_lbl_->setText("Hd Type:");
        heading_len_lbl_->setText("Base L:");
        heading_sats_lbl_->setText("Hd Sats:");
        sats_lbl_->setText("Sats:");
        diff_lbl_->setText("Diff:");
        gdop_lbl_->setText("GDOP:");
        pdop_lbl_->setText("PDOP:");
        hdop_lbl_->setText("HDOP:");
        htdop_lbl_->setText("HTDOP:");
        tdop_lbl_->setText("TDOP:");
        cutoff_lbl_->setText("Cutoff:");
    }
    else
    {
        status_lbl_->setText("状态:");
        time_lbl_->setText("时间:");
        lat_lbl_->setText("纬度:");
        lon_lbl_->setText("经度:");
        alt_lbl_->setText("高度:");
        sigma_lat_lbl_->setText("纬度σ:");
        sigma_lon_lbl_->setText("经度σ:");
        sigma_alt_lbl_->setText("高度σ:");
        undulation_lbl_->setText("异常高:");
        vel_n_lbl_->setText("北速:");
        vel_e_lbl_->setText("东速:");
        vel_ground_lbl_->setText("地速:");
        heading_lbl_->setText("航向:");
        pitch_lbl_->setText("俯仰:");
        heading_type_lbl_->setText("定向类型:");
        heading_len_lbl_->setText("基线长:");
        heading_sats_lbl_->setText("定向卫星:");
        sats_lbl_->setText("卫星:");
        diff_lbl_->setText("差分龄:");
        gdop_lbl_->setText("GDOP:");
        pdop_lbl_->setText("PDOP:");
        hdop_lbl_->setText("HDOP:");
        htdop_lbl_->setText("HTDOP:");
        tdop_lbl_->setText("TDOP:");
        cutoff_lbl_->setText("截止角:");
    }
}

void GnssPanel::updateData(const VaporView::GnssData& gnss_data, quint64 timestamp_us)
{
    if (gnss_data.valid)
    {
        status_label_->setText(QString::fromStdString(gnss_data.position_status));
        status_label_->setProperty("data-valid", true);
        status_label_->style()->unpolish(status_label_);
        status_label_->style()->polish(status_label_);

        const QString formattedText = timestamp_us > 0
            ? QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(timestamp_us / 1000ULL), QTimeZone::UTC)
                  .toString("yyyy-MM-dd HH:mm:ss.zzz 'UTC'")
            : QStringLiteral("---");
        const QString rawText = timestamp_us > 0 ? QString::number(timestamp_us) + "us" : QStringLiteral("---");
        time_label_->setText(QString("%1\n%2").arg(formattedText, rawText));

        lat_label_->setText(fixedDecimalWithUnit(gnss_data.latitude, 8, 12, QStringLiteral("°")));
        lon_label_->setText(fixedDecimalWithUnit(gnss_data.longitude, 8, 13, QStringLiteral("°")));
        alt_label_->setText(fixedDecimalWithUnit(gnss_data.altitude, 3, 10, QStringLiteral("m")));
        sigma_lat_label_->setText(fixedDecimalWithUnit(gnss_data.sigma_lat, 3, 8, QStringLiteral("m")));
        sigma_lon_label_->setText(fixedDecimalWithUnit(gnss_data.sigma_lon, 3, 8, QStringLiteral("m")));
        sigma_alt_label_->setText(fixedDecimalWithUnit(gnss_data.sigma_alt, 3, 8, QStringLiteral("m")));
        undulation_label_->setText(fixedDecimalWithUnit(gnss_data.undulation, 3, 9, QStringLiteral("m")));
        vel_n_label_->setText(fixedDecimalWithUnit(gnss_data.vel_north, 3, 9, QStringLiteral("m/s")));
        vel_e_label_->setText(fixedDecimalWithUnit(gnss_data.vel_east, 3, 9, QStringLiteral("m/s")));
        vel_ground_label_->setText(fixedDecimalWithUnit(gnss_data.vel_ground, 3, 9, QStringLiteral("m/s")));
        heading_label_->setText(fixedDecimalWithUnit(gnss_data.heading, 2, 7, QStringLiteral("°")));
        pitch_label_->setText(fixedDecimalWithUnit(gnss_data.heading_pitch, 2, 7, QStringLiteral("°")));
        heading_type_label_->setText(QString::fromStdString(gnss_data.heading_type));
        heading_len_label_->setText(fixedDecimalWithUnit(gnss_data.heading_length, 3, 9, QStringLiteral("m")));
        heading_sats_label_->setText(QStringLiteral("%1/%2")
            .arg(fixedTextField(QString::number(gnss_data.heading_solnsvs), 3),
                 fixedTextField(QString::number(gnss_data.heading_trackedsvs), 3)));
        sats_label_->setText(QStringLiteral("%1/%2")
            .arg(fixedTextField(QString::number(gnss_data.num_satellites_used), 3),
                 fixedTextField(QString::number(gnss_data.num_satellites_tracked), 3)));
        diff_age_label_->setText(fixedDecimalWithUnit(gnss_data.diff_age, 1, 6, QStringLiteral("s")));
        gdop_label_->setText(fixedDecimalWithUnit(gnss_data.gdop, 2, 6, QString()));
        pdop_label_->setText(fixedDecimalWithUnit(gnss_data.pdop, 2, 6, QString()));
        hdop_label_->setText(fixedDecimalWithUnit(gnss_data.hdop, 2, 6, QString()));
        htdop_label_->setText(fixedDecimalWithUnit(gnss_data.htdop, 2, 6, QString()));
        tdop_label_->setText(fixedDecimalWithUnit(gnss_data.tdop, 2, 6, QString()));
        cutoff_label_->setText(fixedDecimalWithUnit(gnss_data.elevation_cutoff, 1, 6, QStringLiteral("°")));
    }
    else
    {
        status_label_->setText(QString::fromStdString(gnss_data.error_message));
        status_label_->setProperty("data-valid", false);
        status_label_->style()->unpolish(status_label_);
        status_label_->style()->polish(status_label_);
        time_label_->setText(QStringLiteral("---\n---"));
    }
}

ImuPanel::ImuPanel(QWidget *parent)
    : QWidget(parent)
    , rate_label_(nullptr)
    , acc_x_label_(nullptr)
    , acc_y_label_(nullptr)
    , acc_z_label_(nullptr)
    , gyr_x_label_(nullptr)
    , gyr_y_label_(nullptr)
    , gyr_z_label_(nullptr)
    , roll_label_(nullptr)
    , pitch_label_(nullptr)
    , yaw_label_(nullptr)
    , quat_w_label_(nullptr)
    , quat_x_label_(nullptr)
    , quat_y_label_(nullptr)
    , quat_z_label_(nullptr)
    , temp_label_(nullptr)
    , press_label_(nullptr)
    , source_label_(nullptr)
    , time_label_(nullptr)
    , pps_label_(nullptr)
    , source_lbl_(nullptr)
    , time_lbl_(nullptr)
    , pps_lbl_(nullptr)
    , accel_sep_(nullptr)
    , gyro_sep_(nullptr)
    , attitude_sep_(nullptr)
    , quat_sep_(nullptr)
    , env_sep_(nullptr)
    , temp_lbl_(nullptr)
    , press_lbl_(nullptr)
    , acc_x_lbl_(nullptr)
    , acc_y_lbl_(nullptr)
    , acc_z_lbl_(nullptr)
    , gyr_x_lbl_(nullptr)
    , gyr_y_lbl_(nullptr)
    , gyr_z_lbl_(nullptr)
    , roll_lbl_(nullptr)
    , pitch_lbl_(nullptr)
    , yaw_lbl_(nullptr)
    , quat_w_lbl_(nullptr)
    , quat_x_lbl_(nullptr)
    , quat_y_lbl_(nullptr)
    , quat_z_lbl_(nullptr)
    , is_english_(false)
{
    setupUi();
}

void ImuPanel::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(4);
    mainLayout->setContentsMargins(6, 2, 6, 6);

    rate_label_ = new VaporView::VisualTextLabel(this);
    rate_label_->setObjectName("rateLabel");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rate_label_->setFixedHeight(24);
    setFixedNumericLabelWidth(rate_label_, {QStringLiteral("-999.9 Hz"), QStringLiteral("999.9 Hz"), QStringLiteral("-- Hz")}, 4);
    mainLayout->addWidget(rate_label_, 0, Qt::AlignRight);

    auto *colsLayout = new QHBoxLayout();
    colsLayout->setSpacing(12);

    auto *leftLayout = new QGridLayout();
    leftLayout->setVerticalSpacing(4);
    leftLayout->setHorizontalSpacing(1);

    auto *rightLayout = new QGridLayout();
    rightLayout->setVerticalSpacing(4);
    rightLayout->setHorizontalSpacing(1);

    auto createRow = [](QGridLayout* grid, int row, QLabel*& lbl, QLabel*& valueLabel, QWidget* parent) {
        lbl = new QLabel(parent);
        lbl->setObjectName("fieldLabel");
        lbl->setMinimumHeight(22);
        valueLabel = new QLabel("---", parent);
        valueLabel->setObjectName("valueLabel");
        valueLabel->setMinimumHeight(22);
        grid->addWidget(lbl, row, 0);
        grid->addWidget(valueLabel, row, 1);
    };

    auto createSeparator = [](QGridLayout* grid, int row, QLabel*& sep, QWidget* parent) {
        sep = new QLabel(parent);
        sep->setObjectName("separatorLabel");
        sep->setMinimumHeight(26);
        grid->addWidget(sep, row, 0, 1, 2);
    };

    createRow(leftLayout, 0, source_lbl_, source_label_, this);
    createRow(leftLayout, 1, time_lbl_, time_label_, this);
    createRow(leftLayout, 2, pps_lbl_, pps_label_, this);
    createSeparator(leftLayout, 3, accel_sep_, this);
    createRow(leftLayout, 4, acc_x_lbl_, acc_x_label_, this);
    createRow(leftLayout, 5, acc_y_lbl_, acc_y_label_, this);
    createRow(leftLayout, 6, acc_z_lbl_, acc_z_label_, this);
    createSeparator(leftLayout, 7, gyro_sep_, this);
    createRow(leftLayout, 8, gyr_x_lbl_, gyr_x_label_, this);
    createRow(leftLayout, 9, gyr_y_lbl_, gyr_y_label_, this);
    createRow(leftLayout, 10, gyr_z_lbl_, gyr_z_label_, this);

    createSeparator(rightLayout, 0, attitude_sep_, this);
    createRow(rightLayout, 1, roll_lbl_, roll_label_, this);
    createRow(rightLayout, 2, pitch_lbl_, pitch_label_, this);
    createRow(rightLayout, 3, yaw_lbl_, yaw_label_, this);
    createSeparator(rightLayout, 4, quat_sep_, this);
    createRow(rightLayout, 5, quat_w_lbl_, quat_w_label_, this);
    createRow(rightLayout, 6, quat_x_lbl_, quat_x_label_, this);
    createRow(rightLayout, 7, quat_y_lbl_, quat_y_label_, this);
    createRow(rightLayout, 8, quat_z_lbl_, quat_z_label_, this);

    if (time_label_)
    {
        time_label_->setWordWrap(true);
        time_label_->setMinimumHeight(40);
        time_label_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    }

    leftLayout->setColumnStretch(1, 1);
    rightLayout->setColumnStretch(1, 1);

    colsLayout->addLayout(leftLayout, 1);
    colsLayout->addLayout(rightLayout, 1);

    mainLayout->addLayout(colsLayout);
    mainLayout->addStretch();
    setEnglish(false);
}

void ImuPanel::updateRate(double hz)
{
    if (rate_label_)
    {
        rate_label_->setText(std::isfinite(hz)
            ? fixedDecimalWithUnit(hz, 1, 6, QStringLiteral("Hz"))
            : QStringLiteral("%1 Hz").arg(fixedTextField(QStringLiteral("--"), 6)));
    }
}

void ImuPanel::setEnglish(bool english)
{
    is_english_ = english;
    if (english)
    {
        source_lbl_->setText("Source:");
        time_lbl_->setText("Time:");
        pps_lbl_->setText("PPS:");
        accel_sep_->setText("— Accel —");
        gyro_sep_->setText("— Gyro —");
        attitude_sep_->setText("— Attitude —");
        quat_sep_->setText("— Quaternion —");
        acc_x_lbl_->setText("X:");
        acc_y_lbl_->setText("Y:");
        acc_z_lbl_->setText("Z:");
        gyr_x_lbl_->setText("X:");
        gyr_y_lbl_->setText("Y:");
        gyr_z_lbl_->setText("Z:");
        roll_lbl_->setText("Roll:");
        pitch_lbl_->setText("Pitch:");
        yaw_lbl_->setText("Yaw:");
        quat_w_lbl_->setText("W:");
        quat_x_lbl_->setText("X:");
        quat_y_lbl_->setText("Y:");
        quat_z_lbl_->setText("Z:");
    }
    else
    {
        source_lbl_->setText("数据源:");
        time_lbl_->setText("时间:");
        pps_lbl_->setText("PPS有效:");
        accel_sep_->setText("— 加速度 —");
        gyro_sep_->setText("— 陀螺仪 —");
        attitude_sep_->setText("— 姿态 —");
        quat_sep_->setText("— 四元数 —");
        acc_x_lbl_->setText("X:");
        acc_y_lbl_->setText("Y:");
        acc_z_lbl_->setText("Z:");
        gyr_x_lbl_->setText("X:");
        gyr_y_lbl_->setText("Y:");
        gyr_z_lbl_->setText("Z:");
        roll_lbl_->setText("横滚:");
        pitch_lbl_->setText("俯仰:");
        yaw_lbl_->setText("航向:");
        quat_w_lbl_->setText("W:");
        quat_x_lbl_->setText("X:");
        quat_y_lbl_->setText("Y:");
        quat_z_lbl_->setText("Z:");
    }
}

void ImuPanel::updateData(const VaporView::ImuData& imu_data, quint64 gnss_timestamp_us)
{
    if (imu_data.valid)
    {
        source_label_->setText(imuFrameTypeName(imu_data.frame_type));
        source_label_->setProperty("data-valid", true);
        source_label_->style()->unpolish(source_label_);
        source_label_->style()->polish(source_label_);
        quint64 imuTimestampUs = 0;
        if (imu_data.from_hi83 && imu_data.system_time_us > 0)
        {
            imuTimestampUs = static_cast<quint64>(imu_data.system_time_us);
        }
        else if (imu_data.system_time_ms > 0)
        {
            imuTimestampUs = static_cast<quint64>(imu_data.system_time_ms) * 1000ULL;
        }

        bool ppsValid = false;
        quint64 deltaUs = 0;
        if (imuTimestampUs > 0 && gnss_timestamp_us > 0)
        {
            deltaUs = (imuTimestampUs > gnss_timestamp_us) ? (imuTimestampUs - gnss_timestamp_us)
                                                           : (gnss_timestamp_us - imuTimestampUs);
            ppsValid = deltaUs <= kImuPpsSyncWindowUs;
        }

        const QString formattedText = (imuTimestampUs > 0 && ppsValid)
            ? QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(imuTimestampUs / 1000ULL), QTimeZone::UTC)
                  .toString("yyyy-MM-dd HH:mm:ss.zzz 'UTC'")
            : QStringLiteral("---");
        QString rawText = QStringLiteral("---");
        if (imu_data.from_hi83 && imu_data.system_time_us > 0)
        {
            rawText = QString::number(imu_data.system_time_us) + "us";
        }
        else if (imu_data.system_time_ms > 0)
        {
            rawText = QString::number(imu_data.system_time_ms) + "ms";
        }
        time_label_->setText(QString("%1\n%2").arg(formattedText, rawText));

        if (gnss_timestamp_us == 0 || imuTimestampUs == 0)
        {
            pps_label_->setText(is_english_ ? "Unknown" : "未知");
        }
        else if (ppsValid)
        {
            const QString deltaText = fixedTextField(QString::number(deltaUs / 1000ULL), 6);
            pps_label_->setText(is_english_
                ? QString("Valid (Δ%1 ms)").arg(deltaText)
                : QString("有效 (差值%1 ms)").arg(deltaText));
        }
        else
        {
            const QString deltaText = fixedTextField(QString::number(deltaUs / 1000ULL), 6);
            pps_label_->setText(is_english_
                ? QString("Invalid (Δ%1 ms)").arg(deltaText)
                : QString("无效 (差值%1 ms)").arg(deltaText));
        }

        acc_x_label_->setText(fixedDecimalWithUnit(imu_data.acceleration[0], 3, 8, QString()));
        acc_y_label_->setText(fixedDecimalWithUnit(imu_data.acceleration[1], 3, 8, QString()));
        acc_z_label_->setText(fixedDecimalWithUnit(imu_data.acceleration[2], 3, 8, QString()));

        gyr_x_label_->setText(fixedDecimalWithUnit(imu_data.gyroscope[0], 3, 8, QString()));
        gyr_y_label_->setText(fixedDecimalWithUnit(imu_data.gyroscope[1], 3, 8, QString()));
        gyr_z_label_->setText(fixedDecimalWithUnit(imu_data.gyroscope[2], 3, 8, QString()));

        roll_label_->setText(fixedDecimalWithUnit(imu_data.rpy[0], 2, 7, QStringLiteral("°")));
        pitch_label_->setText(fixedDecimalWithUnit(imu_data.rpy[1], 2, 7, QStringLiteral("°")));
        yaw_label_->setText(fixedDecimalWithUnit(imu_data.rpy[2], 2, 7, QStringLiteral("°")));

        quat_w_label_->setText(fixedDecimalWithUnit(imu_data.quaternion[0], 4, 8, QString()));
        quat_x_label_->setText(fixedDecimalWithUnit(imu_data.quaternion[1], 4, 8, QString()));
        quat_y_label_->setText(fixedDecimalWithUnit(imu_data.quaternion[2], 4, 8, QString()));
        quat_z_label_->setText(fixedDecimalWithUnit(imu_data.quaternion[3], 4, 8, QString()));
    }
    else
    {
        source_label_->setText(QString::fromStdString(imu_data.error_message));
        source_label_->setProperty("data-valid", false);
        source_label_->style()->unpolish(source_label_);
        source_label_->style()->polish(source_label_);
        time_label_->setText(QStringLiteral("---\n---"));
        pps_label_->setText(is_english_ ? "Unknown" : "未知");
    }
}

PtbPanel::PtbPanel(QWidget *parent)
    : QWidget(parent)
    , rate_label_(nullptr)
    , pressure_label_(nullptr)
    , status_label_(nullptr)
    , pressure_lbl_(nullptr)
    , pressure_trend_plot_(nullptr)
    , is_english_(false)
{
    setupUi();
}

QSize PtbPanel::minimumSizeHint() const
{
    QSize hint = QWidget::minimumSizeHint();
    hint.setWidth(std::min(hint.width(), kEnvironmentPanelSideBySideMinimumWidth));
    return hint;
}

void PtbPanel::setupUi()
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(2);
    layout->setContentsMargins(6, 1, 6, 4);

    rate_label_ = new VaporView::VisualTextLabel(this);
    rate_label_->setObjectName("rateLabel");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rate_label_->setMinimumHeight(20);
    setFixedNumericLabelWidth(rate_label_, {QStringLiteral("999.9 Hz"), QStringLiteral("-- Hz")}, 2);

    auto *pressLayout = new QHBoxLayout();
    pressLayout->setSpacing(1);
    pressure_lbl_ = new QLabel(this);
    pressure_lbl_->setObjectName("fieldLabel");
    pressure_lbl_->setMinimumHeight(20);
    pressLayout->addWidget(pressure_lbl_);
    pressure_label_ = new QLabel("--- hPa", this);
    pressure_label_->setObjectName("highlightedValue");
    pressure_label_->setMinimumHeight(20);
    pressure_label_->setAlignment(Qt::AlignCenter | Qt::AlignVCenter);
    setMinimumNumericLabelWidth(pressure_label_, {QStringLiteral("1100.00 hPa"), QStringLiteral("--- hPa")}, 4);
    pressLayout->addWidget(pressure_label_, 1);
    pressLayout->addWidget(rate_label_);
    layout->addLayout(pressLayout);

    pressure_trend_plot_ = new EnvironmentTrendSparklineWidget(
        VaporView::AppThemeColor::PlotSeriesPressure,
        QStringLiteral("hPa"),
        this);
    pressure_trend_plot_->setObjectName(QStringLiteral("environmentPressureTrendPlot"));
    pressure_trend_plot_->setProperty("seriesRole", QStringLiteral("pressure"));
    layout->addWidget(pressure_trend_plot_);

    setEnglish(false);
}

void PtbPanel::updateRate(double hz)
{
    if (rate_label_)
    {
        rate_label_->setText((hz > 0.0 && std::isfinite(hz))
            ? fixedDecimalWithUnit(hz, 1, 6, QStringLiteral("Hz"))
            : QStringLiteral("%1 Hz").arg(fixedTextField(QStringLiteral("--"), 6)));
    }
}

void PtbPanel::setEnglish(bool english)
{
    is_english_ = english;
    setLocalizedFixedTextLabel(pressure_lbl_, english, QStringLiteral("Pressure:"), QStringLiteral("气压:"));
    if (pressure_trend_plot_)
    {
        pressure_trend_plot_->setEnglish(english);
    }
}

void PtbPanel::updateData(const VaporView::PtbData& ptb_data)
{
    if (ptb_data.valid)
    {
        pressure_label_->setText(fixedDecimalWithUnit(ptb_data.pressure_hpa, 2, 7, QStringLiteral("hPa")));
        pressure_label_->setProperty("data-valid", true);
        polishNumericLabel(pressure_label_);
        if (pressure_trend_plot_ &&
            shouldAppendEnvironmentSample(ptb_data.timestamp,
                                          last_pressure_timestamp_,
                                          has_pressure_timestamp_))
        {
            pressure_trend_plot_->appendSample(ptb_data.pressure_hpa, ptb_data.timestamp);
        }
    }
    else
    {
        pressure_label_->setText(fixedDecimalWithUnit(std::numeric_limits<double>::quiet_NaN(), 2, 7, QStringLiteral("hPa")));
        pressure_label_->setProperty("data-valid", false);
        polishNumericLabel(pressure_label_);
    }
}

HmpPanel::HmpPanel(QWidget *parent)
    : QWidget(parent)
    , rate_label_(nullptr)
    , humidity_rate_label_(nullptr)
    , humidity_label_(nullptr)
    , temperature_label_(nullptr)
    , status_label_(nullptr)
    , temp_lbl_(nullptr)
    , humidity_lbl_(nullptr)
    , temperature_trend_plot_(nullptr)
    , humidity_trend_plot_(nullptr)
    , is_english_(false)
{
    setupUi();
}

QSize HmpPanel::minimumSizeHint() const
{
    QSize hint = QWidget::minimumSizeHint();
    hint.setWidth(std::min(hint.width(), kEnvironmentPanelSideBySideMinimumWidth));
    return hint;
}

void HmpPanel::setupUi()
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(2);
    layout->setContentsMargins(6, 1, 6, 4);

    rate_label_ = new VaporView::VisualTextLabel(this);
    rate_label_->setObjectName("rateLabel");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rate_label_->setMinimumHeight(20);
    setFixedNumericLabelWidth(rate_label_, {QStringLiteral("999.9 Hz"), QStringLiteral("-- Hz")}, 2);
    humidity_rate_label_ = new VaporView::VisualTextLabel(this);
    humidity_rate_label_->setObjectName("rateLabel");
    humidity_rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    humidity_rate_label_->setMinimumHeight(20);
    setFixedNumericLabelWidth(humidity_rate_label_, {QStringLiteral("999.9 Hz"), QStringLiteral("-- Hz")}, 2);

    auto *tempLayout = new QHBoxLayout();
    tempLayout->setSpacing(1);
    temp_lbl_ = new QLabel(this);
    temp_lbl_->setObjectName("fieldLabel");
    temp_lbl_->setMinimumHeight(20);
    tempLayout->addWidget(temp_lbl_);
    temperature_label_ = new QLabel("--- °C", this);
    temperature_label_->setObjectName("highlightedValue");
    temperature_label_->setMinimumHeight(20);
    setFixedNumericLabelWidth(temperature_label_, {QStringLiteral("-99.9 °C"), QStringLiteral("100.0 °C"), QStringLiteral("--- °C")}, 4);
    tempLayout->addWidget(temperature_label_);
    tempLayout->addStretch();
    tempLayout->addWidget(rate_label_);
    layout->addLayout(tempLayout);

    temperature_trend_plot_ = new EnvironmentTrendSparklineWidget(
        VaporView::AppThemeColor::PlotSeriesTemperature,
        QStringLiteral("°C"),
        this);
    temperature_trend_plot_->setObjectName(QStringLiteral("environmentTemperatureTrendPlot"));
    temperature_trend_plot_->setProperty("seriesRole", QStringLiteral("temperature"));
    layout->addWidget(temperature_trend_plot_);

    auto *humidLayout = new QHBoxLayout();
    humidLayout->setSpacing(1);
    humidity_lbl_ = new QLabel(this);
    humidity_lbl_->setObjectName("fieldLabel");
    humidity_lbl_->setMinimumHeight(20);
    humidLayout->addWidget(humidity_lbl_);
    humidity_label_ = new QLabel("--- %RH", this);
    humidity_label_->setObjectName("highlightedValue");
    humidity_label_->setMinimumHeight(20);
    setFixedNumericLabelWidth(humidity_label_, {QStringLiteral("100.0 %RH"), QStringLiteral("--- %RH")}, 4);
    humidLayout->addWidget(humidity_label_);
    humidLayout->addStretch();
    humidLayout->addWidget(humidity_rate_label_);
    layout->addLayout(humidLayout);

    humidity_trend_plot_ = new EnvironmentTrendSparklineWidget(
        VaporView::AppThemeColor::PlotSeriesHumidity,
        QStringLiteral("%RH"),
        this);
    humidity_trend_plot_->setObjectName(QStringLiteral("environmentHumidityTrendPlot"));
    humidity_trend_plot_->setProperty("seriesRole", QStringLiteral("humidity"));
    layout->addWidget(humidity_trend_plot_);

    setEnglish(false);
}

void HmpPanel::updateRate(double hz)
{
    const QString rateText = (hz > 0.0 && std::isfinite(hz))
        ? fixedDecimalWithUnit(hz, 1, 6, QStringLiteral("Hz"))
        : QStringLiteral("%1 Hz").arg(fixedTextField(QStringLiteral("--"), 6));
    for (QLabel *label : {rate_label_, humidity_rate_label_})
    {
        if (label)
        {
            label->setText(rateText);
        }
    }
}

void HmpPanel::setEnglish(bool english)
{
    is_english_ = english;
    setLocalizedFixedTextLabel(temp_lbl_, english, QStringLiteral("Temp:"), QStringLiteral("温度:"));
    setLocalizedFixedTextLabel(humidity_lbl_, english, QStringLiteral("Humidity:"), QStringLiteral("湿度:"));
    if (temperature_trend_plot_)
    {
        temperature_trend_plot_->setEnglish(english);
    }
    if (humidity_trend_plot_)
    {
        humidity_trend_plot_->setEnglish(english);
    }
}

void HmpPanel::updateData(const VaporView::HmpData& hmp_data)
{
    if (hmp_data.valid)
    {
        temperature_label_->setText(fixedDecimalWithUnit(hmp_data.temperature, 1, 5, QStringLiteral("°C")));
        humidity_label_->setText(fixedDecimalWithUnit(hmp_data.humidity, 1, 5, QStringLiteral("%RH")));
        temperature_label_->setProperty("data-valid", true);
        polishNumericLabel(temperature_label_);
        humidity_label_->setProperty("data-valid", true);
        polishNumericLabel(humidity_label_);
        if (temperature_trend_plot_ &&
            shouldAppendEnvironmentSample(hmp_data.timestamp,
                                          last_temperature_timestamp_,
                                          has_temperature_timestamp_))
        {
            temperature_trend_plot_->appendSample(hmp_data.temperature, hmp_data.timestamp);
        }
        if (humidity_trend_plot_ &&
            shouldAppendEnvironmentSample(hmp_data.timestamp,
                                          last_humidity_timestamp_,
                                          has_humidity_timestamp_))
        {
            humidity_trend_plot_->appendSample(hmp_data.humidity, hmp_data.timestamp);
        }
    }
    else
    {
        temperature_label_->setText(fixedDecimalWithUnit(std::numeric_limits<double>::quiet_NaN(), 1, 5, QStringLiteral("°C")));
        humidity_label_->setText(fixedDecimalWithUnit(std::numeric_limits<double>::quiet_NaN(), 1, 5, QStringLiteral("%RH")));
        temperature_label_->setProperty("data-valid", false);
        polishNumericLabel(temperature_label_);
        humidity_label_->setProperty("data-valid", false);
        polishNumericLabel(humidity_label_);
    }
}

LidarPanel::LidarPanel(QWidget *parent)
    : QWidget(parent)
    , rate_label_(nullptr)
    , distance_label_(nullptr)
    , strength_label_(nullptr)
    , status_label_(nullptr)
    , distance_lbl_(nullptr)
    , strength_lbl_(nullptr)
    , is_english_(false)
{
    setupUi();
}

QSize LidarPanel::minimumSizeHint() const
{
    QSize hint = QWidget::minimumSizeHint();
    hint.setWidth(std::min(hint.width(), kEnvironmentPanelSideBySideMinimumWidth));
    return hint;
}

void LidarPanel::setupUi()
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(2);
    layout->setContentsMargins(6, 1, 6, 4);

    rate_label_ = new VaporView::VisualTextLabel(QStringLiteral("0.0 Hz"), this);
    rate_label_->setObjectName("rateLabel");
    rate_label_->setMinimumHeight(20);
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setFixedNumericLabelWidth(rate_label_, {QStringLiteral("999.9 Hz"), QStringLiteral("-- Hz")}, 2);

    auto *distanceLayout = new QHBoxLayout();
    distanceLayout->setSpacing(1);
    distance_lbl_ = new QLabel(this);
    distance_lbl_->setObjectName("fieldLabel");
    distance_lbl_->setMinimumHeight(20);
    distanceLayout->addWidget(distance_lbl_);
    distance_label_ = new QLabel("--- m", this);
    distance_label_->setObjectName("highlightedValue");
    distance_label_->setMinimumHeight(20);
    setFixedNumericLabelWidth(distance_label_, {QStringLiteral("9999.99 m"), QStringLiteral("--- m")}, 2);
    distanceLayout->addWidget(distance_label_);
    distanceLayout->addSpacing(10);
    strength_lbl_ = new QLabel(this);
    strength_lbl_->setObjectName("fieldLabel");
    strength_lbl_->setMinimumHeight(20);
    distanceLayout->addWidget(strength_lbl_);
    strength_label_ = new QLabel("---", this);
    strength_label_->setObjectName("highlightedValue");
    strength_label_->setMinimumHeight(20);
    setFixedNumericLabelWidth(strength_label_, {QStringLiteral("65535"), QStringLiteral("---")}, 2);
    distanceLayout->addWidget(strength_label_);
    distanceLayout->addStretch();
    distanceLayout->addWidget(rate_label_);
    layout->addLayout(distanceLayout);

    setEnglish(false);
}

void LidarPanel::updateRate(double hz)
{
    if (rate_label_)
    {
        rate_label_->setText((hz > 0.0 && std::isfinite(hz))
            ? fixedDecimalWithUnit(hz, 1, 6, QStringLiteral("Hz"))
            : QStringLiteral("%1 Hz").arg(fixedTextField(QStringLiteral("--"), 6)));
    }
}

void LidarPanel::setEnglish(bool english)
{
    is_english_ = english;
    setLocalizedFixedTextLabel(distance_lbl_, english, QStringLiteral("Distance:"), QStringLiteral("距离:"));
    setLocalizedFixedTextLabel(strength_lbl_, english, QStringLiteral("Strength:"), QStringLiteral("强度:"));
}

void LidarPanel::updateData(const VaporView::LidarData& lidar_data)
{
    if (lidar_data.valid)
    {
        distance_label_->setText(fixedDecimalWithUnit(lidar_data.distance_m, 2, 6, QStringLiteral("m")));
        strength_label_->setText(fixedTextField(QString::number(lidar_data.signal_strength), 4));
        distance_label_->setProperty("data-valid", true);
        strength_label_->setProperty("data-valid", true);
        polishNumericLabel(distance_label_);
        polishNumericLabel(strength_label_);
    }
    else
    {
        distance_label_->setText(fixedDecimalWithUnit(std::numeric_limits<double>::quiet_NaN(), 2, 6, QStringLiteral("m")));
        strength_label_->setText(fixedTextField(QStringLiteral("---"), 4));
        distance_label_->setProperty("data-valid", false);
        strength_label_->setProperty("data-valid", false);
        polishNumericLabel(distance_label_);
        polishNumericLabel(strength_label_);
    }
}
