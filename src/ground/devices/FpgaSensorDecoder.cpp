#include "FpgaSensorDecoder.h"

#include <QtGlobal>

#include <cmath>

namespace VaporView::FpgaSensor
{
namespace
{
quint16 u16(const char *p) { return quint16(quint8(p[0])) | (quint16(quint8(p[1])) << 8); }
quint16 be16(const char *p) { return (quint16(quint8(p[0])) << 8) | quint16(quint8(p[1])); }
quint32 u32(const char *p) { return quint32(quint8(p[0])) | (quint32(quint8(p[1])) << 8) | (quint32(quint8(p[2])) << 16) | (quint32(quint8(p[3])) << 24); }
qint32 i32(const char *p) { return qint32(u32(p)); }
quint64 u64le(const char *p) { quint64 v=0; for (int i=0;i<8;++i) v |= quint64(quint8(p[i])) << (8*i); return v; }
bool readU32(const TlvRecord &r, quint32 &v) { if (r.type != 3 || r.value.size()!=4) return false; v=u32(r.value.constData()); return true; }
bool readI32(const TlvRecord &r, qint32 &v) { if (r.type != 7 || r.value.size()!=4) return false; v=i32(r.value.constData()); return true; }
const TlvRecord *find(const std::vector<TlvRecord> &rs, quint16 tag) { for (const auto &r:rs) if (r.tag==tag) return &r; return nullptr; }

quint8 crc8(const QByteArray &b, int n) { quint8 c=0; for(int i=0;i<n;++i){ c ^= quint8(b[i]); for(int j=0;j<8;++j) c=(c&1)?quint8((c>>1)^0x8c):quint8(c>>1); } return c; }
quint16 crc16(const char *p, int n) { quint16 c=0; for(int i=0;i<n;++i){ c ^= quint16(quint8(p[i]))<<8; for(int j=0;j<8;++j) c=(c&0x8000)?quint16((c<<1)^0x1021):quint16(c<<1); } return c; }
}

bool FpgaSensorDecoder::parseTlv(const QByteArray &payload, quint16 &schema, std::vector<TlvRecord> &records)
{
    records.clear();
    if (payload.size()<4) return false;
    schema=u16(payload.constData()); const quint16 count=u16(payload.constData()+2);
    int off=4;
    for (quint16 i=0;i<count;++i) {
        if (off+4>payload.size()) return false;
        const quint16 tag=u16(payload.constData()+off); const quint8 type=quint8(payload[off+2]); const int len=quint8(payload[off+3]); off+=4;
        if (off+len>payload.size()) return false;
        records.push_back({tag,type,payload.mid(off,len)}); off += len;
        off = (off+3)&~3;
        if (off>payload.size()) return false;
    }
    return off==payload.size();
}

Reading FpgaSensorDecoder::decode(const QByteArray &payload, quint16 source, quint16 message, quint32 flags, quint64 timestamp)
{
    Reading out; out.source=source; out.message=message; out.flags=flags; out.timestamp=timestamp; out.rawPayload=payload;
    out.validity.rawFlags=flags; out.validity.continuityLoss=(flags&(1u<<3))!=0;
    if (source==0x0042) {
        out.kind=SensorKind::Epsilon; out.validity.structure=payload.size()>=8;
        if (!out.validity.structure || quint8(payload[0])!=0xfc) { out.validity.crc=false; return out; }
        const int len=quint8(payload[2]); out.validity.structure=(payload.size()==len+8 && quint8(payload[7+len])==0xfd);
        if (!out.validity.structure) return out;
        const quint8 id=quint8(payload[1]); out.epsilonMessageId=id; out.epsilonSequence=quint8(payload[3]); out.epsilonData=payload.mid(7,len);
        out.validity.crc=(crc8(payload,4)==quint8(payload[4])) && (crc16(payload.constData()+7,len)==be16(payload.constData()+5));
        if (out.validity.crc && id==0x41 && len==48) { out.epsilonDeviceTimestampUs=qint64(u64le(payload.constData()+47)); out.validity.measurement=true; }
        return out;
    }
    out.kind = source==0x0040?SensorKind::Ptb210:source==0x0043?SensorKind::Bmp390:source==0x0044?SensorKind::Sht45:source==0x0045?SensorKind::Tfa1500:source==0x0046?SensorKind::Ai8:SensorKind::Unknown;
    out.validity.structure=parseTlv(payload,out.schema,out.records); if(!out.validity.structure) return out;
    auto scalarI=[&](quint16 tag, std::optional<qint32> &dst){ if(auto r=find(out.records,tag)){qint32 v; if(readI32(*r,v)) dst=v;} };
    auto scalarU=[&](quint16 tag, std::optional<quint32> &dst){ if(auto r=find(out.records,tag)){quint32 v; if(readU32(*r,v)) dst=v;} };
    if (out.kind==SensorKind::Ptb210 && out.schema==1) { scalarI(0x0012,out.pressureMilliPa); if(out.pressureMilliPa) out.pressurePa=*out.pressureMilliPa/1000.0; }
    else if(out.kind==SensorKind::Sht45 && out.schema==1){ std::optional<qint32> t; scalarI(0x10,t); if(t) out.temperatureC=*t/1000.; if(auto r=find(out.records,0x11)){quint32 v; if(readU32(*r,v)) out.humidityPct=v/1000.;} scalarU(1,out.heaterMode); }
    else if(out.kind==SensorKind::Tfa1500 && out.schema==1) scalarU(0x13,out.distanceMm);
    else if(out.kind==SensorKind::Bmp390 && out.schema==1){ if(auto r=find(out.records,0x100);r&&r->type==11)out.bmpCalibration=r->value; scalarU(0x101,out.bmpPressureRaw); scalarU(0x102,out.bmpTemperatureRaw); }
    else if(out.kind==SensorKind::Ai8 && out.schema==2){ scalarI(0x460,out.ai8PvRaw); scalarI(0x461,out.ai8SpRaw); scalarI(0x462,out.ai8SvRaw); scalarU(0x463,out.ai8OpRaw); scalarU(0x464,out.ai8Alarm); scalarU(0x465,out.ai8Control); scalarU(0x466,out.ai8Host); scalarU(0x467,out.ai8SetResult); scalarU(1,out.ai8DeviceStatus); scalarU(2,out.ai8DeviceError); scalarU(4,out.ai8SampleCounter); scalarI(0x14,out.ai8PvMicroC); scalarI(0x15,out.ai8SpMicroC); if(out.ai8DeviceStatus) out.validity.deviceOnline=(*out.ai8DeviceStatus&1)!=0; }
    out.validity.measurement = out.kind!=SensorKind::Unknown;
    if (out.kind==SensorKind::Ptb210) out.validity.measurement=out.pressureMilliPa.has_value();
    if (out.kind==SensorKind::Sht45) out.validity.measurement=out.temperatureC.has_value()&&out.humidityPct.has_value()&&out.heaterMode.has_value();
    if (out.kind==SensorKind::Tfa1500) out.validity.measurement=out.distanceMm.has_value();
    if (out.kind==SensorKind::Ai8) out.validity.measurement=out.ai8DeviceStatus.has_value() && out.ai8DeviceError.has_value();
    out.validity.measurement = out.validity.measurement && !(flags&(1u<<4));
    return out;
}
}
