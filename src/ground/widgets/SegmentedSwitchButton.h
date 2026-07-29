#pragma once

#include <QPushButton>
#include <QString>

class QFocusEvent;
class QMouseEvent;
class QPaintEvent;
class QVariantAnimation;

namespace VaporView::Ground::Widgets
{

class SegmentedSwitchButton : public QPushButton
{
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

    QString leftSegmentText() const;
    QString rightSegmentText() const;
    AccentMode accentMode() const;
    int animationDuration() const;
    bool autoToggle() const;
    bool switchChecked() const;
    bool keyboardFocusIndicatorVisible() const;

    void setSwitchChecked(bool checked, bool animated);

protected:
    void nextCheckState() override;
    void paintEvent(QPaintEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void animateThumbTo(qreal target);
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
    qreal thumb_jelly_ = 0.0;
    int thumb_direction_ = 1;
    QVariantAnimation *thumb_animation_ = nullptr;
};

} // namespace VaporView::Ground::Widgets
