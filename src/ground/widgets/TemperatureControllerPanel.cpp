#include "ground/widgets/TemperatureControllerWidgets.h"

#include "SerialBaudRateCapabilities.h"

#include "shared/theme/AppTheme.h"
#include "shared/theme/SingleLevelPopupComboBox.h"
#include "shared/theme/SingleLevelPopupMenu.h"
#include "ground/widgets/LabelTextSelection.h"
#include "ground/widgets/VisualTextLabel.h"
#include "ground/widgets/TelemetryPanels.h"
#include "ground/widgets/TemperatureTrendPlotWidget.h"

#include <QAction>
#include <QAbstractAnimation>
#include <QApplication>
#include <QButtonGroup>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDoubleValidator>
#include <QEvent>
#include <QFocusEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QIntValidator>
#include <QKeyEvent>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QPalette>
#include <QRadioButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QShortcut>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QStyleOptionToolButton>
#include <QSvgRenderer>
#include <QTimer>
#include <QToolButton>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

using VaporView::AppThemeColor;
using VaporView::appThemeColor;
using VaporView::SingleLevelPopupComboBox;
using VaporView::SingleLevelPopupMenu;
using VaporView::SingleLevelPopupMenuRow;
using VaporView::SingleLevelPopupTextAlignment;
using VaporView::Ground::Widgets::SegmentedSwitchButton;
using VaporView::Ground::Widgets::SourceModeOverviewSwitchButton;
using VaporView::Ground::Widgets::TemperatureControllerOverviewPanel;

namespace
{
constexpr int kHomeOverviewBodyPadding = 2;
constexpr int kTemperatureControllerSettingsInputWidth = 130;
constexpr int kTemperatureControllerStackedWideFieldWidth = 110;
constexpr int kTemperatureControllerChannelParameterInputWidth = 130;
constexpr int kTemperatureControllerPolynomialStackedFieldWidth = 58;
constexpr int kTemperatureControllerCompactColumnGap = 6;
constexpr int kTemperatureControllerChannelParameterRowWidth =
    kTemperatureControllerChannelParameterInputWidth * 2 + kTemperatureControllerCompactColumnGap;
constexpr int kTemperatureControllerStackedFieldSpacing = 6;
constexpr int kTemperatureControllerChannelSubPageVerticalSpacing = 10;
constexpr int kTemperatureControllerCalibrationHandleWidth = 38;
constexpr int kTemperatureControllerCalibrationChevronIconSize = 14;
constexpr int kTemperatureControllerPolynomialColumnCount = 3;
constexpr int kTemperatureControllerControlsCardWidth = 280;
constexpr int kTemperatureControllerControlsCardHorizontalPadding = 6;
constexpr int kTemperatureControllerControlsCardTopPadding = 6;
constexpr int kTemperatureControllerControlsCardBottomPadding = 0;
constexpr int kTemperatureControllerFactoryResetButtonWidth = 170;
constexpr int kTemperatureControllerChannelButtonWidth = 78;
constexpr int kTemperatureControllerCommonButtonWidth = 88;
constexpr int kTemperatureControllerSubTabMinimumWidth = 80;
constexpr int kTemperatureControllerSubTabTextPadding = 28;
constexpr int kTemperatureControllerTopEnableWidth = 106;
constexpr int kTemperatureControllerTopEnableHeight = 34;
constexpr int kTemperatureControllerCompactInputWidth = 112;
constexpr int kTemperatureControllerInlineLabelSpacing = 6;
constexpr int kTemperatureControllerModeTextWidthReserve = 18;
constexpr int kTemperatureControllerAutoPidTextWidthReserve = 36;
// The 266px common-parameter row is 85 + 6 + 84 + 6 + 85, matching 130 + 6 + 130.
constexpr int kTemperatureControllerPidSideInputWidth = 85;
constexpr int kTemperatureControllerPidCenterInputWidth = 84;
constexpr int kTemperatureControllerConfigRowHeight = 38;
constexpr int kTemperatureControllerTopControlsHeight = 38;
constexpr int kTemperatureControllerNavigationButtonHeight = 30;
constexpr int kTemperatureControllerNavigationHorizontalMargin = 2;
constexpr int kTemperatureControllerNavigationVerticalMargin = 3;
constexpr int kTemperatureControllerNavigationSpacing = 2;
constexpr int kTemperatureControllerRowSpacing = 8;
// The selector is below the left card now, so the stack only needs the three-row page content height.
constexpr int kTemperatureControllerChannelConfigSubStackHeight = 218;
constexpr int kTemperatureControllerChannelStackHeight =
    kTemperatureControllerChannelConfigSubStackHeight;
constexpr int kTemperatureControllerCommonStackHeight = kTemperatureControllerChannelStackHeight;
constexpr int kTemperatureControllerConfigPlotHeight =
    kTemperatureControllerControlsCardTopPadding +
    kTemperatureControllerChannelConfigSubStackHeight +
    kTemperatureControllerControlsCardBottomPadding +
    kTemperatureControllerCompactColumnGap +
    kTemperatureControllerConfigRowHeight;
constexpr int kTemperatureControllerHistoryLimit = 240;

double localClockSeconds()
{
    return static_cast<double>(QDateTime::currentMSecsSinceEpoch()) / 1000.0;
}
constexpr const char *kTextWidthCandidatesProperty = "_vv_text_width_candidates";
constexpr const char *kTextWidthPaddingProperty = "_vv_text_width_padding";
constexpr const char *kNumericWidthCandidatesProperty = "_vv_numeric_width_candidates";
constexpr const char *kNumericWidthPaddingProperty = "_vv_numeric_width_padding";

void fitTemperatureComboWidth(QComboBox *combo, int textWidthReserve = kTemperatureControllerModeTextWidthReserve)
{
    if (!combo)
    {
        return;
    }

    combo->ensurePolished();
    QStyleOptionComboBox option;
    option.initFrom(combo);
    constexpr int kWidthProbe = 1000;
    option.rect = QRect(0, 0, kWidthProbe, combo->height());
    option.currentText = combo->currentText();
    option.editable = combo->isEditable();
    option.frame = combo->hasFrame();
    const QRect editField = combo->style()->subControlRect(
        QStyle::CC_ComboBox,
        &option,
        QStyle::SC_ComboBoxEditField,
        combo);
    const int nonTextWidth = editField.isValid()
        ? std::max(0, kWidthProbe - editField.width())
        : 0;
    QFontMetrics metrics(combo->font());
    int longestTextWidth = 0;
    for (int index = 0; index < combo->count(); ++index)
    {
        longestTextWidth = std::max(longestTextWidth,
                                    metrics.horizontalAdvance(combo->itemText(index)));
    }
    if (longestTextWidth > 0)
    {
        combo->setFixedWidth(longestTextWidth + nonTextWidth + textWidthReserve);
    }
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
            QStringLiteral("温控器模式")};
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

void configureSingleLevelComboCheckIcon(SingleLevelPopupComboBox *combo)
{
    if (!combo)
    {
        return;
    }
    combo->setSelectionCheckIconProvider([]() {
        return createLucideIcon(QStringLiteral("check"),
                                appThemeColor(AppThemeColor::MenuCheckText,
                                              VaporView::isDarkThemeEnabled()));
    });
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

class CalibrationSideDrawer final : public QWidget
{
public:
    explicit CalibrationSideDrawer(QWidget *parent = nullptr)
        : QWidget(parent)
        , animation_(new QVariantAnimation(this))
    {
        setObjectName(QStringLiteral("temperatureCalibrationSideDrawer"));
        setMouseTracking(true);
        setFocusPolicy(Qt::TabFocus);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_StyledBackground, false);
        setProperty("temperatureCalibrationSideDrawer", true);
        setProperty("temperatureCalibrationHandleWidth", kTemperatureControllerCalibrationHandleWidth);
        setProperty("expanded", false);
        setProperty("expansionProgress", 0.0);

        reduced_motion_enabled_ = QCoreApplication::instance() &&
            QCoreApplication::instance()->property("vaporViewReducedMotion").toBool();
        setProperty("reducedMotionEnabled", reduced_motion_enabled_);
        animation_->setObjectName(QStringLiteral("temperatureCalibrationDrawerAnimation"));
        animation_->setDuration(reduced_motion_enabled_ ? 150 : 320);
        animation_->setEasingCurve(QEasingCurve::Linear);
        connect(animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            const qreal t = std::clamp(value.toReal(), 0.0, 1.0);
            visual_progress_ = animation_start_progress_ +
                (animation_target_progress_ - animation_start_progress_) * easeOutCubic(t);
            updateDrawerGeometry();
        });
        connect(animation_, &QVariantAnimation::finished, this, [this]() {
            visual_progress_ = animation_target_progress_;
            expansion_progress_ = animation_target_progress_;
            setProperty("expansionProgress", expansion_progress_);
            updateDrawerGeometry();
        });
        updateHandleText();
    }

    void setContentWidget(QWidget *widget)
    {
        if (content_widget_ == widget)
        {
            return;
        }
        if (content_widget_)
        {
            content_widget_->setParent(nullptr);
        }
        content_widget_ = widget;
        if (content_widget_)
        {
            content_widget_->setParent(this);
            content_widget_->setVisible(false);
        }
        updateDrawerGeometry();
    }

    QWidget *contentWidget() const
    {
        return content_widget_;
    }

    void setHandleText(const QString& text)
    {
        handle_text_ = text;
        handle_text_.replace(QStringLiteral("\n-\n"), QStringLiteral("\n"));
        setProperty("temperatureCalibrationHandleText", handle_text_);
        updateHandleText();
        update();
    }

    void setHostRect(const QRect& rect, int preferredHeight)
    {
        host_rect_ = rect;
        preferred_height_ = preferredHeight;
        updateDrawerGeometry();
    }

    void setHostSize(const QSize& size, int preferredHeight)
    {
        setHostRect(QRect(QPoint(0, 0), size), preferredHeight);
    }

    void setExpanded(bool expanded, bool animated = true)
    {
        if (!animated)
        {
            animation_->stop();
            expanded_ = expanded;
            expansion_progress_ = expanded ? 1.0 : 0.0;
            visual_progress_ = expansion_progress_;
            setProperty("expanded", expanded_);
            setProperty("expansionProgress", expansion_progress_);
            updateHandleChevronIconProperty();
            updateDrawerGeometry();
            return;
        }

        if (expanded_ == expanded && animation_->state() != QAbstractAnimation::Running)
        {
            return;
        }
        expanded_ = expanded;
        setProperty("expanded", expanded_);
        updateHandleChevronIconProperty();
        animation_->stop();
        animation_start_progress_ = visual_progress_;
        animation_target_progress_ = expanded ? 1.0 : 0.0;
        expansion_progress_ = animation_start_progress_;
        setProperty("expansionProgress", expansion_progress_);
        animation_->setStartValue(0.0);
        animation_->setEndValue(1.0);
        animation_->start();
    }

    void toggle()
    {
        setExpanded(!expanded_);
    }

    bool isExpanded() const
    {
        return expanded_;
    }

    bool isAnimationRunning() const
    {
        return animation_->state() == QAbstractAnimation::Running;
    }

    qreal expansionProgress() const
    {
        return expansion_progress_;
    }

    int handleWidth() const
    {
        return kTemperatureControllerCalibrationHandleWidth;
    }

    int handleHeight() const
    {
        QFont handleFont = font();
        handleFont.setWeight(QFont::DemiBold);
        const QFontMetrics metrics(handleFont);
        const QStringList rows = handle_text_.split(QLatin1Char('\n'));
        const bool hasChevron = hasChevronRow(rows);
        const int extraGap = rows.size() > 4 ? 3 : 0;
        constexpr int verticalPadding = 8;
        return (rows.size() + (hasChevron ? 1 : 0)) * metrics.height() +
            extraGap + verticalPadding * 2;
    }

    QSize sizeHint() const override
    {
        return QSize(handleWidth(), handleHeight());
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        if (width() <= 0 || height() <= 0)
        {
            return;
        }

        const bool dark = VaporView::isDarkThemeEnabled();
        const bool handleHovered = hovered_handle_ || pressed_;
        const QColor fill = appThemeColor(
            handleHovered || expanded_ ? AppThemeColor::PrimarySubtle : AppThemeColor::SurfaceAlt,
            dark);
        const QColor border = appThemeColor(expanded_ ? AppThemeColor::Primary : AppThemeColor::Border,
                                            dark);
        const QColor text = appThemeColor(expanded_ || handleHovered ? AppThemeColor::Primary : AppThemeColor::Text,
                                          dark);
        const QRectF handleRect = currentHandleRect();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath path;
        const qreal radius = 8.0;
        path.moveTo(handleRect.right(), handleRect.top());
        path.lineTo(handleRect.left() + radius, handleRect.top());
        path.quadTo(handleRect.left(), handleRect.top(), handleRect.left(), handleRect.top() + radius);
        path.lineTo(handleRect.left(), handleRect.bottom() - radius);
        path.quadTo(handleRect.left(), handleRect.bottom(), handleRect.left() + radius, handleRect.bottom());
        path.lineTo(handleRect.right(), handleRect.bottom());
        path.closeSubpath();
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(fill);
        painter.drawPath(path);

        QFont handleFont = font();
        handleFont.setWeight(QFont::DemiBold);
        painter.setFont(handleFont);
        painter.setPen(text);
        const QFontMetrics metrics(handleFont);
        const QStringList rows = handle_text_.split(QLatin1Char('\n'));
        const bool hasChevron = hasChevronRow(rows);
        const int lineHeight = metrics.height();
        const int extraGap = rows.size() > 4 ? 3 : 0;
        const int totalHeight = (rows.size() + (hasChevron ? 1 : 0)) * lineHeight + extraGap;
        int y = qRound(handleRect.top()) +
            std::max(0, qRound((handleRect.height() - totalHeight) / 2.0));
        const auto drawChevron = [&]() {
            const QIcon icon = createLucideIcon(expanded_ ? QStringLiteral("chevron-right")
                                                          : QStringLiteral("chevron-left"),
                                                text);
            const int iconSize = std::min(kTemperatureControllerCalibrationChevronIconSize,
                                          std::max(1, lineHeight));
            const QRect iconRect(qRound(handleRect.left() + (handleRect.width() - iconSize) / 2.0),
                                 y + std::max(0, (lineHeight - iconSize) / 2),
                                 iconSize,
                                 iconSize);
            const QPixmap pixmap = icon.pixmap(QSize(iconSize, iconSize));
            if (!pixmap.isNull())
            {
                painter.drawPixmap(iconRect, pixmap);
            }
            y += lineHeight;
        };
        for (int i = 0; i < rows.size(); ++i)
        {
            painter.drawText(QRectF(handleRect.left(), y, handleRect.width(), lineHeight),
                             Qt::AlignCenter,
                             rows.at(i));
            y += lineHeight;
            if (i == 3)
            {
                y += extraGap;
            }
            if (hasChevron && i == rows.size() - 2)
            {
                drawChevron();
            }
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const bool inHandle = event && currentHandleRect().contains(event->position());
        if (inHandle != hovered_handle_)
        {
            hovered_handle_ = inHandle;
            update();
        }
        QWidget::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        hovered_handle_ = false;
        update();
        QWidget::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event && event->button() == Qt::LeftButton && isHandlePosition(event->position()))
        {
            setFocus(Qt::MouseFocusReason);
            pressed_ = true;
            update();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event && event->button() == Qt::LeftButton)
        {
            const bool activate = pressed_ && isHandlePosition(event->position());
            pressed_ = false;
            update();
            if (activate)
            {
                toggle();
            }
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
                      event->key() == Qt::Key_Space))
        {
            toggle();
            event->accept();
            return;
        }
        if (event && event->key() == Qt::Key_Escape && expanded_)
        {
            setExpanded(false);
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void focusInEvent(QFocusEvent *event) override
    {
        QWidget::focusInEvent(event);
        update();
    }

    void focusOutEvent(QFocusEvent *event) override
    {
        QWidget::focusOutEvent(event);
        update();
    }

private:
    static qreal easeOutCubic(qreal value)
    {
        const qreal t = 1.0 - std::clamp(value, 0.0, 1.0);
        return 1.0 - t * t * t;
    }

    bool isHandlePosition(const QPointF& position) const
    {
        return currentHandleRect().contains(position);
    }

    void updateHandleText()
    {
        if (handle_text_.isEmpty())
        {
            handle_text_ = QStringLiteral("校\n准\n系\n数\nA0\nA7");
            setProperty("temperatureCalibrationHandleText", handle_text_);
        }
        updateHandleChevronIconProperty();
        setProperty("temperatureCalibrationHandleHeight", handleHeight());
        setToolTip(QStringLiteral("展开校准系数 A0-A7"));
        setAccessibleName(QStringLiteral("校准系数 A0-A7"));
    }

    bool hasChevronRow(const QStringList& rows) const
    {
        return rows.size() >= 2 &&
            rows.at(rows.size() - 2).contains(QStringLiteral("A0")) &&
            rows.last().contains(QStringLiteral("A7"));
    }

    void updateHandleChevronIconProperty()
    {
        setProperty("temperatureCalibrationHandleChevronIconName",
                    expanded_ ? QStringLiteral("chevron-right") : QStringLiteral("chevron-left"));
    }

    QRectF currentHandleRect() const
    {
        const int currentHeight = std::min(height(), handleHeight());
        const qreal top = (height() - currentHeight) / 2.0;
        return QRectF(0, top, handleWidth(), currentHeight);
    }

    int expandedWidthForHost() const
    {
        const int hostWidth = std::max(0, host_rect_.width());
        if (hostWidth <= handleWidth())
        {
            return hostWidth;
        }
        const int contentMinimum = content_widget_
            ? std::max(content_widget_->minimumSizeHint().width(), content_widget_->sizeHint().width())
            : 0;
        const int minimumExpanded = handleWidth() + std::max(0, contentMinimum);
        return std::clamp(std::max(minimumExpanded, qRound(hostWidth * 0.46)),
                          handleWidth(),
                          hostWidth);
    }

    void updateDrawerGeometry()
    {
        setProperty("temperatureCalibrationHandleHeight", handleHeight());
        const int hostWidth = std::max(0, host_rect_.width());
        const int hostHeight = std::max(0, host_rect_.height());
        if (hostWidth <= 0 || hostHeight <= 0)
        {
            return;
        }
        const int expandedWidth = expandedWidthForHost();
        const qreal progress = std::clamp(visual_progress_, 0.0, 1.2);
        const int currentWidth = std::clamp(
            qRound(handleWidth() + (expandedWidth - handleWidth()) * progress),
            handleWidth(),
            hostWidth);
        const int contentHeight = std::min(hostHeight, std::max(preferred_height_,
                                                                content_widget_ ? content_widget_->sizeHint().height() : 0));
        const bool collapsed = visual_progress_ <= 0.001;
        const int currentHeight = collapsed ? std::min(hostHeight, handleHeight()) : contentHeight;
        // Keep the handle's vertical center anchored to the sensor-config host
        // while the drawer changes width or switches between collapsed and
        // expanded heights. Only the left edge participates in the animation.
        const int maxTop = host_rect_.top() + std::max(0, hostHeight - currentHeight);
        const int centeredTop = host_rect_.top() + std::max(0, (hostHeight - currentHeight) / 2);
        const int top = std::clamp(centeredTop, host_rect_.top(), maxTop);
        setGeometry(host_rect_.left() + hostWidth - currentWidth,
                    top,
                    currentWidth,
                    currentHeight);
        if (content_widget_)
        {
            const int contentWidth = std::max(0, currentWidth - handleWidth());
            content_widget_->setGeometry(handleWidth(), 0, contentWidth, contentHeight);
            content_widget_->setVisible(currentWidth > handleWidth() + 1 || expanded_);
            content_widget_->raise();
        }
        raise();
        update();
    }

    QWidget *content_widget_ = nullptr;
    QVariantAnimation *animation_ = nullptr;
    QRect host_rect_;
    QString handle_text_;
    qreal expansion_progress_ = 0.0;
    qreal visual_progress_ = 0.0;
    qreal animation_start_progress_ = 0.0;
    qreal animation_target_progress_ = 0.0;
    int preferred_height_ = 0;
    bool expanded_ = false;
    bool reduced_motion_enabled_ = false;
    bool hovered_handle_ = false;
    bool pressed_ = false;
};
} // namespace

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

QString temperatureOverviewOutputPercentText(double value)
{
    if (!std::isfinite(value))
    {
        return QStringLiteral("---");
    }

    return QStringLiteral("%1%").arg(QLocale::c().toString(value, 'f', 2));
}

void setTemperatureOverviewPillText(QLabel *label,
                                    const QString& title,
                                    const QString& number,
                                    const QString& unit,
                                    const QColor& numberColor)
{
    if (!label)
    {
        return;
    }

    const QString displayText = QStringLiteral("%1 %2 %3").arg(title, number, unit);
    label->setProperty("reservedValueText", temperatureOverviewReservedNumberText());
    label->setProperty("reservedDisplayText",
                       QStringLiteral("%1 %2 %3")
                           .arg(title, temperatureOverviewReservedNumberText(), unit));
    label->setProperty("displayText", displayText);
    label->setProperty("legendNumberColor", numberColor.name(QColor::HexRgb));
    label->setProperty("reservedValueFits", false);
    label->setTextFormat(Qt::RichText);
    label->setText(QStringLiteral("%1&nbsp;<span style=\"color: %2;\">%3</span>&nbsp;%4")
                       .arg(title.toHtmlEscaped(),
                            numberColor.name(QColor::HexRgb),
                            number.toHtmlEscaped(),
                            unit.toHtmlEscaped()));
    label->setToolTip(displayText);
    label->setAccessibleName(displayText);
    label->style()->unpolish(label);
    label->style()->polish(label);
}

void setTemperatureOverviewOutputPercentText(QLabel *label, const QString& title, const QString& value)
{
    if (!label)
    {
        return;
    }

    label->setTextFormat(Qt::RichText);
    label->setText(QStringLiteral(
        "<div align=\"center\" style=\"line-height: 13px; white-space: nowrap;\">"
        "<span style=\"font-size: 12px; font-weight: 700;\">%1</span><br/>"
        "<span style=\"font-size: 14px; font-weight: 700;\">%2</span>"
        "</div>")
        .arg(title.toHtmlEscaped(), value.toHtmlEscaped()));
    label->setToolTip(title);
    label->setAccessibleName(title);
}

class TemperatureOverviewSwitchButton final : public SegmentedSwitchButton
{
public:
    explicit TemperatureOverviewSwitchButton(QWidget *parent = nullptr)
        : SegmentedSwitchButton(parent)
    {
        setObjectName(QStringLiteral("temperatureOverviewOutputSwitch"));
        setAccentMode(AccentMode::BinaryState);
        setAutoToggle(false);
        setEnglish(false);
    }

    void setEnglish(bool english)
    {
        setSegmentTexts(english ? QStringLiteral("Off") : QStringLiteral("关闭"),
                        english ? QStringLiteral("On") : QStringLiteral("开启"));
        setStateDescription(english ? QStringLiteral("Output Enable") : QStringLiteral("输出使能"),
                            english ? QStringLiteral(": ") : QStringLiteral("："));
    }
};

class SourceModeOverviewSwitchButtonImpl final : public SourceModeOverviewSwitchButton
{
public:
    explicit SourceModeOverviewSwitchButtonImpl(QWidget *parent = nullptr)
        : SourceModeOverviewSwitchButton(parent)
    {
        setObjectName(QStringLiteral("sourceModeOverviewSwitch"));
        setAccentMode(AccentMode::Primary);
        setAutoToggle(false);
        setEnglish(false);
    }

    void setEnglish(bool english) override
    {
        setSegmentTexts(english ? QStringLiteral("Local") : QStringLiteral("本地"),
                        english ? QStringLiteral("Remote") : QStringLiteral("远程"));
        setStateDescription(english ? QStringLiteral("Source") : QStringLiteral("数据源"),
                            english ? QStringLiteral(": ") : QStringLiteral("："));
    }
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
        textFont.setWeight(QFont::Medium);
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
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

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
        auto configureChannelMenuAction = [this](SingleLevelPopupMenuRow *row, const QString& text, const QString& objectName) {
            QFont rowFont = row->font();
            rowFont.setWeight(QFont::Medium);
            row->setObjectName(objectName);
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
            row->setFocusPolicy(Qt::StrongFocus);
            row->setText(text);
            QWidgetAction *action = channel_menu_->addRow(row);
            action->setObjectName(objectName);
            action->setText(text);
            return action;
        };
        channel_menu_row_1_ = new SingleLevelPopupMenuRow(channel_menu_);
        channel_menu_row_2_ = new SingleLevelPopupMenuRow(channel_menu_);
        channel_action_1_ = configureChannelMenuAction(channel_menu_row_1_, QStringLiteral("通道1"),
                                                       QStringLiteral("temperatureOverviewChannel1MenuAction"));
        channel_action_2_ = configureChannelMenuAction(channel_menu_row_2_, QStringLiteral("通道2"),
                                                       QStringLiteral("temperatureOverviewChannel2MenuAction"));
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

        output_percent_value_ = new QLabel(summary_widget_);
        output_percent_value_->setObjectName(QStringLiteral("temperatureOverviewOutputPercentPill"));
        output_percent_value_->setAlignment(Qt::AlignCenter);
        output_percent_value_->setWordWrap(false);
        output_percent_value_->setFixedWidth(kOverviewControlWidth);
        output_percent_value_->setFixedHeight(kOverviewOutputPercentHeight);
        output_percent_value_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        summaryLayout->addWidget(output_percent_value_, 0);

        output_capsule_ = new QFrame(summary_widget_);
        output_capsule_->setObjectName(QStringLiteral("temperatureOverviewOutputCapsule"));
        output_capsule_->setFixedSize(kOverviewControlWidth, kOverviewOutputCapsuleHeight);
        output_capsule_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        auto *outputLayout = new QVBoxLayout(output_capsule_);
        outputLayout->setContentsMargins(kOverviewOutputHorizontalMargin,
                                         kOverviewOutputVerticalMargin,
                                         kOverviewOutputHorizontalMargin,
                                         kOverviewOutputVerticalMargin);
        outputLayout->setSpacing(kOverviewOutputSpacing);

        output_label_ = new QLabel(output_capsule_);
        output_label_->setObjectName(QStringLiteral("temperatureOverviewOutputLabel"));
        output_label_->setAlignment(Qt::AlignCenter);
        output_label_->setFixedHeight(kOverviewOutputLabelHeight);
        output_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        outputLayout->addWidget(output_label_, 0);

        output_switch_button_ = new TemperatureOverviewSwitchButton(output_capsule_);
        output_switch_button_->setFixedWidth(kOverviewOutputSwitchWidth);
        output_switch_button_->setFixedHeight(kOverviewOutputSwitchHeight);
        output_switch_button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        output_switch_button_->setStyleSheet(QStringLiteral(
            "QPushButton#temperatureOverviewOutputSwitch { min-height: %1px; max-height: %1px; }")
            .arg(kOverviewOutputSwitchHeight));
        connect(output_switch_button_, &SegmentedSwitchButton::selectionRequested, this, [this](bool requested) {
            if (output_enabled_callback_)
            {
                output_enabled_callback_(currentChannelNumber(), requested);
            }
        });
        outputLayout->addWidget(output_switch_button_, 0, Qt::AlignHCenter);
        summaryLayout->addWidget(output_capsule_, 0);
        summaryLayout->addStretch(1);

        layout->addWidget(summary_widget_, 0);

        auto *divider = new QFrame(this);
        divider->setObjectName(QStringLiteral("homeOverviewDivider"));
        divider->setFixedWidth(1);
        divider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        layout->addWidget(divider);

        auto *plotSection = new QWidget(this);
        plotSection->setFixedHeight(kOverviewTrendPlotHeight);
        plotSection->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto *plotSectionLayout = new QVBoxLayout(plotSection);
        plotSectionLayout->setContentsMargins(0, 0, 0, 0);
        plotSectionLayout->setSpacing(0);

        plot_ = new TemperatureTrendPlotWidget(plotSection);
        plot_->setProperty("temperatureOverviewPlot", true);
        plot_->setCompactMode(true);
        plot_->setTimeAxisEnabled(true);
        plot_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        plotSectionLayout->addWidget(plot_, 1);

        value_overlay_ = new QWidget(plot_);
        value_overlay_->setObjectName(QStringLiteral("temperatureOverviewValueOverlay"));
        value_overlay_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        value_overlay_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        auto *valueOverlayLayout = new QVBoxLayout(value_overlay_);
        valueOverlayLayout->setContentsMargins(0, 0, 0, 0);
        valueOverlayLayout->setSpacing(kOverviewValuePillSpacing);

        target_temp_value_ = new QLabel(value_overlay_);
        target_temp_value_->setObjectName(QStringLiteral("temperatureOverviewValuePill"));
        target_temp_value_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        target_temp_value_->setWordWrap(false);
        target_temp_value_->setMinimumHeight(kOverviewValuePillHeight);
        target_temp_value_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        valueOverlayLayout->addWidget(target_temp_value_, 0);

        current_temp_value_ = new QLabel(value_overlay_);
        current_temp_value_->setObjectName(QStringLiteral("temperatureOverviewValuePill"));
        current_temp_value_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        current_temp_value_->setWordWrap(false);
        current_temp_value_->setMinimumHeight(kOverviewValuePillHeight);
        current_temp_value_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        valueOverlayLayout->addWidget(current_temp_value_, 0);
        plot_->installEventFilter(this);
        layout->addWidget(plotSection, 1, Qt::AlignTop);

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

        if (output_label_) output_label_->setText(english ? QStringLiteral("Output Enable") : QStringLiteral("输出使能"));
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
            const auto sampleTimestamp = sample.timestamp == std::chrono::steady_clock::time_point{}
                ? std::chrono::steady_clock::now()
                : sample.timestamp;
            if (!temperature_history_origin_valid_ ||
                sampleTimestamp < temperature_history_origin_)
            {
                temperature_history_origin_ = sampleTimestamp;
                temperature_history_origin_local_seconds_ = localClockSeconds();
                temperature_history_origin_valid_ = true;
                for (int i = 0; i < static_cast<int>(measured_temperature_history_.size()); ++i)
                {
                    measured_temperature_history_[i].clear();
                    measured_temperature_time_history_[i].clear();
                }
            }
            const double sampleTimeSeconds = temperature_history_origin_local_seconds_ +
                std::chrono::duration<double>(sampleTimestamp - temperature_history_origin_).count();
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
                    auto& timeHistory = measured_temperature_time_history_[i];
                    history.append(measured);
                    timeHistory.append(sampleTimeSeconds);
                    while (history.size() > kTemperatureControllerHistoryLimit)
                    {
                        history.removeFirst();
                        timeHistory.removeFirst();
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
        updateOverviewValuePillGeometry();
        scheduleSummaryControlHeightUpdate();
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == summary_widget_ && event->type() == QEvent::Resize)
        {
            scheduleSummaryControlHeightUpdate();
        }
        if (watched == plot_ &&
            (event->type() == QEvent::Resize || event->type() == QEvent::Show ||
             event->type() == QEvent::FontChange))
        {
            QTimer::singleShot(0, this, [this]() {
                updateOverviewValuePillGeometry();
            });
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
    static constexpr int kOverviewChannelHeight = 28;
    static constexpr int kOverviewOutputPercentHeight = 42;
    static constexpr int kOverviewTrendPlotHeight = 144;
    static constexpr int kOverviewValuePillHeight = 30;
    static constexpr int kOverviewValuePillSpacing = 4;
    static constexpr int kOverviewValuePillAxisGap = 8;
    static constexpr int kOverviewValuePillHorizontalPadding = 16;
    static constexpr int kOverviewOutputCapsuleHeight = 60;
    static constexpr int kOverviewOutputSwitchHeight = 34;
    static constexpr int kOverviewOutputFrameWidth = 1;
    static constexpr int kOverviewOutputHorizontalMargin = 4;
    static constexpr int kOverviewOutputVerticalMargin = 3;
    static constexpr int kOverviewOutputSpacing = 2;
    static constexpr int kOverviewOutputLabelHeight = 18;
    static constexpr int kOverviewOutputSwitchWidth =
        kOverviewControlWidth -
        (kOverviewOutputHorizontalMargin + kOverviewOutputFrameWidth) * 2;

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

    void updateOverviewValuePillGeometry()
    {
        if (!plot_ || !value_overlay_ || !target_temp_value_ || !current_temp_value_)
        {
            return;
        }

        const QRectF plotRect = plot_->plotAreaRect();
        const int left = std::max(0,
                                  static_cast<int>(std::ceil(plotRect.left())) +
                                      kOverviewValuePillAxisGap);
        const int top = std::max(0,
                                 static_cast<int>(std::ceil(plotRect.top())) +
                                     kOverviewValuePillAxisGap);
        const int right = std::max(left,
                                   static_cast<int>(std::floor(plotRect.right())) -
                                       kOverviewValuePillAxisGap);
        const int availableWidth = std::max(0, right - left);
        if (availableWidth <= 0 || plotRect.height() <= 1.0)
        {
            value_overlay_->hide();
            return;
        }

        const auto requiredWidth = [this](QLabel *label) {
            if (!label)
            {
                return 0;
            }
            QFont pillFont = label->font();
            pillFont.setWeight(QFont::Bold);
            const QFontMetrics metrics(pillFont);
            const int currentWidth = metrics.horizontalAdvance(
                label->property("displayText").toString());
            const int reservedWidth = metrics.horizontalAdvance(
                label->property("reservedDisplayText").toString());
            return std::max(currentWidth, reservedWidth) + kOverviewValuePillHorizontalPadding;
        };
        const int desiredWidth = std::max(requiredWidth(target_temp_value_),
                                          requiredWidth(current_temp_value_));
        const int width = std::min(availableWidth, std::max(1, desiredWidth));
        const int pillHeight = std::max(
            kOverviewValuePillHeight,
            QFontMetrics(target_temp_value_->font()).height() + 8);
        const int totalHeight = pillHeight * 2 + kOverviewValuePillSpacing;
        if (top + totalHeight > plot_->height())
        {
            value_overlay_->hide();
            return;
        }

        value_overlay_->setGeometry(left, top, width, totalHeight);
        target_temp_value_->setFixedHeight(pillHeight);
        current_temp_value_->setFixedHeight(pillHeight);
        target_temp_value_->setProperty("reservedValueFits", desiredWidth <= availableWidth);
        current_temp_value_->setProperty("reservedValueFits", desiredWidth <= availableWidth);
        value_overlay_->setProperty("axisSafeLeft", plotRect.left());
        value_overlay_->setProperty("axisSafeTop", plotRect.top());
        value_overlay_->setProperty("axisSafeGap", kOverviewValuePillAxisGap);
        value_overlay_->setProperty("contentFits", desiredWidth <= availableWidth);
        value_overlay_->show();
        value_overlay_->raise();
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
        const bool dark = VaporView::isDarkThemeEnabled();
        const bool valid = latest_data_.valid;
        const VaporView::TemperatureControllerChannelData& channel = latest_data_.channels[index];
        const bool measuredValid = valid && std::isfinite(channel.measured_temperature_c);
        const bool targetValid = valid && std::isfinite(channel.target_temperature_c);
        const bool outputPercentValid = valid && std::isfinite(channel.output_percent);
        setTemperatureOverviewOutputPercentText(
            output_percent_value_,
            is_english_ ? QStringLiteral("Output Percent") : QStringLiteral("输出百分比"),
            temperatureOverviewOutputPercentText(outputPercentValid
                ? channel.output_percent
                : std::numeric_limits<double>::quiet_NaN()));
        setTemperatureOverviewPillText(
            target_temp_value_,
            is_english_ ? QStringLiteral("Target") : QStringLiteral("目标"),
            temperatureOverviewNumberText(targetValid ? channel.target_temperature_c : std::numeric_limits<double>::quiet_NaN()),
            is_english_ ? QStringLiteral("°C") : QStringLiteral("℃"),
            appThemeColor(AppThemeColor::ToolbarGreen, dark));
        setTemperatureOverviewPillText(
            current_temp_value_,
            is_english_ ? QStringLiteral("Current") : QStringLiteral("当前"),
            temperatureOverviewNumberText(measuredValid ? channel.measured_temperature_c : std::numeric_limits<double>::quiet_NaN()),
            is_english_ ? QStringLiteral("°C") : QStringLiteral("℃"),
            appThemeColor(AppThemeColor::PlotSeriesTemperature, dark));
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
            plot_->setSampleTimes(measured_temperature_time_history_[index]);
        }
        updateOverviewValuePillGeometry();
    }

    QToolButton *channel_button_ = nullptr;
    QWidget *summary_widget_ = nullptr;
    SingleLevelPopupMenu *channel_menu_ = nullptr;
    QAction *channel_action_1_ = nullptr;
    QAction *channel_action_2_ = nullptr;
    SingleLevelPopupMenuRow *channel_menu_row_1_ = nullptr;
    SingleLevelPopupMenuRow *channel_menu_row_2_ = nullptr;
    QLabel *output_percent_value_ = nullptr;
    QLabel *target_temp_value_ = nullptr;
    QLabel *current_temp_value_ = nullptr;
    QWidget *value_overlay_ = nullptr;
    QFrame *output_capsule_ = nullptr;
    QLabel *output_label_ = nullptr;
    TemperatureOverviewSwitchButton *output_switch_button_ = nullptr;
    TemperatureTrendPlotWidget *plot_ = nullptr;
    VaporView::TemperatureControllerData latest_data_;
    std::array<QVector<double>, 2> measured_temperature_history_{};
    std::array<QVector<double>, 2> measured_temperature_time_history_{};
    std::array<double, 2> target_temperature_by_channel_{
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN()};
    std::chrono::steady_clock::time_point temperature_history_origin_{};
    double temperature_history_origin_local_seconds_ = 0.0;
    std::function<void(quint8, bool)> output_enabled_callback_;
    int selected_channel_index_ = 0;
    bool summary_height_update_pending_ = false;
    bool temperature_history_origin_valid_ = false;
    bool is_english_ = false;
};

TemperatureControllerPanel::TemperatureControllerPanel(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

QWidget *TemperatureControllerPanel::titleStatusWidget() const
{
    return title_status_strip_;
}

bool TemperatureControllerPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched->property("temperatureTopControlAlignmentHost").toBool() &&
        (event->type() == QEvent::Show || event->type() == QEvent::Resize))
    {
        QTimer::singleShot(0, this, [this]() {
            const int channelIndex = std::clamp(selected_channel_index_, 0, 1);
            alignChannelTopControlFields(channelIndex);
            alignCommonSettingsColumns(channelIndex);
        });
    }
    for (int channelIndex = 0; channelIndex < static_cast<int>(channels_.size()); ++channelIndex)
    {
        if (watched == channels_[channelIndex].sensor_config_page &&
            (event->type() == QEvent::Show || event->type() == QEvent::Resize))
        {
            QTimer::singleShot(0, this, [this, channelIndex]() {
                alignSensorCalibrationOverlay(channelIndex);
            });
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TemperatureControllerPanel::alignSensorCalibrationOverlay(int channelIndex)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels_.size()))
    {
        return;
    }
    ChannelWidgets& channel = channels_[channelIndex];
    auto *drawer = static_cast<CalibrationSideDrawer *>(channel.sensor_calibration_drawer);
    if (!channel.sensor_config_page || !channel.sensor_calibration_overlay || !drawer ||
        channel.sensor_config_page->width() <= 0 ||
        channel.sensor_config_page->height() <= 0)
    {
        return;
    }

    QWidget *page = channel.sensor_config_page;
    QWidget *host = drawer->parentWidget();
    if (!host)
    {
        return;
    }
    const QRect pageRectInHost(page->mapTo(host, QPoint(0, 0)), page->size());
    const QRect hostRect(pageRectInHost.left(),
                         0,
                         std::max(0, host->width() - pageRectInHost.left()),
                         host->height());
    drawer->setHostRect(hostRect, hostRect.height());
}

void TemperatureControllerPanel::alignChannelTopControlFields(int channelIndex)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels_.size()))
    {
        return;
    }
    ChannelWidgets& channel = channels_[channelIndex];
    if (!channel.common_top_controls || !channel.common_top_leading_spacer ||
        !channel.common_top_middle_spacer || !channel.common_top_mode_spacer ||
        !channel.enable_field || !channel.auto_pid_field)
    {
        return;
    }

    auto currentFieldWidth = [](QWidget *field) {
        if (!field)
        {
            return 0;
        }
        if (QLayout *layout = field->layout())
        {
            layout->invalidate();
            layout->activate();
        }
        const int width = field->sizeHint().width();
        if (width > 0)
        {
            field->setFixedWidth(width);
        }
        return std::max(0, width);
    };

    const int enableFieldWidth = currentFieldWidth(channel.enable_field);
    const int autoPidFieldWidth = currentFieldWidth(channel.auto_pid_field);
    const int controllerModeFieldWidth = currentFieldWidth(controller_mode_field_);
    QWidget *page = channel.common_top_controls->parentWidget();
    QWidget *selectorRow = page && page->parentWidget()
        ? page->parentWidget()->parentWidget()
        : nullptr;
    auto *selectorRowLayout = selectorRow
        ? qobject_cast<QHBoxLayout *>(selectorRow->layout())
        : nullptr;
    auto *pageLayout = page
        ? qobject_cast<QHBoxLayout *>(page->layout())
        : nullptr;
    const bool commonPageActive = selectorRow && pageLayout &&
        channel.common_top_controls->isVisible() &&
        controller_mode_field_ && controller_mode_field_->parentWidget() == page;
    const bool sensorPageActive = selectorRow && pageLayout &&
        channel.sensor_model_field && channel.sensor_model_field->isVisible() &&
        controller_mode_field_ && controller_mode_field_->parentWidget() == page;

    int rowSpacing = 12;
    int pageSpacing = 12;
    int middleGap = 0;
    if (commonPageActive)
    {
        const int availableWidth = std::max(0, selectorRow->width() -
                                                channel_top_bar_->width() -
                                                enableFieldWidth -
                                                autoPidFieldWidth -
                                                controllerModeFieldWidth);
        const int baseGap = availableWidth / 3;
        const int extraGapPixels = availableWidth % 3;
        rowSpacing = baseGap + (extraGapPixels > 0 ? 1 : 0);
        middleGap = baseGap + (extraGapPixels > 1 ? 1 : 0);
        pageSpacing = baseGap;
        channel.common_top_controls->setFixedWidth(enableFieldWidth + autoPidFieldWidth + middleGap);
    }
    else if (sensorPageActive)
    {
        const int sensorModelFieldWidth = currentFieldWidth(channel.sensor_model_field);
        const int availableWidth = std::max(0, selectorRow->width() -
                                                channel_top_bar_->width() -
                                                sensorModelFieldWidth -
                                                controllerModeFieldWidth);
        rowSpacing = availableWidth / 2;
        pageSpacing = rowSpacing + (availableWidth % 2);
    }

    channel.common_top_leading_spacer->setFixedWidth(0);
    channel.common_top_middle_spacer->setFixedWidth(middleGap);
    channel.common_top_mode_spacer->setFixedWidth(0);
    if (selectorRowLayout)
    {
        selectorRowLayout->setSpacing(rowSpacing);
    }
    if (pageLayout)
    {
        pageLayout->setSpacing(pageSpacing);
    }
    if (QLayout *layout = channel.common_top_controls->layout())
    {
        layout->invalidate();
        layout->activate();
    }
}

void TemperatureControllerPanel::fitControllerModeComboWidth()
{
    fitTemperatureComboWidth(controller_mode_combo_);
}

void TemperatureControllerPanel::placeControllerModeFieldInTopControls(int channelIndex, int subPageIndex)
{
    if (!controller_mode_field_)
    {
        return;
    }

    QWidget *target = nullptr;
    QWidget *anchor = nullptr;
    if (selected_config_page_index_ < 2 &&
        channelIndex >= 0 &&
        channelIndex < static_cast<int>(channels_.size()))
    {
        ChannelWidgets& channel = channels_[channelIndex];
        if (subPageIndex == 0)
        {
            target = channel.common_top_controls->parentWidget();
            anchor = channel.common_top_controls;
        }
        else if (subPageIndex == 2)
        {
            target = channel.sensor_model_field->parentWidget();
            anchor = channel.sensor_model_field;
        }
    }
    if (!target)
    {
        target = controller_mode_top_controls_;
    }
    if (!target || !anchor)
    {
        if (!target)
        {
            controller_mode_field_->setVisible(false);
            return;
        }
    }

    auto *targetLayout = qobject_cast<QHBoxLayout *>(target->layout());
    if (!targetLayout)
    {
        controller_mode_field_->setVisible(false);
        return;
    }

    if (QWidget *previousParent = controller_mode_field_->parentWidget();
        previousParent && previousParent != target)
    {
        if (QLayout *previousLayout = previousParent->layout())
        {
            const int previousIndex = previousLayout->indexOf(controller_mode_field_);
            if (previousIndex >= 0)
            {
                delete previousLayout->takeAt(previousIndex);
            }
        }
    }

    int desiredIndex = anchor ? targetLayout->indexOf(anchor) + 1 : targetLayout->count();
    if (anchor && desiredIndex <= 0)
    {
        desiredIndex = targetLayout->count();
    }

    const int existingIndex = targetLayout->indexOf(controller_mode_field_);
    if (existingIndex >= 0)
    {
        if (existingIndex == desiredIndex)
        {
            controller_mode_field_->setVisible(true);
            return;
        }
        delete targetLayout->takeAt(existingIndex);
        if (existingIndex < desiredIndex)
        {
            --desiredIndex;
        }
    }

    targetLayout->insertWidget(desiredIndex, controller_mode_field_, 0, Qt::AlignVCenter | Qt::AlignRight);
    controller_mode_field_->setVisible(true);
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
    auto *commonSettingsGrid = commonSettingsPage
        ? qobject_cast<QGridLayout *>(commonSettingsPage->layout())
        : nullptr;
    if (!commonSettingsPage || !commonSettingsGrid)
    {
        return;
    }

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

    title_status_strip_ = new QWidget(this);
    title_status_strip_->setObjectName(QStringLiteral("temperatureTitleStatusStrip"));
    title_status_strip_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    title_status_strip_->setFixedHeight(28);
    title_status_strip_->setVisible(false);
    auto *statusLayout = new QHBoxLayout(title_status_strip_);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(10);
    internal_temperature_lbl_ = new QLabel(this);
    internal_temperature_lbl_->setObjectName(QStringLiteral("fieldLabel"));
    internal_temperature_lbl_->setProperty("temperatureControllerInternalTemperatureTitle", true);
    internal_temperature_lbl_->setMinimumHeight(22);
    setFixedTextLabelWidth(internal_temperature_lbl_, temperatureControllerCompactStatusLabelWidthCandidates(), 4);
    internal_temperature_label_ = new QLabel(QStringLiteral("--- °C"), this);
    internal_temperature_label_->setObjectName(QStringLiteral("highlightedValue"));
    internal_temperature_label_->setProperty("temperatureControllerInternalTemperatureValue", true);
    internal_temperature_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    internal_temperature_label_->setMinimumHeight(22);
    setFixedNumericLabelWidth(internal_temperature_label_,
                              {QStringLiteral(" -999.99 °C"), QStringLiteral("     --- °C")},
                              4);
    error_code_lbl_ = new QLabel(this);
    error_code_lbl_->setObjectName(QStringLiteral("fieldLabel"));
    error_code_lbl_->setProperty("temperatureControllerErrorCodeTitle", true);
    error_code_lbl_->setMinimumHeight(22);
    setFixedTextLabelWidth(error_code_lbl_, temperatureControllerCompactStatusLabelWidthCandidates(), 4);
    error_code_label_ = new QLabel(QStringLiteral("0x0000"), this);
    error_code_label_->setObjectName(QStringLiteral("highlightedValue"));
    error_code_label_->setProperty("temperatureControllerErrorCodeValue", true);
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
    controller_mode_lbl_->setFixedHeight(36);
    controller_mode_lbl_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    controller_mode_combo_ = new SingleLevelPopupComboBox(this);
    configureSingleLevelComboCheckIcon(static_cast<SingleLevelPopupComboBox *>(controller_mode_combo_));
    controller_mode_combo_->setObjectName(QStringLiteral("temperatureControllerModeCombo"));
    static_cast<SingleLevelPopupComboBox *>(controller_mode_combo_)->setPopupFitContents(true);
    controller_mode_combo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    controller_mode_combo_->addItem(QStringLiteral("独立控制"), 0);
    controller_mode_combo_->addItem(QStringLiteral("通道1温差控制"), 1);
    controller_mode_combo_->addItem(QStringLiteral("通道2跟随输出"), 2);
    controller_mode_combo_->addItem(QStringLiteral("温差控制+跟随输出"), 3);
    fitControllerModeComboWidth();
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

    auto makeTitleStatusField = [this, statusLayout](const QString& objectName, QLabel *label, QLabel *value) {
        auto *field = new QWidget(title_status_strip_);
        field->setObjectName(objectName);
        field->setProperty("temperatureTitleStatusField", true);
        field->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        field->setFixedHeight(24);
        auto *fieldLayout = new QHBoxLayout(field);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(4);
        fieldLayout->addWidget(label, 0, Qt::AlignVCenter | Qt::AlignLeft);
        fieldLayout->addWidget(value, 0, Qt::AlignVCenter | Qt::AlignLeft);
        statusLayout->addWidget(field, 0, Qt::AlignVCenter | Qt::AlignLeft);
    };
    makeTitleStatusField(QStringLiteral("temperatureTitleInternalTemperatureField"),
                         internal_temperature_lbl_,
                         internal_temperature_label_);
    makeTitleStatusField(QStringLiteral("temperatureTitleErrorCodeField"),
                         error_code_lbl_,
                         error_code_label_);
    makeTitleStatusField(QStringLiteral("temperatureTitlePollingRateField"),
                         rate_title_lbl_,
                         rate_label_);

    controller_mode_field_ = new QWidget(this);
    controller_mode_field_->setObjectName(QStringLiteral("temperatureControllerModeField"));
    controller_mode_field_->setAttribute(Qt::WA_StyledBackground, true);
    controller_mode_field_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    controller_mode_field_->setFixedHeight(kTemperatureControllerTopControlsHeight);
    auto *controllerModeLayout = new QHBoxLayout(controller_mode_field_);
    controllerModeLayout->setContentsMargins(0, 0, 0, 0);
    controllerModeLayout->setSpacing(kTemperatureControllerInlineLabelSpacing);
    controllerModeLayout->addWidget(controller_mode_lbl_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    controllerModeLayout->addWidget(controller_mode_combo_, 0, Qt::AlignVCenter | Qt::AlignLeft);

    auto *configCard = new QFrame(this);
    configCard->setObjectName(QStringLiteral("temperatureConfigCard"));
    configCard->setFrameShape(QFrame::NoFrame);
    configCard->setAttribute(Qt::WA_StyledBackground, true);
    configCard->setMinimumWidth(0);
    configCard->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    auto *configCardLayout = new QVBoxLayout(configCard);
    configCardLayout->setContentsMargins(12, 12, 12, kTemperatureControllerCompactColumnGap);
    configCardLayout->setSpacing(kTemperatureControllerCompactColumnGap);

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
        button->setFixedSize(kTemperatureControllerChannelButtonWidth,
                             kTemperatureControllerNavigationButtonHeight);
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
    common_settings_button_->setFixedSize(kTemperatureControllerCommonButtonWidth,
                                          kTemperatureControllerNavigationButtonHeight);
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
    controller_mode_top_controls_ = new QWidget(channel_top_controls_stack_);
    controller_mode_top_controls_->setObjectName(QStringLiteral("temperatureControllerModeTopControls"));
    controller_mode_top_controls_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    controller_mode_top_controls_->setFixedHeight(kTemperatureControllerTopControlsHeight);
    auto *controllerModeTopLayout = new QHBoxLayout(controller_mode_top_controls_);
    controllerModeTopLayout->setContentsMargins(0, 0, 0, 0);
    controllerModeTopLayout->setSpacing(0);
    controllerModeTopLayout->addStretch(1);
    channel_top_controls_stack_->addWidget(controller_mode_top_controls_);
    channelSelectorRowLayout->addWidget(channel_top_controls_stack_, 1, Qt::AlignVCenter);
    channelTopRowLayout->addWidget(channelSelectorRow);
    configCardLayout->addWidget(channelTopRow, 0);

    auto *contentRow = new QWidget(configCard);
    contentRow->setObjectName(QStringLiteral("temperatureControllerContentRow"));
    contentRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    contentRow->setFixedHeight(kTemperatureControllerConfigPlotHeight);
    auto *contentRowLayout = new QHBoxLayout(contentRow);
    contentRowLayout->setContentsMargins(0, 0, 0, 0);
    contentRowLayout->setSpacing(12);

    auto *leftConfigColumn = new QWidget(contentRow);
    leftConfigColumn->setObjectName(QStringLiteral("temperatureControllerLeftConfigColumn"));
    leftConfigColumn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    leftConfigColumn->setFixedWidth(kTemperatureControllerControlsCardWidth);
    auto *leftConfigColumnLayout = new QVBoxLayout(leftConfigColumn);
    leftConfigColumnLayout->setContentsMargins(0, 0, 0, 0);
    leftConfigColumnLayout->setSpacing(kTemperatureControllerCompactColumnGap);

    auto *controlsCard = new QFrame(leftConfigColumn);
    controlsCard->setObjectName(QStringLiteral("temperatureControllerControlsCard"));
    controlsCard->setFrameShape(QFrame::NoFrame);
    controlsCard->setAttribute(Qt::WA_StyledBackground, true);
    controlsCard->setFixedWidth(kTemperatureControllerControlsCardWidth);
    controlsCard->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *controlsCardLayout = new QVBoxLayout(controlsCard);
    controlsCardLayout->setContentsMargins(kTemperatureControllerControlsCardHorizontalPadding,
                                           kTemperatureControllerControlsCardTopPadding,
                                           kTemperatureControllerControlsCardHorizontalPadding,
                                           kTemperatureControllerControlsCardBottomPadding);
    controlsCardLayout->setSpacing(0);

    channel_stack_ = new QStackedWidget(controlsCard);
    channel_stack_->setObjectName(QStringLiteral("temperatureChannelStack"));
    channel_stack_->setFrameShape(QFrame::NoFrame);
    channel_stack_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    channel_stack_->addWidget(createChannelPage(0));
    channel_stack_->addWidget(createChannelPage(1));
    channel_stack_->addWidget(createCommonSettingsPage());
    controlsCardLayout->addWidget(channel_stack_, 0);

    leftConfigColumnLayout->addWidget(controlsCard, 0);
    contentRowLayout->addWidget(leftConfigColumn, 0, Qt::AlignTop);

    temperature_plot_container_ = new QWidget(contentRow);
    temperature_plot_container_->setObjectName(QStringLiteral("temperatureConfigPlotContainer"));
    temperature_plot_container_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    temperature_plot_container_->setFixedHeight(kTemperatureControllerConfigPlotHeight);
    auto *temperaturePlotLayout = new QGridLayout(temperature_plot_container_);
    temperaturePlotLayout->setContentsMargins(0, 0, 0, 0);
    temperaturePlotLayout->setSpacing(0);

    temperature_plot_ = new TemperatureTrendPlotWidget(temperature_plot_container_);
    temperature_plot_->setProperty("temperatureConfigPlot", true);
    temperature_plot_->setCompactMode(true);
    temperature_plot_->setTimeAxisEnabled(true);
    temperature_plot_->setFixedHeight(kTemperatureControllerConfigPlotHeight);
    temperature_plot_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    temperaturePlotLayout->addWidget(temperature_plot_, 0, 0);
    contentRowLayout->addWidget(temperature_plot_container_, 1, Qt::AlignTop);

    configCardLayout->addWidget(contentRow, 0);
    sub_page_bar_stack_ = new QStackedWidget(leftConfigColumn);
    sub_page_bar_stack_->setObjectName(QStringLiteral("temperatureSubPageBarStack"));
    sub_page_bar_stack_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    sub_page_bar_stack_->addWidget(channels_[0].sub_page_row);
    sub_page_bar_stack_->addWidget(channels_[1].sub_page_row);
    sub_page_bar_stack_->addWidget(common_.sub_top_bar);
    leftConfigColumnLayout->addWidget(sub_page_bar_stack_, 0, Qt::AlignLeft | Qt::AlignTop);

    updateChannelStackMinimumHeight();
    selectChannel(0);
    layout->addWidget(configCard, 0);

    status_label_ = new QLabel(this);
    status_label_->setObjectName(QStringLiteral("fieldLabel"));
    status_label_->setMinimumHeight(20);
    status_label_->setWordWrap(true);
    layout->addWidget(status_label_);
    for (QLabel *fieldLabel : findChildren<QLabel *>(QStringLiteral("fieldLabel")))
    {
        if (fieldLabel != status_label_)
        {
            VaporView::configureSelectableCardTitle(fieldLabel);
        }
    }
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
        fieldLayout->setSpacing(kTemperatureControllerInlineLabelSpacing);
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
    auto *enableSwitch = static_cast<TemperatureOverviewSwitchButton *>(channel.enable_switch);
    connect(enableSwitch, &SegmentedSwitchButton::selectionRequested, this, [this, channelNumber](bool requested) {
        emit outputEnabledRequested(channelNumber, requested);
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
    configureSingleLevelComboCheckIcon(autoPidCombo);
    autoPidCombo->setShowSelectionCheck(false);
    channel.auto_pid_combo = autoPidCombo;
    channel.auto_pid_combo->setObjectName(QStringLiteral("temperatureAutoPidComboChannel%1").arg(index + 1));
    channel.auto_pid_combo->setFixedWidth(kTemperatureControllerCompactInputWidth);
    channel.auto_pid_combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    channel.auto_pid_combo->addItem(QStringLiteral("关闭"), 0);
    channel.auto_pid_combo->addItem(QStringLiteral("PID自整定"), 1);
    channel.auto_pid_combo->addItem(QStringLiteral("实时优化(预留)"), 2);
    fitTemperatureComboWidth(channel.auto_pid_combo, kTemperatureControllerAutoPidTextWidthReserve);
    channel.auto_pid_field = makeCommonTopField(
        QStringLiteral("自动 PID"), channel.auto_pid_combo, channel.auto_pid_label_text);
    commonLayout->addWidget(channel.auto_pid_field, 0, Qt::AlignVCenter);
    channel.common_top_mode_spacer = new QWidget(channel.common_top_controls);
    channel.common_top_mode_spacer->setObjectName(
        QStringLiteral("temperatureTopModeSpacerChannel%1").arg(index + 1));
    channel.common_top_mode_spacer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    channel.common_top_mode_spacer->setFixedSize(0, kTemperatureControllerTopControlsHeight);
    commonLayout->addWidget(channel.common_top_mode_spacer, 0, Qt::AlignVCenter);
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
    channel.sensor_config_top_bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
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
        button->setMinimumWidth(std::max(kTemperatureControllerSubTabMinimumWidth,
                                         button->fontMetrics().horizontalAdvance(button->text()) +
                                             kTemperatureControllerSubTabTextPadding));
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    };
    auto makeTabButton = [this, index, bar = channel.sensor_config_top_bar, fitSubTabButtonWidth](const QString& text, int pageIndex) {
        auto *button = new QPushButton(text, bar);
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::TabFocus);
        button->setProperty("temperatureChannelSubSelector", true);
        button->setMinimumHeight(kTemperatureControllerNavigationButtonHeight);
        button->setMaximumHeight(kTemperatureControllerNavigationButtonHeight);
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
    barLayout->addWidget(channel.common_params_button, 1);
    barLayout->addWidget(channel.advanced_params_button, 1);
    barLayout->addWidget(channel.sensor_config_button, 1);
    channel.sensor_config_top_bar->setMinimumWidth(channel.sensor_config_top_bar->sizeHint().width());

    auto *subPageRow = new QWidget(page);
    subPageRow->setObjectName(QStringLiteral("temperatureChannelSubPageRowChannel%1").arg(index + 1));
    subPageRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *subPageRowLayout = new QHBoxLayout(subPageRow);
    subPageRowLayout->setContentsMargins(0, 0, 0, 0);
    subPageRowLayout->setSpacing(0);
    subPageRowLayout->addWidget(channel.sensor_config_top_bar, 1, Qt::AlignLeft | Qt::AlignVCenter);
    channel.sub_page_row = subPageRow;

    channel.config_sub_stack = new QStackedWidget(page);
    channel.config_sub_stack->setObjectName(QStringLiteral("temperatureChannelConfigSubStackChannel%1").arg(index + 1));
    channel.config_sub_stack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    channel.config_sub_stack->setFixedHeight(kTemperatureControllerChannelConfigSubStackHeight);

    channel.config_sub_stack->addWidget(createChannelCommonParamsPage(index));
    channel.config_sub_stack->addWidget(createChannelAdvancedParamsPage(index));
    channel.config_sub_stack->addWidget(createChannelSensorConfigPage(index));
    layout->addWidget(channel.config_sub_stack, 0);
    selectChannelSubPage(index, 0);
    return page;
}

QWidget *TemperatureControllerPanel::createChannelCommonParamsPage(int index)
{
    QWidget *page = new QWidget(channels_[index].config_sub_stack);
    page->setObjectName(QStringLiteral("temperatureChannelCommonParamsPageChannel%1").arg(index + 1));
    auto *layout = new QGridLayout(page);
    layout->setContentsMargins(0, 0, 0, 6);
    layout->setHorizontalSpacing(kTemperatureControllerCompactColumnGap);
    layout->setVerticalSpacing(kTemperatureControllerChannelSubPageVerticalSpacing);
    layout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    ChannelWidgets& channel = channels_[index];

    auto makeFieldLabel = [page](const QString& text) {
        auto *label = new QLabel(text, page);
        label->setObjectName(QStringLiteral("fieldLabel"));
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setMinimumHeight(22);
        return label;
    };
    auto makeStackedField = [page, &makeFieldLabel](const QString& labelText,
                                                     QWidget *editor,
                                                     QLabel *&label,
                                                     int fieldWidth) {
        label = makeFieldLabel(labelText);
        editor->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        auto *cell = new QWidget(page);
        cell->setObjectName(QStringLiteral("temperatureConfigFieldColumn"));
        cell->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        cell->setFixedWidth(fieldWidth);
        auto *cellLayout = new QVBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(kTemperatureControllerStackedFieldSpacing);
        cellLayout->addWidget(label, 0, Qt::AlignLeft | Qt::AlignVCenter);
        cellLayout->addWidget(editor, 0, Qt::AlignLeft | Qt::AlignVCenter);
        return cell;
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
        spin->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    channel.kp_spin->setFixedWidth(kTemperatureControllerPidSideInputWidth);
    channel.ki_spin->setFixedWidth(kTemperatureControllerPidCenterInputWidth);
    channel.kd_spin->setFixedWidth(kTemperatureControllerPidSideInputWidth);
    QLabel *kpLabelText = nullptr;
    QLabel *kiLabelText = nullptr;
    QLabel *kdLabelText = nullptr;

    channel.mode_combo = new SingleLevelPopupComboBox(page);
    configureSingleLevelComboCheckIcon(static_cast<SingleLevelPopupComboBox *>(channel.mode_combo));
    channel.mode_combo->setObjectName(QStringLiteral("temperatureOutputModeComboChannel%1").arg(index + 1));
    channel.mode_combo->setFixedWidth(kTemperatureControllerChannelParameterInputWidth);
    channel.mode_combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    channel.mode_combo->addItem(QStringLiteral("制冷和加热"), 0);
    channel.mode_combo->addItem(QStringLiteral("制冷"), 1);
    channel.mode_combo->addItem(QStringLiteral("加热"), 2);
    channel.mode_combo->addItem(QStringLiteral("关闭"), 3);
    QWidget *modeField = makeStackedField(QStringLiteral("输出模式"),
                                          channel.mode_combo,
                                          channel.mode_label_text,
                                          kTemperatureControllerChannelParameterInputWidth);
    connect(channel.mode_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, channelNumber, combo = channel.mode_combo](int) {
        emit outputModeRequested(channelNumber, static_cast<quint16>(combo->currentData().toUInt()));
    });

    channel.target_spin = new QDoubleSpinBox(page);
    channel.target_spin->setObjectName(QStringLiteral("temperatureTargetSpinChannel%1").arg(index + 1));
    channel.target_spin->setRange(-40.0, 100.0);
    channel.target_spin->setDecimals(5);
    channel.target_spin->setSuffix(QStringLiteral(" °C"));
    channel.target_spin->setFixedWidth(kTemperatureControllerChannelParameterInputWidth);
    QWidget *targetField = makeStackedField(QStringLiteral("目标温度(°C)"),
                                            channel.target_spin,
                                            channel.target_label_text,
                                            kTemperatureControllerChannelParameterInputWidth);
    connect(channel.target_spin, &QDoubleSpinBox::editingFinished, this, [this, channelNumber, spin = channel.target_spin]() {
        emit targetTemperatureRequested(channelNumber, spin->value());
    });

    channel.max_output_spin = new QSpinBox(this);
    channel.max_output_spin->setObjectName(QStringLiteral("temperatureMaxOutputSpinChannel%1").arg(index + 1));
    setWidgetBooleanProperty(channel.max_output_spin, "temperatureMaxOutputWarning", true);
    setDangerTextPalette(channel.max_output_spin);
    channel.max_output_spin->setRange(0, 90);
    channel.max_output_spin->setSuffix(QStringLiteral(" %"));
    channel.max_output_spin->setFixedWidth(kTemperatureControllerChannelParameterInputWidth);
    QWidget *maxOutputField = makeStackedField(QStringLiteral("最大输出电压百分比(%)"),
                                               channel.max_output_spin,
                                               channel.max_output_label_text,
                                               kTemperatureControllerChannelParameterInputWidth);
    maxOutputField->setFixedWidth(kTemperatureControllerChannelParameterRowWidth);
    if (channel.max_output_label_text)
    {
        setWidgetBooleanProperty(channel.max_output_label_text, "temperatureMaxOutputWarning", true);
        setDangerTextPalette(channel.max_output_label_text);
    }

    QWidget *kpField = makeStackedField(QStringLiteral("P"),
                                        channel.kp_spin,
                                        kpLabelText,
                                        kTemperatureControllerPidSideInputWidth);
    QWidget *kiField = makeStackedField(QStringLiteral("I"),
                                        channel.ki_spin,
                                        kiLabelText,
                                        kTemperatureControllerPidCenterInputWidth);
    QWidget *kdField = makeStackedField(QStringLiteral("D"),
                                        channel.kd_spin,
                                        kdLabelText,
                                        kTemperatureControllerPidSideInputWidth);

    auto *pidFields = new QWidget(page);
    pidFields->setObjectName(QStringLiteral("temperaturePidFieldsChannel%1").arg(index + 1));
    pidFields->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    pidFields->setFixedWidth(kTemperatureControllerPidSideInputWidth * 2 +
                             kTemperatureControllerPidCenterInputWidth +
                             kTemperatureControllerCompactColumnGap * 2);
    auto *pidFieldsLayout = new QHBoxLayout(pidFields);
    pidFieldsLayout->setContentsMargins(0, 0, 0, 0);
    pidFieldsLayout->setSpacing(kTemperatureControllerCompactColumnGap);
    pidFieldsLayout->addWidget(kpField, 0, Qt::AlignLeft | Qt::AlignTop);
    pidFieldsLayout->addWidget(kiField, 0, Qt::AlignLeft | Qt::AlignTop);
    pidFieldsLayout->addWidget(kdField, 0, Qt::AlignLeft | Qt::AlignTop);

    auto *outputTargetFields = new QWidget(page);
    outputTargetFields->setObjectName(QStringLiteral("temperatureOutputTargetFieldsChannel%1").arg(index + 1));
    outputTargetFields->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    outputTargetFields->setFixedWidth(kTemperatureControllerChannelParameterRowWidth);
    auto *outputTargetFieldsLayout = new QHBoxLayout(outputTargetFields);
    outputTargetFieldsLayout->setContentsMargins(0, 0, 0, 0);
    outputTargetFieldsLayout->setSpacing(kTemperatureControllerCompactColumnGap);
    outputTargetFieldsLayout->addWidget(modeField, 0, Qt::AlignLeft | Qt::AlignTop);
    outputTargetFieldsLayout->addWidget(targetField, 0, Qt::AlignLeft | Qt::AlignTop);

    layout->addWidget(pidFields, 0, 0, 1, 3, Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(outputTargetFields, 1, 0, 1, 3, Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(maxOutputField, 2, 0, 1, 2, Qt::AlignLeft | Qt::AlignTop);

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
    layout->setContentsMargins(0, 0, 0, 6);
    layout->setHorizontalSpacing(kTemperatureControllerCompactColumnGap);
    layout->setVerticalSpacing(kTemperatureControllerChannelSubPageVerticalSpacing);
    layout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    ChannelWidgets& channel = channels_[index];

    auto addField = [page, layout](int row,
                                   int column,
                                   const QString& labelText,
                                   QWidget *editor,
                                   QLabel *&label) {
        label = new QLabel(labelText, page);
        label->setObjectName(QStringLiteral("fieldLabel"));
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setMinimumHeight(22);
        editor->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        auto *cell = new QWidget(page);
        cell->setObjectName(QStringLiteral("temperatureConfigFieldColumn"));
        cell->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        cell->setFixedWidth(kTemperatureControllerChannelParameterInputWidth);
        auto *cellLayout = new QVBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(kTemperatureControllerStackedFieldSpacing);
        cellLayout->addWidget(label, 0, Qt::AlignLeft | Qt::AlignVCenter);
        cellLayout->addWidget(editor, 0, Qt::AlignLeft | Qt::AlignVCenter);
        layout->addWidget(cell, row, column, Qt::AlignLeft | Qt::AlignTop);
    };

    const quint8 channelNumber = static_cast<quint8>(index + 1);

    channel.overtemp_upper_spin = new QDoubleSpinBox(page);
    channel.overtemp_upper_spin->setObjectName(QStringLiteral("temperatureOvertempUpperSpinChannel%1").arg(index + 1));
    channel.overtemp_upper_spin->setRange(-3000.0, 5000.0);
    channel.overtemp_upper_spin->setDecimals(5);
    channel.overtemp_upper_spin->setSingleStep(0.00001);
    channel.overtemp_upper_spin->setFixedWidth(kTemperatureControllerChannelParameterInputWidth);
    addField(0, 0, QStringLiteral("高温报警值(°C)"), channel.overtemp_upper_spin, channel.overtemp_upper_label_text);
    setDangerTextPalette(channel.overtemp_upper_label_text);

    channel.overtemp_lower_spin = new QDoubleSpinBox(page);
    channel.overtemp_lower_spin->setObjectName(QStringLiteral("temperatureOvertempLowerSpinChannel%1").arg(index + 1));
    channel.overtemp_lower_spin->setRange(-3000.0, 5000.0);
    channel.overtemp_lower_spin->setDecimals(5);
    channel.overtemp_lower_spin->setSingleStep(0.00001);
    channel.overtemp_lower_spin->setFixedWidth(kTemperatureControllerChannelParameterInputWidth);
    addField(0, 1, QStringLiteral("低温报警值(°C)"), channel.overtemp_lower_spin, channel.overtemp_lower_label_text);
    setDangerTextPalette(channel.overtemp_lower_label_text);

    channel.temperature_slope_spin = new QDoubleSpinBox(page);
    channel.temperature_slope_spin->setObjectName(QStringLiteral("temperatureSlopeSpinChannel%1").arg(index + 1));
    channel.temperature_slope_spin->setRange(0.0, 10.0);
    channel.temperature_slope_spin->setDecimals(3);
    channel.temperature_slope_spin->setSingleStep(0.001);
    channel.temperature_slope_spin->setFixedWidth(kTemperatureControllerChannelParameterInputWidth);
    addField(1, 0, QStringLiteral("温度变化速率(°C/s)"), channel.temperature_slope_spin, channel.temperature_slope_label_text);

    channel.startup_delay_spin = new QSpinBox(page);
    channel.startup_delay_spin->setObjectName(QStringLiteral("temperatureStartupDelaySpinChannel%1").arg(index + 1));
    channel.startup_delay_spin->setRange(3, 180);
    channel.startup_delay_spin->setSuffix(QStringLiteral(" s"));
    channel.startup_delay_spin->setFixedWidth(kTemperatureControllerChannelParameterInputWidth);
    addField(1, 1, QStringLiteral("开机输出延时(s)"), channel.startup_delay_spin, channel.startup_delay_label_text);

    channel.sensor_resistance_edit = new QLineEdit(page);
    channel.sensor_resistance_edit->setObjectName(QStringLiteral("temperatureSensorResistanceEditChannel%1").arg(index + 1));
    channel.sensor_resistance_edit->setReadOnly(true);
    channel.sensor_resistance_edit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    channel.sensor_resistance_edit->setFixedWidth(kTemperatureControllerChannelParameterInputWidth);
    channel.sensor_resistance_edit->setText(QStringLiteral("---"));
    addField(2, 0, QStringLiteral("传感器电阻(Ω)"), channel.sensor_resistance_edit, channel.sensor_resistance_label_text);

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
    page->setProperty("temperatureSensorCalibrationLayoutHost", true);
    page->installEventFilter(this);
    auto *layout = new QGridLayout(page);
    layout->setContentsMargins(0, 0, 0, 6);
    layout->setHorizontalSpacing(kTemperatureControllerCompactColumnGap);
    layout->setVerticalSpacing(kTemperatureControllerChannelSubPageVerticalSpacing);
    layout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    ChannelWidgets& channel = channels_[index];

    auto makeFieldLabel = [page](const QString& text) {
        auto *label = new QLabel(text, page);
        label->setObjectName(QStringLiteral("fieldLabel"));
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setMinimumHeight(22);
        return label;
    };
    auto makeFieldCell = [page, &makeFieldLabel](const QString& labelText,
                                                 QWidget *editor,
                                                 QLabel *&label,
                                                 int fieldWidth) {
        label = makeFieldLabel(labelText);
        editor->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        auto *cell = new QWidget(page);
        cell->setObjectName(QStringLiteral("temperatureConfigFieldRow"));
        cell->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        cell->setFixedWidth(fieldWidth);
        auto *cellLayout = new QVBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(kTemperatureControllerStackedFieldSpacing);
        cellLayout->addWidget(label, 0, Qt::AlignLeft | Qt::AlignVCenter);
        cellLayout->addWidget(editor, 0, Qt::AlignLeft | Qt::AlignVCenter);
        return cell;
    };
    auto addFieldToLayout = [&makeFieldCell](QGridLayout *targetLayout,
                                             int row,
                                             int column,
                                             const QString& labelText,
                                             QWidget *editor,
                                             QLabel *&label,
                                             int fieldWidth) {
        QWidget *cell = makeFieldCell(labelText, editor, label, fieldWidth);
        targetLayout->addWidget(cell, row, column, Qt::AlignLeft | Qt::AlignTop);
    };
    auto addFieldToRow = [&makeFieldCell](QHBoxLayout *targetLayout,
                                          const QString& labelText,
                                          QWidget *editor,
                                          QLabel *&label,
                                          int fieldWidth) {
        QWidget *cell = makeFieldCell(labelText, editor, label, fieldWidth);
        targetLayout->addWidget(cell, 0, Qt::AlignLeft | Qt::AlignTop);
    };
    auto makeFieldRow = [page, index](const QString& objectName, int fixedWidth) {
        auto *row = new QWidget(page);
        row->setObjectName(QStringLiteral("%1Channel%2").arg(objectName).arg(index + 1));
        row->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        row->setFixedWidth(fixedWidth);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(kTemperatureControllerCompactColumnGap);
        return std::pair<QWidget *, QHBoxLayout *>(row, rowLayout);
    };
    auto makeIntegerEdit = [page](const QString& name, int min, int max, int width) {
        auto *edit = new QLineEdit(page);
        edit->setObjectName(name);
        edit->setFixedWidth(width);
        edit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        edit->setValidator(new QIntValidator(min, max, edit));
        return edit;
    };
    auto makeDecimalEdit = [page](const QString& name, double min, double max, int decimals, int width) {
        auto *edit = new QLineEdit(page);
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

    auto [ntcFields, ntcFieldsLayout] = makeFieldRow(
        QStringLiteral("temperatureNtcFields"),
        kTemperatureControllerStackedWideFieldWidth * 2 + kTemperatureControllerCompactColumnGap);
    auto [ptR0Fields, ptR0FieldsLayout] = makeFieldRow(
        QStringLiteral("temperaturePtR0Fields"),
        kTemperatureControllerStackedWideFieldWidth * 2 + kTemperatureControllerCompactColumnGap);
    auto [ptBFields, ptBFieldsLayout] = makeFieldRow(
        QStringLiteral("temperaturePtBFields"),
        kTemperatureControllerStackedWideFieldWidth * 2 + kTemperatureControllerCompactColumnGap);

    channel.ntc_r0_edit = makeIntegerEdit(QStringLiteral("temperatureNtcR0EditChannel%1").arg(index + 1),
                                          0,
                                          9000000,
                                          kTemperatureControllerStackedWideFieldWidth);
    channel.ntc_r0_edit->setText(QStringLiteral("0"));
    addFieldToRow(ntcFieldsLayout,
                  QStringLiteral("NTC R0(Ohm)"),
                  channel.ntc_r0_edit,
                  channel.ntc_r0_label_text,
                  kTemperatureControllerStackedWideFieldWidth);

    channel.ntc_b_edit = makeDecimalEdit(QStringLiteral("temperatureNtcBEditChannel%1").arg(index + 1),
                                         1000.0,
                                         50000.0,
                                         2,
                                         kTemperatureControllerStackedWideFieldWidth);
    channel.ntc_b_edit->setText(QStringLiteral("1000.00"));
    addFieldToRow(ntcFieldsLayout,
                  QStringLiteral("NTC B"),
                  channel.ntc_b_edit,
                  channel.ntc_b_label_text,
                  kTemperatureControllerStackedWideFieldWidth);
    layout->addWidget(ntcFields, 0, 0, 1, 2, Qt::AlignLeft | Qt::AlignTop);

    channel.pt_r0_edit = makeDecimalEdit(QStringLiteral("temperaturePtR0EditChannel%1").arg(index + 1),
                                         0.0,
                                         10000.0,
                                         3,
                                         kTemperatureControllerStackedWideFieldWidth);
    channel.pt_r0_edit->setText(QStringLiteral("0.000"));
    addFieldToRow(ptR0FieldsLayout,
                  QStringLiteral("PT R0(Ohm)"),
                  channel.pt_r0_edit,
                  channel.pt_r0_label_text,
                  kTemperatureControllerStackedWideFieldWidth);

    channel.pt_a_edit = makeDecimalEdit(QStringLiteral("temperaturePtAEditChannel%1").arg(index + 1),
                                        -9.0,
                                        9.0,
                                        6,
                                        kTemperatureControllerStackedWideFieldWidth);
    addFieldToRow(ptR0FieldsLayout,
                  QStringLiteral("PT A(E-3)"),
                  channel.pt_a_edit,
                  channel.pt_a_label_text,
                  kTemperatureControllerStackedWideFieldWidth);
    layout->addWidget(ptR0Fields, 1, 0, 1, 2, Qt::AlignLeft | Qt::AlignTop);

    channel.pt_b_edit = makeDecimalEdit(QStringLiteral("temperaturePtBEditChannel%1").arg(index + 1),
                                        -90.0,
                                        90.0,
                                        6,
                                        kTemperatureControllerStackedWideFieldWidth);
    addFieldToRow(ptBFieldsLayout,
                  QStringLiteral("PT B(E-7)"),
                  channel.pt_b_edit,
                  channel.pt_b_label_text,
                  kTemperatureControllerStackedWideFieldWidth);

    channel.pt_c_edit = makeDecimalEdit(QStringLiteral("temperaturePtCEditChannel%1").arg(index + 1),
                                        -9.0,
                                        9.0,
                                        6,
                                        kTemperatureControllerStackedWideFieldWidth);
    addFieldToRow(ptBFieldsLayout,
                  QStringLiteral("PT C(E-12)"),
                  channel.pt_c_edit,
                  channel.pt_c_label_text,
                  kTemperatureControllerStackedWideFieldWidth);
    layout->addWidget(ptBFields, 2, 0, 1, 2, Qt::AlignLeft | Qt::AlignTop);

    auto *polynomialFields = new QFrame(page);
    polynomialFields->setObjectName(QStringLiteral("temperaturePolynomialFieldsChannel%1").arg(index + 1));
    polynomialFields->setProperty("temperatureSensorCalibrationOverlay", true);
    polynomialFields->setFrameShape(QFrame::NoFrame);
    polynomialFields->setAttribute(Qt::WA_StyledBackground, true);
    polynomialFields->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    polynomialFields->setMinimumWidth(kTemperatureControllerPolynomialStackedFieldWidth *
                                      kTemperatureControllerPolynomialColumnCount +
                                      kTemperatureControllerCompactColumnGap *
                                          (kTemperatureControllerPolynomialColumnCount - 1) +
                                      2);
    auto *polynomialLayout = new QGridLayout(polynomialFields);
    polynomialLayout->setObjectName(QStringLiteral("temperaturePolynomialFieldsGridChannel%1").arg(index + 1));
    polynomialLayout->setContentsMargins(kTemperatureControllerCompactColumnGap,
                                         0,
                                         kTemperatureControllerCompactColumnGap,
                                         0);
    polynomialLayout->setHorizontalSpacing(kTemperatureControllerCompactColumnGap);
    polynomialLayout->setVerticalSpacing(kTemperatureControllerChannelSubPageVerticalSpacing);
    polynomialLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    for (int i = 0; i < 8; ++i)
    {
        auto *edit = new QLineEdit(page);
        edit->setObjectName(QStringLiteral("temperaturePolynomialA%1EditChannel%2").arg(i).arg(index + 1));
        edit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        edit->setText(QStringLiteral("0E+0"));
        channel.polynomial_edits[static_cast<size_t>(i)] = edit;
        edit->setFixedWidth(kTemperatureControllerPolynomialStackedFieldWidth);
        QLabel *label = nullptr;
        addFieldToLayout(polynomialLayout,
                         i / kTemperatureControllerPolynomialColumnCount,
                         i % kTemperatureControllerPolynomialColumnCount,
                         QStringLiteral("A%1").arg(i),
                         edit,
                         label,
                         kTemperatureControllerPolynomialStackedFieldWidth);
        channel.polynomial_label_text[static_cast<size_t>(i)] = label;
    }

    QWidget *drawerParent = page;
    if (channel_stack_ && channel_stack_->parentWidget())
    {
        drawerParent = channel_stack_->parentWidget();
    }
    auto *calibrationDrawer = new CalibrationSideDrawer(drawerParent);
    calibrationDrawer->setObjectName(QStringLiteral("temperatureCalibrationSideDrawerChannel%1").arg(index + 1));
    calibrationDrawer->setProperty("temperatureCalibrationSideDrawer", true);
    calibrationDrawer->setProperty("temperatureCalibrationChannel", index + 1);
    calibrationDrawer->setHandleText(QStringLiteral("校\n准\n系\n数\nA0\nA7"));
    calibrationDrawer->setContentWidget(polynomialFields);
    channel.sensor_config_page = page;
    channel.sensor_calibration_overlay = polynomialFields;
    channel.sensor_calibration_drawer = calibrationDrawer;
    calibrationDrawer->setVisible(false);
    polynomialFields->setVisible(false);
    auto *escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), page);
    escapeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escapeShortcut, &QShortcut::activated, this, [this, channelIndex = index]() {
        ChannelWidgets& channel = channels_[channelIndex];
        auto *drawer = static_cast<CalibrationSideDrawer *>(channel.sensor_calibration_drawer);
        if (drawer && drawer->isExpanded())
        {
            drawer->setExpanded(false);
            drawer->setFocus(Qt::OtherFocusReason);
        }
    });
    QTimer::singleShot(0, this, [this, channelIndex = index]() {
        alignSensorCalibrationOverlay(channelIndex);
    });

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
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 6);
    layout->setSpacing(kTemperatureControllerChannelSubPageVerticalSpacing);
    layout->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    auto *fieldsContainer = new QWidget(page);
    fieldsContainer->setObjectName(QStringLiteral("temperatureCommonSettingsFields"));
    fieldsContainer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *fieldsLayout = new QGridLayout(fieldsContainer);
    fieldsLayout->setObjectName(QStringLiteral("temperatureCommonSettingsFieldsGrid"));
    fieldsLayout->setContentsMargins(0, 0, 0, 0);
    fieldsLayout->setHorizontalSpacing(kTemperatureControllerCompactColumnGap);
    fieldsLayout->setVerticalSpacing(kTemperatureControllerChannelSubPageVerticalSpacing);
    fieldsLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    auto makeFieldLabel = [page](const QString& text) {
        auto *label = new QLabel(text, page);
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
        cell->setFixedWidth(kTemperatureControllerSettingsInputWidth);
        auto *cellLayout = new QVBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(kTemperatureControllerStackedFieldSpacing);
        cellLayout->addWidget(label, 0, Qt::AlignLeft | Qt::AlignVCenter);
        cellLayout->addWidget(editor, 0, Qt::AlignLeft | Qt::AlignVCenter);
        return cell;
    };

    common_.address_spin = new QSpinBox(page);
    common_.address_spin->setObjectName(QStringLiteral("temperatureDeviceAddressSpin"));
    common_.address_spin->setRange(1, 247);
    common_.address_spin->setFixedWidth(kTemperatureControllerSettingsInputWidth);
    QWidget *addressField =
        makeField(QStringLiteral("设置温控器485站号"), common_.address_spin, common_.address_label_text);

    common_.rs485_baud_combo = new QComboBox(page);
    common_.rs485_baud_combo->setObjectName(QStringLiteral("temperatureRs485BaudCombo"));
    common_.rs485_baud_combo->setFixedWidth(kTemperatureControllerSettingsInputWidth);
    common_.rs485_baud_combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    const QStringList& baudRates = VaporView::rd105BaudCapabilities().presets;
    for (int i = 0; i < baudRates.size(); ++i)
    {
        common_.rs485_baud_combo->addItem(baudRates.at(i), i);
    }
    QWidget *rs485BaudField =
        makeField(QStringLiteral("设置485串口波特率"), common_.rs485_baud_combo, common_.rs485_baud_label_text);

    common_.overtemp_output_combo = new SingleLevelPopupComboBox(this);
    configureSingleLevelComboCheckIcon(static_cast<SingleLevelPopupComboBox *>(common_.overtemp_output_combo));
    common_.overtemp_output_combo->setObjectName(QStringLiteral("temperatureOvertempOutputModeCombo"));
    common_.overtemp_output_combo->setFixedWidth(kTemperatureControllerSettingsInputWidth);
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
    common_.internal_temperature_edit->setFixedWidth(kTemperatureControllerSettingsInputWidth);
    QWidget *internalTemperatureField =
        makeField(QStringLiteral("温控器自身温度(°C)"), common_.internal_temperature_edit, common_.internal_temperature_label_text);

    fieldsLayout->addWidget(addressField, 0, 0, Qt::AlignLeft | Qt::AlignTop);
    fieldsLayout->addWidget(rs485BaudField, 0, 1, Qt::AlignLeft | Qt::AlignTop);
    fieldsLayout->addWidget(overtempOutputField, 1, 0, Qt::AlignLeft | Qt::AlignTop);
    fieldsLayout->addWidget(internalTemperatureField, 1, 1, Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(fieldsContainer, 0, Qt::AlignLeft | Qt::AlignTop);

    common_.factory_reset_button = new QPushButton(QStringLiteral("恢复出厂设置"), page);
    common_.factory_reset_button->setObjectName(QStringLiteral("temperatureFactoryResetButton"));
    common_.factory_reset_button->setCursor(Qt::PointingHandCursor);
    common_.factory_reset_button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    common_.factory_reset_button->setFixedSize(kTemperatureControllerFactoryResetButtonWidth, 40);
    common_.factory_reset_button->setIconSize(QSize(18, 18));
    common_.factory_reset_button->setIcon(createLucideIcon(QStringLiteral("refresh-cw"),
                                                           appThemeColor(AppThemeColor::ToolbarRed, VaporView::isDarkThemeEnabled())));
    connect(common_.factory_reset_button, &QPushButton::clicked, this, [this]() {
        emit factoryResetRequested();
    });
    layout->addWidget(common_.factory_reset_button, 0, Qt::AlignHCenter | Qt::AlignTop);

    common_.sub_top_bar = new QFrame(page);
    common_.sub_top_bar->setObjectName(QStringLiteral("temperatureCommonSettingsSubTopBar"));
    common_.sub_top_bar->setFrameShape(QFrame::NoFrame);
    common_.sub_top_bar->setAttribute(Qt::WA_StyledBackground, true);
    common_.sub_top_bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *subBarLayout = new QHBoxLayout(common_.sub_top_bar);
    subBarLayout->setContentsMargins(kTemperatureControllerNavigationHorizontalMargin,
                                     kTemperatureControllerNavigationVerticalMargin,
                                     kTemperatureControllerNavigationHorizontalMargin,
                                     kTemperatureControllerNavigationVerticalMargin);
    subBarLayout->setSpacing(kTemperatureControllerNavigationSpacing);
    auto fitSubTabButtonWidth = [](QPushButton *button) {
        if (!button)
        {
            return;
        }
        button->ensurePolished();
        button->setMinimumWidth(std::max(kTemperatureControllerSubTabMinimumWidth,
                                         button->fontMetrics().horizontalAdvance(button->text()) +
                                             kTemperatureControllerSubTabTextPadding));
        button->setMinimumHeight(kTemperatureControllerNavigationButtonHeight);
        button->setMaximumHeight(kTemperatureControllerNavigationButtonHeight);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    };
    auto makeCommonSubButton = [this, page, fitSubTabButtonWidth](const QString& text, int subPageIndex) {
        auto *button = new QPushButton(text, page);
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::TabFocus);
        button->setProperty("temperatureChannelSubSelector", true);
        fitSubTabButtonWidth(button);
        connect(button, &QPushButton::clicked, this, [this, subPageIndex]() {
            const int channelIndex = std::clamp(selected_channel_index_, 0, 1);
            selectChannel(channelIndex);
            selectChannelSubPage(channelIndex, subPageIndex);
        });
        return button;
    };
    common_.common_params_button = makeCommonSubButton(QStringLiteral("常用参数"), 0);
    common_.advanced_params_button = makeCommonSubButton(QStringLiteral("专业参数"), 1);
    common_.sensor_config_button = makeCommonSubButton(QStringLiteral("传感器配置"), 2);
    common_.common_params_button->setObjectName(QStringLiteral("temperatureCommonSettingsCommonParamsButton"));
    common_.advanced_params_button->setObjectName(QStringLiteral("temperatureCommonSettingsAdvancedParamsButton"));
    common_.sensor_config_button->setObjectName(QStringLiteral("temperatureCommonSettingsSensorConfigButton"));
    common_.common_params_button->setChecked(true);
    subBarLayout->addWidget(common_.common_params_button, 1);
    subBarLayout->addWidget(common_.advanced_params_button, 1);
    subBarLayout->addWidget(common_.sensor_config_button, 1);

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
    if (sub_page_bar_stack_)
    {
        sub_page_bar_stack_->setCurrentIndex(pageIndex);
    }
    if (channel_top_controls_stack_)
    {
        const int subPageIndex = std::clamp(selected_channel_sub_page_index_, 0, 2);
        if (pageIndex < 2 && subPageIndex != 1)
        {
            channel_top_controls_stack_->setCurrentIndex(channelIndex);
            if (channels_[channelIndex].config_sub_stack)
            {
                channels_[channelIndex].config_sub_stack->setCurrentIndex(selected_channel_sub_page_index_);
            }
        }
        else
        {
            channel_top_controls_stack_->setCurrentIndex(2);
        }
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
        channel_top_controls_stack_->setVisible(true);
        placeControllerModeFieldInTopControls(channelIndex, subPageIndex);
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
        temperature_plot_->setSampleTimes(measured_temperature_time_history_[channelIndex]);
    }
    updateCalibrationDrawerVisibility();
}

void TemperatureControllerPanel::updateCalibrationDrawerVisibility()
{
    const bool sensorPageSelected = selected_config_page_index_ < 2 &&
        selected_channel_sub_page_index_ == 2;
    const int selectedChannelIndex = std::clamp(selected_channel_index_, 0, 1);
    for (int index = 0; index < static_cast<int>(channels_.size()); ++index)
    {
        auto *drawer = static_cast<CalibrationSideDrawer *>(channels_[index].sensor_calibration_drawer);
        if (!drawer)
        {
            continue;
        }
        const bool active = sensorPageSelected && index == selectedChannelIndex;
        if (!active)
        {
            drawer->setExpanded(false, false);
            drawer->setVisible(false);
            continue;
        }
        drawer->setVisible(true);
        QTimer::singleShot(0, this, [this, index]() {
            alignSensorCalibrationOverlay(index);
        });
    }
}

void TemperatureControllerPanel::updateChannelStackMinimumHeight()
{
    if (!channel_stack_)
    {
        return;
    }

    int maximumPageHeight = kTemperatureControllerChannelStackHeight;
    int maximumSubBarHeight = 0;
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

        QMargins channelMargins;
        if (QLayout *channelLayout = channelPage->layout())
        {
            channelLayout->invalidate();
            channelLayout->activate();
            channelMargins = channelLayout->contentsMargins();
        }
        const int channelPageHeight = channelMargins.top() + kTemperatureControllerChannelConfigSubStackHeight +
            channelMargins.bottom();
        channelPage->setFixedHeight(channelPageHeight);
        maximumPageHeight = std::max(maximumPageHeight, channelPageHeight);

        if (channel.sub_page_row)
        {
            const int subPageRowHeight = std::max(
                kTemperatureControllerConfigRowHeight,
                channel.sensor_config_top_bar ? channel.sensor_config_top_bar->sizeHint().height()
                                              : kTemperatureControllerConfigRowHeight);
            channel.sub_page_row->setFixedHeight(subPageRowHeight);
            maximumSubBarHeight = std::max(maximumSubBarHeight, subPageRowHeight);
        }
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

    if (common_.sub_top_bar)
    {
        const int commonSubBarHeight = std::max(
            kTemperatureControllerConfigRowHeight,
            common_.sub_top_bar->sizeHint().height());
        common_.sub_top_bar->setFixedHeight(commonSubBarHeight);
        maximumSubBarHeight = std::max(maximumSubBarHeight, commonSubBarHeight);
    }

    channel_stack_->setFixedHeight(maximumPageHeight);
    channel_stack_->updateGeometry();
    if (sub_page_bar_stack_ && maximumSubBarHeight > 0)
    {
        sub_page_bar_stack_->setFixedHeight(maximumSubBarHeight);
        sub_page_bar_stack_->updateGeometry();
    }
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
        updateButton(channel.common_params_button, 0);
        updateButton(channel.advanced_params_button, 1);
        updateButton(channel.sensor_config_button, 2);
    }
    updateButton(common_.common_params_button, 0);
    updateButton(common_.advanced_params_button, 1);
    updateButton(common_.sensor_config_button, 2);

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
        channel_top_controls_stack_->setVisible(true);
        if (pageIndex != 1)
        {
            channel_top_controls_stack_->setCurrentIndex(selectedChannelIndex);
            placeControllerModeFieldInTopControls(selectedChannelIndex, pageIndex);
            refreshTopControlsLayout();
        }
        else
        {
            channel_top_controls_stack_->setCurrentIndex(2);
            placeControllerModeFieldInTopControls(selectedChannelIndex, pageIndex);
            refreshTopControlsLayout();
        }
    }
    updateCalibrationDrawerVisibility();
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
    if (controller_mode_lbl_) controller_mode_lbl_->setText(is_english_ ? QStringLiteral("Mode:") : QStringLiteral("温控器模式"));
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
        fitControllerModeComboWidth();
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
    auto fitCommonSubButtonWidth = [](QPushButton *button) {
        if (!button)
        {
            return;
        }
        button->ensurePolished();
        button->setMinimumWidth(std::max(kTemperatureControllerSubTabMinimumWidth,
                                         button->fontMetrics().horizontalAdvance(button->text()) +
                                             kTemperatureControllerSubTabTextPadding));
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    };
    if (common_.common_params_button)
    {
        common_.common_params_button->setText(is_english_ ? QStringLiteral("Common") : QStringLiteral("常用参数"));
        fitCommonSubButtonWidth(common_.common_params_button);
    }
    if (common_.advanced_params_button)
    {
        common_.advanced_params_button->setText(is_english_ ? QStringLiteral("Advanced") : QStringLiteral("专业参数"));
        fitCommonSubButtonWidth(common_.advanced_params_button);
    }
    if (common_.sensor_config_button)
    {
        common_.sensor_config_button->setText(is_english_ ? QStringLiteral("Sensor Config") : QStringLiteral("传感器配置"));
        fitCommonSubButtonWidth(common_.sensor_config_button);
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
            button->setMinimumWidth(std::max(kTemperatureControllerSubTabMinimumWidth,
                                             button->fontMetrics().horizontalAdvance(button->text()) +
                                                 kTemperatureControllerSubTabTextPadding));
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
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
        if (auto *drawer = static_cast<CalibrationSideDrawer *>(channel.sensor_calibration_drawer))
        {
            drawer->setHandleText(is_english_
                ? QStringLiteral("Cal\nA0\nA7")
                : QStringLiteral("校\n准\n系\n数\nA0\nA7"));
            drawer->setToolTip(is_english_
                ? QStringLiteral("Expand calibration coefficients A0-A7")
                : QStringLiteral("展开校准系数 A0-A7"));
            drawer->setAccessibleName(is_english_
                ? QStringLiteral("Calibration coefficients A0-A7")
                : QStringLiteral("校准系数 A0-A7"));
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
            fitTemperatureComboWidth(channel.auto_pid_combo, kTemperatureControllerAutoPidTextWidthReserve);
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
        const double sampleTimeSeconds = localClockSeconds();
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
                auto& timeHistory = measured_temperature_time_history_[i];
                history.append(measured);
                timeHistory.append(sampleTimeSeconds);
                while (history.size() > kTemperatureControllerHistoryLimit)
                {
                    history.removeFirst();
                    timeHistory.removeFirst();
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
        temperature_plot_->setSampleTimes(measured_temperature_time_history_[channelIndex]);
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
    auto *combo = new SingleLevelPopupComboBox(parent);
    combo->setShowSelectionCheck(showSelectionCheck);
    combo->setPopupFitContents(popupFitContents);
    configureSingleLevelComboCheckIcon(combo);
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
