#pragma once

#include <QDateTime>
#include <QString>

#include <functional>

namespace VaporView::Ground::Session
{

class RecordingScheduleController final
{
public:
    enum class Mode
    {
        None,
        Interval,
        FixedTime
    };

    enum class Phase
    {
        Idle,
        WaitingToStart,
        Recording
    };

    struct Configuration
    {
        Mode mode = Mode::None;
        int durationSeconds = 10 * 60;
        int intervalSeconds = 60 * 60;
        bool fixedCountEnabled = false;
        int totalRuns = 1;
        QDateTime firstStartTime;
    };

    struct StartResult
    {
        bool started = false;
        QString failureReason;
    };

    struct Hooks
    {
        std::function<StartResult()> startRecording;
        std::function<bool()> stopRecording;
        std::function<bool()> sessionOpen;
        std::function<void(const QString& english, const QString& chinese)> log;
        std::function<void()> stateChanged;
    };

    explicit RecordingScheduleController(Hooks hooks = {});

    void setHooks(Hooks hooks);
    void configure(const Configuration& configuration,
                   const QDateTime& fallbackStartTime = QDateTime::currentDateTime());
    void cancel(bool announce = true);
    void tick(const QDateTime& now = QDateTime::currentDateTime());

    bool isActive() const;
    Mode mode() const;
    Phase phase() const;
    int durationSeconds() const;
    int intervalSeconds() const;
    bool fixedCountEnabled() const;
    int totalRuns() const;
    int completedRuns() const;
    QDateTime nextStartTime() const;
    QDateTime stopTime() const;

    QString summary(bool english) const;
    QString statusLine(bool english) const;

    static QString formatDateTime(const QDateTime& dateTime);
    static QString formatDuration(int seconds, bool english);

private:
    void scheduleNextInterval(const QDateTime& fromTime);
    void completeRound(bool counted, const QDateTime& now);
    void notifyStateChanged() const;
    void log(const QString& english, const QString& chinese) const;
    bool sessionOpen() const;

    Hooks hooks_;
    Mode mode_ = Mode::None;
    Phase phase_ = Phase::Idle;
    int duration_seconds_ = 10 * 60;
    int interval_seconds_ = 60 * 60;
    bool fixed_count_enabled_ = false;
    int total_runs_ = 1;
    int completed_runs_ = 0;
    QDateTime next_start_time_;
    QDateTime stop_time_;
    bool round_observed_session_ = false;
};

} // namespace VaporView::Ground::Session
