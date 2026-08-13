#include "ground/main/MainWindow.h"
#include "test_ui_helpers.h"

#include <QAction>
#include <QApplication>
#include <QEventLoop>
#include <QFrame>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <QWidget>

#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

QList<QToolButton *> menuRows(QWidget *menu,
                              Qt::FindChildOptions options = Qt::FindChildrenRecursively)
{
    QList<QToolButton *> rows;
    if (!menu)
    {
        return rows;
    }
    for (QToolButton *row : menu->findChildren<QToolButton *>(QString(), options))
    {
        if (row && row->property("titleApplicationMenuItem").toBool())
        {
            rows.push_back(row);
        }
    }
    return rows;
}

QList<QToolButton *> visibleMenuRows(QWidget *menu)
{
    QList<QToolButton *> rows;
    for (QToolButton *row : menuRows(menu))
    {
        if (row && row->isVisible())
        {
            rows.push_back(row);
        }
    }
    return rows;
}

QToolButton *findRow(QWidget *menu, const QString& objectName)
{
    for (QToolButton *row : menuRows(menu))
    {
        if (row->objectName() == objectName)
        {
            return row;
        }
    }
    return nullptr;
}

void sendKey(QWidget *receiver, int key)
{
    require(receiver != nullptr, "key receiver exists");
    if (QWidget *ownerWindow = receiver->window())
    {
        ownerWindow->raise();
        ownerWindow->activateWindow();
    }
    if (receiver->focusPolicy() != Qt::NoFocus && !receiver->hasFocus())
    {
        receiver->setFocus(Qt::OtherFocusReason);
    }
    VaporViewTest::processEventsFor(40);
    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(receiver, &event);
    VaporViewTest::processEventsFor(50);
}

void sendMouseClick(QWidget *receiver)
{
    require(receiver != nullptr, "mouse click receiver exists");
    const QPointF localPos(receiver->rect().center());
    const QPointF scenePos = localPos;
    const QPointF globalPos = receiver->mapToGlobal(localPos.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress,
                      localPos,
                      scenePos,
                      globalPos,
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(receiver, &press);
    QMouseEvent release(QEvent::MouseButtonRelease,
                        localPos,
                        scenePos,
                        globalPos,
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(receiver, &release);
    VaporViewTest::processEventsFor(80);
}

void sendMouseEnter(QWidget *receiver)
{
    require(receiver != nullptr, "mouse enter receiver exists");
    QEvent event(QEvent::Enter);
    QApplication::sendEvent(receiver, &event);
    VaporViewTest::processEventsFor(80);
}

void resetToRootKeyboardMenu(QWidget *panel, QWidget *subMenu, QWidget *nestedMenu)
{
    require(panel != nullptr, "title menu panel exists for root reset");
    if (nestedMenu)
    {
        nestedMenu->hide();
        if (QWidget *nestedWindow = nestedMenu->window())
        {
            nestedWindow->hide();
        }
    }
    if (subMenu)
    {
        subMenu->hide();
        if (QWidget *subWindow = subMenu->window())
        {
            subWindow->hide();
        }
    }
    panel->show();
    panel->raise();
    panel->activateWindow();
    panel->setFocus(Qt::OtherFocusReason);
    VaporViewTest::processEventsFor(80);
}

}  // namespace

int main(int argc, char **argv)
{
    QTemporaryDir settingsDirectory;
    require(settingsDirectory.isValid(), "temporary settings directory created");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDirectory.path());

    QApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("VaporViewTitleApplicationMenuTest"));
    application.setApplicationName(QStringLiteral("title_application_menu_test"));

    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
    settings.setValue(QStringLiteral("dark_theme_enabled"), false);
    settings.setValue(QStringLiteral("font_scale_percent"), 100);
    settings.setValue(QStringLiteral("recording_directory"), settingsDirectory.path());
    settings.sync();

    MainWindow window;
    window.resize(1100, 720);
    window.show();
    require(VaporViewTest::waitForWindowExposed(&window),
            "main window exposed for title application menu test");

    auto *titleMenuButton = window.findChild<QToolButton *>(QStringLiteral("titleBarMenuButton"));
    require(titleMenuButton != nullptr, "title bar menu button exists");
    require(titleMenuButton->focusPolicy() == Qt::StrongFocus,
            "title bar menu button accepts restored keyboard focus");
    require(titleMenuButton->accessibleName() == QStringLiteral("菜单"),
            "titleBarMenuButton exposes a localized accessible name");

    titleMenuButton->click();
    VaporViewTest::processEventsFor(80);

    auto *panel = window.findChild<QFrame *>(QStringLiteral("titleApplicationPanel"));
    auto *subMenu = window.findChild<QFrame *>(QStringLiteral("titleApplicationSubMenu"));
    auto *nestedMenu = window.findChild<QFrame *>(QStringLiteral("titleApplicationNestedMenu"));
    auto *mainMenu = window.findChild<QFrame *>(QStringLiteral("titleApplicationMainMenu"));
    require(panel && mainMenu && subMenu && nestedMenu,
            "title application menu keeps all three existing menu containers");
    require(window.findChild<QMenu *>(QStringLiteral("titleApplicationPanel")) == nullptr,
            "title application menu does not use a system QMenu");
    require(panel->isVisible(), "title application menu panel is visible");

    const QList<QToolButton *> rootRows = menuRows(mainMenu, Qt::FindDirectChildrenOnly);
    require(rootRows.size() >= 4, "title application menu exposes root rows as QToolButtons");
    for (QToolButton *row : rootRows)
    {
        require(!row->hasFocus() && !row->property("keyboardFocus").toBool(),
                "opening title application menu leaves root rows unhighlighted");
    }

    QToolButton *mouseFileRootRow = rootRows.first();
    sendMouseEnter(mouseFileRootRow);
    require(subMenu->isVisible(), "mouse hover opens the File submenu");
    const QList<QToolButton *> fileSubmenuRows = visibleMenuRows(subMenu);
    require(!fileSubmenuRows.isEmpty(), "File submenu exposes command rows");
    require(!fileSubmenuRows.first()->hasFocus() &&
                !fileSubmenuRows.first()->property("keyboardFocus").toBool(),
            "mouse-opened submenu leaves its first row unhighlighted");

    QToolButton *developerRootRow = rootRows.value(2);
    sendMouseEnter(developerRootRow);
    require(subMenu->isVisible(), "mouse hover opens the Developer submenu");
    QToolButton *csvRateRow = findRow(subMenu, QStringLiteral("titleMenuDeviceCsvRecordingRateAction"));
    require(csvRateRow != nullptr, "Developer submenu exposes the CSV-rate nested row");
    sendMouseEnter(csvRateRow);
    require(nestedMenu->isVisible(), "mouse hover opens the CSV-rate nested submenu");
    const QList<QToolButton *> csvRateRows = visibleMenuRows(nestedMenu);
    require(!csvRateRows.isEmpty(), "CSV-rate nested submenu exposes command rows");
    require(!csvRateRows.first()->hasFocus() &&
                !csvRateRows.first()->property("keyboardFocus").toBool(),
            "mouse-opened nested submenu leaves its first row unhighlighted");

    panel->hide();
    subMenu->window()->hide();
    nestedMenu->window()->hide();
    VaporViewTest::processEventsFor(30);
    titleMenuButton->click();
    VaporViewTest::processEventsFor(80);

    QSet<QString> objectNames;
    for (QToolButton *row : rootRows)
    {
        require(!row->objectName().isEmpty(), "root menu row has a stable object name");
        require(!objectNames.contains(row->objectName()), "root menu row object names are unique");
        objectNames.insert(row->objectName());
        require(!row->accessibleName().isEmpty(), "root menu row has an accessible name");
        require(row->focusPolicy() == Qt::StrongFocus, "root menu row accepts keyboard focus");
        require(row->defaultAction() != nullptr &&
                    row->defaultAction()->objectName() == row->objectName(),
                "root menu row is bound to the QAction with the same command ID");
    }

    resetToRootKeyboardMenu(panel, subMenu, nestedMenu);
    auto *firstRow = rootRows.first();
    sendKey(panel, Qt::Key_Down);
    require(QApplication::focusWidget() == firstRow,
            "Down enters the title application menu at the first root row");
    sendKey(firstRow, Qt::Key_Down);
    require(QApplication::focusWidget() == rootRows.value(1),
            "Down moves to the next root menu row");
    sendKey(rootRows.value(1), Qt::Key_Home);
    require(QApplication::focusWidget() == rootRows.first(),
            "Home moves to the first root menu row");
    sendKey(rootRows.first(), Qt::Key_End);
    require(QApplication::focusWidget() == rootRows.last(),
            "End moves to the last root menu row");
    sendKey(rootRows.last(), Qt::Key_Up);
    require(QApplication::focusWidget() == rootRows.value(rootRows.size() - 2),
            "Up moves to the previous root menu row");

    rootRows.first()->setFocus(Qt::OtherFocusReason);
    sendKey(rootRows.first(), Qt::Key_Right);
    require(subMenu->isVisible(), "Right opens the first-level submenu");
    const QList<QToolButton *> visibleSubmenuRows = visibleMenuRows(subMenu);
    require(!visibleSubmenuRows.isEmpty() && visibleSubmenuRows.first()->hasFocus(),
            "opening a submenu focuses its first row");

    const QStringList requiredActionNames{
        QStringLiteral("titleMenuRecordingFolderAction"),
        QStringLiteral("titleMenuDataViewerAction"),
        QStringLiteral("titleMenuExitAction"),
        QStringLiteral("titleMenuViewSizeIncreaseAction"),
        QStringLiteral("titleMenuViewSizeStandardAction"),
        QStringLiteral("titleMenuViewSizeDecreaseAction"),
        QStringLiteral("titleMenuViewLogPanelAction"),
        QStringLiteral("titleMenuLanguageChineseAction"),
        QStringLiteral("titleMenuLanguageEnglishAction")
    };
    for (const QString& actionName : requiredActionNames)
    {
        QAction *action = window.findChild<QAction *>(actionName);
        require(action != nullptr, "title menu command QAction has the requested stable ID");
        require(action->property("titleMenuCommandId").toString() == actionName,
                "title menu QAction carries its stable command ID property");
    }

    require(findRow(subMenu, QStringLiteral("titleMenuRecordingFolderAction")) != nullptr,
            "recording-folder command row uses its stable object name");
    require(findRow(subMenu, QStringLiteral("titleMenuDataViewerAction")) != nullptr,
            "data-viewer command row uses its stable object name");
    require(findRow(subMenu, QStringLiteral("titleMenuExitAction")) != nullptr,
            "exit command row uses its stable object name");

    auto *viewLogAction = window.findChild<QAction *>(QStringLiteral("titleMenuViewLogPanelAction"));
    require(viewLogAction != nullptr, "View log panel action exists for trigger-count regression");
    int viewLogTriggerCount = 0;
    QObject::connect(viewLogAction, &QAction::triggered, &window, [&viewLogTriggerCount]() {
        ++viewLogTriggerCount;
    });

    auto reopenViewSubmenu = [&]() -> QToolButton * {
        if (!panel->isVisible())
        {
            titleMenuButton->click();
            VaporViewTest::processEventsFor(80);
        }
        auto *viewSectionRow = rootRows.value(1);
        require(viewSectionRow != nullptr, "View section row exists for trigger regression");
        viewSectionRow->setFocus(Qt::OtherFocusReason);
        sendKey(viewSectionRow, Qt::Key_Right);
        require(subMenu->isVisible(), "View submenu opens for trigger regression");
        auto *row = findRow(subMenu, QStringLiteral("titleMenuViewLogPanelAction"));
        require(row != nullptr && row->isEnabled(),
                "View log panel row is available for trigger regression");
        row->setFocus(Qt::OtherFocusReason);
        return row;
    };

    QToolButton *triggerRow = reopenViewSubmenu();
    sendKey(triggerRow, Qt::Key_Enter);
    require(viewLogTriggerCount == 1,
            "Enter triggers the focused title-menu QAction exactly once");
    require(!panel->isVisible(),
            "Enter-triggered command closes the title menu");

    triggerRow = reopenViewSubmenu();
    sendKey(triggerRow, Qt::Key_Space);
    require(viewLogTriggerCount == 2,
            "Space triggers the focused title-menu QAction exactly once");
    require(!panel->isVisible(),
            "Space-triggered command closes the title menu");

    triggerRow = reopenViewSubmenu();
    sendMouseClick(triggerRow);
    require(viewLogTriggerCount == 3,
            "a single mouse click triggers the title-menu QAction exactly once");
    require(!panel->isVisible(),
            "mouse-click command closes the title menu");

    titleMenuButton->click();
    VaporViewTest::processEventsFor(80);
    panel = window.findChild<QFrame *>(QStringLiteral("titleApplicationPanel"));
    subMenu = window.findChild<QFrame *>(QStringLiteral("titleApplicationSubMenu"));
    nestedMenu = window.findChild<QFrame *>(QStringLiteral("titleApplicationNestedMenu"));
    mainMenu = window.findChild<QFrame *>(QStringLiteral("titleApplicationMainMenu"));
    require(panel && mainMenu && subMenu && nestedMenu,
            "title application menu containers still exist after trigger regression checks");
    const QList<QToolButton *> reopenedRootRows = menuRows(mainMenu, Qt::FindDirectChildrenOnly);
    require(reopenedRootRows.size() == rootRows.size(),
            "title application menu root rows reopen consistently after trigger regression checks");

    auto *viewRootRow = reopenedRootRows.value(1);
    auto *fileRootRow = reopenedRootRows.first();
    fileRootRow->setFocus(Qt::OtherFocusReason);
    sendKey(fileRootRow, Qt::Key_Right);
    require(subMenu->isVisible(), "Right reopens the File submenu after trigger regression checks");
    const QList<QToolButton *> reopenedFileSubmenuRows = visibleMenuRows(subMenu);
    require(!reopenedFileSubmenuRows.isEmpty() && reopenedFileSubmenuRows.first()->hasFocus(),
            "File submenu focus is restored before the Left-key regression check");
    sendKey(QApplication::focusWidget(), Qt::Key_Left);
    if (!fileRootRow->hasFocus())
    {
        QWidget *focusWidget = QApplication::focusWidget();
        QWidget *activeWindow = QApplication::activeWindow();
        std::cerr << "left focus diagnostic: fileHasFocus=" << fileRootRow->hasFocus()
                  << " focusObject=" << (focusWidget ? focusWidget->objectName().toStdString() : "<null>")
                  << " activeWindow=" << (activeWindow ? activeWindow->objectName().toStdString() : "<null>")
                  << " subVisible=" << subMenu->isVisible()
                  << " panelVisible=" << panel->isVisible()
                  << '\n';
    }
    require(fileRootRow->hasFocus(), "Left returns focus from a submenu to its root row");
    viewRootRow->setFocus(Qt::OtherFocusReason);
    sendKey(viewRootRow, Qt::Key_Right);
    require(subMenu->isVisible(), "Right reopens the View submenu");
    auto *viewSizeIncreaseRow = findRow(subMenu, QStringLiteral("titleMenuViewSizeIncreaseAction"));
    auto *viewSizeStandardRow = findRow(subMenu, QStringLiteral("titleMenuViewSizeStandardAction"));
    auto *viewSizeDecreaseRow = findRow(subMenu, QStringLiteral("titleMenuViewSizeDecreaseAction"));
    require(viewSizeIncreaseRow != nullptr &&
                viewSizeStandardRow != nullptr &&
                viewSizeDecreaseRow != nullptr,
            "View submenu exposes the three view-size choices");
    require(viewSizeIncreaseRow->defaultAction()->text() == QStringLiteral("放大") &&
                viewSizeStandardRow->defaultAction()->text() == QStringLiteral("标准") &&
                viewSizeDecreaseRow->defaultAction()->text() == QStringLiteral("缩小"),
            "View size choices use simple labels without percentages");
    require(!viewSizeIncreaseRow->defaultAction()->isChecked() &&
                viewSizeStandardRow->defaultAction()->isChecked() &&
                !viewSizeDecreaseRow->defaultAction()->isChecked(),
            "standard view size is checked at the default 100 percent setting");
    require(!viewSizeIncreaseRow->defaultAction()->isCheckable() &&
                viewSizeStandardRow->defaultAction()->isCheckable() &&
                !viewSizeDecreaseRow->defaultAction()->isCheckable(),
            "only the standard view-size command behaves like a checked state");
    require(findRow(subMenu, QStringLiteral("titleMenuFontTinyAction")) == nullptr &&
                findRow(subMenu, QStringLiteral("titleMenuFontExtraSmallAction")) == nullptr &&
                findRow(subMenu, QStringLiteral("titleMenuFontSmallAction")) == nullptr &&
                findRow(subMenu, QStringLiteral("titleMenuFontNormalAction")) == nullptr &&
                findRow(subMenu, QStringLiteral("titleMenuFontLargeAction")) == nullptr &&
                findRow(subMenu, QStringLiteral("titleMenuFontExtraLargeAction")) == nullptr,
            "View submenu no longer exposes percentage-based size choices");
    auto *languageRow = findRow(subMenu, QStringLiteral("titleMenuLanguageAction"));
    require(languageRow != nullptr && languageRow->isEnabled(),
            "View submenu exposes the language row as a real button");
    languageRow->setFocus(Qt::OtherFocusReason);
    sendKey(languageRow, Qt::Key_Right);
    require(nestedMenu->isVisible(), "Right opens the nested language submenu");
    require(findRow(nestedMenu, QStringLiteral("titleMenuLanguageChineseAction")) != nullptr &&
                findRow(nestedMenu, QStringLiteral("titleMenuLanguageEnglishAction")) != nullptr,
            "language submenu exposes both stable language command rows");

    sendKey(QApplication::focusWidget(), Qt::Key_Escape);
    require(!nestedMenu->isVisible() && languageRow->hasFocus(),
            "Esc closes the nested submenu and restores parent-row focus");
    sendKey(languageRow, Qt::Key_Escape);
    require(!subMenu->isVisible() && viewRootRow->hasFocus(),
            "Esc closes the first-level submenu and restores root-row focus");
    sendKey(viewRootRow, Qt::Key_Escape);
    require(!panel->isVisible() && titleMenuButton->hasFocus(),
            "Esc closes the title menu and restores focus to titleBarMenuButton");

    auto storedFontScalePercent = []() {
        QSettings storedSettings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
        storedSettings.sync();
        return storedSettings.value(QStringLiteral("font_scale_percent"), 0).toInt();
    };
    QAction *increaseViewSizeAction = viewSizeIncreaseRow->defaultAction();
    QAction *standardViewSizeAction = viewSizeStandardRow->defaultAction();
    QAction *decreaseViewSizeAction = viewSizeDecreaseRow->defaultAction();
    require(increaseViewSizeAction != nullptr &&
                standardViewSizeAction != nullptr &&
                decreaseViewSizeAction != nullptr,
            "view-size rows keep their command actions for relative-step regression");
    increaseViewSizeAction->trigger();
    require(storedFontScalePercent() == 110,
            "Zoom In increases the current view size by ten percent");
    increaseViewSizeAction->trigger();
    require(storedFontScalePercent() == 120,
            "Zoom In is relative to the updated view size");
    decreaseViewSizeAction->trigger();
    require(storedFontScalePercent() == 110,
            "Zoom Out decreases the current view size by ten percent");
    standardViewSizeAction->trigger();
    require(storedFontScalePercent() == 100,
            "Standard resets the view size to one hundred percent");
    for (int i = 0; i < 9; ++i)
    {
        increaseViewSizeAction->trigger();
    }
    require(storedFontScalePercent() == 180,
            "Zoom In clamps the view size at one hundred eighty percent");
    increaseViewSizeAction->trigger();
    require(storedFontScalePercent() == 180,
            "Zoom In keeps the view size at the upper limit");
    for (int i = 0; i < 13; ++i)
    {
        decreaseViewSizeAction->trigger();
    }
    require(storedFontScalePercent() == 60,
            "Zoom Out clamps the view size at sixty percent");
    decreaseViewSizeAction->trigger();
    require(storedFontScalePercent() == 60,
            "Zoom Out keeps the view size at the lower limit");
    standardViewSizeAction->trigger();
    require(storedFontScalePercent() == 100,
            "Standard resets the bounded view size to one hundred percent");
    VaporViewTest::processEventsFor(120);

    std::cout << "title_application_menu_test passed\n";
    return 0;
}
