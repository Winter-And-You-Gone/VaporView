#pragma once

#include "TelemetryTypes.h"
#include <QVector>

namespace VaporView
{
// VaporView UI heuristics, not RTCM or EPSILON specification limits.
struct RtcmHealthPolicy
{
    static constexpr qint64 warningMs = 1000;
    static constexpr qint64 delayedMs = 3000;
    static constexpr qint64 interruptedMs = 5000;
    static constexpr qint64 statusTimeoutMs = 3000;
    static constexpr qint64 rateWindowMs = 5000;
    static constexpr qint64 lossWindowMs = 10000;
    static constexpr double lossWarningPercent = 5.0;
    static constexpr quint64 lossWarningMinFrames = 20;
};

enum class RtcmHealth
{
    Local, Disabled, Waiting, Normal, Warning, Delayed, Interrupted,
    LinkDisconnected, StatusUnavailable,
};

struct RtcmStatusSnapshot
{
    RtcmHealth health = RtcmHealth::Disabled;
    bool remote = false;
    bool available = false;
    qint64 ageMs = -1;
    double bytesPerSecond = 0.0;
    bool lossAvailable = false;
    double lossPercent = 0.0;
    bool lossWarning = false;
    quint64 receivedBytes = 0;
    quint64 receivedChunks = 0;
    quint64 droppedBytes = 0;
    quint64 droppedChunks = 0;
    quint64 linkFramesReceived = 0;
    quint64 linkFramesLost = 0;
    quint64 recentDroppedChunks = 0;
};

class RtcmStatusModel
{
public:
    void reset();
    void reconnect();
    void setContext(bool remote, bool running);
    void receiveStatus(const TelemetryStatus& status, qint64 nowMs);
    RtcmStatusSnapshot snapshot(bool linkOpen, qint64 nowMs) const;

private:
    struct Sample
    {
        qint64 startMs = 0;
        qint64 endMs = 0;
        quint64 bytes = 0;
        quint64 received = 0;
        quint64 lost = 0;
        quint64 dropped = 0;
    };
    void clearWindow();

    TelemetryStatus status_;
    bool has_status_ = false;
    bool baseline_valid_ = false;
    bool remote_ = false;
    bool running_ = false;
    qint64 status_ms_ = 0;
    qint64 window_start_ms_ = 0;
    QVector<Sample> samples_;
};
} // namespace VaporView
