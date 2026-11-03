#pragma once
#include "CalibrationProject.h"
#include "FpgaWaveformAssembler.h"
#include <QObject>
#include <QMutex>
#include <QQueue>

namespace VaporView::Ground::Calibration
{
// Producers call directly on the acquisition thread. Consumers must use bounded
// admission (CalibrationService::offer); the bus never queues full waveforms.
class RealtimePeakBus final : public QObject
{
    Q_OBJECT
public:
    explicit RealtimePeakBus(QObject *parent = nullptr);
    void setDevice(bool connected, const QJsonObject& identity);
    void boundary();
    void publish(quint64 hostTimeUs, const FpgaWave::CompletedStream&, const QString& transport = {});
    QJsonObject diagnostics() const;
signals:
    void peakReady(const VaporView::Ground::Calibration::PeakRecord&);
    void deviceChanged(bool connected, const QJsonObject& identity);
private:
    mutable QMutex mutex_;
    quint64 inputCount_ = 0, publishedCount_ = 0, duplicateCount_ = 0, continuityCount_ = 0;
    bool connected_ = false;
    QJsonObject identity_;
    QString stream_;
    quint64 segment_ = 1, sequence_ = 0;
    QString remoteStream_;
    quint64 remoteSequence_ = 0;
    QQueue<QString> keys_;
    QSet<QString> seen_;
};
}
Q_DECLARE_METATYPE(VaporView::Ground::Calibration::PeakRecord)
