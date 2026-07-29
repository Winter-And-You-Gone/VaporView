#include "ground/widgets/SegmentedSwitchButton.h"

#include "shared/theme/AppTheme.h"

#include <QAbstractAnimation>
#include <QEasingCurve>
#include <QFocusEvent>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>

namespace VaporView::Ground::Widgets
{

SegmentedSwitchButton::SegmentedSwitchButton(QWidget *parent)
    : QPushButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setProperty("segmentedSwitchControl", true);
    setProperty("keyboardFocusIndicatorVisible", false);

    thumb_position_ = isChecked() ? 1.0 : 0.0;
    thumb_animation_ = new QVariantAnimation(this);
    thumb_animation_->setDuration(160);
    thumb_animation_->setEasingCurve(QEasingCurve::OutCubic);
    connect(thumb_animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        const qreal progress = std::clamp(value.toReal(), 0.0, 1.0);
        constexpr qreal kPi = 3.14159265358979323846;
        thumb_position_ = thumb_start_position_ +
            (thumb_target_position_ - thumb_start_position_) * progress;
        thumb_jelly_ = std::sin(progress * kPi);
        update();
    });
    connect(thumb_animation_, &QVariantAnimation::finished, this, [this]() {
        thumb_position_ = thumb_target_position_;
        thumb_jelly_ = 0.0;
        update();
    });
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
    thumb_animation_->setDuration(std::max(0, durationMs));
}

void SegmentedSwitchButton::setAutoToggle(bool autoToggle)
{
    auto_toggle_ = autoToggle;
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
    return thumb_animation_->duration();
}

bool SegmentedSwitchButton::autoToggle() const
{
    return auto_toggle_;
}

bool SegmentedSwitchButton::switchChecked() const
{
    return isChecked();
}

bool SegmentedSwitchButton::keyboardFocusIndicatorVisible() const
{
    return keyboard_focus_indicator_visible_;
}

void SegmentedSwitchButton::setSwitchChecked(bool checked, bool animated)
{
    const qreal target = checked ? 1.0 : 0.0;
    const bool continuingSameAnimation =
        !animated &&
        thumb_animation_->state() == QAbstractAnimation::Running &&
        qFuzzyCompare(thumb_target_position_, target);

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
    thumb_jelly_ = 0.0;
    update();
}

void SegmentedSwitchButton::nextCheckState()
{
    if (auto_toggle_)
    {
        QPushButton::nextCheckState();
        refreshAccessibleText();
        animateThumbTo(isChecked() ? 1.0 : 0.0);
    }
}

void SegmentedSwitchButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    const bool dark = VaporView::isDarkThemeEnabled();
    const bool enabled = isEnabled();
    const bool binaryStateAccent = accent_mode_ == AccentMode::BinaryState;
    const QColor accent = binaryStateAccent
        ? VaporView::appThemeColor(isChecked()
                                      ? VaporView::AppThemeColor::ToolbarGreen
                                      : VaporView::AppThemeColor::ToolbarRed,
                                  dark)
        : VaporView::appThemeColor(VaporView::AppThemeColor::Primary, dark);
    const QColor border = VaporView::appThemeColor(VaporView::AppThemeColor::Border, dark);
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
    const QColor selectedText = enabled
        ? accent
        : VaporView::appThemeColor(VaporView::AppThemeColor::TextMuted, dark);
    const QColor inactiveText = enabled
        ? VaporView::appThemeColor(VaporView::AppThemeColor::White, dark)
        : VaporView::appThemeColor(VaporView::AppThemeColor::TextMuted, dark);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF outerRect = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    constexpr qreal kOuterRadius = 10.0;
    constexpr qreal kInset = 3.0;
    constexpr qreal kInnerInset = 2.0;
    painter.setPen(QPen(border, 1.0));
    painter.setBrush(outerFill);
    painter.drawRoundedRect(outerRect, kOuterRadius, kOuterRadius);

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

    painter.setPen(Qt::NoPen);
    painter.setBrush(trackFill);
    painter.drawRoundedRect(trackRect, kOuterRadius - kInset, kOuterRadius - kInset);
    painter.setBrush(selectedFill);
    painter.drawRoundedRect(selectedRect,
                            kOuterRadius - kInset - kInnerInset,
                            kOuterRadius - kInset - kInnerInset);

    const QRectF leftRect(contentRect.left(), contentRect.top(), segmentWidth, contentRect.height());
    const QRectF rightRect(contentRect.left() + segmentWidth,
                           contentRect.top(),
                           segmentWidth,
                           contentRect.height());
    const bool leftSelected = thumb_position_ < 0.5;
    auto drawSegmentText = [&painter](const QRectF& textRect, const QString& text) {
        const QRectF textBounds = QFontMetricsF(painter.font()).tightBoundingRect(text);
        const QPointF baseline(textRect.center().x() - textBounds.center().x(),
                               textRect.center().y() - textBounds.center().y());
        painter.drawText(baseline, text);
    };
    QFont segmentFont = font();
    segmentFont.setWeight(QFont::DemiBold);
    painter.setFont(segmentFont);
    painter.setPen(leftSelected ? selectedText : inactiveText);
    drawSegmentText(leftRect, left_text_);
    painter.setPen(leftSelected ? inactiveText : selectedText);
    drawSegmentText(rightRect, right_text_);

    if (enabled && keyboard_focus_indicator_visible_)
    {
        painter.setPen(QPen(VaporView::appThemeColor(VaporView::AppThemeColor::Focus, dark), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(outerRect.adjusted(2.0, 2.0, -2.0, -2.0),
                                kOuterRadius - 2.0,
                                kOuterRadius - 2.0);
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

void SegmentedSwitchButton::mousePressEvent(QMouseEvent *event)
{
    setKeyboardFocusIndicatorVisible(false);
    QPushButton::mousePressEvent(event);
}

void SegmentedSwitchButton::animateThumbTo(qreal target)
{
    if (qFuzzyCompare(thumb_position_, target))
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
