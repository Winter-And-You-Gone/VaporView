#pragma once

#include "TelemetryTypes.h"

namespace VaporView
{
// The telemetry transports are ordered byte streams. Only valid RTCM payloads
// enter this tracker; the shared frame-header sequence is deliberately ignored.
class RtcmLinkTracker
{
public:
    void reset() { *this = {}; }

    void receive(const RtcmFrameSequence& frame)
    {
        if (frame.stream_id == 0)
        {
            if (stream_id_ != 0) reset();
            ++received_;
            return;
        }
        if (frame.stream_id != stream_id_ ||
            frame.sequence < last_sequence_)
        {
            reset();
            stream_id_ = frame.stream_id;
        }
        // A new stream/reconnect starts at the first observed frame. Loss before
        // this baseline, and a lost tail without a subsequent frame, is unknown.
        if (stream_id_ != 0 && last_sequence_ != 0 && frame.sequence > last_sequence_)
            lost_ += frame.sequence - last_sequence_ - 1;
        if (frame.sequence != last_sequence_)
            ++received_;
        last_sequence_ = frame.sequence;
    }

    quint64 streamId() const { return stream_id_; }
    quint64 received() const { return received_; }
    quint64 lost() const { return lost_; }

private:
    quint64 stream_id_ = 0;
    quint64 last_sequence_ = 0;
    quint64 received_ = 0;
    quint64 lost_ = 0;
};
} // namespace VaporView
