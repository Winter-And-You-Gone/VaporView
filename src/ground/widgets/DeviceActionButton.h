#pragma once

#include <QToolButton>
#include <QStyleOptionToolButton>
#include <QStylePainter>

// Rotating a fixed-size action icon must not invalidate its ancestor layouts.
class DeviceActionButton final : public QToolButton
{
public:
    using QToolButton::QToolButton;
    void setAnimationFrame(const QIcon& icon)
    {
        animationIcon_ = icon;
        update();
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QStyleOptionToolButton option;
        initStyleOption(&option);
        if (property("state").toString() == QStringLiteral("connecting") && !animationIcon_.isNull())
            option.icon = animationIcon_;
        QStylePainter painter(this);
        painter.drawComplexControl(QStyle::CC_ToolButton, option);
    }
private:
    QIcon animationIcon_;
};

inline void setDeviceActionAnimationFrame(QToolButton *button, const QIcon& icon)
{
    if (auto *animated = dynamic_cast<DeviceActionButton *>(button)) animated->setAnimationFrame(icon);
    else button->setIcon(icon);
}
