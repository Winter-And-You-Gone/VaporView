#include "ground/devices/LocalConnectionCoordinator.h"

#include <algorithm>
#include <utility>

namespace VaporView::Ground::Devices
{

LocalConnectionCoordinator::LocalConnectionCoordinator(
    LocalConnectionCoordinatorHooks hooks,
    int timeoutMs)
    : hooks_(std::move(hooks))
    , timeout_ms_(std::max(1, timeoutMs))
{
    timeout_timer_.setSingleShot(true);
    QObject::connect(&timeout_timer_, &QTimer::timeout, [this]() { onTimeout(); });
}

LocalConnectionCoordinator::~LocalConnectionCoordinator()
{
    timeout_timer_.stop();
}

void LocalConnectionCoordinator::setHooks(LocalConnectionCoordinatorHooks hooks)
{
    hooks_ = std::move(hooks);
}

bool LocalConnectionCoordinator::begin(LocalConnectionRequest request)
{
    if (inProgress() || !hooks_.startSerial)
    {
        return false;
    }

    phase_ = LocalConnectionPhase::SerialDevices;
    cancel_requested_ = false;
    pending_outcome_ = LocalConnectionOutcome::Cancelled;
    waveform_requested_ = request.includeWaveform;
    serial_connected_ = false;
    waveform_connected_ = false;
    timeout_timer_.start(timeout_ms_);

    if (!hooks_.startSerial(std::move(request)))
    {
        timeout_timer_.stop();
        phase_ = LocalConnectionPhase::Idle;
        return false;
    }
    return true;
}

void LocalConnectionCoordinator::serialFinished(bool connected)
{
    if (phase_ != LocalConnectionPhase::SerialDevices)
    {
        return;
    }

    serial_connected_ = connected;
    if (cancel_requested_)
    {
        complete(pending_outcome_);
        return;
    }
    startWaveformPhase();
}

void LocalConnectionCoordinator::waveformStateChanged(bool connected)
{
    if (phase_ != LocalConnectionPhase::Waveform)
    {
        return;
    }

    waveform_connected_ = connected;
    complete(cancel_requested_ ? pending_outcome_ :
             (connected ? LocalConnectionOutcome::Completed : LocalConnectionOutcome::Failed));
}

void LocalConnectionCoordinator::cancel()
{
    if (!inProgress())
    {
        return;
    }

    cancel_requested_ = true;
    pending_outcome_ = LocalConnectionOutcome::Cancelled;
    if (phase_ == LocalConnectionPhase::SerialDevices)
    {
        if (hooks_.cancelSerial)
        {
            hooks_.cancelSerial();
        }
        return;
    }

    if (hooks_.cancelWaveform)
    {
        hooks_.cancelWaveform();
    }
    complete(LocalConnectionOutcome::Cancelled);
}

void LocalConnectionCoordinator::disconnect()
{
    const bool serialPhaseActive = phase_ == LocalConnectionPhase::SerialDevices;
    timeout_timer_.stop();
    phase_ = LocalConnectionPhase::Idle;
    cancel_requested_ = false;
    pending_outcome_ = LocalConnectionOutcome::Cancelled;
    serial_connected_ = false;
    waveform_connected_ = false;
    waveform_requested_ = true;

    if (serialPhaseActive && hooks_.cancelSerial)
    {
        hooks_.cancelSerial();
    }
    if (hooks_.stopSerial)
    {
        hooks_.stopSerial();
    }
    if (hooks_.stopWaveform)
    {
        hooks_.stopWaveform();
    }
}

bool LocalConnectionCoordinator::inProgress() const
{
    return phase_ != LocalConnectionPhase::Idle;
}

bool LocalConnectionCoordinator::cancelRequested() const
{
    return cancel_requested_;
}

LocalConnectionPhase LocalConnectionCoordinator::phase() const
{
    return phase_;
}

void LocalConnectionCoordinator::startWaveformPhase()
{
    if (!waveform_requested_)
    {
        complete(LocalConnectionOutcome::Completed);
        return;
    }
    if (hooks_.waveformConnected && hooks_.waveformConnected())
    {
        waveform_connected_ = true;
        complete(LocalConnectionOutcome::Completed);
        return;
    }
    if (!hooks_.waveformAvailable || !hooks_.waveformAvailable())
    {
        complete(LocalConnectionOutcome::Completed);
        return;
    }

    phase_ = LocalConnectionPhase::Waveform;
    timeout_timer_.start(timeout_ms_);
    if (!hooks_.startWaveform || !hooks_.startWaveform())
    {
        waveform_connected_ = hooks_.waveformConnected && hooks_.waveformConnected();
        complete(waveform_connected_ ? LocalConnectionOutcome::Completed
                                     : LocalConnectionOutcome::Failed);
        return;
    }
    if (hooks_.waveformConnected && hooks_.waveformConnected())
    {
        waveform_connected_ = true;
        complete(LocalConnectionOutcome::Completed);
    }
}

void LocalConnectionCoordinator::onTimeout()
{
    if (!inProgress())
    {
        return;
    }

    cancel_requested_ = true;
    pending_outcome_ = LocalConnectionOutcome::TimedOut;
    if (phase_ == LocalConnectionPhase::SerialDevices)
    {
        if (hooks_.cancelSerial)
        {
            hooks_.cancelSerial();
        }
        return;
    }
    if (hooks_.cancelWaveform)
    {
        hooks_.cancelWaveform();
    }
    complete(LocalConnectionOutcome::TimedOut);
}

void LocalConnectionCoordinator::complete(LocalConnectionOutcome outcome)
{
    if (!inProgress())
    {
        return;
    }

    timeout_timer_.stop();
    LocalConnectionResult result;
    result.outcome = outcome;
    result.serialConnected = serial_connected_;
    result.waveformConnected = waveform_connected_;
    phase_ = LocalConnectionPhase::Idle;
    cancel_requested_ = false;
    pending_outcome_ = LocalConnectionOutcome::Cancelled;
    serial_connected_ = false;
    waveform_connected_ = false;
    waveform_requested_ = true;
    if (hooks_.finished)
    {
        hooks_.finished(result);
    }
}

}  // namespace VaporView::Ground::Devices
