#include "FpgaDeviceController.h"
#include "ppk/ObservationStore.h"
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
    if(base==0x6000)return offset<=0x38;
    if(base==0x6200||base==0x6400)return offset<=0x3c;
    if(base==0x6300)return offset<=0x40;
    if(base==0x6500)return offset<=0x5c;
    if(base==0x6600)return offset<=0x64;
    if(base==0x6100)return offset<=0x48;
    if(base==0x6700)return offset<=0x34;
    if(base==0x7000)return offset<=12||offset==0x10||offset==0x14||offset==0x18||offset==0x34||(offset>=0x1c&&offset<=0x30)||(offset>=0x38&&offset<=0x44);
    if(base==0x8000)return offset<=0x3c;
    return false;
}
class ScriptedTransport final : public FpgaUsbTransport {
public:
    bool opened=false,closed=false,strict=true;
    bool holdResponses=false;
    quint64 responseTick=0;
    quint32 failWriteAddress=0xffffffff;
    quint32 failWriteValue=0xffffffff;
    bool failTemperature=false,confirmTemperature=true;
    quint32 ignoreWriteAddress=0xffffffff;
    QMap<quint32,quint32> regs;
    QMap<quint32,int> pendingReads;
    QVector<Command> commands;
    QByteArray incoming;
    ScriptedTransport(){
        regs={{0x4000,0x00200101},{0x4100,0x00210101},{0x4018,0x00011800},{0x4118,0x00011800},
            {0x5000,0x00300101},{0x5100,0x00310101},{0x5024,0x00040101},{0x5124,0x00040101},{0x6600,0x00460200},
            {0x4010,1000000},{0x4014,1000000},{0x4114,1000000},{0x504c,10000},{0x514c,10000},
            {0x5050,100},{0x5150,100},{0x6614,1},{0x661c,1},{0x662c,1000},
            {0x6010,4800},{0x6018,500},{0x6210,460800},{0x6510,115200},{0x6314,250},{0x6414,2000}};
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
        if(command.message==3&&command.address==failWriteAddress&&(failWriteValue==0xffffffff||command.value==failWriteValue))status=7;
        if(command.message==3 && (command.address==0x6010 || command.address==0x6210 || command.address==0x6414 || command.address==0x662c)
            && (regs.value((command.address&~255u)+4)&1))status=7;
        if(command.message==0xb&&command.address==17&&failTemperature)status=14;
        std::vector<std::uint8_t> response;append32(response,status);
        if(!status){
            if(command.message==0xfe)response.insert(response.end(),f.payload.begin(),f.payload.end());
            else if(command.message==1){for(quint32 v:{0xffffffffu,0x100u,100000000u,8192u,4096u,0u})append32(response,v);}
            else if(command.message==2){
                append32(response,command.address);append16(response,command.count);append16(response,0);
                for(int i=0;i<command.count;++i){const auto a=command.address+4*i;quint32 v=regs.value(a);if(pendingReads.value(a)>0){--pendingReads[a];v|=4;}append32(response,v);}
            } else if(command.message==3){
                for(int i=0;i<command.count;++i){const auto a=command.address+4*i;if(a==ignoreWriteAddress)continue;const auto value=word(f.payload,8+i*4);regs[a]=(a>=0x6000&&a<=0x660c&&(a&255)==12)?regs.value(a)&~value:value;
                    if(a==0x50&&value==0)regs[0x7020]=regs[0x7024]=0;
                    if((a&255)==4&&a>=0x2000&&a<=0x5104){const auto b=a&~255u;regs[b+8]=(value&1)|2;if(b==0x4000||b==0x4100)regs[b+8]|=0x10;
                        if(value&2){regs[b+0xc]=0;regs[b+4]=value&1;}
                    }
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
                else if(command.address==16&&command.source==0x41){regs[0x6140]=command.value;++regs[0x6148];}
                else if(command.address==1){const auto base=0x6000+(command.source-0x40)*0x100;regs[base+4]=command.value;regs[base+8]=(regs.value(base+8)&~9u)|(command.value&1);if(base==0x6000&&!command.value)regs[base+8]|=8;if(base==0x6500)pendingReads[base+8]=2;}
            }
        }
        const auto wire=FpgaVlp1::buildFrame(FpgaVlp1::FrameType::Response,f.header.sequence,f.header.source,f.header.message,response,true,3,responseTick);
        incoming.append(reinterpret_cast<const char*>(wire.data()),wire.size());return bytes.size();
    }
    void clear(){commands.clear();}
    QVector<Command> writes() const {QVector<Command> out;for(const auto &c:commands)if(c.message==3)out.append(c);return out;}
};
struct Fixture {
    ScriptedTransport *transport;
    FpgaDeviceController controller;
    QStringList diagnostics;
    QVector<LogRecord> records;
    Fixture():transport(new ScriptedTransport),controller(std::unique_ptr<FpgaUsbTransport>(transport)){
        QObject::connect(&controller,&FpgaDeviceController::logRecordGenerated,&controller,[this](const LogRecord &r){
            if(r.fields.value("event")=="fpga_operation_diagnostic")diagnostics.append(r.message);
            records.append(r);
        });
    }
    void connect(){controller.connectDevice({},"auto");waitUntil([this]{return !controller.busy();},"connection completed");require(controller.ready(),"strict register handshake succeeds");}
    void finish(){waitUntil([this]{return !controller.busy();},"operation completes");}
};
bool inPage(quint32 a,quint32 base){return a>=base&&a<base+0x200;}
void connectionEventTimeFilter(){
    Fixture f;
    const auto event=[](quint16 message,quint64 tick,quint32 count=4) {
        std::vector<std::uint8_t> payload;
        const bool status=message==0x1400;
        for(quint32 value:std::vector<quint32>{1,status?0u:0x66u,status?0u:0x44u,status?0u:4u,0,status?count:0u,0,0})append32(payload,value);
        const auto wire=FpgaVlp1::buildFrame(FpgaVlp1::FrameType(status?0x12:0x11),900,status?1:0x46,message,payload,true,3,tick);
        return QByteArray(reinterpret_cast<const char*>(wire.data()),wire.size());
    };
    const auto count=[&] {int n=0;for(const auto& r:f.records)if(r.fields.value("event")=="fpga_firmware_event")++n;return n;};
    constexpr quint64 currentTick=1000000000000ULL;
    f.transport->responseTick=currentTick;
    f.transport->holdResponses=true;
    f.controller.connectDevice({},"auto");
    // Backlog and a new handshake-time fault both arrive before the PING reply.
    f.transport->incoming.prepend(event(0x1402,1)+event(0x1402,currentTick));
    f.transport->holdResponses=false;f.finish();
    require(f.controller.ready() && count()==1,"old backlog filtered while new handshake error survives");
    f.transport->incoming.append(event(0x1402,2)+event(0x1400,currentTick,100)+event(0x1402,currentTick+1));
    waitUntil([&]{return count()>=2;},"current error after handshake remains visible");
    require(count()==2,"late old event and first historical drop total produce no warnings");
    f.transport->incoming.append(event(0x1400,currentTick+2,101));
    waitUntil([&]{return count()>=3;},"new drop increment produces warning");
    require(count()==3,"one warning for one new drop increment");
    f.controller.disconnectDevice();
    f.transport->responseTick=0;f.connect();
    f.transport->incoming.append(event(0x1400,5,200)+event(0x1402,6));
    waitUntil([&]{return count()>=4;},"reconnect accepts new device clock epoch");
    require(count()==4,"reconnect resets cumulative drop baseline");
}
void advancedDataPaths(){
    Fixture f;f.connect();f.transport->clear();
    f.controller.connectSensor({{"op","sensor"},{"source",0x41},{"enabled",true},{"poll_ms",500},{"parameters",QJsonObject{{"address",241}}}});f.finish();
    require(f.transport->regs.value(0x6114)==241 && f.transport->regs.value(0x611c)==500 && (f.transport->regs.value(0x6104)&1),"HMP advanced parameters applied before enable");
    f.transport->regs[0x6140]=0x447d5000;
    f.controller.executeOperation({{"op","sensor_action"},{"source",0x41},{"action",16},{"value",double(0x447d5000)}});f.finish();
    require(f.transport->regs.value(0x6148)==1,"HMP compensation confirms a new write even when its value is unchanged");
    FpgaControlConfig config;config.dlia[0].mode=6;config.dlia[1].mode=7;config.adcCnvTicks=3;config.adcBusyTimeoutTicks=200;config.adcExpectedPoints[1]=10000;config.dlia[1].fragmentPoints=128;
    f.controller.applyConfiguration(config,"acquisition");f.finish();require(f.transport->regs.value(0x4030)==3 && f.transport->regs.value(0x4034)==200 && f.transport->regs.value(0x411c)==10000,"ADC timing and cycle checks written");
    f.controller.applyConfiguration(config,"dlia");f.finish();require(f.transport->regs.value(0x5020)==6 && f.transport->regs.value(0x5120)==7 && f.transport->regs.value(0x5140)==128,"LPF bypass and fragment size applied");
    int epochs=0;Ppk::RawSatelliteEpoch decoded;
    QObject::connect(&f.controller,&FpgaDeviceController::observationEpochReceived,&f.controller,[&](const QByteArray& bytes){require(Ppk::decodeEpoch(bytes,decoded),"observation epoch encoding valid");++epochs;});
    auto inject=[&](quint8 type,quint16 source,quint16 message,const std::vector<std::uint8_t>& payload) {const auto wire=FpgaVlp1::buildFrame(FpgaVlp1::FrameType(type),100+epochs,source,message,payload,true,3,123);f.transport->incoming.append(reinterpret_cast<const char*>(wire.data()),wire.size());};
    Ppk::RawSatellitePacket packet;packet.epoch.unixSeconds=1700000000;packet.epoch.nanoseconds=100;packet.epoch.receiver=1;packet.totalPackets=2;packet.packetNumber=0;
    Ppk::RawSatelliteObservation observation;observation.system=1;observation.prn=7;observation.frequency=0;observation.pseudoRangeM=23456789;packet.epoch.observations.push_back(observation);
    inject(0x10,0x42,0x1200,Ppk::encodeRawSatelliteFrame(packet,1));packet.packetNumber=1;packet.epoch.observations[0].prn=8;inject(0x10,0x42,0x1200,Ppk::encodeRawSatelliteFrame(packet,2));
    waitUntil([&]{return epochs==1;},"FPGA raw observations assembled independently of presentation");require(decoded.observations.size()==2,"both raw satellite packets preserved");
    inject(0x10,0x42,0x1200,Ppk::encodeRawSatelliteFrame(packet,3));
    waitUntil([&]{for(const auto& record:f.records)if(record.fields.value("observation_event")=="epsilon_raw_satellite_packet_duplicate")return true;return false;},"reconnect retains observation diagnostics and rejects duplicate epochs");
    std::vector<std::uint8_t> event;for(quint32 word:{1u,0x60u,2u,2u,0u,0u,0u,0u})append32(event,word);inject(0x11,0x40,0x1402,event);
    waitUntil([&]{for(const auto& record:f.records)if(record.fields.value("event")=="fpga_firmware_event")return true;return false;},"firmware error event produces a visible log");
    f.controller.executeOperation({{"op","time_tag"},{"low",4294967295.0},{"high",123}});f.finish();
    require(f.transport->regs.value(0x1038)==0xffffffff && f.transport->regs.value(0x103c)==123,"GNSS tag writes both halves without writing read-only tag-valid");
    f.controller.executeOperation({{"op","motor_config"},{"frequency",1000},{"high_ticks",255},{"dir_ticks",505},{"polarity",3}});f.finish();
    require(f.transport->regs.value(0x6704)==0 && f.transport->regs.value(0x6714)==1000 && f.transport->regs.value(0x6730)==3,"motor configuration confirms parameters while leaving motor disabled");
    f.controller.executeOperation({{"op","stream_config"},{"mode",1},{"high_water",2048},{"burst",16}});f.finish();
    require(f.transport->regs.value(0x7010)==1 && f.transport->regs.value(0x7018)==2048 && f.transport->regs.value(0x7034)==16,"upload tuning has verified hardware readbacks");
    f.controller.executeOperation({{"op","watchdog"},{"value",30000}});f.finish();f.transport->clear();
    waitUntil([&]{return !f.transport->writes().isEmpty();},"confirmed watchdog feeds periodically",2000);
    require(f.transport->writes().first().address==0x44,"watchdog heartbeat targets the write-only feed register");
    f.controller.executeOperation({{"op","watchdog"},{"value",0}});f.finish();
    Fixture rejected;rejected.connect();rejected.transport->failWriteAddress=0x40;
    rejected.controller.executeOperation({{"op","watchdog"},{"value",30000}});rejected.finish();
    QElapsedTimer elapsed;elapsed.start();while(elapsed.elapsed()<1150){QCoreApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}
    for(const auto& command:rejected.transport->writes())require(command.address!=0x44,"failed watchdog setup never starts feeding");
}
void connectionResultLogs(){
    const auto count=[](const Fixture& f,quint16 source,const QString& result) {
        int n=0;for(const auto& r:f.records)if(r.fields.value("event")=="fpga_device_connection_result"
            &&r.fields.value("fpga_source").toUInt()==source&&r.fields.value("result")==result)++n;return n;
    };
    Fixture f;
    for(quint16 source:{0x40,0x45,0x46})f.transport->regs[0x6004+(source-0x40)*0x100]=1;
    f.connect();
    for(quint16 source:{0x40,0x45,0x46})require(count(f,source,"waiting")==1,"each recovered sensor logs confirmed enable separately");
    require(count(f,0x45,"succeeded")==0,"CONTROL readback alone never reports sensor connection success");
    auto *session=f.controller.findChild<FpgaDeviceSession*>();
    FpgaSensor::Reading ai8;ai8.source=0x46;ai8.kind=FpgaSensor::SensorKind::Ai8;ai8.schema=2;
    ai8.validity.structure=ai8.validity.crc=ai8.validity.deviceOnline=ai8.validity.measurement=true;
    ai8.ai8DeviceStatus=3;ai8.ai8DeviceError=0;ai8.ai8PvRaw=250;ai8.ai8SpRaw=250;ai8.ai8SvRaw=250;
    ai8.ai8OpRaw=0;ai8.ai8Alarm=0;ai8.ai8Control=0;ai8.ai8Host=0;ai8.ai8SetResult=0;ai8.ai8SampleCounter=1;
    emit session->sensorReadingReceived(ai8);
    const auto feed=[&](quint32 flags) {
        const auto payload=QByteArray::fromHex("0100010013000304c0d40100");
        const auto frame=FpgaVlp1::buildFrame(FpgaVlp1::FrameType::Data,1,0x45,0x1100,
            std::vector<std::uint8_t>(payload.begin(),payload.end()),true,flags,100000000);
        session->ingestReplayBytes(QByteArray(reinterpret_cast<const char*>(frame.data()),frame.size()));
    };
    feed(1u<<4);QCoreApplication::processEvents();
    require(count(f,0x45,"succeeded")==0,"invalid sensor data cannot produce success");
    feed(0);waitUntil([&]{return count(f,0x45,"succeeded")==1;},"first valid sample reports connection success");
    feed(0);waitUntil([&]{return count(f,0x40,"failed")==1&&count(f,0x46,"failed")==1;},"missing data reports per-device failure",11000);
    require(count(f,0x45,"succeeded")==1&&count(f,0x45,"failed")==0,"continuous samples never spam connection logs");
    f.controller.refresh();f.finish();
    require(count(f,0x40,"failed")==1,"periodic or manual reads do not repeat failed results");
    Fixture failed;
    for(quint16 source:{0x40,0x42,0x43,0x44,0x45,0x46})failed.transport->regs[0x6004+(source-0x40)*0x100]=1;
    failed.transport->failWriteAddress=0x4004;failed.transport->failWriteValue=0;
    failed.controller.connectDevice({},"auto");failed.finish();
    for(quint16 source:{0x40,0x42,0x43,0x44,0x45,0x46}) {
        require(count(failed,source,"failed")==1&&count(failed,source,"succeeded")==0,"aborted recovery logs every affected device and no false success");
    }
    Fixture cancelled;cancelled.connect();cancelled.controller.connectSensor({{"op","sensor"},{"source",0x45},{"enabled",true}});cancelled.finish();
    cancelled.controller.disconnectDevice();
    require(count(cancelled,0x45,"succeeded")==0&&count(cancelled,0x45,"failed")==0,"manual cancellation clears pending connection evidence");
}
int indexOfWrite(const QVector<Command>& commands,quint32 address,quint32 value){for(int i=0;i<commands.size();++i)if(commands[i].message==3&&commands[i].address==address&&commands[i].value==value)return i;return -1;}
void sensorRates(){
    Fixture f;f.connect();
    double rate=-1;int presentations=0;
    QObject::connect(&f.controller,&FpgaDeviceController::measurementUpdated,[&](const FpgaSensor::AdaptedMeasurements& m) {
        if(m.reading.source==0x45){rate=m.sampleRateHz;++presentations;}
    });
    QByteArray burst;
    const auto payload=QByteArray::fromHex("0100010013000304c0d40100");
    for(int i=0;i<=1000;++i){
        const auto frame=FpgaVlp1::buildFrame(FpgaVlp1::FrameType::Data,i+1,0x45,0x1100,
            std::vector<std::uint8_t>(payload.begin(),payload.end()),true,1,quint64(i)*100000);
        burst.append(reinterpret_cast<const char*>(frame.data()),frame.size());
    }
    f.controller.findChild<FpgaDeviceSession*>()->ingestReplayBytes(burst);
    waitUntil([&]{return presentations>0;},"coalesced sensor presentation arrives");
    require(rate==1000.0&&presentations==1,"one presentation retains the rate of all 1001 raw sensor frames");
    waitUntil([&]{return rate==0.0;},"stalled sensor data clears the frequency",6500);
    f.controller.disconnectDevice();
}
void connectionAndVersions(){
    Fixture f;f.transport->regs[0x50]=3;f.transport->regs[0x403c]=f.transport->regs[0x413c]=1;f.connect();for(const auto &c:f.transport->writes())require(c.address<0x2000||c.address>=0x4000,"connection recovery never changes WMS/DAC outputs");
    require(f.controller.hardwareValues().value(0x50)==3&&f.controller.hardwareValues().value(0x701c)==9&&f.controller.hardwareValues().value(0x802c)==7&&f.controller.hardwareValues().value(0x8034)==3,"connection snapshots existing RAW mask, drop, USB queue and stall evidence");
    require(f.transport->regs.value(0x50)==3&&f.transport->regs.value(0x403c)==1,"connection never disables preexisting RAW streaming");
    for(quint32 address:{0x2cu,0x30u,0x34u,0x7004u,0x8004u,0x7020u,0x7024u,0x7028u,0x702cu,0x6104u,0x6704u,0x1004u})require(f.controller.hardwareValues().contains(address),"connection reads all documented upload controls and diagnostic state without starting modules");
    for(quint32 address:{0x6010u,0x6018u,0x6210u,0x6510u,0x6314u,0x6414u})
        require(f.controller.hardwareValues().contains(address)&&f.controller.hardwareValues().value(address)==f.transport->regs.value(address),
            "connection reads actual sensor baud and polling values, including non-default settings");
    f.controller.disconnectDevice();require(f.transport->closed&&!f.controller.ready(),"disconnect closes unique transport");
    Fixture wrong;wrong.transport->regs[0x5024]=0x00020101;wrong.controller.connectDevice({},"auto");wrong.finish();require(!wrong.controller.ready(),"wrong filter version forbids readiness");
    wrong.controller.setDac(0,true);wrong.controller.applyConfiguration({});require(wrong.transport->writes().isEmpty(),"version mismatch forbids all writes");
    Fixture disabled;disabled.transport->regs[0x8004]=0;disabled.controller.connectDevice({},"auto");disabled.finish();
    require(!disabled.controller.ready()&&disabled.transport->writes().isEmpty(),"disabled USB transmitter fails ready condition without blind recovery writes");
}
void connectionRecovery(){
    Fixture f;
    const QMap<quint32,quint32> saved{{0x2004,1},{0x2104,1},{0x3004,1},{0x3104,0},
        {0x4004,1},{0x4104,0},{0x5004,1},{0x5104,0},{0x403c,1},{0x413c,0},{0x50,1},
        {0x6004,1},{0x6204,0},{0x6304,1},{0x6404,1},{0x6504,1},{0x6604,0},
        {0x501c,20000},{0x511c,10000}};
    for(auto it=saved.cbegin();it!=saved.cend();++it)f.transport->regs[it.key()]=it.value();
    f.transport->regs[0x600c]=5;f.transport->regs[0x630c]=7;f.transport->regs[0x500c]=0x84;
    bool evidence=false;
    QObject::connect(&f.controller,&FpgaDeviceController::snapshot,[&](quint64,const QJsonObject &o){
        evidence|=o.value("hardware_registers").toObject().value("0x0000600c").toInt()==5;
    });
    f.connect();require(evidence,"connection preserves pre-recovery error evidence");
    for(auto it=saved.cbegin();it!=saved.cend();++it)require(f.transport->regs.value(it.key())==it.value(),"recovery preserves device parameters, outputs, RAW and asymmetric enable states");
    require(f.transport->regs.value(0x600c)==0&&f.transport->regs.value(0x630c)==0&&f.transport->regs.value(0x500c)==0,"connection clears sensor history and DLIA partial errors");
    require(indexOfWrite(f.transport->commands,0x4004,2)>indexOfWrite(f.transport->commands,0x5104,0),"connection drains both channels before shared PHY reset");
    int ptbStops=0,ptbStarts=0;
    for(const auto &c:f.transport->commands)if(c.message==0xb){
        require(c.source==0x40||c.source==0x43||c.source==0x44||c.source==0x45,"disabled sensors are never started");
        if(c.source==0x40){ptbStops+=c.value==0;ptbStarts+=c.value==1;}
    }
    bool tfaStopped=false;int tfaTransitionReads=0;
    for(const auto &c:f.transport->commands){
        if(c.message==0xb&&c.source==0x45&&c.value==0){tfaStopped=true;tfaTransitionReads=0;}
        if(tfaStopped&&c.message==2&&c.address==0x6508)++tfaTransitionReads;
        if(c.message==0xb&&c.source==0x45&&c.value==1)
            require(tfaStopped&&tfaTransitionReads>=3,"TFA restart waits until its pending serial transition finishes");
    }
    require(ptbStops==1&&ptbStarts==1,"enabled PTB restarts exactly once per connection");
    f.transport->clear();f.controller.refresh();f.finish();require(f.transport->writes().isEmpty(),"manual refresh never repeats connection reset");
    Fixture failed;failed.transport->failWriteAddress=0x4004;failed.controller.connectDevice({},"auto");failed.finish();
    require(!failed.controller.ready(),"recovery failure cannot report ready");
    failed.controller.refresh();failed.finish();require(!failed.controller.ready(),"readback cannot bypass failed connection recovery");
    for(const auto &c:failed.transport->commands)require(c.message!=0xb,"recovery failure aborts before restarting sensors");
}
void backgroundRefresh(){
    Fixture f;f.connect();
    QTimer *statusTimer=nullptr;
    for(auto *timer:f.controller.findChildren<QTimer*>())
        if(timer->interval()==2000)statusTimer=timer;
    require(statusTimer,"periodic status timer exists");
    int stateChanges=0,readbacks=0;
    bool uiBusy=false;
    QObject::connect(&f.controller,&FpgaDeviceController::connectionChanged,&f.controller,
        [&](bool,bool busy,const QString&){++stateChanges;uiBusy=busy;});
    QObject::connect(&f.controller,&FpgaDeviceController::hardwareValuesChanged,&f.controller,
        [&](const auto&){++readbacks;});
    for(int cycle=0;cycle<3;++cycle){
        f.transport->regs[0x701c]=20+cycle;
        f.transport->regs[0x6010]=4800+cycle*1200;
        QMetaObject::invokeMethod(statusTimer,"timeout",Qt::DirectConnection);
        waitUntil([&]{return readbacks==cycle+1;},"background read completes");
        require(stateChanges==0&&!uiBusy,"automatic refresh never toggles page busy state or completion text");
        require(readbacks==cycle+1&&f.controller.hardwareValues().value(0x701c)==quint32(20+cycle),"silent refresh still publishes fresh hardware values");
        require(f.controller.hardwareValues().value(0x6010)==quint32(4800+cycle*1200),"periodic refresh reads changed sensor baud rather than cached presets");
    }
    f.transport->clear();f.transport->holdResponses=true;
    QMetaObject::invokeMethod(statusTimer,"timeout",Qt::DirectConnection);
    require(!f.controller.busy()&&!uiBusy&&f.transport->commands.size()==1,"background read leaves local and remote controls available");
    f.controller.setDac(1,true);
    require(uiBusy&&stateChanges==1,"user operation takes priority over background refresh");
    require(f.transport->commands.size()==1,"foreground command waits for in-flight read response");
    f.transport->holdResponses=false;f.finish();
    require(!uiBusy&&stateChanges==2&&f.transport->regs.value(0x3104)==1,"user operation completes rather than being rejected by polling");
    require(f.diagnostics.isEmpty(),"background overlap generates no rejection");
    stateChanges=0;f.controller.refresh();
    require(uiBusy,"manual refresh retains visible progress");f.finish();
    require(stateChanges==2&&!uiBusy,"manual refresh retains completion notification");
    f.transport->regs[0x5024]=0;
    QMetaObject::invokeMethod(statusTimer,"timeout",Qt::DirectConnection);
    waitUntil([&]{return !f.controller.ready();},"background version failure completes");
    require(!f.controller.ready()&&stateChanges==3&&!f.diagnostics.isEmpty(),"background version failure remains visible and disables writes");
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
void restartClearsDliaState(){
    Fixture f;f.connect();
    f.transport->regs[0x501c]=20000;f.transport->regs[0x511c]=10000;
    for(int attempt=0;attempt<2;++attempt){
        f.transport->regs[0x500c]=f.transport->regs[0x510c]=0x84;
        f.transport->clear();f.controller.setAcquisition(true);f.finish();
        const auto commands=f.transport->commands;
        const int a0=indexOfWrite(commands,0x4004,0),a1=indexOfWrite(commands,0x4104,0);
        const int d1=indexOfWrite(commands,0x5104,0),resetAdc=indexOfWrite(commands,0x4004,2);
        const int resetD0=indexOfWrite(commands,0x5004,2),resetD1=indexOfWrite(commands,0x5104,2);
        const int enable=indexOfWrite(commands,0x5004,1);
        require(a0>=0&&a1>a0&&d1>a1&&resetAdc>d1&&resetD0>resetAdc&&resetD1>resetD0&&enable>resetD1,"restart drains producers before resetting and enabling consumers");
        bool adcCommitted=false,d0Committed=false,d1Committed=false;
        for(int i=resetD1+1;i<enable;++i)if(commands[i].message==5){adcCommitted|=commands[i].source==0x20;d0Committed|=commands[i].source==0x30;d1Committed|=commands[i].source==0x31;}
        require(adcCommitted&&d0Committed&&d1Committed,"restart commits retained ADC and both DLIA settings before enabling");
        require(f.transport->regs[0x500c]==0&&f.transport->regs[0x510c]==0,"restart clears sticky DLIA error state");
        require(f.transport->regs[0x504c]==20000&&f.transport->regs[0x514c]==10000,"restart preserves different per-channel output rates");
        for(const auto &c:f.transport->writes())require(!((c.address>=0x4010&&c.address<=0x403c)||(c.address>=0x4110&&c.address<=0x413c)||(c.address>=0x5010&&c.address<=0x5054)||(c.address>=0x5110&&c.address<=0x5154)),"start never overwrites user sampling or demodulation parameters");
        require(f.diagnostics.isEmpty(),"restart completes without diagnostics");
        f.controller.setAcquisition(false);f.finish();
    }
    f.transport->failWriteAddress=0x5004;f.transport->failWriteValue=2;f.transport->clear();
    f.controller.setAcquisition(true);f.finish();
    require(indexOfWrite(f.transport->commands,0x4004,1)<0&&indexOfWrite(f.transport->commands,0x4104,1)<0,"failed preparation must not enable ADC capture");
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
void scopedConfiguration(){
    for(const QString section:{QString("wms"),QString("dlia"),QString("acquisition")}) {
        Fixture f;f.connect();FpgaControlConfig config;f.transport->clear();
        f.controller.applyConfiguration(config,section);f.finish();
        require(f.diagnostics.isEmpty(),"scoped configuration completes");
        bool selected=false;
        for(const auto &c:f.transport->writes()) {
            const bool wms=c.address>=0x2000&&c.address<0x3200;
            const bool dlia=(c.address>=0x5010&&c.address<=0x5020)||(c.address>=0x5110&&c.address<=0x5120);
            require(section=="wms"||!wms,"other sections never write WMS or DAC");
            require(section=="dlia"||!dlia,"other sections never write DLIA parameters");
            require(section=="acquisition"||c.address!=0x4010,"other sections never write ADC rate");
            selected|=section=="wms"?wms:section=="dlia"?dlia:c.address==0x4010;
        }
        require(selected,"selected section writes parameters");
    }
    Fixture f;f.connect();FpgaControlConfig config;config.adcRateHz=1000;
    f.transport->clear();f.controller.applyConfiguration(config,"dlia");f.finish();
    require(f.diagnostics.isEmpty(),"DLIA validates against device ADC rate, not unrelated edits");
    f.transport->clear();f.controller.applyConfiguration(config,"acquisition");f.finish();
    require(f.transport->writes().isEmpty(),"ADC reduction cannot invalidate existing DLIA output rate");
    f.transport->clear();f.controller.applyConfiguration(config,"invalid");f.finish();
    require(f.transport->writes().isEmpty(),"unknown section cannot write hardware");
}
void rawAndBusy(){
    Fixture f;f.connect();f.controller.setConfiguration(FpgaControlConfig::fromJson({{"rawWindowSeconds",1}}));f.transport->clear();
    f.transport->holdResponses=true;f.controller.setRawEnabled(true);const auto commands=f.transport->commands.size();f.controller.setWaveform(1,true);require(f.transport->commands.size()==commands,"busy controller rejects overlapping operation");
    f.controller.setTemperature(std::numeric_limits<double>::quiet_NaN());
    require(f.controller.busy()&&f.transport->commands.size()==commands,"invalid overlapping request must not abort in-flight RAW operation");
    f.transport->holdResponses=false;f.finish();for(const auto &c:f.transport->writes())require(c.address==0x50||c.address==0x403c||c.address==0x413c,"RAW streaming is not ADC acquisition enable");
    bool maskVerified=false;for(const auto &c:f.transport->commands)if(c.message==2&&c.address==0x50&&c.count==1)maskVerified=true;require(maskVerified,"RAW high mask write is independently verified before proceeding");
    f.transport->clear();QElapsedTimer observation;observation.start();
    waitUntil([&]{return observation.elapsed()>11000;},"observe RAW beyond the former default window",12500);
    require(f.transport->regs.value(0x50)==3&&f.transport->regs.value(0x403c)==1&&f.transport->regs.value(0x413c)==1,
        "RAW remains enabled beyond ten seconds and ignores a saved legacy window");
    require(f.transport->writes().isEmpty(),"elapsed time never sends a RAW stop command");
    f.transport->regs[0x7020]=f.transport->regs[0x7024]=16; // Framer retains the tail until RAW masking finalizes it.
    f.controller.setRawEnabled(false);f.finish();
    require(f.transport->regs.value(0x403c)==0&&f.transport->regs.value(0x413c)==0,"RAW stop clears both enables");
    require(indexOfWrite(f.transport->commands,0x50,0)>indexOfWrite(f.transport->commands,0x413c,0),"stop RAW producers before closing upload gate");
    int drainReads=0;for(const auto &c:f.transport->commands)if(c.message==2&&c.count==1&&(c.address==0x7020||c.address==0x7024))++drainReads;
    require(drainReads>=2,"RAW stop waits for both finalized RAW frame buffers");
}
void sensorUploadAndFailure(){
    Fixture f;f.transport->regs[0x2c]=0x7d;f.transport->regs[0x30]=0;f.transport->regs[0x7004]=0;f.transport->regs[0x50]=3;f.connect();
    f.transport->clear();f.controller.setSensorEnabled(0x40,true);f.finish();
    require(f.transport->regs.value(0x2c)==0xffffffff&&f.transport->regs.value(0x30)==0xffffffff&&f.transport->regs.value(0x7004)==1&&f.transport->regs.value(0x8004)==1,"sensor enable restores management/time upload bits and STREAM/USB path");
    require(f.transport->regs.value(0x50)==3,"sensor enable does not turn off existing RAW upload");
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
void ptbHistoricalErrorRecovery(){
    Fixture f;f.connect();f.transport->regs[0x600c]=5;f.controller.refresh();f.finish();f.transport->clear();
    bool evidence=false;
    QObject::connect(&f.controller,&FpgaDeviceController::snapshot,[&](quint64,const QJsonObject &o){
        if(o.value("hardware_registers").toObject().value("0x0000600c").toInt()==5)evidence=true;
    });
    f.controller.setSensorEnabled(0x40,true);f.finish();
    require(evidence&&f.transport->regs.value(0x600c)==0,"PTB re-enable preserves evidence and acknowledges stale errors");
    require(indexOfWrite(f.transport->commands,0x600c,5)>=0,"PTB clears only observed error bits");
    f.transport->clear();f.controller.setSensorEnabled(0x40,false);f.finish();
    require(indexOfWrite(f.transport->commands,0x600c,5)<0,"disable never clears diagnostic evidence");
    f.transport->regs[0x600c]=1;f.controller.refresh();f.finish();f.transport->clear();f.transport->failWriteAddress=0x600c;
    f.controller.setSensorEnabled(0x40,true);f.finish();
    for(const auto &c:f.transport->commands)require(c.message!=0xb,"failed error acknowledgement aborts re-enable");
}
void sensorConfiguration(){
    Fixture f;f.connect();
    for(quint16 source:{0x40,0x42,0x43,0x44,0x45,0x46}) {
        const quint32 base=0x6000+(source-0x40)*0x100;
        for(bool enabled:{true,false}) {
            f.controller.setSensorEnabled(source,enabled);f.finish();f.transport->clear();f.diagnostics.clear();
            QJsonObject request{{"op","sensor_config"},{"source",source}};
            const bool uart=source==0x40 || source==0x42 || source==0x45;
            const quint32 address=uart?base+0x10:base+(source==0x46?0x2c:0x14);
            const quint32 value=uart?(source==0x40?9600:source==0x42?921600:500000):500;
            request[uart?"baud":"poll_ms"]=int(value);
            if(source==0x40)request["poll_ms"]=750;
            f.controller.configureSensor(request);f.finish();
            require(f.diagnostics.isEmpty() && f.controller.hardwareValues().value(address)==value,"sensor parameters succeed only after actual readback");
            require(bool(f.transport->regs.value(base+4)&1)==enabled,"parameter transaction restores prior enabled state");
            const int writeIndex=indexOfWrite(f.transport->commands,address,value);
            require(writeIndex>=0,"target parameter written over VLP1");
            bool stopped=false,verified=false,restarted=false;
            for(int i=0;i<f.transport->commands.size();++i) {
                const auto &command=f.transport->commands[i];
                if(command.message==0xb) {require(command.source==source,"parameter update never stops another sensor");stopped|=command.value==0&&i<writeIndex;restarted|=command.value==1&&i>writeIndex;}
                if(command.message==3)require((command.address&~255u)==base,"no acquisition or unrelated configuration writes");
                if(command.message==2 && command.address==address && i>writeIndex)verified=true;
                if(command.message==0xb && command.value==1)require(verified,"restore only after parameter verification");
            }
            require(stopped==enabled && restarted==enabled,"disabled channels remain disabled");
        }
    }
    f.transport->clear();f.controller.configureSensor({{"op","sensor_config"},{"source",0x40},{"baud",115200}});
    require(f.transport->commands.isEmpty(),"invalid range causes no hardware side effects");
    for(const auto &entry:{qMakePair(0x601c,1u),qMakePair(0x641c,1u),qMakePair(0x6514,2u)}) {
        f.transport->regs[entry.first]=entry.second;f.transport->clear();f.diagnostics.clear();
        const quint16 source=entry.first==0x601c?0x40:entry.first==0x641c?0x44:0x45;
        f.controller.configureSensor(source==0x45?QJsonObject{{"op","sensor_config"},{"source",source},{"baud",115200}}:
            QJsonObject{{"op","sensor_config"},{"source",source},{"poll_ms",500}});f.finish();
        require(!f.diagnostics.isEmpty() && f.transport->writes().isEmpty(),"current mode constraints checked before disabling or writing");
        for(const auto &command:f.transport->commands)require(command.message==2,"constraint rejection only reads hardware");
        f.transport->regs[entry.first]=0;
    }
    f.controller.setSensorEnabled(0x40,true);f.finish();f.transport->clear();f.transport->failWriteAddress=0x6010;
    f.controller.configureSensor({{"op","sensor_config"},{"source",0x40},{"baud",4800}});f.finish();
    require(!f.controller.hardwareValues().contains(0x6010) && f.transport->regs.value(0x6004)==0,"failed write leaves channel stopped and parameter unconfirmed");
    int writes=0;for(const auto &c:f.transport->commands)if(c.message==3)++writes;
    require(writes==1,"failed parameter write is never retried");
    f.transport->failWriteAddress=0xffffffff;f.transport->ignoreWriteAddress=0x6010;f.transport->clear();f.diagnostics.clear();
    f.controller.configureSensor({{"op","sensor_config"},{"source",0x40},{"baud",4800}});f.finish();
    require(!f.diagnostics.isEmpty() && f.transport->regs.value(0x6004)==0,"ACK without matching readback fails without restoring channel");
    writes=0;for(const auto &c:f.transport->commands)if(c.message==3)++writes;
    require(writes==1,"readback mismatch rereads but never repeats write");

    Fixture connected;connected.connect();
    for(quint16 source:{0x40,0x42,0x43,0x44,0x45,0x46}) {
        connected.controller.setSensorEnabled(source,false);connected.finish();
        const auto before=connected.transport->regs;connected.transport->clear();connected.diagnostics.clear();
        QJsonObject request{{"op","sensor"},{"source",source},{"enabled",true}};
        const quint32 base=0x6000+(source-0x40)*0x100;
        const bool baud=source==0x40||source==0x42||source==0x45;
        const quint32 address=base+(baud?0x10:source==0x46?0x2c:0x14),value=baud?(source==0x40?9600:500000):500;
        request[baud?"baud":"poll_ms"]=int(value);
        connected.controller.connectSensor(request);connected.finish();
        require(connected.diagnostics.isEmpty()&&connected.controller.hardwareValues().value(address)==value
            &&connected.controller.hardwareValues().value(base+4)==1,"connect applies and verifies the staged parameter before enabling");
        bool verified=false,enabled=false;
        const int written=indexOfWrite(connected.transport->commands,address,value);
        for(int i=0;i<connected.transport->commands.size();++i) {
            const auto& command=connected.transport->commands[i];
            if(i>written&&command.message==2&&command.address==address)verified=true;
            if(command.message==0xb&&command.value==1){require(command.source==source&&verified,"only target sensor starts after parameter readback");enabled=true;}
        }
        require(written>=0&&enabled,"connect actually writes and enables over VLP1");
        for(quint32 control:{0x6004u,0x6204u,0x6304u,0x6404u,0x6504u,0x6604u,0x4004u,0x4104u,0x5004u,0x5104u})
            if(control!=base+4)require(connected.transport->regs.value(control)==before.value(control),"connect preserves other sensors and acquisition");
    }
    connected.controller.setSensorEnabled(0x42,false);connected.finish();connected.transport->clear();
    connected.transport->failWriteAddress=0x6210;
    connected.controller.connectSensor({{"op","sensor"},{"source",0x42},{"enabled",true},{"baud",921600}});connected.finish();
    require(connected.transport->regs.value(0x6204)==0,"rejected staged write keeps the device disconnected");
    for(const auto& command:connected.transport->commands)require(command.message!=0xb,"failed staged write never enables a sensor");
    require(connected.transport->writes().size()==1,"failed connection never automatically resends parameters");
}
void threadCleanup(){
    QThread worker;auto *transport=new ScriptedTransport;QPointer<FpgaDeviceController> controller=new FpgaDeviceController(std::unique_ptr<FpgaUsbTransport>(transport));
    controller->moveToThread(&worker);QObject::connect(&worker,&QThread::finished,controller,&QObject::deleteLater);worker.start();
    QMetaObject::invokeMethod(controller,[controller]{controller->connectDevice({},"auto");},Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(controller,[controller]{controller->disconnectDevice();},Qt::BlockingQueuedConnection);
    worker.quit();require(worker.wait(3000),"USB worker thread stops promptly");require(controller.isNull(),"controller, session, transport and timers destroyed on owning thread");
}
}
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    if(app.arguments().contains("--sensor-config")) {sensorConfiguration();std::puts("FPGA sensor configuration tests passed");return 0;}
    if(app.arguments().contains("--hardware-probe")) {
        FpgaDeviceController controller;
        const int durationIndex=app.arguments().indexOf("--hardware-duration-ms");
        const int durationMs=durationIndex>=0?app.arguments().value(durationIndex+1).toInt():6000;
        require(durationMs>0,"positive hardware probe duration");
        QString error;
        QJsonObject beforeRecovery;
        QObject::connect(&controller,&FpgaDeviceController::snapshot,[&](quint64,const QJsonObject &o){
            if(o.value("event")=="config") {
                beforeRecovery=o.value("hardware_registers").toObject();
                for(quint32 b:{0x2000u,0x2100u,0x3000u,0x3100u,0x4000u,0x4100u,0x5000u,0x5100u,0x6000u,0x6200u,0x6300u,0x6400u,0x6500u,0x6600u})
                    std::printf("before recovery control %04x=%u\n",b+4,quint32(beforeRecovery.value(QString("0x%1").arg(b+4,8,16,QChar('0'))).toDouble()));
            }
        });
        QObject::connect(&controller,&FpgaDeviceController::logRecordGenerated,[&](const LogRecord &record){
            if(record.level>=LogLevel::Error) error=record.message;
            std::fprintf(stderr,"%s\n",qPrintable(record.message));
        });
        // Exercises the production connection recovery, preserving prior enable
        // states and parameters while clearing historical error/partial state.
        for(int attempt=0;attempt<2;++attempt) {
            controller.connectDevice({},"auto");
            waitUntil([&]{return !controller.busy();},"hardware connection handshake finishes");
            require(error.isEmpty() && controller.ready(),"real board passes complete UI connection handshake");
            for(quint32 b:{0x2000u,0x2100u,0x3000u,0x3100u,0x4000u,0x4100u,0x5000u,0x5100u,0x6000u,0x6200u,0x6300u,0x6400u,0x6500u,0x6600u}) {
                const auto key=QString("0x%1").arg(b+4,8,16,QChar('0'));
                require(beforeRecovery.contains(key)&&controller.hardwareValues().value(b+4)==quint32(beforeRecovery.value(key).toDouble()),"connection preserves existing module enable state");
            }
            QMap<quint16,int> completeCycles,incompleteCycles,validSamples,invalidSamples;
            QElapsedTimer timer;timer.start();
            auto *session=controller.findChild<FpgaDeviceSession*>();
            const auto waveConnection=QObject::connect(session,&FpgaDeviceSession::waveformCompleted,[&](const VaporView::FpgaWave::CompletedStream &w){
                if(timer.elapsed()<1500)return;
                if(w.complete&&!w.partial&&!w.overflow&&!w.continuityError)++completeCycles[w.source];else ++incompleteCycles[w.source];
            });
            const auto sensorConnection=QObject::connect(session,&FpgaDeviceSession::sensorReadingReceived,[&](const VaporView::FpgaSensor::Reading &r){
                if(timer.elapsed()<1500)return;
                if(r.validity.measurement)++validSamples[r.source];else ++invalidSamples[r.source];
            });
            while(timer.elapsed()<durationMs && error.isEmpty()) {
                QCoreApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);
            }
            require(error.isEmpty() && controller.ready(),"idle polling and periodic register refresh remain connected");
            const auto diagnostic=controller.findChild<FpgaDeviceSession*>()->transport()->diagnostic();
            std::printf("hardware idle %d ms: cancelled read retries=%llu, bytes=%llu\n",durationMs,
                diagnostic.fields.value("cancelled_read_retries").toULongLong(),diagnostic.bytesRead);
            controller.refresh();
            waitUntil([&]{return !controller.busy();},"readback after extended idle finishes");
            require(error.isEmpty() && controller.ready(),"readback still succeeds after extended idle");
            QObject::disconnect(waveConnection);QObject::disconnect(sensorConnection);
            for(quint16 source:{0x30,0x31})std::printf("source=%04x complete=%d incomplete=%d\n",source,completeCycles.value(source),incompleteCycles.value(source));
            for(quint16 source:{0x40,0x42,0x43,0x44,0x45,0x46})std::printf("sensor=%04x valid=%d invalid=%d\n",source,validSamples.value(source),invalidSamples.value(source));
            controller.disconnectDevice();
        }
        std::puts("hardware controller connect/idle/reconnect probe passed");return 0;
    }
    connectionEventTimeFilter();advancedDataPaths();connectionResultLogs();sensorRates();connectionAndVersions();connectionRecovery();backgroundRefresh();independentAndAcquisition();restartClearsDliaState();configurationAndFailure();scopedConfiguration();rawAndBusy();temperature();sensorUploadAndFailure();ai8DisabledConfiguration();ptbHistoricalErrorRecovery();threadCleanup();std::puts("FPGA controller tests passed");return 0;
}
