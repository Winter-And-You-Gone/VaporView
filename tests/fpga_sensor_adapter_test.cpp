#include "FpgaSensorAdapter.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace VaporView::FpgaSensor;
namespace
{
void require(bool v, const char *why) { if (!v) { std::cerr << why << '\n'; std::exit(1); } }
void put16(QByteArray &b, quint16 v) { b.append(char(v)); b.append(char(v >> 8)); }
void put32(QByteArray &b, quint32 v) { for (int i=0;i<4;++i) b.append(char(v >> (i*8))); }
void record(QByteArray &b, quint16 tag, quint8 type, const QByteArray &v)
{
    put16(b,tag); b.append(char(type)); b.append(char(v.size())); b.append(v);
    while (b.size()%4) b.append(char(0));
}
QByteArray value32(quint32 v) { QByteArray b; put32(b,v); return b; }
quint8 crc8(const QByteArray &b)
{
    quint8 c=0; for(int i=0;i<4;++i) { c ^= quint8(b[i]); for(int j=0;j<8;++j) c=(c&1)?quint8((c>>1)^0x8c):quint8(c>>1); } return c;
}
QByteArray frame(quint8 id, const QByteArray &data)
{
    QByteArray b; b.append(char(0xfc)); b.append(char(id)); b.append(char(data.size())); b.append(char(7)); b.append(char(crc8(b)));
    quint16 c=0; for(auto v:data) { c ^= quint16(quint8(v))<<8; for(int j=0;j<8;++j) c=c&0x8000?quint16((c<<1)^0x1021):quint16(c<<1); }
    b.append(char(c>>8)); b.append(char(c)); b.append(data); b.append(char(0xfd)); return b;
}
template<class T> void at(QByteArray &b, int offset, T v) { std::memcpy(b.data()+offset,&v,sizeof(v)); }
Reading eps(quint8 id, const QByteArray &b, quint64 tick)
{
    return FpgaSensorDecoder::decode(frame(id,b),0x42,0x1200,0,tick);
}
}
int main()
{
    FpgaSensorAdapter adapter;
    QByteArray raw; put16(raw,1); put16(raw,2);
    record(raw,0x101,3,value32(6461000)); record(raw,0x102,3,value32(8499712));
    auto reading=FpgaSensorDecoder::decode(raw,0x43);
    require(!adapter.accept(reading).reading.pressurePa,"BMP must await calibration");
    QByteArray cal; put16(cal,1); put16(cal,1);
    record(cal,0x100,11,QByteArray::fromHex("986d4c4bf9fd1c4d1706019349005a03fa800f08f5"));
    adapter.accept(FpgaSensorDecoder::decode(cal,0x43));
    auto compensated=adapter.accept(reading);
    // Python reference from the source-reviewed Bosch polynomial, not hardware validation.
    require(compensated.reading.pressurePa && std::abs(*compensated.reading.pressurePa-101537.00931008058)<1e-7,"BMP golden pressure Pa");
    require(compensated.reading.temperatureC && std::abs(*compensated.reading.temperatureC-23.606603474356234)<1e-10,"BMP golden temperature C");
    adapter.reset(); require(!adapter.accept(reading).reading.pressurePa,"BMP reconnect clears calibration");
    adapter.accept(FpgaSensorDecoder::decode(cal,0x43));
    reading.bmpPressureRaw=0x1000000; require(!adapter.accept(reading).reading.pressurePa,"BMP unsigned 24-bit bounds");

    QByteArray ahrs(48,char(0)); at<float>(ahrs,12,0.25f); at<float>(ahrs,24,1.f);
    auto attitude=adapter.accept(eps(0x41,ahrs,100));
    require(attitude.epsilon && attitude.attitudeValid && !attitude.navigationValid && !attitude.utcValid,"indoor AHRS does not invent GNSS/UTC");
    require(std::abs(attitude.epsilon->roll_deg-14.32394487827)<1e-8,"AHRS radians to degrees");
    QByteArray state(102,char(0)); at<quint16>(state,2,0x6f); at<quint32>(state,6,1700000000); at<quint32>(state,10,500000);
    at<double>(state,14,0.5); at<double>(state,22,-1.0); at<double>(state,30,12.5);
    auto system=adapter.accept(eps(0x50,state,1000));
    require(system.navigationValid && system.utcValid && system.orientationInitialized,"SYS_STATE own initialized flags");
    require(std::abs(system.epsilon->latitude_deg-28.64788975654)<1e-8 && std::abs(system.epsilon->longitude_deg+57.29577951308)<1e-8,"latitude first and signed longitude");
    QByteArray ins(72,char(0)); at<float>(ins,24,-100.f); at<float>(ins,32,20.f); at<float>(ins,44,-3.f);
    auto navigation=adapter.accept(eps(0x42,ins,1001));
    require(navigation.navigationValid && navigation.epsilon->ned_n_m==-100 && navigation.epsilon->ned_d_m==20,"NED remains local distance, down positive");
    require(!adapter.accept(eps(0x42,ins,200001001)).navigationValid,"stale associated status cannot authorize navigation");
    adapter.reset(); require(!adapter.accept(eps(0x42,ins,1001)).navigationValid,"reconnect loses old status");
    adapter.accept(eps(0x50,state,1000)); at<quint32>(state,10,1000000);
    require(!adapter.accept(eps(0x50,state,1001)).utcValid,"invalid UTC microseconds");
    auto bad=frame(0x50,state); bad[5]=char(quint8(bad[5])^1);
    require(!adapter.accept(FpgaSensorDecoder::decode(bad,0x42,0x1200)).epsilon,"CRC failure never alters measurement");
    require(!adapter.accept(eps(0x50,QByteArray(101,char(0)),1002)).epsilon,"SYS_STATE exact length");
    require(!adapter.accept(eps(0x7f,QByteArray(4,char(0)),1003)).epsilon,"unknown ID is retained raw only");

    QByteArray ai; put16(ai,2); put16(ai,13);
    const quint16 tags[]={0x460,0x461,0x462,0x463,0x464,0x465,0x466,0x467,1,2,4,0x14,0x15};
    const qint32 vals[]={-25,400,400,500,0x20,1,0x200,0x10000,1,0x2000120,12,40000000,-2500000};
    for(int i=0;i<13;++i) record(ai,tags[i],i<3||i>=11?7:3,value32(quint32(vals[i])));
    auto aiReading=FpgaSensorDecoder::decode(ai,0x46);
    auto sample=adapter.accept(aiReading);
    require(sample.ai8 && sample.ai8->measurementValid && sample.ai8->measuredC==-2.5 && sample.ai8->setpointC==40,"AI8 signed tenths");
    require(aiReading.ai8SpMicroC==40000000 && aiReading.ai8PvMicroC==-2500000,"AI8 optional tag14 is SP, tag15 is PV");
    require(sample.ai8->alarmActive && sample.ai8->online && sample.ai8->setSucceeded,"alarm independent of connection and verified set result");
    require(sample.ai8Live && sample.ai8Live->measuredC[0]==-2.5 && std::isnan(sample.ai8Live->measuredC[1]),"AI8 maps only selected channel");
    aiReading.ai8DeviceStatus=3; sample=adapter.accept(aiReading);
    require(!sample.ai8->measurementValid && sample.ai8->online,"AI8 transport failure isolates stale snapshot");
    aiReading.ai8SetResult=0; require(!adapter.accept(aiReading).ai8->setSucceeded,"zero result without valid bit is not success");
    std::cout << "fpga sensor adapter tests passed\n";
}
