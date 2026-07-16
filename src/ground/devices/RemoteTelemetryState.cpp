#include "ground/devices/RemoteTelemetryState.h"

namespace VaporView::Ground::Devices
{
namespace
{
constexpr qint64 kPacketRateWindowMs = 5000;
}

void RemoteTelemetryState::reset()
{
    device_states_.clear();
    last_data_ms_.clear();
    packet_arrivals_ms_.clear();
    waveform_arrivals_ms_.clear();
    last_status_ms_ = 0;
}

void RemoteTelemetryState::markLinkClosed()
{
    last_status_ms_ = 0;
    packet_arrivals_ms_.clear();
    waveform_arrivals_ms_.clear();
}

void RemoteTelemetryState::setDeviceState(VaporView::SkyDeviceId device, VaporView::DeviceState state)
{
    device_states_.insert(device, state);
}

VaporView::DeviceState RemoteTelemetryState::deviceState(VaporView::SkyDeviceId device) const
{
    return device_states_.value(device, VaporView::DeviceState::Disconnected);
}

void RemoteTelemetryState::noteDeviceData(VaporView::SkyDeviceId device, qint64 nowMs)
{
    last_data_ms_.insert(device, nowMs);
}

void RemoteTelemetryState::clearDeviceData(VaporView::SkyDeviceId device)
{
    last_data_ms_.remove(device);
}

qint64 RemoteTelemetryState::lastDeviceDataMs(VaporView::SkyDeviceId device) const
{
    return last_data_ms_.value(device, 0);
}

bool RemoteTelemetryState::deviceDataFresh(VaporView::SkyDeviceId device,
                                           qint64 nowMs,
                                           qint64 timeoutMs) const
{
    const qint64 timestamp = lastDeviceDataMs(device);
    return timestamp > 0 && nowMs >= timestamp && nowMs - timestamp <= timeoutMs;
}

void RemoteTelemetryState::noteStatus(qint64 nowMs)
{
    last_status_ms_ = nowMs;
}

qint64 RemoteTelemetryState::lastStatusMs() const
{
    return last_status_ms_;
}

bool RemoteTelemetryState::statusFresh(qint64 nowMs, qint64 timeoutMs) const
{
    return last_status_ms_ > 0 && nowMs >= last_status_ms_ && nowMs - last_status_ms_ <= timeoutMs;
}

void RemoteTelemetryState::noteArrival(QVector<qint64>& arrivals, qint64 nowMs)
{
    arrivals.push_back(nowMs);
    while (!arrivals.isEmpty() && nowMs - arrivals.front() > kPacketRateWindowMs)
    {
        arrivals.removeFirst();
    }
}

double RemoteTelemetryState::rateFromArrivals(const QVector<qint64>& arrivals)
{
    if (arrivals.size() < 2)
    {
        return 0.0;
    }
    const qint64 elapsedMs = arrivals.back() - arrivals.front();
    return elapsedMs > 0
        ? (arrivals.size() - 1) * 1000.0 / static_cast<double>(elapsedMs)
        : 0.0;
}

void RemoteTelemetryState::notePacket(VaporView::MsgType type, qint64 nowMs)
{
    noteArrival(packet_arrivals_ms_[static_cast<int>(type)], nowMs);
}

void RemoteTelemetryState::noteWaveformPacket(quint16 channelId, qint64 nowMs)
{
    noteArrival(waveform_arrivals_ms_[static_cast<int>(channelId)], nowMs);
}

double RemoteTelemetryState::packetRate(VaporView::MsgType type) const
{
    return rateFromArrivals(packet_arrivals_ms_.value(static_cast<int>(type)));
}

double RemoteTelemetryState::waveformPacketRate(quint16 channelId) const
{
    return rateFromArrivals(waveform_arrivals_ms_.value(static_cast<int>(channelId)));
}

} // namespace VaporView::Ground::Devices
