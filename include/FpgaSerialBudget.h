#pragma once
#include <QByteArray>
#include <QHash>
#include <QtGlobal>

namespace VaporView {
// A one-second token bucket reserves at most 25% of 8N1 theoretical payload
// for diagnostic sensor/preview frames. It does not govern control or Basic.
class FpgaSerialBudget {
public:
    void reset(int baud, qint64 nowMs);
    qint64 capacity() const { return capacity_; }
    qint64 maximumFrameBytes() const { return capacity_ / 2; }
    static qint64 maximumPendingBytes(int baud);
    static bool queueFits(int baud, qint64 pending, qint64 incoming);
    bool accept(qint64 bytes, qint64 nowMs, int sensorSource = -1);
    QByteArray previewPayload(const QByteArray& payload) const;
private:
    qint64 capacity_ = 0, lastMs_ = 0;
    double tokens_ = 0;
    QHash<int,qint64> sensorSentMs_;
};
}
