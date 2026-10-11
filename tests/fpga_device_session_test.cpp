#include "FpgaDeviceSession.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace VaporView;
using namespace VaporView::Ground::Devices;
namespace {
void require(bool ok,const char *message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
void appendU16(QByteArray& bytes,quint16 value){bytes.append(char(value));bytes.append(char(value>>8));}
void appendU32(QByteArray& bytes,quint32 value){for(int i=0;i<4;++i)bytes.append(char(value>>(8*i)));}
QByteArray toArray(const std::vector<std::uint8_t>& bytes){return QByteArray(reinterpret_cast<const char*>(bytes.data()),qsizetype(bytes.size()));}
QByteArray frame(FpgaVlp1::FrameType type,quint32 sequence,quint16 source,quint16 message,const QByteArray& payload,quint64 tick=0,quint32 cycle=0){
    return toArray(FpgaVlp1::buildFrame(type,sequence,source,message,std::vector<std::uint8_t>(payload.begin(),payload.end()),true,3,tick,cycle));
}
QByteArray success(){QByteArray b;appendU32(b,0);return b;}
QByteArray capability(){auto b=success();for(quint32 value:{0xffffu,0x100u,100000000u,8192u,4096u,1u})appendU32(b,value);return b;}
QByteArray registers(quint32 address,quint16 count,const QVector<quint32>& values,quint16 reserved=0){
    auto b=success();appendU32(b,address);appendU16(b,count);appendU16(b,reserved);for(auto value:values)appendU32(b,value);return b;
}
QByteArray pressure(){QByteArray b;appendU16(b,1);appendU16(b,1);appendU16(b,0x12);b.append(char(7));b.append(char(4));appendU32(b,101459000);return b;}
QByteArray firstRawFragment(){
    QByteArray b;appendU16(b,2);appendU16(b,24);appendU32(b,1000000);appendU32(b,4);appendU32(b,0);
    appendU16(b,0);appendU16(b,2);appendU32(b,0);appendU32(b,2);appendU32(b,0);appendU32(b,10);appendU32(b,11);return b;
}
void validContinuousResponses(){
    // Five-byte echo needs three padding bytes. The following capability,
    // register response and sensor data share the same USB read.
    const auto bytes=frame(FpgaVlp1::FrameType::Response,1,1,0xfe,success()+"hello")+
        frame(FpgaVlp1::FrameType::Response,2,1,1,capability())+
        frame(FpgaVlp1::FrameType::Response,3,1,2,registers(0x6600,2,{0x00460200,1}))+
        frame(FpgaVlp1::FrameType::Data,99,0x40,0x1100,pressure());
    FpgaDeviceSession session(std::make_unique<FpgaUsbReplayTransport>(bytes));
    require(session.open("replay"),"open deterministic replay transport");
    int completed=0;bool capabilitiesDone=false,registersDone=false,ptbUpdated=false;
    QObject::connect(&session,&FpgaDeviceSession::commandCompleted,[&](quint32 seq,quint16 message,bool ok,quint32 status,const QByteArray& payload){
        require(ok&&status==0,"valid command response succeeds");require(seq>=1&&seq<=3,"expected queued command sequence");
        const quint16 expected[]={0,0xfe,1,2};require(message==expected[seq],"completion preserves matching message for each sequence");
        if(seq==1)require(payload.mid(4)=="hello","PING echo retains non-word-aligned bytes");++completed;
    });
    QObject::connect(&session,&FpgaDeviceSession::capabilitiesChanged,[&](const FpgaCapabilities &c){require(c.valid&&c.protocolVersion==0x100&&c.timeClockHz==100000000&&c.maxBulkPayload==8192,"capabilities retain documented units");capabilitiesDone=true;});
    QObject::connect(&session,&FpgaDeviceSession::registerReadCompleted,[&](quint32 seq,quint32 address,const QVector<quint32> &values){require(seq==3&&address==0x6600&&values==QVector<quint32>({0x00460200,1}),"requested register count and values routed together");registersDone=true;});
    QObject::connect(&session,&FpgaDeviceSession::ptbDataUpdated,[&](const PtbData &p){require(p.valid&&p.pressure_hpa>1014.5&&p.pressure_hpa<1014.7,"PTB millipascal converted to hPa");ptbUpdated=true;});
    require(session.ping("hello")==1,"PING sequence assigned");require(session.requestCapabilities()==2,"capability command queued");require(session.readRegisters(0x6600,2)==3,"READ command queued");
    session.poll();require(completed==3&&capabilitiesDone&&registersDone&&ptbUpdated,"padding and glued frames preserve completions and sensor data");
    require(session.transport()->diagnostic().bytesWritten>0,"command bytes actually sent");session.close();
}
void continuousReceiveScheduling(){
    class Stream final : public FpgaUsbTransport {
    public:
        bool open(const QString&) override {opened=true;return true;}
        void close() override {opened=false;}
        bool isOpen() const override {return opened;}
        qint64 read(QByteArray &bytes,qint64) override {
            ++reads;
            bytes=available ? pressureFrame : QByteArray{};
            return bytes.size();
        }
        qint64 write(const QByteArray &bytes) override {return bytes.size();}
        FpgaUsbDiagnostic diagnostic() const override {return {};}
        QByteArray pressureFrame=frame(FpgaVlp1::FrameType::Data,1,0x40,0x1100,pressure());
        bool opened=false,available=true;
        int reads=0;
    };
    auto wire=std::make_unique<Stream>();auto *stream=wire.get();
    FpgaDeviceSession session(std::move(wire));
    require(session.open(),"open continuous stream");
    int received=0;
    QObject::connect(&session,&FpgaDeviceSession::ptbDataUpdated,[&](const PtbData&){++received;});
    session.poll();
    require(received>1&&received<=64,"busy stream drains multiple transfers but yields a bounded batch");
    auto *timer=session.findChild<QTimer*>();
    require(timer&&timer->isActive()&&timer->interval()==0,"busy stream resumes without a timer delay");
    stream->available=false;const int previousReads=stream->reads;
    session.poll();
    require(stream->reads==previousReads+1&&timer->interval()==1,"empty stream backs off after one read");
    stream->available=true;
    QTimer::singleShot(0,&session,[&]{session.close();});
    QElapsedTimer deadline;deadline.start();
    while(session.isOpen()&&deadline.elapsed()<1000)QCoreApplication::processEvents();
    require(!session.isOpen()&&!timer->isActive(),"continuous input does not starve queued disconnect");
    require(session.open()&&timer->interval()==1,"reopen restores idle polling");
    QObject::connect(&session,&FpgaDeviceSession::ptbDataUpdated,[&](const PtbData&){session.close();});
    const int beforeClose=stream->reads;session.poll();
    require(stream->reads==beforeClose+1&&!timer->isActive(),"close from a receive callback stops draining and never restarts timer");
}
void malformedResponse(quint16 message,const QByteArray& payload,const char *description){
    FpgaDeviceSession session(std::make_unique<FpgaUsbReplayTransport>(frame(FpgaVlp1::FrameType::Response,1,1,message,payload)));
    require(session.open("replay"),"open malformed-response fixture");int completed=0,readEvents=0,capEvents=0;
    QObject::connect(&session,&FpgaDeviceSession::commandCompleted,[&](quint32 seq,quint16 msg,bool ok,quint32 status,const QByteArray&){
        require(seq==1&&msg==message,"rejection identifies original command");require(!ok&&status==12,description);++completed;
    });
    QObject::connect(&session,&FpgaDeviceSession::registerReadCompleted,[&](quint32,quint32,const QVector<quint32>&){++readEvents;});
    QObject::connect(&session,&FpgaDeviceSession::capabilitiesChanged,[&](const FpgaCapabilities&){++capEvents;});
    if(message==2)require(session.readRegisters(0x6600,2)==1,"malformed READ request sent");
    else if(message==1)require(session.requestCapabilities()==1,"malformed capability request sent");
    else require(session.ping("hello")==1,"malformed PING request sent");
    session.poll();require(completed==1&&readEvents==0&&capEvents==0,"CRC-valid malformed response never updates actual values or capabilities");session.close();
}
void semanticRejections(){
    malformedResponse(2,registers(0x6604,2,{1,2}),"READ cannot substitute another address");
    malformedResponse(2,registers(0x6600,1,{1}),"READ cannot return fewer registers");
    malformedResponse(2,registers(0x6600,3,{1,2,3}),"READ cannot return extra registers");
    malformedResponse(2,registers(0x6600,2,{1}),"READ count requires complete payload");
    malformedResponse(2,registers(0x6600,2,{1,2,3}),"READ rejects trailing data behind correct count");
    malformedResponse(2,registers(0x6600,2,{1,2},1),"READ reserved field must be zero");
    malformedResponse(2,success(),"READ status-only ACK is not register data");
    malformedResponse(0xfe,success()+"HELLO","PING status cannot replace requested echo");
    malformedResponse(0xfe,success()+"hell","PING echo cannot be truncated");
    malformedResponse(1,capability().left(27),"capability cannot omit trailing byte");
    malformedResponse(1,capability()+QByteArray(4,char(0)),"capability must match published layout exactly");
}
void partialWaveformsSurviveClose(){
    auto transport=std::make_unique<FpgaUsbReplayTransport>(frame(FpgaVlp1::FrameType::Data,1,0x20,0x1000,firstRawFragment(),100,7));
    FpgaDeviceSession session(std::move(transport));require(session.open("replay"),"open partial-wave fixture");
    QVector<FpgaWave::CompletedStream> evidence;
    QObject::connect(&session,&FpgaDeviceSession::waveformCompleted,[&](const FpgaWave::CompletedStream &s){evidence.append(s);});
    session.poll();require(evidence.isEmpty(),"single fragment is not complete scan");session.close();
    require(evidence.size()==1,"close flushes missing-fragment evidence");const auto &s=evidence.front();
    require(s.source==0x20&&s.cycleId==7&&s.timestamp==100&&!s.complete&&s.partial&&s.continuityError,"close preserves partial quality and grouping key");
    require(s.signed32Samples==QVector<qint32>({10,11})&&s.fragmentFirstPoints==QVector<quint32>({0})&&s.fragmentPointCounts==QVector<quint32>({2}),"partial evidence retains available codes and fragment spans");
    session.close();require(evidence.size()==1,"repeated close never duplicates evidence");
}
void expiredWaveformsVisible(){
    QByteArray bytes;for(quint32 cycle=1;cycle<=5;++cycle)bytes+=frame(FpgaVlp1::FrameType::Data,cycle,0x20,0x1000,firstRawFragment(),cycle*100,cycle);
    FpgaDeviceSession session(std::make_unique<FpgaUsbReplayTransport>(bytes));require(session.open("replay"),"open missing-cycles fixture");QVector<FpgaWave::CompletedStream> evidence;
    QObject::connect(&session,&FpgaDeviceSession::waveformCompleted,[&](const FpgaWave::CompletedStream &s){evidence.append(s);});
    session.poll();require(evidence.size()==1&&evidence.front().cycleId==1&&evidence.front().partial,"new cycles publish oldest missing-cycle evidence before disconnect");
    session.flushWaveforms();require(evidence.size()==5,"explicit drain publishes remaining partial cycles");session.close();require(evidence.size()==5,"close after explicit drain never double counts");
}
void boundedReadRetry(){
    auto recoveredWire=std::make_unique<FpgaUsbReplayTransport>();auto *recovered=recoveredWire.get();
    auto missingWire=std::make_unique<FpgaUsbReplayTransport>();auto *missing=missingWire.get();
    auto writeWire=std::make_unique<FpgaUsbReplayTransport>();auto *written=writeWire.get();
    FpgaDeviceSession recoverSession(std::move(recoveredWire)),missingSession(std::move(missingWire)),writeSession(std::move(writeWire));
    require(recoverSession.open()&&missingSession.open()&&writeSession.open(),"open timeout fixtures");
    int completions=0,readTimeouts=0,writeTimeouts=0;
    QObject::connect(&recoverSession,&FpgaDeviceSession::commandCompleted,[&](quint32,quint16,bool ok,quint32,const QByteArray&){require(ok,"retried read is valid");++completions;});
    QObject::connect(&missingSession,&FpgaDeviceSession::commandTimedOut,[&](quint32,quint16){++readTimeouts;});
    QObject::connect(&writeSession,&FpgaDeviceSession::commandTimedOut,[&](quint32,quint16){++writeTimeouts;});
    const auto seq=recoverSession.readRegisters(0x6600,1);missingSession.readRegisters(0x6600,1);
    writeSession.writeRegisters(0x2004,{1});
    const auto readBytes=recovered->writtenBytes(),writeBytes=written->writtenBytes();
    QThread::msleep(10020);
    recoverSession.poll();missingSession.poll();writeSession.poll();
    require(recovered->writtenBytes()==readBytes+readBytes,"missing register response repeats only the original read");
    require(written->writtenBytes()==writeBytes&&writeTimeouts==1,"uncertain write reports timeout without replay");
    recovered->appendIncoming(frame(FpgaVlp1::FrameType::Response,seq,1,2,registers(0x6600,1,{0x00460200})));
    recoverSession.poll();require(completions==1,"read response accepted after one retry");
    QThread::msleep(10020);missingSession.poll();
    require(readTimeouts==1&&missing->writtenBytes()==readBytes+readBytes,"missing read fails after one retry rather than retrying forever");
}
void transportErrorRetainsDetail(){
    class FailedRead final : public FpgaUsbTransport {
    public:
        bool open(const QString&) override {opened=true;return true;}
        void close() override {opened=false;detail={};}
        bool isOpen() const override {return opened;}
        qint64 read(QByteArray &bytes,qint64) override {
            bytes.clear();opened=false;detail.state=FpgaUsbTransportState::Error;
            detail.detail="Cypress bulk IN failed (NT=0xc000000e)";return -1;
        }
        qint64 write(const QByteArray&) override {return -1;}
        FpgaUsbDiagnostic diagnostic() const override {return detail;}
        bool opened=false;FpgaUsbDiagnostic detail;
    };
    FpgaDeviceSession session(std::make_unique<FailedRead>());
    require(session.open(),"open failed-read fixture");
    QString error;
    QObject::connect(&session,&FpgaDeviceSession::errorOccurred,[&](const QString &message){error=message;});
    session.poll();
    require(!session.isOpen() && session.state()==FpgaSessionState::Error,"read failure closes session");
    require(error.contains("NT=0xc000000e") && session.stateDetail()==error,"driver error survives transport cleanup");
}
}
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    if(app.arguments().contains("--hardware-stream-probe")) {
        // Read-only timer-driven streaming check. Acquisition must already be
        // running, with every other USB reader disconnected. No control writes.
        FpgaDeviceSession session;
        QEventLoop loop;
        QString error;
        quint64 cycles[2]={},partial[2]={},overflow[2]={},continuity[2]={};
        QVector<quint32> drops[2];
        bool measuring=false;
        QObject::connect(&session,&FpgaDeviceSession::errorOccurred,[&](const QString &value){error=value;loop.quit();});
        QObject::connect(&session,&FpgaDeviceSession::waveformCompleted,[&](const FpgaWave::CompletedStream &s){
            if(!measuring||s.source<0x30||s.source>0x31)return;
            const int ch=s.source-0x30;++cycles[ch];partial[ch]+=s.partial;overflow[ch]+=s.overflow;continuity[ch]+=s.continuityError;
        });
        QObject::connect(&session,&FpgaDeviceSession::registerReadCompleted,[&](quint32,quint32 address,const QVector<quint32>& values){
            if(values.isEmpty())return;
            if(address==0x5048||address==0x5148)drops[(address-0x5048)/0x100].append(values.front());
            std::printf("register %04x = %u\n",address,values.front());
        });
        require(session.open(),"open exclusive USB stream probe");
        session.readRegisters(0x5004,1);session.readRegisters(0x5104,1);
        QTimer::singleShot(3000,&loop,[&]{
            session.readRegisters(0x5048,1);session.readRegisters(0x5148,1);measuring=true;
        });
        QTimer::singleShot(23000,&loop,[&]{
            measuring=false;session.readRegisters(0x5048,1);session.readRegisters(0x5148,1);
        });
        QTimer::singleShot(25000,&loop,&QEventLoop::quit);
        loop.exec();session.close();
        bool passed=error.isEmpty();
        for(int ch=0;ch<2;++ch){
            const bool valid=drops[ch].size()==2;
            const quint32 delta=valid?drops[ch][1]-drops[ch][0]:0;
            std::printf("channel %d: cycles=%llu partial=%llu overflow=%llu continuity=%llu drops_valid=%d drops_delta=%u\n",
                ch+1,cycles[ch],partial[ch],overflow[ch],continuity[ch],valid,delta);
            passed=passed&&valid&&cycles[ch]>0&&delta==0&&overflow[ch]==0&&continuity[ch]==0;
        }
        if(!error.isEmpty())std::fprintf(stderr,"%s\n",qPrintable(error));
        require(passed,"timer-driven stream receives active channels without new drops");
        std::puts("hardware streaming probe passed; partial flags reported separately");return 0;
    }
    if(app.arguments().contains("--hardware-probe")) {
        FpgaDeviceSession session;
        QObject::connect(&session,&FpgaDeviceSession::errorOccurred,[](const QString &error){std::fprintf(stderr,"%s\n",qPrintable(error));});
        require(session.open(),"open attached GP01 board");
        int completed=0;
        QObject::connect(&session,&FpgaDeviceSession::commandCompleted,[&](quint32,quint16 message,bool ok,quint32 status,const QByteArray&){
            std::printf("command 0x%04x: success=%d status=%u\n",message,ok,status);
            require(ok,"read-only hardware handshake response succeeds");++completed;
        });
        session.ping("VaporView");session.requestCapabilities();session.readRegisters(0,5);
        QElapsedTimer timer;timer.start();
        while(timer.elapsed()<12000 && session.isOpen()) {session.poll();QThread::msleep(1);}
        require(session.isOpen(),"hardware connection survives idle reads after handshake");
        require(completed==3 && session.capabilities().valid,"PING, capabilities and register read complete");
        session.close();std::puts("hardware session probe passed");return 0;
    }
    validContinuousResponses();continuousReceiveScheduling();semanticRejections();partialWaveformsSurviveClose();expiredWaveformsVisible();transportErrorRetainsDetail();boundedReadRetry();std::puts("fpga_device_session_test passed; software fixtures only");return 0;
}
