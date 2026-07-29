#include "ground/widgets/SegmentedSwitchButton.h"

#include "shared/theme/AppTheme.h"

#include <QAbstractAnimation>
#include <QCoreApplication>
#include <QEasingCurve>
#include <QFocusEvent>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>

namespace
{

constexpr char kReducedMotionProperty[] = "vaporViewReducedMotion";

qreal clampUnit(qreal value)
{
    return std::clamp(value, 0.0, 1.0);
}

qreal smoothStep(qreal value)
{
    const qreal t = clampUnit(value);
    return t * t * (3.0 - 2.0 * t);
}

qreal easeOutCubic(qreal value)
{
    const qreal t = 1.0 - clampUnit(value);
    return 1.0 - t * t * t;
}

qreal mixValue(qreal from, qreal to, qreal progress)
{
    return from + (to - from) * clampUnit(progress);
}

QColor mixColor(const QColor& from, const QColor& to, qreal progress)
{
    const qreal t = clampUnit(progress);
    QColor result;
    result.setRgbF(mixValue(from.redF(), to.redF(), t),
                   mixValue(from.greenF(), to.greenF(), t),
                   mixValue(from.blueF(), to.blueF(), t),
                   mixValue(from.alphaF(), to.alphaF(), t));
    return result;
}

qreal interpolateStage(qreal progress,
                       qreal startProgress,
                       qreal endProgress,
                       qreal from,
                       qreal to)
{
    const qreal duration = std::max<qreal>(0.001, endProgress - startProgress);
    return mixValue(from, to, smoothStep((progress - startProgress) / duration));
}

} // namespace

namespace VaporView::Ground::Widgets
{

SegmentedSwitchButton::SegmentedSwitchButton(QWidget *parent)
    : QPushButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setProperty("segmentedSwitchControl", true);
    setProperty("keyboardFocusIndicatorVisible", false);

    thumb_position_ = isChecked() ? 1.0 : 0.0;
    if (QCoreApplication::instance())
    {
        reduced_motion_enabled_ =
            QCoreApplication::instance()->property(kReducedMotionProperty).toBool();
    }
    setProperty("reducedMotionEnabled", reduced_motion_enabled_);

    thumb_animation_ = new QVariantAnimation(this);
    thumb_animation_->setObjectName(QStringLiteral("segmentedSwitchThumbAnimation"));
    thumb_animation_->setDuration(animation_duration_ms_);
    thumb_animation_->setEasingCurve(QEasingCurve::Linear);
    connect(thumb_animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        const qreal progress = clampUnit(value.toReal());
        if (reduced_motion_enabled_)
        {
            thumb_position_ = mixValue(thumb_start_position_,
                                       thumb_target_position_,
                                       easeOutCubic(progress));
            thumb_scale_x_ = 1.0;
            thumb_scale_y_ = 1.0;
            update();
            return;
        }

        const qreal distance = thumb_target_position_ - thumb_start_position_;
        const qreal launchPosition = thumb_start_position_ + distance * 0.04;
        const qreal overshootPosition =
            thumb_target_position_ + thumb_direction_ * thumb_overshoot_position_;
        const qreal undershootPosition =
            thumb_target_position_ - thumb_direction_ * thumb_overshoot_position_ * 0.24;
        if (progress < 0.14)
        {
            thumb_position_ = mixValue(thumb_start_position_,
                                       launchPosition,
                                       smoothStep(progress / 0.14));
        }
        else if (progress < 0.62)
        {
            thumb_position_ = mixValue(launchPosition,
                                       overshootPosition,
                                       easeOutCubic((progress - 0.14) / 0.48));
        }
        else if (progress < 0.80)
        {
            thumb_position_ = mixValue(overshootPosition,
                                       undershootPosition,
                                       smoothStep((progress - 0.62) / 0.18));
        }
        else
        {
            thumb_position_ = mixValue(undershootPosition,
                                       thumb_target_position_,
                                       smoothStep((progress - 0.80) / 0.20));
        }

        if (progress < 0.16)
        {
            thumb_scale_x_ = interpolateStage(progress, 0.0, 0.16, thumb_start_scale_x_, 1.10);
            thumb_scale_y_ = interpolateStage(progress, 0.0, 0.16, thumb_start_scale_y_, 0.94);
        }
        else if (progress < 0.56)
        {
            thumb_scale_x_ = interpolateStage(progress, 0.16, 0.56, 1.10, 1.02);
            thumb_scale_y_ = interpolateStage(progress, 0.16, 0.56, 0.94, 0.99);
        }
        else if (progress < 0.66)
        {
            thumb_scale_x_ = interpolateStage(progress, 0.56, 0.66, 1.02, 0.96);
            thumb_scale_y_ = interpolateStage(progress, 0.56, 0.66, 0.99, 1.025);
        }
        else if (progress < 0.82)
        {
            thumb_scale_x_ = interpolateStage(progress, 0.66, 0.82, 0.96, 1.025);
            thumb_scale_y_ = interpolateStage(progress, 0.66, 0.82, 1.025, 0.985);
        }
        else
        {
            thumb_scale_x_ = interpolateStage(progress, 0.82, 1.0, 1.025, 1.0);
            thumb_scale_y_ = interpolateStage(progress, 0.82, 1.0, 0.985, 1.0);
        }
        update();
    });
    connect(thumb_animation_, &QVariantAnimation::finished, this, [this]() {
        thumb_position_ = thumb_target_position_;
        thumb_scale_x_ = 1.0;
        thumb_scale_y_ = 1.0;
        update();
    });

    press_animation_ = new QVariantAnimation(this);
    press_animation_->setObjectName(QStringLiteral("segmentedSwitchPressAnimation"));
    press_animation_->setEasingCurve(QEasingCurve::OutCubic);
    connect(press_animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        press_progress_ = clampUnit(value.toReal());
        update();
    });
    connect(this, &QPushButton::pressed, this, [this]() { animatePressTo(1.0); });
    connect(this, &QPushButton::released, this, [this]() { animatePressTo(0.0); });
}

void SegmentedSwitchButton::setSegmentTexts(const QString& leftText, const QString& rightText)
{
    left_text_ = leftText;
    right_text_ = rightText;
    refreshAccessibleText();
    update();
}

void SegmentedSwitchButton::setStateDescription(const QString& prefix, const QString& separator)
{
    state_description_prefix_ = prefix;
    state_description_separator_ = separator;
    refreshAccessibleText();
}

void SegmentedSwitchButton::setAccentMode(AccentMode mode)
{
    if (accent_mode_ == mode)
    {
        return;
    }
    accent_mode_ = mode;
    update();
}

void SegmentedSwitchButton::setAnimationDuration(int durationMs)
{
    animation_duration_ms_ = std::max(0, durationMs);
    if (thumb_animation_->state() != QAbstractAnimation::Running)
    {
        thumb_animation_->setDuration(animation_duration_ms_);
    }
}

void SegmentedSwitchButton::setAutoToggle(bool autoToggle)
{
    auto_toggle_ = autoToggle;
}

void SegmentedSwitchButton::setReducedMotionEnabled(bool enabled)
{
    if (reduced_motion_enabled_ == enabled)
    {
        return;
    }

    const bool wasAnimating = thumb_animation_->state() == QAbstractAnimation::Running;
    const qreal target = thumb_target_position_;
    thumb_animation_->stop();
    press_animation_->stop();
    reduced_motion_enabled_ = enabled;
    press_progress_ = 0.0;
    thumb_scale_x_ = 1.0;
    thumb_scale_y_ = 1.0;
    setProperty("reducedMotionEnabled", enabled);
    if (wasAnimating && !qFuzzyCompare(1.0 + thumb_position_, 1.0 + target))
    {
        animateThumbTo(target);
    }
    else
    {
        update();
    }
}

QString SegmentedSwitchButton::leftSegmentText() const
{
    return left_text_;
}

QString SegmentedSwitchButton::rightSegmentText() const
{
    return right_text_;
}

SegmentedSwitchButton::AccentMode SegmentedSwitchButton::accentMode() const
{
    return accent_mode_;
}

int SegmentedSwitchButton::animationDuration() const
{
    return animation_duration_ms_;
}

bool SegmentedSwitchButton::autoToggle() const
{
    return auto_toggle_;
}

bool SegmentedSwitchButton::reducedMotionEnabled() const
{
    return reduced_motion_enabled_;
}

bool SegmentedSwitchButton::switchChecked() const
{
    return isChecked();
}

bool SegmentedSwitchButton::keyboardFocusIndicatorVisible() const
{
    return keyboard_focus_indicator_visible_;
}

qreal SegmentedSwitchButton::thumbPosition() const
{
    return thumb_position_;
}

qreal SegmentedSwitchButton::thumbHorizontalScale() const
{
    return thumb_scale_x_;
}

qreal SegmentedSwitchButton::thumbVerticalScale() const
{
    return thumb_scale_y_;
}

bool SegmentedSwitchButton::switchAnimationRunning() const
{
    return thumb_animation_->state() == QAbstractAnimation::Running;
}

QSize SegmentedSwitchButton::sizeHint() const
{
    return QSize(250, 62);
}

QSize SegmentedSwitchButton::minimumSizeHint() const
{
    return QSize(96, 32);
}

void SegmentedSwitchButton::setSwitchChecked(bool checked, bool animated)
{
    const qreal target = checked ? 1.0 : 0.0;
    const bool continuingSameAnimation =
        thumb_animation_->state() == QAbstractAnimation::Running &&
        qFuzzyCompare(1.0 + thumb_target_position_, 1.0 + target);

    {
        const QSignalBlocker blocker(this);
        setChecked(checked);
    }
    refreshAccessibleText();
    if (continuingSameAnimation)
    {
        update();
        return;
    }

    if (animated)
    {
        animateThumbTo(target);
        return;
    }

    thumb_animation_->stop();
    thumb_position_ = target;
    thumb_start_position_ = target;
    thumb_target_position_ = target;
    thumb_scale_x_ = 1.0;
    thumb_scale_y_ = 1.0;
    update();
}

void SegmentedSwitchButton::nextCheckState()
{
    requestSelection(pointer_selection_pending_ ? pointer_right_selected_ : !isChecked());
}

void SegmentedSwitchButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    const bool dark = VaporView::isDarkThemeEnabled();
    const bool enabled = isEnabled();
    const bool binaryStateAccent = accent_mode_ == AccentMode::BinaryState;
    const qreal selectionProgress = clampUnit(thumb_position_);
    const QColor primaryAccent =
        VaporView::appThemeColor(VaporView::AppThemeColor::Primary, dark);
    const QColor leftAccent = binaryStateAccent
        ? VaporView::appThemeColor(VaporView::AppThemeColor::ToolbarRed, dark)
        : primaryAccent;
    const QColor rightAccent = binaryStateAccent
        ? VaporView::appThemeColor(VaporView::AppThemeColor::ToolbarGreen, dark)
        : primaryAccent;
    const QColor accent = mixColor(leftAccent, rightAccent, selectionProgress);
    const QColor border =
        VaporView::appThemeColor(VaporView::AppThemeColor::BorderStrong, dark);
    const QColor outerFill = enabled && !binaryStateAccent
        ? VaporView::appThemeColor(VaporView::AppThemeColor::PrimarySubtle, dark)
        : VaporView::appThemeColor(binaryStateAccent
                                      ? VaporView::AppThemeColor::Surface
                                      : VaporView::AppThemeColor::SurfaceAlt,
                                  dark);
    const QColor trackFill = enabled
        ? accent
        : VaporView::appThemeColor(VaporView::AppThemeColor::Surface, dark);
    const QColor selectedFill = enabled
        ? VaporView::appThemeColor(VaporView::AppThemeColor::Surface, dark)
        : VaporView::appThemeColor(VaporView::AppThemeColor::SurfaceAlt, dark);
    const QColor inactiveText = enabled
        ? VaporView::appThemeColor(VaporView::AppThemeColor::White, dark)
        : VaporView::appThemeColor(VaporView::AppThemeColor::TextMuted, dark);
    const QColor leftText = enabled
        ? mixColor(inactiveText, leftAccent, 1.0 - selectionProgress)
        : inactiveText;
    const QColor rightText = enabled
        ? mixColor(inactiveText, rightAccent, selectionProgress)
        : inactiveText;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const qreal pressedScale = 1.0 - 0.015 * press_progress_;
    const QPointF widgetCenter = rect().center();
    painter.translate(widgetCenter);
    painter.scale(pressedScale, pressedScale);
    painter.translate(-widgetCenter);

    const QRectF outerRect = QRectF(rect()).adjusted(0.75, 0.75, -0.75, -0.75);
    const qreal outerRadius = outerRect.height() / 2.0;
    const qreal trackInset = std::clamp(height() * 0.08, 2.5, 5.0);
    const qreal contentInset = std::clamp(height() * 0.045, 1.5, 3.0);
    painter.setPen(QPen(border, 1.0));
    painter.setBrush(outerFill);
    painter.drawRoundedRect(outerRect, outerRadius, outerRadius);

    const QRectF trackRect = outerRect.adjusted(trackInset,
                                                trackInset,
                                                -trackInset,
                                                -trackInset);
    const QRectF contentRect = trackRect.adjusted(contentInset,
                                                  contentInset,
                                                  -contentInset,
                                                  -contentInset);
    QColor trackOutline = VaporView::appThemeColor(VaporView::AppThemeColor::White, dark);
    trackOutline.setAlphaF(enabled ? 0.88 : 0.35);
    painter.setPen(QPen(trackOutline, 1.0));
    painter.setBrush(trackFill);
    painter.drawRoundedRect(trackRect, trackRect.height() / 2.0, trackRect.height() / 2.0);

    const qreal segmentWidth = contentRect.width() / 2.0;
    QRectF selectedRect(contentRect.left() + segmentWidth * thumb_position_,
                        contentRect.top(),
                        segmentWidth,
                        contentRect.height());
    if (enabled)
    {
        const qreal scaledWidth = segmentWidth * thumb_scale_x_;
        if (thumb_direction_ >= 0)
        {
            selectedRect.setWidth(scaledWidth);
        }
        else
        {
            selectedRect.setLeft(selectedRect.right() - scaledWidth);
        }
        const qreal scaledHeight = contentRect.height() * thumb_scale_y_;
        selectedRect.setTop(contentRect.center().y() - scaledHeight / 2.0);
        selectedRect.setBottom(contentRect.center().y() + scaledHeight / 2.0);
    }

    QColor thumbShadow = border;
    thumbShadow.setAlphaF(enabled ? 0.18 : 0.08);
    const QRectF shadowRect = selectedRect.translated(0.0, std::max<qreal>(0.5, height() * 0.012));
    painter.setPen(Qt::NoPen);
    painter.setBrush(thumbShadow);
    painter.drawRoundedRect(shadowRect, shadowRect.height() / 2.0, shadowRect.height() / 2.0);
    painter.setBrush(selectedFill);
    painter.drawRoundedRect(selectedRect, selectedRect.height() / 2.0, selectedRect.height() / 2.0);

    const QRectF leftRect(contentRect.left(), contentRect.top(), segmentWidth, contentRect.height());
    const QRectF rightRect(contentRect.left() + segmentWidth,
                           contentRect.top(),
                           segmentWidth,
                           contentRect.height());
    auto drawSegmentText = [&painter](const QRectF& textRect, const QString& text) {
        const QRectF textBounds = QFontMetricsF(painter.font()).tightBoundingRect(text);
        const QPointF baseline(textRect.center().x() - textBounds.center().x(),
                               textRect.center().y() - textBounds.center().y());
        painter.drawText(baseline, text);
    };
    QFont segmentFont = font();
    segmentFont.setWeight(QFont::DemiBold);
    int fontSizePx = std::clamp(qRound(contentRect.height() * 0.58), 12, 26);
    segmentFont.setPixelSize(fontSizePx);
    const qreal maximumTextWidth = std::max<qreal>(1.0, segmentWidth - contentInset * 2.0);
    while (fontSizePx > 10)
    {
        const QFontMetricsF metrics(segmentFont);
        if (std::max(metrics.horizontalAdvance(left_text_), metrics.horizontalAdvance(right_text_)) <=
            maximumTextWidth)
        {
            break;
        }
        segmentFont.setPixelSize(--fontSizePx);
    }
    painter.setFont(segmentFont);
    painter.setPen(leftText);
    drawSegmentText(leftRect, left_text_);
    painter.setPen(rightText);
    drawSegmentText(rightRect, right_text_);

    if (enabled && keyboard_focus_indicator_visible_)
    {
        painter.setPen(QPen(VaporView::appThemeColor(VaporView::AppThemeColor::Focus, dark), 1.0));
        painter.setBrush(Qt::NoBrush);
        const QRectF focusRect = outerRect.adjusted(2.0, 2.0, -2.0, -2.0);
        painter.drawRoundedRect(focusRect, focusRect.height() / 2.0, focusRect.height() / 2.0);
    }
}

void SegmentedSwitchButton::focusInEvent(QFocusEvent *event)
{
    QPushButton::focusInEvent(event);
    const Qt::FocusReason reason = event->reason();
    setKeyboardFocusIndicatorVisible(reason == Qt::TabFocusReason ||
                                     reason == Qt::BacktabFocusReason ||
                                     reason == Qt::ShortcutFocusReason);
}

void SegmentedSwitchButton::focusOutEvent(QFocusEvent *event)
{
    QPushButton::focusOutEvent(event);
    setKeyboardFocusIndicatorVisible(false);
}

void SegmentedSwitchButton::keyPressEvent(QKeyEvent *event)
{
    if (isEnabled() && event->modifiers() == Qt::NoModifier)
    {
        if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)
        {
            requestSelection(event->key() == Qt::Key_Right);
            event->accept();
            return;
        }
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
            !event->isAutoRepeat())
        {
            click();
            event->accept();
            return;
        }
    }

    QPushButton::keyPressEvent(event);
}

void SegmentedSwitchButton::mousePressEvent(QMouseEvent *event)
{
    setKeyboardFocusIndicatorVisible(false);
    pointer_selection_pending_ =
        event->button() == Qt::LeftButton && rect().contains(event->position().toPoint());
    if (pointer_selection_pending_)
    {
        pointer_right_selected_ = event->position().x() >= width() / 2.0;
    }
    QPushButton::mousePressEvent(event);
}

void SegmentedSwitchButton::mouseReleaseEvent(QMouseEvent *event)
{
    if (pointer_selection_pending_ &&
        event->button() == Qt::LeftButton &&
        rect().contains(event->position().toPoint()))
    {
        pointer_right_selected_ = event->position().x() >= width() / 2.0;
    }
    QPushButton::mouseReleaseEvent(event);
    pointer_selection_pending_ = false;
}

void SegmentedSwitchButton::animateThumbTo(qreal target)
{
    if (qFuzzyCompare(1.0 + thumb_position_, 1.0 + target))
    {
        thumb_position_ = target;
        thumb_start_position_ = target;
        thumb_target_position_ = target;
        thumb_scale_x_ = 1.0;
        thumb_scale_y_ = 1.0;
        update();
        return;
    }

    thumb_animation_->stop();
    thumb_start_position_ = thumb_position_;
    thumb_target_position_ = target;
    thumb_start_scale_x_ = thumb_scale_x_;
    thumb_start_scale_y_ = thumb_scale_y_;
    thumb_direction_ = thumb_target_position_ >= thumb_start_position_ ? 1 : -1;
    const qreal trackInset = std::clamp(height() * 0.08, 2.5, 5.0);
    const qreal contentInset = std::clamp(height() * 0.045, 1.5, 3.0);
    const qreal segmentWidth =
        std::max<qreal>(1.0, (width() - 1.5 - 2.0 * (trackInset + contentInset)) / 2.0);
    const qreal overshootPixels = std::clamp(width() * 0.014, 2.0, 3.5);
    thumb_overshoot_position_ = overshootPixels / segmentWidth;
    if (reduced_motion_enabled_)
    {
        thumb_scale_x_ = 1.0;
        thumb_scale_y_ = 1.0;
    }
    thumb_animation_->setDuration(reduced_motion_enabled_
                                      ? std::min(animation_duration_ms_, 150)
                                      : animation_duration_ms_);
    thumb_animation_->setStartValue(0.0);
    thumb_animation_->setEndValue(1.0);
    thumb_animation_->start();
}

void SegmentedSwitchButton::animatePressTo(qreal target)
{
    if (reduced_motion_enabled_)
    {
        press_animation_->stop();
        press_progress_ = 0.0;
        update();
        return;
    }

    press_animation_->stop();
    press_animation_->setDuration(target > press_progress_ ? 80 : 120);
    press_animation_->setStartValue(press_progress_);
    press_animation_->setEndValue(clampUnit(target));
    press_animation_->start();
}

void SegmentedSwitchButton::requestSelection(bool rightSelected)
{
    if (!isEnabled() || rightSelected == isChecked())
    {
        return;
    }

    if (auto_toggle_)
    {
        setChecked(rightSelected);
        refreshAccessibleText();
        animateThumbTo(rightSelected ? 1.0 : 0.0);
    }
    emit selectionRequested(rightSelected);
}

void SegmentedSwitchButton::refreshAccessibleText()
{
    const QString selectedText = isChecked() ? right_text_ : left_text_;
    const QString description = state_description_prefix_.isEmpty()
        ? selectedText
        : state_description_prefix_ + state_description_separator_ + selectedText;
    setText(description);
    setToolTip(description);
    setAccessibleName(description);
}

void SegmentedSwitchButton::setKeyboardFocusIndicatorVisible(bool visible)
{
    if (keyboard_focus_indicator_visible_ == visible)
    {
        return;
    }
    keyboard_focus_indicator_visible_ = visible;
    setProperty("keyboardFocusIndicatorVisible", visible);
    update();
}

} // namespace VaporView::Ground::Widgets
