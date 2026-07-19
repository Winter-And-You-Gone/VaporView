#ifndef VAPORVIEW_LABEL_TEXT_SELECTION_H_
#define VAPORVIEW_LABEL_TEXT_SELECTION_H_

#include <QEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QObject>

namespace VaporView
{

class SelectableLabelFocusFilter final : public QObject
{
public:
    explicit SelectableLabelFocusFilter(QLabel *label)
        : QObject(label)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        auto *label = qobject_cast<QLabel *>(watched);
        if (label && event->type() == QEvent::MouseButtonPress)
        {
            const auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                label->setFocus(Qt::MouseFocusReason);
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

inline void configureSelectableLabel(QLabel *label)
{
    if (!label)
    {
        return;
    }

    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    label->setFocusPolicy(Qt::ClickFocus);
    label->installEventFilter(new SelectableLabelFocusFilter(label));
}

}  // namespace VaporView

#endif
