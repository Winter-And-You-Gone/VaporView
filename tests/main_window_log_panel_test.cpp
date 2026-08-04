#include "LogService.h"
#include "ground/main/MainWindow.h"
#include "ground/main/UiLogModel.h"
#include "shared/config/SettingsWriteBarrier.h"
#include "test_ui_helpers.h"

#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>

#include <cstdlib>
#include <iostream>
#include <memory>

namespace
{

struct MainWindowSettingsBackup
{
    MainWindowSettingsBackup()
    {
        QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
        hadLogAutoFollow = settings.contains(QStringLiteral("log_auto_follow"));
        logAutoFollow = settings.value(QStringLiteral("log_auto_follow"));
        hadLogViewMode = settings.contains(QStringLiteral("log_view_mode"));
        logViewMode = settings.value(QStringLiteral("log_view_mode"));
        settings.remove(QStringLiteral("log_auto_follow"));
        settings.remove(QStringLiteral("log_view_mode"));
        settings.sync();
    }

    void restore()
    {
        QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
        if (hadLogAutoFollow)
        {
            settings.setValue(QStringLiteral("log_auto_follow"), logAutoFollow);
        }
        else
        {
            settings.remove(QStringLiteral("log_auto_follow"));
        }
        if (hadLogViewMode)
        {
            settings.setValue(QStringLiteral("log_view_mode"), logViewMode);
        }
        else
        {
            settings.remove(QStringLiteral("log_view_mode"));
        }
        settings.sync();
    }

    bool hadLogAutoFollow = false;
    QVariant logAutoFollow;
    bool hadLogViewMode = false;
    QVariant logViewMode;
};

MainWindowSettingsBackup *settingsBackup = nullptr;

[[noreturn]] void fail(const char *message)
{
    std::cerr << "FAIL: " << message << '\n';
    VaporView::setSettingsWritesSuspended(false);
    if (settingsBackup)
    {
        settingsBackup->restore();
    }
    std::exit(1);
}

void require(bool condition, const char *message)
{
    if (!condition)
    {
        fail(message);
    }
}

void publishRecord(VaporView::LogService& logService,
                   VaporView::LogLevel level,
                   const QString& source,
                   const QString& category,
                   const QString& message,
                   QVariantMap fields = {})
{
    logService.publish(level, source, category, message, fields);
    VaporViewTest::processEventsFor(90);
}

QToolButton *findLogModeButton(MainWindow& window, const QString& text)
{
    const QList<QToolButton*> buttons = window.findChildren<QToolButton *>(QStringLiteral("logViewModeButton"));
    for (QToolButton *button : buttons)
    {
        if (button && button->text() == text)
        {
            return button;
        }
    }
    return nullptr;
}

QToolButton *findAccessibleToolButton(MainWindow& window, const QString& accessibleName)
{
    const QList<QToolButton*> buttons = window.findChildren<QToolButton *>();
    for (QToolButton *button : buttons)
    {
        if (button && button->accessibleName() == accessibleName)
        {
            return button;
        }
    }
    return nullptr;
}

QToolButton *findActionButton(MainWindow& window, const QString& actionText, const QString& toolTipText)
{
    const QList<QToolButton*> buttons = window.findChildren<QToolButton *>();
    for (QToolButton *button : buttons)
    {
        if (!button)
        {
            continue;
        }
        if ((button->defaultAction() && button->defaultAction()->text() == actionText) ||
            button->toolTip() == toolTipText)
        {
            return button;
        }
    }
    return nullptr;
}

}  // namespace

int main(int argc, char **argv)
{
    QTemporaryDir settingsDirectory;
    require(settingsDirectory.isValid(), "temporary settings directory created");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDirectory.path());

    QTemporaryDir logDirectory;
    require(logDirectory.isValid(), "temporary log directory created");

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("VaporView"));
    app.setOrganizationName(QStringLiteral("VaporView"));

    MainWindowSettingsBackup backup;
    settingsBackup = &backup;
    VaporView::setSettingsWritesSuspended(true);

    VaporView::LogService logService(QStringLiteral("VaporViewLogPanelTest"),
                                     nullptr,
                                     logDirectory.path(),
                                     logDirectory.path());
    auto window = std::make_unique<MainWindow>();
    window->show();
    require(VaporViewTest::waitForWindowExposed(window.get()), "main window exposed for log panel test");

    auto *logList = window->findChild<QListView *>(QStringLiteral("logListView"));
    auto *searchButton = findAccessibleToolButton(*window, QStringLiteral("logSearchButton"));
    auto *searchMenu = window->findChild<QMenu *>(QStringLiteral("logSearchMenu"));
    auto *searchEdit = window->findChild<QLineEdit *>(QStringLiteral("logSearchEdit"));
    auto *newEntriesButton = window->findChild<QPushButton *>(QStringLiteral("logNewEntriesButton"));
    auto *attentionAction = window->findChild<QAction *>(QStringLiteral("logFilterAttentionMenuAction"));
    auto *allAction = window->findChild<QAction *>(QStringLiteral("logFilterAllMenuAction"));
    auto *debugAction = window->findChild<QAction *>(QStringLiteral("logFilterDebugMenuAction"));
    auto *followAction = window->findChild<QAction *>(QStringLiteral("logFilterAutoFollowMenuAction"));
    auto *clearButton = findActionButton(*window,
                                         QStringLiteral("清空显示"),
                                         QStringLiteral("仅清空当前显示，不删除日志文件"));

    require(logList && logList->model(), "log list view is backed by a model");
    require(searchButton != nullptr, "log search icon button exists");
    require(searchMenu != nullptr, "log search popup menu exists");
    require(searchEdit != nullptr, "log search edit exists inside the popup");
    require(newEntriesButton != nullptr, "new log indicator button exists");
    require(findLogModeButton(*window, QStringLiteral("关注")) == nullptr &&
                findLogModeButton(*window, QStringLiteral("全部")) == nullptr &&
                findLogModeButton(*window, QStringLiteral("调试")) == nullptr,
            "positive log view buttons are not duplicated outside the filter menu");
    require(window->findChild<QToolButton *>(QStringLiteral("logAutoFollowButton")) == nullptr,
            "auto-follow is not duplicated outside the filter menu");
    require(attentionAction && allAction && debugAction && followAction, "log filter menu actions exist");
    require(clearButton != nullptr, "clear display button exists");
    require(!searchEdit->isVisible(), "log search edit is hidden until the search icon is clicked");
    searchButton->click();
    VaporViewTest::processEventsFor(90);
    require(searchMenu->isVisible() && searchEdit->isVisible(),
            "clicking the search icon opens the search popup");
    searchMenu->hide();
    VaporViewTest::processEventsFor(30);

    publishRecord(logService,
                  VaporView::LogLevel::Info,
                  QStringLiteral("Ground"),
                  QStringLiteral("ordinary"),
                  QStringLiteral("普通 Info"),
                  {{QStringLiteral("event"), QStringLiteral("ordinary_info")}});
    publishRecord(logService,
                  VaporView::LogLevel::Debug,
                  QStringLiteral("Qt"),
                  QStringLiteral("qt"),
                  QStringLiteral("调试细节"),
                  {{QStringLiteral("event"), QStringLiteral("debug_line")}});
    publishRecord(logService,
                  VaporView::LogLevel::Warning,
                  QStringLiteral("Ground"),
                  QStringLiteral("device.connection"),
                  QStringLiteral("设备连接失败。"),
                  {{QStringLiteral("event"), QStringLiteral("device_connection_failed")},
                   {QStringLiteral("error_code"), QStringLiteral("DEVICE_CONNECTION_FAILED")}});
    publishRecord(logService,
                  VaporView::LogLevel::Info,
                  QStringLiteral("Ground"),
                  QStringLiteral("recording"),
                  QStringLiteral("记录已开始。"),
                  {{QStringLiteral("event"), QStringLiteral("recording_started")},
                   {QStringLiteral("ui_visibility"), QStringLiteral("attention")}});

    require(logList->model()->rowCount() == 2,
            "attention view shows warning and explicit attention Info only");

    allAction->trigger();
    VaporViewTest::processEventsFor(30);
    require(logList->model()->rowCount() == 3, "all view includes ordinary Info but keeps Debug hidden");

    debugAction->trigger();
    VaporViewTest::processEventsFor(30);
    require(logList->model()->rowCount() == 4, "debug view includes Debug records");

    searchEdit->setText(QStringLiteral("DEVICE_CONNECTION_FAILED"));
    VaporViewTest::processEventsFor(30);
    require(logList->model()->rowCount() == 1, "search matches structured error_code");
    searchEdit->clear();
    VaporViewTest::processEventsFor(30);

    clearButton->click();
    VaporViewTest::processEventsFor(90);
    require(logList->model()->rowCount() == 0, "clear display leaves the visible panel empty");

    attentionAction->trigger();
    followAction->trigger();
    VaporViewTest::processEventsFor(30);
    publishRecord(logService,
                  VaporView::LogLevel::Error,
                  QStringLiteral("Ground"),
                  QStringLiteral("device.connection"),
                  QStringLiteral("设备连接失败。"),
                  {{QStringLiteral("event"), QStringLiteral("device_connection_failed")},
                   {QStringLiteral("error_code"), QStringLiteral("DEVICE_CONNECTION_FAILED")}});
    require(newEntriesButton->isVisible(), "new log indicator appears when auto follow is disabled");
    require(newEntriesButton->text().contains(QStringLiteral("1")), "new log indicator counts pending visible rows");
    newEntriesButton->click();
    VaporViewTest::processEventsFor(30);
    require(!newEntriesButton->isVisible(), "clicking new log indicator clears unread state");

    clearButton->click();
    VaporViewTest::processEventsFor(90);
    require(logList->model()->rowCount() == 0, "clear display resets before pending overload test");
    logService.publish(VaporView::LogLevel::Critical,
                       QStringLiteral("Ground"),
                       QStringLiteral("pending.overload"),
                       QStringLiteral("关键待处理日志"),
                       {{QStringLiteral("event"), QStringLiteral("pending_overload_critical")},
                        {QStringLiteral("ui_visibility"), QStringLiteral("attention")}});
    for (int i = 0; i < VaporView::Ground::Main::kMaxPendingUiLogRecords; ++i)
    {
        logService.publish(VaporView::LogLevel::Info,
                           QStringLiteral("Ground"),
                           QStringLiteral("pending.overload"),
                           QStringLiteral("普通待处理日志 %1").arg(i),
                           {{QStringLiteral("event"), QStringLiteral("pending_overload_info_%1").arg(i)},
                            {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
    VaporViewTest::processEventsFor(140);
    searchEdit->setText(QStringLiteral("pending_overload_critical"));
    VaporViewTest::processEventsFor(30);
    require(logList->model()->rowCount() == 1,
            "pending overload keeps Critical instead of dropping it behind ordinary Info");
    searchEdit->clear();
    VaporViewTest::processEventsFor(30);

    clearButton->click();
    VaporViewTest::processEventsFor(90);
    followAction->trigger();
    VaporViewTest::processEventsFor(30);
    for (int i = 0; i < 80; ++i)
    {
        logService.publish(VaporView::LogLevel::Warning,
                           QStringLiteral("Ground"),
                           QStringLiteral("scroll.history"),
                           QStringLiteral("滚动测试警告 %1").arg(i),
                           {{QStringLiteral("event"), QStringLiteral("scroll_history_warning_%1").arg(i)}});
    }
    VaporViewTest::processEventsFor(140);
    logList->scrollToBottom();
    VaporViewTest::processEventsFor(30);
    QScrollBar *logScrollBar = logList->verticalScrollBar();
    require(logScrollBar != nullptr, "log list has a vertical scrollbar");
    require(logScrollBar->maximum() > 0, "log list has enough rows to scroll");
    const int historyScrollValue = logScrollBar->maximum() / 2;
    logScrollBar->setValue(historyScrollValue);
    VaporViewTest::processEventsFor(30);
    const int beforeNewLogScrollValue = logScrollBar->value();
    require(beforeNewLogScrollValue < logScrollBar->maximum(), "manual scroll moves away from bottom");
    logService.publish(VaporView::LogLevel::Warning,
                       QStringLiteral("Ground"),
                       QStringLiteral("scroll.history"),
                       QStringLiteral("滚动测试新增警告"),
                       {{QStringLiteral("event"), QStringLiteral("scroll_history_new_warning")}});
    VaporViewTest::processEventsFor(140);
    require(logScrollBar->value() == beforeNewLogScrollValue,
            "new log does not jump when auto follow is enabled but the user is viewing history");
    require(newEntriesButton->isVisible(), "new log indicator appears while viewing history with auto follow enabled");

    window.reset();
    VaporView::setSettingsWritesSuspended(false);
    backup.restore();
    settingsBackup = nullptr;
    return 0;
}
