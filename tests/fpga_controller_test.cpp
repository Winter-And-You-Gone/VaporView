#include "FpgaDeviceController.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPointer>
#include <QThread>
#include <QtEndian>
#include <cstdio>
#include <cstdlib>
#include <functional>
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
    bool opened=false,closed=false,strict=true;
    bool holdResponses=false;
    quint32 failWriteAddress=0xffffffff;
    bool failTemperature=false,confirmTemperature=true;
    QMap<quint32,quint32> regs;
    QMap<quint32,int> pendingReads;
    QVector<Command> commands;
    QByteArray incoming;
    ScriptedTransport(){
        regs={{0x4000,0x00200101},{0x4100,0x00210101},{0x4018,0x00011800},{0x4118,0x00011800},
            {0x5000,0x00300101},{0x5100,0x00310101},{0x5024,0x00040101},{0x5124,0x00040101},{0x6600,0x00460200},
            {0x4010,1000000},{0x4014,1000000},{0x4114,1000000},{0x504c,10000},{0x514c,10000},
            {0x5050,100},{0x5150,100},{0x6614,1},{0x661c,1},{0x662c,1000}};
        regs[0x8004]=1;regs[0x8014]=1;regs[0x7004]=1;regs[0x2c]=regs[0x30]=0xffffffff;
        regs[0x701c]=9;regs[0x802c]=7;regs[0x8034]=3;
        for(quint32 b:{0x2000u,0x2100u,0x3000u,0x3100u,0x4000u,0x4100u,0x5000u,0x5100u})regs[b+8]=2;
    }
    bool open(const QString&) override {opened=true;closed=false;return true;}
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
        incoming.append(reinterpret_cast<const char*>(wire.data()),wire.size());return bytes.size();
    }
    void clear(){commands.clear();}
    QVector<Command> writes() const {QVector<Command> out;for(const auto &c:commands)if(c.message==3)out.append(c);return out;}
};
struct Fixture {
    ScriptedTransport *transport;
    FpgaDeviceController controller;
    QStringList diagnostics;
    Fixture():transport(new ScriptedTransport),controller(std::unique_ptr<FpgaUsbTransport>(transport)){
        QObject::connect(&controller,&FpgaDeviceController::logRecordGenerated,&controller,[this](const LogRecord &r){diagnostics.append(r.message);});
    }
    void connect(){controller.connectDevice({},"auto");waitUntil([this]{return !controller.busy();},"connection completed");require(controller.ready(),"strict register handshake succeeds");}
    void finish(){waitUntil([this]{return !controller.busy();},"operation completes");}
};
bool inPage(quint32 a,quint32 base){return a>=base&&a<base+0x200;}
int indexOfWrite(const QVector<Command>& commands,quint32 address,quint32 value){for(int i=0;i<commands.size();++i)if(commands[i].message==3&&commands[i].address==address&&commands[i].value==value)return i;return -1;}
void connectionAndVersions(){
    Fixture f;f.transport->regs[0x50]=3;f.transport->regs[0x403c]=f.transport->regs[0x413c]=1;f.connect();for(const auto &c:f.transport->commands)require(c.message==1||c.message==2||c.message==0xfe,"connection only PING, capabilities and legal reads; no output side effects");
    require(f.controller.hardwareValues().value(0x50)==3&&f.controller.hardwareValues().value(0x701c)==9&&f.controller.hardwareValues().value(0x802c)==7&&f.controller.hardwareValues().value(0x8034)==3,"connection snapshots existing RAW mask, drop, USB queue and stall evidence");
    require(f.transport->regs.value(0x50)==3&&f.transport->regs.value(0x403c)==1,"connection never disables preexisting RAW streaming");
    for(quint32 address:{0x2cu,0x30u,0x34u,0x7004u,0x8004u,0x7020u,0x7024u,0x7028u,0x702cu,0x6104u,0x6704u,0x1004u})require(f.controller.hardwareValues().contains(address),"connection reads all documented upload controls and diagnostic state without starting modules");
    f.controller.disconnectDevice();require(f.transport->closed&&!f.controller.ready(),"disconnect closes unique transport");
    Fixture wrong;wrong.transport->regs[0x5024]=0x00020101;wrong.controller.connectDevice({},"auto");wrong.finish();require(!wrong.controller.ready(),"wrong filter version forbids readiness");
    wrong.controller.setDac(0,true);wrong.controller.applyConfiguration({});require(wrong.transport->writes().isEmpty(),"version mismatch forbids all writes");
    Fixture disabled;disabled.transport->regs[0x8004]=0;disabled.controller.connectDevice({},"auto");disabled.finish();
    require(!disabled.controller.ready()&&disabled.transport->writes().isEmpty(),"disabled USB transmitter fails ready condition without blind recovery writes");
}
void independentAndAcquisition(){
    Fixture f;f.connect();f.transport->clear();f.controller.setWaveform(0,true);f.finish();
    for(const auto &c:f.transport->writes())require(c.address==4||inPage(c.address,0x2000),"independent WMS never writes DAC/ADC/DLIA");
    f.transport->clear();f.controller.setDac(1,true);f.finish();for(const auto &c:f.transport->writes())require(c.address==4||inPage(c.address,0x3000),"independent DAC never changes upload controls or other modules");
    f.transport->clear();f.controller.setAcquisition(true);f.finish();const auto writes=f.transport->writes();
    const int w0=indexOfWrite(writes,0x2004,1),w1=indexOfWrite(writes,0x2104,1),d0=indexOfWrite(writes,0x5004,1),d1=indexOfWrite(writes,0x5104,1),a0=indexOfWrite(writes,0x4004,1),a1=indexOfWrite(writes,0x4104,1);
    require(w0>=0&&w1>=0&&d0>w1&&d1>d0&&a0>d1&&a1>a0,"acquisition starts WMS labels before DLIA before both ADCs");
    for(const auto &c:writes)require(!inPage(c.address,0x3000),"acquisition never starts DAC");
    require(indexOfWrite(writes,0x2c,0xffffffff)>=0&&indexOfWrite(writes,0x30,0xffffffff)>=0&&indexOfWrite(writes,0x7004,1)>=0&&indexOfWrite(writes,0x8004,1)>=0,"acquisition restores complete stream masks and enables STREAM/USB without reset pulses");
    f.transport->clear();f.controller.setAcquisition(false);f.finish();const auto stop=f.transport->commands;
    const int sa0=indexOfWrite(stop,0x4004,0),sa1=indexOfWrite(stop,0x4104,0),sd0=indexOfWrite(stop,0x5004,0);
    require(sa0>=0&&sa1>sa0&&sd0>sa1,"stop disables both ADCs before DLIA");
    bool fifoRead=false;for(int i=sa1+1;i<sd0;++i)if(stop[i].message==2&&stop[i].address==0x4020)fifoRead=true;
    require(fifoRead,"stop observes drained ADC FIFO before disabling DLIA");
    for(const auto &c:f.transport->writes())require(!inPage(c.address,0x2000)&&!inPage(c.address,0x3000)&&c.address!=4,"stop preserves WMS, DAC and global enable");
}
void configurationAndFailure(){
    Fixture f;f.connect();FpgaControlConfig config;config.adcRateHz=500000;f.transport->clear();f.controller.applyConfiguration(config);f.finish();
    int rateWrites=0;for(const auto &c:f.transport->writes()){if(c.address==0x4010){++rateWrites;require(c.value==500000,"shared ADC requested rate");}require(c.address!=0x4110&&c.address!=0x4130&&c.address!=0x4134,"ADC1 common PHY registers remain read only");}
    require(rateWrites==1&&f.controller.hardwareValues().value(0x4114)==500000,"only ADC0 programs shared rate and both actual rates read back");
    int pendingPolls=0;bool shadowRead=false;for(const auto &c:f.transport->commands){if(c.message==2&&c.address==0x2008&&c.count==1)++pendingPolls;if(c.message==2&&c.address==0x2010)shadowRead=true;}
    require(pendingPolls>=3&&shadowRead,"configuration checks shadow and waits pending to clear");
    f.transport->clear();f.transport->failWriteAddress=0x2010;f.controller.applyConfiguration(config);f.finish();
    require(!f.diagnostics.isEmpty(),"failed write reports diagnostic");
    bool failed=false;for(const auto &c:f.transport->commands){if(failed)require(c.message==2,"failed operation cannot continue downstream side effects");if(c.message==3&&c.address==0x2010)failed=true;}require(failed,"failure injected at WMS shadow");
}
void rawAndBusy(){
    Fixture f;f.connect();FpgaControlConfig config;config.rawWindowSeconds=1;f.controller.setConfiguration(config);f.transport->clear();
    f.transport->holdResponses=true;f.controller.setRawEnabled(true);const auto commands=f.transport->commands.size();f.controller.setWaveform(1,true);require(f.transport->commands.size()==commands,"busy controller rejects overlapping operation");
    f.controller.setTemperature(std::numeric_limits<double>::quiet_NaN());
    require(f.controller.busy()&&f.transport->commands.size()==commands,"invalid overlapping request must not abort in-flight RAW operation");
    f.transport->holdResponses=false;f.finish();for(const auto &c:f.transport->writes())require(c.address==0x50||c.address==0x403c||c.address==0x413c,"RAW streaming is not ADC acquisition enable");
    bool maskVerified=false;for(const auto &c:f.transport->commands)if(c.message==2&&c.address==0x50&&c.count==1)maskVerified=true;require(maskVerified,"RAW high mask write is independently verified before proceeding");
    waitUntil([&]{return f.transport->regs.value(0x50)==0&&!f.controller.busy();},"RAW bounded window automatically disables",4000);
    require(f.transport->regs.value(0x403c)==0&&f.transport->regs.value(0x413c)==0,"RAW stop clears both enables");
}
void sensorUploadAndFailure(){
    Fixture f;f.transport->regs[0x2c]=0x7d;f.transport->regs[0x30]=0;f.transport->regs[0x7004]=0;f.transport->regs[0x50]=3;f.connect();
    f.transport->clear();f.controller.setSensorEnabled(0x40,true);f.finish();
    require(f.transport->regs.value(0x2c)==0xffffffff&&f.transport->regs.value(0x30)==0xffffffff&&f.transport->regs.value(0x7004)==1&&f.transport->regs.value(0x8004)==1,"sensor enable restores management/time upload bits and STREAM/USB path");
    require(f.transport->regs.value(0x50)==3,"sensor enable does not turn off existing RAW window");
    for(const auto &c:f.transport->writes())require(c.address!=0x6104&&c.address!=0x6704&&c.address!=0x1004&&c.address!=0x50,"upload preparation never starts HMP, motor, time or modifies RAW mask");
    f.transport->clear();f.transport->failWriteAddress=0x2c;f.controller.setAcquisition(true);f.finish();
    require(f.transport->commands.size()==1&&f.transport->commands.front().address==0x2c,"upload mask failure aborts before acquisition side effects");
    f.transport->failWriteAddress=0xffffffff;f.transport->regs[0x8004]=0;f.controller.refresh();f.finish();f.transport->clear();f.controller.setAcquisition(true);
    require(!f.controller.ready()&&f.transport->commands.isEmpty(),"observed disabled USB path removes readiness and forbids blind write recovery");
}
void temperature(){
    Fixture f;f.connect();f.transport->clear();f.controller.setTemperature(40.0);f.finish();
    require(f.diagnostics.isEmpty(),"verified AI8 setpoint completes without diagnostic failure");
    require((f.transport->regs.value(0x6658)>>16)==400,"AI8 actual setpoint confirmed by readback");
    bool result=false,actual=false;for(const auto &c:f.transport->commands){result|=c.message==2&&c.address==0x6648;actual|=c.message==2&&c.address==0x6654;}
    require(result&&actual,"AI8 reads result and actual setpoint after action");
    f.transport->clear();f.transport->failTemperature=true;f.controller.setTemperature(41.0);f.finish();
    require(f.transport->commands.size()==2&&f.transport->commands.last().message==0xb,"failed AI8 action stops after baseline read and action");
    f.transport->failTemperature=false;f.transport->confirmTemperature=false;f.transport->clear();f.diagnostics.clear();f.controller.setTemperature(42.0);f.finish();
    require(!f.diagnostics.isEmpty(),"AI8 ACK without updated confirmation count or equal readback fails");
    f.transport->clear();f.controller.setTemperature(2200.0);f.finish();require(f.transport->commands.isEmpty(),"AI8 microdegree command rejects values beyond signed I32");
}
void ai8DisabledConfiguration(){
    Fixture f;f.transport->regs[0x6660]=200;f.transport->regs[0x6664]=1;f.connect();
    FpgaControlConfig config;config.ai8.channel=2;config.ai8.slaveAddress=7;config.ai8.timeoutMs=250;config.ai8.retryLimit=2;
    f.transport->clear();f.controller.applyConfiguration(config);f.finish();
    require(f.transport->regs.value(0x6614)==7&&f.transport->regs.value(0x661c)==2&&f.transport->regs.value(0x6660)==250&&f.transport->regs.value(0x6664)==2,"disabled AI8 applies verified address, channel, timeout and retries");
    f.controller.setSensorEnabled(0x46,true);f.finish();
    config.ai8.channel=3;f.transport->clear();f.controller.applyConfiguration(config);f.finish();
    require(f.transport->writes().isEmpty(),"enabled AI8 configuration changes rejected before any unrelated side effects");
}
void threadCleanup(){
    QThread worker;auto *transport=new ScriptedTransport;QPointer<FpgaDeviceController> controller=new FpgaDeviceController(std::unique_ptr<FpgaUsbTransport>(transport));
    controller->moveToThread(&worker);QObject::connect(&worker,&QThread::finished,controller,&QObject::deleteLater);worker.start();
    QMetaObject::invokeMethod(controller,[controller]{controller->connectDevice({},"auto");},Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(controller,[controller]{controller->disconnectDevice();},Qt::BlockingQueuedConnection);
    worker.quit();require(worker.wait(3000),"USB worker thread stops promptly");require(controller.isNull(),"controller, session, transport and timers destroyed on owning thread");
}
}
int main(int argc,char **argv){QCoreApplication app(argc,argv);connectionAndVersions();independentAndAcquisition();configurationAndFailure();rawAndBusy();temperature();sensorUploadAndFailure();ai8DisabledConfiguration();threadCleanup();std::puts("FPGA controller tests passed");return 0;}
