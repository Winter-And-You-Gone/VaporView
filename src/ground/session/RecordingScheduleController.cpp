#include "ground/session/RecordingScheduleController.h"

#include <algorithm>

namespace VaporView::Ground::Session
{

RecordingScheduleController::RecordingScheduleController(Hooks hooks)
    : hooks_(std::move(hooks))
{
}

void RecordingScheduleController::setHooks(Hooks hooks)
{
    hooks_ = std::move(hooks);
}

void RecordingScheduleController::configure(const Configuration& configuration,
                                             const QDateTime& fallbackStartTime)
{
    if (configuration.mode == Mode::None)
    {
        cancel(false);
        return;
    }

    mode_ = configuration.mode;
    phase_ = Phase::WaitingToStart;
    duration_seconds_ = std::max(1, configuration.durationSeconds);
    interval_seconds_ = std::max(1, configuration.intervalSeconds);
    fixed_count_enabled_ = configuration.fixedCountEnabled;
    total_runs_ = std::clamp(configuration.totalRuns, 1, 999);
    completed_runs_ = 0;
    next_start_time_ = configuration.firstStartTime.isValid()
        ? configuration.firstStartTime
        : fallbackStartTime;
    stop_time_ = {};
    round_observed_session_ = false;

    log({VaporView::LogLevel::Info,
         QStringLiteral("scheduled_recording_configured"),
         QStringLiteral("定时记录已配置。"),
         {{QStringLiteral("summary"), summary(false)},
          {QStringLiteral("mode"), mode_ == Mode::FixedTime ? QStringLiteral("fixed_time")
                                                            : QStringLiteral("interval")},
          {QStringLiteral("duration_seconds"), duration_seconds_},
          {QStringLiteral("interval_seconds"), interval_seconds_},
          {QStringLiteral("fixed_count_enabled"), fixed_count_enabled_},
          {QStringLiteral("total_runs"), total_runs_},
          {QStringLiteral("next_start_time"), formatDateTime(next_start_time_)},
          {QStringLiteral("ui_visibility"), QStringLiteral("details")}}});
    notifyStateChanged();
}

void RecordingScheduleController::cancel(bool announce)
{
    const bool wasActive = isActive();
    mode_ = Mode::None;
    phase_ = Phase::Idle;
    next_start_time_ = {};
    stop_time_ = {};
    completed_runs_ = 0;
    round_observed_session_ = false;

    if (announce && wasActive)
    {
        log({VaporView::LogLevel::Info,
             QStringLiteral("scheduled_recording_canceled"),
             QStringLiteral("定时记录已取消。"),
             {{QStringLiteral("ui_visibility"), QStringLiteral("details")}}});
    }
    notifyStateChanged();
}

void RecordingScheduleController::tick(const QDateTime& now)
{
    if (!isActive())
    {
        return;
    }

    if (phase_ == Phase::WaitingToStart)
    {
        if (!next_start_time_.isValid() || now < next_start_time_)
        {
            notifyStateChanged();
            return;
        }

        const StartResult result = hooks_.startRecording
            ? hooks_.startRecording()
            : StartResult{false, QStringLiteral("Recording start handler is unavailable.")};
        if (result.started)
        {
            phase_ = Phase::Recording;
            stop_time_ = now.addSecs(duration_seconds_);
            round_observed_session_ = sessionOpen();
            notifyStateChanged();
            return;
        }

        const QString englishReason = result.failureReason.isEmpty()
            ? QStringLiteral("Unknown reason.")
            : result.failureReason;
        log({VaporView::LogLevel::Warning,
             QStringLiteral("scheduled_recording_start_failed"),
             QStringLiteral("定时记录未能启动。"),
             {{QStringLiteral("reason_code"), QStringLiteral("DEPENDENCY_UNAVAILABLE")},
              {QStringLiteral("failure_reason"), englishReason},
              {QStringLiteral("mode"), mode_ == Mode::FixedTime ? QStringLiteral("fixed_time")
                                                                : QStringLiteral("interval")},
              {QStringLiteral("next_start_time"), formatDateTime(next_start_time_)},
              {QStringLiteral("ui_dedupe_key"), QStringLiteral("scheduled_recording:start_failed")}}});
        if (mode_ == Mode::FixedTime)
        {
            cancel(false);
            return;
        }

        scheduleNextInterval(now);
        notifyStateChanged();
        return;
    }

    if (phase_ != Phase::Recording)
    {
        return;
    }

    if (sessionOpen())
    {
        round_observed_session_ = true;
    }
    else if (round_observed_session_)
    {
        completeRound(true, now);
        return;
    }

    if (stop_time_.isValid() && now >= stop_time_)
    {
        const bool stopped = hooks_.stopRecording && hooks_.stopRecording();
        if (stopped)
        {
            completeRound(true, now);
            return;
        }
        log({VaporView::LogLevel::Warning,
             QStringLiteral("scheduled_recording_stop_failed"),
             QStringLiteral("定时记录未能停止，当前记录链路不可用。"),
             {{QStringLiteral("reason_code"), QStringLiteral("DEPENDENCY_UNAVAILABLE")},
              {QStringLiteral("ui_dedupe_key"), QStringLiteral("scheduled_recording:stop_failed")}}});
    }

    notifyStateChanged();
}

bool RecordingScheduleController::isActive() const
{
    return mode_ != Mode::None;
}

RecordingScheduleController::Mode RecordingScheduleController::mode() const
{
    return mode_;
}

RecordingScheduleController::Phase RecordingScheduleController::phase() const
{
    return phase_;
}

int RecordingScheduleController::durationSeconds() const
{
    return duration_seconds_;
}

int RecordingScheduleController::intervalSeconds() const
{
    return interval_seconds_;
}

bool RecordingScheduleController::fixedCountEnabled() const
{
    return fixed_count_enabled_;
}

int RecordingScheduleController::totalRuns() const
{
    return total_runs_;
}

int RecordingScheduleController::completedRuns() const
{
    return completed_runs_;
}

QDateTime RecordingScheduleController::nextStartTime() const
{
    return next_start_time_;
}

QDateTime RecordingScheduleController::stopTime() const
{
    return stop_time_;
}

QString RecordingScheduleController::summary(bool english) const
{
    if (!isActive())
    {
        return english ? QStringLiteral("Configure scheduled recording")
                       : QStringLiteral("配置定时记录");
    }

    const QString runText = fixed_count_enabled_
        ? (english
               ? QStringLiteral("%1 / %2 rounds").arg(completed_runs_).arg(total_runs_)
               : QStringLiteral("已完成 %1 / %2 次").arg(completed_runs_).arg(total_runs_))
        : (english ? QStringLiteral("loop until canceled") : QStringLiteral("循环直到取消"));
    const QString duration = formatDuration(duration_seconds_, english);

    if (phase_ == Phase::Recording)
    {
        return english
            ? QStringLiteral("Scheduled recording: recording until %1, duration %2, %3")
                  .arg(formatDateTime(stop_time_), duration, runText)
            : QStringLiteral("定时记录：记录中，计划 %1 停止，时长 %2，%3")
                  .arg(formatDateTime(stop_time_), duration, runText);
    }

    if (mode_ == Mode::FixedTime)
    {
        return english
            ? QStringLiteral("Scheduled recording: starts at %1, duration %2")
                  .arg(formatDateTime(next_start_time_), duration)
            : QStringLiteral("定时记录：%1 开始，记录 %2")
                  .arg(formatDateTime(next_start_time_), duration);
    }

    return english
        ? QStringLiteral("Scheduled recording: next start %1, duration %2, interval %3, %4")
              .arg(formatDateTime(next_start_time_),
                   duration,
                   formatDuration(interval_seconds_, true),
                   runText)
        : QStringLiteral("定时记录：下次 %1 开始，记录 %2，间隔 %3，%4")
              .arg(formatDateTime(next_start_time_),
                   duration,
                   formatDuration(interval_seconds_, false),
                   runText);
}

QString RecordingScheduleController::statusLine(bool english) const
{
    if (!isActive())
    {
        return {};
    }

    const QString countText = fixed_count_enabled_
        ? (english
               ? QStringLiteral(" (%1/%2)").arg(completed_runs_).arg(total_runs_)
               : QStringLiteral("（%1/%2）").arg(completed_runs_).arg(total_runs_))
        : QString();
    if (phase_ == Phase::Recording)
    {
        return english
            ? QStringLiteral("Schedule: recording, stops at %1%2")
                  .arg(formatDateTime(stop_time_), countText)
            : QStringLiteral("定时：记录中，%1 停止%2")
                  .arg(formatDateTime(stop_time_), countText);
    }
    return english
        ? QStringLiteral("Schedule: next start %1%2")
              .arg(formatDateTime(next_start_time_), countText)
        : QStringLiteral("定时：下次 %1 开始%2")
              .arg(formatDateTime(next_start_time_), countText);
}

QString RecordingScheduleController::formatDateTime(const QDateTime& dateTime)
{
    return dateTime.isValid()
        ? dateTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        : QStringLiteral("--");
}

QString RecordingScheduleController::formatDuration(int seconds, bool english)
{
    seconds = std::max(0, seconds);
    const int hours = seconds / 3600;
    const int minutes = (seconds % 3600) / 60;
    const int secs = seconds % 60;
    return english
        ? QStringLiteral("%1h %2m %3s").arg(hours).arg(minutes).arg(secs)
        : QStringLiteral("%1小时%2分%3秒").arg(hours).arg(minutes).arg(secs);
}

void RecordingScheduleController::scheduleNextInterval(const QDateTime& fromTime)
{
    phase_ = Phase::WaitingToStart;
    next_start_time_ = fromTime.addSecs(interval_seconds_);
    stop_time_ = {};
    round_observed_session_ = false;
}

void RecordingScheduleController::completeRound(bool counted, const QDateTime& now)
{
    if (counted)
    {
        ++completed_runs_;
    }

    const bool fixedDone = fixed_count_enabled_ && completed_runs_ >= total_runs_;
    if (mode_ == Mode::FixedTime || fixedDone)
    {
        log({VaporView::LogLevel::Info,
             QStringLiteral("scheduled_recording_completed"),
             QStringLiteral("定时记录已完成。"),
             {{QStringLiteral("summary"), summary(false)},
              {QStringLiteral("completed_runs"), completed_runs_},
              {QStringLiteral("total_runs"), total_runs_},
              {QStringLiteral("ui_visibility"), QStringLiteral("details")}}});
        cancel(false);
        return;
    }

    scheduleNextInterval(now);
    log({VaporView::LogLevel::Info,
         QStringLiteral("scheduled_recording_next_start_scheduled"),
         QStringLiteral("已安排下一次定时记录。"),
         {{QStringLiteral("next_start_time"), formatDateTime(next_start_time_)},
          {QStringLiteral("completed_runs"), completed_runs_},
          {QStringLiteral("ui_visibility"), QStringLiteral("details")}}});
    notifyStateChanged();
}

void RecordingScheduleController::notifyStateChanged() const
{
    if (hooks_.stateChanged)
    {
        hooks_.stateChanged();
    }
}

void RecordingScheduleController::log(LogEntry entry) const
{
    if (hooks_.log)
    {
        hooks_.log(entry);
    }
}

bool RecordingScheduleController::sessionOpen() const
{
    return hooks_.sessionOpen && hooks_.sessionOpen();
}

} // namespace VaporView::Ground::Session
