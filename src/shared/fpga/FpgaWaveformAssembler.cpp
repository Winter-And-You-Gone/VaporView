#include "FpgaWaveformAssembler.h"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace VaporView::FpgaWave {
namespace {
quint16 u16(const char *p) { return quint16(quint8(p[0])) | (quint16(quint8(p[1])) << 8); }
quint32 u32(const char *p) { return quint32(quint8(p[0])) | (quint32(quint8(p[1])) << 8) | (quint32(quint8(p[2])) << 16) | (quint32(quint8(p[3])) << 24); }
bool rawSource(quint16 source) { return source==0x20 || source==0x21; }
bool dliaSource(quint16 source) { return source==0x30 || source==0x31; }
constexpr quint32 maxPoints = 1u << 20;
constexpr quint16 maxFragments = 8192;
constexpr qint64 maxPayloadBytes = 8192;
}

Assembler::Assembler(std::size_t limit) : maxPendingGroupsPerSource_(limit)
{
    if(limit==0 || limit>16) throw std::invalid_argument("FPGA pending cycle limit must be 1..16");
}

bool Assembler::parseHeader(const Fragment& f, Pending& p, quint16& index,
    quint32& first, quint32& points, quint32& bpp, quint32& headerBytes)
{
    if(f.payload.size()<20 || f.payload.size()>maxPayloadBytes)return false;
    const bool raw=rawSource(f.source) && f.message==0x1000;
    const bool dlia=dliaSource(f.source) && f.message==0x1001;
    if(!raw && !dlia)return false;
    const char *data=f.payload.constData();
    p.stream.schema=u16(data);
    if(p.stream.schema!=2 && !(raw && p.stream.schema==1))return false;
    headerBytes=p.stream.schema==1?20:32;
    if(f.payload.size()<headerBytes)return false;
    p.stream.rate=u32(data+4);
    p.stream.totalPoints=u32(data+8);
    if(p.stream.rate==0 || p.stream.totalPoints==0 || p.stream.totalPoints>maxPoints)return false;
    if(raw) {
        const quint32 format=u32(data+12);
        if(format>1)return false;
        p.stream.format=quint16(format);
        p.stream.adcBits=u16(data+2);
        if(p.stream.adcBits==0 || p.stream.adcBits>32)return false;
        bpp=4;
        if(p.stream.schema==1) {
            if(p.stream.totalPoints>2043 || u32(data+16)!=0)return false;
            index=0;p.fragmentCount=1;first=0;points=p.stream.totalPoints;
        } else {
            index=u16(data+16);p.fragmentCount=u16(data+18);first=u32(data+20);points=u32(data+24);
            if(u32(data+28)!=0)return false;
        }
    } else {
        p.stream.format=u16(data+2);
        if(p.stream.format>2)return false;
        index=u16(data+12);p.fragmentCount=u16(data+14);first=u32(data+16);points=u32(data+20);bpp=u32(data+24);
        const quint32 expected=p.stream.format==0?8:p.stream.format==1?16:24;
        if(bpp!=expected || u32(data+28)!=0)return false;
    }
    p.stream.bytesPerPoint=bpp;
    if(p.fragmentCount==0 || p.fragmentCount>maxFragments || index>=p.fragmentCount ||
       points==0 || quint64(first)+points>p.stream.totalPoints)return false;
    if(quint64(headerBytes)+quint64(points)*bpp!=quint64(f.payload.size()))return false;
    return true;
}

CompletedStream Assembler::finish(const Pending& pending,bool expired)
{
    CompletedStream result=pending.stream;
    result.overflow=(result.flags & (1u<<3))!=0;
    result.partial=result.partial || (result.flags & (1u<<5))!=0 || expired;
    if(expired || pending.fragments.size()!=pending.fragmentCount)result.continuityError=true;
    std::vector<quint16> order;
    for(const auto& entry:pending.fragments)order.push_back(entry.first);
    std::sort(order.begin(),order.end(),[&](quint16 a,quint16 b){
        if(pending.firstPoints.at(a)!=pending.firstPoints.at(b))return pending.firstPoints.at(a)<pending.firstPoints.at(b);
        return a<b;
    });
    quint64 next=0;
    quint16 nextIndex=0;
    for(quint16 index:order) {
        const quint32 first=pending.firstPoints.at(index),count=pending.pointCounts.at(index);
        if(first!=next || index!=nextIndex)result.continuityError=true;
        next=quint64(first)+count;++nextIndex;
        result.fragmentFirstPoints.push_back(first);
        result.fragmentPointCounts.push_back(count);
        result.pointBytes.append(pending.fragments.at(index));
    }
    if(next!=result.totalPoints)result.continuityError=true;
    result.complete=!result.partial && !result.overflow && !result.continuityError;
    if(rawSource(result.source)) {
        for(qsizetype offset=0;offset<result.pointBytes.size();offset+=4) {
            const auto value=u32(result.pointBytes.constData()+offset);
            if(result.format==0)result.signed32Samples.push_back(qint32(value));
            else result.unsigned32Samples.push_back(value);
        }
    } else {
        for(qsizetype offset=0;offset<result.pointBytes.size();offset+=result.bytesPerPoint) {
            const char *data=result.pointBytes.constData()+offset;
            CompletedStream::DliaPoint point;
            if(result.format==0) {point.hasHarmonics=true;point.h1=qint32(u32(data));point.h2=qint32(u32(data+4));}
            else {
                point.hasIq=true;point.i1=qint32(u32(data));point.q1=qint32(u32(data+4));point.i2=qint32(u32(data+8));point.q2=qint32(u32(data+12));
                if(result.format==2){point.hasHarmonics=true;point.h1=qint32(u32(data+16));point.h2=qint32(u32(data+20));}
            }
            result.dliaPoints.push_back(point);
        }
    }
    return result;
}

void Assembler::expireOldest(quint16 source)
{
    auto oldest=pending_.end();
    for(auto it=pending_.begin();it!=pending_.end();++it) {
        if(it->first.source==source && (oldest==pending_.end() || it->second.arrivalOrder<oldest->second.arrivalOrder))oldest=it;
    }
    if(oldest!=pending_.end()){expired_.push_back(finish(oldest->second,true));pending_.erase(oldest);}
}

void Assembler::rememberCompleted(const Key& key)
{
    completed_[key]=++arrivalOrder_;
    std::size_t count=0;
    auto oldest=completed_.end();
    for(auto it=completed_.begin();it!=completed_.end();++it)if(it->first.source==key.source) {
        ++count;
        if(oldest==completed_.end() || it->second<oldest->second)oldest=it;
    }
    if(count>16)completed_.erase(oldest);
}

std::optional<CompletedStream> Assembler::accept(const Fragment& fragment)
{
    Pending parsed;
    parsed.stream.source=fragment.source;parsed.stream.message=fragment.message;parsed.stream.flags=fragment.flags;
    parsed.stream.cycleId=fragment.cycleId;parsed.stream.timestamp=fragment.timestamp;
    quint16 index=0;quint32 first=0,points=0,bpp=0,headerBytes=0;
    const Key key{fragment.source,fragment.message,fragment.cycleId,fragment.timestamp};
    auto existing=pending_.find(key);
    if(!parseHeader(fragment,parsed,index,first,points,bpp,headerBytes)) {
        // Unknown schema remains an opaque archived packet, never a numerical stream.
        // A malformed continuation invalidates an otherwise known pending cycle.
        if(existing!=pending_.end())existing->second.stream.continuityError=true;
        return std::nullopt;
    }
    if(completed_.count(key)) {
        parsed.stream.continuityError=true;
        parsed.fragments.emplace(index,fragment.payload.mid(headerBytes));
        parsed.firstPoints.emplace(index,first);parsed.pointCounts.emplace(index,points);
        return finish(parsed,true);
    }
    if(existing==pending_.end()) {
        size_t count=0;for(const auto& entry:pending_)if(entry.first.source==fragment.source)++count;
        if(count>=maxPendingGroupsPerSource_)expireOldest(fragment.source);
        parsed.arrivalOrder=++arrivalOrder_;
        existing=pending_.emplace(key,std::move(parsed)).first;
    } else {
        const auto& previous=existing->second.stream;
        if(previous.schema!=parsed.stream.schema || previous.format!=parsed.stream.format ||
           previous.rate!=parsed.stream.rate || previous.totalPoints!=parsed.stream.totalPoints ||
           previous.bytesPerPoint!=bpp || previous.adcBits!=parsed.stream.adcBits ||
           existing->second.fragmentCount!=parsed.fragmentCount) {
            existing->second.stream.continuityError=true;
            return std::nullopt;
        }
        if(previous.flags!=fragment.flags)existing->second.stream.continuityError=true;
        existing->second.stream.flags|=fragment.flags;
    }
    auto& pending=existing->second;
    if(pending.fragments.count(index)) {
        // Do not overwrite the first copy: duplicate/overlap evidence must survive.
        pending.stream.continuityError=true;
        return std::nullopt;
    }
    const quint64 incomingBytes=quint64(points)*bpp;
    if(pending.pointByteCount+incomingBytes>quint64(pending.stream.totalPoints)*bpp) {
        pending.stream.continuityError=true;
        return std::nullopt;
    }
    for(const auto& span:pending.firstPoints) {
        const quint64 end=quint64(span.second)+pending.pointCounts.at(span.first);
        if(quint64(first)<end && quint64(span.second)<quint64(first)+points)pending.stream.continuityError=true;
    }
    pending.fragments.emplace(index,fragment.payload.mid(headerBytes));
    pending.pointByteCount+=incomingBytes;
    pending.firstPoints.emplace(index,first);pending.pointCounts.emplace(index,points);
    if(pending.fragments.size()!=pending.fragmentCount)return std::nullopt;
    auto result=finish(pending);pending_.erase(existing);rememberCompleted(key);return result;
}

QVector<CompletedStream> Assembler::takeExpired()
{ QVector<CompletedStream> result;result.swap(expired_);return result; }
QVector<CompletedStream> Assembler::flush()
{
    auto result=takeExpired();
    for(const auto& entry:pending_)result.push_back(finish(entry.second,true));
    pending_.clear();return result;
}
void Assembler::clear() {pending_.clear();completed_.clear();expired_.clear();arrivalOrder_=0;}
std::size_t Assembler::pendingStreamCount() const {return pending_.size();}
} // namespace VaporView::FpgaWave
