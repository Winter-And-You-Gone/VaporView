#include "RealtimePeakBus.h"
#include "FpgaHarmonics.h"
#include <QCryptographicHash>
#include <QMutexLocker>

namespace VaporView::Ground::Calibration
{
RealtimePeakBus::RealtimePeakBus(QObject *parent):QObject(parent),stream_(calibrationUuid())
{ qRegisterMetaType<PeakRecord>(); }
void RealtimePeakBus::setDevice(bool connected,const QJsonObject& identity)
{
    {
        QMutexLocker lock(&mutex_);
        if(connected_==connected && identity_==identity) return;
        connected_=connected;identity_=identity;
        stream_=calibrationUuid();++segment_;sequence_=0;keys_.clear();seen_.clear();
        remoteStream_.clear();remoteSequence_=0;
    }
    emit deviceChanged(connected,identity);
}
void RealtimePeakBus::boundary() { QMutexLocker lock(&mutex_);++segment_; }
QJsonObject RealtimePeakBus::diagnostics() const
{
    QMutexLocker lock(&mutex_);
    return {{"input",QString::number(inputCount_)},{"published",QString::number(publishedCount_)},
        {"duplicate",QString::number(duplicateCount_)},{"continuity_errors",QString::number(continuityCount_)},
        {"deduplication_current",seen_.size()},{"deduplication_capacity",4096}};
}
void RealtimePeakBus::publish(quint64 host,const FpgaWave::CompletedStream& s,const QString& transport)
{
    if(s.preview || (s.source!=0x30 && s.source!=0x31)) return;
    PeakRecord r;
    {
        QMutexLocker lock(&mutex_);++inputCount_;if(!connected_) return;
        if(!transport.isEmpty() && identity_.value("transport")!=transport)return;
        const auto key=s.featureStreamId+QStringLiteral("%1:%2:%3:%4").arg(segment_).arg(s.source).arg(s.timestamp).arg(s.cycleId);
        if(seen_.contains(key)){++duplicateCount_;return;}
        seen_.insert(key);keys_.enqueue(key);if(keys_.size()>4096)seen_.remove(keys_.dequeue());
        auto f=s.normalizedFeature?*s.normalizedFeature:FpgaWave::normalizedCycleFeature(s);
        if(!s.featureStreamId.isEmpty()) {
            if(remoteStream_==s.featureStreamId && remoteSequence_ && s.featureSequence!=remoteSequence_+1)f.quality|=16;
            if(remoteStream_!=s.featureStreamId && !remoteStream_.isEmpty()){++segment_;f.quality|=16;}
            remoteStream_=s.featureStreamId;remoteSequence_=s.featureSequence;
        }
        auto& v=r.feature;v.streamIdentity=stream_;v.hostTimeUs=host;v.waveformSequence=sequence_++;
        v.segmentId=segment_;v.channel=s.source;v.cycleId=s.cycleId;v.fpgaTick=s.timestamp;
        v.originalPoints=s.totalPoints;v.peak=f.peak;v.peakIndex=f.peakIndex;v.validPoints=f.validPoints;v.qualityFlags=f.quality;
        v.id=QString::fromLatin1(QCryptographicHash::hash((stream_+'|'+QString::number(v.channel)+'|'+QString::number(v.waveformSequence)).toUtf8(),QCryptographicHash::Sha256).toHex());
        r.deviceIdentity=identity_;++publishedCount_;if(f.quality&16)++continuityCount_;
    }
    emit peakReady(r);
}
}
