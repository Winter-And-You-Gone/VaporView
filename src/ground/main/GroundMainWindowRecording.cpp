#include "ground/main/GroundMainWindowImplementation.h"

#include <utility>

namespace
{

QString recordingStatusHtmlFromPlainText(const QString& plainText)
{
    QString html = QStringLiteral(
        "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">");
    const QStringList lines = plainText.split(QLatin1Char('\n'));
    for (int row = 0; row < lines.size(); ++row)
    {
        const QString line = lines.at(row);
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
        {
            html += QStringLiteral("<tr><td colspan=\"3\">&nbsp;</td></tr>");
            continue;
        }

        const bool fullWidthLine = row == 0 || trimmed.endsWith(QChar(0xFF1A)) ||
                                   trimmed.endsWith(QLatin1Char(':'));
        if (fullWidthLine)
        {
            html += QStringLiteral(
                        "<tr><td colspan=\"2\" style=\"white-space:nowrap; padding-top:%1px;\">%2</td></tr>")
                        .arg(row == 0 ? 0 : 4)
                        .arg(trimmed.toHtmlEscaped());
            continue;
        }

        int separator = line.indexOf(QChar(0xFF1A));
        int separatorWidth = 1;
        if (separator < 0)
        {
            separator = line.indexOf(QStringLiteral(": "));
            separatorWidth = 2;
        }
        if (separator < 0)
        {
            html += QStringLiteral(
                        "<tr><td colspan=\"2\" style=\"white-space:nowrap;\">%1</td></tr>")
                        .arg(trimmed.toHtmlEscaped());
            continue;
        }

        const QString label = line.left(separator + 1).trimmed();
        QString value = line.mid(separator + separatorWidth).trimmed();
        QString unit;
        const int unitSeparator = value.lastIndexOf(QLatin1Char(' '));
        if (unitSeparator > 0)
        {
            const QString candidate = value.mid(unitSeparator + 1);
            if (candidate == QStringLiteral("行") ||
                candidate == QStringLiteral("帧") ||
                candidate == QStringLiteral("条") ||
                candidate == QStringLiteral("rows") ||
                candidate == QStringLiteral("frames") ||
                candidate == QStringLiteral("features") ||
                candidate == QStringLiteral("records"))
            {
                unit = candidate;
                value = value.left(unitSeparator).trimmed();
            }
        }
        const QString displayedValue = unit.isEmpty()
            ? value
            : QStringLiteral("%1 %2").arg(value, unit);
        const QString displayedValueHtml =
            displayedValue.toHtmlEscaped() + QStringLiteral("&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;");

        html += QStringLiteral(
            "<tr>"
            "<td style=\"white-space:nowrap;\">%1</td>"
            "<td align=\"right\" style=\"white-space:nowrap; padding-left:8px;\">%2</td>"
            "</tr>")
            .arg(label.toHtmlEscaped(),
                 displayedValueHtml);
    }
    html += QStringLiteral("</table>");
    return html;
}


void publishPendingUiLogDropNotice(quint64 dropped)
{
    if (dropped == 0)
    {
        return;
    }

    VaporView::LogService::withCurrentInstance([dropped](VaporView::LogService& logService) {
        logService.publish(VaporView::LogLevel::Info,
                           QStringLiteral("Ground"),
                           QStringLiteral("ui.log"),
                           QStringLiteral("桌面日志 UI 队列已满，已丢弃部分待显示记录。"),
                           {{QStringLiteral("event"), QStringLiteral("ui_log_pending_queue_dropped")},
                            {QStringLiteral("ui_visibility"), QStringLiteral("hidden")},
                            {QStringLiteral("dropped_count"), static_cast<qulonglong>(dropped)},
                            {QStringLiteral("pending_limit"), kMaxPendingUiLogRecords}});
    });
}

}  // namespace

void MainWindow::publishGroundLog(VaporView::LogLevel level,
                                  const QString& category,
                                  const QString& event,
                                  const QString& message,
                                  QVariantMap fields)
{
    fields.insert(QStringLiteral("event"), event);
    fields.insert(QStringLiteral("ui_visible"), true);
    if (!fields.contains(QStringLiteral("ui_visibility")))
    {
        fields.insert(QStringLiteral("ui_visibility"),
                      level >= VaporView::LogLevel::Warning ? QStringLiteral("attention")
                                                            : QStringLiteral("details"));
    }

    VaporView::LogRecord record;
    record.level = level;
    record.source = QStringLiteral("Ground");
    record.category = category;
    record.message = message;
    record.fields = std::move(fields);

    const bool published = VaporView::LogService::withCurrentInstance([&](VaporView::LogService& logService) {
        logService.publish(record);
    });
    if (!published)
    {
        enqueueUiLogRecord(record);
    }
    state_->has_inline_progress_log_ = false;
}

void MainWindow::publishTemperatureCommandLog(VaporView::LogLevel level,
                                              const QString& event,
                                              const QString& message,
                                              QVariantMap fields)
{
    fields.insert(QStringLiteral("device"), fields.value(QStringLiteral("device"), QStringLiteral("RD105")));
    fields.insert(QStringLiteral("device_id"),
                  fields.value(QStringLiteral("device_id"), QStringLiteral("temperature_controller")));
    publishGroundLog(level,
                     QStringLiteral("device.temperature.command"),
                     event,
                     message,
                     std::move(fields));
}

void MainWindow::enqueueUiLogRecord(const VaporView::LogRecord& record)
{
    if (record.category == QStringLiteral("ui.progress") ||
        VaporView::Ground::Main::uiLogEvent(record) == QStringLiteral("ui_log_pending_queue_dropped"))
    {
        return;
    }
    if (state_->pending_ui_log_records_.size() >= kMaxPendingUiLogRecords)
    {
        const int dropRow = VaporView::Ground::Main::uiLogPendingDropRow(
            state_->pending_ui_log_records_, record);
        ++state_->pending_ui_log_records_dropped_;
        if (dropRow < 0)
        {
            return;
        }
        state_->pending_ui_log_records_.removeAt(dropRow);
    }
    state_->pending_ui_log_records_.append(record);
    if (state_->log_flush_timer_)
    {
        if (!state_->log_flush_timer_->isActive())
        {
            state_->log_flush_timer_->start();
        }
        return;
    }
    flushPendingUiLogRecords();
}

void MainWindow::flushPendingUiLogRecords()
{
    if (state_->pending_ui_log_records_.isEmpty() || !state_->log_model_)
    {
        return;
    }

    if (state_->log_flush_timer_)
    {
        state_->log_flush_timer_->stop();
    }

    const bool wasNearBottom = isLogViewNearBottom();
    const bool shouldFollow = state_->log_auto_follow_enabled_ && wasNearBottom;
    const quint64 droppedPendingRecords = std::exchange(state_->pending_ui_log_records_dropped_, 0ULL);
    QVector<VaporView::LogRecord> records;
    records.swap(state_->pending_ui_log_records_);
    publishPendingUiLogDropNotice(droppedPendingRecords);

    int visibleInCurrentView = 0;
    int warningCount = 0;
    int errorCount = 0;
    int statusCount = 0;
    for (const VaporView::LogRecord& record : records)
    {
        if (VaporView::Ground::Main::uiLogRecordVisibleInMode(record, state_->log_view_mode_))
        {
            ++visibleInCurrentView;
        }
        const auto decision = VaporView::Ground::Main::uiLogVisibilityForRecord(record);
        if (decision.explicitVisibility &&
            decision.visibility == VaporView::Ground::Main::LogUiVisibility::Hidden)
        {
            continue;
        }
        if (record.level >= VaporView::LogLevel::Error)
        {
            ++errorCount;
        }
        else if (record.level == VaporView::LogLevel::Warning)
        {
            ++warningCount;
        }
        else if (record.level == VaporView::LogLevel::Info &&
                 decision.visibility == VaporView::Ground::Main::LogUiVisibility::Attention)
        {
            ++statusCount;
        }
    }

    state_->log_model_->appendRecords(records);

    if (shouldFollow)
    {
        QTimer::singleShot(0, this, [this]() {
            scrollLogViewToBottom();
        });
    }
    else if (visibleInCurrentView > 0)
    {
        state_->log_new_visible_count_ += visibleInCurrentView;
        state_->log_unread_warning_count_ += warningCount;
        state_->log_unread_error_count_ += errorCount;
        state_->log_unread_status_count_ += statusCount;
        updateLogUnreadUi();
    }
}

bool MainWindow::shouldShowLogRecord(const VaporView::LogRecord& record) const
{
    return VaporView::Ground::Main::uiLogRecordVisibleInMode(record, state_->log_view_mode_);
}

void MainWindow::renderLogView()
{
    if (!state_->log_filter_proxy_)
    {
        return;
    }

    state_->log_filter_proxy_->setViewMode(state_->log_view_mode_);
    state_->log_filter_proxy_->setSearchText(state_->log_search_edit_ ? state_->log_search_edit_->text() : QString());
    state_->has_inline_progress_log_ = false;
    if (state_->log_auto_follow_enabled_)
    {
        QTimer::singleShot(0, this, [this]() {
            scrollLogViewToBottom();
        });
    }
    updateLogFilterAction();
    updateLogUnreadUi();
}

void MainWindow::setLogViewMode(VaporView::Ground::Main::LogUiViewMode mode, bool persist)
{
    if (state_->log_view_mode_ == mode)
    {
        renderLogView();
        return;
    }
    state_->log_view_mode_ = mode;
    if (persist)
    {
        QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
        VaporView::setPersistentSetting(settings,
                                        QStringLiteral("log_view_mode"),
                                        VaporView::Ground::Main::uiLogViewModeToSetting(mode));
    }
    renderLogView();
}

bool MainWindow::isLogViewNearBottom() const
{
    if (!state_->log_list_view_)
    {
        return true;
    }
    QScrollBar *scrollBar = state_->log_list_view_->verticalScrollBar();
    if (!scrollBar)
    {
        return true;
    }
    return scrollBar->maximum() - scrollBar->value() <= scalePixels(32);
}

void MainWindow::scrollLogViewToBottom()
{
    if (state_->log_list_view_)
    {
        state_->log_list_view_->scrollToBottom();
    }
    clearLogUnreadState();
}

void MainWindow::updateLogFollowState()
{
    if (isLogViewNearBottom() && !state_->log_side_panel_collapsed_)
    {
        clearLogUnreadState();
    }
}

void MainWindow::clearLogUnreadState()
{
    state_->log_new_visible_count_ = 0;
    state_->log_unread_warning_count_ = 0;
    state_->log_unread_error_count_ = 0;
    state_->log_unread_status_count_ = 0;
    if (state_->log_model_)
    {
        state_->log_model_->clearUnread();
    }
    updateLogUnreadUi();
}

void MainWindow::updateLogUnreadUi()
{
    if (state_->log_new_entries_btn_)
    {
        const int count = state_->log_new_visible_count_;
        state_->log_new_entries_btn_->setVisible(count > 0);
        state_->log_new_entries_btn_->setText(state_->is_english_
            ? QStringLiteral("%1 new").arg(count)
            : QStringLiteral("%1 条新日志").arg(count));
    }
    if (state_->log_new_entries_row_)
    {
        state_->log_new_entries_row_->setVisible(state_->log_new_visible_count_ > 0);
    }
    if (state_->log_inline_title_lbl_)
    {
        const int severe = state_->log_unread_error_count_ + state_->log_unread_warning_count_;
        const int status = state_->log_unread_status_count_;
        QString text = state_->is_english_ ? QStringLiteral("Log") : QStringLiteral("日志");
        if (severe > 0)
        {
            text += QStringLiteral("  !%1").arg(severe);
        }
        else if (status > 0)
        {
            text += QStringLiteral("  %1").arg(status);
        }
        state_->log_inline_title_lbl_->setText(text);
    }
}

void MainWindow::updateLogFilterAction()
{
    if (!state_->log_filter_btn_)
    {
        return;
    }

    const QIcon checkIcon = createMenuCheckIcon(state_->dark_theme_enabled_);
    QStringList filterTexts = state_->is_english_
        ? QStringList{
              QStringLiteral("Attention"),
              QStringLiteral("All"),
              QStringLiteral("Debug"),
              QStringLiteral("Auto follow"),
              QStringLiteral("Hide source/category")}
        : QStringList{
              QStringLiteral("关注"),
              QStringLiteral("全部"),
              QStringLiteral("调试"),
              QStringLiteral("自动跟随"),
              QStringLiteral("过滤[来源/分类]")};
    const QFontMetrics filterTextMetrics(qApp ? qApp->font() : font());
    int filterTextWidth = 0;
    for (const QString& text : filterTexts)
    {
        filterTextWidth = std::max(filterTextWidth, filterTextMetrics.horizontalAdvance(text));
    }
    const int rowLeftPadding = scalePixels(18);
    const int rowRightPadding = scalePixels(14);
    const int rowSpacing = scalePixels(6);
    const int checkSlotWidth = scalePixels(18);
    const int rowHeight = scalePixels(36);
    const int menuItemWidth = rowLeftPadding + filterTextWidth + rowSpacing + checkSlotWidth + rowRightPadding;

    const auto updateAction = [this, &checkIcon, filterTextWidth, checkSlotWidth, menuItemWidth, rowHeight, rowLeftPadding, rowRightPadding, rowSpacing](QAction *action,
                                                             bool checked,
                                                             const QString& englishText,
                                                             const QString& chineseText,
                                                             const QString& englishDetail,
                                                             const QString& chineseDetail) {
        if (!action)
        {
            return;
        }
        const QString text = state_->is_english_ ? englishText : chineseText;
        const QString detail = state_->is_english_ ? englishDetail : chineseDetail;
        action->setText(text);
        action->setToolTip(detail);

        auto *widgetAction = qobject_cast<QWidgetAction *>(action);
        auto *row = widgetAction ? qobject_cast<SingleLevelPopupMenuRow *>(widgetAction->defaultWidget()) : nullptr;
        if (!row)
        {
            action->setIcon(checked ? checkIcon : QIcon());
            return;
        }

        row->setHorizontalPadding(rowLeftPadding, rowRightPadding);
        row->setRowSpacing(rowSpacing);
        row->setCheckSlotWidth(checkSlotWidth);
        row->setCheckIconSize(QSize(scalePixels(16), scalePixels(16)));
        row->setRowHeight(rowHeight);
        row->setMinimumRowWidth(menuItemWidth);
        row->setFixedWidth(menuItemWidth);
        row->setTextFixedWidth(filterTextWidth);
        row->setText(text);
        row->setToolTip(detail);
        row->setCheckIcon(checkIcon);
        row->setChecked(checked);
        row->refreshTheme();
    };

    updateAction(state_->log_filter_ack_action_,
                 state_->log_view_mode_ == VaporView::Ground::Main::LogUiViewMode::Attention,
                 QStringLiteral("Attention"),
                 QStringLiteral("关注"),
                 QStringLiteral("Show warnings, errors, critical records, and Info records explicitly marked for attention"),
                 QStringLiteral("只显示警告、错误、严重日志，以及显式标记需要关注的 Info"));
    updateAction(state_->log_filter_config_action_,
                 state_->log_view_mode_ == VaporView::Ground::Main::LogUiViewMode::All,
                 QStringLiteral("All"),
                 QStringLiteral("全部"),
                 QStringLiteral("Show Info and higher records, while keeping Debug hidden"),
                 QStringLiteral("显示 Info 及以上日志，Debug 仍保持隐藏"));
    updateAction(state_->log_filter_connection_action_,
                 state_->log_view_mode_ == VaporView::Ground::Main::LogUiViewMode::Debug,
                 QStringLiteral("Debug"),
                 QStringLiteral("调试"),
                 QStringLiteral("Show Debug diagnostics together with Info, Warning, Error, and Critical records"),
                 QStringLiteral("显示 Debug 诊断以及 Info、Warning、Error、Critical 日志"));
    updateAction(state_->log_filter_recording_action_,
                 state_->log_auto_follow_enabled_,
                 QStringLiteral("Auto follow"),
                 QStringLiteral("自动跟随"),
                 QStringLiteral("Follow new logs only while the view is already near the bottom"),
                 QStringLiteral("仅在当前接近底部时跟随新日志"));
    updateAction(state_->log_filter_source_category_action_,
                 state_->log_hide_source_category_enabled_,
                 QStringLiteral("Hide source/category"),
                 QStringLiteral("过滤[来源/分类]"),
                 QStringLiteral("Hide source/category in displayed rows while keeping full fields in log files"),
                 QStringLiteral("隐藏每条显示日志中的来源/分类，日志文件仍保留完整字段"));

    if (state_->log_filter_menu_)
    {
        state_->log_filter_menu_->setTitle(state_->is_english_ ? QStringLiteral("Log View")
                                               : QStringLiteral("日志视图"));
        state_->log_filter_menu_->refreshTheme();
        state_->log_filter_menu_->setPanelContentWidth(menuItemWidth);
    }
    if (state_->log_filter_btn_)
    {
        state_->log_filter_btn_->setIcon(createLogFilterIcon());
        state_->log_filter_btn_->setToolTip(state_->is_english_ ? QStringLiteral("Log view")
                                                 : QStringLiteral("日志视图"));
    }
    if (state_->log_search_btn_)
    {
        state_->log_search_btn_->setIcon(createLogSearchIcon());
        state_->log_search_btn_->setToolTip(state_->is_english_ ? QStringLiteral("Search logs")
                                                : QStringLiteral("搜索日志"));
    }
    if (state_->log_search_edit_)
    {
        state_->log_search_edit_->setPlaceholderText(state_->is_english_
            ? QStringLiteral("Search logs")
            : QStringLiteral("搜索日志"));
    }
}

void MainWindow::updateRecordingStatusLabel()
{
    if (state_->recording_status_title_lbl_)
    {
        state_->recording_status_title_lbl_->setText(
            state_->is_english_
                ? (isUiTestMode() ? QStringLiteral("Recording Status (UI Test)")
                                  : QStringLiteral("Recording Status"))
                : (isUiTestMode() ? QStringLiteral("记录状态（界面测试）")
                                  : QStringLiteral("记录状态")));
    }
    if (!state_->recording_status_label_)
    {
        return;
    }

    auto applyRecordingStatusText = [this](const QString& plainText) {
        state_->recording_status_label_->setText(recordingStatusHtmlFromPlainText(plainText));
        state_->recording_status_label_->setToolTip(plainText);
        if (state_->recording_status_card_)
        {
            state_->recording_status_card_->setToolTip(plainText);
        }
    };

    if (isUiTestMode())
    {
        const qint64 elapsedMs = state_->ui_test_recording_state_ == 0
            ? 0
            : std::max<qint64>(uiTestRecordingElapsedMs(), 5000);
        const auto countAtRate = [elapsedMs](qint64 rateHz) {
            return static_cast<qulonglong>(std::max<qint64>(0, elapsedMs) * rateHz / 1000);
        };
        const qulonglong rawTotal = countAtRate(100) + countAtRate(10) + countAtRate(2) +
                                    countAtRate(20) + countAtRate(10);
        const char *visual = state_->ui_test_recording_state_ == 1
            ? "connected" : state_->ui_test_recording_state_ == 2 ? "connecting" : "disconnected";
        const QString stateText = state_->ui_test_recording_state_ == 1
            ? (state_->is_english_ ? QStringLiteral("Recording: On (UI Test)")
                                   : QStringLiteral("记录：进行中（界面测试）"))
            : state_->ui_test_recording_state_ == 2
                ? (state_->is_english_ ? QStringLiteral("Recording: Paused (UI Test)")
                                       : QStringLiteral("记录：已暂停（界面测试）"))
                : (state_->is_english_ ? QStringLiteral("Recording: Off (UI test)")
                                       : QStringLiteral("记录：未记录（界面测试）"));
        const QString session = state_->ui_test_recording_state_ == 0
            ? QStringLiteral("--")
            : QStringLiteral("UI-TEST-SESSION");
        const QString detail = state_->is_english_
            ? QStringLiteral("Session: %1\nElapsed: %2\nExternal device records: %3 rows\nWaveform frames: %4 frames\nRecorded RAW EPSILON: %5 records\nRecorded RAW PTB210: %6 records\nRecorded RAW HMP3: %7 records\nRecorded RAW TFA1500: %8 records\nRecorded RAW TCP: %9 records\nRecorded RAW total: %10 records\nFile output: none (memory only)")
                  .arg(session)
                  .arg(formatElapsedCompact(static_cast<quint64>(std::max<qint64>(0, elapsedMs))))
                  .arg(countAtRate(20))
                  .arg(countAtRate(10))
                  .arg(countAtRate(100))
                  .arg(countAtRate(10))
                  .arg(countAtRate(2))
                  .arg(countAtRate(20))
                  .arg(countAtRate(10))
                  .arg(rawTotal)
            : QStringLiteral("会话：%1\n时长：%2\n外部设备记录：%3 行\n波形帧数：%4 帧\n已记录：\nRAW EPSILON：%5 条\nRAW PTB210：%6 条\nRAW HMP3：%7 条\nRAW TFA1500：%8 条\nRAW TCP：%9 条\nRAW 记录总数：%10 条\n文件写入：无（仅内存模拟）")
                  .arg(session)
                  .arg(formatElapsedCompact(static_cast<quint64>(std::max<qint64>(0, elapsedMs))))
                  .arg(countAtRate(20))
                  .arg(countAtRate(10))
                  .arg(countAtRate(100))
                  .arg(countAtRate(10))
                  .arg(countAtRate(2))
                  .arg(countAtRate(20))
                  .arg(countAtRate(10))
                  .arg(rawTotal);
        const QString text = QStringLiteral("%1\n%2").arg(stateText, detail);
        setSectionTitleIconName(state_->recording_status_title_lbl_,
                                state_->ui_test_recording_state_ == 1
                                    ? QStringLiteral("pencil-sparkles")
                                    : QStringLiteral("pencil"),
                                state_->dark_theme_enabled_);
        const QString visualStatus = QString::fromLatin1(visual);
        const bool visualChanged = state_->recording_status_label_->property("status").toString() != visualStatus;
        applyRecordingStatusText(text);
        state_->recording_status_label_->setProperty("status", visualStatus);
        if (state_->recording_status_card_)
        {
            state_->recording_status_card_->setProperty("status", visualStatus);
        }
        if (visualChanged)
        {
            if (state_->recording_status_card_)
            {
                state_->recording_status_card_->style()->unpolish(state_->recording_status_card_);
                state_->recording_status_card_->style()->polish(state_->recording_status_card_);
            }
            state_->recording_status_label_->style()->unpolish(state_->recording_status_label_);
            state_->recording_status_label_->style()->polish(state_->recording_status_label_);
        }
        updateRecordingActionStates();
        return;
    }

    auto setVisualStatus = [this](const char *status) {
        const QString statusValue = QString::fromLatin1(status);
        state_->recording_status_label_->setProperty("status", statusValue);
        if (state_->recording_status_card_)
        {
            state_->recording_status_card_->setProperty("status", statusValue);
        }
    };
    auto polishVisualStatus = [this]() {
        state_->recording_status_label_->style()->unpolish(state_->recording_status_label_);
        state_->recording_status_label_->style()->polish(state_->recording_status_label_);
        if (state_->recording_status_card_)
        {
            state_->recording_status_card_->style()->unpolish(state_->recording_status_card_);
            state_->recording_status_card_->style()->polish(state_->recording_status_card_);
        }
    };
    auto localDetailText = [this](const QString& session,
                                  const QString& elapsed,
                                  qlonglong sensorRows,
                                  qlonglong waveformFrames,
                                  qulonglong rawEpsilon,
                                  qulonglong rawPtb,
                                  qulonglong rawHmp,
                                  qulonglong rawLidar,
                                  qulonglong rawWaveform,
                                  qulonglong rawLaserTemperature,
                                  qulonglong rawSystemTemperature,
                                  qulonglong rawTotal) {
        return state_->is_english_
            ? QStringLiteral("Session: %1\nElapsed: %2\nExternal device records: %3 rows\nWaveform frames: %4 frames\nRecorded RAW EPSILON: %5 records\nRecorded RAW PTB210: %6 records\nRecorded RAW HMP3: %7 records\nRecorded RAW TFA1500: %8 records\nRecorded RAW TCP: %9 records\nRecorded RAW RD105: %10 records\nRecorded RAW AI-8288: %11 records\nRecorded RAW total: %12 records")
                  .arg(session)
                  .arg(elapsed)
                  .arg(sensorRows)
                  .arg(waveformFrames)
                  .arg(rawEpsilon)
                  .arg(rawPtb)
                  .arg(rawHmp)
                  .arg(rawLidar)
                  .arg(rawWaveform)
                  .arg(rawLaserTemperature)
                  .arg(rawSystemTemperature)
                  .arg(rawTotal)
            : QStringLiteral("会话：%1\n时长：%2\n外部设备记录：%3 行\n波形帧数：%4 帧\n已记录：\nRAW EPSILON：%5 条\nRAW PTB210：%6 条\nRAW HMP3：%7 条\nRAW TFA1500：%8 条\nRAW TCP：%9 条\nRAW RD105：%10 条\nRAW AI-8288：%11 条\nRAW 记录总数：%12 条")
                  .arg(session)
                  .arg(elapsed)
                  .arg(sensorRows)
                  .arg(waveformFrames)
                  .arg(rawEpsilon)
                  .arg(rawPtb)
                  .arg(rawHmp)
                  .arg(rawLidar)
                  .arg(rawWaveform)
                  .arg(rawLaserTemperature)
                  .arg(rawSystemTemperature)
                  .arg(rawTotal);
    };
    auto appendScheduledLine = [this](const QString& text) {
        const QString scheduledLine = state_->recording_schedule_controller_
            ? state_->recording_schedule_controller_->statusLine(state_->is_english_)
            : QString();
        return scheduledLine.isEmpty() ? text : QStringLiteral("%1\n%2").arg(text, scheduledLine);
    };
    auto setRecordingTitleIcon = [this](bool recordingActive) {
        setSectionTitleIconName(state_->recording_status_title_lbl_,
                                recordingActive ? QStringLiteral("pencil-sparkles") : QStringLiteral("pencil"),
                                state_->dark_theme_enabled_);
    };

    if (isRemoteSkyMode())
    {
        const bool useLastRemoteStatus =
            state_->remote_status_.session_name.isEmpty() &&
            state_->remote_status_.telemetry_record_count == 0 &&
            state_->remote_status_.raw_waveform_record_count == 0 &&
            state_->has_last_remote_recording_status_;
        const VaporView::TelemetryStatus& displayStatus = useLastRemoteStatus
            ? state_->last_remote_recording_status_
            : state_->remote_status_;
        const quint64 rawTotal =
            displayStatus.raw_navigation_record_count +
            displayStatus.raw_pressure_record_count +
            displayStatus.raw_temperature_humidity_record_count +
            displayStatus.raw_distance_record_count +
            displayStatus.raw_waveform_record_count +
            displayStatus.raw_laser_temperature_controller_record_count +
            displayStatus.raw_system_temperature_controller_record_count;
        const QString elapsed = formatElapsedCompact(displayStatus.recording_elapsed_ms);
        const QString session = displayStatus.session_name.isEmpty()
            ? QStringLiteral("--")
            : displayStatus.session_name;
        const QString detail = state_->is_english_
            ? QStringLiteral("Session: %1\nElapsed: %2\nExternal device records: %3 rows\nWave features: %4 features\nWaveform frames: %5 frames\nRecorded RAW EPSILON: %6 records\nRecorded RAW PTB210: %7 records\nRecorded RAW HMP3: %8 records\nRecorded RAW TFA1500: %9 records\nRecorded RAW TCP: %10 records\nRecorded RAW RD105: %11 records\nRecorded RAW AI-8288: %12 records\nRecorded RAW total: %13 records")
                  .arg(session)
                  .arg(elapsed)
                  .arg(displayStatus.telemetry_record_count)
                  .arg(displayStatus.waveform_feature_record_count)
                  .arg(displayStatus.waveform_snapshot_record_count)
                  .arg(displayStatus.raw_navigation_record_count)
                  .arg(displayStatus.raw_pressure_record_count)
                  .arg(displayStatus.raw_temperature_humidity_record_count)
                  .arg(displayStatus.raw_distance_record_count)
                  .arg(displayStatus.raw_waveform_record_count)
                  .arg(displayStatus.raw_laser_temperature_controller_record_count)
                  .arg(displayStatus.raw_system_temperature_controller_record_count)
                  .arg(rawTotal)
            : QStringLiteral("会话：%1\n时长：%2\n外部设备记录：%3 行\n波形特征：%4 条\n波形帧数：%5 帧\n已记录：\nRAW EPSILON：%6 条\nRAW PTB210：%7 条\nRAW HMP3：%8 条\nRAW TFA1500：%9 条\nRAW TCP：%10 条\nRAW RD105：%11 条\nRAW AI-8288：%12 条\nRAW 记录总数：%13 条")
                  .arg(session)
                  .arg(elapsed)
                  .arg(displayStatus.telemetry_record_count)
                  .arg(displayStatus.waveform_feature_record_count)
                  .arg(displayStatus.waveform_snapshot_record_count)
                  .arg(displayStatus.raw_navigation_record_count)
                  .arg(displayStatus.raw_pressure_record_count)
                  .arg(displayStatus.raw_temperature_humidity_record_count)
                  .arg(displayStatus.raw_distance_record_count)
                  .arg(displayStatus.raw_waveform_record_count)
                  .arg(displayStatus.raw_laser_temperature_controller_record_count)
                  .arg(displayStatus.raw_system_temperature_controller_record_count)
                  .arg(rawTotal);
        const QString detailWithSchedule = appendScheduledLine(detail);
        if (state_->remote_recording_state_ == 1)
        {
            setRecordingTitleIcon(true);
            applyRecordingStatusText(
                QString(state_->is_english_ ? "Sky Recording: On\n%1" : "天空端记录：进行中\n%1")
                    .arg(detailWithSchedule));
            setVisualStatus("connected");
        }
        else if (state_->remote_recording_state_ == 2)
        {
            setRecordingTitleIcon(false);
            applyRecordingStatusText(
                QString(state_->is_english_ ? "Sky Recording: Paused\n%1" : "天空端记录：已暂停\n%1")
                    .arg(detailWithSchedule));
            setVisualStatus("connecting");
        }
        else
        {
            setRecordingTitleIcon(false);
            applyRecordingStatusText(
                QString(state_->is_english_ ? "Sky Recording: Off\n%1" : "天空端记录：未记录\n%1")
                    .arg(detailWithSchedule));
            setVisualStatus("disconnected");
        }
        polishVisualStatus();
        updateRecordingActionStates();
        return;
    }

    const auto recordingStatus = state_->recording_service_->status();
    const QString session = recordingStatus.sessionName.isEmpty()
        ? QStringLiteral("--")
        : recordingStatus.sessionName;
    const quint64 rawTotal =
        recordingStatus.rawNavigationRecords +
        recordingStatus.rawPressureRecords +
        recordingStatus.rawTemperatureHumidityRecords +
        recordingStatus.rawDistanceRecords +
        recordingStatus.rawWaveformRecords +
        recordingStatus.rawLaserTemperatureControllerRecords +
        recordingStatus.rawSystemTemperatureControllerRecords;
    const QString detail = localDetailText(
        session,
        formatElapsedCompact(recordingStatus.recordingElapsedMs),
        static_cast<qlonglong>(recordingStatus.sensorRows),
        static_cast<qlonglong>(recordingStatus.waveformFrames),
        static_cast<qulonglong>(recordingStatus.rawNavigationRecords),
        static_cast<qulonglong>(recordingStatus.rawPressureRecords),
        static_cast<qulonglong>(recordingStatus.rawTemperatureHumidityRecords),
        static_cast<qulonglong>(recordingStatus.rawDistanceRecords),
        static_cast<qulonglong>(recordingStatus.rawWaveformRecords),
        static_cast<qulonglong>(recordingStatus.rawLaserTemperatureControllerRecords),
        static_cast<qulonglong>(recordingStatus.rawSystemTemperatureControllerRecords),
        rawTotal);
    if (recordingStatus.sessionOpen)
    {
        if (recordingStatus.paused)
        {
            setRecordingTitleIcon(false);
            applyRecordingStatusText(
                QString(state_->is_english_ ? "Recording: Paused\n%1" : "记录：已暂停\n%1")
                    .arg(appendScheduledLine(detail)));
            setVisualStatus("connecting");
        }
        else
        {
            setRecordingTitleIcon(true);
            applyRecordingStatusText(
                QString(state_->is_english_ ? "Recording: On\n%1" : "记录：进行中\n%1")
                    .arg(appendScheduledLine(detail)));
            setVisualStatus("connected");
        }
    }
    else
    {
        applyRecordingStatusText(
            QString(state_->is_english_ ? "Recording: Off\n%1" : "记录：未记录\n%1")
                .arg(appendScheduledLine(detail)));
        setRecordingTitleIcon(false);
        setVisualStatus("disconnected");
    }

    polishVisualStatus();
    updateRecordingActionStates();
}

qint64 MainWindow::uiTestRecordingElapsedMs() const
{
    qint64 elapsedMs = state_->ui_test_recording_elapsed_ms_;
    if (state_->ui_test_recording_state_ == 1 && state_->ui_test_recording_started_ms_ > 0)
    {
        elapsedMs += std::max<qint64>(
            0, QDateTime::currentMSecsSinceEpoch() - state_->ui_test_recording_started_ms_);
    }
    return elapsedMs;
}

void MainWindow::startOrResumeUiTestRecording()
{
    if (state_->ui_test_recording_state_ == 0)
    {
        state_->ui_test_recording_elapsed_ms_ = 0;
    }
    state_->ui_test_recording_started_ms_ = QDateTime::currentMSecsSinceEpoch();
    state_->ui_test_recording_state_ = 1;
}

void MainWindow::pauseUiTestRecording()
{
    if (state_->ui_test_recording_state_ != 1)
    {
        return;
    }
    state_->ui_test_recording_elapsed_ms_ = uiTestRecordingElapsedMs();
    state_->ui_test_recording_started_ms_ = 0;
    state_->ui_test_recording_state_ = 2;
}

void MainWindow::resetUiTestRecording()
{
    state_->ui_test_recording_state_ = 0;
    state_->ui_test_recording_started_ms_ = 0;
    state_->ui_test_recording_elapsed_ms_ = 0;
}

QString MainWindow::defaultRecordingDirectory() const
{
    return VaporView::Ground::Session::GroundRecordingService::defaultRecordingDirectory();
}

QString MainWindow::configuredRecordingDirectory() const
{
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
    const QString configured = QDir::fromNativeSeparators(
        settings.value(QStringLiteral("recording_directory")).toString().trimmed());
    return configured.isEmpty() || !QFileInfo(configured).isDir()
        ? defaultRecordingDirectory()
        : QDir::cleanPath(configured);
}

bool MainWindow::startRecordingSession()
{
    if (isUiTestMode())
    {
        startOrResumeUiTestRecording();
        updateRecordingStatusLabel();
        publishUiTestEvent(QStringLiteral("ui_test_recording_started"),
                           state_->is_english_ ? QStringLiteral("Simulated recording started; no directory or file was created")
                                               : QStringLiteral("模拟记录已开始；未创建目录或文件"));
        return true;
    }
    const bool resuming = state_->recording_service_->isSessionOpen() && state_->recording_service_->isPaused();
    VaporView::Ground::Session::GroundRecordingOptions options;
    options.baseDirectory = state_->recording_directory_.trimmed();
    if (options.baseDirectory.isEmpty())
    {
        options.baseDirectory = defaultRecordingDirectory();
    }
    options.exportRateHz = state_->recording_export_rate_hz_;
    options.deviceConfig.waveformHost = state_->tcp_wave_panel_
        ? state_->tcp_wave_panel_->host()
        : QStringLiteral("127.0.0.1");
    options.deviceConfig.waveformPort = state_->tcp_wave_panel_ ? state_->tcp_wave_panel_->port() : 8888;
    auto serialConfig = [this](QComboBox *port, QComboBox *baud, QComboBox *rate) {
        VaporView::Ground::Session::GroundRecordingSerialConfig config;
        config.port = localSerialPortComboValue(port);
        config.baud = baud ? baud->currentText() : QString();
        config.rateHz = rate ? rate->currentText() : QString();
        return config;
    };
    options.deviceConfig.epsilon = serialConfig(state_->epsilon_port_combo_, state_->epsilon_baud_combo_, nullptr);
    options.deviceConfig.ptb = serialConfig(state_->ptb_port_combo_, state_->ptb_baud_combo_, state_->ptb_rate_combo_);
    options.deviceConfig.hmp = serialConfig(state_->hmp_port_combo_, state_->hmp_baud_combo_, state_->hmp_rate_combo_);
    options.deviceConfig.lidar = serialConfig(state_->lidar_port_combo_, state_->lidar_baud_combo_, state_->lidar_rate_combo_);
    options.deviceConfig.laserTemperatureController =
        serialConfig(state_->temperature_port_combo_, state_->temperature_baud_combo_, state_->temperature_rate_combo_);
    options.deviceConfig.laserTemperatureController.slaveAddress =
        QString::number(rememberedTemperatureSlaveAddress());
    options.deviceConfig.systemTemperatureController =
        serialConfig(state_->device_config_.ai8_temperature_port_combo,
                     state_->device_config_.ai8_temperature_baud_combo,
                     state_->device_config_.ai8_temperature_rate_combo);
    options.deviceConfig.systemTemperatureController.slaveAddress =
        QString::number(state_->ai8_temperature_controller_panel_
                            ? state_->ai8_temperature_controller_panel_->currentPageData().global.address
                            : 1);

    VaporView::Ground::Session::GroundRecordingStartError startError =
        VaporView::Ground::Session::GroundRecordingStartError::None;
    QString errorMessage;
    if (!state_->recording_service_->start(options, &startError, &errorMessage))
    {
        using StartError = VaporView::Ground::Session::GroundRecordingStartError;
        QString message;
        QString errorCode;
        switch (startError)
        {
        case StartError::CreateSessionLayout:
            message = state_->is_english_ ? QStringLiteral("Failed to create session directories")
                                  : QStringLiteral("无法创建会话目录结构");
            errorCode = QStringLiteral("SESSION_LAYOUT_CREATE_FAILED");
            break;
        case StartError::OpenSessionFiles:
            message = state_->is_english_ ? QStringLiteral("Failed to open session files for writing")
                                  : QStringLiteral("无法打开会话文件进行写入");
            errorCode = QStringLiteral("SESSION_FILES_OPEN_FAILED");
            break;
        case StartError::WriteSessionMetadata:
            message = state_->is_english_ ? QStringLiteral("Failed to save session metadata")
                                  : QStringLiteral("无法保存会话元数据");
            errorCode = QStringLiteral("SESSION_METADATA_WRITE_FAILED");
            break;
        case StartError::None:
            message = state_->is_english_ ? QStringLiteral("Failed to start recording session")
                                  : QStringLiteral("启动记录会话失败");
            errorCode = QStringLiteral("SESSION_RECORDING_START_FAILED");
            break;
        }
        QVariantMap fields{{QStringLiteral("error_code"), errorCode}};
        if (!errorMessage.isEmpty())
        {
            fields.insert(QStringLiteral("system_error"), errorMessage);
        }
        publishGroundLog(VaporView::LogLevel::Error,
                         QStringLiteral("session.recording"),
                         QStringLiteral("session_recording_start_failed"),
                         QStringLiteral("启动记录会话失败。"),
                         fields);
        QMessageBox::warning(this,
                             state_->is_english_ ? QStringLiteral("Error") : QStringLiteral("错误"),
                             message);
        return false;
    }

    const auto status = state_->recording_service_->status();
    updateRecordingStatusLabel();
    if (resuming)
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("session.recording"),
                         QStringLiteral("session_recording_resumed"),
                         QStringLiteral("已继续记录会话。"),
                         {{QStringLiteral("session_directory"), status.sessionDirectory},
                          {QStringLiteral("resumed"), true},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
    else
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("session.recording"),
                         QStringLiteral("session_recording_started"),
                         QStringLiteral("已开始记录会话。"),
                         {{QStringLiteral("session_directory"), status.sessionDirectory},
                          {QStringLiteral("resumed"), false},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
    return true;
}
void MainWindow::onChooseRecordingDirectoryClicked()
{
    if (!isUiTestMode())
    {
        state_->recording_directory_ = configuredRecordingDirectory();
    }
    const QString currentDirectory = state_->recording_directory_.isEmpty() ? defaultRecordingDirectory() : state_->recording_directory_;
    const QString selectedDirectory = QFileDialog::getExistingDirectory(
        this,
        state_->is_english_ ? "Select Recording Folder" : "选择记录目录",
        currentDirectory,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (selectedDirectory.isEmpty())
    {
        return;
    }

    state_->recording_directory_ = QDir::fromNativeSeparators(selectedDirectory);
    QSettings settings("VaporView", "MainWindow");
    VaporView::setPersistentSetting(settings, QStringLiteral("recording_directory"), state_->recording_directory_);
    publishGroundLog(VaporView::LogLevel::Info,
                     QStringLiteral("configuration.apply"),
                     QStringLiteral("recording_directory_updated"),
                     QStringLiteral("记录目录已更新。"),
                     {{QStringLiteral("recording_directory"), state_->recording_directory_},
                      {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
}

void MainWindow::updateScheduledRecordingAction()
{
    if (!state_->scheduled_recording_action_ || !state_->recording_schedule_controller_)
    {
        return;
    }

    const bool active = state_->recording_schedule_controller_->isActive();
    state_->scheduled_recording_action_->setText(state_->is_english_ ? "Scheduled Recording" : "定时记录");
    state_->scheduled_recording_action_->setToolTip(
        active ? state_->recording_schedule_controller_->summary(state_->is_english_)
               : (state_->is_english_ ? QStringLiteral("Configure scheduled recording")
                              : QStringLiteral("配置定时记录")));
    state_->scheduled_recording_action_->setCheckable(false);
    state_->scheduled_recording_action_->setChecked(false);
}

QString MainWindow::scheduledRecordingStartBlockReason() const
{
    if (isUiTestMode())
    {
        const std::array devices{VaporView::SkyDeviceId::Epsilon, VaporView::SkyDeviceId::Ptb,
                                 VaporView::SkyDeviceId::Hmp, VaporView::SkyDeviceId::Lidar,
                                 VaporView::SkyDeviceId::TemperatureController, VaporView::SkyDeviceId::WaveTcp};
        const bool connected = std::any_of(devices.cbegin(), devices.cend(),
            [this](VaporView::SkyDeviceId device) {
                return state_->ui_test_model_->deviceState(device) == VaporView::DeviceState::Connected;
            });
        return connected ? QString() : (state_->is_english_
            ? QStringLiteral("No simulated device is connected.")
            : QStringLiteral("没有已连接的模拟设备。"));
    }
    if (isRemoteSkyMode())
    {
        if (!state_->remote_sky_controller_)
        {
            return state_->is_english_
                ? QStringLiteral("Remote Sky telemetry service is not initialized.")
                : QStringLiteral("天空端数传服务未初始化。");
        }
        if (!state_->remote_sky_controller_->isOpen())
        {
            return state_->is_english_
                ? QStringLiteral("Remote Sky telemetry is not connected.")
                : QStringLiteral("天空端数传未连接。");
        }
        if (state_->remote_recording_state_ == 1)
        {
            const QString sessionName = state_->has_last_remote_recording_status_ &&
                                            !state_->last_remote_recording_status_.session_name.isEmpty()
                                        ? state_->last_remote_recording_status_.session_name
                                        : QString();
            if (!sessionName.isEmpty())
            {
                return state_->is_english_
                    ? QStringLiteral("Remote Sky is already recording session %1.").arg(sessionName)
                    : QStringLiteral("天空端已经在记录中，会话：%1。").arg(sessionName);
            }
            return state_->is_english_
                ? QStringLiteral("Remote Sky is already recording.")
                : QStringLiteral("天空端已经在记录中。");
        }
        return QString();
    }

    const bool tcpConnected = state_->tcp_wave_panel_ && state_->tcp_wave_panel_->isConnected();
    const bool recordingSourceAvailable = state_->is_connected_ || tcpConnected;
    if (!recordingSourceAvailable)
    {
        return state_->is_english_
            ? QStringLiteral("No local recording source is connected.")
            : QStringLiteral("本地记录源未连接。");
    }
    if (state_->connection_attempt_in_progress_)
    {
        return state_->is_english_
            ? QStringLiteral("A connection attempt is still in progress.")
            : QStringLiteral("连接流程正在进行。");
    }
    if (state_->port_detection_in_progress_)
    {
        return state_->is_english_
            ? QStringLiteral("Serial-port auto detection is still in progress.")
            : QStringLiteral("串口自动识别正在进行。");
    }
    if (state_->epsilon_reconfigure_in_progress_)
    {
        return state_->is_english_
            ? QStringLiteral("EPSILON reconfiguration is still in progress.")
            : QStringLiteral("EPSILON 配置流程正在进行。");
    }
    if (state_->recording_service_->isActive())
    {
        return state_->is_english_
            ? QStringLiteral("A local recording session is already running.")
            : QStringLiteral("本地记录已经在进行中。");
    }
    return QString();
}

bool MainWindow::scheduledRecordingSessionOpen() const
{
    if (isUiTestMode())
    {
        return state_->ui_test_recording_state_ != 0;
    }
    if (isRemoteSkyMode())
    {
        return state_->remote_recording_state_ == 1 || state_->remote_recording_state_ == 2;
    }
    return state_->recording_service_->isSessionOpen();
}

bool MainWindow::tryStartScheduledRecording(QString *failureReason)
{
    const QString blockReason = scheduledRecordingStartBlockReason();
    if (!blockReason.isEmpty())
    {
        if (failureReason)
        {
            *failureReason = blockReason;
        }
        return false;
    }

    if (isUiTestMode())
    {
        return startRecordingSession();
    }

    if (isRemoteSkyMode())
    {
        const quint16 seq = state_->remote_sky_controller_
            ? state_->remote_sky_controller_->sendCommand(VaporView::CommandId::StartRecording)
            : 0;
        if (seq != 0)
        {
            publishGroundLog(VaporView::LogLevel::Info,
                             QStringLiteral("session.recording"),
                             QStringLiteral("scheduled_recording_start_command_sent"),
                             QStringLiteral("定时记录开始命令已发送。"),
                             {{QStringLiteral("execution_path"), QStringLiteral("remote_sky")},
                              {QStringLiteral("command"), VaporView::commandIdName(VaporView::CommandId::StartRecording)},
                              {QStringLiteral("command_seq"), seq},
                              {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
        }
        else if (failureReason)
        {
            *failureReason = state_->is_english_
                ? QStringLiteral("Failed to send the remote start command.")
                : QStringLiteral("远程开始记录命令发送失败。");
        }
        return seq != 0;
    }

    const bool started = startRecordingSession() && state_->recording_service_->isActive();
    if (!started && failureReason)
    {
        *failureReason = state_->is_english_
            ? QStringLiteral("Local recording session failed to open.")
            : QStringLiteral("本地记录会话打开失败。");
    }
    return started;
}

bool MainWindow::tryStopScheduledRecording()
{
    if (isUiTestMode())
    {
        resetUiTestRecording();
        updateRecordingStatusLabel();
        publishUiTestEvent(QStringLiteral("ui_test_scheduled_recording_stopped"),
                           state_->is_english_ ? QStringLiteral("Simulated scheduled recording stopped")
                                               : QStringLiteral("模拟定时记录已停止"));
        return true;
    }
    if (isRemoteSkyMode())
    {
        if (!state_->remote_sky_controller_ || !state_->remote_sky_controller_->isOpen())
        {
            return false;
        }
        const quint16 seq = state_->remote_sky_controller_->sendCommand(VaporView::CommandId::StopRecording);
        if (seq != 0)
        {
            publishGroundLog(VaporView::LogLevel::Info,
                             QStringLiteral("session.recording"),
                             QStringLiteral("scheduled_recording_stop_command_sent"),
                             QStringLiteral("定时记录停止命令已发送。"),
                             {{QStringLiteral("execution_path"), QStringLiteral("remote_sky")},
                              {QStringLiteral("command"), VaporView::commandIdName(VaporView::CommandId::StopRecording)},
                              {QStringLiteral("command_seq"), seq},
                              {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
        }
        return seq != 0;
    }

    if (state_->recording_service_->isSessionOpen())
    {
        stopRecording(true);
    }
    return true;
}

void MainWindow::onScheduledRecordingTick()
{
    if (state_->recording_schedule_controller_)
    {
        state_->recording_schedule_controller_->tick();
    }
}

void MainWindow::onScheduledRecordingClicked()
{
    QDialog dialog(this);
    dialog.setWindowTitle(state_->is_english_ ? QStringLiteral("Scheduled Recording")
                                      : QStringLiteral("定时记录"));

    auto *dialogLayout = new QVBoxLayout(&dialog);
    dialogLayout->setContentsMargins(0, 0, 0, 0);
    dialogLayout->setSpacing(0);

    auto *bodyWidget = new QWidget(&dialog);
    auto *rootLayout = new QVBoxLayout(bodyWidget);
    rootLayout->setContentsMargins(16, 14, 16, 14);
    rootLayout->setSpacing(12);
    dialogLayout->addWidget(bodyWidget);

    if (state_->recording_schedule_controller_ && state_->recording_schedule_controller_->isActive())
    {
        auto *summaryLabel = new QLabel(state_->recording_schedule_controller_->summary(state_->is_english_), bodyWidget);
        summaryLabel->setWordWrap(true);
        summaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        rootLayout->addWidget(summaryLabel);
    }

    const int scheduledLabelWidth = scalePixels(state_->is_english_ ? 142 : 84);
    auto createScheduledRowLabel = [scheduledLabelWidth](const QString& text, QWidget *parent) {
        auto *label = new QLabel(text, parent);
        label->setFixedWidth(scheduledLabelWidth);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        return label;
    };
    auto createScheduledRow = [&createScheduledRowLabel](QWidget *parent,
                                                         const QString& labelText,
                                                         QWidget *field,
                                                         bool expandField = true) {
        auto *row = new QWidget(parent);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        layout->addWidget(createScheduledRowLabel(labelText, row), 0, Qt::AlignVCenter);
        if (expandField)
        {
            QSizePolicy policy = field->sizePolicy();
            policy.setHorizontalPolicy(QSizePolicy::Expanding);
            field->setSizePolicy(policy);
            layout->addWidget(field, 1);
        }
        else
        {
            layout->addWidget(field, 0, Qt::AlignLeft | Qt::AlignVCenter);
            layout->addStretch(1);
        }
        return row;
    };

    auto *modeCombo = new QComboBox(bodyWidget);
    modeCombo->addItem(state_->is_english_ ? QStringLiteral("Interval schedule") : QStringLiteral("周期执行"));
    modeCombo->addItem(state_->is_english_ ? QStringLiteral("Start at local time") : QStringLiteral("指定时间点"));
    modeCombo->setMaxVisibleItems(2);
    for (int i = 0; i < modeCombo->count(); ++i)
    {
        modeCombo->setItemData(i, QSize(0, scalePixels(42)), Qt::SizeHintRole);
    }
    auto applyModeComboPopupStyle = [this, modeCombo]() {
        configureComboPopup(modeCombo);
    };
    applyModeComboPopupStyle();
    rootLayout->addWidget(createScheduledRow(bodyWidget,
                                             state_->is_english_ ? QStringLiteral("Mode:") : QStringLiteral("模式:"),
                                             modeCombo));

    auto makeScheduledSpinStyle = []() {
        return QStringLiteral(
            "QSpinBox::up-button:hover, QSpinBox::down-button:hover, QDateTimeEdit::up-button:hover, QDateTimeEdit::down-button:hover, "
            "QSpinBox::up-button:pressed, QSpinBox::down-button:pressed, QDateTimeEdit::up-button:pressed, QDateTimeEdit::down-button:pressed, "
            "QSpinBox::up-button:disabled, QSpinBox::down-button:disabled, QDateTimeEdit::up-button:disabled, QDateTimeEdit::down-button:disabled { background-color: transparent; }");
    };
    const QString scheduledSpinStyle = makeScheduledSpinStyle();
    auto createDurationInput = [this, scheduledSpinStyle](QWidget *parent, QSpinBox *&hours, QSpinBox *&minutes, QSpinBox *&seconds) {
        auto *widget = new QWidget(parent);
        auto *layout = new QHBoxLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);
        hours = new QSpinBox(widget);
        minutes = new QSpinBox(widget);
        seconds = new QSpinBox(widget);
        for (QSpinBox *spin : {hours, minutes, seconds})
        {
            spin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
            spin->setAlignment(Qt::AlignRight);
            spin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            spin->setStyleSheet(scheduledSpinStyle);
        }
        hours->setRange(0, 999);
        minutes->setRange(0, 59);
        seconds->setRange(0, 59);
        hours->setSuffix(state_->is_english_ ? QStringLiteral(" h") : QStringLiteral(" 时"));
        minutes->setSuffix(state_->is_english_ ? QStringLiteral(" m") : QStringLiteral(" 分"));
        seconds->setSuffix(state_->is_english_ ? QStringLiteral(" s") : QStringLiteral(" 秒"));
        layout->addWidget(hours, 1);
        layout->addWidget(minutes, 1);
        layout->addWidget(seconds, 1);
        return widget;
    };

    QSpinBox *durationHours = nullptr;
    QSpinBox *durationMinutes = nullptr;
    QSpinBox *durationSeconds = nullptr;
    rootLayout->addWidget(createScheduledRow(bodyWidget,
                                             state_->is_english_ ? QStringLiteral("Record duration:")
                                                         : QStringLiteral("记录时长:"),
                                             createDurationInput(bodyWidget, durationHours, durationMinutes, durationSeconds)));

    auto setDurationValue = [](QSpinBox *hours, QSpinBox *minutes, QSpinBox *seconds, int totalSeconds) {
        totalSeconds = std::max(0, totalSeconds);
        if (hours) hours->setValue(totalSeconds / 3600);
        if (minutes) minutes->setValue((totalSeconds % 3600) / 60);
        if (seconds) seconds->setValue(totalSeconds % 60);
    };
    auto readDurationValue = [](QSpinBox *hours, QSpinBox *minutes, QSpinBox *seconds) {
        return (hours ? hours->value() : 0) * 3600 +
               (minutes ? minutes->value() : 0) * 60 +
               (seconds ? seconds->value() : 0);
    };
    setDurationValue(durationHours,
                     durationMinutes,
                     durationSeconds,
                     state_->recording_schedule_controller_->durationSeconds());

    auto *stack = new QStackedWidget(bodyWidget);

    auto *intervalPage = new QWidget(stack);
    auto *intervalLayout = new QVBoxLayout(intervalPage);
    intervalLayout->setContentsMargins(0, 0, 0, 0);
    intervalLayout->setSpacing(12);
    auto makeScheduledToggleStyle = [this]() {
        const QString textColor = appThemeColorName(state_->dark_theme_enabled_ ? AppThemeColor::TextStrong : AppThemeColor::Text,
                                                    state_->dark_theme_enabled_);
        return QStringLiteral(
            "QToolButton { background-color: transparent; border: 0px; border-radius: 0px; color: %1; padding: 2px 4px; margin: 0px; }"
            "QToolButton:hover, QToolButton:checked, QToolButton:pressed, QToolButton:focus { background-color: transparent; border: 0px; }"
            "QToolButton::menu-indicator { image: none; width: 0px; height: 0px; }")
                .arg(textColor);
    };
    const QString scheduledToggleStyle = makeScheduledToggleStyle();
    auto updateScheduledToggleIcon = [this](QToolButton *button) {
        if (!button)
        {
            return;
        }
        const QColor checkedColor = state_->dark_theme_enabled_
            ? appThemeColor(AppThemeColor::Primary, true)
            : appThemeColor(AppThemeColor::ToolbarBlue, false);
        button->setIcon(createLucideIcon(button->isChecked()
            ? QStringLiteral("square-check-big")
            : QStringLiteral("square"),
            button->isChecked() ? checkedColor : toolbarColor(AppThemeColor::ToolbarDisabled)));
        button->setIconSize(QSize(26, 26));
    };
    auto createScheduledToggle = [scheduledToggleStyle, updateScheduledToggleIcon](const QString& text, QWidget *parent) {
        auto *button = new QToolButton(parent);
        button->setText(text);
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(scheduledToggleStyle);
        updateScheduledToggleIcon(button);
        QObject::connect(button, &QToolButton::toggled, button, [button, updateScheduledToggleIcon]() {
            updateScheduledToggleIcon(button);
        });
        return button;
    };

    QSpinBox *intervalHours = nullptr;
    QSpinBox *intervalMinutes = nullptr;
    QSpinBox *intervalSeconds = nullptr;
    intervalLayout->addWidget(createScheduledRow(intervalPage,
                                                 state_->is_english_ ? QStringLiteral("Interval:")
                                                             : QStringLiteral("间隔时长:"),
                                                 createDurationInput(intervalPage, intervalHours, intervalMinutes, intervalSeconds)));
    setDurationValue(intervalHours,
                     intervalMinutes,
                     intervalSeconds,
                     state_->recording_schedule_controller_->intervalSeconds());

    auto *immediateCheck = createScheduledToggle(state_->is_english_ ? QStringLiteral("Start first round immediately")
                                                             : QStringLiteral("首次立即开始"),
                                                 intervalPage);
    immediateCheck->setChecked(true);
    intervalLayout->addWidget(createScheduledRow(intervalPage, QString(), immediateCheck, false));

    auto *countWidget = new QWidget(intervalPage);
    auto *countLayout = new QHBoxLayout(countWidget);
    countLayout->setContentsMargins(0, 0, 0, 0);
    countLayout->setSpacing(10);
    auto *loopCheck = createScheduledToggle(state_->is_english_ ? QStringLiteral("Loop until canceled")
                                                        : QStringLiteral("循环直到取消"),
                                            countWidget);
    auto *fixedCheck = createScheduledToggle(state_->is_english_ ? QStringLiteral("Fixed count")
                                                         : QStringLiteral("固定次数"),
                                             countWidget);
    auto *countGroup = new QButtonGroup(countWidget);
    countGroup->setExclusive(true);
    countGroup->addButton(loopCheck);
    countGroup->addButton(fixedCheck);
    auto *countSpin = new QSpinBox(countWidget);
    countSpin->setRange(1, 999);
    countSpin->setValue(std::clamp(state_->recording_schedule_controller_->totalRuns(), 1, 999));
    countSpin->setEnabled(state_->recording_schedule_controller_->fixedCountEnabled());
    countSpin->setStyleSheet(scheduledSpinStyle);
    loopCheck->setChecked(!state_->recording_schedule_controller_->fixedCountEnabled());
    fixedCheck->setChecked(state_->recording_schedule_controller_->fixedCountEnabled());
    QObject::connect(fixedCheck, &QToolButton::toggled, countSpin, &QWidget::setEnabled);
    countLayout->addWidget(loopCheck);
    countLayout->addWidget(fixedCheck);
    countLayout->addWidget(countSpin);
    countLayout->addStretch(1);
    intervalLayout->addWidget(createScheduledRow(intervalPage,
                                                 state_->is_english_ ? QStringLiteral("Repeat:")
                                                             : QStringLiteral("重复:"),
                                                 countWidget));
    intervalLayout->addStretch(1);
    stack->addWidget(intervalPage);

    auto *fixedTimePage = new QWidget(stack);
    auto *fixedTimeLayout = new QVBoxLayout(fixedTimePage);
    fixedTimeLayout->setContentsMargins(0, 0, 0, 0);
    fixedTimeLayout->setSpacing(12);
    auto *fixedTimeEdit = new QDateTimeEdit(fixedTimePage);
    fixedTimeEdit->setCalendarPopup(true);
    fixedTimeEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    fixedTimeEdit->setDateTime(state_->recording_schedule_controller_->mode() == RecordingScheduleController::Mode::FixedTime &&
                                   state_->recording_schedule_controller_->nextStartTime().isValid()
                               ? state_->recording_schedule_controller_->nextStartTime()
                               : QDateTime::currentDateTime().addSecs(5 * 60));
    fixedTimeEdit->setStyleSheet(scheduledSpinStyle);
    fixedTimeLayout->addWidget(createScheduledRow(fixedTimePage,
                                                  state_->is_english_ ? QStringLiteral("Start time:")
                                                              : QStringLiteral("开始时间:"),
                                                  fixedTimeEdit));
    fixedTimeLayout->addStretch(1);
    stack->addWidget(fixedTimePage);
    rootLayout->addWidget(stack);

    auto applyScheduledDialogLocalStyle = [applyModeComboPopupStyle,
                                           makeScheduledSpinStyle,
                                           makeScheduledToggleStyle,
                                           durationHours,
                                           durationMinutes,
                                           durationSeconds,
                                           intervalHours,
                                           intervalMinutes,
                                           intervalSeconds,
                                           countSpin,
                                           fixedTimeEdit,
                                           immediateCheck,
                                           loopCheck,
                                           fixedCheck,
                                           updateScheduledToggleIcon]() {
        applyModeComboPopupStyle();

        const QString spinStyle = makeScheduledSpinStyle();
        for (QSpinBox *spin : {durationHours, durationMinutes, durationSeconds,
                               intervalHours, intervalMinutes, intervalSeconds,
                               countSpin})
        {
            if (spin)
            {
                spin->setStyleSheet(spinStyle);
            }
        }
        if (fixedTimeEdit)
        {
            fixedTimeEdit->setStyleSheet(spinStyle);
        }

        const QString toggleStyle = makeScheduledToggleStyle();
        for (QToolButton *button : {immediateCheck, loopCheck, fixedCheck})
        {
            if (button)
            {
                button->setStyleSheet(toggleStyle);
                updateScheduledToggleIcon(button);
            }
        }
    };
    applyScheduledDialogLocalStyle();
    if (state_->theme_toggle_action_)
    {
        QObject::connect(state_->theme_toggle_action_, &QAction::changed, &dialog,
                         [applyScheduledDialogLocalStyle]() {
                             applyScheduledDialogLocalStyle();
                         });
    }

    modeCombo->setCurrentIndex(
        state_->recording_schedule_controller_->mode() == RecordingScheduleController::Mode::FixedTime ? 1 : 0);
    stack->setCurrentIndex(modeCombo->currentIndex());
    QObject::connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     stack, &QStackedWidget::setCurrentIndex);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, bodyWidget);
    if (!state_->is_english_)
    {
        if (auto *okButton = buttonBox->button(QDialogButtonBox::Ok))
        {
            okButton->setText(QStringLiteral("确定"));
        }
        if (auto *cancelButton = buttonBox->button(QDialogButtonBox::Cancel))
        {
            cancelButton->setText(QStringLiteral("取消"));
        }
    }
    QPushButton *cancelScheduleButton = nullptr;
    if (state_->recording_schedule_controller_->isActive())
    {
        cancelScheduleButton = buttonBox->addButton(state_->is_english_ ? QStringLiteral("Cancel Schedule")
                                                                : QStringLiteral("取消定时"),
                                                    QDialogButtonBox::DestructiveRole);
        QObject::connect(cancelScheduleButton, &QPushButton::clicked, &dialog, [this, &dialog]() {
            state_->recording_schedule_controller_->cancel(true);
            dialog.reject();
        });
    }
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog, [this,
                                                                       &dialog,
                                                                       modeCombo,
                                                                       durationHours,
                                                                       durationMinutes,
                                                                       durationSeconds,
                                                                       intervalHours,
                                                                       intervalMinutes,
                                                                       intervalSeconds,
                                                                       immediateCheck,
                                                                       fixedCheck,
                                                                       countSpin,
                                                                       fixedTimeEdit,
                                                                       readDurationValue]() {
        const int recordDuration = readDurationValue(durationHours, durationMinutes, durationSeconds);
        if (recordDuration <= 0)
        {
            QMessageBox::warning(&dialog,
                                 state_->is_english_ ? QStringLiteral("Scheduled Recording") : QStringLiteral("定时记录"),
                                 state_->is_english_ ? QStringLiteral("Record duration must be at least 1 second.")
                                             : QStringLiteral("记录时长至少需要 1 秒。"));
            return;
        }

        const QDateTime now = QDateTime::currentDateTime();
        if (modeCombo->currentIndex() == 0)
        {
            const int intervalDuration = readDurationValue(intervalHours, intervalMinutes, intervalSeconds);
            if (intervalDuration <= 0)
            {
                QMessageBox::warning(&dialog,
                                     state_->is_english_ ? QStringLiteral("Scheduled Recording") : QStringLiteral("定时记录"),
                                     state_->is_english_ ? QStringLiteral("Interval must be at least 1 second.")
                                                 : QStringLiteral("间隔时长至少需要 1 秒。"));
                return;
            }
            RecordingScheduleController::Configuration configuration;
            configuration.mode = RecordingScheduleController::Mode::Interval;
            configuration.durationSeconds = recordDuration;
            configuration.intervalSeconds = intervalDuration;
            configuration.fixedCountEnabled = fixedCheck && fixedCheck->isChecked();
            configuration.totalRuns = countSpin ? countSpin->value() : 1;
            configuration.firstStartTime = immediateCheck && immediateCheck->isChecked()
                ? now
                : now.addSecs(intervalDuration);
            state_->recording_schedule_controller_->configure(configuration, now);
            state_->recording_schedule_controller_->tick(now);
            dialog.accept();
            return;
        }

        const QDateTime startTime = fixedTimeEdit ? fixedTimeEdit->dateTime() : QDateTime();
        if (!startTime.isValid() || startTime < now)
        {
            QMessageBox::warning(&dialog,
                                 state_->is_english_ ? QStringLiteral("Scheduled Recording") : QStringLiteral("定时记录"),
                                 state_->is_english_ ? QStringLiteral("Start time must be in the future.")
                                             : QStringLiteral("开始时间必须晚于当前时间。"));
            return;
        }
        RecordingScheduleController::Configuration configuration;
        configuration.mode = RecordingScheduleController::Mode::FixedTime;
        configuration.durationSeconds = recordDuration;
        configuration.intervalSeconds = 1;
        configuration.fixedCountEnabled = true;
        configuration.totalRuns = 1;
        configuration.firstStartTime = startTime;
        state_->recording_schedule_controller_->configure(configuration, now);
        dialog.accept();
    });

    rootLayout->addWidget(buttonBox);
    VaporView::installCustomTitleBar(&dialog, false);
    dialog.resize(scalePixels(state_->is_english_ ? 470 : 400), dialog.sizeHint().height());
    dialog.exec();
    updateScheduledRecordingAction();
}

void MainWindow::onStartRecordingClicked()
{
    if (isUiTestMode())
    {
        startRecordingSession();
        return;
    }
    if (isRemoteSkyMode())
    {
        if (!state_->remote_sky_controller_ || !state_->remote_sky_controller_->isOpen())
        {
            publishGroundLog(VaporView::LogLevel::Warning,
                             QStringLiteral("session.recording"),
                             QStringLiteral("session_recording_rejected_dependency_unavailable"),
                             QStringLiteral("开始记录前请先连接天空端数传。"),
                             {{QStringLiteral("reason_code"), QStringLiteral("DEPENDENCY_UNAVAILABLE")},
                              {QStringLiteral("dependency"), QStringLiteral("remote_sky_telemetry")},
                              {QStringLiteral("mode"), QStringLiteral("remote_sky")},
                              {QStringLiteral("ui_dedupe_key"), QStringLiteral("recording:remote_sky:not_connected")}});
            return;
        }
        state_->remote_sky_controller_->sendCommand(VaporView::CommandId::StartRecording);
        return;
    }

    const bool tcpConnected = state_->tcp_wave_panel_ && state_->tcp_wave_panel_->isConnected();
    if (!state_->is_connected_ && !tcpConnected)
    {
        publishGroundLog(VaporView::LogLevel::Warning,
                         QStringLiteral("session.recording"),
                         QStringLiteral("session_recording_rejected_no_source"),
                         QStringLiteral("开始记录前，至少需要一个串口设备在线或 TCP 波形链路已连接。"),
                         {{QStringLiteral("reason_code"), QStringLiteral("NO_RECORDING_SOURCE_CONNECTED")},
                          {QStringLiteral("mode"), QStringLiteral("local")},
                          {QStringLiteral("serial_connected"), state_->is_connected_},
                          {QStringLiteral("tcp_wave_connected"), tcpConnected},
                          {QStringLiteral("ui_dedupe_key"), QStringLiteral("recording:local:no_source")}});
        return;
    }

    startRecordingSession();
}

void MainWindow::onPauseRecordingClicked()
{
    if (isUiTestMode())
    {
        if (state_->ui_test_recording_state_ == 1)
        {
            pauseUiTestRecording();
            updateRecordingStatusLabel();
            publishUiTestEvent(QStringLiteral("ui_test_recording_paused"),
                               state_->is_english_ ? QStringLiteral("Simulated recording paused")
                                                   : QStringLiteral("模拟记录已暂停"));
        }
        return;
    }
    if (isRemoteSkyMode())
    {
        if (state_->remote_sky_controller_) state_->remote_sky_controller_->sendCommand(VaporView::CommandId::PauseRecording);
        return;
    }
    pauseRecordingSession(true);
}

void MainWindow::onStopRecordingClicked()
{
    if (isUiTestMode())
    {
        resetUiTestRecording();
        updateRecordingStatusLabel();
        publishUiTestEvent(QStringLiteral("ui_test_recording_stopped"),
                           state_->is_english_ ? QStringLiteral("Simulated recording stopped")
                                               : QStringLiteral("模拟记录已停止"));
        return;
    }
    if (isRemoteSkyMode())
    {
        if (state_->remote_sky_controller_) state_->remote_sky_controller_->sendCommand(VaporView::CommandId::StopRecording);
        return;
    }
    stopRecording(true);
}

void MainWindow::pauseRecordingSession(bool announce)
{
    const QString sessionDirectory = state_->recording_service_->status().sessionDirectory;
    if (!state_->recording_service_->pause())
    {
        return;
    }
    updateRecordingStatusLabel();
    if (announce)
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("session.recording"),
                         QStringLiteral("session_recording_paused"),
                         QStringLiteral("已暂停记录会话。"),
                         {{QStringLiteral("session_directory"), sessionDirectory},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
}

void MainWindow::stopRecording(bool announce)
{
    const auto beforeStop = state_->recording_service_->status();
    if (beforeStop.sessionOpen)
    {
        if (!state_->recording_service_->appendEvent(QStringLiteral("info"), QStringLiteral("Recording stop requested.")))
        {
            VaporView::LogService::withCurrentInstance([](VaporView::LogService& logService) {
                logService.publish(VaporView::LogLevel::Error,
                                   QStringLiteral("Ground"),
                                   QStringLiteral("session.write"),
                                   QStringLiteral("无法写入记录停止摘要。"),
                                   {{QStringLiteral("event"), QStringLiteral("recording_stop_summary_append_failed")},
                                    {QStringLiteral("error_code"), QStringLiteral("RECORDING_STOP_SUMMARY_APPEND_FAILED")}});
            });
        }
    }
    const auto summary = state_->recording_service_->stop();
    updateRecordingStatusLabel();
    if (announce && summary.hadOpenSession)
    {
        publishGroundLog(VaporView::LogLevel::Info,
                         QStringLiteral("session.recording"),
                         QStringLiteral("session_recording_stopped"),
                         QStringLiteral("记录会话已结束。"),
                         {{QStringLiteral("sensor_rows"), static_cast<qulonglong>(summary.sensorRows)},
                          {QStringLiteral("waveform_frames"), static_cast<qulonglong>(summary.waveformFrames)},
                          {QStringLiteral("session_directory"), summary.sessionDirectory},
                          {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
}
void MainWindow::onTcpRawWaveFrameReady(quint64 timestampUs,
                                        QByteArray rawSignalPayload,
                                        QByteArray harmonicPayload,
                                        VaporView::TcpFloatEncoding floatEncoding)
{
    if (isUiTestMode())
    {
        return;
    }
    state_->recording_service_->recordTcpWaveFrame(timestampUs,
                                           rawSignalPayload,
                                           harmonicPayload,
                                           floatEncoding);
}
void MainWindow::updateRecordingActionStates()
{
    if (isUiTestMode())
    {
        const std::array devices{VaporView::SkyDeviceId::Epsilon, VaporView::SkyDeviceId::Ptb,
                                 VaporView::SkyDeviceId::Hmp, VaporView::SkyDeviceId::Lidar,
                                 VaporView::SkyDeviceId::TemperatureController, VaporView::SkyDeviceId::WaveTcp};
        const bool anyConnected = std::any_of(devices.cbegin(), devices.cend(),
            [this](VaporView::SkyDeviceId device) {
                return state_->ui_test_model_->deviceState(device) == VaporView::DeviceState::Connected;
            });
        if (state_->start_recording_btn_) state_->start_recording_btn_->setEnabled(anyConnected && state_->ui_test_recording_state_ != 1);
        if (state_->pause_recording_btn_) state_->pause_recording_btn_->setEnabled(state_->ui_test_recording_state_ == 1);
        if (state_->stop_recording_btn_) state_->stop_recording_btn_->setEnabled(state_->ui_test_recording_state_ != 0);
        updateScheduledRecordingAction();
        return;
    }
    if (isRemoteSkyMode())
    {
        const bool linkOpen = state_->remote_sky_controller_ && state_->remote_sky_controller_->isOpen();
        const bool recordingActive = state_->remote_recording_state_ == 1;
        const bool recordingPaused = state_->remote_recording_state_ == 2;
        if (state_->start_recording_btn_) state_->start_recording_btn_->setEnabled(linkOpen && !recordingActive);
        if (state_->pause_recording_btn_) state_->pause_recording_btn_->setEnabled(linkOpen && recordingActive);
        if (state_->stop_recording_btn_) state_->stop_recording_btn_->setEnabled(linkOpen && (recordingActive || recordingPaused));
        updateScheduledRecordingAction();
        return;
    }

    const bool tcpConnected = state_->tcp_wave_panel_ && state_->tcp_wave_panel_->isConnected();
    const bool recordingSourceAvailable = state_->is_connected_ || tcpConnected;
    const bool sessionOpen = state_->recording_service_->isSessionOpen();
    const bool recordingPaused = state_->recording_service_->isPaused();
    const bool recordingActive = state_->recording_service_->isActive();
    const bool uiBusy = state_->connection_attempt_in_progress_ || state_->port_detection_in_progress_ || state_->epsilon_reconfigure_in_progress_;
    const bool canStart = recordingSourceAvailable && !uiBusy && (!sessionOpen || recordingPaused);
    const bool canPause = !uiBusy && recordingActive;
    const bool canStop = sessionOpen && !uiBusy;

    if (state_->start_recording_btn_)
    {
        state_->start_recording_btn_->setEnabled(canStart);
    }
    if (state_->pause_recording_btn_)
    {
        state_->pause_recording_btn_->setEnabled(canPause);
    }
    if (state_->stop_recording_btn_)
    {
        state_->stop_recording_btn_->setEnabled(canStop);
    }
    updateScheduledRecordingAction();
}
