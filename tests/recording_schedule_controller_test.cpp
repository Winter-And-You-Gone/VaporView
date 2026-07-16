#include "ground/session/RecordingScheduleController.h"

#include <QCoreApplication>
#include <QTimeZone>

#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using Controller = VaporView::Ground::Session::RecordingScheduleController;

    bool sessionOpen = false;
    bool startSucceeds = true;
    bool stopSucceeds = true;
    int startCalls = 0;
    int stopCalls = 0;
    int stateChanges = 0;
    QStringList logs;

    Controller controller({
        [&]() {
            ++startCalls;
            sessionOpen = startSucceeds;
            return Controller::StartResult{startSucceeds,
                                           startSucceeds ? QString() : QStringLiteral("blocked")};
        },
        [&]() {
            ++stopCalls;
            if (stopSucceeds)
            {
                sessionOpen = false;
            }
            return stopSucceeds;
        },
        [&]() { return sessionOpen; },
        [&](const QString& english, const QString&) { logs.push_back(english); },
        [&]() { ++stateChanges; },
    });

    const QDateTime firstStart(QDate(2026, 7, 16), QTime(12, 0), QTimeZone::UTC);
    Controller::Configuration interval;
    interval.mode = Controller::Mode::Interval;
    interval.durationSeconds = 10;
    interval.intervalSeconds = 30;
    interval.fixedCountEnabled = true;
    interval.totalRuns = 2;
    interval.firstStartTime = firstStart;
    controller.configure(interval, firstStart);
    require(controller.isActive() && controller.phase() == Controller::Phase::WaitingToStart,
            "interval schedule waits for its first start");
    require(controller.summary(true).contains(QStringLiteral("2 rounds")),
            "schedule summary exposes the configured run count");

    controller.tick(firstStart);
    require(startCalls == 1 && controller.phase() == Controller::Phase::Recording,
            "due interval schedule starts recording exactly once");
    require(controller.stopTime() == firstStart.addSecs(10),
            "recording stop time uses the configured duration");

    controller.tick(firstStart.addSecs(10));
    require(stopCalls == 1 && controller.completedRuns() == 1 && controller.isActive(),
            "first fixed-count interval round stops and remains scheduled");
    require(controller.nextStartTime() == firstStart.addSecs(40),
            "next interval starts after the configured post-round interval");

    controller.tick(firstStart.addSecs(40));
    controller.tick(firstStart.addSecs(50));
    require(startCalls == 2 && stopCalls == 2 && !controller.isActive(),
            "final fixed-count round completes and cancels the schedule");

    startSucceeds = false;
    sessionOpen = false;
    Controller::Configuration fixed;
    fixed.mode = Controller::Mode::FixedTime;
    fixed.durationSeconds = 5;
    fixed.fixedCountEnabled = true;
    fixed.totalRuns = 1;
    fixed.firstStartTime = firstStart.addSecs(100);
    controller.configure(fixed, fixed.firstStartTime);
    controller.tick(fixed.firstStartTime);
    require(!controller.isActive(),
            "failed fixed-time start does not remain armed indefinitely");
    require(!logs.isEmpty() && logs.back().contains(QStringLiteral("could not start")),
            "failed start emits a diagnostic log");

    Controller::Configuration retry = interval;
    retry.fixedCountEnabled = false;
    retry.firstStartTime = firstStart.addSecs(200);
    controller.configure(retry, retry.firstStartTime);
    controller.tick(retry.firstStartTime);
    require(controller.isActive() &&
                controller.phase() == Controller::Phase::WaitingToStart &&
                controller.nextStartTime() == retry.firstStartTime.addSecs(retry.intervalSeconds),
            "failed interval start is rescheduled at the next interval boundary");

    controller.cancel(false);
    require(!controller.isActive() && stateChanges > 0,
            "cancel resets the controller and reports state changes");
    require(Controller::formatDuration(3661, true) == QStringLiteral("1h 1m 1s") &&
                Controller::formatDuration(3661, false) == QStringLiteral("1小时1分1秒"),
            "duration formatting preserves both UI languages");

    std::cout << "PASS: recording schedule controller tests\n";
    return 0;
}
