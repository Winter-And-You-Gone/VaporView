#include "ground/widgets/TemperatureControllerWidgets.h"

#include "shared/theme/AppTheme.h"
#include "shared/theme/SingleLevelPopupMenu.h"
#include "ground/widgets/VisualTextLabel.h"
#include "ground/widgets/TelemetryPanels.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QDir>
#include <QDoubleValidator>
#include <QEasingCurve>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QIntValidator>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPalette>
#include <QPolygonF>
#include <QRadioButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStyle>
#include <QStyleOptionToolButton>
#include <QSvgRenderer>
#include <QTimer>
#include <QToolButton>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

using VaporView::AppThemeColor;
using VaporView::appThemeColor;
using VaporView::SingleLevelPopupMenu;
using VaporView::SingleLevelPopupMenuRow;
using VaporView::SingleLevelPopupTextAlignment;
using VaporView::Ground::Widgets::SourceModeOverviewSwitchButton;
using VaporView::Ground::Widgets::TemperatureControllerOverviewPanel;

namespace
{
constexpr int kHomeOverviewBodyPadding = 2;
constexpr int kTemperatureControllerPlotWidth = 260;
constexpr int kTemperatureControllerPlotMinHeight = 190;
constexpr int kTemperatureControllerInputWidth = 112;
constexpr int kTemperatureControllerWideInputWidth = 138;
constexpr int kTemperatureControllerRs485BaudWidth = 100;
constexpr int kTemperatureControllerTopEnableWidth = 106;
constexpr int kTemperatureControllerTopEnableHeight = 34;
constexpr int kTemperatureControllerTopModeWidth = 132;
constexpr int kTemperatureControllerTopTargetWidth = 172;
constexpr int kTemperatureControllerCompactInputWidth = 112;
constexpr int kTemperatureControllerCompactPidInputWidth = 82;
constexpr int kTemperatureControllerAdvancedInputWidth = 128;
constexpr int kTemperatureControllerSensorInputWidth = 82;
constexpr int kTemperatureControllerPtCoefficientInputWidth = 104;
constexpr int kTemperatureControllerPolynomialInputWidth = 62;
constexpr int kTemperatureControllerSensorFieldSpacing = 6;
constexpr int kTemperatureControllerSensorLabelPadding = 16;
constexpr int kTemperatureControllerConfigRowHeight = 38;
constexpr int kTemperatureControllerTopControlsHeight = 38;
constexpr int kTemperatureControllerNavigationButtonHeight = 30;
constexpr int kTemperatureControllerNavigationHorizontalMargin = 4;
constexpr int kTemperatureControllerNavigationVerticalMargin = 3;
constexpr int kTemperatureControllerNavigationSpacing = 4;
constexpr int kTemperatureControllerRowSpacing = 8;
constexpr int kTemperatureControllerChannelConfigSubStackHeight =
    kTemperatureControllerConfigRowHeight * 2 + kTemperatureControllerRowSpacing;
constexpr int kTemperatureControllerChannelStackHeight =
    kTemperatureControllerChannelConfigSubStackHeight +
    kTemperatureControllerRowSpacing +
    kTemperatureControllerConfigRowHeight;
constexpr int kTemperatureControllerCommonStackHeight = kTemperatureControllerChannelStackHeight;
constexpr int kTemperatureControllerHistoryLimit = 240;
constexpr const char *kTextWidthCandidatesProperty = "_vv_text_width_candidates";
constexpr const char *kTextWidthPaddingProperty = "_vv_text_width_padding";
constexpr const char *kNumericWidthCandidatesProperty = "_vv_numeric_width_candidates";
constexpr const char *kNumericWidthPaddingProperty = "_vv_numeric_width_padding";

void configureTemperatureControllerTwoRowGrid(QGridLayout *layout, int horizontalSpacing)
{
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(horizontalSpacing);
    layout->setVerticalSpacing(kTemperatureControllerRowSpacing);
    layout->setAlignment(Qt::AlignTop);
    layout->setRowMinimumHeight(0, kTemperatureControllerConfigRowHeight);
    layout->setRowMinimumHeight(1, kTemperatureControllerConfigRowHeight);
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

void applyFixedNumericLabelWidth(QLabel *label, const QStringList& candidates, int padding = 0)
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
    label->setFixedWidth(width);
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

void applyFixedTextLabelWidth(QLabel *label, const QStringList& candidates, int padding = 0)
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
    label->setFixedWidth(width);
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

void refreshFixedTextLabelWidth(QLabel *label)
{
    if (!label)
    {
        return;
    }
    const QStringList candidates = label->property(kTextWidthCandidatesProperty).toStringList();
    if (!candidates.isEmpty())
    {
        applyFixedTextLabelWidth(label,
                                 candidates,
                                 std::max(0, label->property(kTextWidthPaddingProperty).toInt()));
    }
}

void refreshFixedNumericLabelWidth(QLabel *label)
{
    if (!label)
    {
        return;
    }
    const QStringList candidates = label->property(kNumericWidthCandidatesProperty).toStringList();
    if (!candidates.isEmpty())
    {
        applyFixedNumericLabelWidth(label,
                                    candidates,
                                    std::max(0, label->property(kNumericWidthPaddingProperty).toInt()));
    }
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

QStringList temperatureControllerStatusLabelWidthCandidates()
{
    return {QStringLiteral("Internal:"),
            QStringLiteral("Error:"),
            QStringLiteral("Mode:"),
            QStringLiteral("Controller Mode:"),
            QStringLiteral("自身温度:"),
            QStringLiteral("错误码:"),
            QStringLiteral("温控器模式:")};
}

QStringList temperatureControllerCompactStatusLabelWidthCandidates()
{
    return {QStringLiteral("Internal:"),
            QStringLiteral("Error:"),
            QStringLiteral("自身温度:"),
            QStringLiteral("错误码:")};
}

QStringList temperatureControllerRateLabelWidthCandidates()
{
    return {QStringLiteral("Polling rate:"), QStringLiteral("轮询频率:")};
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

QString formatTemperaturePolynomial(qint64 mantissa, int exponent)
{
    if (mantissa == 0)
    {
        return QStringLiteral("0E+0");
    }
    const double coefficient = static_cast<double>(mantissa) / 10000000000000.0;
    return QStringLiteral("%1E%2%3")
        .arg(coefficient, 0, 'g', 14)
        .arg(exponent >= 0 ? QLatin1Char('+') : QLatin1Char('-'))
        .arg(std::abs(exponent));
}

bool parseTemperaturePolynomial(const QString& text, qint64& mantissa, qint16& exponent)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
    {
        mantissa = 0;
        exponent = 0;
        return true;
    }
    bool ok = false;
    const double value = QLocale::c().toDouble(trimmed, &ok);
    if (!ok || !std::isfinite(value))
    {
        return false;
    }
    if (qFuzzyIsNull(value))
    {
        mantissa = 0;
        exponent = 0;
        return true;
    }
    int exp = 0;
    double normalized = value;
    while (std::abs(normalized) >= 10.0 && exp < 100)
    {
        normalized /= 10.0;
        ++exp;
    }
    while (std::abs(normalized) < 1.0 && exp > -100)
    {
        normalized *= 10.0;
        --exp;
    }
    const qint64 scaled = qRound64(normalized * 10000000000000.0);
    if (scaled < -99999999999999LL || scaled > 99999999999999LL || exp < -100 || exp > 100)
    {
        return false;
    }
    mantissa = scaled;
    exponent = static_cast<qint16>(exp);
    return true;
}

QString formatTemperatureSensorDecimal(qint64 scaledValue, double scale, int decimals)
{
    return QLocale::c().toString(static_cast<double>(scaledValue) / scale, 'f', decimals);
}

QString formatTemperatureSensorDouble(double value, int decimals)
{
    return QLocale::c().toString(value, 'f', decimals);
}

QString findResourceFile(const QString& relativePath)
{
    const QString appDir = QApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(relativePath),
        QDir(appDir).filePath(QStringLiteral("../") + relativePath),
        QDir(appDir).filePath(QStringLiteral("../../") + relativePath)};
    for (const QString& path : candidates)
    {
        if (QFileInfo::exists(path))
        {
            return path;
        }
    }
    return QString();
}

bool isDarkToolbarTheme()
{
    if (qApp)
    {
        const QVariant value = qApp->property(VaporView::kAppDarkThemeProperty);
        if (value.isValid())
        {
            return value.toBool();
        }
        const QPalette palette = qApp->palette();
        return palette.color(QPalette::Window).lightness() < 128 ||
               palette.color(QPalette::Base).lightness() < 128;
    }
    return false;
}

QColor toolbarColor(AppThemeColor color)
{
    return appThemeColor(color, isDarkToolbarTheme());
}

QPixmap renderLucidePixmap(const QByteArray& svgData, const QColor& color, qreal devicePixelRatio)
{
    QByteArray tinted = svgData;
    tinted.replace("currentColor", color.name(QColor::HexRgb).toUtf8());
    const qreal dpr = std::max<qreal>(1.0, devicePixelRatio);
    constexpr int kLogicalSize = 32;
    const int physicalSize = std::max(1, static_cast<int>(std::ceil(kLogicalSize * dpr)));
    QPixmap pixmap(physicalSize, physicalSize);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QSvgRenderer renderer(tinted);
    if (!renderer.isValid())
    {
        return QPixmap();
    }
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, QRectF(2, 2, 28, 28));
    return pixmap;
}

void addLucideIconPixmaps(QIcon& icon, const QByteArray& svgData, const QColor& color, QIcon::Mode mode)
{
    for (const qreal dpr : {1.0, 1.25, 1.5, 2.0, 3.0})
    {
        icon.addPixmap(renderLucidePixmap(svgData, color, dpr), mode);
    }
}

QIcon createLucideIcon(const QString& iconName, const QColor& color)
{
    static QHash<QString, QIcon> cache;
    const QColor disabledColor = toolbarColor(AppThemeColor::ToolbarDisabled);
    const QString cacheKey = QStringLiteral("%1:%2:%3")
        .arg(iconName)
        .arg(color.rgba(), 0, 16)
        .arg(disabledColor.rgba(), 0, 16);
    const auto it = cache.constFind(cacheKey);
    if (it != cache.constEnd())
    {
        return it.value();
    }
    QFile file(findResourceFile(QStringLiteral("resources/lucide/%1.svg").arg(iconName)));
    if (!file.open(QIODevice::ReadOnly))
    {
        return QIcon();
    }
    const QByteArray svgData = file.readAll();
    QIcon icon;
    addLucideIconPixmaps(icon, svgData, color, QIcon::Normal);
    addLucideIconPixmaps(icon, svgData, disabledColor, QIcon::Disabled);
    cache.insert(cacheKey, icon);
    return icon;
}

void setDangerTextPalette(QWidget *widget)
{
    if (!widget)
    {
        return;
    }
    QPalette palette = widget->palette();
    const QColor danger = appThemeColor(AppThemeColor::Danger, VaporView::isDarkThemeEnabled());
    palette.setColor(QPalette::WindowText, danger);
    palette.setColor(QPalette::Text, danger);
    widget->setPalette(palette);
}

void setWidgetBooleanProperty(QWidget *widget, const char *propertyName, bool enabled)
{
    if (!widget)
    {
        return;
    }
    if (widget->property(propertyName).toBool() == enabled)
    {
        widget->update();
        return;
    }
    widget->setProperty(propertyName, enabled);
    if (widget->style())
    {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
    }
    widget->update();
}
} // namespace

class TemperatureTrendPlotWidget : public QWidget
{
public:
    explicit TemperatureTrendPlotWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("temperatureTrendPlot"));
        setFont(numericFontFrom(font()));
        applyPlotSizing();
        updateSampleProperties();
    }

    void setCompactMode(bool compact)
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

    void setEnglish(bool english)
    {
        is_english_ = english;
        update();
    }

    void setChannelIndex(int channelIndex)
    {
        channel_index_ = std::clamp(channelIndex, 0, 1);
        update();
    }

    void setSamples(const QVector<double>& samples)
    {
        samples_ = samples;
        updateSampleProperties();
        update();
    }

    void setTargetTemperature(double celsius)
    {
        target_temperature_c_ = std::isfinite(celsius)
            ? celsius
            : std::numeric_limits<double>::quiet_NaN();
        updateSampleProperties();
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QPalette palette = this->palette();
        QColor background = palette.color(QPalette::Base);
        if (!background.isValid() || background.alpha() == 0)
        {
            background = palette.color(QPalette::Window);
        }
        const bool dark = background.lightness() < 128;
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
        constexpr int kXAxisTicks = 4;
        const qreal leftAxisWidth = axisFm.horizontalAdvance(QStringLiteral("999")) + 6.0;
        constexpr qreal kBottomAxisHeight = 18.0;
        const QRectF plotRect = rect().adjusted(leftAxisWidth, 4.0, -4.0, -kBottomAxisHeight);
        auto drawGridAndAxes = [&](double minValue, double maxValue, int sampleCount) {
            painter.setPen(QPen(grid, 1));
            for (int i = 0; i <= kXAxisTicks; ++i)
            {
                const qreal x = plotRect.left() + plotRect.width() * i / static_cast<qreal>(kXAxisTicks);
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
            for (int i = 0; i <= kXAxisTicks; ++i)
            {
                const int sampleIndex = sampleCount <= 1
                    ? 0
                    : qRound((sampleCount - 1) * i / static_cast<double>(kXAxisTicks));
                const qreal x = plotRect.left() + plotRect.width() * i / static_cast<qreal>(kXAxisTicks);
                const QString label = QString::number(sampleIndex);
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
        finiteSamples.reserve(samples_.size());
        for (double value : samples_)
        {
            if (std::isfinite(value))
            {
                finiteSamples.append(value);
            }
        }

        if (finiteSamples.isEmpty() || plotRect.width() <= 1.0 || plotRect.height() <= 1.0)
        {
            const auto [minValue, maxValue] = temperatureAxisRange(QVector<double>(), target_temperature_c_);
            drawGridAndAxes(minValue, maxValue, 0);
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
        drawGridAndAxes(minValue, maxValue, finiteSamples.size());

        QPolygonF polyline;
        polyline.reserve(finiteSamples.size());
        const int count = finiteSamples.size();
        for (int i = 0; i < count; ++i)
        {
            const double ratio = count == 1 ? 0.0 : static_cast<double>(i) / static_cast<double>(count - 1);
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

private:
    static QString axisTickLabel(double value)
    {
        return std::abs(value - std::round(value)) < 0.05
            ? QString::number(qRound(value))
            : QString::number(value, 'f', 1);
    }

    static std::pair<double, double> temperatureAxisRange(const QVector<double>& finiteSamples, double targetTemperature)
    {
        double minValue = std::isfinite(targetTemperature) ? targetTemperature - 2.0 : 20.0;
        double maxValue = std::isfinite(targetTemperature) ? targetTemperature + 2.0 : 25.0;
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

    void updateSampleProperties()
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
        setProperty("xAxisTickCount", 5);
    }

    void applyPlotSizing()
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

    QVector<double> samples_;
    double target_temperature_c_ = std::numeric_limits<double>::quiet_NaN();
    int channel_index_ = 0;
    bool compact_mode_ = false;
    bool is_english_ = false;
};

QString temperatureOverviewNumberText(double value)
{
    if (!std::isfinite(value))
    {
        return QStringLiteral("---");
    }

    return QLocale::c().toString(value, 'f', 5);
}

QString temperatureOverviewReservedNumberText()
{
    return QStringLiteral("999.99999");
}

int temperatureOverviewValueFontSizePx(const QLabel *label, const QString& value)
{
    constexpr int kFallbackWidth = 99;
    constexpr int kHorizontalPadding = 10;
    constexpr int kMinimumFontSize = 14;
    constexpr int kMaximumFontSize = 19;
    const int width = label && label->width() > 0 ? label->width() : kFallbackWidth;
    const int availableWidth = std::max(32, width - kHorizontalPadding);
    QFont valueFont = label ? label->font() : QFont();
    valueFont.setWeight(QFont::Bold);
    const QString reservedText = temperatureOverviewReservedNumberText();
    for (int size = kMaximumFontSize; size >= kMinimumFontSize; --size)
    {
        valueFont.setPixelSize(size);
        const QFontMetrics metrics(valueFont);
        const int requiredWidth = std::max(metrics.horizontalAdvance(value),
                                           metrics.horizontalAdvance(reservedText));
        if (requiredWidth <= availableWidth)
        {
            return size;
        }
    }
    return kMinimumFontSize;
}

void setTemperatureOverviewPillText(QLabel *label, const QString& title, const QString& value)
{
    if (!label)
    {
        return;
    }

    const int valueFontSize = temperatureOverviewValueFontSizePx(label, value);
    label->setProperty("reservedValueText", temperatureOverviewReservedNumberText());
    label->setProperty("valueFontSizePx", valueFontSize);
    QFont valueFont = label->font();
    valueFont.setWeight(QFont::Bold);
    valueFont.setPixelSize(valueFontSize);
    const int availableWidth = std::max(32, label->width() - 10);
    label->setProperty("reservedValueFits",
                       QFontMetrics(valueFont).horizontalAdvance(temperatureOverviewReservedNumberText()) <= availableWidth);
    label->setTextFormat(Qt::RichText);
    label->setText(QStringLiteral(
        "<div align=\"center\" style=\"line-height: 14px; white-space: nowrap;\">"
        "<span style=\"font-size: 12px; font-weight: 700;\">%1</span><br/>"
        "<span style=\"font-size: %2px; font-weight: 700;\">%3</span>"
        "</div>")
        .arg(title.toHtmlEscaped(), QString::number(valueFontSize), value.toHtmlEscaped()));
    label->style()->unpolish(label);
    label->style()->polish(label);
}

class TemperatureOverviewSwitchButton final : public QPushButton
{
public:
    explicit TemperatureOverviewSwitchButton(QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        setCheckable(true);
        setObjectName(QStringLiteral("temperatureOverviewOutputSwitch"));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        thumb_position_ = isChecked() ? 1.0 : 0.0;
        thumb_animation_ = new QVariantAnimation(this);
        thumb_animation_->setDuration(180);
        thumb_animation_->setEasingCurve(QEasingCurve::OutCubic);
        connect(thumb_animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            const qreal progress = std::clamp(value.toReal(), 0.0, 1.0);
            constexpr qreal kPi = 3.14159265358979323846;
            thumb_position_ = thumb_start_position_ + (thumb_target_position_ - thumb_start_position_) * progress;
            thumb_jelly_ = std::sin(progress * kPi);
            update();
        });
        connect(thumb_animation_, &QVariantAnimation::finished, this, [this]() {
            thumb_position_ = thumb_target_position_;
            thumb_jelly_ = 0.0;
            update();
        });
        refreshText();
    }

    void setEnglish(bool english)
    {
        is_english_ = english;
        refreshText();
    }

    bool switchChecked() const
    {
        return isChecked();
    }

    void setSwitchChecked(bool checked, bool animated)
    {
        const QSignalBlocker blocker(this);
        setChecked(checked);
        refreshText();

        const qreal target = checked ? 1.0 : 0.0;
        if (animated)
        {
            animateThumbTo(target);
        }
        else
        {
            if (thumb_animation_)
            {
                thumb_animation_->stop();
            }
            thumb_position_ = target;
            thumb_target_position_ = target;
            thumb_start_position_ = target;
            thumb_jelly_ = 0.0;
            update();
        }
    }

protected:
    void nextCheckState() override
    {
        // The overview switch is controlled by the command confirmation flow.
        // Do not let QAbstractButton pre-toggle before the confirmation dialog.
    }

    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        const bool dark = VaporView::isDarkThemeEnabled();
        const bool checked = isChecked();
        const bool enabled = isEnabled();
        const QColor stateFill = checked
            ? appThemeColor(AppThemeColor::ToolbarGreen, dark)
            : appThemeColor(AppThemeColor::ToolbarRed, dark);
        const QColor border = appThemeColor(AppThemeColor::Border, dark);
        const QColor fill = appThemeColor(AppThemeColor::Surface, dark);
        const QColor switchFill = enabled
            ? stateFill
            : appThemeColor(AppThemeColor::Surface, dark);
        const QColor text = enabled
            ? appThemeColor(AppThemeColor::Primary, dark)
            : appThemeColor(AppThemeColor::TextMuted, dark);
        const QColor selectedFill = enabled
            ? appThemeColor(AppThemeColor::Surface, dark)
            : appThemeColor(AppThemeColor::SurfaceAlt, dark);
        const QColor selectedText = enabled
            ? stateFill
            : text;
        const QColor inactiveText = enabled
            ? appThemeColor(AppThemeColor::White, dark)
            : appThemeColor(AppThemeColor::TextMuted, dark);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF pillRect = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        constexpr qreal kControlRadius = 10.0;
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(fill);
        painter.drawRoundedRect(pillRect, kControlRadius, kControlRadius);

        const qreal gap = 3.0;
        const QRectF trackRect = pillRect.adjusted(gap, gap, -gap, -gap);
        QFont segmentFont = font();
        segmentFont.setWeight(QFont::DemiBold);

        const QRectF switchRect = trackRect;
        constexpr qreal kInnerGap = 2.0;
        const QRectF switchCapsuleRect = switchRect.adjusted(0.5, 0.5, -0.5, -0.5);
        const QRectF switchContentRect = switchCapsuleRect.adjusted(kInnerGap, kInnerGap, -kInnerGap, -kInnerGap);
        const qreal segmentWidth = switchContentRect.width() / 2.0;
        const qreal selectedLeft = switchContentRect.left() + segmentWidth * thumb_position_;
        QRectF selectedRect(selectedLeft, switchContentRect.top(), segmentWidth, switchContentRect.height());
        if (enabled && thumb_jelly_ > 0.001)
        {
            const qreal stretch = std::min<qreal>(segmentWidth * 0.22, 10.0) * thumb_jelly_;
            if (thumb_direction_ >= 0)
            {
                selectedRect.adjust(-stretch * 0.35, 0.0, stretch * 0.65, 0.0);
            }
            else
            {
                selectedRect.adjust(-stretch * 0.65, 0.0, stretch * 0.35, 0.0);
            }
            selectedRect.setLeft(std::max(selectedRect.left(), switchContentRect.left()));
            selectedRect.setRight(std::min(selectedRect.right(), switchContentRect.right()));
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(switchFill);
        painter.drawRoundedRect(switchCapsuleRect, kControlRadius - gap, kControlRadius - gap);
        painter.setPen(Qt::NoPen);
        painter.setBrush(selectedFill);
        painter.drawRoundedRect(selectedRect, kControlRadius - gap - kInnerGap, kControlRadius - gap - kInnerGap);

        const QRectF offRect(switchContentRect.left(), switchContentRect.top(), segmentWidth, switchContentRect.height());
        const QRectF onRect(switchContentRect.left() + segmentWidth, switchContentRect.top(), segmentWidth, switchContentRect.height());
        const bool offSelected = thumb_position_ < 0.5;
        auto drawSegmentText = [&painter](const QRectF& textRect, const QString& text) {
            const QRectF textBounds = QFontMetricsF(painter.font()).tightBoundingRect(text);
            const QPointF baseline(textRect.center().x() - textBounds.center().x(),
                                   textRect.center().y() - textBounds.center().y());
            painter.drawText(baseline, text);
        };
        painter.setFont(segmentFont);
        painter.setPen(offSelected ? selectedText : inactiveText);
        drawSegmentText(offRect, offText());
        painter.setPen(offSelected ? inactiveText : selectedText);
        drawSegmentText(onRect, onText());

        if (hasFocus())
        {
            painter.setPen(QPen(appThemeColor(AppThemeColor::Focus, dark), 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(pillRect.adjusted(2.0, 2.0, -2.0, -2.0),
                                    kControlRadius - 2.0,
                                    kControlRadius - 2.0);
        }
    }

private:
    QString offText() const
    {
        return is_english_ ? QStringLiteral("Off") : QStringLiteral("关闭");
    }

    QString onText() const
    {
        return is_english_ ? QStringLiteral("On") : QStringLiteral("开启");
    }

    QString outputLabelText() const
    {
        return is_english_ ? QStringLiteral("Output Enable") : QStringLiteral("输出使能");
    }

    void refreshText()
    {
        const QString text = QStringLiteral("%1: %2")
            .arg(outputLabelText(), isChecked() ? onText() : offText());
        setText(text);
        setToolTip(text);
        setStatusTip(text);
        setAccessibleName(text);
    }

    void animateThumbTo(qreal target)
    {
        if (!thumb_animation_)
        {
            thumb_position_ = target;
            thumb_target_position_ = target;
            thumb_start_position_ = target;
            thumb_jelly_ = 0.0;
            update();
            return;
        }

        if (qFuzzyCompare(thumb_position_, target))
        {
            thumb_position_ = target;
            thumb_target_position_ = target;
            thumb_start_position_ = target;
            thumb_jelly_ = 0.0;
            update();
            return;
        }

        thumb_animation_->stop();
        thumb_start_position_ = thumb_position_;
        thumb_target_position_ = target;
        thumb_direction_ = thumb_target_position_ >= thumb_start_position_ ? 1 : -1;
        thumb_jelly_ = 0.0;
        thumb_animation_->setStartValue(0.0);
        thumb_animation_->setEndValue(1.0);
        thumb_animation_->start();
    }

    bool is_english_ = false;
    qreal thumb_position_ = 0.0;
    qreal thumb_start_position_ = 0.0;
    qreal thumb_target_position_ = 0.0;
    qreal thumb_jelly_ = 0.0;
    int thumb_direction_ = 1;
    QVariantAnimation *thumb_animation_ = nullptr;
};

class SingleLevelPopupComboBox final : public QComboBox
{
public:
    explicit SingleLevelPopupComboBox(QWidget *parent = nullptr)
        : QComboBox(parent)
        , popup_menu_(new SingleLevelPopupMenu(this))
    {
        popup_menu_->setObjectName(QStringLiteral("singleLevelComboPopupMenu"));
        popup_menu_->setCornerRadius(10);
        popup_menu_->setPanelPadding(12);
        setProperty("usesSingleLevelPopupMenu", true);
    }

    void showPopup() override
    {
        rebuildPopupRows();
        popup_menu_->popupFrom(this);
    }

    void hidePopup() override
    {
        if (popup_menu_)
        {
            popup_menu_->hide();
        }
        QComboBox::hidePopup();
    }

    void setShowSelectionCheck(bool show)
    {
        show_selection_check_ = show;
    }

    void setPopupFitContents(bool fit)
    {
        popup_fit_contents_ = fit;
    }

private:
    bool itemEnabled(int index) const
    {
        if (!model())
        {
            return true;
        }
        const QModelIndex modelIndex = model()->index(index, modelColumn(), rootModelIndex());
        return !modelIndex.isValid() || (modelIndex.flags() & Qt::ItemIsEnabled);
    }

    void rebuildPopupRows()
    {
        if (!popup_menu_)
        {
            return;
        }

        popup_menu_->clear();
        const QIcon checkIcon = createLucideIcon(QStringLiteral("check"),
                                                 appThemeColor(AppThemeColor::MenuCheckText,
                                                               VaporView::isDarkThemeEnabled()));
        for (int i = 0; i < count(); ++i)
        {
            auto *row = new SingleLevelPopupMenuRow(popup_menu_);
            row->setText(itemText(i));
            row->setChecked(i == currentIndex());
            if (show_selection_check_)
            {
                row->setCheckIcon(checkIcon);
                row->setCheckIconSize(QSize(16, 16));
            }
            row->setTextAlignment(SingleLevelPopupTextAlignment::Left);
            row->setHorizontalPadding(18, 14);
            row->setCheckSlotWidth(show_selection_check_ ? 18 : 0);
            row->setRowSpacing(show_selection_check_ ? 6 : 0);
            row->setRowHeight(40);
            row->setMinimumRowWidth(width());
            row->setEnabled(itemEnabled(i));
            QWidgetAction *action = popup_menu_->addRow(row);
            if (!action)
            {
                continue;
            }
            action->setData(i);
            action->setEnabled(row->isEnabled());
            connect(action, &QAction::triggered, this, [this, i]() {
                if (i >= 0 && i < count())
                {
                    setCurrentIndex(i);
                }
            });
        }
        popup_menu_->refreshTheme();
        int popupContentWidth = width();
        if (popup_fit_contents_)
        {
            for (SingleLevelPopupMenuRow *row : popup_menu_->rows())
            {
                popupContentWidth = std::max(popupContentWidth, row->sizeHint().width());
            }
        }
        popup_menu_->setPanelContentWidth(popupContentWidth);
    }

    SingleLevelPopupMenu *popup_menu_ = nullptr;
    bool show_selection_check_ = true;
    bool popup_fit_contents_ = false;
};

class SourceModeOverviewSwitchButtonImpl final : public SourceModeOverviewSwitchButton
{
public:
    explicit SourceModeOverviewSwitchButtonImpl(QWidget *parent = nullptr)
        : SourceModeOverviewSwitchButton(parent)
    {
        setCheckable(true);
        setObjectName(QStringLiteral("sourceModeOverviewSwitch"));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        thumb_position_ = isChecked() ? 1.0 : 0.0;
        thumb_animation_ = new QVariantAnimation(this);
        thumb_animation_->setDuration(160);
        thumb_animation_->setEasingCurve(QEasingCurve::OutCubic);
        connect(thumb_animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            const qreal progress = std::clamp(value.toReal(), 0.0, 1.0);
            constexpr qreal kPi = 3.14159265358979323846;
            thumb_position_ = thumb_start_position_ + (thumb_target_position_ - thumb_start_position_) * progress;
            thumb_jelly_ = std::sin(progress * kPi);
            update();
        });
        connect(thumb_animation_, &QVariantAnimation::finished, this, [this]() {
            thumb_position_ = thumb_target_position_;
            thumb_jelly_ = 0.0;
            update();
        });
        refreshText();
    }

    void setEnglish(bool english)
    {
        is_english_ = english;
        refreshText();
        update();
    }

    bool switchChecked() const
    {
        return isChecked();
    }

    void setSwitchChecked(bool checked, bool animated)
    {
        const qreal target = checked ? 1.0 : 0.0;
        const bool continuingSameAnimation =
            !animated &&
            thumb_animation_ &&
            thumb_animation_->state() == QAbstractAnimation::Running &&
            qFuzzyCompare(thumb_target_position_, target);

        {
            const QSignalBlocker blocker(this);
            setChecked(checked);
        }
        refreshText();
        if (continuingSameAnimation)
        {
            update();
            return;
        }

        if (animated)
        {
            animateThumbTo(target);
        }
        else
        {
            if (thumb_animation_)
            {
                thumb_animation_->stop();
            }
            thumb_position_ = target;
            thumb_start_position_ = target;
            thumb_target_position_ = target;
            thumb_jelly_ = 0.0;
            update();
        }
    }

protected:
    void nextCheckState() override
    {
        // Source mode changes are routed through the existing data-source combo.
    }

    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        const bool dark = VaporView::isDarkThemeEnabled();
        const bool enabled = isEnabled();
        const QColor border = appThemeColor(AppThemeColor::Border, dark);
        const QColor fill = enabled
            ? appThemeColor(AppThemeColor::PrimarySubtle, dark)
            : appThemeColor(AppThemeColor::SurfaceAlt, dark);
        const QColor trackFill = enabled
            ? appThemeColor(AppThemeColor::Primary, dark)
            : appThemeColor(AppThemeColor::Surface, dark);
        const QColor selectedFill = enabled
            ? appThemeColor(AppThemeColor::Surface, dark)
            : appThemeColor(AppThemeColor::SurfaceAlt, dark);
        const QColor selectedText = enabled
            ? appThemeColor(AppThemeColor::Primary, dark)
            : appThemeColor(AppThemeColor::TextMuted, dark);
        const QColor inactiveText = enabled
            ? appThemeColor(AppThemeColor::White, dark)
            : appThemeColor(AppThemeColor::TextMuted, dark);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF outerRect = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        constexpr qreal kOuterRadius = 10.0;
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(fill);
        painter.drawRoundedRect(outerRect, kOuterRadius, kOuterRadius);

        constexpr qreal kInset = 3.0;
        constexpr qreal kInnerInset = 2.0;
        const QRectF trackRect = outerRect.adjusted(kInset, kInset, -kInset, -kInset);
        const QRectF contentRect = trackRect.adjusted(kInnerInset, kInnerInset, -kInnerInset, -kInnerInset);
        const qreal segmentWidth = contentRect.width() / 2.0;
        QRectF selectedRect(contentRect.left() + segmentWidth * thumb_position_,
                            contentRect.top(),
                            segmentWidth,
                            contentRect.height());
        if (enabled && thumb_jelly_ > 0.001)
        {
            const qreal stretch = std::min<qreal>(segmentWidth * 0.24, 12.0) * thumb_jelly_;
            if (thumb_direction_ >= 0)
            {
                selectedRect.adjust(-stretch * 0.35, 0.0, stretch * 0.65, 0.0);
            }
            else
            {
                selectedRect.adjust(-stretch * 0.65, 0.0, stretch * 0.35, 0.0);
            }
            selectedRect.setLeft(std::max(selectedRect.left(), contentRect.left()));
            selectedRect.setRight(std::min(selectedRect.right(), contentRect.right()));
        }
        const QRectF localRect(contentRect.left(), contentRect.top(), segmentWidth, contentRect.height());
        const QRectF remoteRect(contentRect.left() + segmentWidth, contentRect.top(), segmentWidth, contentRect.height());
        const bool localSelected = thumb_position_ < 0.5;

        painter.setPen(Qt::NoPen);
        painter.setBrush(trackFill);
        painter.drawRoundedRect(trackRect, kOuterRadius - kInset, kOuterRadius - kInset);
        painter.setBrush(selectedFill);
        painter.drawRoundedRect(selectedRect, kOuterRadius - kInset - kInnerInset, kOuterRadius - kInset - kInnerInset);

        QFont segmentFont = font();
        segmentFont.setWeight(QFont::DemiBold);
        painter.setFont(segmentFont);
        painter.setPen(localSelected ? selectedText : inactiveText);
        painter.drawText(localRect, Qt::AlignCenter, localText());
        painter.setPen(localSelected ? inactiveText : selectedText);
        painter.drawText(remoteRect, Qt::AlignCenter, remoteText());

        if (hasFocus())
        {
            painter.setPen(QPen(appThemeColor(AppThemeColor::Focus, dark), 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(outerRect.adjusted(2.0, 2.0, -2.0, -2.0),
                                    kOuterRadius - 2.0,
                                    kOuterRadius - 2.0);
        }
    }

private:
    QString localText() const
    {
        return is_english_ ? QStringLiteral("Local") : QStringLiteral("本地");
    }

    QString remoteText() const
    {
        return is_english_ ? QStringLiteral("Remote") : QStringLiteral("远程");
    }

    void refreshText()
    {
        const QString text = is_english_
            ? QStringLiteral("Source: %1").arg(isChecked() ? remoteText() : localText())
            : QStringLiteral("数据源：%1").arg(isChecked() ? remoteText() : localText());
        setText(text);
        setToolTip(text);
        setStatusTip(text);
        setAccessibleName(text);
    }

    void animateThumbTo(qreal target)
    {
        if (!thumb_animation_ || qFuzzyCompare(thumb_position_, target))
        {
            thumb_position_ = target;
            thumb_start_position_ = target;
            thumb_target_position_ = target;
            thumb_jelly_ = 0.0;
            update();
            return;
        }

        thumb_animation_->stop();
        thumb_start_position_ = thumb_position_;
        thumb_target_position_ = target;
        thumb_direction_ = thumb_target_position_ >= thumb_start_position_ ? 1 : -1;
        thumb_jelly_ = 0.0;
        thumb_animation_->setStartValue(0.0);
        thumb_animation_->setEndValue(1.0);
        thumb_animation_->start();
    }

    bool is_english_ = false;
    qreal thumb_position_ = 0.0;
    qreal thumb_start_position_ = 0.0;
    qreal thumb_target_position_ = 0.0;
    qreal thumb_jelly_ = 0.0;
    int thumb_direction_ = 1;
    QVariantAnimation *thumb_animation_ = nullptr;
};

class TemperatureOverviewChannelButton final : public QToolButton
{
public:
    explicit TemperatureOverviewChannelButton(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setProperty("textAlignment", QStringLiteral("center"));
        setProperty("iconAlignment", QStringLiteral("right"));
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        QStyleOptionToolButton option;
        initStyleOption(&option);
        option.text.clear();
        option.icon = QIcon();
        option.arrowType = Qt::NoArrow;
        style()->drawComplexControl(QStyle::CC_ToolButton, &option, &painter, this);

        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(palette().color(QPalette::ButtonText));
        QFont textFont = font();
        textFont.setWeight(QFont::DemiBold);
        painter.setFont(textFont);
        painter.drawText(rect().adjusted(18, 0, -18, 0), Qt::AlignCenter, text());

        const QIcon currentIcon = icon();
        if (!currentIcon.isNull())
        {
            const QSize size = iconSize().isValid() ? iconSize() : QSize(14, 14);
            const QRect iconRect(width() - size.width() - 10,
                                 (height() - size.height()) / 2,
                                 size.width(),
                                 size.height());
            currentIcon.paint(&painter, iconRect, Qt::AlignCenter, isEnabled() ? QIcon::Normal : QIcon::Disabled);
        }
    }
};

class TemperatureControllerOverviewPanelImpl final : public TemperatureControllerOverviewPanel
{
public:
    explicit TemperatureControllerOverviewPanelImpl(QWidget *parent = nullptr)
        : TemperatureControllerOverviewPanel(parent)
    {
        setObjectName(QStringLiteral("temperatureOverviewPanel"));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(kHomeOverviewBodyPadding,
                                   kHomeOverviewBodyPadding,
                                   kHomeOverviewBodyPadding,
                                   kHomeOverviewBodyPadding);
        layout->setSpacing(7);

        summary_widget_ = new QWidget(this);
        summary_widget_->setObjectName(QStringLiteral("temperatureOverviewSummary"));
        summary_widget_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        summary_widget_->setFixedWidth(kOverviewControlWidth);
        summary_widget_->installEventFilter(this);
        auto *summaryLayout = new QVBoxLayout(summary_widget_);
        summaryLayout->setContentsMargins(0, 0, 0, 0);
        summaryLayout->setSpacing(kOverviewSummarySpacing);

        channel_button_ = new TemperatureOverviewChannelButton(summary_widget_);
        channel_button_->setObjectName(QStringLiteral("temperatureOverviewChannelButton"));
        channel_button_->setFixedWidth(kOverviewControlWidth);
        channel_button_->setFixedHeight(kOverviewChannelHeight);
        channel_button_->setPopupMode(QToolButton::DelayedPopup);
        channel_button_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        channel_button_->setLayoutDirection(Qt::RightToLeft);
        channel_button_->setIconSize(QSize(14, 14));
        channel_button_->setFocusPolicy(Qt::StrongFocus);
        channel_button_->setCursor(Qt::PointingHandCursor);
        channel_menu_ = new SingleLevelPopupMenu(channel_button_);
        channel_menu_->setObjectName(QStringLiteral("temperatureOverviewChannelMenu"));
        channel_menu_->setFixedWidth(kOverviewMenuOuterWidth);
        channel_menu_->setPanelPadding(kOverviewMenuPadding);
        channel_menu_->setCornerRadius(kOverviewMenuCornerRadius);
        channel_menu_->refreshTheme();
        connect(channel_menu_, &QMenu::aboutToShow, this, [this]() {
            updateSummaryControlHeights();
            if (channel_menu_) channel_menu_->applyRoundedMask();
        });
        connect(channel_button_, &QToolButton::clicked, this, [this]() {
            popupChannelMenu();
        });
        auto configureChannelMenuAction = [this](SingleLevelPopupMenuRow *row, const QString& text) {
            QFont rowFont = row->font();
            rowFont.setWeight(QFont::DemiBold);
            row->setFont(rowFont);
            row->setTextAlignment(SingleLevelPopupTextAlignment::Center);
            row->setCheckSlotWidth(18);
            row->setCheckIconSize(QSize(14, 14));
            row->setHorizontalPadding(12, 10);
            row->setRowSpacing(4);
            row->setRowHeight(kOverviewChannelHeight);
            row->setMinimumRowWidth(kOverviewMenuItemWidth);
            row->setFixedSize(kOverviewMenuItemWidth, kOverviewChannelHeight);
            row->setCursor(Qt::PointingHandCursor);
            row->setFocusPolicy(Qt::NoFocus);
            row->setText(text);
            QWidgetAction *action = channel_menu_->addRow(row);
            action->setText(text);
            return action;
        };
        channel_menu_row_1_ = new SingleLevelPopupMenuRow(channel_menu_);
        channel_menu_row_2_ = new SingleLevelPopupMenuRow(channel_menu_);
        channel_action_1_ = configureChannelMenuAction(channel_menu_row_1_, QStringLiteral("通道1"));
        channel_action_2_ = configureChannelMenuAction(channel_menu_row_2_, QStringLiteral("通道2"));
        for (QAction *action : {channel_action_1_, channel_action_2_})
        {
            action->setCheckable(true);
        }
        connect(channel_action_1_, &QAction::triggered, this, [this]() {
            selectChannel(0);
            if (channel_menu_) channel_menu_->hide();
        });
        connect(channel_action_2_, &QAction::triggered, this, [this]() {
            selectChannel(1);
            if (channel_menu_) channel_menu_->hide();
        });
        channel_button_->setMenu(channel_menu_);
        channel_button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        summaryLayout->addWidget(channel_button_, 0);

        target_temp_value_ = new QLabel(summary_widget_);
        target_temp_value_->setObjectName(QStringLiteral("temperatureOverviewValuePill"));
        target_temp_value_->setAlignment(Qt::AlignCenter);
        target_temp_value_->setWordWrap(false);
        target_temp_value_->setFixedWidth(kOverviewControlWidth);
        target_temp_value_->setMinimumHeight(kOverviewMinimumValueHeight);
        target_temp_value_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        summaryLayout->addWidget(target_temp_value_, 1);

        current_temp_value_ = new QLabel(summary_widget_);
        current_temp_value_->setObjectName(QStringLiteral("temperatureOverviewValuePill"));
        current_temp_value_->setAlignment(Qt::AlignCenter);
        current_temp_value_->setWordWrap(false);
        current_temp_value_->setFixedWidth(kOverviewControlWidth);
        current_temp_value_->setMinimumHeight(kOverviewMinimumValueHeight);
        current_temp_value_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        summaryLayout->addWidget(current_temp_value_, 1);

        output_switch_button_ = new TemperatureOverviewSwitchButton(summary_widget_);
        output_switch_button_->setFixedWidth(kOverviewControlWidth);
        output_switch_button_->setFixedHeight(kOverviewOutputHeight);
        output_switch_button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        output_switch_button_->setStyleSheet(QStringLiteral(
            "QPushButton#temperatureOverviewOutputSwitch { min-height: %1px; max-height: %1px; }")
            .arg(kOverviewOutputHeight));
        connect(output_switch_button_, &QPushButton::clicked, this, [this]() {
            const bool requested = !output_switch_button_->switchChecked();
            if (output_enabled_callback_)
            {
                output_enabled_callback_(currentChannelNumber(), requested);
            }
        });
        summaryLayout->addWidget(output_switch_button_, 0);

        layout->addWidget(summary_widget_, 0);

        auto *divider = new QFrame(this);
        divider->setObjectName(QStringLiteral("homeOverviewDivider"));
        divider->setFixedWidth(1);
        divider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        layout->addWidget(divider);

        auto *plotSection = new QWidget(this);
        plotSection->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        auto *plotSectionLayout = new QVBoxLayout(plotSection);
        plotSectionLayout->setContentsMargins(0, 0, 0, 0);
        plotSectionLayout->setSpacing(0);

        plot_ = new TemperatureTrendPlotWidget(this);
        plot_->setCompactMode(true);
        plot_->setMinimumHeight(136);
        plot_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        plotSectionLayout->addWidget(plot_, 1);
        layout->addWidget(plotSection, 1);

        setEnglish(false);
        updateData(VaporView::TemperatureControllerData());
        updateSummaryControlHeights();
        scheduleSummaryControlHeightUpdate();
    }

    void setEnglish(bool english)
    {
        is_english_ = english;
        if (channel_action_1_) channel_action_1_->setText(english ? QStringLiteral("CH 1") : QStringLiteral("通道1"));
        if (channel_action_2_) channel_action_2_->setText(english ? QStringLiteral("CH 2") : QStringLiteral("通道2"));
        if (output_switch_button_) output_switch_button_->setEnglish(english);
        if (plot_)
        {
            plot_->setEnglish(english);
        }
        updateChannelButtonText();
        updateThemedIcons();
        refreshChannelUi();
    }

    void updateData(const VaporView::TemperatureControllerData& sample)
    {
        latest_data_ = sample;
        if (sample.valid)
        {
            for (int i = 0; i < static_cast<int>(measured_temperature_history_.size()); ++i)
            {
                const double target = sample.channels[i].target_temperature_c;
                if (std::isfinite(target))
                {
                    target_temperature_by_channel_[i] = target;
                }
                const double measured = sample.channels[i].measured_temperature_c;
                if (std::isfinite(measured))
                {
                    auto& history = measured_temperature_history_[i];
                    history.append(measured);
                    while (history.size() > kTemperatureControllerHistoryLimit)
                    {
                        history.removeFirst();
                    }
                }
            }
        }
        refreshChannelUi();
    }

    void setOutputEnabledCallback(std::function<void(quint8, bool)> callback)
    {
        output_enabled_callback_ = std::move(callback);
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        updateSummaryControlHeights();
        scheduleSummaryControlHeightUpdate();
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == summary_widget_ && event->type() == QEvent::Resize)
        {
            scheduleSummaryControlHeightUpdate();
        }
        return QWidget::eventFilter(watched, event);
    }

public:
    void updateThemedIcons()
    {
        if (channel_button_)
        {
            channel_button_->setIcon(createLucideIcon(QStringLiteral("chevron-down"),
                                                      toolbarColor(AppThemeColor::ToolbarBlue)));
            channel_button_->setIconSize(QSize(14, 14));
        }
        syncChannelMenuRow(channel_menu_row_1_, channel_action_1_);
        syncChannelMenuRow(channel_menu_row_2_, channel_action_2_);
    }

private:
    static constexpr int kOverviewControlWidth = 99;
    static constexpr int kOverviewMenuPadding = 12;
    static constexpr int kOverviewMenuCornerRadius = 10;
    static constexpr int kOverviewMenuShadowMargin = 22;
    static constexpr int kOverviewMenuItemWidth = kOverviewControlWidth;
    static constexpr int kOverviewMenuOuterWidth = kOverviewControlWidth + kOverviewMenuShadowMargin * 2;
    static constexpr int kOverviewSummarySpacing = 4;
    static constexpr int kOverviewChannelHeight = 34;
    static constexpr int kOverviewMinimumValueHeight = 44;
    static constexpr int kOverviewOutputHeight = 56;

    quint8 currentChannelNumber() const
    {
        return static_cast<quint8>(currentChannelIndex() + 1);
    }

    void scheduleSummaryControlHeightUpdate()
    {
        if (summary_height_update_pending_)
        {
            return;
        }
        summary_height_update_pending_ = true;
        QTimer::singleShot(0, this, [this]() {
            summary_height_update_pending_ = false;
            updateSummaryControlHeights();
        });
    }

    void updateSummaryControlHeights()
    {
        if (!summary_widget_ || !channel_button_)
        {
            return;
        }

        const int channelHeight = channel_button_->height();
        auto setMenuItemHeight = [](SingleLevelPopupMenuRow *widget, const QSize& size) {
            if (!widget)
            {
                return;
            }

            widget->setRowHeight(size.height());
            widget->setMinimumRowWidth(size.width());
            if (widget->minimumSize() != size || widget->maximumSize() != size || widget->size() != size)
            {
                widget->setFixedSize(size);
                widget->resize(size);
                widget->updateGeometry();
            }
        };
        setMenuItemHeight(channel_menu_row_1_, QSize(kOverviewMenuItemWidth, channelHeight));
        setMenuItemHeight(channel_menu_row_2_, QSize(kOverviewMenuItemWidth, channelHeight));
        if (channel_menu_)
        {
            channel_menu_->setFixedWidth(kOverviewMenuOuterWidth);
            channel_menu_->refreshTheme();
        }
    }

    void popupChannelMenu()
    {
        if (!channel_button_ || !channel_menu_)
        {
            return;
        }

        updateSummaryControlHeights();
        channel_menu_->popupFrom(channel_button_);
    }

    int currentChannelIndex() const
    {
        return std::clamp(selected_channel_index_, 0, 1);
    }

    QString channelText(int index) const
    {
        if (is_english_)
        {
            return index == 0 ? QStringLiteral("CH 1") : QStringLiteral("CH 2");
        }
        return index == 0 ? QStringLiteral("通道1") : QStringLiteral("通道2");
    }

    void selectChannel(int index)
    {
        const int nextIndex = std::clamp(index, 0, 1);
        if (selected_channel_index_ == nextIndex)
        {
            updateChannelButtonText();
            return;
        }
        selected_channel_index_ = nextIndex;
        updateChannelButtonText();
        refreshChannelUi();
    }

    void updateChannelButtonText()
    {
        if (channel_button_)
        {
            const QString text = channelText(currentChannelIndex());
            channel_button_->setText(text);
            channel_button_->setToolTip(is_english_
                ? QStringLiteral("Select temperature controller channel")
                : QStringLiteral("选择温控通道"));
            channel_button_->setAccessibleName(channel_button_->toolTip());
        }
        if (channel_action_1_) channel_action_1_->setChecked(currentChannelIndex() == 0);
        if (channel_action_2_) channel_action_2_->setChecked(currentChannelIndex() == 1);
        syncChannelMenuRow(channel_menu_row_1_, channel_action_1_);
        syncChannelMenuRow(channel_menu_row_2_, channel_action_2_);
    }

    void syncChannelMenuRow(SingleLevelPopupMenuRow *row, QAction *action)
    {
        if (!row || !action)
        {
            return;
        }
        row->setText(action->text());
        const bool selected = action->isChecked();
        row->setCheckIcon(createLucideIcon(QStringLiteral("check"),
                                           toolbarColor(AppThemeColor::MenuCheckText)));
        row->setChecked(selected);
        row->refreshTheme();
        row->update();
    }

    void refreshChannelUi()
    {
        const int index = currentChannelIndex();
        const bool valid = latest_data_.valid;
        const VaporView::TemperatureControllerChannelData& channel = latest_data_.channels[index];
        const bool measuredValid = valid && std::isfinite(channel.measured_temperature_c);
        const bool targetValid = valid && std::isfinite(channel.target_temperature_c);
        setTemperatureOverviewPillText(
            target_temp_value_,
            is_english_ ? QStringLiteral("Target Temp °C") : QStringLiteral("目标温度℃"),
            temperatureOverviewNumberText(targetValid ? channel.target_temperature_c : std::numeric_limits<double>::quiet_NaN()));
        setTemperatureOverviewPillText(
            current_temp_value_,
            is_english_ ? QStringLiteral("Current Temp °C") : QStringLiteral("当前温度℃"),
            temperatureOverviewNumberText(measuredValid ? channel.measured_temperature_c : std::numeric_limits<double>::quiet_NaN()));
        if (channel_button_)
        {
            channel_button_->setProperty("available", valid);
            channel_button_->setEnabled(valid);
            channel_button_->style()->unpolish(channel_button_);
            channel_button_->style()->polish(channel_button_);
            channel_button_->update();
        }
        if (output_switch_button_)
        {
            const bool outputEnabled = valid && channel.output_enabled;
            output_switch_button_->setEnabled(valid);
            output_switch_button_->setSwitchChecked(outputEnabled, output_switch_button_->switchChecked() != outputEnabled);
        }
        if (plot_)
        {
            plot_->setChannelIndex(index);
            plot_->setTargetTemperature(target_temperature_by_channel_[index]);
            plot_->setSamples(measured_temperature_history_[index]);
        }
    }

    QToolButton *channel_button_ = nullptr;
    QWidget *summary_widget_ = nullptr;
    SingleLevelPopupMenu *channel_menu_ = nullptr;
    QAction *channel_action_1_ = nullptr;
    QAction *channel_action_2_ = nullptr;
    SingleLevelPopupMenuRow *channel_menu_row_1_ = nullptr;
    SingleLevelPopupMenuRow *channel_menu_row_2_ = nullptr;
    QLabel *target_temp_value_ = nullptr;
    QLabel *current_temp_value_ = nullptr;
    TemperatureOverviewSwitchButton *output_switch_button_ = nullptr;
    TemperatureTrendPlotWidget *plot_ = nullptr;
    VaporView::TemperatureControllerData latest_data_;
    std::array<QVector<double>, 2> measured_temperature_history_{};
    std::array<double, 2> target_temperature_by_channel_{
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN()};
    std::function<void(quint8, bool)> output_enabled_callback_;
    int selected_channel_index_ = 0;
    bool summary_height_update_pending_ = false;
    bool is_english_ = false;
};

TemperatureControllerPanel::TemperatureControllerPanel(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

bool TemperatureControllerPanel::eventFilter(QObject *watched, QEvent *event)
{
    const QVariant channelProperty = watched->property("temperatureSensorPolynomialChannel");
    if (channelProperty.isValid() &&
        (event->type() == QEvent::Show || event->type() == QEvent::Resize))
    {
        alignSensorTopPolynomialFields(channelProperty.toInt());
    }
    if (watched->property("temperatureTopControlAlignmentHost").toBool() &&
        (event->type() == QEvent::Show || event->type() == QEvent::Resize))
    {
        QTimer::singleShot(0, this, [this]() {
            const int channelIndex = std::clamp(selected_channel_index_, 0, 1);
            alignChannelTopControlFields(channelIndex);
            alignCommonSettingsColumns(channelIndex);
        });
    }
    return QWidget::eventFilter(watched, event);
}

void TemperatureControllerPanel::alignSensorTopPolynomialFields(int channelIndex)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels_.size()))
    {
        return;
    }
    ChannelWidgets& channel = channels_[channelIndex];
    QWidget *subPageRow = channel.sensor_config_top_bar
        ? channel.sensor_config_top_bar->parentWidget()
        : nullptr;
    QWidget *fieldsGroup = subPageRow
        ? subPageRow->findChild<QWidget *>(
              QStringLiteral("temperatureSensorTopPolynomialFieldsChannel%1").arg(channelIndex + 1),
              Qt::FindDirectChildrenOnly)
        : nullptr;
    auto *fieldsLayout = fieldsGroup
        ? qobject_cast<QHBoxLayout *>(fieldsGroup->layout())
        : nullptr;
    if (!fieldsGroup || !fieldsLayout || fieldsGroup->width() <= 0)
    {
        return;
    }

    constexpr int fieldSpacing = 4;
    int labelWidth = 0;
    for (int column = 0; column < 4; ++column)
    {
        QLabel *label = channel.polynomial_label_text[static_cast<size_t>(column + 4)];
        if (!label)
        {
            return;
        }
        label->ensurePolished();
        labelWidth = std::max(labelWidth, label->fontMetrics().boundingRect(label->text()).width() + 4);
    }
    const int fieldWidth = std::max(1,
                                    (fieldsGroup->width() -
                                     fieldsLayout->spacing() * 3) / 4);
    for (int column = 0; column < 4; ++column)
    {
        QLabel *label = channel.polynomial_label_text[static_cast<size_t>(column + 4)];
        QLineEdit *edit = channel.polynomial_edits[static_cast<size_t>(column + 4)];
        QWidget *field = edit ? edit->parentWidget() : nullptr;
        auto *fieldLayout = field ? qobject_cast<QHBoxLayout *>(field->layout()) : nullptr;
        if (!label || !edit || !field || !fieldLayout)
        {
            return;
        }
        label->setFixedWidth(labelWidth);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(fieldSpacing);
        field->setFixedWidth(fieldWidth);
    }
    fieldsLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    fieldsLayout->invalidate();
    fieldsLayout->activate();
}

void TemperatureControllerPanel::alignChannelTopControlFields(int channelIndex)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels_.size()))
    {
        return;
    }
    ChannelWidgets& channel = channels_[channelIndex];
    QWidget *commonPage = channel.config_sub_stack && channel.config_sub_stack->count() > 0
        ? channel.config_sub_stack->widget(0)
        : nullptr;
    auto *commonGrid = commonPage ? qobject_cast<QGridLayout *>(commonPage->layout()) : nullptr;
    QLayoutItem *secondColumnItem = commonGrid ? commonGrid->itemAtPosition(0, 3) : nullptr;
    QLayoutItem *thirdColumnItem = commonGrid ? commonGrid->itemAtPosition(0, 6) : nullptr;
    QWidget *secondColumnLabel = secondColumnItem ? secondColumnItem->widget() : nullptr;
    QWidget *thirdColumnLabel = thirdColumnItem ? thirdColumnItem->widget() : nullptr;
    if (!channel.common_top_controls || !channel.common_top_leading_spacer ||
        !channel.common_top_middle_spacer || !channel.enable_field || !channel.auto_pid_field ||
        !secondColumnLabel || !thirdColumnLabel)
    {
        return;
    }

    const int secondColumnX = channel.common_top_controls->mapFromGlobal(
        secondColumnLabel->mapToGlobal(QPoint(0, 0))).x();
    const int thirdColumnX = channel.common_top_controls->mapFromGlobal(
        thirdColumnLabel->mapToGlobal(QPoint(0, 0))).x();
    const int enableFieldWidth = std::max(channel.enable_field->width(),
                                          channel.enable_field->sizeHint().width());
    channel.common_top_leading_spacer->setFixedWidth(std::max(0, secondColumnX));
    channel.common_top_middle_spacer->setFixedWidth(
        std::max(0, thirdColumnX - secondColumnX - enableFieldWidth));
    if (QLayout *layout = channel.common_top_controls->layout())
    {
        layout->invalidate();
        layout->activate();
    }
}

void TemperatureControllerPanel::alignCommonSettingsColumns(int channelIndex)
{
    if (!channel_stack_ || channelIndex < 0 ||
        channelIndex >= static_cast<int>(channels_.size()))
    {
        return;
    }

    QWidget *commonSettingsPage = channel_stack_->count() > 2
        ? channel_stack_->widget(2)
        : nullptr;
    QWidget *channelCommonPage = channels_[channelIndex].config_sub_stack &&
            channels_[channelIndex].config_sub_stack->count() > 0
        ? channels_[channelIndex].config_sub_stack->widget(0)
        : nullptr;
    auto *commonSettingsGrid = commonSettingsPage
        ? qobject_cast<QGridLayout *>(commonSettingsPage->layout())
        : nullptr;
    auto *channelCommonGrid = channelCommonPage
        ? qobject_cast<QGridLayout *>(channelCommonPage->layout())
        : nullptr;
    QLayoutItem *secondColumnItem = channelCommonGrid
        ? channelCommonGrid->itemAtPosition(0, 3)
        : nullptr;
    QWidget *secondColumnLabel = secondColumnItem ? secondColumnItem->widget() : nullptr;
    if (!commonSettingsPage || !commonSettingsGrid || !channelCommonGrid || !secondColumnLabel)
    {
        return;
    }

    channelCommonGrid->invalidate();
    channelCommonGrid->activate();
    commonSettingsGrid->invalidate();
    commonSettingsGrid->activate();

    int firstColumnWidth = commonSettingsGrid->cellRect(0, 0).width();
    for (int row = 0; row < 3; ++row)
    {
        QLayoutItem *item = commonSettingsGrid->itemAtPosition(row, 0);
        if (item && item->widget())
        {
            firstColumnWidth = std::max(firstColumnWidth, item->widget()->sizeHint().width());
        }
    }
    const int commonPageOriginX = channel_stack_->mapFromGlobal(
        commonSettingsPage->mapToGlobal(QPoint(0, 0))).x();
    const int targetSecondColumnX = channel_stack_->mapFromGlobal(
        secondColumnLabel->mapToGlobal(QPoint(0, 0))).x() - commonPageOriginX;
    commonSettingsGrid->setColumnMinimumWidth(
        1,
        std::max(0, targetSecondColumnX - firstColumnWidth));
    commonSettingsGrid->invalidate();
    commonSettingsGrid->activate();
}

void TemperatureControllerPanel::setupUi()
{
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(10);

    auto *statusLayout = new QGridLayout();
    statusLayout->setHorizontalSpacing(8);
    statusLayout->setVerticalSpacing(6);
    internal_temperature_lbl_ = new QLabel(this);
    internal_temperature_lbl_->setObjectName(QStringLiteral("fieldLabel"));
    internal_temperature_lbl_->setMinimumHeight(22);
    setFixedTextLabelWidth(internal_temperature_lbl_, temperatureControllerCompactStatusLabelWidthCandidates(), 4);
    internal_temperature_label_ = new QLabel(QStringLiteral("--- °C"), this);
    internal_temperature_label_->setObjectName(QStringLiteral("highlightedValue"));
    internal_temperature_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    internal_temperature_label_->setMinimumHeight(22);
    setFixedNumericLabelWidth(internal_temperature_label_,
                              {QStringLiteral(" -999.99 °C"), QStringLiteral("     --- °C")},
                              4);
    error_code_lbl_ = new QLabel(this);
    error_code_lbl_->setObjectName(QStringLiteral("fieldLabel"));
    error_code_lbl_->setMinimumHeight(22);
    setFixedTextLabelWidth(error_code_lbl_, temperatureControllerCompactStatusLabelWidthCandidates(), 4);
    error_code_label_ = new QLabel(QStringLiteral("0x0000"), this);
    error_code_label_->setObjectName(QStringLiteral("highlightedValue"));
    error_code_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    error_code_label_->setMinimumHeight(22);
    setFixedNumericLabelWidth(error_code_label_,
                              {QStringLiteral("0xFFFF"), QStringLiteral("---")},
                              4);
    rate_title_lbl_ = new QLabel(this);
    rate_title_lbl_->setObjectName(QStringLiteral("fieldLabel"));
    rate_title_lbl_->setProperty("temperatureControllerRateTitle", true);
    rate_title_lbl_->setMinimumHeight(22);
    setFixedTextLabelWidth(rate_title_lbl_, temperatureControllerRateLabelWidthCandidates(), 4);
    controller_mode_lbl_ = new QLabel(this);
    controller_mode_lbl_->setObjectName(QStringLiteral("fieldLabel"));
    controller_mode_lbl_->setProperty("temperatureControllerModeLabel", true);
    controller_mode_lbl_->setMinimumHeight(22);
    setFixedTextLabelWidth(controller_mode_lbl_, temperatureControllerStatusLabelWidthCandidates(), 4);
    controller_mode_combo_ = new SingleLevelPopupComboBox(this);
    controller_mode_combo_->setObjectName(QStringLiteral("temperatureControllerModeCombo"));
    controller_mode_combo_->setFixedWidth(206);
    controller_mode_combo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    controller_mode_combo_->addItem(QStringLiteral("独立控制"), 0);
    controller_mode_combo_->addItem(QStringLiteral("通道1温差控制"), 1);
    controller_mode_combo_->addItem(QStringLiteral("通道2跟随输出"), 2);
    controller_mode_combo_->addItem(QStringLiteral("温差控制+跟随输出"), 3);
    connect(controller_mode_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        emit controllerModeRequested(static_cast<quint16>(controller_mode_combo_->currentData().toUInt()));
    });
    rate_label_ = new VaporView::VisualTextLabel(this);
    rate_label_->setObjectName(QStringLiteral("rateLabel"));
    rate_label_->setProperty("temperatureControllerRateValue", true);
    rate_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    rate_label_->setMinimumHeight(22);
    setFixedNumericLabelWidth(rate_label_, {QStringLiteral("-999.9 Hz"), QStringLiteral("999.9 Hz"), QStringLiteral("-- Hz")}, 4);
    polishNumericLabel(rate_label_);
    statusLayout->addWidget(internal_temperature_lbl_, 0, 0, Qt::AlignVCenter | Qt::AlignLeft);
    statusLayout->addWidget(internal_temperature_label_, 0, 1, Qt::AlignVCenter | Qt::AlignLeft);
    statusLayout->addWidget(error_code_lbl_, 0, 2, Qt::AlignVCenter | Qt::AlignLeft);
    statusLayout->addWidget(error_code_label_, 0, 3, Qt::AlignVCenter | Qt::AlignLeft);
    statusLayout->addWidget(rate_title_lbl_, 0, 4, Qt::AlignVCenter | Qt::AlignLeft);
    statusLayout->addWidget(rate_label_, 0, 5, Qt::AlignVCenter | Qt::AlignLeft);
    statusLayout->addWidget(controller_mode_lbl_, 0, 7, Qt::AlignVCenter | Qt::AlignLeft);
    statusLayout->addWidget(controller_mode_combo_, 0, 8, Qt::AlignVCenter | Qt::AlignLeft);
    statusLayout->setColumnStretch(6, 1);
    layout->addLayout(statusLayout);

    auto *configCard = new QFrame(this);
    configCard->setObjectName(QStringLiteral("temperatureConfigCard"));
    configCard->setFrameShape(QFrame::NoFrame);
    configCard->setAttribute(Qt::WA_StyledBackground, true);
    configCard->setMinimumWidth(0);
    configCard->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    auto *configCardLayout = new QVBoxLayout(configCard);
    configCardLayout->setContentsMargins(12, 12, 12, 12);
    configCardLayout->setSpacing(kTemperatureControllerRowSpacing);

    auto *channelTopRow = new QWidget(configCard);
    channelTopRow->setObjectName(QStringLiteral("temperatureChannelTopRow"));
    channelTopRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *channelTopRowLayout = new QVBoxLayout(channelTopRow);
    channelTopRowLayout->setContentsMargins(0, 0, 0, 0);
    channelTopRowLayout->setSpacing(8);

    auto *channelSelectorRow = new QWidget(channelTopRow);
    channelSelectorRow->setObjectName(QStringLiteral("temperatureChannelSelectorRow"));
    channelSelectorRow->setProperty("temperatureTopControlAlignmentHost", true);
    channelSelectorRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    channelSelectorRow->installEventFilter(this);
    auto *channelSelectorRowLayout = new QHBoxLayout(channelSelectorRow);
    channelSelectorRowLayout->setContentsMargins(0, 0, 0, 0);
    channelSelectorRowLayout->setSpacing(12);

    channel_top_bar_ = new QFrame(channelTopRow);
    channel_top_bar_->setObjectName(QStringLiteral("temperatureChannelTopBar"));
    channel_top_bar_->setFrameShape(QFrame::NoFrame);
    channel_top_bar_->setAttribute(Qt::WA_StyledBackground, true);
    channel_top_bar_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *channelTopBarLayout = new QHBoxLayout(channel_top_bar_);
    channelTopBarLayout->setContentsMargins(kTemperatureControllerNavigationHorizontalMargin,
                                            kTemperatureControllerNavigationVerticalMargin,
                                            kTemperatureControllerNavigationHorizontalMargin,
                                            kTemperatureControllerNavigationVerticalMargin);
    channelTopBarLayout->setSpacing(kTemperatureControllerNavigationSpacing);

    auto createChannelButton = [this](int index) {
        auto *button = new QPushButton(this);
        button->setObjectName(QStringLiteral("temperatureChannelSelectorButton%1").arg(index + 1));
        button->setProperty("temperatureChannelSelector", true);
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::TabFocus);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        button->setFixedSize(72, kTemperatureControllerNavigationButtonHeight);
        button->setText(index == 0 ? QStringLiteral("通道1") : QStringLiteral("通道2"));
        connect(button, &QPushButton::clicked, this, [this, index]() {
            selectChannel(index);
        });
        return button;
    };
    channel_button_1_ = createChannelButton(0);
    channel_button_2_ = createChannelButton(1);
    common_settings_button_ = new QPushButton(this);
    common_settings_button_->setObjectName(QStringLiteral("temperatureCommonSettingsButton"));
    common_settings_button_->setProperty("temperatureChannelSelector", true);
    common_settings_button_->setCheckable(true);
    common_settings_button_->setCursor(Qt::PointingHandCursor);
    common_settings_button_->setFocusPolicy(Qt::TabFocus);
    common_settings_button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    common_settings_button_->setFixedSize(88, kTemperatureControllerNavigationButtonHeight);
    common_settings_button_->setText(QStringLiteral("通用设置"));
    connect(common_settings_button_, &QPushButton::clicked, this, [this]() {
        selectChannel(2);
    });
    channelTopBarLayout->addWidget(channel_button_1_);
    channelTopBarLayout->addWidget(channel_button_2_);
    channelTopBarLayout->addWidget(common_settings_button_);
    channelSelectorRowLayout->addWidget(channel_top_bar_, 0, Qt::AlignLeft | Qt::AlignVCenter);

    channel_top_controls_stack_ = new QStackedWidget(channelSelectorRow);
    channel_top_controls_stack_->setObjectName(QStringLiteral("temperatureChannelTopControlsStack"));
    channel_top_controls_stack_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    channel_top_controls_stack_->setFixedHeight(kTemperatureControllerTopControlsHeight);
    channel_top_controls_stack_->addWidget(createChannelTopControlsPage(0));
    channel_top_controls_stack_->addWidget(createChannelTopControlsPage(1));
    channelSelectorRowLayout->addWidget(channel_top_controls_stack_, 1, Qt::AlignVCenter);
    channelTopRowLayout->addWidget(channelSelectorRow);
    configCardLayout->addWidget(channelTopRow);

    channel_stack_ = new QStackedWidget(configCard);
    channel_stack_->setObjectName(QStringLiteral("temperatureChannelStack"));
    channel_stack_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    channel_stack_->addWidget(createChannelPage(0));
    channel_stack_->addWidget(createChannelPage(1));
    channel_stack_->addWidget(createCommonSettingsPage());
    updateChannelStackMinimumHeight();
    configCardLayout->addWidget(channel_stack_, 0);
    selectChannel(0);
    layout->addWidget(configCard, 0);

    temperature_plot_ = new TemperatureTrendPlotWidget(this);
    temperature_plot_->setProperty("temperatureConfigPlot", true);
    temperature_plot_->setCompactMode(true);
    temperature_plot_->setMinimumHeight(220);
    temperature_plot_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(temperature_plot_, 1);

    status_label_ = new QLabel(this);
    status_label_->setObjectName(QStringLiteral("fieldLabel"));
    status_label_->setMinimumHeight(20);
    status_label_->setWordWrap(true);
    layout->addWidget(status_label_);
    setEnglish(false);
    setCommandStatus(QStringLiteral("写入命令会在天空端读回确认后才返回成功。"));
    updateData(VaporView::TemperatureControllerData());
}

QWidget *TemperatureControllerPanel::createChannelTopControlsPage(int index)
{
    QWidget *page = new QWidget(channel_top_controls_stack_);
    page->setObjectName(QStringLiteral("temperatureChannelTopControlsPageChannel%1").arg(index + 1));
    page->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    page->setFixedHeight(kTemperatureControllerTopControlsHeight);
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    ChannelWidgets& channel = channels_[index];
    const quint8 channelNumber = static_cast<quint8>(index + 1);

    channel.common_top_controls = new QWidget(page);
    channel.common_top_controls->setObjectName(QStringLiteral("temperatureChannelCommonTopControlsChannel%1").arg(index + 1));
    channel.common_top_controls->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    channel.common_top_controls->setFixedHeight(kTemperatureControllerTopControlsHeight);
    auto *commonLayout = new QHBoxLayout(channel.common_top_controls);
    commonLayout->setContentsMargins(0, 0, 0, 0);
    commonLayout->setSpacing(0);
    auto makeCommonTopField = [this](const QString& text, QWidget *editor, QLabel *&label) {
        auto *field = new QWidget();
        field->setObjectName(QStringLiteral("temperatureTopBarField"));
        field->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        field->setFixedHeight(kTemperatureControllerTopControlsHeight);
        auto *fieldLayout = new QHBoxLayout(field);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(6);
        label = new QLabel(text, field);
        label->setObjectName(QStringLiteral("fieldLabel"));
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        fieldLayout->addWidget(label, 0, Qt::AlignVCenter);
        fieldLayout->addWidget(editor, 0, Qt::AlignVCenter);
        return field;
    };

    channel.common_top_leading_spacer = new QWidget(channel.common_top_controls);
    channel.common_top_leading_spacer->setObjectName(
        QStringLiteral("temperatureTopLeadingSpacerChannel%1").arg(index + 1));
    channel.common_top_leading_spacer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    channel.common_top_leading_spacer->setFixedSize(0, kTemperatureControllerTopControlsHeight);
    commonLayout->addWidget(channel.common_top_leading_spacer, 0, Qt::AlignVCenter);

    channel.enable_switch = new TemperatureOverviewSwitchButton(channel.common_top_controls);
    channel.enable_switch->setObjectName(QStringLiteral("temperatureOutputEnableSwitchChannel%1").arg(index + 1));
    channel.enable_switch->setProperty("temperatureOutputEnableSwitch", true);
    channel.enable_switch->setFixedSize(kTemperatureControllerTopEnableWidth, kTemperatureControllerTopEnableHeight);
    channel.enable_switch->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(channel.enable_switch, &QPushButton::clicked, this, [this, channelNumber, enableSwitch = channel.enable_switch]() {
        emit outputEnabledRequested(channelNumber, !static_cast<TemperatureOverviewSwitchButton *>(enableSwitch)->switchChecked());
    });
    channel.enable_field = makeCommonTopField(
        QStringLiteral("输出使能"), channel.enable_switch, channel.enable_label_text);
    commonLayout->addWidget(channel.enable_field, 0, Qt::AlignVCenter);

    channel.common_top_middle_spacer = new QWidget(channel.common_top_controls);
    channel.common_top_middle_spacer->setObjectName(
        QStringLiteral("temperatureTopMiddleSpacerChannel%1").arg(index + 1));
    channel.common_top_middle_spacer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    channel.common_top_middle_spacer->setFixedSize(0, kTemperatureControllerTopControlsHeight);
    commonLayout->addWidget(channel.common_top_middle_spacer, 0, Qt::AlignVCenter);

    auto *autoPidCombo = new SingleLevelPopupComboBox(channel.common_top_controls);
    autoPidCombo->setShowSelectionCheck(false);
    channel.auto_pid_combo = autoPidCombo;
    channel.auto_pid_combo->setObjectName(QStringLiteral("temperatureAutoPidComboChannel%1").arg(index + 1));
    channel.auto_pid_combo->setFixedWidth(kTemperatureControllerCompactInputWidth);
    channel.auto_pid_combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    channel.auto_pid_combo->addItem(QStringLiteral("关闭"), 0);
    channel.auto_pid_combo->addItem(QStringLiteral("PID自整定"), 1);
    channel.auto_pid_combo->addItem(QStringLiteral("实时优化(预留)"), 2);
    channel.auto_pid_field = makeCommonTopField(
        QStringLiteral("自动 PID"), channel.auto_pid_combo, channel.auto_pid_label_text);
    commonLayout->addWidget(channel.auto_pid_field, 0, Qt::AlignVCenter);
    commonLayout->addStretch(1);
    connect(channel.auto_pid_combo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this, channelNumber, combo = channel.auto_pid_combo](int) {
                emit autoPidRequested(channelNumber, static_cast<quint16>(combo->currentData().toUInt()));
            });
    layout->addWidget(channel.common_top_controls, 1, Qt::AlignVCenter);

    channel.sensor_model_field = new QWidget(page);
    channel.sensor_model_field->setObjectName(QStringLiteral("temperatureTopBarField"));
    channel.sensor_model_field->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    channel.sensor_model_field->setFixedHeight(kTemperatureControllerTopControlsHeight);
    auto *fieldLayout = new QHBoxLayout(channel.sensor_model_field);
    fieldLayout->setContentsMargins(0, 0, 0, 0);
    fieldLayout->setSpacing(8);

    channel.sensor_model_label_text = new QLabel(QStringLiteral("模型"), channel.sensor_model_field);
    channel.sensor_model_label_text->setObjectName(QStringLiteral("fieldLabel"));
    channel.sensor_model_label_text->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    channel.sensor_model_label_text->setMinimumHeight(22);
    channel.sensor_model_label_text->setFixedWidth(40);
    fieldLayout->addWidget(channel.sensor_model_label_text, 0, Qt::AlignLeft | Qt::AlignVCenter);

    channel.sensor_model_selector = new QWidget(channel.sensor_model_field);
    channel.sensor_model_selector->setObjectName(QStringLiteral("temperatureSensorModelSelectorChannel%1").arg(index + 1));
    channel.sensor_model_selector->setProperty("temperatureSensorModelSelector", true);
    channel.sensor_model_selector->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *selectorLayout = new QHBoxLayout(channel.sensor_model_selector);
    selectorLayout->setContentsMargins(0, 0, 0, 0);
    selectorLayout->setSpacing(16);

    channel.sensor_model_group = new QButtonGroup(page);
    channel.sensor_model_group->setExclusive(true);
    const std::array<QString, 4> labels = {
        QStringLiteral("B-Value"),
        QStringLiteral("PT"),
        QStringLiteral("S-H"),
        QStringLiteral("MF501"),
    };
    const std::array<QString, 4> objectSuffixes = {
        QStringLiteral("BValue"),
        QStringLiteral("Pt"),
        QStringLiteral("Sh"),
        QStringLiteral("Mf501"),
    };
    for (int i = 0; i < static_cast<int>(labels.size()); ++i)
    {
        auto *radio = new QRadioButton(labels[static_cast<size_t>(i)], channel.sensor_model_selector);
        radio->setObjectName(QStringLiteral("temperatureSensorModel%1RadioChannel%2")
                                 .arg(objectSuffixes[static_cast<size_t>(i)])
                                 .arg(index + 1));
        radio->setProperty("temperatureSensorModelOption", true);
        radio->setCursor(Qt::PointingHandCursor);
        channel.sensor_model_group->addButton(radio, i);
        channel.sensor_model_radios[static_cast<size_t>(i)] = radio;
        selectorLayout->addWidget(radio, 0, Qt::AlignVCenter);
    }
    if (auto *firstRadio = channel.sensor_model_group->button(0))
    {
        firstRadio->setChecked(true);
    }
    fieldLayout->addWidget(channel.sensor_model_selector, 0, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(channel.sensor_model_field, 0, Qt::AlignVCenter);
    channel.common_top_controls->setVisible(true);
    channel.sensor_model_field->setVisible(false);

    const int channelIndex = index;
    connect(channel.sensor_model_group, &QButtonGroup::idToggled, this, [this, channelIndex](int, bool checked) {
        if (checked)
        {
            emitSensorConfigRequest(channelIndex);
        }
    });
    return page;
}

QWidget *TemperatureControllerPanel::createChannelPage(int index)
{
    QWidget *page = new QWidget(channel_stack_);
    page->setObjectName(QStringLiteral("temperatureChannelConfigPageChannel%1").arg(index + 1));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(kTemperatureControllerRowSpacing);
    layout->setAlignment(Qt::AlignTop);

    ChannelWidgets& channel = channels_[index];
    channel.sensor_config_top_bar = new QFrame(page);
    channel.sensor_config_top_bar->setObjectName(QStringLiteral("temperatureChannelSubTopBar"));
    channel.sensor_config_top_bar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *barLayout = new QHBoxLayout(channel.sensor_config_top_bar);
    barLayout->setContentsMargins(kTemperatureControllerNavigationHorizontalMargin,
                                  kTemperatureControllerNavigationVerticalMargin,
                                  kTemperatureControllerNavigationHorizontalMargin,
                                  kTemperatureControllerNavigationVerticalMargin);
    barLayout->setSpacing(kTemperatureControllerNavigationSpacing);

    auto fitSubTabButtonWidth = [](QPushButton *button) {
        if (!button)
        {
            return;
        }
        button->ensurePolished();
        button->setFixedWidth(std::max(88, button->fontMetrics().horizontalAdvance(button->text()) + 40));
    };
    auto makeTabButton = [this, index, bar = channel.sensor_config_top_bar, fitSubTabButtonWidth](const QString& text, int pageIndex) {
        auto *button = new QPushButton(text, bar);
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::TabFocus);
        button->setProperty("temperatureChannelSubSelector", true);
        button->setFixedHeight(kTemperatureControllerNavigationButtonHeight);
        fitSubTabButtonWidth(button);
        connect(button, &QPushButton::clicked, this, [this, index, pageIndex]() {
            selectChannelSubPage(index, pageIndex);
        });
        return button;
    };
    channel.common_params_button = makeTabButton(QStringLiteral("常用参数"), 0);
    channel.advanced_params_button = makeTabButton(QStringLiteral("专业参数"), 1);
    channel.sensor_config_button = makeTabButton(QStringLiteral("传感器配置"), 2);
    channel.common_params_button->setObjectName(QStringLiteral("temperatureChannelCommonParamsButton%1").arg(index + 1));
    channel.advanced_params_button->setObjectName(QStringLiteral("temperatureChannelAdvancedParamsButton%1").arg(index + 1));
    channel.sensor_config_button->setObjectName(QStringLiteral("temperatureChannelSensorConfigButton%1").arg(index + 1));
    barLayout->addWidget(channel.common_params_button);
    barLayout->addWidget(channel.advanced_params_button);
    barLayout->addWidget(channel.sensor_config_button);
    channel.sensor_config_top_bar->setMinimumWidth(channel.sensor_config_top_bar->sizeHint().width());

    auto *subPageRow = new QWidget(page);
    subPageRow->setObjectName(QStringLiteral("temperatureChannelSubPageRowChannel%1").arg(index + 1));
    subPageRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *subPageRowLayout = new QHBoxLayout(subPageRow);
    subPageRowLayout->setContentsMargins(0, 0, 0, 0);
    subPageRowLayout->setSpacing(12);
    subPageRowLayout->addWidget(channel.sensor_config_top_bar, 0, Qt::AlignLeft | Qt::AlignVCenter);

    auto *sensorTopPolynomialFields = new QWidget(subPageRow);
    sensorTopPolynomialFields->setObjectName(
        QStringLiteral("temperatureSensorTopPolynomialFieldsChannel%1").arg(index + 1));
    sensorTopPolynomialFields->setProperty("temperatureSensorPolynomialChannel", index);
    sensorTopPolynomialFields->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    sensorTopPolynomialFields->installEventFilter(this);
    auto *sensorTopPolynomialLayout = new QHBoxLayout(sensorTopPolynomialFields);
    sensorTopPolynomialLayout->setContentsMargins(0, 0, 0, 0);
    sensorTopPolynomialLayout->setSpacing(4);
    sensorTopPolynomialFields->setVisible(false);
    subPageRowLayout->addWidget(sensorTopPolynomialFields, 1, Qt::AlignVCenter);

    channel.config_sub_stack = new QStackedWidget(page);
    channel.config_sub_stack->setObjectName(QStringLiteral("temperatureChannelConfigSubStackChannel%1").arg(index + 1));
    channel.config_sub_stack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    channel.config_sub_stack->setFixedHeight(kTemperatureControllerChannelConfigSubStackHeight);

    channel.config_sub_stack->addWidget(createChannelCommonParamsPage(index));
    channel.config_sub_stack->addWidget(createChannelAdvancedParamsPage(index));
    channel.config_sub_stack->addWidget(createChannelSensorConfigPage(index));
    layout->addWidget(channel.config_sub_stack, 0);
    layout->addWidget(subPageRow, 0);
    selectChannelSubPage(index, 0);
    return page;
}

QWidget *TemperatureControllerPanel::createChannelCommonParamsPage(int index)
{
    QWidget *page = new QWidget(channels_[index].config_sub_stack);
    page->setObjectName(QStringLiteral("temperatureChannelCommonParamsPageChannel%1").arg(index + 1));
    auto *layout = new QGridLayout(page);
    configureTemperatureControllerTwoRowGrid(layout, 6);
    ChannelWidgets& channel = channels_[index];

    auto makeFieldLabel = [this](const QString& text) {
        auto *label = new QLabel(text, this);
        label->setObjectName(QStringLiteral("fieldLabel"));
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setMinimumHeight(22);
        return label;
    };

    const quint8 channelNumber = static_cast<quint8>(index + 1);

    channel.kp_spin = new QSpinBox(this);
    channel.ki_spin = new QSpinBox(this);
    channel.kd_spin = new QSpinBox(this);
    channel.kp_spin->setObjectName(QStringLiteral("temperaturePidKpSpinChannel%1").arg(index + 1));
    channel.ki_spin->setObjectName(QStringLiteral("temperaturePidKiSpinChannel%1").arg(index + 1));
    channel.kd_spin->setObjectName(QStringLiteral("temperaturePidKdSpinChannel%1").arg(index + 1));
    for (QSpinBox *spin : {channel.kp_spin, channel.ki_spin, channel.kd_spin})
    {
        spin->setRange(0, std::numeric_limits<int>::max());
        spin->setFixedWidth(kTemperatureControllerCompactPidInputWidth);
        spin->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    QLabel *kpLabelText = nullptr;
    QLabel *kiLabelText = nullptr;
    QLabel *kdLabelText = nullptr;
    kpLabelText = makeFieldLabel(QStringLiteral("P"));
    kiLabelText = makeFieldLabel(QStringLiteral("I"));
    kdLabelText = makeFieldLabel(QStringLiteral("D"));

    channel.mode_combo = new SingleLevelPopupComboBox(page);
    channel.mode_combo->setObjectName(QStringLiteral("temperatureOutputModeComboChannel%1").arg(index + 1));
    channel.mode_combo->setFixedWidth(kTemperatureControllerTopModeWidth);
    channel.mode_combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    channel.mode_combo->addItem(QStringLiteral("制冷和加热"), 0);
    channel.mode_combo->addItem(QStringLiteral("制冷"), 1);
    channel.mode_combo->addItem(QStringLiteral("加热"), 2);
    channel.mode_combo->addItem(QStringLiteral("关闭"), 3);
    channel.mode_label_text = makeFieldLabel(QStringLiteral("输出模式"));
    connect(channel.mode_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, channelNumber, combo = channel.mode_combo](int) {
        emit outputModeRequested(channelNumber, static_cast<quint16>(combo->currentData().toUInt()));
    });

    channel.target_spin = new QDoubleSpinBox(page);
    channel.target_spin->setObjectName(QStringLiteral("temperatureTargetSpinChannel%1").arg(index + 1));
    channel.target_spin->setRange(-40.0, 100.0);
    channel.target_spin->setDecimals(5);
    channel.target_spin->setSuffix(QStringLiteral(" °C"));
    channel.target_spin->setFixedWidth(kTemperatureControllerTopTargetWidth);
    channel.target_label_text = makeFieldLabel(QStringLiteral("目标温度(°C)"));
    connect(channel.target_spin, &QDoubleSpinBox::editingFinished, this, [this, channelNumber, spin = channel.target_spin]() {
        emit targetTemperatureRequested(channelNumber, spin->value());
    });

    channel.max_output_spin = new QSpinBox(this);
    channel.max_output_spin->setObjectName(QStringLiteral("temperatureMaxOutputSpinChannel%1").arg(index + 1));
    setWidgetBooleanProperty(channel.max_output_spin, "temperatureMaxOutputWarning", true);
    setDangerTextPalette(channel.max_output_spin);
    channel.max_output_spin->setRange(0, 90);
    channel.max_output_spin->setSuffix(QStringLiteral(" %"));
    channel.max_output_spin->setFixedWidth(kTemperatureControllerCompactInputWidth);
    channel.max_output_label_text = makeFieldLabel(QStringLiteral("最大输出电压百分比(%)"));
    if (channel.max_output_label_text)
    {
        setWidgetBooleanProperty(channel.max_output_label_text, "temperatureMaxOutputWarning", true);
        setDangerTextPalette(channel.max_output_label_text);
    }

    layout->addWidget(kpLabelText, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(channel.kp_spin, 0, 1, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(kiLabelText, 0, 3, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(channel.ki_spin, 0, 4, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(kdLabelText, 0, 6, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(channel.kd_spin, 0, 7, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(channel.mode_label_text, 1, 0, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(channel.mode_combo, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(channel.target_label_text, 1, 3, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(channel.target_spin, 1, 4, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(channel.max_output_label_text, 1, 6, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(channel.max_output_spin, 1, 7, Qt::AlignLeft | Qt::AlignVCenter);
    layout->setColumnStretch(2, 1);
    layout->setColumnStretch(5, 1);

    connect(channel.max_output_spin, &QSpinBox::editingFinished, this, [this, channelNumber, spin = channel.max_output_spin]() {
        emit maxOutputPercentRequested(channelNumber, static_cast<quint16>(spin->value()));
    });
    auto emitPid = [this, channelNumber, index]() {
        const ChannelWidgets& channel = channels_[index];
        emit pidRequested(channelNumber,
                          static_cast<quint32>(channel.kp_spin->value()),
                          static_cast<quint32>(channel.ki_spin->value()),
                          static_cast<quint32>(channel.kd_spin->value()));
    };
    connect(channel.kp_spin, &QSpinBox::editingFinished, this, emitPid);
    connect(channel.ki_spin, &QSpinBox::editingFinished, this, emitPid);
    connect(channel.kd_spin, &QSpinBox::editingFinished, this, emitPid);
    QWidget::setTabOrder(channel.auto_pid_combo, channel.kp_spin);
    QWidget::setTabOrder(channel.kp_spin, channel.ki_spin);
    QWidget::setTabOrder(channel.ki_spin, channel.kd_spin);
    QWidget::setTabOrder(channel.kd_spin, channel.mode_combo);
    QWidget::setTabOrder(channel.mode_combo, channel.target_spin);
    QWidget::setTabOrder(channel.target_spin, channel.max_output_spin);
    QWidget::setTabOrder(channel.max_output_spin, channel.common_params_button);
    return page;
}

QWidget *TemperatureControllerPanel::createChannelAdvancedParamsPage(int index)
{
    QWidget *page = new QWidget(channels_[index].config_sub_stack);
    page->setObjectName(QStringLiteral("temperatureChannelAdvancedParamsPageChannel%1").arg(index + 1));
    auto *layout = new QGridLayout(page);
    configureTemperatureControllerTwoRowGrid(layout, 6);
    layout->setColumnStretch(2, 1);
    layout->setColumnStretch(5, 1);
    ChannelWidgets& channel = channels_[index];

    auto addField = [page, layout](int row,
                                   int fieldColumn,
                                   const QString& labelText,
                                   QWidget *editor,
                                   QLabel *&label) {
        label = new QLabel(labelText, page);
        label->setObjectName(QStringLiteral("fieldLabel"));
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setMinimumHeight(22);
        editor->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        const int labelColumn = fieldColumn * 3;
        layout->addWidget(label, row, labelColumn, Qt::AlignLeft | Qt::AlignVCenter);
        layout->addWidget(editor, row, labelColumn + 1, Qt::AlignLeft | Qt::AlignVCenter);
    };

    const quint8 channelNumber = static_cast<quint8>(index + 1);

    channel.overtemp_upper_spin = new QDoubleSpinBox(page);
    channel.overtemp_upper_spin->setObjectName(QStringLiteral("temperatureOvertempUpperSpinChannel%1").arg(index + 1));
    channel.overtemp_upper_spin->setRange(-3000.0, 5000.0);
    channel.overtemp_upper_spin->setDecimals(5);
    channel.overtemp_upper_spin->setSingleStep(0.00001);
    channel.overtemp_upper_spin->setFixedWidth(kTemperatureControllerAdvancedInputWidth);
    addField(0, 0, QStringLiteral("高温报警值(°C)"), channel.overtemp_upper_spin, channel.overtemp_upper_label_text);
    setDangerTextPalette(channel.overtemp_upper_label_text);

    channel.overtemp_lower_spin = new QDoubleSpinBox(page);
    channel.overtemp_lower_spin->setObjectName(QStringLiteral("temperatureOvertempLowerSpinChannel%1").arg(index + 1));
    channel.overtemp_lower_spin->setRange(-3000.0, 5000.0);
    channel.overtemp_lower_spin->setDecimals(5);
    channel.overtemp_lower_spin->setSingleStep(0.00001);
    channel.overtemp_lower_spin->setFixedWidth(kTemperatureControllerAdvancedInputWidth);
    addField(0, 1, QStringLiteral("低温报警值(°C)"), channel.overtemp_lower_spin, channel.overtemp_lower_label_text);
    setDangerTextPalette(channel.overtemp_lower_label_text);

    channel.temperature_slope_spin = new QDoubleSpinBox(page);
    channel.temperature_slope_spin->setObjectName(QStringLiteral("temperatureSlopeSpinChannel%1").arg(index + 1));
    channel.temperature_slope_spin->setRange(0.0, 10.0);
    channel.temperature_slope_spin->setDecimals(3);
    channel.temperature_slope_spin->setSingleStep(0.001);
    channel.temperature_slope_spin->setFixedWidth(kTemperatureControllerAdvancedInputWidth);
    addField(1, 0, QStringLiteral("温度变化速率(°C/s)"), channel.temperature_slope_spin, channel.temperature_slope_label_text);

    channel.startup_delay_spin = new QSpinBox(page);
    channel.startup_delay_spin->setObjectName(QStringLiteral("temperatureStartupDelaySpinChannel%1").arg(index + 1));
    channel.startup_delay_spin->setRange(3, 180);
    channel.startup_delay_spin->setSuffix(QStringLiteral(" s"));
    channel.startup_delay_spin->setFixedWidth(kTemperatureControllerAdvancedInputWidth);
    addField(1, 1, QStringLiteral("开机输出延时(s)"), channel.startup_delay_spin, channel.startup_delay_label_text);

    channel.sensor_resistance_edit = new QLineEdit(page);
    channel.sensor_resistance_edit->setObjectName(QStringLiteral("temperatureSensorResistanceEditChannel%1").arg(index + 1));
    channel.sensor_resistance_edit->setReadOnly(true);
    channel.sensor_resistance_edit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    channel.sensor_resistance_edit->setFixedWidth(kTemperatureControllerAdvancedInputWidth);
    channel.sensor_resistance_edit->setText(QStringLiteral("---"));
    addField(1, 2, QStringLiteral("传感器电阻(Ω)"), channel.sensor_resistance_edit, channel.sensor_resistance_label_text);

    connect(channel.overtemp_upper_spin, &QDoubleSpinBox::editingFinished, this, [this, channelNumber, spin = channel.overtemp_upper_spin]() {
        emit overtempUpperRequested(channelNumber, spin->value());
    });
    connect(channel.overtemp_lower_spin, &QDoubleSpinBox::editingFinished, this, [this, channelNumber, spin = channel.overtemp_lower_spin]() {
        emit overtempLowerRequested(channelNumber, spin->value());
    });
    connect(channel.temperature_slope_spin, &QDoubleSpinBox::editingFinished, this, [this, channelNumber, spin = channel.temperature_slope_spin]() {
        emit temperatureSlopeRequested(channelNumber, spin->value());
    });
    connect(channel.startup_delay_spin, &QSpinBox::editingFinished, this, [this, channelNumber, spin = channel.startup_delay_spin]() {
        emit startupDelayRequested(channelNumber, static_cast<quint16>(spin->value()));
    });
    return page;
}

QWidget *TemperatureControllerPanel::createChannelSensorConfigPage(int index)
{
    QWidget *page = new QWidget(channels_[index].config_sub_stack);
    page->setObjectName(QStringLiteral("temperatureChannelSensorConfigPageChannel%1").arg(index + 1));
    auto *layout = new QGridLayout(page);
    configureTemperatureControllerTwoRowGrid(layout, 0);
    for (int spacerColumn = 1; spacerColumn < 8; spacerColumn += 2)
    {
        layout->setColumnMinimumWidth(spacerColumn, 0);
        layout->setColumnStretch(spacerColumn, 1);
    }
    ChannelWidgets& channel = channels_[index];
    auto *sensorTopPolynomialFields = channel.sensor_config_top_bar->parentWidget()->findChild<QWidget *>(
        QStringLiteral("temperatureSensorTopPolynomialFieldsChannel%1").arg(index + 1),
        Qt::FindDirectChildrenOnly);
    auto *sensorTopPolynomialLayout =
        qobject_cast<QHBoxLayout *>(sensorTopPolynomialFields ? sensorTopPolynomialFields->layout() : nullptr);
    std::array<QList<QLabel *>, 5> fieldLabelsByColumn;
    std::array<QList<QWidget *>, 5> fieldEditorsByColumn;

    auto makeFieldLabel = [this](const QString& text) {
        auto *label = new QLabel(text, this);
        label->setObjectName(QStringLiteral("fieldLabel"));
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setMinimumHeight(22);
        return label;
    };
    auto addField = [layout, &makeFieldLabel, &fieldLabelsByColumn, &fieldEditorsByColumn](int row, int column, const QString& labelText, QWidget *editor, QLabel *&label) {
        label = makeFieldLabel(labelText);
        fieldLabelsByColumn.at(static_cast<size_t>(column)).append(label);
        fieldEditorsByColumn.at(static_cast<size_t>(column)).append(editor);
        editor->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        auto *cell = new QWidget();
        cell->setObjectName(QStringLiteral("temperatureConfigFieldRow"));
        cell->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        cell->setFixedHeight(kTemperatureControllerConfigRowHeight);
        auto *cellLayout = new QHBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(kTemperatureControllerSensorFieldSpacing);
        cellLayout->addWidget(label, 0, Qt::AlignLeft | Qt::AlignVCenter);
        cellLayout->addWidget(editor, 0, Qt::AlignLeft | Qt::AlignVCenter);
        layout->addWidget(cell, row, column * 2, Qt::AlignLeft | Qt::AlignVCenter);
    };
    auto makeIntegerEdit = [this](const QString& name, int min, int max, int width) {
        auto *edit = new QLineEdit(this);
        edit->setObjectName(name);
        edit->setFixedWidth(width);
        edit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        edit->setValidator(new QIntValidator(min, max, edit));
        return edit;
    };
    auto makeDecimalEdit = [this](const QString& name, double min, double max, int decimals, int width) {
        auto *edit = new QLineEdit(this);
        edit->setObjectName(name);
        edit->setFixedWidth(width);
        edit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto *validator = new QDoubleValidator(min, max, decimals, edit);
        validator->setNotation(QDoubleValidator::StandardNotation);
        validator->setLocale(QLocale::c());
        edit->setValidator(validator);
        edit->setText(formatTemperatureSensorDouble(0.0, decimals));
        return edit;
    };

    channel.ntc_r0_edit = makeIntegerEdit(QStringLiteral("temperatureNtcR0EditChannel%1").arg(index + 1),
                                          0,
                                          9000000,
                                          kTemperatureControllerSensorInputWidth);
    channel.ntc_r0_edit->setText(QStringLiteral("0"));
    addField(0, 0, QStringLiteral("NTC R0(Ohm)"), channel.ntc_r0_edit, channel.ntc_r0_label_text);

    channel.ntc_b_edit = makeDecimalEdit(QStringLiteral("temperatureNtcBEditChannel%1").arg(index + 1),
                                         1000.0,
                                         50000.0,
                                         2,
                                         kTemperatureControllerSensorInputWidth);
    channel.ntc_b_edit->setText(QStringLiteral("1000.00"));
    addField(1, 0, QStringLiteral("NTC B"), channel.ntc_b_edit, channel.ntc_b_label_text);

    channel.pt_r0_edit = makeDecimalEdit(QStringLiteral("temperaturePtR0EditChannel%1").arg(index + 1),
                                         0.0,
                                         10000.0,
                                         3,
                                         kTemperatureControllerSensorInputWidth);
    channel.pt_r0_edit->setText(QStringLiteral("0.000"));
    addField(0, 1, QStringLiteral("PT R0(Ohm)"), channel.pt_r0_edit, channel.pt_r0_label_text);

    channel.pt_a_edit = makeDecimalEdit(QStringLiteral("temperaturePtAEditChannel%1").arg(index + 1),
                                        -9.0,
                                        9.0,
                                        6,
                                        kTemperatureControllerPtCoefficientInputWidth);
    addField(0, 2, QStringLiteral("PT A(E-3)"), channel.pt_a_edit, channel.pt_a_label_text);
    channel.pt_b_edit = makeDecimalEdit(QStringLiteral("temperaturePtBEditChannel%1").arg(index + 1),
                                        -90.0,
                                        90.0,
                                        6,
                                        kTemperatureControllerPtCoefficientInputWidth);
    addField(0, 3, QStringLiteral("PT B(E-7)"), channel.pt_b_edit, channel.pt_b_label_text);
    channel.pt_c_edit = makeDecimalEdit(QStringLiteral("temperaturePtCEditChannel%1").arg(index + 1),
                                        -9.0,
                                        9.0,
                                        6,
                                        kTemperatureControllerPtCoefficientInputWidth);
    addField(0, 4, QStringLiteral("PT C(E-12)"), channel.pt_c_edit, channel.pt_c_label_text);

    for (int i = 0; i < 8; ++i)
    {
        auto *edit = new QLineEdit(this);
        edit->setObjectName(QStringLiteral("temperaturePolynomialA%1EditChannel%2").arg(i).arg(index + 1));
        edit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        edit->setText(QStringLiteral("0E+0"));
        channel.polynomial_edits[static_cast<size_t>(i)] = edit;
        if (i < 4)
        {
            edit->setFixedWidth(kTemperatureControllerPolynomialInputWidth);
            addField(1,
                     1 + i,
                     QStringLiteral("A%1").arg(i),
                     edit,
                     channel.polynomial_label_text[static_cast<size_t>(i)]);
            continue;
        }

        QLabel *&label = channel.polynomial_label_text[static_cast<size_t>(i)];
        label = makeFieldLabel(QStringLiteral("A%1").arg(i));
        edit->setMinimumWidth(kTemperatureControllerPolynomialInputWidth);
        edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto *field = new QWidget(sensorTopPolynomialFields);
        field->setObjectName(QStringLiteral("temperatureSensorTopPolynomialA%1FieldChannel%2")
                                 .arg(i)
                                 .arg(index + 1));
        field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        edit->ensurePolished();
        field->setFixedHeight(std::max(kTemperatureControllerConfigRowHeight,
                                       edit->sizeHint().height()));
        auto *fieldLayout = new QHBoxLayout(field);
        fieldLayout->setContentsMargins(2, 0, 2, 0);
        fieldLayout->setSpacing(4);
        fieldLayout->addWidget(label, 0, Qt::AlignLeft | Qt::AlignVCenter);
        fieldLayout->addWidget(edit, 1, Qt::AlignVCenter);
        if (sensorTopPolynomialLayout)
        {
            sensorTopPolynomialLayout->addWidget(field, 1, Qt::AlignVCenter);
        }
    }

    for (size_t column = 0; column < fieldLabelsByColumn.size(); ++column)
    {
        const QList<QLabel *>& labels = fieldLabelsByColumn[column];
        int columnLabelWidth = 0;
        for (QLabel *label : labels)
        {
            label->ensurePolished();
            columnLabelWidth = std::max(
                columnLabelWidth,
                label->fontMetrics().boundingRect(label->text()).width());
        }
        for (QLabel *label : labels)
        {
            label->setFixedWidth(columnLabelWidth + kTemperatureControllerSensorLabelPadding);
        }

        const QList<QWidget *>& editors = fieldEditorsByColumn[column];
        int columnEditorWidth = 0;
        for (const QWidget *editor : editors)
        {
            columnEditorWidth = std::max(columnEditorWidth, editor->width());
        }
        for (QWidget *editor : editors)
        {
            editor->setFixedWidth(columnEditorWidth);
        }
        for (qsizetype fieldIndex = 0; fieldIndex < editors.size(); ++fieldIndex)
        {
            QWidget *fieldCell = editors.at(fieldIndex)->parentWidget();
            const int fieldCellWidth = labels.at(fieldIndex)->width() +
                kTemperatureControllerSensorFieldSpacing + columnEditorWidth;
            fieldCell->setFixedWidth(fieldCellWidth);
        }
    }

    const int channelIndex = index;
    auto emitConfig = [this, channelIndex]() { emitSensorConfigRequest(channelIndex); };
    for (QLineEdit *edit : {channel.ntc_r0_edit,
                            channel.ntc_b_edit,
                            channel.pt_r0_edit,
                            channel.pt_a_edit,
                            channel.pt_b_edit,
                            channel.pt_c_edit})
    {
        connect(edit, &QLineEdit::editingFinished, this, emitConfig);
    }
    for (QLineEdit *edit : channel.polynomial_edits)
    {
        connect(edit, &QLineEdit::editingFinished, this, emitConfig);
    }
    return page;
}

QWidget *TemperatureControllerPanel::createCommonSettingsPage()
{
    QWidget *page = new QWidget(channel_stack_);
    page->setObjectName(QStringLiteral("temperatureCommonSettingsPage"));
    page->setMinimumHeight(kTemperatureControllerCommonStackHeight);
    auto *layout = new QGridLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(0);
    layout->setVerticalSpacing(8);
    layout->setAlignment(Qt::AlignTop);
    layout->setColumnMinimumWidth(1, 0);
    layout->setColumnStretch(3, 1);
    layout->setRowMinimumHeight(0, kTemperatureControllerConfigRowHeight);
    layout->setRowMinimumHeight(1, kTemperatureControllerConfigRowHeight);
    layout->setRowMinimumHeight(2, kTemperatureControllerConfigRowHeight);

    auto makeFieldLabel = [this](const QString& text) {
        auto *label = new QLabel(text, this);
        label->setObjectName(QStringLiteral("fieldLabel"));
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setMinimumHeight(22);
        return label;
    };

    auto makeField = [page, &makeFieldLabel](const QString& labelText, QWidget *editor, QLabel *&label) {
        label = makeFieldLabel(labelText);
        editor->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        auto *cell = new QWidget(page);
        cell->setObjectName(QStringLiteral("temperatureCommonFieldRow"));
        cell->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        cell->setFixedHeight(kTemperatureControllerConfigRowHeight);
        auto *cellLayout = new QHBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(8);
        cellLayout->addWidget(label, 0, Qt::AlignLeft | Qt::AlignVCenter);
        cellLayout->addWidget(editor, 0, Qt::AlignLeft | Qt::AlignVCenter);
        return cell;
    };

    auto alignLabelColumn = [](std::initializer_list<QLabel *> labels) {
        int width = 0;
        for (QLabel *label : labels)
        {
            if (label)
            {
                width = std::max(width, label->sizeHint().width());
            }
        }
        for (QLabel *label : labels)
        {
            if (label)
            {
                label->setFixedWidth(width);
            }
        }
    };

    common_.address_spin = new QSpinBox(page);
    common_.address_spin->setObjectName(QStringLiteral("temperatureDeviceAddressSpin"));
    common_.address_spin->setRange(1, 247);
    common_.address_spin->setFixedWidth(kTemperatureControllerInputWidth);
    QWidget *addressField =
        makeField(QStringLiteral("设置温控器485站号"), common_.address_spin, common_.address_label_text);

    common_.rs485_baud_combo = new QComboBox(page);
    common_.rs485_baud_combo->setObjectName(QStringLiteral("temperatureRs485BaudCombo"));
    common_.rs485_baud_combo->setFixedWidth(kTemperatureControllerRs485BaudWidth);
    common_.rs485_baud_combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    const QList<int> baudRates = {4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800};
    for (int i = 0; i < baudRates.size(); ++i)
    {
        common_.rs485_baud_combo->addItem(QString::number(baudRates.at(i)), i);
    }
    QWidget *rs485BaudField =
        makeField(QStringLiteral("设置485串口波特率"), common_.rs485_baud_combo, common_.rs485_baud_label_text);

    common_.overtemp_output_combo = new SingleLevelPopupComboBox(this);
    common_.overtemp_output_combo->setObjectName(QStringLiteral("temperatureOvertempOutputModeCombo"));
    common_.overtemp_output_combo->setFixedWidth(kTemperatureControllerWideInputWidth);
    common_.overtemp_output_combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    common_.overtemp_output_combo->addItem(QStringLiteral("继续输出"), 0);
    common_.overtemp_output_combo->addItem(QStringLiteral("关闭输出"), 1);
    QWidget *overtempOutputField =
        makeField(QStringLiteral("过温输出模式"), common_.overtemp_output_combo, common_.overtemp_output_label_text);
    common_.overtemp_output_label_text->setProperty("temperatureOvertempWarning", true);

    common_.internal_temperature_edit = new QLineEdit(page);
    common_.internal_temperature_edit->setObjectName(QStringLiteral("temperatureCommonInternalTemperatureEdit"));
    common_.internal_temperature_edit->setReadOnly(true);
    common_.internal_temperature_edit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    common_.internal_temperature_edit->setFixedWidth(kTemperatureControllerInputWidth);
    QWidget *internalTemperatureField =
        makeField(QStringLiteral("温控器自身温度(°C)"), common_.internal_temperature_edit, common_.internal_temperature_label_text);

    alignLabelColumn({common_.address_label_text, common_.overtemp_output_label_text});
    alignLabelColumn({common_.rs485_baud_label_text, common_.internal_temperature_label_text});

    layout->addWidget(addressField, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(rs485BaudField, 0, 2, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(overtempOutputField, 1, 0, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(internalTemperatureField, 1, 2, Qt::AlignLeft | Qt::AlignVCenter);

    common_.factory_reset_button = new QPushButton(QStringLiteral("恢复出厂设置"), page);
    common_.factory_reset_button->setObjectName(QStringLiteral("temperatureFactoryResetButton"));
    common_.factory_reset_button->setCursor(Qt::PointingHandCursor);
    common_.factory_reset_button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    common_.factory_reset_button->setFixedSize(142, 34);
    common_.factory_reset_button->setIconSize(QSize(18, 18));
    common_.factory_reset_button->setIcon(createLucideIcon(QStringLiteral("refresh-cw"),
                                                           appThemeColor(AppThemeColor::ToolbarRed, VaporView::isDarkThemeEnabled())));
    connect(common_.factory_reset_button, &QPushButton::clicked, this, [this]() {
        emit factoryResetRequested();
    });
    layout->addWidget(common_.factory_reset_button, 2, 0, Qt::AlignLeft | Qt::AlignVCenter);

    connect(common_.address_spin, &QSpinBox::editingFinished, this, [this]() {
        emit deviceAddressRequested(static_cast<quint16>(common_.address_spin->value()));
    });
    connect(common_.rs485_baud_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        emit rs485BaudRequested(static_cast<quint16>(common_.rs485_baud_combo->currentData().toUInt()));
    });

    connect(common_.overtemp_output_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        emit overtempOutputModeRequested(static_cast<quint16>(common_.overtemp_output_combo->currentData().toUInt()));
    });
    return page;
}

void TemperatureControllerPanel::selectChannel(int index)
{
    const int pageIndex = std::clamp(index, 0, 2);
    selected_config_page_index_ = pageIndex;
    if (pageIndex < 2)
    {
        selected_channel_index_ = pageIndex;
    }
    const int channelIndex = std::clamp(selected_channel_index_, 0, 1);
    if (channel_stack_)
    {
        updateChannelStackMinimumHeight();
        channel_stack_->setCurrentIndex(pageIndex);
        channel_stack_->updateGeometry();
    }
    if (channel_top_controls_stack_)
    {
        if (pageIndex < 2)
        {
            channel_top_controls_stack_->setCurrentIndex(channelIndex);
            if (channels_[channelIndex].config_sub_stack)
            {
                channels_[channelIndex].config_sub_stack->setCurrentIndex(selected_channel_sub_page_index_);
            }
        }
        const int subPageIndex = std::clamp(selected_channel_sub_page_index_, 0, 2);
        if (pageIndex < 2)
        {
            if (channels_[channelIndex].common_top_controls)
            {
                channels_[channelIndex].common_top_controls->setVisible(subPageIndex == 0);
            }
            if (channels_[channelIndex].sensor_model_field)
            {
                channels_[channelIndex].sensor_model_field->setVisible(subPageIndex == 2);
            }
        }
        const bool showTopControls = pageIndex < 2 && subPageIndex != 1;
        channel_top_controls_stack_->setVisible(showTopControls);
        refreshTopControlsLayout();
    }
    auto updateButton = [pageIndex](QPushButton *button, int buttonIndex) {
        if (!button)
        {
            return;
        }
        const QSignalBlocker blocker(button);
        button->setChecked(pageIndex == buttonIndex);
        button->style()->unpolish(button);
        button->style()->polish(button);
        button->update();
    };
    updateButton(channel_button_1_, 0);
    updateButton(channel_button_2_, 1);
    updateButton(common_settings_button_, 2);

    if (temperature_plot_ && pageIndex < 2)
    {
        temperature_plot_->setChannelIndex(channelIndex);
        temperature_plot_->setTargetTemperature(target_temperature_by_channel_[channelIndex]);
        temperature_plot_->setSamples(measured_temperature_history_[channelIndex]);
    }
}

void TemperatureControllerPanel::updateChannelStackMinimumHeight()
{
    if (!channel_stack_)
    {
        return;
    }

    int maximumPageHeight = kTemperatureControllerChannelStackHeight;
    for (int index = 0; index < static_cast<int>(channels_.size()); ++index)
    {
        ChannelWidgets& channel = channels_[index];
        QWidget *channelPage = channel_stack_->widget(index);
        if (!channelPage)
        {
            continue;
        }

        if (channel.config_sub_stack)
        {
            channel.config_sub_stack->setFixedHeight(kTemperatureControllerChannelConfigSubStackHeight);
        }

        int subPageRowHeight = kTemperatureControllerConfigRowHeight;
        QWidget *subPageRow = channel.sensor_config_top_bar
            ? channel.sensor_config_top_bar->parentWidget()
            : nullptr;
        if (subPageRow)
        {
            subPageRowHeight = std::max(subPageRowHeight,
                                        channel.sensor_config_top_bar->sizeHint().height());
            const QWidget *sensorTopPolynomialFields = subPageRow->findChild<QWidget *>(
                QStringLiteral("temperatureSensorTopPolynomialFieldsChannel%1").arg(index + 1),
                Qt::FindDirectChildrenOnly);
            if (sensorTopPolynomialFields)
            {
                subPageRowHeight = std::max(subPageRowHeight,
                                            sensorTopPolynomialFields->sizeHint().height());
            }
            subPageRow->setFixedHeight(subPageRowHeight);
        }

        QMargins channelMargins;
        int channelSpacing = 0;
        if (QLayout *channelLayout = channelPage->layout())
        {
            channelLayout->invalidate();
            channelLayout->activate();
            channelMargins = channelLayout->contentsMargins();
            channelSpacing = channelLayout->spacing();
        }
        const int channelPageHeight = channelMargins.top() + kTemperatureControllerChannelConfigSubStackHeight +
            channelSpacing + subPageRowHeight + channelMargins.bottom();
        channelPage->setFixedHeight(channelPageHeight);
        maximumPageHeight = std::max(maximumPageHeight, channelPageHeight);
    }

    if (QWidget *commonPage = channel_stack_->widget(2))
    {
        if (QLayout *commonLayout = commonPage->layout())
        {
            commonLayout->invalidate();
            commonLayout->activate();
        }
        const int commonHeight = std::max(kTemperatureControllerCommonStackHeight,
                                          commonPage->sizeHint().height());
        commonPage->setFixedHeight(commonHeight);
        maximumPageHeight = std::max(maximumPageHeight, commonHeight);
    }

    channel_stack_->setFixedHeight(maximumPageHeight);
    channel_stack_->updateGeometry();
}

void TemperatureControllerPanel::selectChannelSubPage(int channelIndex, int subPageIndex)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels_.size()))
    {
        return;
    }
    const int pageIndex = std::clamp(subPageIndex, 0, 2);
    selected_channel_sub_page_index_ = pageIndex;
    auto updateButton = [pageIndex](QPushButton *button, int index) {
        if (!button)
        {
            return;
        }
        const QSignalBlocker blocker(button);
        button->setChecked(pageIndex == index);
        button->style()->unpolish(button);
        button->style()->polish(button);
        button->update();
    };
    for (int index = 0; index < static_cast<int>(channels_.size()); ++index)
    {
        ChannelWidgets& channel = channels_[index];
        if (channel.config_sub_stack)
        {
            channel.config_sub_stack->setCurrentIndex(pageIndex);
        }
        QWidget *sensorTopPolynomialFields = nullptr;
        if (channel.sensor_config_top_bar && channel.sensor_config_top_bar->parentWidget())
        {
            sensorTopPolynomialFields = channel.sensor_config_top_bar->parentWidget()->findChild<QWidget *>(
                QStringLiteral("temperatureSensorTopPolynomialFieldsChannel%1").arg(index + 1),
                Qt::FindDirectChildrenOnly);
        }
        if (sensorTopPolynomialFields)
        {
            sensorTopPolynomialFields->setVisible(pageIndex == 2);
            if (pageIndex == 2)
            {
                QTimer::singleShot(0, this, [this, index]() {
                    alignSensorTopPolynomialFields(index);
                });
            }
        }
        updateButton(channel.common_params_button, 0);
        updateButton(channel.advanced_params_button, 1);
        updateButton(channel.sensor_config_button, 2);
    }

    const int selectedChannelIndex = std::clamp(selected_channel_index_, 0, 1);
    ChannelWidgets& selectedChannel = channels_[selectedChannelIndex];
    if (channel_top_controls_stack_ && selected_config_page_index_ < 2)
    {
        if (selectedChannel.common_top_controls)
        {
            selectedChannel.common_top_controls->setVisible(pageIndex == 0);
        }
        if (selectedChannel.sensor_model_field)
        {
            selectedChannel.sensor_model_field->setVisible(pageIndex == 2);
        }
        channel_top_controls_stack_->setVisible(pageIndex != 1);
        if (pageIndex != 1)
        {
            channel_top_controls_stack_->setCurrentIndex(selectedChannelIndex);
            refreshTopControlsLayout();
        }
    }
}

void TemperatureControllerPanel::emitSensorConfigRequest(int index)
{
    if (index < 0 || index >= static_cast<int>(channels_.size()))
    {
        return;
    }
    const ChannelWidgets& channel = channels_[index];
    if (!channel.sensor_model_group || !channel.ntc_r0_edit || !channel.ntc_b_edit ||
        !channel.pt_r0_edit || !channel.pt_a_edit || !channel.pt_b_edit || !channel.pt_c_edit)
    {
        return;
    }

    auto failField = [this](QLineEdit *edit, const QString& label, const QString& minText, const QString& maxText) {
        if (edit)
        {
            edit->setFocus();
            edit->selectAll();
        }
        setCommandStatus(is_english_
            ? QStringLiteral("%1 must be between %2 and %3.").arg(label, minText, maxText)
            : QStringLiteral("%1 取值范围应为 %2 到 %3。").arg(label, minText, maxText),
            true);
    };
    auto parseIntegerField = [failField](QLineEdit *edit,
                                         const QString& label,
                                         qint64 min,
                                         qint64 max,
                                         qint64& output) {
        bool ok = false;
        const qint64 value = QLocale::c().toLongLong(edit ? edit->text().trimmed() : QString(), &ok);
        if (!ok || value < min || value > max)
        {
            failField(edit, label, QString::number(min), QString::number(max));
            return false;
        }
        output = value;
        if (edit)
        {
            const QSignalBlocker blocker(edit);
            edit->setText(QString::number(value));
        }
        return true;
    };
    auto parseDecimalField = [failField](QLineEdit *edit,
                                         const QString& label,
                                         double min,
                                         double max,
                                         int decimals,
                                         double scale,
                                         qint64& output) {
        bool ok = false;
        const double value = QLocale::c().toDouble(edit ? edit->text().trimmed() : QString(), &ok);
        if (!ok || !std::isfinite(value) || value < min || value > max)
        {
            failField(edit,
                      label,
                      formatTemperatureSensorDouble(min, decimals),
                      formatTemperatureSensorDouble(max, decimals));
            return false;
        }
        output = qRound64(value * scale);
        if (edit)
        {
            const QSignalBlocker blocker(edit);
            edit->setText(formatTemperatureSensorDecimal(output, scale, decimals));
        }
        return true;
    };

    qint64 ntcR0 = 0;
    qint64 ntcB = 0;
    qint64 ptR0 = 0;
    qint64 ptA = 0;
    qint64 ptB = 0;
    qint64 ptC = 0;
    if (!parseIntegerField(channel.ntc_r0_edit, QStringLiteral("NTC R0(Ohm)"), 0, 9000000, ntcR0) ||
        !parseDecimalField(channel.ntc_b_edit, QStringLiteral("NTC B"), 1000.0, 50000.0, 2, 100.0, ntcB) ||
        !parseDecimalField(channel.pt_r0_edit, QStringLiteral("PT R0(Ohm)"), 0.0, 10000.0, 3, 1000.0, ptR0) ||
        !parseDecimalField(channel.pt_a_edit, QStringLiteral("PT A(E-3)"), -9.0, 9.0, 6, 1000000.0, ptA) ||
        !parseDecimalField(channel.pt_b_edit, QStringLiteral("PT B(E-7)"), -90.0, 90.0, 6, 100000.0, ptB) ||
        !parseDecimalField(channel.pt_c_edit, QStringLiteral("PT C(E-12)"), -9.0, 9.0, 6, 10000.0, ptC))
    {
        return;
    }

    VaporView::TemperatureControllerCommand command;
    command.channel = static_cast<quint8>(index + 1);
    command.sensor_model = static_cast<quint16>(std::max(0, channel.sensor_model_group->checkedId()));
    command.ntc_r0 = static_cast<quint32>(ntcR0);
    command.ntc_b = static_cast<quint32>(ntcB);
    command.pt_r0 = static_cast<quint32>(ptR0);
    command.pt_a = static_cast<qint32>(ptA);
    command.pt_b = static_cast<qint32>(ptB);
    command.pt_c = static_cast<qint32>(ptC);
    for (size_t i = 0; i < command.polynomial_mantissas.size(); ++i)
    {
        QLineEdit *edit = channel.polynomial_edits[i];
        qint64 mantissa = 0;
        qint16 exponent = 0;
        if (edit && !parseTemperaturePolynomial(edit->text(), mantissa, exponent))
        {
            edit->setFocus();
            setCommandStatus(is_english_
                ? QStringLiteral("Invalid polynomial coefficient. Use scientific notation such as 0E+0.")
                : QStringLiteral("多项式系数格式无效，请使用类似 0E+0 的科学计数法。"),
                true);
            return;
        }
        command.polynomial_mantissas[i] = mantissa;
        command.polynomial_exponents[i] = exponent;
        if (edit)
        {
            const QSignalBlocker blocker(edit);
            edit->setText(formatTemperaturePolynomial(mantissa, exponent));
        }
    }
    emit sensorConfigRequested(command);
}

void TemperatureControllerPanel::refreshTopControlsLayout()
{
    if (!channel_top_controls_stack_)
    {
        return;
    }
    if (QWidget *currentTopControls = channel_top_controls_stack_->currentWidget())
    {
        if (QLayout *currentLayout = currentTopControls->layout())
        {
            currentLayout->invalidate();
            currentLayout->activate();
        }
        currentTopControls->updateGeometry();
    }
    channel_top_controls_stack_->updateGeometry();
    if (QWidget *parent = channel_top_controls_stack_->parentWidget())
    {
        if (QLayout *parentLayout = parent->layout())
        {
            parentLayout->invalidate();
            parentLayout->activate();
        }
    }
    const int channelIndex = std::clamp(selected_channel_index_, 0, 1);
    alignChannelTopControlFields(channelIndex);
    alignCommonSettingsColumns(channelIndex);
}

void TemperatureControllerPanel::updateRate(double hz)
{
    if (rate_label_)
    {
        rate_label_->setText((hz > 0.0 && std::isfinite(hz))
            ? fixedDecimalWithUnit(hz, 1, 6, QStringLiteral("Hz"))
            : QStringLiteral("%1 Hz").arg(fixedTextField(QStringLiteral("--"), 6)));
    }
}

void TemperatureControllerPanel::setEnglish(bool english)
{
    is_english_ = english;
    if (temperature_plot_)
    {
        temperature_plot_->setEnglish(english);
    }
    updateChannelTexts();
}

int TemperatureControllerPanel::channelIndex(quint8 channel) const
{
    if (channel == 0 || channel > channels_.size())
    {
        return -1;
    }
    return static_cast<int>(channel - 1);
}

void TemperatureControllerPanel::markCommandPending(VaporView::CommandId command, const VaporView::TemperatureControllerCommand& payload)
{
    const int index = channelIndex(payload.channel == 0 ? 1 : payload.channel);
    PendingChannelEdits *pending = index >= 0 ? &pending_channel_edits_[index] : nullptr;
    switch (command)
    {
    case VaporView::CommandId::SetTemperatureTarget:
        if (pending)
        {
            pending->target_temperature = true;
            pending->target_temperature_c = payload.target_temperature_c;
        }
        break;
    case VaporView::CommandId::SetTemperatureOutputMode:
        if (pending)
        {
            pending->output_mode = true;
            pending->output_mode_value = static_cast<int>(payload.output_mode);
        }
        break;
    case VaporView::CommandId::SetTemperatureMaxOutputPercent:
        if (pending)
        {
            pending->max_output_percent = true;
            pending->max_output_percent_value = static_cast<int>(payload.max_output_percent);
        }
        break;
    case VaporView::CommandId::SetTemperaturePid:
        if (pending)
        {
            pending->pid = true;
            pending->kp = static_cast<int>(payload.kp);
            pending->ki = static_cast<int>(payload.ki);
            pending->kd = static_cast<int>(payload.kd);
        }
        break;
    case VaporView::CommandId::SetTemperatureAutoPid:
        if (pending)
        {
            pending->auto_pid = true;
            pending->auto_pid_mode = static_cast<int>(payload.auto_pid_mode);
        }
        break;
    case VaporView::CommandId::SetTemperatureOvertempUpper:
        if (pending)
        {
            pending->overtemp_upper = true;
            pending->overtemp_upper_c = payload.overtemp_upper_c;
        }
        break;
    case VaporView::CommandId::SetTemperatureOvertempLower:
        if (pending)
        {
            pending->overtemp_lower = true;
            pending->overtemp_lower_c = payload.overtemp_lower_c;
        }
        break;
    case VaporView::CommandId::SetTemperatureSlope:
        if (pending)
        {
            pending->temperature_slope = true;
            pending->temperature_slope_c_per_s = payload.temperature_slope_c_per_s;
        }
        break;
    case VaporView::CommandId::SetTemperatureStartupDelay:
        if (pending)
        {
            pending->startup_delay = true;
            pending->startup_delay_s = static_cast<int>(payload.startup_delay_s);
        }
        break;
    case VaporView::CommandId::SetTemperatureSensorConfig:
        if (pending)
        {
            pending->sensor_config = true;
            pending->sensor_config_value = payload;
        }
        break;
    case VaporView::CommandId::SetTemperatureControllerMode:
        pending_controller_mode_ = true;
        pending_controller_mode_value_ = static_cast<int>(payload.controller_mode);
        break;
    case VaporView::CommandId::SetTemperatureDeviceAddress:
        pending_common_edits_.device_address = true;
        pending_common_edits_.device_address_value = static_cast<int>(payload.device_address);
        break;
    case VaporView::CommandId::SetTemperatureRs485Baud:
        pending_common_edits_.rs485_baud = true;
        pending_common_edits_.rs485_baud_index = static_cast<int>(payload.rs485_baud_index);
        break;
    case VaporView::CommandId::SetTemperatureOvertempOutputMode:
        pending_common_edits_.overtemp_output_mode = true;
        pending_common_edits_.overtemp_output_mode_value = static_cast<int>(payload.overtemp_output_mode);
        break;
    case VaporView::CommandId::RestoreTemperatureFactoryDefaults:
        pending_common_edits_ = PendingCommonEdits{};
        break;
    default:
        break;
    }
}

void TemperatureControllerPanel::clearCommandPending(VaporView::CommandId command, quint8 channel)
{
    const int index = channelIndex(channel == 0 ? 1 : channel);
    PendingChannelEdits *pending = index >= 0 ? &pending_channel_edits_[index] : nullptr;
    switch (command)
    {
    case VaporView::CommandId::SetTemperatureTarget:
        if (pending) pending->target_temperature = false;
        break;
    case VaporView::CommandId::SetTemperatureOutputMode:
        if (pending) pending->output_mode = false;
        break;
    case VaporView::CommandId::SetTemperatureMaxOutputPercent:
        if (pending) pending->max_output_percent = false;
        break;
    case VaporView::CommandId::SetTemperaturePid:
        if (pending) pending->pid = false;
        break;
    case VaporView::CommandId::SetTemperatureAutoPid:
        if (pending) pending->auto_pid = false;
        break;
    case VaporView::CommandId::SetTemperatureOvertempUpper:
        if (pending) pending->overtemp_upper = false;
        break;
    case VaporView::CommandId::SetTemperatureOvertempLower:
        if (pending) pending->overtemp_lower = false;
        break;
    case VaporView::CommandId::SetTemperatureSlope:
        if (pending) pending->temperature_slope = false;
        break;
    case VaporView::CommandId::SetTemperatureStartupDelay:
        if (pending) pending->startup_delay = false;
        break;
    case VaporView::CommandId::SetTemperatureSensorConfig:
        if (pending) pending->sensor_config = false;
        break;
    case VaporView::CommandId::SetTemperatureControllerMode:
        pending_controller_mode_ = false;
        break;
    case VaporView::CommandId::SetTemperatureDeviceAddress:
        pending_common_edits_.device_address = false;
        break;
    case VaporView::CommandId::SetTemperatureRs485Baud:
        pending_common_edits_.rs485_baud = false;
        break;
    case VaporView::CommandId::SetTemperatureOvertempOutputMode:
        pending_common_edits_.overtemp_output_mode = false;
        break;
    case VaporView::CommandId::RestoreTemperatureFactoryDefaults:
        pending_common_edits_ = PendingCommonEdits{};
        break;
    default:
        break;
    }
}

void TemperatureControllerPanel::updateChannelTexts()
{
    if (internal_temperature_lbl_) internal_temperature_lbl_->setText(is_english_ ? QStringLiteral("Internal:") : QStringLiteral("自身温度:"));
    if (error_code_lbl_) error_code_lbl_->setText(is_english_ ? QStringLiteral("Error:") : QStringLiteral("错误码:"));
    if (rate_title_lbl_) rate_title_lbl_->setText(is_english_ ? QStringLiteral("Polling rate:") : QStringLiteral("轮询频率:"));
    if (controller_mode_lbl_) controller_mode_lbl_->setText(is_english_ ? QStringLiteral("Mode:") : QStringLiteral("温控器模式:"));
    refreshFixedTextLabelWidth(internal_temperature_lbl_);
    refreshFixedTextLabelWidth(error_code_lbl_);
    refreshFixedTextLabelWidth(rate_title_lbl_);
    refreshFixedTextLabelWidth(controller_mode_lbl_);
    if (controller_mode_combo_)
    {
        const QSignalBlocker blocker(controller_mode_combo_);
        controller_mode_combo_->setItemText(0, is_english_ ? QStringLiteral("Independent") : QStringLiteral("独立控制"));
        controller_mode_combo_->setItemText(1, is_english_ ? QStringLiteral("CH1 target follows CH2 temp") : QStringLiteral("通道1温差控制"));
        controller_mode_combo_->setItemText(2, is_english_ ? QStringLiteral("CH2 output follows CH1") : QStringLiteral("通道2跟随输出"));
        controller_mode_combo_->setItemText(3, is_english_ ? QStringLiteral("Combined") : QStringLiteral("温差控制+跟随输出"));
        controller_mode_combo_->setToolTip(is_english_
            ? QStringLiteral("RD105 CONTMODE. Modes 2 and 3 require a resistor temperature sensor on channel 2.")
            : QStringLiteral("RD105 CONTMODE。使用模式2和3时，通道2传感器接口应接入电阻温度传感器。"));
    }
    if (channel_button_1_) channel_button_1_->setText(is_english_ ? QStringLiteral("Channel 1") : QStringLiteral("通道1"));
    if (channel_button_2_) channel_button_2_->setText(is_english_ ? QStringLiteral("Channel 2") : QStringLiteral("通道2"));
    if (common_settings_button_) common_settings_button_->setText(is_english_ ? QStringLiteral("Common") : QStringLiteral("通用设置"));
    if (common_.address_label_text) common_.address_label_text->setText(is_english_ ? QStringLiteral("RS485 address") : QStringLiteral("设置温控器485站号"));
    if (common_.rs485_baud_label_text) common_.rs485_baud_label_text->setText(is_english_ ? QStringLiteral("RS485 baud") : QStringLiteral("设置485串口波特率"));
    if (common_.overtemp_output_label_text) common_.overtemp_output_label_text->setText(is_english_ ? QStringLiteral("Over-temp output") : QStringLiteral("过温输出模式"));
    if (common_.internal_temperature_label_text) common_.internal_temperature_label_text->setText(is_english_ ? QStringLiteral("Internal temp (°C)") : QStringLiteral("温控器自身温度(°C)"));
    if (common_.factory_reset_button)
    {
        common_.factory_reset_button->setText(is_english_ ? QStringLiteral("Factory Reset") : QStringLiteral("恢复出厂设置"));
        common_.factory_reset_button->setIcon(createLucideIcon(QStringLiteral("refresh-cw"),
                                                               appThemeColor(AppThemeColor::ToolbarRed, VaporView::isDarkThemeEnabled())));
    }
    if (common_.overtemp_output_combo)
    {
        const QSignalBlocker blocker(common_.overtemp_output_combo);
        common_.overtemp_output_combo->setItemText(0, is_english_ ? QStringLiteral("Continue output") : QStringLiteral("继续输出"));
        common_.overtemp_output_combo->setItemText(1, is_english_ ? QStringLiteral("Disable output") : QStringLiteral("关闭输出"));
    }
    for (ChannelWidgets& channel : channels_)
    {
        auto fitSubTabButtonWidth = [](QPushButton *button) {
            if (!button)
            {
                return;
            }
            button->ensurePolished();
            button->setFixedWidth(std::max(88, button->fontMetrics().horizontalAdvance(button->text()) + 40));
        };
        if (channel.common_params_button)
        {
            channel.common_params_button->setText(is_english_ ? QStringLiteral("Common") : QStringLiteral("常用参数"));
            fitSubTabButtonWidth(channel.common_params_button);
        }
        if (channel.advanced_params_button)
        {
            channel.advanced_params_button->setText(is_english_ ? QStringLiteral("Advanced") : QStringLiteral("专业参数"));
            fitSubTabButtonWidth(channel.advanced_params_button);
        }
        if (channel.sensor_config_button)
        {
            channel.sensor_config_button->setText(is_english_ ? QStringLiteral("Sensor Config") : QStringLiteral("传感器配置"));
            fitSubTabButtonWidth(channel.sensor_config_button);
        }
        if (channel.sensor_config_top_bar)
        {
            channel.sensor_config_top_bar->setMinimumWidth(0);
            if (QLayout *layout = channel.sensor_config_top_bar->layout())
            {
                layout->invalidate();
                layout->activate();
            }
            channel.sensor_config_top_bar->setMinimumWidth(channel.sensor_config_top_bar->sizeHint().width());
        }
        if (channel.target_label_text) channel.target_label_text->setText(is_english_ ? QStringLiteral("Target Temp (°C)") : QStringLiteral("目标温度(°C)"));
        if (channel.enable_label_text) channel.enable_label_text->setText(is_english_ ? QStringLiteral("Output Enable") : QStringLiteral("输出使能"));
        if (channel.mode_label_text) channel.mode_label_text->setText(is_english_ ? QStringLiteral("Output Mode") : QStringLiteral("输出模式"));
        if (channel.max_output_label_text)
        {
            channel.max_output_label_text->setText(is_english_ ? QStringLiteral("Max Output Voltage (%)") : QStringLiteral("最大输出电压百分比(%)"));
            setDangerTextPalette(channel.max_output_label_text);
        }
        setDangerTextPalette(channel.max_output_spin);
        if (channel.auto_pid_label_text) channel.auto_pid_label_text->setText(is_english_ ? QStringLiteral("Auto PID") : QStringLiteral("自动 PID"));
        if (channel.overtemp_upper_label_text)
        {
            channel.overtemp_upper_label_text->setText(is_english_ ? QStringLiteral("High Temp Alarm (°C)") : QStringLiteral("高温报警值(°C)"));
            setDangerTextPalette(channel.overtemp_upper_label_text);
        }
        if (channel.overtemp_lower_label_text)
        {
            channel.overtemp_lower_label_text->setText(is_english_ ? QStringLiteral("Low Temp Alarm (°C)") : QStringLiteral("低温报警值(°C)"));
            setDangerTextPalette(channel.overtemp_lower_label_text);
        }
        if (channel.temperature_slope_label_text) channel.temperature_slope_label_text->setText(is_english_ ? QStringLiteral("Temperature Rate (°C/s)") : QStringLiteral("温度变化速率(°C/s)"));
        if (channel.startup_delay_label_text) channel.startup_delay_label_text->setText(is_english_ ? QStringLiteral("Startup Output Delay (s)") : QStringLiteral("开机输出延时(s)"));
        if (channel.sensor_resistance_label_text) channel.sensor_resistance_label_text->setText(is_english_ ? QStringLiteral("Sensor Resistance (Ω)") : QStringLiteral("传感器电阻(Ω)"));
        if (channel.sensor_model_label_text) channel.sensor_model_label_text->setText(is_english_ ? QStringLiteral("Model") : QStringLiteral("模型"));
        if (channel.ntc_r0_label_text) channel.ntc_r0_label_text->setText(QStringLiteral("NTC R0(Ohm)"));
        if (channel.ntc_b_label_text) channel.ntc_b_label_text->setText(QStringLiteral("NTC B"));
        if (channel.pt_r0_label_text) channel.pt_r0_label_text->setText(QStringLiteral("PT R0(Ohm)"));
        if (channel.pt_a_label_text) channel.pt_a_label_text->setText(QStringLiteral("PT A(E-3)"));
        if (channel.pt_b_label_text) channel.pt_b_label_text->setText(QStringLiteral("PT B(E-7)"));
        if (channel.pt_c_label_text) channel.pt_c_label_text->setText(QStringLiteral("PT C(E-12)"));
        for (size_t i = 0; i < channel.polynomial_label_text.size(); ++i)
        {
            if (channel.polynomial_label_text[i]) channel.polynomial_label_text[i]->setText(QStringLiteral("A%1").arg(i));
        }
        if (channel.enable_switch)
        {
            auto *enableSwitch = static_cast<TemperatureOverviewSwitchButton *>(channel.enable_switch);
            enableSwitch->setEnglish(is_english_);
        }
        if (channel.mode_combo)
        {
            const QSignalBlocker blocker(channel.mode_combo);
            channel.mode_combo->setItemText(0, is_english_ ? QStringLiteral("Cool + Heat") : QStringLiteral("制冷和加热"));
            channel.mode_combo->setItemText(1, is_english_ ? QStringLiteral("Cool") : QStringLiteral("制冷"));
            channel.mode_combo->setItemText(2, is_english_ ? QStringLiteral("Heat") : QStringLiteral("加热"));
            channel.mode_combo->setItemText(3, is_english_ ? QStringLiteral("Off") : QStringLiteral("关闭"));
        }
        if (channel.auto_pid_combo)
        {
            const QSignalBlocker blocker(channel.auto_pid_combo);
            channel.auto_pid_combo->setItemText(0, is_english_ ? QStringLiteral("Off") : QStringLiteral("关闭"));
            channel.auto_pid_combo->setItemText(1, is_english_ ? QStringLiteral("PID auto-tune") : QStringLiteral("PID自整定"));
            channel.auto_pid_combo->setItemText(2, is_english_ ? QStringLiteral("Realtime optimize (reserved)") : QStringLiteral("实时优化(预留)"));
            channel.auto_pid_combo->setToolTip(is_english_
                ? QStringLiteral("RD105 AUTOPID: off, PID auto-tune, or reserved realtime optimization.")
                : QStringLiteral("RD105 AUTOPID：关闭、PID自整定，或预留的实时优化。"));
        }
        if (channel.sensor_model_group)
        {
            const std::array<QString, 4> labels = {
                QStringLiteral("B-Value"),
                QStringLiteral("PT"),
                QStringLiteral("S-H"),
                QStringLiteral("MF501"),
            };
            const QString tooltip = is_english_
                ? QStringLiteral("RD105 POLYOMIAL register: B-value, PT, Steinhart-Hart, or MF501 model.")
                : QStringLiteral("RD105 POLYOMIAL 寄存器：B 值、PT、Steinhart-Hart 或 MF501 模型。");
            if (channel.sensor_model_selector)
            {
                channel.sensor_model_selector->setToolTip(tooltip);
            }
            for (int i = 0; i < static_cast<int>(labels.size()); ++i)
            {
                if (auto *radio = channel.sensor_model_radios[static_cast<size_t>(i)])
                {
                    radio->setText(labels[static_cast<size_t>(i)]);
                    radio->setToolTip(tooltip);
                }
            }
        }
    }
    if (status_label_ && status_label_->text().isEmpty()) setCommandStatus(is_english_ ? QStringLiteral("Writes are confirmed by reading back from RD105.") : QStringLiteral("写入命令会在天空端读回确认后才返回成功。"));
    QTimer::singleShot(0, this, [this]() {
        const int channelIndex = std::clamp(selected_channel_index_, 0, 1);
        alignChannelTopControlFields(channelIndex);
        alignCommonSettingsColumns(channelIndex);
    });
}

void TemperatureControllerPanel::updateChannelData(int index, const VaporView::TemperatureControllerChannelData& channelData, bool valid)
{
    ChannelWidgets& channel = channels_[index];
    PendingChannelEdits& pending = pending_channel_edits_[index];
    auto hasEditorFocus = [](QWidget *widget) {
        QWidget *focus = QApplication::focusWidget();
        return widget && (widget->hasFocus() || (focus && widget->isAncestorOf(focus)));
    };
    if (valid)
    {
        const QSignalBlocker targetBlocker(channel.target_spin);
        const QSignalBlocker modeBlocker(channel.mode_combo);
        const QSignalBlocker autoPidBlocker(channel.auto_pid_combo);
        const QSignalBlocker maxOutputBlocker(channel.max_output_spin);
        const QSignalBlocker kpBlocker(channel.kp_spin);
        const QSignalBlocker kiBlocker(channel.ki_spin);
        const QSignalBlocker kdBlocker(channel.kd_spin);
        const QSignalBlocker overtempUpperBlocker(channel.overtemp_upper_spin);
        const QSignalBlocker overtempLowerBlocker(channel.overtemp_lower_spin);
        const QSignalBlocker temperatureSlopeBlocker(channel.temperature_slope_spin);
        const QSignalBlocker startupDelayBlocker(channel.startup_delay_spin);

        if (pending.target_temperature &&
            std::isfinite(channelData.target_temperature_c) &&
            std::abs(channelData.target_temperature_c - pending.target_temperature_c) < 0.00001)
        {
            pending.target_temperature = false;
        }
        if (!pending.target_temperature && !hasEditorFocus(channel.target_spin))
        {
            channel.target_spin->setValue(channelData.target_temperature_c);
        }

        if (channel.enable_switch)
        {
            auto *enableSwitch = static_cast<TemperatureOverviewSwitchButton *>(channel.enable_switch);
            enableSwitch->setEnabled(valid);
            enableSwitch->setSwitchChecked(channelData.output_enabled,
                                           enableSwitch->switchChecked() != channelData.output_enabled);
        }

        if (pending.output_mode && channelData.output_mode == pending.output_mode_value)
        {
            pending.output_mode = false;
        }
        if (!pending.output_mode && !hasEditorFocus(channel.mode_combo))
        {
            const int modeIndex = channel.mode_combo->findData(channelData.output_mode);
            channel.mode_combo->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
        }

        if (pending.auto_pid && channelData.auto_pid_mode == pending.auto_pid_mode)
        {
            pending.auto_pid = false;
        }
        if (!pending.auto_pid && !hasEditorFocus(channel.auto_pid_combo))
        {
            const int autoPidIndex = channel.auto_pid_combo->findData(channelData.auto_pid_mode);
            channel.auto_pid_combo->setCurrentIndex(autoPidIndex >= 0 ? autoPidIndex : 0);
        }

        if (pending.overtemp_upper && std::isfinite(channelData.overtemp_upper_c) &&
            std::abs(channelData.overtemp_upper_c - pending.overtemp_upper_c) < 0.00001)
        {
            pending.overtemp_upper = false;
        }
        if (!pending.overtemp_upper && std::isfinite(channelData.overtemp_upper_c) &&
            !hasEditorFocus(channel.overtemp_upper_spin))
        {
            channel.overtemp_upper_spin->setValue(channelData.overtemp_upper_c);
        }
        if (pending.overtemp_lower && std::isfinite(channelData.overtemp_lower_c) &&
            std::abs(channelData.overtemp_lower_c - pending.overtemp_lower_c) < 0.00001)
        {
            pending.overtemp_lower = false;
        }
        if (!pending.overtemp_lower && std::isfinite(channelData.overtemp_lower_c) &&
            !hasEditorFocus(channel.overtemp_lower_spin))
        {
            channel.overtemp_lower_spin->setValue(channelData.overtemp_lower_c);
        }
        if (pending.temperature_slope && std::isfinite(channelData.temperature_slope_c_per_s) &&
            std::abs(channelData.temperature_slope_c_per_s - pending.temperature_slope_c_per_s) < 0.001)
        {
            pending.temperature_slope = false;
        }
        if (!pending.temperature_slope && std::isfinite(channelData.temperature_slope_c_per_s) &&
            !hasEditorFocus(channel.temperature_slope_spin))
        {
            channel.temperature_slope_spin->setValue(channelData.temperature_slope_c_per_s);
        }
        if (pending.startup_delay && channelData.startup_delay_s == pending.startup_delay_s)
        {
            pending.startup_delay = false;
        }
        if (!pending.startup_delay && !hasEditorFocus(channel.startup_delay_spin))
        {
            channel.startup_delay_spin->setValue(std::clamp(channelData.startup_delay_s,
                                                            channel.startup_delay_spin->minimum(),
                                                            channel.startup_delay_spin->maximum()));
        }
        if (channel.sensor_resistance_edit)
        {
            channel.sensor_resistance_edit->setText(std::isfinite(channelData.sensor_resistance_ohm)
                ? QString::number(channelData.sensor_resistance_ohm, 'f', 6)
                : QStringLiteral("---"));
        }

        if (pending.max_output_percent && channelData.max_output_percent == pending.max_output_percent_value)
        {
            pending.max_output_percent = false;
        }
        if (!pending.max_output_percent && !hasEditorFocus(channel.max_output_spin))
        {
            channel.max_output_spin->setValue(channelData.max_output_percent);
        }

        if (pending.pid &&
            channelData.kp == pending.kp &&
            channelData.ki == pending.ki &&
            channelData.kd == pending.kd)
        {
            pending.pid = false;
        }
        if (!pending.pid && !hasEditorFocus(channel.kp_spin))
        {
            channel.kp_spin->setValue(channelData.kp);
        }
        if (!pending.pid && !hasEditorFocus(channel.ki_spin))
        {
            channel.ki_spin->setValue(channelData.ki);
        }
        if (!pending.pid && !hasEditorFocus(channel.kd_spin))
        {
            channel.kd_spin->setValue(channelData.kd);
        }

        const VaporView::TemperatureControllerCommand& pendingConfig = pending.sensor_config_value;
        const bool pendingPolynomialMatches =
            std::equal(channelData.polynomial_mantissas.cbegin(),
                       channelData.polynomial_mantissas.cend(),
                       pendingConfig.polynomial_mantissas.cbegin()) &&
            std::equal(channelData.polynomial_exponents.cbegin(),
                       channelData.polynomial_exponents.cend(),
                       pendingConfig.polynomial_exponents.cbegin(),
                       [](int current, qint16 pending) { return current == static_cast<int>(pending); });
        if (pending.sensor_config &&
            channelData.sensor_model == static_cast<int>(pendingConfig.sensor_model) &&
            channelData.ntc_b == static_cast<int>(pendingConfig.ntc_b) &&
            channelData.ntc_r0 == static_cast<int>(pendingConfig.ntc_r0) &&
            channelData.pt_r0 == static_cast<int>(pendingConfig.pt_r0) &&
            channelData.pt_a == pendingConfig.pt_a &&
            channelData.pt_b == pendingConfig.pt_b &&
            channelData.pt_c == pendingConfig.pt_c &&
            pendingPolynomialMatches)
        {
            pending.sensor_config = false;
        }
        if (!pending.sensor_config && channel.sensor_model_group && channel.sensor_model_selector &&
            !hasEditorFocus(channel.sensor_model_selector))
        {
            if (auto *button = channel.sensor_model_group->button(channelData.sensor_model))
            {
                const QSignalBlocker blocker(channel.sensor_model_group);
                button->setChecked(true);
            }
        }
        auto updateSensorEdit = [&hasEditorFocus](QLineEdit *edit, const QString& text) {
            if (!edit || hasEditorFocus(edit))
            {
                return;
            }
            const QSignalBlocker blocker(edit);
            edit->setText(text);
        };
        if (!pending.sensor_config)
        {
            updateSensorEdit(channel.ntc_r0_edit, QString::number(std::clamp(channelData.ntc_r0, 0, 9000000)));
            updateSensorEdit(channel.ntc_b_edit, formatTemperatureSensorDecimal(channelData.ntc_b, 100.0, 2));
            updateSensorEdit(channel.pt_r0_edit, formatTemperatureSensorDecimal(channelData.pt_r0, 1000.0, 3));
            updateSensorEdit(channel.pt_a_edit, formatTemperatureSensorDecimal(channelData.pt_a, 1000000.0, 6));
            updateSensorEdit(channel.pt_b_edit, formatTemperatureSensorDecimal(channelData.pt_b, 100000.0, 6));
            updateSensorEdit(channel.pt_c_edit, formatTemperatureSensorDecimal(channelData.pt_c, 10000.0, 6));
        }
        if (!pending.sensor_config)
        {
            for (size_t i = 0; i < channel.polynomial_edits.size(); ++i)
            {
                QLineEdit *edit = channel.polynomial_edits[i];
                if (!edit || hasEditorFocus(edit))
                {
                    continue;
                }
                const QSignalBlocker blocker(edit);
                edit->setText(formatTemperaturePolynomial(channelData.polynomial_mantissas[i],
                                                          channelData.polynomial_exponents[i]));
            }
        }
    }
}

void TemperatureControllerPanel::updateData(const VaporView::TemperatureControllerData& controllerData)
{
    internal_temperature_label_->setText(fixedDecimalWithUnit(controllerData.valid ? controllerData.internal_temperature_c : std::numeric_limits<double>::quiet_NaN(), 2, 8, QStringLiteral("°C")));
    error_code_label_->setText(controllerData.valid ? QStringLiteral("0x%1").arg(controllerData.error_code, 4, 16, QLatin1Char('0')).toUpper() : QStringLiteral("---"));
    if (controller_mode_combo_)
    {
        const QSignalBlocker blocker(controller_mode_combo_);
        if (pending_controller_mode_ &&
            controllerData.valid &&
            controllerData.controller_mode == pending_controller_mode_value_)
        {
            pending_controller_mode_ = false;
        }
        QWidget *focus = QApplication::focusWidget();
        const bool controllerModeHasFocus =
            controller_mode_combo_->hasFocus() ||
            (focus && controller_mode_combo_->isAncestorOf(focus));
        if (!pending_controller_mode_ && !controllerModeHasFocus)
        {
            const int modeIndex = controller_mode_combo_->findData(controllerData.valid ? controllerData.controller_mode : 0);
            controller_mode_combo_->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
        }
    }
    if (!error_text_label_)
    {
        error_text_label_ = new QLabel(this);
    }
    error_code_label_->setToolTip(controllerData.valid && controllerData.error_code != 0
        ? (is_english_ ? QStringLiteral("RD105 reported an error bitmask. Check the controller/manual before enabling output.")
                       : QStringLiteral("RD105 返回错误位掩码。开启输出前请检查温控器和手册。"))
        : (is_english_ ? QStringLiteral("No error reported") : QStringLiteral("未报告错误")));
    auto hasEditorFocus = [](QWidget *widget) {
        QWidget *focus = QApplication::focusWidget();
        return widget && (widget->hasFocus() || (focus && widget->isAncestorOf(focus)));
    };
    if (controllerData.valid)
    {
        if (pending_common_edits_.device_address &&
            controllerData.device_address == pending_common_edits_.device_address_value)
        {
            pending_common_edits_.device_address = false;
        }
        if (common_.address_spin && !pending_common_edits_.device_address && !hasEditorFocus(common_.address_spin))
        {
            const QSignalBlocker blocker(common_.address_spin);
            common_.address_spin->setValue(std::clamp(controllerData.device_address, common_.address_spin->minimum(), common_.address_spin->maximum()));
        }
        if (pending_common_edits_.rs485_baud &&
            controllerData.rs485_baud_index == pending_common_edits_.rs485_baud_index)
        {
            pending_common_edits_.rs485_baud = false;
        }
        if (common_.rs485_baud_combo && !pending_common_edits_.rs485_baud && !hasEditorFocus(common_.rs485_baud_combo))
        {
            const QSignalBlocker blocker(common_.rs485_baud_combo);
            const int baudIndex = common_.rs485_baud_combo->findData(controllerData.rs485_baud_index);
            common_.rs485_baud_combo->setCurrentIndex(baudIndex >= 0 ? baudIndex : 0);
        }
        if (pending_common_edits_.overtemp_output_mode &&
            controllerData.overtemp_output_mode == pending_common_edits_.overtemp_output_mode_value)
        {
            pending_common_edits_.overtemp_output_mode = false;
        }
        if (common_.overtemp_output_combo && !pending_common_edits_.overtemp_output_mode && !hasEditorFocus(common_.overtemp_output_combo))
        {
            const QSignalBlocker blocker(common_.overtemp_output_combo);
            const int overtempIndex = common_.overtemp_output_combo->findData(controllerData.overtemp_output_mode);
            common_.overtemp_output_combo->setCurrentIndex(overtempIndex >= 0 ? overtempIndex : 0);
        }
    }
    if (common_.internal_temperature_edit)
    {
        common_.internal_temperature_edit->setText(controllerData.valid && std::isfinite(controllerData.internal_temperature_c)
            ? QString::number(controllerData.internal_temperature_c, 'f', 0)
            : QStringLiteral("---"));
    }
    if (controllerData.valid)
    {
        for (int i = 0; i < static_cast<int>(measured_temperature_history_.size()); ++i)
        {
            const double target = controllerData.channels[i].target_temperature_c;
            if (std::isfinite(target))
            {
                target_temperature_by_channel_[i] = target;
            }
            const double measured = controllerData.channels[i].measured_temperature_c;
            if (std::isfinite(measured))
            {
                auto& history = measured_temperature_history_[i];
                history.append(measured);
                while (history.size() > kTemperatureControllerHistoryLimit)
                {
                    history.removeFirst();
                }
            }
        }
    }
    updateChannelData(0, controllerData.channels[0], controllerData.valid);
    updateChannelData(1, controllerData.channels[1], controllerData.valid);
    if (temperature_plot_)
    {
        const int channelIndex = std::clamp(selected_channel_index_, 0, 1);
        temperature_plot_->setChannelIndex(channelIndex);
        temperature_plot_->setTargetTemperature(target_temperature_by_channel_[channelIndex]);
        temperature_plot_->setSamples(measured_temperature_history_[channelIndex]);
    }
}

void TemperatureControllerPanel::setCommandStatus(const QString& text, bool error)
{
    if (!status_label_)
    {
        return;
    }
    status_label_->setText(text);
    status_label_->setProperty("data-valid", !error);
    status_label_->style()->unpolish(status_label_);
    status_label_->style()->polish(status_label_);
}

void TemperatureControllerPanel::setOutputEnabledControl(quint8 channel, bool enabled)
{
    if (channel == 0 || channel > channels_.size())
    {
        return;
    }
    QPushButton *button = channels_[channel - 1].enable_switch;
    if (!button)
    {
        return;
    }
    auto *enableSwitch = static_cast<TemperatureOverviewSwitchButton *>(button);
    enableSwitch->setSwitchChecked(enabled, enableSwitch->switchChecked() != enabled);
}

namespace VaporView::Ground::Widgets
{

QComboBox *createSingleLevelPopupComboBox(QWidget *parent,
                                          bool showSelectionCheck,
                                          bool popupFitContents)
{
    auto *combo = new ::SingleLevelPopupComboBox(parent);
    combo->setShowSelectionCheck(showSelectionCheck);
    combo->setPopupFitContents(popupFitContents);
    return combo;
}

SourceModeOverviewSwitchButton *createSourceModeOverviewSwitchButton(QWidget *parent)
{
    return new ::SourceModeOverviewSwitchButtonImpl(parent);
}

TemperatureControllerOverviewPanel *createTemperatureControllerOverviewPanel(QWidget *parent)
{
    return new ::TemperatureControllerOverviewPanelImpl(parent);
}

} // namespace VaporView::Ground::Widgets
