#include "FpgaSensorDecoder.h"
#include <QByteArray>
#include <cstdlib>
#include <iostream>
#include <cstring>
#include <limits>
using namespace VaporView::FpgaSensor;
static void put16(QByteArray &b, quint16 v){b.append(char(v));b.append(char(v>>8));}
static void put32(QByteArray &b, quint32 v){for(int i=0;i<4;++i)b.append(char(v>>(8*i)));}
static QByteArray tlv(quint16 tag, quint8 type, quint32 value){QByteArray b;put16(b,1);put16(b,1);put16(b,tag);b.append(char(type));b.append(char(4));put32(b,value);return b;}
static void require(bool v){if(!v){std::cerr<<"decoder assertion failed\n";std::exit(1);}}
int main(){
    const auto hc2aPayload=[](float rh,float temperature){QByteArray b;put16(b,1);put16(b,2);for(auto pair:{qMakePair(quint16(0x100),rh),qMakePair(quint16(0x101),temperature)}) {put16(b,pair.first);b.append(char(9));b.append(char(4));quint32 bits;std::memcpy(&bits,&pair.second,4);put32(b,bits);}return b;};
    auto hc2a=FpgaSensorDecoder::decode(hc2aPayload(45.25f,23.5f),0x41);require(hc2a.kind==SensorKind::Hc2a && hc2a.validity.measurement && *hc2a.temperatureC==23.5 && *hc2a.humidityPct==45.25);
    require(!FpgaSensorDecoder::decode(hc2aPayload(101.0f,23.5f),0x41).validity.measurement);
    require(!FpgaSensorDecoder::decode(hc2aPayload(45.0f,std::numeric_limits<float>::quiet_NaN()),0x41).validity.measurement);
    require(!FpgaSensorDecoder::decode(hc2aPayload(45.0f,601.0f),0x41).validity.measurement);
    auto alarmPayload=hc2aPayload(45.0f,23.5f);alarmPayload[2]=3;
    put16(alarmPayload,2);alarmPayload.append(char(3));alarmPayload.append(char(4));put32(alarmPayload,4);
    const auto alarm=FpgaSensorDecoder::decode(alarmPayload,0x41,0x1100,16);
    require(!alarm.validity.measurement && alarm.temperatureC==23.5 && alarm.hc2aDeviceError==4);
    auto p=FpgaSensorDecoder::decode(tlv(0x12,7,101459000),0x40); require(p.validity.structure&&p.pressurePa&&*p.pressurePa==101459.0);
    auto s=tlv(0x10,7,25321); s[2]=char(3); s[3]=char(3); /* malformed length is rejected */ require(!FpgaSensorDecoder::decode(s,0x44).validity.structure);
    QByteArray e; e.append(char(0xfc));e.append(char(0x41));e.append(char(0));e.append(char(7));e.append(char(0));e.append(char(0));e.append(char(0));e.append(char(0xfd)); auto er=FpgaSensorDecoder::decode(e,0x42); require(er.kind==SensorKind::Epsilon&&er.validity.structure&&!er.validity.crc);
    const auto rd105Payload=[](quint16 schema,quint32 channel,qint32 duty,quint32 enabled) {
        QByteArray b;put16(b,schema);put16(b,schema==2?6:3);
        const auto add=[&](quint16 tag,quint8 type,quint32 value) {
            put16(b,tag);b.append(char(type));b.append(char(4));put32(b,value);
        };
        add(0x14,7,27000000);add(0x15,7,26250000);add(2,3,0);
        if(schema==2) {add(0x480,3,channel);add(0x481,7,quint32(duty));add(0x482,3,enabled);}
        return b;
    };
    const auto legacyRd=FpgaSensorDecoder::decode(rd105Payload(1,0,0,0),0x4a);
    require(legacyRd.validity.measurement&&!legacyRd.rd105Channel&&!legacyRd.rd105OutputRaw&&!legacyRd.rd105OutputEnabled);
    const auto dualRd=FpgaSensorDecoder::decode(rd105Payload(2,2,-845000,1),0x4a);
    require(dualRd.validity.measurement&&dualRd.rd105Channel==2&&dualRd.rd105OutputRaw==-845000&&dualRd.rd105OutputEnabled==1);
    require(FpgaSensorDecoder::decode(rd105Payload(2,2,-845000,1),0x48).kind==SensorKind::Unknown);
    const auto disabledRd=FpgaSensorDecoder::decode(rd105Payload(2,1,0,0),0x4a);
    require(disabledRd.validity.measurement&&disabledRd.rd105OutputRaw==0&&disabledRd.rd105OutputEnabled==0);
    // Cross-check the lower-computer RD105 schema2 golden vectors, not the TLV helper above.
    const auto goldenChannel1=FpgaSensorDecoder::decode(QByteArray::fromHex(
        "020006001400070440787d011500070440787d010200030400000000800403040100000081040704c063ffff8204030400000000"),0x4a);
    require(goldenChannel1.validity.measurement&&goldenChannel1.rd105Channel==1&&goldenChannel1.rd105TargetMicroC==25000000
        &&goldenChannel1.rd105ActualMicroC==25000000&&goldenChannel1.rd105OutputRaw==-40000);
    const auto goldenChannel2=FpgaSensorDecoder::decode(QByteArray::fromHex(
        "0200060014000704e04341ff15000704806967ff0200030400000000800403040200000081040704204e00008204030400000000"),0x4a);
    require(goldenChannel2.validity.measurement&&goldenChannel2.rd105Channel==2&&goldenChannel2.rd105TargetMicroC==-12500000
        &&goldenChannel2.rd105ActualMicroC==-10000000&&goldenChannel2.rd105OutputRaw==20000);
    for(quint32 channel:{0u,3u})require(!FpgaSensorDecoder::decode(rd105Payload(2,channel,0,0),0x4a).validity.measurement);
    require(!FpgaSensorDecoder::decode(rd105Payload(2,1,2000001,1),0x4a).validity.measurement);
    require(!FpgaSensorDecoder::decode(rd105Payload(2,1,0,2),0x4a).validity.measurement);
    for(qint32 temperature:{-400000000,1000000000}) {
        auto boundary=rd105Payload(2,1,0,0);
        for(int offset:{8,16})for(int byte=0;byte<4;++byte)boundary[offset+byte]=char(quint32(temperature)>>(8*byte));
        require(FpgaSensorDecoder::decode(boundary,0x4a).validity.measurement);
    }
    for(qint32 temperature:{-400000010,1000000010,25000001,std::numeric_limits<qint32>::min()}) {
        auto invalid=rd105Payload(2,1,0,0);
        for(int byte=0;byte<4;++byte)invalid[16+byte]=char(quint32(temperature)>>(8*byte));
        require(!FpgaSensorDecoder::decode(invalid,0x4a).validity.measurement);
    }
    for(quint32 mm:{50u,43540u,300000u}) {
        const auto range=FpgaSensorDecoder::decode(tlv(0x13,3,mm),0x45);
        require(range.kind==SensorKind::Sdfm300 && range.validity.measurement && range.distanceMm==mm);
    }
    for(quint32 mm:{0u,49u,300001u,655350u,0xffffffffu})
        require(!FpgaSensorDecoder::decode(tlv(0x13,3,mm),0x45).validity.measurement);
    std::cout<<"fpga sensor decoder tests passed\n"; }
