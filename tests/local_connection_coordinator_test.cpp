#include "ground/devices/LocalConnectionCoordinator.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <cstdlib>
#include <iostream>
#include <vector>

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

void drainEvents(int timeoutMs = 50)
{
    QEventLoop loop;
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace VaporView::Ground::Devices;

    std::vector<QString> events;
    LocalConnectionCoordinatorHooks hooks;
    bool serialStarted = false;
    bool serialCanceled = false;
    bool waveformStarted = false;
    bool waveformCanceled = false;
    bool waveformConnected = false;
    LocalConnectionResult result;
    bool finished = false;
    hooks.startSerial = [&](LocalConnectionRequest) {
        serialStarted = true;
        events.emplace_back(QStringLiteral("serial-start"));
        return true;
    };
    hooks.cancelSerial = [&]() { serialCanceled = true; events.emplace_back(QStringLiteral("serial-cancel")); };
    hooks.waveformAvailable = []() { return true; };
    hooks.waveformConnected = [&]() { return waveformConnected; };
    hooks.startWaveform = [&]() {
        waveformStarted = true;
        events.emplace_back(QStringLiteral("waveform-start"));
        return true;
    };
    hooks.cancelWaveform = [&]() { waveformCanceled = true; events.emplace_back(QStringLiteral("waveform-cancel")); };
    hooks.finished = [&](const LocalConnectionResult& value) {
        result = value;
        finished = true;
        events.emplace_back(QStringLiteral("finished"));
    };

    LocalConnectionCoordinator coordinator(std::move(hooks), 1000);
    LocalConnectionRequest request;
    require(coordinator.begin(request), "coordinator starts serial phase");
    require(serialStarted && coordinator.phase() == LocalConnectionPhase::SerialDevices,
            "serial phase is active");
    coordinator.serialFinished(true);
    require(waveformStarted && coordinator.phase() == LocalConnectionPhase::Waveform,
            "waveform phase starts after serial completion");
    waveformConnected = true;
    coordinator.waveformStateChanged(true);
    require(finished && result.outcome == LocalConnectionOutcome::Completed && result.connected(),
            "successful result combines both sources");
    require(events.at(0) == QStringLiteral("serial-start") &&
                events.at(1) == QStringLiteral("waveform-start") &&
                events.at(2) == QStringLiteral("finished"),
            "source order is serial then waveform then result");

    finished = false;
    waveformConnected = false;
    require(coordinator.begin(request), "coordinator starts second serial phase");
    coordinator.cancel();
    require(serialCanceled && !finished && coordinator.cancelRequested(),
            "serial cancellation waits for controller completion");
    coordinator.serialFinished(false);
    require(finished && result.outcome == LocalConnectionOutcome::Cancelled && !result.connected(),
            "cancelled serial result does not start waveform");

    finished = false;
    require(coordinator.begin(request), "coordinator starts timeout case");
    drainEvents(1100);
    require(serialCanceled && !finished && coordinator.cancelRequested(),
            "serial timeout requests cancellation before completion");
    coordinator.serialFinished(false);
    require(finished && result.outcome == LocalConnectionOutcome::TimedOut,
            "timeout result is preserved after serial completion");

    finished = false;
    require(coordinator.begin(request), "coordinator starts waveform timeout case");
    coordinator.serialFinished(false);
    require(coordinator.phase() == LocalConnectionPhase::Waveform, "waveform timeout phase active");
    drainEvents(1100);
    require(waveformCanceled && finished && result.outcome == LocalConnectionOutcome::TimedOut,
            "waveform timeout cancels waveform and completes");

    finished = false;
    waveformConnected = false;
    require(coordinator.begin(request), "coordinator starts waveform failure case");
    coordinator.serialFinished(true);
    coordinator.waveformStateChanged(false);
    require(finished && result.outcome == LocalConnectionOutcome::Failed &&
                result.serialConnected && !result.waveformConnected,
            "waveform failure has an explicit failed outcome");

    finished = false;
    require(coordinator.begin(request), "coordinator starts explicit disconnect case");
    coordinator.serialFinished(true);
    coordinator.disconnect();
    coordinator.waveformStateChanged(false);
    coordinator.serialFinished(false);
    drainEvents();
    require(!finished && coordinator.phase() == LocalConnectionPhase::Idle,
            "explicit disconnect suppresses late source callbacks");

    std::cout << "local_connection_coordinator_test passed\n";
    return 0;
}
