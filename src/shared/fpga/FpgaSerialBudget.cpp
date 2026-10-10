#include "FpgaSerialBudget.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <limits>

namespace VaporView {
void FpgaSerialBudget::reset(int baud, qint64 nowMs)
{
    capacity_=qMax<qint64>(0,qint64(baud)/40); // baud / 10 bits * 25%.
    tokens_=double(capacity_); lastMs_=nowMs; sensorSentMs_.clear();
}
qint64 FpgaSerialBudget::maximumPendingBytes(int baud)
{ return qMax<qint64>(16*1024,qint64(qMax(0,baud))/5); }
bool FpgaSerialBudget::queueFits(int baud,qint64 pending,qint64 incoming)
{
    const auto maximum=maximumPendingBytes(baud);
    return pending>=0 && incoming>=0 && incoming<=maximum && pending<=maximum-incoming;
}
bool FpgaSerialBudget::accept(qint64 bytes,qint64 nowMs,int sensorSource)
{
    if(nowMs<lastMs_) { tokens_=0;lastMs_=nowMs;sensorSentMs_.clear(); }
    tokens_=std::min(double(capacity_),tokens_+double(nowMs-lastMs_)*double(capacity_)/1000.0);lastMs_=nowMs;
    if(bytes<=0 || bytes>maximumFrameBytes() || double(bytes)>tokens_) return false;
    if(sensorSource>=0 && sensorSentMs_.contains(sensorSource) && nowMs-sensorSentMs_.value(sensorSource)<1000) return false;
    tokens_-=double(bytes);
    if(sensorSource>=0) sensorSentMs_.insert(sensorSource,nowMs);
    return true;
}
QByteArray FpgaSerialBudget::previewPayload(const QByteArray& payload) const
{
    const auto document=QJsonDocument::fromJson(payload);
    if(!document.isObject()) return {};
    const auto original=document.object();
    const auto points=original.value("points").toArray();
    const double originalStride=original.value("stride").toDouble();
    if(points.size()>64 || originalStride<1 || originalStride>4294967295.0) return {};
    // Input is the bounded 64-point IPC preview. Never mutate its stride,
    // sample rate, or original point count; compose additional decimation.
    for(const int limit:{64,32,16,8}) {
        const int step=qMax(1,(int(points.size())+limit-1)/limit);
        const quint64 stride=quint64(originalStride)*quint64(step);
        if(stride>std::numeric_limits<quint32>::max()) continue;
        auto output=original;QJsonArray reduced;
        for(qsizetype i=0;i<points.size();i+=step) reduced.append(points[i]);
        output.insert("points",reduced);output.insert("stride",double(stride));
        const auto bytes=QJsonDocument(output).toJson(QJsonDocument::Compact);
        if(bytes.size()+21<=maximumFrameBytes()) return bytes;
    }
    return {}; // Even eight points cannot fit: skip the complete frame.
}
}
