#pragma once

#include "ground/devices/LocalDeviceConnectionController.h"

#include <QTimer>

#include <functional>

namespace VaporView::Ground::Devices
{

enum class LocalConnectionPhase
{
    Idle,
    SerialDevices,
    Waveform
};

enum class LocalConnectionOutcome
{
    Completed,
    Failed,
    Cancelled,
    TimedOut,
    Rejected
};

struct LocalConnectionResult
{
    LocalConnectionOutcome outcome = LocalConnectionOutcome::Rejected;
    bool serialConnected = false;
    bool waveformConnected = false;

    bool connected() const
    {
        return serialConnected || waveformConnected;
    }
};

struct LocalConnectionCoordinatorHooks
{
    std::function<bool(LocalConnectionRequest)> startSerial;
    std::function<void()> cancelSerial;
    std::function<void()> stopSerial;
    std::function<bool()> waveformAvailable;
    std::function<bool()> waveformConnected;
    std::function<bool()> startWaveform;
    std::function<void()> cancelWaveform;
    std::function<void()> stopWaveform;
    std::function<void(const LocalConnectionResult&)> finished;
};

class LocalConnectionCoordinator final
{
public:
    static constexpr int DefaultTimeoutMs = 15'000;

    explicit LocalConnectionCoordinator(
        LocalConnectionCoordinatorHooks hooks = {},
        int timeoutMs = DefaultTimeoutMs);
    ~LocalConnectionCoordinator();

    LocalConnectionCoordinator(const LocalConnectionCoordinator&) = delete;
    LocalConnectionCoordinator& operator=(const LocalConnectionCoordinator&) = delete;

    void setHooks(LocalConnectionCoordinatorHooks hooks);
    bool begin(LocalConnectionRequest request);
    void serialFinished(bool connected);
    void waveformStateChanged(bool connected);
    void cancel();
    void disconnect();

    bool inProgress() const;
    bool cancelRequested() const;
    LocalConnectionPhase phase() const;

private:
    void startWaveformPhase();
    void onTimeout();
    void complete(LocalConnectionOutcome outcome);

    LocalConnectionCoordinatorHooks hooks_;
    QTimer timeout_timer_;
    int timeout_ms_;
    LocalConnectionPhase phase_ = LocalConnectionPhase::Idle;
    LocalConnectionOutcome pending_outcome_ = LocalConnectionOutcome::Cancelled;
    bool cancel_requested_ = false;
    bool serial_connected_ = false;
    bool waveform_connected_ = false;
};

}  // namespace VaporView::Ground::Devices
