#include "shared/theme/SingleLevelPopupMenu.h"

#include <QAction>
#include <QApplication>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QToolButton>
#include <QWidgetAction>

#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << std::endl;
        std::exit(1);
    }
}

void pump()
{
    for (int i = 0; i < 4; ++i)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

QAction *addActionRow(VaporView::SingleLevelPopupMenu& menu,
                      const QString& objectName,
                      const QString& text,
                      bool enabled = true,
                      bool checkable = false,
                      bool checked = false,
                      bool closeOnClick = true,
                      bool visible = true)
{
    auto *row = new VaporView::SingleLevelPopupMenuRow(&menu);
    row->setObjectName(objectName);
    row->setText(text);
    row->setCheckSlotWidth(checkable ? 18 : 0);
    row->setCloseOnClick(closeOnClick);
    QAction *action = menu.addRow(row);
    require(action != nullptr, "addRow returns a QAction-backed widget action");
    action->setObjectName(objectName);
    action->setText(text);
    action->setToolTip(QStringLiteral("说明：%1").arg(text));
    action->setEnabled(enabled);
    action->setCheckable(checkable);
    action->setChecked(checked);
    action->setVisible(visible);
    pump();
    return action;
}

VaporView::SingleLevelPopupMenuRow *rowFor(QAction *action)
{
    auto *widgetAction = qobject_cast<QWidgetAction *>(action);
    return widgetAction ? qobject_cast<VaporView::SingleLevelPopupMenuRow *>(widgetAction->defaultWidget()) : nullptr;
}

void sendKey(QWidget *target, int key)
{
    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
    pump();
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QWidget anchor;
    anchor.setObjectName(QStringLiteral("popupAnchorButton"));
    anchor.setFocusPolicy(Qt::StrongFocus);
    anchor.resize(80, 28);
    anchor.show();
    anchor.setFocus();
    pump();

    VaporView::SingleLevelPopupMenu menu(&anchor);
    menu.setObjectName(QStringLiteral("singleLevelPopupMenuTestMenu"));
    menu.setPanelPadding(0);
    menu.setCornerRadius(8);

    QAction *first = addActionRow(menu,
                                  QStringLiteral("menuActionFirst"),
                                  QStringLiteral("第一项"));
    QAction *disabled = addActionRow(menu,
                                     QStringLiteral("menuActionDisabled"),
                                     QStringLiteral("不可用项"),
                                     false);
    QAction *hidden = addActionRow(menu,
                                   QStringLiteral("menuActionHidden"),
                                   QStringLiteral("隐藏项"),
                                   true,
                                   false,
                                   false,
                                   true,
                                   false);
    QAction *checkable = addActionRow(menu,
                                      QStringLiteral("menuActionCheckable"),
                                      QStringLiteral("可勾选项"),
                                      true,
                                      true,
                                      true,
                                      false);

    VaporView::SingleLevelPopupMenuRow *firstRow = rowFor(first);
    VaporView::SingleLevelPopupMenuRow *disabledRow = rowFor(disabled);
    VaporView::SingleLevelPopupMenuRow *hiddenRow = rowFor(hidden);
    VaporView::SingleLevelPopupMenuRow *checkableRow = rowFor(checkable);

    require(firstRow != nullptr && disabledRow != nullptr && hiddenRow != nullptr && checkableRow != nullptr,
            "all interactive menu rows are real row widgets");
    require(qobject_cast<QToolButton *>(firstRow) != nullptr,
            "menu row exposes real QToolButton semantics");
    require(firstRow->defaultAction() == first,
            "row defaultAction remains the QAction source");
    require(firstRow->objectName() == QStringLiteral("menuActionFirst"),
            "row inherits a stable language-independent objectName");
    require(firstRow->accessibleName() == QStringLiteral("第一项"),
            "row accessibleName mirrors localized action text");
    require(firstRow->accessibleDescription().contains(QStringLiteral("说明")),
            "row accessibleDescription follows action tooltip");
    require(firstRow->focusPolicy() == Qt::StrongFocus,
            "row accepts keyboard focus");
    require(!disabledRow->isEnabled(),
            "disabled QAction disables the row control");
    require(!hiddenRow->isVisible(),
            "hidden QAction hides the row control instead of exposing a zero-height visible item");
    require(checkableRow->isCheckable() && checkableRow->isChecked() && checkableRow->isChecked(),
            "checkable QAction exposes checked button state");
    require(checkableRow->closeOnClick() == false,
            "non-closing checkable rows preserve closeOnClick behavior");
    {
        QSet<QString> objectNames;
        for (VaporView::SingleLevelPopupMenuRow *row : menu.rows())
        {
            require(row != nullptr && !row->objectName().isEmpty(),
                    "every interactive row has a non-empty objectName");
            require(!objectNames.contains(row->objectName()),
                    "interactive row objectNames are unique inside the menu");
            objectNames.insert(row->objectName());
        }
    }

    first->setText(QStringLiteral("第一项更新"));
    first->setToolTip(QStringLiteral("更新后的说明"));
    first->setEnabled(false);
    pump();
    require(firstRow->text() == QStringLiteral("第一项更新"),
            "QAction text updates row text");
    require(firstRow->accessibleName() == QStringLiteral("第一项更新"),
            "QAction text updates localized accessibleName");
    require(firstRow->accessibleDescription() == QStringLiteral("更新后的说明"),
            "QAction tooltip updates accessibleDescription");
    require(!firstRow->isEnabled(),
            "QAction enabled state syncs to row");

    first->setEnabled(true);
    checkable->setChecked(false);
    pump();
    require(!checkableRow->isChecked(),
            "QAction checked state syncs to row");
    checkableRow->setChecked(true);
    pump();
    require(checkable->isChecked(),
            "row checked state writes through QAction");

    int triggerCount = 0;
    QObject::connect(first, &QAction::triggered, [&triggerCount]() {
        ++triggerCount;
    });
    int checkableTriggerCount = 0;
    QObject::connect(checkable, &QAction::triggered, [&checkableTriggerCount]() {
        ++checkableTriggerCount;
    });
    firstRow->click();
    pump();
    require(triggerCount == 1,
            "clicking a real row button triggers QAction exactly once");

    menu.popupFrom(&anchor);
    pump();
    require(menu.isVisible(), "popupFrom shows the custom popup shell");
    require(QApplication::focusWidget() == checkableRow,
            "menu focuses checked row on open");
    require(checkableRow->property("keyboardFocus").toBool(),
            "keyboard focus state is visually tracked separately");

    sendKey(checkableRow, Qt::Key_Down);
    require(QApplication::focusWidget() == firstRow,
            "Down skips disabled and hidden rows and wraps to first enabled row");
    sendKey(firstRow, Qt::Key_Up);
    require(QApplication::focusWidget() == checkableRow,
            "Up skips disabled and hidden rows and wraps to the last enabled row");
    sendKey(firstRow, Qt::Key_End);
    require(QApplication::focusWidget() == checkableRow,
            "End focuses last enabled row");
    sendKey(checkableRow, Qt::Key_Home);
    require(QApplication::focusWidget() == firstRow,
            "Home focuses first enabled row");

    triggerCount = 0;
    sendKey(firstRow, Qt::Key_Return);
    require(triggerCount == 1,
            "Return invokes focused row without double trigger");

    menu.popupFrom(&anchor);
    pump();
    sendKey(QApplication::focusWidget(), Qt::Key_Space);
    require(checkableTriggerCount == 1,
            "Space invokes focused checkable row without double trigger");
    require(menu.isVisible(),
            "Space on a non-closing checkable row preserves closeOnClick=false menu visibility");
    sendKey(QApplication::focusWidget(), Qt::Key_Tab);
    require(!menu.isVisible(),
            "Tab closes menu using the menu-level focus policy");

    menu.popupFrom(&anchor);
    pump();
    sendKey(QApplication::focusWidget(), Qt::Key_Escape);
    require(!menu.isVisible(),
            "Escape closes menu");
    pump();
    require(QApplication::focusWidget() == &anchor,
            "closing menu restores focus to opener");

    QPointer<QAction> deletedAction = addActionRow(menu,
                                                   QStringLiteral("menuActionDestroyed"),
                                                   QStringLiteral("销毁项"));
    QPointer<VaporView::SingleLevelPopupMenuRow> deletedRow = rowFor(deletedAction);
    delete deletedAction;
    pump();
    require(deletedAction.isNull(),
            "destroyed QAction is actually deleted");
    require(deletedRow.isNull(),
            "destroyed QAction removes orphaned menu row safely");

    return 0;
}
