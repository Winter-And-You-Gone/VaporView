#pragma once

#include <QPushButton>
#include <QSize>
#include <QString>

class QFocusEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QVariantAnimation;

namespace VaporView::Ground::Widgets
{

class SegmentedSwitchButton : public QPushButton
{
    Q_OBJECT

public:
    enum class AccentMode
    {
        Primary,
        BinaryState
    };

    explicit SegmentedSwitchButton(QWidget *parent = nullptr);

    void setSegmentTexts(const QString& leftText, const QString& rightText);
    void setStateDescription(const QString& prefix, const QString& separator);
    void setAccentMode(AccentMode mode);
    void setAnimationDuration(int durationMs);
    void setAutoToggle(bool autoToggle);
    void setReducedMotionEnabled(bool enabled);

    QString leftSegmentText() const;
    QString rightSegmentText() const;
    AccentMode accentMode() const;
    int animationDuration() const;
    bool autoToggle() const;
    bool reducedMotionEnabled() const;
    bool switchChecked() const;
    bool keyboardFocusIndicatorVisible() const;
    qreal thumbPosition() const;
    qreal thumbHorizontalScale() const;
    qreal thumbVerticalScale() const;
    bool switchAnimationRunning() const;

    void setSwitchChecked(bool checked, bool animated);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void selectionRequested(bool rightSelected);

protected:
    void nextCheckState() override;
    void paintEvent(QPaintEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void animateThumbTo(qreal target);
    void animatePressTo(qreal target);
    void requestSelection(bool rightSelected);
    void refreshAccessibleText();
    void setKeyboardFocusIndicatorVisible(bool visible);

    QString left_text_;
    QString right_text_;
    QString state_description_prefix_;
    QString state_description_separator_ = QStringLiteral(": ");
    AccentMode accent_mode_ = AccentMode::Primary;
    bool auto_toggle_ = true;
    bool keyboard_focus_indicator_visible_ = false;
    qreal thumb_position_ = 0.0;
    qreal thumb_start_position_ = 0.0;
    qreal thumb_target_position_ = 0.0;
    qreal thumb_scale_x_ = 1.0;
    qreal thumb_scale_y_ = 1.0;
    qreal thumb_start_scale_x_ = 1.0;
    qreal thumb_start_scale_y_ = 1.0;
    qreal thumb_overshoot_position_ = 0.0;
    qreal press_progress_ = 0.0;
    int thumb_direction_ = 1;
    int animation_duration_ms_ = 480;
    bool reduced_motion_enabled_ = false;
    bool pointer_selection_pending_ = false;
    bool pointer_right_selected_ = false;
    QVariantAnimation *thumb_animation_ = nullptr;
    QVariantAnimation *press_animation_ = nullptr;
};

} // namespace VaporView::Ground::Widgets
