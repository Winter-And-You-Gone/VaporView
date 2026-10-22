#include "FpgaDeviceController.h"
#include "FpgaUsbSimulator.h"
#include "FpgaTelemetry.h"
#include "FpgaExtensions.h"
#include "ground/devices/EpsilonConfigurationService.h"
#include "ground/devices/FpgaInstrumentService.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QDebug>
#include <QtEndian>
#include <cmath>

using namespace VaporView;
using namespace VaporView::Ground::Devices;
namespace {
bool waitFor(const std::function<bool()>& condition,int timeout=15000) {
    QElapsedTimer time;time.start();while(!condition() && time.elapsed()<timeout) {QCoreApplication::processEvents(QEventLoop::AllEvents,20);QThread::msleep(1);}return condition();
}
QByteArray rtcm(int length) {
    QByteArray data(length+3,'\0');data[0]=char(0xd3);data[1]=char(length>>8);data[2]=char(length);
    quint32 crc=0;for(char b:data) {crc^=quint32(quint8(b))<<16;for(int i=0;i<8;++i){crc<<=1;if(crc&0x1000000)crc^=0x1864cfb;}}
    data.append(char(crc>>16));data.append(char(crc>>8));data.append(char(crc));return data;
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);int errors=0;
    const auto check=[&](bool ok,const char* text){if(!ok){qCritical()<<text;++errors;}};
    auto simulator=makeFpgaUsbSimulator();auto* transport=simulator.get();
    FpgaDeviceController controller(std::move(simulator));
    QVector<FpgaSensor::Reading> readings;
    QObject::connect(&controller,&FpgaDeviceController::measurementUpdated,&app,[&](const FpgaSensor::AdaptedMeasurements& m){readings.append(m.reading);});
    controller.connectDevice({},"simulator",FpgaDeviceController::ConnectMode::PreserveRunningModules);
    check(waitFor([&]{return controller.ready()&&!controller.busy();}),"new firmware handshake");
    check(controller.extensions()==0x3f,"GET_CAPABILITIES extension bits");
    const auto call=[&](const QJsonObject& request) {
        bool finished=false;QJsonObject result;
        const auto observer=QObject::connect(&controller,&FpgaDeviceController::extensionCompleted,&app,[&](const QJsonObject& value){result=value;finished=true;});
        controller.executeOperation(request);
        check(waitFor([&]{return finished;}),"extension completed without hanging");QObject::disconnect(observer);
        return result;
    };
    const auto param=[&](int source,int action,int address,int count,const QByteArray& data=QByteArray{}) {
        return call(QJsonObject{{"op","instrument"},{"source",source},{"action",action},{"address",address},{"count",count},{"data",QString::fromLatin1(data.toBase64())}});
    };
    controller.setSensorEnabled(0x48,true);check(waitFor([&]{return !controller.busy();}),"enable independent RD105");
    check(waitFor([&]{for(const auto& r:readings)if(r.source==0x48 && r.validity.measurement)return true;return false;}),"RD105 valid measurement TLV");
    check(!(controller.hardwareValues().value(0x6604)&1),"RD105 enable does not enable AI8");
    auto result=param(0x48,2,0x1100,1,QByteArray::fromHex("0001"));
    check(result.value("ok").toBool() && result.value("verified").toBool(),"RD105 write is independently verified");
    result=param(0x48,1,0x1100,1);
    check(QByteArray::fromBase64(result.value("data").toString().toLatin1())==QByteArray::fromHex("0001"),"RD105 actual parameter bytes");
    QByteArray block;for(int i=0;i<32;++i)block.append(char(i+1));
    result=param(0x48,2,0x1315,16,block);check(result.value("ok").toBool(),"RD105 full 16-register transaction");
    transport->injectFault("parameter_mismatch");result=param(0x48,2,0x1100,1,QByteArray::fromHex("0002"));
    check(!result.value("ok").toBool(),"RD105 mismatch cannot report success");
    transport->injectFault("parameter_timeout");result=param(0x48,2,0x1100,1,QByteArray::fromHex("0001"));
    check(!result.value("ok").toBool(),"RD105 missing device ACK cannot report success");
    result=call({{"op","device_temperature"},{"source",0x48},{"celsius",26.12345}});
    check(result.value("ok").toBool() && controller.hardwareValues().value(0x6840)==26123450,"RD105 setpoint preserves native precision and uses verified completion counter");
    controller.setSensorEnabled(0x46,true);check(waitFor([&]{return !controller.busy();}),"enable AI8");
    check(controller.hardwareValues().value(0x6668)==1,"AI8 enable applies and verifies eight-channel scanning by default");
    check(waitFor([&]{QSet<quint32> channels;for(const auto& r:readings)if(r.source==0x46 && r.schema==3 && r.ai8Channel)channels.insert(*r.ai8Channel);return channels.size()==8;}),"all 8 AI8 channels survive presentation coalescing");
    {
        FpgaSensor::FpgaSensorAdapter adapter;FpgaSensor::Reading first,second;
        for(const auto& r:readings)if(r.ai8Channel==1)first=r;else if(r.ai8Channel==2)second=r;
        adapter.accept(first);adapter.accept(second);second.ai8DeviceStatus=0;second.validity.deviceOnline=second.validity.measurement=false;
        const auto failed=adapter.accept(second);
        check(failed.ai8Live && std::isfinite(failed.ai8Live->measuredC[0]) && std::isnan(failed.ai8Live->measuredC[1]),"failed scan channel invalidates only its own PV");
        auto malformed=first.rawPayload;qToLittleEndian<quint32>(9,malformed.data()+malformed.size()-4);
        check(!FpgaSensor::FpgaSensorDecoder::decode(malformed,0x46).validity.measurement,"out-of-range scan channel cannot become valid");
        FpgaSensor::Reading remote;const auto json=FpgaRemote::encodeSensor(first,100);
        check(FpgaRemote::parseSensor(json,remote) && remote.ai8Channel==first.ai8Channel,"schema 3 channel survives remote telemetry");
    }
    result=call({{"op","ai8_scan"},{"enabled",false}});check(!result.value("ok").toBool(),"AI8 scan cannot change while enabled");
    result=call({{"op","ai8_cache"},{"channel",8}});check(result.value("ok").toBool() && result.value("channel").toInt()==8 && (result.value("status").toInt()&3)==3,"AI8 cached channel quality is read back independently");
    result=param(0x46,2,7,1,QByteArray::fromHex("013b"));check(result.value("ok").toBool(),"AI8 channel 8 parameter write/readback");
    result=call({{"op","device_temperature"},{"source",0x46},{"channel",8},{"celsius",31.6}});
    check(result.value("ok").toBool(),"AI8 targeted setpoint while scanning");
    result=param(0x46,1,7,1);check(QByteArray::fromBase64(result.value("data").toString().toLatin1())==QByteArray::fromHex("013c"),"AI8 target channel remains channel 8");
    result=param(0x46,1,0,1);check(QByteArray::fromBase64(result.value("data").toString().toLatin1())==QByteArray::fromHex("00fb"),"other AI8 channels are unchanged");
    Ai8TemperatureControllerProtocol::Selection selected;selected.channel=8;
    auto page=readFpgaAi8Page(call,Ai8TemperatureControllerProtocol::Page::Channel,selected);
    check(page.success && page.data.channel.setpointC==31.6,"existing AI8 parameter page uses FPGA register adapter");
    const auto snapshot=sendFpgaTemperatureCommand(call,CommandId::QueryDeviceStatus,{});
    check(snapshot.status==LocalTemperatureCommandStatus::Confirmed && snapshot.latestData.valid,"RD105 full parameter snapshot uses existing device decoding");
    check(std::isnan(snapshot.latestData.channels[1].target_temperature_c),"single-channel RD105 does not read or fabricate a second channel");
    TemperatureControllerCommand secondChannel;secondChannel.channel=2;
    const auto dualSnapshot=sendFpgaTemperatureCommand(call,CommandId::QueryDeviceStatus,secondChannel);
    check(dualSnapshot.status==LocalTemperatureCommandStatus::Confirmed && std::isfinite(dualSnapshot.latestData.channels[1].target_temperature_c),"explicit RD105 channel two reads both channels");
    controller.setSensorEnabled(0x42,true);check(waitFor([&]{return !controller.busy();}),"enable EPSILON");
    const auto mailbox=[&](int kind,const QByteArray& data,int target=0) {
        return call(QJsonObject{{"op","epsilon_mailbox"},{"kind",kind},{"target",target},{"data",QString::fromLatin1(data.toBase64())}});
    };
    result=mailbox(0,"#fconfig\r\n");check(result.value("ok").toBool() && !result.value("verified").toBool(),"mailbox transport completion is not device verification");
    transport->injectFault("epsilon_error");result=mailbox(0,"#fparam get GNSS_L_IMU_ANT1_X\r\n");
    check(result.value("ok").toBool() && QByteArray::fromBase64(result.value("data").toString().toLatin1()).contains("ERROR"),"device ASCII error preserved through transport");
    Ground::EpsilonDeviceOperation operation;operation.exchange=[&](const std::string& command,int) {
        const auto r=mailbox(0,QByteArray::fromStdString(command));return QByteArray::fromBase64(r.value("data").toString().toLatin1()).toStdString();
    };
    EpsilonSettingsOperation settings;settings.group=EpsilonSettingsGroup::Installation;settings.values={{"GNSS_L_IMU_ANT1_X",1.25}};
    auto epsilon=Ground::EpsilonConfigurationService::applySettings(operation,settings,{});
    check(epsilon.succeeded() && epsilon.settings_snapshot.readback_verified,"EPSILON existing setting parser verifies actual mailbox readback");
    epsilon=Ground::EpsilonConfigurationService::configurePacketRates(operation,50,50,{{0x40,50},{0x5a,10}},QString(),{});
    check(epsilon.succeeded() && epsilon.settings_snapshot.readback_verified && epsilon.settings_snapshot.restart_required,"EPSILON packet rates preserve command case and verify the actual fmsg list");
    const auto frame=rtcm(1023);check(FpgaExtensions::rtcmFrame(frame),"maximum RTCM3 CRC24Q fixture");
    result=mailbox(1,frame);check(result.value("ok").toBool() && result.value("rtcm_bytes").toInt()==1029,"RTCM whole frame mailbox with FDILink fragmentation");
    auto invalid=frame;invalid[9]^=1;check(!FpgaRemote::validateControl({{"op","epsilon_mailbox"},{"kind",1},{"data",QString::fromLatin1(invalid.toBase64())}}),"invalid RTCM rejected before writes");
    result=mailbox(1,frame,1);check(result.value("ok").toBool() && result.value("rtcm_bytes").toInt()==2058,"external RS422 target accepts raw full RTCM");
    result=call({{"op","dlia_cutoff"},{"channel",0},{"hz",250}});check(result.value("ok").toBool() && controller.hardwareValues().value(0x505c)==250,"DLIA cutoff shadow and active readback");
    check(!FpgaRemote::validateControl({{"op","dlia_cutoff"},{"channel",0},{"hz",123}}),"unsupported cutoff rejected");
    result=param(0x48,3,0,1,QByteArray::fromHex("0001"));check(result.value("ok").toBool() && result.value("unverified").toBool() && !result.value("verified").toBool(),"factory reset ACK is explicitly unverified");
    check(!(controller.hardwareValues().value(0x6804)&1),"factory reset stops RD105 polling");
    controller.disconnectDevice();
    auto old=makeFpgaUsbSimulator();old->injectFault("legacy_extensions");FpgaDeviceController legacy(std::move(old));
    legacy.connectDevice({},"simulator",FpgaDeviceController::ConnectMode::PreserveRunningModules);
    check(waitFor([&]{return legacy.ready()&&!legacy.busy();}),"legacy extension word remains compatible");
    check(!legacy.hardwareValues().contains(0x6800) && !legacy.hardwareValues().contains(0x5058),"legacy firmware never reads unsupported registers");
    legacy.disconnectDevice();
    qInfo()<<"FPGA extension failures:"<<errors;return errors?1:0;
}
