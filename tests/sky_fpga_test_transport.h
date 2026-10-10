#pragma once
#include "FpgaDeviceController.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPointer>
#include <QThread>
#include <QtEndian>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <atomic>
#include <limits>

using namespace VaporView;
using namespace VaporView::Ground::Devices;
namespace {
void require(bool ok,const char *message) { if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);} }
void append16(std::vector<std::uint8_t>& b,quint16 v){b.push_back(v&255);b.push_back(v>>8);}
void append32(std::vector<std::uint8_t>& b,quint32 v){for(int i=0;i<4;++i)b.push_back((v>>(i*8))&255);}
quint32 word(const std::vector<std::uint8_t>& b,int i){require(i+4<=int(b.size()),"complete command payload");return qFromLittleEndian<quint32>(b.data()+i);}
quint16 half(const std::vector<std::uint8_t>& b,int i){require(i+2<=int(b.size()),"complete command count");return qFromLittleEndian<quint16>(b.data()+i);}
void waitUntil(const std::function<bool()>& done,const char *message,int timeout=12000){
    QElapsedTimer clock;clock.start();while(!done()&&clock.elapsed()<timeout){QCoreApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}require(done(),message);
}
struct Command { quint16 message=0,source=0;quint32 address=0,value=0;quint16 count=0; };
// Model the documented readable register map, including sparse system and
// stream pages. Unmapped addresses and the write-only watchdog are rejected.
bool readable(quint32 address){
    if(address%4)return false;
    if(address<=0x10||(address>=0x20&&address<=0x40)||(address>=0x48&&address<=0x50)||(address>=0x110&&address<=0x118))return true;
    if(address>=0x1000&&address<=0x1040)return true;
    const auto base=address&~255u,offset=address&255;
    if(base==0x2000||base==0x2100)return offset<=0x48;
    if(base==0x3000||base==0x3100)return offset<=0x44;
    if(base==0x4000||base==0x4100)return offset<=0x3c;
    if(base==0x5000||base==0x5100)return offset<=0x54;
    if(base>=0x6000&&base<=0x6700){if(offset<=12)return true;if(base==0x6600)return offset<=0x64;return base==0x6300&&offset==0x10;}
    if(base==0x7000)return offset<=12||offset==0x14||(offset>=0x1c&&offset<=0x30)||(offset>=0x38&&offset<=0x44);
    if(base==0x8000)return offset<=0x3c;
    return false;
}
class ScriptedTransport final : public FpgaUsbTransport {
public:
    std::atomic<int> temperatureWrites{0}, openCount{0};
    std::shared_ptr<std::atomic_bool> destroyedClosed;
    ~ScriptedTransport() override { if(destroyedClosed) destroyedClosed->store(closed && !opened); }
    bool opened=false,closed=false,strict=true;
    bool holdResponses=false;
    quint32 failWriteAddress=0xffffffff;
    bool failTemperature=false,confirmTemperature=true;
    QMap<quint32,quint32> regs;
    QMap<quint32,int> pendingReads;
    QVector<Command> commands;
    QByteArray incoming;
    QByteArray sensorWire;
    ScriptedTransport(){
        regs={{0x4000,0x00200101},{0x4100,0x00210101},{0x4018,0x00011800},{0x4118,0x00011800},
            {0x5000,0x00300101},{0x5100,0x00310101},{0x5024,0x00040101},{0x5124,0x00040101},{0x6600,0x00460200},
            {0x4010,1000000},{0x4014,1000000},{0x4114,1000000},{0x504c,10000},{0x514c,10000},
            {0x5050,100},{0x5150,100},{0x6614,1},{0x661c,1},{0x662c,1000}};
        regs[0x8004]=1;regs[0x8014]=1;regs[0x7004]=1;regs[0x2c]=regs[0x30]=0xffffffff;
        regs[0x701c]=9;regs[0x802c]=7;regs[0x8034]=3;
        for(quint32 b:{0x2000u,0x2100u,0x3000u,0x3100u,0x4000u,0x4100u,0x5000u,0x5100u})regs[b+8]=2;
    }
    bool open(const QString&) override {++openCount;opened=true;closed=false;return true;}
    void close() override {opened=false;closed=true;incoming.clear();}
    bool isOpen() const override{return opened;}
    qint64 read(QByteArray& out,qint64 max) override{if(holdResponses)return 0;const auto n=qMin<qint64>(max,incoming.size());out=incoming.left(n);incoming.remove(0,n);return n;}
    FpgaUsbDiagnostic diagnostic() const override{return {};}
    qint64 write(const QByteArray& bytes) override {
        FpgaVlp1::StreamParser parser;const auto frames=parser.feed(reinterpret_cast<const std::uint8_t*>(bytes.constData()),bytes.size());
        require(frames.size()==1,"exactly one valid VLP command per mock write");const auto &f=frames.front();
        require(f.header.frameType==FpgaVlp1::FrameType::Command,"only commands sent to hardware");
        Command command;command.message=f.header.message;command.source=f.header.source;
        if(command.message==2||command.message==3){command.address=word(f.payload,0);command.count=half(f.payload,4);if(command.message==3)command.value=word(f.payload,8);}
        else if(command.message==0xb){command.address=word(f.payload,0);command.value=word(f.payload,4);}
        commands.append(command);
        if(command.message==0xb && command.address==17) ++temperatureWrites;
        quint32 status=0;
        if(command.message==2&&strict){for(int i=0;i<command.count;++i)if(!readable(command.address+4*i))status=5;}
        if(command.message==3&&command.address==failWriteAddress)status=7;
        if(command.message==0xb&&command.address==17&&failTemperature)status=14;
        std::vector<std::uint8_t> response;append32(response,status);
        if(!status){
            if(command.message==0xfe)response.insert(response.end(),f.payload.begin(),f.payload.end());
            else if(command.message==1){for(quint32 v:{0xffffffffu,0x100u,100000000u,8192u,4096u,0u})append32(response,v);}
            else if(command.message==2){
                append32(response,command.address);append16(response,command.count);append16(response,0);
                for(int i=0;i<command.count;++i){const auto a=command.address+4*i;quint32 v=regs.value(a);if(pendingReads.value(a)>0){--pendingReads[a];v|=4;}append32(response,v);}
            } else if(command.message==3){
                for(int i=0;i<command.count;++i){const auto a=command.address+4*i;const auto value=word(f.payload,8+i*4);regs[a]=value;
                    if((a&255)==4&&a>=0x2000&&a<=0x5104){const auto b=a&~255u;regs[b+8]=(value&1)|2;if(b==0x4000||b==0x4100)regs[b+8]|=0x10;}
                }
            } else if(command.message==5){
                const quint32 base=command.source>=0x30?0x5000+(command.source-0x30)*0x100:
                    command.source>=0x20?0x4000+(command.source-0x20)*0x100:
                    command.source>=0x12?0x3000+(command.source-0x12)*0x100:0x2000+(command.source-0x10)*0x100;
                pendingReads[base+8]=2;
                if(base==0x4000){regs[0x4014]=regs[0x4114]=regs[0x4010];}
                if(base==0x5000||base==0x5100){regs[base+0x4c]=regs[base+0x1c];regs[base+0x50]=regs[0x4014]/qMax(1u,regs[base+0x1c]);}
                if(base==0x2000||base==0x2100)regs[base+0x2c]=regs[base+0x28];
                if(base==0x3000||base==0x3100)regs[base+0x3c]=16666666;
            } else if(command.message==0xb){
                if(command.address==17&&confirmTemperature){++regs[0x6648];regs[0x664c]=0x10000;const auto raw=quint16(qint32(command.value)/100000);regs[0x6654]=(quint32(raw)<<16)|raw;regs[0x6658]=quint32(raw)<<16;}
                else if(command.address==1){const auto base=0x6000+(command.source-0x40)*0x100;regs[base+4]=command.value;}
            }
        }
        const auto wire=FpgaVlp1::buildFrame(FpgaVlp1::FrameType::Response,f.header.sequence,f.header.source,f.header.message,response);
        incoming.append(reinterpret_cast<const char*>(wire.data()),wire.size());
        if(command.message==0xb && command.address==17) incoming.append(sensorWire);
        return bytes.size();
    }
    void clear(){commands.clear();}
    QVector<Command> writes() const {QVector<Command> out;for(const auto &c:commands)if(c.message==3)out.append(c);return out;}
};
}
