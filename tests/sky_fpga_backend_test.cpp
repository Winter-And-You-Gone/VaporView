#include "SkyFpgaBackend.h"
#include "sky_fpga_test_transport.h"

void diagnosticReadback()
{
    auto mock=std::make_unique<ScriptedTransport>();auto *wire=mock.get();
    mock->regs[0x5024]=0;
    SkyFpgaBackend backend(std::move(mock));
    const auto operation=[&](const QJsonObject& request,CommandErrorCode expected) {
        bool done=false;QJsonObject result;
        backend.submit(request,[&](CommandErrorCode code,QString,QJsonObject status) {
            require(code==expected,"diagnostic operation returns the expected final result");result=status;done=true;
        });
        waitUntil([&]{return done;},"diagnostic operation completes");return result;
    };
    operation({{"op","refresh"}},CommandErrorCode::DeviceNotConnected);
    auto status=operation({{"op","connect"},{"backend","auto"},{"locator","mock"}},CommandErrorCode::ConfigApplyFailed);
    require(status.value("connected").toBool()&&!status.value("ready").toBool(),"failed handshake retains the USB connection for read-only diagnostics");
    wire->clear();wire->regs[0x6010]=9600;
    status=operation({{"op","refresh"}},CommandErrorCode::ConfigApplyFailed);
    require(status.value("registers").toObject().value(QString::number(0x6010)).toInt()==9600,
        "Sky refresh reads fresh registers after a failed handshake, just like local refresh");
    require(!status.value("ready").toBool()&&!wire->commands.isEmpty(),"diagnostic readback cannot bypass failed connection recovery");
    for(const auto& command:wire->commands)require(command.message==2,"diagnostic refresh sends only register reads");
    const auto commandCount=wire->commands.size();
    operation({{"op","sensor_config"},{"source",0x40},{"baud",4800}},CommandErrorCode::DeviceNotConnected);
    require(wire->commands.size()==commandCount,"unready Sky controller still blocks parameter writes");
    wire->regs[0x5024]=0x00040101;
    status=operation({{"op","refresh"}},CommandErrorCode::ConfigApplyFailed);
    require(!status.value("ready").toBool()&&wire->openCount.load()==1,"refresh never reconnects or silently resumes failed recovery");
    operation({{"op","disconnect"}},CommandErrorCode::Ok);
    status=operation({{"op","connect"},{"backend","auto"},{"locator","mock"}},CommandErrorCode::Ok);
    require(status.value("ready").toBool(),"explicit reconnect performs complete recovery before enabling writes");
    backend.shutdown();
}

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    diagnosticReadback();
    auto mock=std::make_unique<ScriptedTransport>(); auto *wire=mock.get(); mock->failTemperature=true;mock->failWriteAddress=0x2010;
    SkyFpgaBackend backend(std::move(mock));
    QObject::connect(&backend,&SkyFpgaBackend::logRecord,[](const LogRecord& record) {
        require(record.source=="SkyCore","shared controller diagnostics identify actual Sky execution owner");
    });
    int completions=0;
    bool discovered=false;
    backend.submit({{"op","discover"}},[&](CommandErrorCode code,QString,QJsonObject status) {
        require(code==CommandErrorCode::Ok && !status.value("connected").toBool(),"discovery works before USB connection");
        const auto devices=status.value("usb_discovery").toObject().value("devices").toArray();
        require(devices.size()==1 && devices.first().toObject().value("locator").toString()=="mock","discovery returns worker's device list");
        discovered=true;
    });
    waitUntil([&]{return discovered;},"read-only discovery completed");
    require(wire->openCount.load()==0 && wire->commands.isEmpty(),"discovery never opens USB or sends physical commands");
    require(!backend.statusDocument().contains("usb_discovery"),"paths are returned only on request, not periodic telemetry");
    backend.submit({{"op","connect"},{"backend","auto"},{"locator","mock"}},
        [&](CommandErrorCode code,QString,QJsonObject status) {
            require(code==CommandErrorCode::Ok && status.value("ready").toBool(),"handshake waits for verified versions"); ++completions;
        });
    require(completions==0,"queued USB connect cannot report success");
    backend.submit({{"op","refresh"}}, [&](CommandErrorCode code,QString,QJsonObject) {
        require(code==CommandErrorCode::DeviceOperationBusy,"second client cannot own active handshake");
    });
    waitUntil([&]{return completions==1;},"asynchronous handshake finished");
    require(wire->openCount.load()==1,"single USB owner opens once");
    const auto sensorRegisters=backend.statusDocument().value("registers").toObject();
    require(sensorRegisters.value(QString::number(0x6010)).toInt()==4800
        &&sensorRegisters.value(QString::number(0x6210)).toInt()==460800
        &&sensorRegisters.value(QString::number(0x6510)).toInt()==115200
        &&sensorRegisters.value(QString::number(0x6414)).toInt()==2000,
        "Sky status forwards actual sensor baud and polling register readbacks to ground");
    bool settingsDone=false;
    backend.submit({{"op","sensor"},{"source",0x40},{"enabled",true},{"baud",9600},{"poll_ms",250}},
        [&](CommandErrorCode code,QString,QJsonObject status) {
            const auto values=status.value("registers").toObject();
            require(code==CommandErrorCode::Ok && values.value(QString::number(0x6010)).toInt()==9600
                && values.value(QString::number(0x6018)).toInt()==250 && values.value(QString::number(0x6004)).toInt()==1,
                "Sky connects only after staged baud and polling have verified readbacks");settingsDone=true;
        });
    require(!settingsDone,"parameter submission is not success at enqueue");
    waitUntil([&]{return settingsDone;},"remote parameter write and readback completed");
    settingsDone=false;
    backend.submit({{"op","sensor"},{"source",0x41},{"enabled",true},{"poll_ms",500},{"parameters",QJsonObject{{"address",241}}}},
        [&](CommandErrorCode code,QString,QJsonObject status) {
            const auto values=status.value("registers").toObject();
            const auto config=FpgaControlConfig::fromJson(status.value("configuration").toObject());
            require(code==CommandErrorCode::Ok && values.value(QString::number(0x6114)).toInt()==241
                && values.value(QString::number(0x611c)).toInt()==500 && values.value(QString::number(0x6104)).toInt()==1
                && config.sensorParameters.value(0x41).value("address").toInt()==241,
                "Sky HMP connects after advanced parameter verification and synchronizes confirmed configuration");settingsDone=true;
        });
    waitUntil([&]{return settingsDone;},"remote HMP advanced configuration completed");
    settingsDone=false;
    backend.submit({{"op","sensor"},{"source",0x46},{"enabled",true},{"poll_ms",500}},
        [&](CommandErrorCode code,QString,QJsonObject status) {
            require(code==CommandErrorCode::Ok && FpgaControlConfig::fromJson(status.value("configuration").toObject()).ai8.pollIntervalMs==500,
                "confirmed AI8 polling stays synchronized with the FPGA configuration page");settingsDone=true;
        });
    waitUntil([&]{return settingsDone;},"AI8 final parameter write completed");
    const auto payload=QByteArray::fromHex("0100010013000304c0d40100");
    for(int i=0;i<=1000;++i){
        const auto frame=FpgaVlp1::buildFrame(FpgaVlp1::FrameType::Data,i+1,0x45,0x1100,
            std::vector<std::uint8_t>(payload.begin(),payload.end()),true,1,quint64(i)*100000);
        wire->sensorWire.append(reinterpret_cast<const char*>(frame.data()),frame.size());
    }
    backend.submit({{"op","temperature"},{"celsius",25.0}},[&](CommandErrorCode code,QString error,QJsonObject) {
        require(code==CommandErrorCode::ConfigApplyFailed && !error.isEmpty(),"hardware failure delivered after FPGA response"); ++completions;
    });
    require(completions==1,"temperature cannot complete at enqueue");
    waitUntil([&]{return completions==2;},"failed temperature final callback");
    require(wire->temperatureWrites.load()==1,"failed write never automatically retries");
    waitUntil([&]{return backend.statusDocument().value("sensor_rates_hz").toObject().value("69").toDouble()==1000.0;},
        "Sky status retains raw sensor frequency despite presentation coalescing");
    const auto confirmedConfiguration=backend.statusDocument().value("configuration").toObject();
    FpgaControlConfig requested;requested.wms[0].scanMilliHz+=1000;
    backend.submit({{"op","configure"},{"configuration",requested.toJson()}},[&](CommandErrorCode code,QString error,QJsonObject status) {
        require(code==CommandErrorCode::ConfigApplyFailed && !error.isEmpty(),"partial configuration failure is explicit");
        require(status.value("configuration").toObject()==confirmedConfiguration,"failed requested config never becomes externally confirmed config");
        require(!status.value("registers").toObject().isEmpty() && !status.value("detail").toString().isEmpty(),"partial operation retains hardware readback evidence and failure detail");
        ++completions;
    });
    require(backend.statusDocument().value("configuration").toObject()==confirmedConfiguration,"queued configuration remains unconfirmed");
    waitUntil([&]{return completions==3;},"configuration failure final result");
    backend.submit({{"op","write_register"},{"address",1}},[&](CommandErrorCode code,QString,QJsonObject) {
        require(code==CommandErrorCode::InvalidPayload,"arbitrary register request rejected"); ++completions;
    });
    backend.submit({{"op","disconnect"}},[&](CommandErrorCode code,QString,QJsonObject status) {
        require(code==CommandErrorCode::Ok && !status.value("connected").toBool(),"disconnect final result clears USB state"); ++completions;
        require(status.value("sensor_rates_hz").toObject().isEmpty(),"Sky USB disconnect clears cached sensor rates");
    });
    waitUntil([&]{return completions==5;},"disconnect callback returned");
    backend.shutdown();
    return 0;
}
