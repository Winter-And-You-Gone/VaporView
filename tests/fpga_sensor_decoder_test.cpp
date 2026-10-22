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
    const auto hmpPayload=[](float rh,float temperature){QByteArray b;put16(b,1);put16(b,2);for(auto pair:{qMakePair(quint16(0x100),rh),qMakePair(quint16(0x101),temperature)}) {put16(b,pair.first);b.append(char(9));b.append(char(4));quint32 bits;std::memcpy(&bits,&pair.second,4);put32(b,bits);}return b;};
    auto hmp=FpgaSensorDecoder::decode(hmpPayload(45.25f,23.5f),0x41);require(hmp.kind==SensorKind::Hmp && hmp.validity.measurement && *hmp.temperatureC==23.5 && *hmp.humidityPct==45.25);
    require(!FpgaSensorDecoder::decode(hmpPayload(101.0f,23.5f),0x41).validity.measurement);
    require(!FpgaSensorDecoder::decode(hmpPayload(45.0f,std::numeric_limits<float>::quiet_NaN()),0x41).validity.measurement);
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
    const auto legacyRd=FpgaSensorDecoder::decode(rd105Payload(1,0,0,0),0x48);
    require(legacyRd.validity.measurement&&!legacyRd.rd105Channel&&!legacyRd.rd105OutputRaw&&!legacyRd.rd105OutputEnabled);
    const auto dualRd=FpgaSensorDecoder::decode(rd105Payload(2,2,-845000,1),0x48);
    require(dualRd.validity.measurement&&dualRd.rd105Channel==2&&dualRd.rd105OutputRaw==-845000&&dualRd.rd105OutputEnabled==1);
    const auto disabledRd=FpgaSensorDecoder::decode(rd105Payload(2,1,0,0),0x48);
    require(disabledRd.validity.measurement&&disabledRd.rd105OutputRaw==0&&disabledRd.rd105OutputEnabled==0);
    for(quint32 channel:{0u,3u})require(!FpgaSensorDecoder::decode(rd105Payload(2,channel,0,0),0x48).validity.measurement);
    require(!FpgaSensorDecoder::decode(rd105Payload(2,1,2000001,1),0x48).validity.measurement);
    require(!FpgaSensorDecoder::decode(rd105Payload(2,1,0,2),0x48).validity.measurement);
    std::cout<<"fpga sensor decoder tests passed\n"; }
