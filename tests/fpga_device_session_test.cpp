#include "FpgaDeviceSession.h"
#include <QCoreApplication>
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
}
int main(int argc,char **argv){QCoreApplication app(argc,argv);validContinuousResponses();semanticRejections();partialWaveformsSurviveClose();expiredWaveformsVisible();std::puts("fpga_device_session_test passed; software fixtures only");return 0;}
