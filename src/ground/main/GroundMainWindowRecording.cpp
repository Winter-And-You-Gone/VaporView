#include "ground/main/GroundMainWindowImplementation.h"

void MainWindow::log(const QString& message)
{
    auto scrollLogToBottom = [this]() {
        if (state_->log_text_edit_)
        {
            state_->log_text_edit_->ensureCursorVisible();
            if (QScrollBar *scrollBar = state_->log_text_edit_->verticalScrollBar())
            {
                scrollBar->setValue(scrollBar->maximum());
            }
        }
    };

    if (message.startsWith('\r'))
    {
        if (!state_->log_text_edit_)
        {
            return;
        }
        const QString inlineMessage = message.mid(1);
        QTextCursor cursor = state_->log_text_edit_->textCursor();
        cursor.movePosition(QTextCursor::End);

        if (!state_->has_inline_progress_log_)
        {
            if (!state_->log_text_edit_->document()->isEmpty())
            {
                cursor.insertBlock();
            }
            cursor.insertText(inlineMessage);
            state_->has_inline_progress_log_ = true;
        }
        else
        {
            cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            cursor.insertText(inlineMessage);
        }

        state_->log_text_edit_->setTextCursor(cursor);
        scrollLogToBottom();
        return;
    }

    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    const QString displayLine = QString("[%1] %2").arg(timestamp, message);
    state_->log_entries_.append(displayLine);
    if (state_->log_entries_.size() > kMaxLogEntryCount)
    {
        state_->log_entries_.remove(0, state_->log_entries_.size() - kMaxLogEntryCount);
    }
    if (state_->log_text_edit_ && shouldShowLogLine(displayLine))
    {
        state_->log_text_edit_->append(displayLine);
        scrollLogToBottom();
    }
    state_->has_inline_progress_log_ = false;

    state_->recording_service_->appendEvent(QStringLiteral("info"), message);
    if (shouldMirrorToErrorLog(message))
    {
        state_->recording_service_->appendError(message);
    }
}

bool MainWindow::shouldShowLogLine(const QString& line) const
{
    if (state_->log_filter_ack_enabled_ && line.contains(QStringLiteral("ACK"), Qt::CaseInsensitive))
    {
        return false;
    }
    if (state_->log_filter_config_enabled_ &&
        (line.contains(QStringLiteral("配置"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("config"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("频率"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("波特率"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("output rate"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("sample rate"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("rate"), Qt::CaseInsensitive)))
    {
        return false;
    }
    if (state_->log_filter_connection_enabled_ &&
        (line.contains(QStringLiteral("连接"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("断开"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("串口"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("端口"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("connect"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("disconnect"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("port"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("telemetry link"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("handshake"), Qt::CaseInsensitive)))
    {
        return false;
    }
    if (state_->log_filter_recording_enabled_ &&
        (line.contains(QStringLiteral("记录"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("定时"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("recording"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("record"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("session"), Qt::CaseInsensitive) ||
         line.contains(QStringLiteral("schedule"), Qt::CaseInsensitive)))
    {
        return false;
    }
    return true;
}

void MainWindow::renderLogView()
{
    if (!state_->log_text_edit_)
    {
        return;
    }

    state_->log_text_edit_->clear();
    for (const QString& line : state_->log_entries_)
    {
        if (shouldShowLogLine(line))
        {
            state_->log_text_edit_->append(line);
        }
    }
    state_->has_inline_progress_log_ = false;
    if (QScrollBar *scrollBar = state_->log_text_edit_->verticalScrollBar())
    {
        scrollBar->setValue(scrollBar->maximum());
    }
}

void MainWindow::updateLogFilterAction()
{
    if (!state_->log_filter_ack_action_)
    {
        return;
    }

    const QIcon checkIcon = createMenuCheckIcon(state_->dark_theme_enabled_);
    const QStringList filterTexts = state_->is_english_
        ? QStringList{
              QStringLiteral("Filter ACK logs"),
              QStringLiteral("Filter config and rate logs"),
              QStringLiteral("Filter connection and port logs"),
              QStringLiteral("Filter recording and schedule logs")}
        : QStringList{
              QStringLiteral("过滤 ACK 日志"),
              QStringLiteral("过滤配置和频率日志"),
              QStringLiteral("过滤连接和端口日志"),
              QStringLiteral("过滤记录和定时日志")};
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
                                                             bool enabled,
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
            action->setIcon(enabled ? checkIcon : QIcon());
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
        row->setChecked(enabled);
        row->refreshTheme();
    };

    updateAction(state_->log_filter_ack_action_,
                 state_->log_filter_ack_enabled_,
                 QStringLiteral("Filter ACK logs"),
                 QStringLiteral("过滤 ACK 日志"),
                 QStringLiteral("Hide remote ACK command result logs from the display"),
                 QStringLiteral("仅从显示中隐藏远程 ACK 命令结果日志"));
    updateAction(state_->log_filter_config_action_,
                 state_->log_filter_config_enabled_,
                 QStringLiteral("Filter config and rate logs"),
                 QStringLiteral("过滤配置和频率日志"),
                 QStringLiteral("Hide configuration, baud-rate, and output-rate logs from the display"),
                 QStringLiteral("仅从显示中隐藏配置、波特率和输出频率日志"));
    updateAction(state_->log_filter_connection_action_,
                 state_->log_filter_connection_enabled_,
                 QStringLiteral("Filter connection and port logs"),
                 QStringLiteral("过滤连接和端口日志"),
                 QStringLiteral("Hide connection, disconnection, port, and handshake logs from the display"),
                 QStringLiteral("仅从显示中隐藏连接、断开、端口和握手日志"));
    updateAction(state_->log_filter_recording_action_,
                 state_->log_filter_recording_enabled_,
                 QStringLiteral("Filter recording and schedule logs"),
                 QStringLiteral("过滤记录和定时日志"),
                 QStringLiteral("Hide recording session and scheduled-recording logs from the display"),
                 QStringLiteral("仅从显示中隐藏记录会话和定时记录日志"));

    if (state_->log_filter_menu_)
    {
        state_->log_filter_menu_->setTitle(state_->is_english_ ? QStringLiteral("Log Filters")
                                               : QStringLiteral("日志过滤"));
        state_->log_filter_menu_->refreshTheme();
        state_->log_filter_menu_->setPanelContentWidth(menuItemWidth);
    }
    if (state_->log_filter_btn_)
    {
        state_->log_filter_btn_->setIcon(createLogFilterIcon());
        state_->log_filter_btn_->setToolTip(state_->is_english_ ? QStringLiteral("Log filters")
                                                : QStringLiteral("日志过滤"));
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

    if (isUiTestMode())
    {
        const qint64 elapsedMs = uiTestRecordingElapsedMs();
        const auto countAtRate = [elapsedMs](qint64 rateHz) {
            return static_cast<qulonglong>(std::max<qint64>(0, elapsedMs) * rateHz / 1000);
        };
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
            ? QStringLiteral("Session: %1\nElapsed: %2\nSensor rows: %3\nWaveform frames: %4\nRaw EPSILON: %5\nRaw PTB: %6\nRaw HMP: %7\nRaw Lidar: %8\nRaw TCP wave: %9\nFile output: none (memory only)")
                  .arg(session)
                  .arg(formatElapsedCompact(static_cast<quint64>(std::max<qint64>(0, elapsedMs))))
                  .arg(countAtRate(20))
                  .arg(countAtRate(10))
                  .arg(countAtRate(100))
                  .arg(countAtRate(10))
                  .arg(countAtRate(2))
                  .arg(countAtRate(20))
                  .arg(countAtRate(10))
            : QStringLiteral("会话：%1\n时长：%2\n设备行数：%3\n波形帧数：%4\nRaw EPSILON：%5\nRaw PTB：%6\nRaw HMP：%7\nRaw Lidar：%8\nRaw TCP 波形：%9\n文件写入：无（仅内存模拟）")
                  .arg(session)
                  .arg(formatElapsedCompact(static_cast<quint64>(std::max<qint64>(0, elapsedMs))))
                  .arg(countAtRate(20))
                  .arg(countAtRate(10))
                  .arg(countAtRate(100))
                  .arg(countAtRate(10))
                  .arg(countAtRate(2))
                  .arg(countAtRate(20))
                  .arg(countAtRate(10));
        const QString text = QStringLiteral("%1\n%2").arg(stateText, detail);
        setSectionTitleIconName(state_->recording_status_title_lbl_,
                                state_->ui_test_recording_state_ == 1
                                    ? QStringLiteral("pencil-sparkles")
                                    : QStringLiteral("pencil"),
                                state_->dark_theme_enabled_);
        const QString visualStatus = QString::fromLatin1(visual);
        const bool visualChanged = state_->recording_status_label_->property("status").toString() != visualStatus;
        state_->recording_status_label_->setText(text);
        state_->recording_status_label_->setToolTip(text);
        state_->recording_status_label_->setProperty("status", visualStatus);
        if (state_->recording_status_card_)
        {
            state_->recording_status_card_->setProperty("status", visualStatus);
            state_->recording_status_card_->setToolTip(text);
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
                                  qlonglong sensorRows,
                                  qlonglong waveformFrames,
                                  qulonglong rawEpsilon,
                                  qulonglong rawPtb,
                                  qulonglong rawHmp,
                                  qulonglong rawLidar,
                                  qulonglong rawWaveform) {
        return state_->is_english_
            ? QStringLiteral("Session: %1\nSensor rows: %2\nWaveform frames: %3\nRaw EPSILON: %4\nRaw PTB: %5\nRaw HMP: %6\nRaw Lidar: %7\nRaw TCP wave: %8")
                  .arg(session)
                  .arg(sensorRows)
                  .arg(waveformFrames)
                  .arg(rawEpsilon)
                  .arg(rawPtb)
                  .arg(rawHmp)
                  .arg(rawLidar)
                  .arg(rawWaveform)
            : QStringLiteral("会话：%1\n设备行数：%2\n波形帧数：%3\nRaw EPSILON：%4\nRaw PTB：%5\nRaw HMP：%6\nRaw Lidar：%7\nRaw TCP 波形：%8")
                  .arg(session)
                  .arg(sensorRows)
                  .arg(waveformFrames)
                  .arg(rawEpsilon)
                  .arg(rawPtb)
                  .arg(rawHmp)
                  .arg(rawLidar)
                  .arg(rawWaveform);
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
            displayStatus.raw_waveform_record_count;
        const QString elapsed = formatElapsedCompact(displayStatus.recording_elapsed_ms);
        const QString session = displayStatus.session_name.isEmpty()
            ? QStringLiteral("--")
            : displayStatus.session_name;
        const QString detail = state_->is_english_
            ? QStringLiteral("Session: %1\nElapsed: %2\nTelemetry rows: %3\nWave features: %4\nWave snapshots: %5\nRaw EPSILON: %6\nRaw PTB: %7\nRaw HMP: %8\nRaw Lidar: %9\nRaw TCP wave: %10")
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
            : QStringLiteral("会话：%1\n时长：%2\n遥测行数：%3\n波形特征：%4\n波形快照：%5\nRaw EPSILON：%6\nRaw PTB：%7\nRaw HMP：%8\nRaw Lidar：%9\nRaw TCP 波形：%10")
                  .arg(session)
                  .arg(elapsed)
                  .arg(displayStatus.telemetry_record_count)
                  .arg(displayStatus.waveform_feature_record_count)
                  .arg(displayStatus.waveform_snapshot_record_count)
                  .arg(displayStatus.raw_navigation_record_count)
                  .arg(displayStatus.raw_pressure_record_count)
                  .arg(displayStatus.raw_temperature_humidity_record_count)
                  .arg(displayStatus.raw_distance_record_count)
                  .arg(displayStatus.raw_waveform_record_count);
        const QString detailWithSchedule = appendScheduledLine(detail);
        state_->recording_status_label_->setToolTip(detailWithSchedule);
        if (state_->recording_status_card_)
        {
            state_->recording_status_card_->setToolTip(detailWithSchedule);
        }
        if (state_->remote_recording_state_ == 1)
        {
            setRecordingTitleIcon(true);
            state_->recording_status_label_->setText(
                QString(state_->is_english_ ? "Sky Recording: On\n%1\nRaw total: %2"
                                    : "天空端记录：进行中\n%1\nRaw 总数：%2")
                    .arg(detailWithSchedule)
                    .arg(rawTotal));
            setVisualStatus("connected");
        }
        else if (state_->remote_recording_state_ == 2)
        {
            setRecordingTitleIcon(false);
            state_->recording_status_label_->setText(
                QString(state_->is_english_ ? "Sky Recording: Paused\n%1\nRaw total: %2"
                                    : "天空端记录：已暂停\n%1\nRaw 总数：%2")
                    .arg(detailWithSchedule)
                    .arg(rawTotal));
            setVisualStatus("connecting");
        }
        else
        {
            setRecordingTitleIcon(false);
            state_->recording_status_label_->setText(
                QString(state_->is_english_ ? "Sky Recording: Off\n%1\nRaw total: %2"
                                    : "天空端记录：未记录\n%1\nRaw 总数：%2")
                    .arg(detailWithSchedule)
                    .arg(rawTotal));
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
    const QString detail = localDetailText(
        session,
        static_cast<qlonglong>(recordingStatus.sensorRows),
        static_cast<qlonglong>(recordingStatus.waveformFrames),
        static_cast<qulonglong>(recordingStatus.rawNavigationRecords),
        static_cast<qulonglong>(recordingStatus.rawPressureRecords),
        static_cast<qulonglong>(recordingStatus.rawTemperatureHumidityRecords),
        static_cast<qulonglong>(recordingStatus.rawDistanceRecords),
        static_cast<qulonglong>(recordingStatus.rawWaveformRecords));
    if (recordingStatus.sessionOpen)
    {
        if (recordingStatus.paused)
        {
            setRecordingTitleIcon(false);
            state_->recording_status_label_->setText(
                QString(state_->is_english_ ? "Recording: Paused\n%1" : "记录：已暂停\n%1")
                    .arg(appendScheduledLine(detail)));
            setVisualStatus("connecting");
        }
        else
        {
            setRecordingTitleIcon(true);
            state_->recording_status_label_->setText(
                QString(state_->is_english_ ? "Recording: On\n%1" : "记录：进行中\n%1")
                    .arg(appendScheduledLine(detail)));
            setVisualStatus("connected");
        }
    }
    else
    {
        state_->recording_status_label_->setText(
            QString(state_->is_english_ ? "Recording: Off\n%1" : "记录：未记录\n%1")
                .arg(appendScheduledLine(detail)));
        setRecordingTitleIcon(false);
        setVisualStatus("disconnected");
    }

    const QString summary = state_->recording_status_label_->text();
    state_->recording_status_label_->setToolTip(summary);
    if (state_->recording_status_card_)
    {
        state_->recording_status_card_->setToolTip(summary);
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
        logUiTest(state_->is_english_ ? QStringLiteral("Simulated recording started; no directory or file was created")
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
    options.deviceConfig.temperatureController =
        serialConfig(state_->temperature_port_combo_, state_->temperature_baud_combo_, state_->temperature_rate_combo_);

    VaporView::Ground::Session::GroundRecordingStartError startError =
        VaporView::Ground::Session::GroundRecordingStartError::None;
    QString errorMessage;
    if (!state_->recording_service_->start(options, &startError, &errorMessage))
    {
        using StartError = VaporView::Ground::Session::GroundRecordingStartError;
        QString message;
        switch (startError)
        {
        case StartError::CreateSessionLayout:
            message = state_->is_english_ ? QStringLiteral("Failed to create session directories")
                                  : QStringLiteral("无法创建会话目录结构");
            break;
        case StartError::OpenSessionFiles:
            message = state_->is_english_ ? QStringLiteral("Failed to open session files for writing")
                                  : QStringLiteral("无法打开会话文件进行写入");
            break;
        case StartError::WriteSessionMetadata:
            message = state_->is_english_ ? QStringLiteral("Failed to save session metadata")
                                  : QStringLiteral("无法保存会话元数据");
            break;
        case StartError::None:
            message = state_->is_english_ ? QStringLiteral("Failed to start recording session")
                                  : QStringLiteral("启动记录会话失败");
            break;
        }
        if (!errorMessage.isEmpty())
        {
            log(QStringLiteral("Recording service: %1").arg(errorMessage));
        }
        QMessageBox::warning(this,
                             state_->is_english_ ? QStringLiteral("Error") : QStringLiteral("错误"),
                             message);
        return false;
    }

    const auto status = state_->recording_service_->status();
    updateRecordingStatusLabel();
    log(QString(resuming
        ? (state_->is_english_ ? "Resumed recording session: %1" : "已继续记录会话: %1")
        : (state_->is_english_ ? "Started recording session: %1" : "已开始记录会话: %1"))
            .arg(status.sessionDirectory));
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
    log(QString(state_->is_english_ ? "Recording folder set to: %1" : "记录目录已设置为: %1").arg(state_->recording_directory_));
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
            log(state_->is_english_ ? "Scheduled recording start command sent"
                            : "定时记录开始命令已发送");
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
        logUiTest(state_->is_english_ ? QStringLiteral("Simulated scheduled recording stopped")
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
            log(state_->is_english_ ? "Scheduled recording stop command sent"
                            : "定时记录停止命令已发送");
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
            log(state_->is_english_ ? "Connect Remote Sky telemetry before recording" : "开始记录前请先连接天空端数传");
            return;
        }
        state_->remote_sky_controller_->sendCommand(VaporView::CommandId::StartRecording);
        return;
    }

    const bool tcpConnected = state_->tcp_wave_panel_ && state_->tcp_wave_panel_->isConnected();
    if (!state_->is_connected_ && !tcpConnected)
    {
        log(state_->is_english_ ? "At least one serial device or the TCP wave link must be connected before recording"
                        : "开始记录前，至少需要一个串口设备在线或 TCP 波形链路已连接");
        return;
    }

    if (!startRecordingSession())
    {
        log(state_->is_english_ ? "Failed to start recording session" : "启动记录会话失败");
    }
}

void MainWindow::onPauseRecordingClicked()
{
    if (isUiTestMode())
    {
        if (state_->ui_test_recording_state_ == 1)
        {
            pauseUiTestRecording();
            updateRecordingStatusLabel();
            logUiTest(state_->is_english_ ? QStringLiteral("Simulated recording paused")
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
        logUiTest(state_->is_english_ ? QStringLiteral("Simulated recording stopped")
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
        log(QString(state_->is_english_ ? "Paused recording session: %1" : "已暂停记录会话: %1")
                .arg(sessionDirectory));
    }
}

void MainWindow::stopRecording(bool announce)
{
    const auto summary = state_->recording_service_->stop();
    updateRecordingStatusLabel();
    if (announce && summary.hadOpenSession)
    {
        log(QString(state_->is_english_
            ? "Stopped recording (%1 sensor rows, %2 waveform frames): %3"
            : "记录已结束（设备 %1 行，波形 %2 帧）: %3")
                .arg(summary.sensorRows)
                .arg(summary.waveformFrames)
                .arg(summary.sessionDirectory));
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
