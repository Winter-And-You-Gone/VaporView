#include "ground/devices/RtcmStatusModel.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace VaporView::Ground::Devices
{
namespace
{
qint64 statusTimeout(const TelemetryStatus& status)
{
    const double rate = std::isfinite(status.status_rate_hz) && status.status_rate_hz > 0
        ? status.status_rate_hz : 1.0;
    return std::max(RtcmHealthPolicy::statusTimeoutMs,
        static_cast<qint64>(std::ceil(3000.0 / std::max(0.001, rate))));
}
}

void RtcmStatusModel::reset()
{
    *this = {};
}

void RtcmStatusModel::clearWindow()
{
    samples_.clear();
    baseline_valid_ = false;
}

void RtcmStatusModel::reconnect()
{
    clearWindow();
    has_status_ = false;
    status_ = {};
}

void RtcmStatusModel::setContext(bool remote, bool running)
{
    if (remote_ != remote || running_ != running) clearWindow();
    remote_ = remote;
    running_ = running;
}

void RtcmStatusModel::receiveStatus(const TelemetryStatus& status, qint64 nowMs)
{
    const bool resetCounters = !has_status_ ||
        status.rtcm_boot_id != status_.rtcm_boot_id ||
        status.rtcm_link_stream_id != status_.rtcm_link_stream_id ||
        status.rtcm_forward_enabled != status_.rtcm_forward_enabled ||
        status.rtcm_observability_version != status_.rtcm_observability_version ||
        status.rtcm_report_time_us < status_.rtcm_report_time_us ||
        status.rtcm_correction_bytes_received < status_.rtcm_correction_bytes_received ||
        status.rtcm_correction_chunks_received < status_.rtcm_correction_chunks_received ||
        status.rtcm_correction_dropped_bytes < status_.rtcm_correction_dropped_bytes ||
        status.rtcm_correction_dropped_chunks < status_.rtcm_correction_dropped_chunks ||
        status.rtcm_link_frames_received < status_.rtcm_link_frames_received ||
        status.rtcm_link_frames_lost < status_.rtcm_link_frames_lost ||
        nowMs <= status_ms_ || nowMs - status_ms_ > statusTimeout(status_);
    if (resetCounters) clearWindow();
    if (baseline_valid_)
    {
        samples_.push_back({status_ms_, nowMs,
            status.rtcm_correction_bytes_received - status_.rtcm_correction_bytes_received,
            status.rtcm_link_frames_received - status_.rtcm_link_frames_received,
            status.rtcm_link_frames_lost - status_.rtcm_link_frames_lost,
            status.rtcm_correction_dropped_chunks - status_.rtcm_correction_dropped_chunks});
    }
    else
    {
        window_start_ms_ = nowMs;
    }
    while (!samples_.isEmpty() &&
           samples_.front().endMs <= nowMs - RtcmHealthPolicy::lossWindowMs)
        samples_.removeFirst();
    status_ = status;
    has_status_ = true;
    baseline_valid_ = true;
    status_ms_ = nowMs;
}

RtcmStatusSnapshot RtcmStatusModel::snapshot(bool linkOpen, qint64 nowMs) const
{
    RtcmStatusSnapshot result;
    result.remote = remote_;
    if (!remote_)
    {
        result.health = running_ ? RtcmHealth::Local : RtcmHealth::Disabled;
        return result;
    }
    result.available = has_status_ && status_.rtcm_observability_version == 1;
    result.receivedBytes = status_.rtcm_correction_bytes_received;
    result.receivedChunks = status_.rtcm_correction_chunks_received;
    result.droppedBytes = status_.rtcm_correction_dropped_bytes;
    result.droppedChunks = status_.rtcm_correction_dropped_chunks;
    result.linkFramesReceived = status_.rtcm_link_frames_received;
    result.linkFramesLost = status_.rtcm_link_frames_lost;
    if (result.available && status_.rtcm_correction_last_receive_time_us != 0 &&
        status_.rtcm_report_time_us >= status_.rtcm_correction_last_receive_time_us)
    {
        const quint64 age =
            (status_.rtcm_report_time_us - status_.rtcm_correction_last_receive_time_us) / 1000;
        // Age stays in Sky's clock domain at report time, then advances using
        // Ground's monotonic clock. No synchronized host clocks are required.
        result.ageMs = static_cast<qint64>(std::min<quint64>(age,
            std::numeric_limits<qint64>::max() / 2)) + std::max<qint64>(0, nowMs - status_ms_);
    }
    const bool statusFresh = has_status_ && nowMs >= status_ms_ &&
        nowMs - status_ms_ <= statusTimeout(status_);
    if (!running_) result.health = RtcmHealth::Disabled;
    else if (!linkOpen || (has_status_ && !statusFresh)) result.health = RtcmHealth::LinkDisconnected;
    else if (!has_status_) result.health = RtcmHealth::Waiting;
    else if (!result.available) result.health = RtcmHealth::StatusUnavailable;
    else if (!status_.rtcm_forward_enabled) result.health = RtcmHealth::Disabled;
    else if (result.ageMs < 0) result.health = RtcmHealth::Waiting;
    else if (result.ageMs >= RtcmHealthPolicy::interruptedMs) result.health = RtcmHealth::Interrupted;
    else if (result.ageMs >= RtcmHealthPolicy::delayedMs) result.health = RtcmHealth::Delayed;
    else if (result.ageMs >= RtcmHealthPolicy::warningMs) result.health = RtcmHealth::Warning;
    else result.health = RtcmHealth::Normal;

    if (!running_ || !linkOpen || !statusFresh || !result.available || !status_.rtcm_forward_enabled)
        return result;

    double bytes = 0.0, received = 0.0, lost = 0.0;
    for (const Sample& sample : samples_)
    {
        const auto fraction = [&sample, nowMs](qint64 windowMs) {
            const qint64 overlap = std::max<qint64>(0,
                sample.endMs - std::max(sample.startMs, nowMs - windowMs));
            return static_cast<double>(overlap) / (sample.endMs - sample.startMs);
        };
        bytes += sample.bytes * fraction(RtcmHealthPolicy::rateWindowMs);
        received += sample.received * fraction(RtcmHealthPolicy::lossWindowMs);
        lost += sample.lost * fraction(RtcmHealthPolicy::lossWindowMs);
        if (sample.endMs > nowMs - RtcmHealthPolicy::lossWindowMs)
            result.recentDroppedChunks += sample.dropped;
    }
    const qint64 elapsedMs = std::clamp<qint64>(nowMs - window_start_ms_,
        1000, RtcmHealthPolicy::rateWindowMs);
    result.bytesPerSecond = bytes * 1000.0 / elapsedMs;
    result.lossAvailable = status_.rtcm_link_stream_id != 0 && received + lost > 0;
    if (result.lossAvailable) result.lossPercent = lost * 100.0 / (received + lost);
    result.lossWarning = received + lost >= RtcmHealthPolicy::lossWarningMinFrames &&
        result.lossPercent >= RtcmHealthPolicy::lossWarningPercent;
    if (result.health == RtcmHealth::Normal &&
        (result.recentDroppedChunks != 0 || result.lossWarning))
        result.health = RtcmHealth::Warning;
    return result;
}
} // namespace VaporView::Ground::Devices
