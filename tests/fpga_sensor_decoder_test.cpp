#include "FpgaSensorDecoder.h"
#include <QByteArray>
#include <cassert>
#include <iostream>
using namespace VaporView::FpgaSensor;
static void put16(QByteArray &b, quint16 v){b.append(char(v));b.append(char(v>>8));}
static void put32(QByteArray &b, quint32 v){for(int i=0;i<4;++i)b.append(char(v>>(8*i)));}
static QByteArray tlv(quint16 tag, quint8 type, quint32 value){QByteArray b;put16(b,1);put16(b,1);put16(b,tag);b.append(char(type));b.append(char(4));put32(b,value);return b;}
int main(){
    auto p=FpgaSensorDecoder::decode(tlv(0x12,7,101459000),0x40); assert(p.validity.structure&&p.pressurePa&&*p.pressurePa==101459.0);
    auto s=tlv(0x10,7,25321); s[2]=char(3); s[3]=char(3); /* malformed length is rejected */ assert(!FpgaSensorDecoder::decode(s,0x44).validity.structure);
    QByteArray e; e.append(char(0xfc));e.append(char(0x41));e.append(char(0));e.append(char(7));e.append(char(0));e.append(char(0));e.append(char(0));e.append(char(0xfd)); auto er=FpgaSensorDecoder::decode(e,0x42); assert(er.kind==SensorKind::Epsilon&&er.validity.structure&&!er.validity.crc);
    std::cout<<"fpga sensor decoder tests passed\n"; }
