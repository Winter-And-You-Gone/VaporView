#include "FpgaDeviceController.h"
#include "FpgaUsbSimulator.h"
#include "FpgaVlp1.h"
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
    auto simulator=makeFpgaUsbSimulator(nullptr,FpgaSimulatorStartup::Idle,FpgaSimulatorFirmware::HistoricalExtensions);auto* transport=simulator.get();
    FpgaDeviceController controller(std::move(simulator));
    QVector<FpgaSensor::Reading> readings;
    QObject::connect(&controller,&FpgaDeviceController::measurementUpdated,&app,[&](const FpgaSensor::AdaptedMeasurements& m){readings.append(m.reading);});
    controller.connectDevice({},"simulator",FpgaDeviceController::ConnectMode::PreserveRunningModules);
    check(waitFor([&]{return controller.ready()&&!controller.busy();}),"new firmware handshake");
    check(controller.extensions()==0x3f,"GET_CAPABILITIES extension bits");
    auto historic=FpgaControlConfig{};historic.ai8.scan=true;controller.setConfiguration(historic);
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
    check(waitFor([&]{QSet<quint32> channels;for(const auto& r:readings)if(r.source==0x48&&r.validity.measurement&&r.rd105Channel)channels.insert(*r.rd105Channel);return channels.size()==2;}),
        "both RD105 channels survive controller presentation coalescing");
    check(!(controller.hardwareValues().value(0x6604)&1),"RD105 enable does not enable AI8");
    auto result=param(0x48,2,0x1100,1,QByteArray::fromHex("0001"));
    check(result.value("ok").toBool() && result.value("verified").toBool(),"RD105 write is independently verified");
    result=param(0x48,1,0x1100,1);
    check(QByteArray::fromBase64(result.value("data").toString().toLatin1())==QByteArray::fromHex("0001"),"RD105 actual parameter bytes");
    result=param(0x48,2,0x2100,1,QByteArray::fromHex("0001"));
    check(result.value("ok").toBool(),"RD105 channel two output can be enabled independently");
    check(waitFor([&]{bool positive=false,negative=false;for(const auto& r:readings)if(r.source==0x48&&r.validity.measurement&&r.rd105OutputEnabled==1&&r.rd105OutputRaw) {
        positive|=r.rd105Channel==1&&*r.rd105OutputRaw>0;negative|=r.rd105Channel==2&&*r.rd105OutputRaw<0;
    }return positive&&negative;}),"RD105 simulator exposes signed output percentages for both enabled channels");
    FpgaSensor::Reading negativeOutput;
    for(const auto& r:readings)if(r.rd105Channel==2&&r.rd105OutputRaw&&*r.rd105OutputRaw<0)negativeOutput=r;
    FpgaSensor::Reading remoteOutput;
    check(FpgaRemote::parseSensor(FpgaRemote::encodeSensor(negativeOutput,100),remoteOutput)
        &&remoteOutput.rd105OutputRaw==negativeOutput.rd105OutputRaw&&remoteOutput.rd105OutputEnabled==1,
        "remote telemetry preserves a negative RD105 output");
    readings.clear();
    result=param(0x48,2,0x1100,1,QByteArray::fromHex("0000"));
    check(result.value("ok").toBool()&&waitFor([&]{bool stopped=false,running=false;for(const auto& r:readings) {
        stopped|=r.rd105Channel==1&&r.rd105OutputEnabled==0&&r.rd105OutputRaw==0;
        running|=r.rd105Channel==2&&r.rd105OutputEnabled==1&&r.rd105OutputRaw&&*r.rd105OutputRaw<0;
    }return stopped&&running;}),"disabling RD105 channel one clears only its output percentage");
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
    {
        auto scanSimulator=makeFpgaUsbSimulator(nullptr,FpgaSimulatorStartup::Idle,FpgaSimulatorFirmware::Hcd2Ai8Scan);
        auto* scanTransport=scanSimulator.get();check(scanTransport->open(),"scan-only HCD2 fixture opens");
        FpgaVlp1::StreamParser parser;quint32 sequence=10000;
        const auto wireExchange=[&](const std::vector<std::uint8_t>& request) {
            const auto expected=qFromLittleEndian<quint32>(request.data()+12);
            scanTransport->write(QByteArray(reinterpret_cast<const char*>(request.data()),qsizetype(request.size())));
            std::vector<std::uint8_t> payload;bool received=false;
            check(waitFor([&] {QByteArray part;scanTransport->read(part,16384);
                for(const auto& frame:parser.feed(reinterpret_cast<const std::uint8_t*>(part.constData()),part.size()))
                    if(frame.header.frameType==FpgaVlp1::FrameType::Response&&frame.header.sequence==expected){payload=frame.payload;received=true;}
                return received;}),"scan-only register request completes");
            return payload;
        };
        const auto wireWord=[&](const std::vector<std::uint8_t>& payload,int offset=0) {
            check(payload.size()>=std::size_t(offset+4),"scan-only response has a complete word");
            return payload.size()>=std::size_t(offset+4)?qFromLittleEndian<quint32>(payload.data()+offset):0u;
        };
        check(wireWord(wireExchange(FpgaVlp1::buildGetCapabilities(++sequence)),24)==0x2b,
            "scan-only firmware adds only AI8 scan to the HCD2 capability word");
        check(wireWord(wireExchange(FpgaVlp1::buildReadReg(++sequence,0x66fc,1)),12)==0x0b,
            "scan-only cache features exclude generic parameter transactions");
        check(wireWord(wireExchange(FpgaVlp1::buildReadReg(++sequence,0x6684,1)))==5 &&
            wireWord(wireExchange(FpgaVlp1::buildWriteReg(++sequence,0x6680,{1})))==5 &&
            wireWord(wireExchange(FpgaVlp1::buildReadReg(++sequence,0x6884,1)))==5,
            "scan-only firmware does not expose withdrawn AI8 or RD105 parameter registers");
        check(wireWord(wireExchange(FpgaVlp1::buildReadReg(++sequence,0x66b8,2)),12)==0 &&
            wireWord(wireExchange(FpgaVlp1::buildWriteReg(++sequence,0x66b8,{1})))==6,
            "new per-channel counters start at zero and are read-only");
        scanTransport->close();
        FpgaDeviceController scanController(std::move(scanSimulator));
        QVector<FpgaSensor::Reading> scanReadings;
        QObject::connect(&scanController,&FpgaDeviceController::measurementUpdated,&app,
            [&](const FpgaSensor::AdaptedMeasurements& m){scanReadings.append(m.reading);});
        scanController.connectDevice({},"simulator",FpgaDeviceController::ConnectMode::PreserveRunningModules);
        check(waitFor([&]{return scanController.ready()&&!scanController.busy();}),"scan-only HCD2 handshake succeeds");
        check(scanController.extensions()==0x2b && scanController.hardwareValues().value(0x6800)==0x00480100 &&
            !scanController.hardwareValues().contains(0x6684) && !scanController.hardwareValues().contains(0x6884),
            "scan capability keeps LC identification and avoids unsupported parameter reads");
        const auto scanCall=[&](const QJsonObject& request) {
            bool finished=false;QJsonObject response;
            const auto observer=QObject::connect(&scanController,&FpgaDeviceController::extensionCompleted,&app,
                [&](const QJsonObject& value){response=value;finished=true;});
            scanController.executeOperation(request);check(waitFor([&]{return finished;}),"scan-only operation completes");
            QObject::disconnect(observer);return response;
        };
        auto scanConfiguration=FpgaControlConfig{};scanConfiguration.ai8.scan=true;
        scanController.setConfiguration(scanConfiguration);scanController.setSensorEnabled(0x46,true);
        check(waitFor([&]{return !scanController.busy();}) && scanController.hardwareValues().value(0x6668)==1,
            "0x2b firmware configures eight-channel scanning without legacy parameter capabilities");
        check(waitFor([&]{QSet<quint32> channels;for(const auto& r:scanReadings)
            if(r.source==0x46&&r.schema==3&&r.ai8Channel&&r.ai8SampleCounter.value_or(0)>0)channels.insert(*r.ai8Channel);
            return channels.size()==8;}),"scan-only firmware delivers all eight channel-tagged snapshots with counters");
        check(!scanCall({{"op","instrument"},{"source",0x46},{"action",1},{"address",0},{"count",1},{"data",""}}).value("ok").toBool() &&
            !scanCall({{"op","instrument"},{"source",0x48},{"action",1},{"address",0x1000},{"count",1},{"data",""}}).value("ok").toBool(),
            "scan-only controller rejects generic AI8 parameters and independent RD105 operations");
        const auto targeted=scanCall({{"op","device_temperature"},{"source",0x46},{"channel",8},{"celsius",31.6}});
        check(targeted.value("ok").toBool() && targeted.value("verified").toBool(),
            "scan-only capability permits the existing targeted setpoint with independent confirmation");
        check(waitFor([&]{for(const auto& r:scanReadings)
            if(r.ai8Channel==8 && r.ai8SpRaw==316)return true;return false;}),"targeted scan-channel setpoint appears in its own snapshot");
        const auto cache=scanCall({{"op","ai8_cache"},{"channel",8}});
        check(cache.value("ok").toBool() && (cache.value("status").toInt()&3)==3 &&
            (quint32(cache.value("pv_sp").toDouble())>>16)==316,
            "scan-only cached channel preserves its quality and targeted setpoint");
        scanController.setSensorEnabled(0x46,false);check(waitFor([&]{return !scanController.busy();}),"scan-only channel disables");
        scanController.disconnectDevice();
        check(scanTransport->open(),"scan-only transport reopens for retained diagnostic counters");
        parser=FpgaVlp1::StreamParser{};
        check(wireWord(wireExchange(FpgaVlp1::buildReadReg(++sequence,0x66b8,2)),12)>0 &&
            wireWord(wireExchange(FpgaVlp1::buildReadReg(++sequence,0x66bc,1)),12)==0,
            "disabling and reconnecting retain per-channel success and failure counters");
        std::vector<std::uint8_t> resetPayload(8,0);
        check(wireWord(wireExchange(FpgaVlp1::buildFrame(FpgaVlp1::FrameType::Command,++sequence,0x46,8,resetPayload)))==0 &&
            wireWord(wireExchange(FpgaVlp1::buildReadReg(++sequence,0x66b8,2)),12)==0,
            "AI8 module reset clears the new cached counters");
        scanTransport->close();
    }
    auto old=makeFpgaUsbSimulator();old->injectFault("legacy_extensions");FpgaDeviceController legacy(std::move(old));
    legacy.connectDevice({},"simulator",FpgaDeviceController::ConnectMode::PreserveRunningModules);
    check(waitFor([&]{return legacy.ready()&&!legacy.busy();}),"legacy extension word remains compatible");
    check(legacy.hardwareValues().value(0x6800)==0x00480100 && !legacy.hardwareValues().contains(0x6884)
        && !legacy.hardwareValues().contains(0x5058),"reduced extension capabilities retain read-only LC identification without accessing RD105 parameters or DLIA cutoff registers");
    legacy.disconnectDevice();
    qInfo()<<"FPGA extension failures:"<<errors;return errors?1:0;
}
