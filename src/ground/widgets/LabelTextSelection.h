#ifndef VAPORVIEW_LABEL_TEXT_SELECTION_H_
#define VAPORVIEW_LABEL_TEXT_SELECTION_H_

#include "ground/widgets/VisualTextLabel.h"

#include <QClipboard>
#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMouseEvent>
#include <QObject>

namespace VaporView
{

class SelectableCardTitleEventFilter final : public QObject
{
public:
    explicit SelectableCardTitleEventFilter(QLabel *label)
        : QObject(label)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        auto *label = qobject_cast<QLabel *>(watched);
        if (label && event->type() == QEvent::ContextMenu)
        {
            event->accept();
            return true;
        }
        if (label && event->type() == QEvent::KeyPress)
        {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->matches(QKeySequence::Copy) && label->hasSelectedText())
            {
                if (QClipboard *clipboard = QGuiApplication::clipboard())
                {
                    clipboard->setText(label->selectedText());
                    event->accept();
                    return true;
                }
            }
        }
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

inline void configureSelectableCardTitle(QLabel *label)
{
    if (!label)
    {
        return;
    }

    if (auto *visualLabel = dynamic_cast<VisualTextLabel *>(label))
    {
        visualLabel->setCustomMouseSelectionEnabled(true);
        label->setTextInteractionFlags(Qt::TextSelectableByKeyboard);
    }
    else
    {
        label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    }
    label->setFocusPolicy(Qt::ClickFocus);
    label->installEventFilter(new SelectableCardTitleEventFilter(label));
}

}  // namespace VaporView

#endif
