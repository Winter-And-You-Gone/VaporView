#include "FpgaDeviceController.h"
#include "FpgaUsbSimulator.h"
#include "FpgaVlp1.h"
#include "FpgaTelemetry.h"
#include "FpgaExtensions.h"
#include "FpgaDliaSos.h"
#include "TelemetryCodec.h"
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
#include <complex>

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
    {
        const auto words=FpgaDliaSos::design(3,1000000,1250);
        check(words.size()==30 && words[0]==0x406d9806 && words[1]==0x300d0000 && words[2]==0x000080db,
            "SOS reference order-3 coefficient packing matches firmware handoff");
        for(int order=2;order<=8;++order)for(quint32 hz:{100u,1250u,10000u,499999u})
            check(FpgaDliaSos::design(order,1000000,hz).size()==30,"all supported orders produce stable complete banks");
        for(int order=2;order<=8;++order) {
            const auto bank=FpgaDliaSos::design(order,1000000,1250);
            const auto z=std::polar(1.0,-2*std::acos(-1.0)*1250/1000000);
            std::complex<double> gain=1;
            for(int section=0;section<4;++section) {
                double coefficient[5]{};
                for(int i=0;i<5;++i) {
                    quint64 raw=0;
                    for(int b=0;b<6;++b) {const int index=(section*5+i)*6+b;raw|=quint64((bank[index/4]>>(8*(index%4)))&255)<<(8*b);}
                    const qint64 value=(raw&(quint64(1)<<47))?qint64(raw)-(qint64(1)<<48):qint64(raw);
                    coefficient[i]=double(value)/FpgaDliaSos::One;
                }
                gain*=(coefficient[0]+coefficient[1]*z+coefficient[2]*z*z)/(1.0+coefficient[3]*z+coefficient[4]*z*z);
            }
            check(std::abs(std::abs(gain)-std::sqrt(0.5))<1e-6,"quantized filter has the requested Butterworth -3dB cutoff for every order");
        }
        check(FpgaDliaSos::design(1,1000000,1000).isEmpty() && FpgaDliaSos::design(4,1000,500).isEmpty(),"invalid order and Nyquist rejected");
        FpgaDeviceController modern(makeFpgaUsbSimulator(nullptr,FpgaSimulatorStartup::Idle,FpgaSimulatorFirmware::SosPps));
        modern.connectDevice({},"simulator",FpgaDeviceController::ConnectMode::PreserveRunningModules);
        check(waitFor([&]{return modern.ready()&&!modern.busy();}),"SOS/PPS API discovery connects");
        const auto run=[&](QJsonObject request) {
            check(waitFor([&]{return !modern.busy();}),"modern controller idle");
            static int ticket=0;request["ticket"]=QString::number(++ticket);
            QJsonObject result;bool done=false;
            const auto observer=QObject::connect(&modern,&FpgaDeviceController::extensionCompleted,&app,[&](const QJsonObject& r){if(r.value("ticket")==request.value("ticket")){result=r;done=true;}});
            modern.executeOperation(request);check(waitFor([&]{return done;}),"modern operation finishes");QObject::disconnect(observer);return result;
        };
        auto result=run({{"op","dlia_sos"},{"channel",0},{"order",3},{"hz",1250}});
        check(result.value("channel").toInt(-1)==0 && result.value("words_q46").isArray(),"SOS recording identifies the channel and coefficient bank");
        check(result.value("ok").toBool() && result.value("verified").toBool() && modern.hardwareValues().value(0x5024)==0x80000003u,"SOS upload commits and checks active profile");
        modern.refresh();check(waitFor([&]{return !modern.busy();}) && modern.ready(),"custom profile survives version validation");
        modern.applyConfiguration(FpgaControlConfig{},"dlia");
        check(waitFor([&]{return !modern.busy();}) && modern.hardwareValues().value(0x5024)==0x80000003u,"ordinary DLIA settings preserve SOS");
        result=run({{"op","wms_trigger"},{"channel",0},{"mode",1},{"idle",-123456}});
        check(result.value("mode").toInt()==1 && result.value("idle_q31").toInt()==-123456,"PPS recording retains applied signed configuration");
        check(result.value("ok").toBool() && modern.hardwareValues().value(0x204c)==1 && qint32(modern.hardwareValues().value(0x2050))==-123456,"PPS mode and signed idle roundtrip");
        result=run({{"op","pps_snapshot"},{"channel",0}});
        check(result.contains("accepted") && result.contains("completed") && result.contains("aborted_count"),"PPS recording retains coherence counters");
        check(result.value("ok").toBool() && !result.value("window_valid").toBool(),"no-trigger snapshot cannot claim valid scan");
        result=run({{"op","dlia_cutoff"},{"channel",0},{"hz",1000}});
        check(result.value("ok").toBool() && modern.hardwareValues().value(0x5024)==0x00040102,"explicit builtin restoration");
        modern.disconnectDevice();
        Ai8TemperatureControllerProtocol::LiveData live,parsed;live.sampleTicksValid=true;live.sampleTicks[4]=1234567890123;
        check(TelemetryCodec::parseAi8TemperatureControllerStatus(TelemetryCodec::serializeAi8TemperatureControllerStatus(live),parsed)
            && parsed.sampleTicksValid && parsed.sampleTicks==live.sampleTicks,"AI8 per-channel sample identities survive remote telemetry");
    }
    auto simulator=makeFpgaUsbSimulator(nullptr,FpgaSimulatorStartup::Idle,FpgaSimulatorFirmware::HistoricalExtensions);auto* transport=simulator.get();
    FpgaDeviceController controller(std::move(simulator));
    QVector<FpgaSensor::Reading> readings;
    TemperatureControllerData temperature;int temperatureUpdates=0;
    QVector<FpgaVlp1::Frame> commands;
    QObject::connect(&controller,&FpgaDeviceController::temperatureDataUpdated,&app,[&](const TemperatureControllerData& data){temperature=data;++temperatureUpdates;});
    QObject::connect(&controller,&FpgaDeviceController::rawCommand,&app,[&](quint64,const QByteArray& bytes) {
        FpgaVlp1::StreamParser parser;
        for(const auto& command:parser.feed(reinterpret_cast<const std::uint8_t*>(bytes.constData()),size_t(bytes.size())))commands.append(command);
    });
    QObject::connect(&controller,&FpgaDeviceController::measurementUpdated,&app,[&](const FpgaSensor::AdaptedMeasurements& m){readings.append(m.reading);});
    controller.connectDevice({},"simulator",FpgaDeviceController::ConnectMode::PreserveRunningModules);
    check(waitFor([&]{return controller.ready()&&!controller.busy();}),"new firmware handshake");
    check(controller.extensions()==0x3f,"GET_CAPABILITIES extension bits");
    auto historic=FpgaControlConfig{};historic.ai8.scan=true;controller.setConfiguration(historic);
    const auto call=[&](const QJsonObject& request) {
        check(waitFor([&]{return !controller.busy();}),"extension waits for the current UART poll");
        static quint64 nextTicket=0;const auto ticket=QString::number(++nextTicket);
        auto operation=request;operation["ticket"]=ticket;
        bool finished=false;QJsonObject result;
        const auto observer=QObject::connect(&controller,&FpgaDeviceController::extensionCompleted,&app,[&](const QJsonObject& value){if(value.value("ticket").toString()==ticket){result=value;finished=true;}});
        controller.executeOperation(operation);
        check(waitFor([&]{return finished;}),"extension completed without hanging");QObject::disconnect(observer);
        return result;
    };
    const auto param=[&](int source,int action,int address,int count,const QByteArray& data=QByteArray{}) {
        return call(QJsonObject{{"op","instrument"},{"source",source},{"action",action},{"address",address},{"count",count},{"data",QString::fromLatin1(data.toBase64())}});
    };
    const auto writes=[&] {
        int count=0;for(const auto& command:commands)if(command.header.message==3)++count;return count;
    };
    const int beforeLegacy=writes();
    auto result=param(0x4a,2,0x1100,1,QByteArray::fromHex("0001"));
    check(!result.value("ok").toBool(),"legacy 6A instrument writes are rejected");
    result=call({{"op","device_temperature"},{"source",0x4a},{"celsius",26.12345}});
    check(!result.value("ok").toBool() && writes()==beforeLegacy,
        "legacy temperature requests send zero register writes");
    controller.setSensorEnabled(0x4a,true);
    check(waitFor([&]{return controller.temperatureConnected() && temperature.valid && std::isfinite(temperature.channels[0].measured_temperature_c) && std::isfinite(temperature.channels[1].measured_temperature_c);}),"both physical TEC103 instruments identify and deliver typed data");
    check(temperatureUpdates>0 && std::isfinite(temperature.channels[0].measured_temperature_c)
        && std::isfinite(temperature.channels[1].measured_temperature_c),"both GPIO channels survive typed temperature delivery");
    check(std::abs(temperature.channels[0].target_temperature_c-27.3)<1e-8
        && std::abs(temperature.channels[1].target_temperature_c-26.0)<1e-8,
        "two physical TC1 targets remain independent");
    bool legacyReading=false;for(const auto& reading:readings)legacyReading|=reading.source==0x4a;
    check(!legacyReading,"live GPIO temperatures do not fabricate legacy 6A measurement frames");
    check(!(controller.hardwareValues().value(0x6604)&1),"TEC103 GPIO connection does not enable AI8");
    check(waitFor([&]{return controller.busy();}),"native periodic polling owns the local controller");
    const int queuedCommandStart=commands.size();const QString queuedTicket=QStringLiteral("1000000");
    QJsonObject queuedReply;bool queuedFinished=false;
    const auto queuedObserver=QObject::connect(&controller,&FpgaDeviceController::extensionCompleted,&app,
        [&](const QJsonObject& response) {
            if(response.value("ticket").toString()==queuedTicket){queuedReply=response;queuedFinished=true;}
        });
    controller.executeOperation({{"op","uart_exchange"},{"channel",1},{"ticket",queuedTicket},
        {"data",QString::fromLatin1(QByteArray("TEC=?@").toBase64())},
        {"timeout_ms",1200},{"max_rx",2048},{"quiet_ms",30}});
    check(waitFor([&]{return queuedFinished;}),"local native request waits for the internal poll to yield");
    QObject::disconnect(queuedObserver);
    check(queuedReply.value("ok").toBool() && QByteArray::fromBase64(queuedReply.value("data").toString().toLatin1()).contains("TEC=103@"),
        "local request receives its own native identity response");
    QByteArray queuedTx;
    for(int i=queuedCommandStart;i<commands.size();++i) {
        const auto& command=commands[i];
        if(command.header.message==3 && command.payload.size()>=12
            && qFromLittleEndian<quint32>(command.payload.data())==0x6818)
            queuedTx.append(char(qFromLittleEndian<quint32>(command.payload.data()+8)));
    }
    check(queuedTx.count("TEC=?@")==1,"local deferred identity request reaches the physical UART exactly once");
    check(waitFor([&]{return !controller.busy();}),"temperature polling yields before AI8 action");
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
    check(snapshot.status==LocalTemperatureCommandStatus::Confirmed && snapshot.latestData.valid,"TEC103 full query uses native GPIO protocol");
    check(std::isfinite(snapshot.latestData.channels[0].target_temperature_c)
        && std::isfinite(snapshot.latestData.channels[1].target_temperature_c),"status query reads both physical internal TC1 channels");
    auto signedOutput=snapshot.latestData;signedOutput.channels[1].output_percent=-18.0;
    TemperatureControllerData remoteTemperature;
    check(TelemetryCodec::parseTemperatureControllerStatus(TelemetryCodec::serializeTemperatureControllerStatus(signedOutput),remoteTemperature)
        && remoteTemperature.channels[1].output_percent==-18.0,"typed remote temperature telemetry preserves signed output");
    TemperatureControllerCommand secondChannel;secondChannel.channel=2;
    const auto dualSnapshot=sendFpgaTemperatureCommand(call,CommandId::QueryDeviceStatus,secondChannel);
    check(dualSnapshot.status==LocalTemperatureCommandStatus::Confirmed
        && std::isfinite(dualSnapshot.latestData.channels[0].target_temperature_c)
        && std::isfinite(dualSnapshot.latestData.channels[1].target_temperature_c),"status query preserves both physical channels for either UI selection");
    check(waitFor([&]{return !controller.busy();}),"temperature polling yields before EPSILON action");
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
    result=param(0x4a,3,0,1,QByteArray::fromHex("0001"));check(!result.value("ok").toBool(),"unsupported factory reset is rejected");
    check(controller.temperatureConnected() && controller.hardwareValues().value(0x6804)==1
        && controller.hardwareValues().value(0x6904)==1,"rejected factory reset preserves GPIO temperature polling");
    for(const auto& command:commands)if(command.header.message==3 && command.payload.size()>=8) {
        const auto address=qFromLittleEndian<quint32>(command.payload.data());
        check(address<0x6a00 || address>=0x6b00,"field integration never writes the legacy 6A page");
    }
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
            wireWord(wireExchange(FpgaVlp1::buildReadReg(++sequence,0x6a84,1)))==5,
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
            !scanController.hardwareValues().contains(0x6684) && !scanController.hardwareValues().contains(0x6a84),
            "scan capability keeps LC identification and avoids unsupported parameter reads");
        const auto scanCall=[&](const QJsonObject& request) {
            check(waitFor([&]{return !scanController.busy();}),"scan-only operation waits for the current poll");
            static quint64 nextTicket=0;const auto ticket=QString::number(++nextTicket);
            auto operation=request;operation["ticket"]=ticket;
            bool finished=false;QJsonObject response;
            const auto observer=QObject::connect(&scanController,&FpgaDeviceController::extensionCompleted,&app,
                [&](const QJsonObject& value){if(value.value("ticket").toString()==ticket){response=value;finished=true;}});
            scanController.executeOperation(operation);check(waitFor([&]{return finished;}),"scan-only operation completes");
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
            !scanCall({{"op","instrument"},{"source",0x4a},{"action",1},{"address",0x1000},{"count",1},{"data",""}}).value("ok").toBool(),
            "scan-only controller rejects withdrawn AI8 parameter transactions and the legacy 6A path");
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
        scanController.connectSensor({{"op","sensor"},{"source",0x4a},{"enabled",true}});
        check(waitFor([&]{return scanController.temperatureConnected();}),
            "0x2b firmware supports TEC103 through raw LC bridges without the legacy RD105 capability");
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
    {
        FpgaDeviceController production(makeFpgaUsbSimulator());
        TemperatureControllerData productionData;
        QObject::connect(&production,&FpgaDeviceController::temperatureDataUpdated,&app,
            [&](const TemperatureControllerData& value){productionData=value;});
        production.connectDevice({},"simulator",FpgaDeviceController::ConnectMode::PreserveRunningModules);
        check(waitFor([&]{return production.ready()&&!production.busy();}),"production profile handshakes");
        check(production.extensions()==0x3b && production.hardwareValues().value(0x6a00)==0x004a0103
            && production.hardwareValues().value(0x6800)==0x00480100 && production.hardwareValues().value(0x6900)==0x00490100,
            "legacy 6A and raw LC bridges retain their distinct FPGA identities");
        const auto relayCall=[&](const QJsonObject& request) {
            check(waitFor([&]{return !production.busy();}),"production operation waits for the current GPIO poll");
            static quint64 nextTicket=0;const auto ticket=QString::number(++nextTicket);
            auto operation=request;operation["ticket"]=ticket;
            bool done=false;QJsonObject reply;
            const auto observer=QObject::connect(&production,&FpgaDeviceController::extensionCompleted,&app,
                [&](const QJsonObject& value){if(value.value("ticket").toString()==ticket){reply=value;done=true;}});
            production.executeOperation(operation);check(waitFor([&]{return done;}),"production transaction finishes");
            QObject::disconnect(observer);return reply;
        };
        production.connectSensor({{"op","sensor"},{"source",0x4a},{"enabled",true}});
        check(waitFor([&]{return production.temperatureConnected() && productionData.valid && std::isfinite(productionData.channels[0].measured_temperature_c) && std::isfinite(productionData.channels[1].measured_temperature_c);}),
            "production GPIO connection identifies both independent TEC103 instruments");
        check(production.hardwareValues().value(0x6804)==1 && production.hardwareValues().value(0x6904)==1
            && production.hardwareValues().value(0x6810)==38400 && production.hardwareValues().value(0x6910)==38400
            && production.hardwareValues().value(0x6a04)==0,"GPIO connection enables only the two native UART bridges");
        TemperatureControllerCommand target;target.channel=2;target.target_temperature_c=-12.34567;
        auto command=sendFpgaTemperatureCommand(relayCall,CommandId::SetTemperatureTarget,target);
        check(command.status==LocalTemperatureCommandStatus::Confirmed,"TEC103 physical channel two negative setpoint is verified");
        TemperatureControllerCommand query;query.channel=2;
        const auto full=sendFpgaTemperatureCommand(relayCall,CommandId::QueryDeviceStatus,query);
        check(full.status==LocalTemperatureCommandStatus::Confirmed && std::abs(full.latestData.channels[1].target_temperature_c+12.34567)<1e-8
            && std::abs(full.latestData.channels[0].target_temperature_c-27.3)<1e-8
            && full.latestData.device_address==1 && full.latestData.rs485_baud_index==-1,
            "TEC103 query preserves both physical TC1 channels and does not invent shared RS485 settings");
        int forbiddenCalls=0;
        const auto forbidden=[&](const QJsonObject&){++forbiddenCalls;return QJsonObject{};};
        for(auto id:{CommandId::SetTemperatureDeviceAddress,CommandId::SetTemperatureRs485Baud,CommandId::RestoreTemperatureFactoryDefaults})
            check(sendFpgaTemperatureCommand(forbidden,id,{}).status==LocalTemperatureCommandStatus::Rejected,
                "unsupported GPIO communication maintenance is rejected before transport");
        check(forbiddenCalls==0,"TEC103 maintenance never sends a destructive transaction");
        production.disconnectDevice();
    }
    auto old=makeFpgaUsbSimulator(nullptr,FpgaSimulatorStartup::Idle,FpgaSimulatorFirmware::CurrentHcd2);old->injectFault("legacy_extensions");FpgaDeviceController legacy(std::move(old));
    legacy.connectDevice({},"simulator",FpgaDeviceController::ConnectMode::PreserveRunningModules);
    check(waitFor([&]{return legacy.ready()&&!legacy.busy();}),"legacy extension word remains compatible");
    check(legacy.hardwareValues().value(0x6800)==0x00480100 && !legacy.hardwareValues().contains(0x6a84)
        && !legacy.hardwareValues().contains(0x5058),"reduced extension capabilities retain read-only LC identification without accessing RD105 parameters or DLIA cutoff registers");
    legacy.disconnectDevice();
    {
        auto partialSimulator=makeFpgaUsbSimulator();auto* partialTransport=partialSimulator.get();
        FpgaDeviceController partial(std::move(partialSimulator));TemperatureControllerData partialData;
        QString partialDetail;QVector<LogRecord> partialLogs;
        QObject::connect(&partial,&FpgaDeviceController::temperatureDataUpdated,&app,
            [&](const TemperatureControllerData& value){partialData=value;});
        QObject::connect(&partial,&FpgaDeviceController::temperatureConnectionChanged,&app,
            [&](bool,const QString& detail){partialDetail=detail;});
        QObject::connect(&partial,&FpgaDeviceController::logRecordGenerated,&app,
            [&](const LogRecord& value){partialLogs.append(value);});
        partial.connectDevice({},"simulator",FpgaDeviceController::ConnectMode::PreserveRunningModules);
        check(waitFor([&]{return partial.ready()&&!partial.busy();}),"partial-wiring fixture connects");
        partialTransport->injectFault("tec103_lc1_disconnected");
        partial.connectSensor({{"op","sensor"},{"source",0x4a},{"enabled",true}});
        check(waitFor([&]{return partial.temperatureConnected() && partialData.valid
            && std::isfinite(partialData.channels[0].measured_temperature_c)
            && std::isnan(partialData.channels[1].measured_temperature_c);}),
            "one connected TEC103 remains usable while the second signal wire is absent");
        check(partialDetail.contains(QStringLiteral("LC0：已连接")) && partialDetail.contains(QStringLiteral("LC1：未连接"))
            && partialData.device_address==-1 && partialData.controller_mode==-1 && partialData.overtemp_output_mode==-1,
            "partial connection reports each port and never invents shared metadata");
        check(waitFor([&] {bool partialSucceeded=false;for(const auto& record:partialLogs)
            partialSucceeded|=record.fields.value("reason_code")=="TEC103_PARTIAL_CONNECTION"
                && record.fields.value("result")=="succeeded";
        return partialSucceeded;}),"the available instrument completes the connection with an explicit partial result");
        partialTransport->injectFault("tec103_lc1_reconnected");
        check(waitFor([&]{return partial.temperatureConnected() && partialData.valid
            &&std::isfinite(partialData.channels[1].measured_temperature_c)
            &&partialDetail.contains(QStringLiteral("双 TEC103 已连接"));}),
            "reconnecting the missing signal restores both ports without reopening the available instrument");
        check(partialData.device_address==1 && partialData.controller_mode==0,
            "shared metadata becomes known only after both instruments reply consistently");
        partial.disconnectDevice();
    }
    {
        auto motorSimulator=makeFpgaUsbSimulator();auto* motorTransport=motorSimulator.get();
        FpgaDeviceController motorController(std::move(motorSimulator));QObject receiver;
        QByteArray tx;int heartbeats=0;bool queryFailed=false;
        QObject::connect(&motorController,&FpgaDeviceController::rawCommand,&receiver,[&](quint64,const QByteArray& bytes) {
            FpgaVlp1::StreamParser parser;
            for(const auto& frame:parser.feed(reinterpret_cast<const std::uint8_t*>(bytes.constData()),size_t(bytes.size()))) {
                if(frame.header.message==12 && frame.header.source==0x47 && frame.payload.size()>=8
                    &&qFromLittleEndian<quint32>(frame.payload.data())==5)++heartbeats;
                if(frame.header.message==3 && frame.payload.size()>=12
                    &&qFromLittleEndian<quint32>(frame.payload.data())==0x6818)
                    tx.append(char(qFromLittleEndian<quint32>(frame.payload.data()+8)));
            }
        });
        QObject::connect(&motorController,&FpgaDeviceController::extensionCompleted,&receiver,[&](const QJsonObject& result) {
            if(result.value("ticket").toString()=="1")queryFailed=!result.value("ok").toBool();
        });
        motorController.connectDevice({},"simulator",FpgaDeviceController::ConnectMode::PreserveRunningModules);
        check(waitFor([&]{return motorController.ready()&&!motorController.busy();}),"motor isolation fixture connects");
        const auto operate=[&](const QJsonObject& request) {
            check(waitFor([&]{return !motorController.busy();}),"motor fixture waits before its next action");
            motorController.executeOperation(request);
            check(waitFor([&]{return !motorController.busy();}),"motor fixture action completes");
        };
        operate({{"op","motor"},{"action",4},{"value",1}});
        // Configure via the same public sensor path; keep a real owner heartbeat
        // flowing while the independently identified instrument stops replying.
        QTimer heartbeat;heartbeat.setInterval(50);
        QObject::connect(&heartbeat,&QTimer::timeout,&receiver,[&] {
            motorController.executeOperation({{"op","motor_heartbeat"}});
        });heartbeat.start();
        motorController.connectSensor({{"op","sensor"},{"source",0x4a},{"enabled",true}});
        check(waitFor([&]{return motorController.temperatureConnected();}),"motor fixture identifies both TEC103 instruments");
        check(waitFor([&]{return !motorController.busy();}),"motor fixture obtains a UART transaction slot");
        const int beforeTimeout=heartbeats;tx.clear();motorTransport->injectFault("tec103_timeout");
        motorController.executeOperation({{"op","uart_exchange"},{"channel",1},{"ticket","1"},
            {"data",QString::fromLatin1(QByteArray("TEC=?@").toBase64())},
            {"timeout_ms",200},{"max_rx",2048},{"quiet_ms",20}});
        check(waitFor([&]{return queryFailed;}),"a nonresponding TEC103 query fails without retry");
        const int afterTimeout=heartbeats;
        check(waitFor([&]{return heartbeats>afterTimeout+2;}),"owner heartbeats continue after a device-level UART timeout");
        check(heartbeats>beforeTimeout && tx.count("TEC=?@")==1,"UART timeout neither revokes motor ownership nor retransmits the instrument query");
        operate({{"op","motor"},{"action",4},{"value",0}});heartbeat.stop();motorController.disconnectDevice();
    }
    qInfo()<<"FPGA extension failures:"<<errors;return errors?1:0;
}
