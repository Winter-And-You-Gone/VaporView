#include "FpgaWaveformAssembler.h"
#include <cstdlib>
#include <iostream>
using namespace VaporView::FpgaWave;
namespace {
void check(bool ok,const char *name){if(!ok){std::cerr<<"FAILED: "<<name<<'\n';std::exit(1);}}
void put16(QByteArray& b,quint16 v){b.append(char(v));b.append(char(v>>8));}
void put32(QByteArray& b,quint32 v){for(int i=0;i<4;++i)b.append(char(v>>(8*i)));}
void replace32(QByteArray& b,int offset,quint32 v){for(int i=0;i<4;++i)b[offset+i]=char(v>>(8*i));}
QByteArray raw(quint16 schema,quint16 index,quint16 fragments,quint32 first,quint32 total,
               const QVector<quint32>& values,quint32 format=0)
{
    QByteArray b;put16(b,schema);put16(b,24);put32(b,1000000);put32(b,total);put32(b,format);
    if(schema==1)put32(b,0);
    else {put16(b,index);put16(b,fragments);put32(b,first);put32(b,values.size());put32(b,0);}
    for(auto value:values)put32(b,value);return b;
}
QByteArray dlia(quint16 format,quint16 index,quint16 fragments,quint32 first,quint32 total,
                const QVector<qint32>& words)
{
    const quint32 bpp=format==0?8:format==1?16:24;
    QByteArray b;put16(b,2);put16(b,format);put32(b,10000);put32(b,total);put16(b,index);put16(b,fragments);
    put32(b,first);put32(b,quint32(words.size())*4/bpp);put32(b,bpp);put32(b,0);
    for(auto value:words)put32(b,quint32(value));return b;
}
Fragment adc(const QByteArray& payload,quint32 cycle=5,quint64 tick=100,quint32 flags=3)
{return {0x20,0x1000,flags,cycle,tick,payload};}
}
int main()
{
    Assembler assembler;
    const auto first=raw(2,0,2,0,4,{10,11});const auto second=raw(2,1,2,2,4,{12,13});
    check(!assembler.accept(adc(second)),"out of order begins with last fragment");
    auto result=assembler.accept(adc(first));check(result.has_value() && result->complete,"out of order complete");
    check(result->signed32Samples==QVector<qint32>({10,11,12,13}),"ordered by point position");
    check(result->adcBits==24 && result->rate==1000000,"ADC units metadata");
    result=assembler.accept(adc(second));
    check(result.has_value() && !result->complete && result->continuityError,"duplicate after completed cycle not shown as a new complete scan");
    result=assembler.accept(adc(raw(1,0,1,0,3,{0x80000000,0xffffffff,0x7fffffff}),6));
    check(result.has_value() && result->complete && result->schema==1,"RAW schema1 accepted");
    check(result->signed32Samples==QVector<qint32>({qint32(0x80000000u),-1,0x7fffffff}),"signed raw sign extension");
    result=assembler.accept(adc(raw(1,0,1,0,2,{0xffffffff,0x80000000},1),7));
    check(result.has_value() && result->signed32Samples.isEmpty() && result->unsigned32Samples==QVector<quint32>({0xffffffff,0x80000000}),"unsigned raw keeps magnitude");
    for(quint16 format=0;format<3;++format){
        QVector<qint32> words=format==0?QVector<qint32>{123,456}:format==1?QVector<qint32>{-1,-2,3,4}:QVector<qint32>{-1,-2,3,4,123,456};
        result=assembler.accept({0x30,0x1001,3,quint32(10+format),200,dlia(format,0,1,0,1,words)});
        check(result.has_value() && result->complete && result->dliaPoints.size()==1,"DLIA formats decoded");
        const auto& point=result->dliaPoints[0];
        check(point.hasIq==(format!=0) && point.hasHarmonics==(format!=1),"DLIA component presence explicit");
        if(format!=0)check(point.i1==-1 && point.q1==-2 && point.i2==3 && point.q2==4,"IQ values signed");
        if(format!=1)check(point.h1==123 && point.h2==456,"H1 H2 values");
        check(result->signed32Samples.isEmpty(),"DLIA not raw sample vector");
    }
    check(!assembler.accept(adc(first,20,100,0x28)),"flagged start");
    result=assembler.accept(adc(second,20,100,0x28));
    check(result.has_value() && !result->complete && result->partial && result->overflow && !result->continuityError,"flagged full cycle not complete");
    check(!assembler.accept(adc(first,21)),"duplicate test start");
    check(!assembler.accept(adc(first,21)),"duplicate rejected preserving first");
    result=assembler.accept(adc(second,21));check(result.has_value() && result->continuityError && !result->complete,"duplicate evidence retained");
    check(!assembler.accept(adc(first,22)),"overlap start");
    result=assembler.accept(adc(raw(2,1,2,1,4,{20,21}),22));
    check(result.has_value() && result->continuityError && !result->complete,"overlap and missing tail fail");
    check(!assembler.accept(adc(raw(2,0,2,1,4,{10}),23)),"nonzero first start");
    result=assembler.accept(adc(raw(2,1,2,2,4,{12,13}),23));
    check(result.has_value() && result->continuityError,"missing first point fails");
    check(!assembler.accept(adc(first,24)),"count mismatch first");
    check(!assembler.accept(adc(raw(2,1,3,2,4,{12,13}),24)),"fragment count drift rejected");
    result=assembler.accept(adc(second,24));check(result.has_value() && result->continuityError,"count drift remains quality fault");
    QByteArray huge=first;replace32(huge,8,0xffffffff);check(!assembler.accept(adc(huge,30)),"hostile total count rejected");
    QByteArray wrongFormat=first;replace32(wrongFormat,12,0x10000);check(!assembler.accept(adc(wrongFormat,31)),"u32 format not truncated to16bits");
    QByteArray invalid=first;invalid[0]=char(99);check(!assembler.accept(adc(invalid,32)),"unknown schema opaque");
    check(!assembler.accept({0x30,0x1001,3,33,0,raw(1,0,1,0,1,{1})}),"undocumented DLIA schema1 not invented");
    auto wrongBpp=dlia(0,0,1,0,1,{1,2});replace32(wrongBpp,24,16);
    check(!assembler.accept({0x30,0x1001,3,34,0,wrongBpp}),"DLIA format byte-size disagreement rejected");
    check(assembler.pendingStreamCount()==0,"invalid new packets do not allocate cycles");
    Assembler bounded(2);
    check(!bounded.accept(adc(first,40,100)),"bounded cycle1");
    check(!bounded.accept(adc(first,41,200)),"bounded cycle2");
    check(!bounded.accept(adc(first,42,300)),"bounded cycle3 expires oldest");
    auto expired=bounded.takeExpired();check(expired.size()==1 && expired[0].cycleId==40 && expired[0].partial && expired[0].continuityError && !expired[0].complete,"oldest missing-cycle evidence");
    check(bounded.pendingStreamCount()==2,"bounded cycle memory");
    result=bounded.accept(adc(second,42,300));check(result.has_value() && result->complete,"new cycle never patches expired cycle");
    auto flushed=bounded.flush();check(flushed.size()==1 && flushed[0].cycleId==41 && flushed[0].fragmentFirstPoints==QVector<quint32>({0}),"flush exposes missing fragment spans");
    check(bounded.pendingStreamCount()==0 && bounded.takeExpired().isEmpty(),"flush clears bounded cache");
    check(!bounded.accept(adc(first,50,100)),"first tagged group");
    check(!bounded.accept(adc(second,50,101)),"same cycle different tick isolated");
    check(bounded.flush().size()==2,"timestamp part of group key");
    std::cout<<"fpga_waveform_assembler_test passed; software fixtures only\n";
    return 0;
}
