#pragma once

#include "TelemetryTypes.h"

#include <QHash>
#include <QVector>

namespace VaporView::Ground::Devices
{

class RemoteTelemetryState final
{
public:
    void reset();
    void markLinkClosed();

    void setDeviceState(VaporView::SkyDeviceId device, VaporView::DeviceState state);
    VaporView::DeviceState deviceState(VaporView::SkyDeviceId device) const;

    void noteDeviceData(VaporView::SkyDeviceId device, qint64 nowMs);
    void clearDeviceData(VaporView::SkyDeviceId device);
    qint64 lastDeviceDataMs(VaporView::SkyDeviceId device) const;
    bool deviceDataFresh(VaporView::SkyDeviceId device, qint64 nowMs, qint64 timeoutMs) const;

    void noteStatus(qint64 nowMs);
    qint64 lastStatusMs() const;
    bool statusFresh(qint64 nowMs, qint64 timeoutMs = 3000) const;

    void notePacket(VaporView::MsgType type, qint64 nowMs);
    void noteWaveformPacket(quint16 channelId, qint64 nowMs);
    double packetRate(VaporView::MsgType type) const;
    double waveformPacketRate(quint16 channelId) const;

private:
    static void noteArrival(QVector<qint64>& arrivals, qint64 nowMs);
    static double rateFromArrivals(const QVector<qint64>& arrivals);

    QHash<VaporView::SkyDeviceId, VaporView::DeviceState> device_states_;
    QHash<VaporView::SkyDeviceId, qint64> last_data_ms_;
    QHash<int, QVector<qint64>> packet_arrivals_ms_;
    QHash<int, QVector<qint64>> waveform_arrivals_ms_;
    qint64 last_status_ms_ = 0;
};

} // namespace VaporView::Ground::Devices
