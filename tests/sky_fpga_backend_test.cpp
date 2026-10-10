#include "SkyFpgaBackend.h"
#include "sky_fpga_test_transport.h"

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    auto mock=std::make_unique<ScriptedTransport>(); auto *wire=mock.get(); mock->failTemperature=true;mock->failWriteAddress=0x2010;
    SkyFpgaBackend backend(std::move(mock));
    QObject::connect(&backend,&SkyFpgaBackend::logRecord,[](const LogRecord& record) {
        require(record.source=="SkyCore","shared controller diagnostics identify actual Sky execution owner");
    });
    int completions=0;
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
    backend.submit({{"op","temperature"},{"celsius",25.0}},[&](CommandErrorCode code,QString error,QJsonObject) {
        require(code==CommandErrorCode::ConfigApplyFailed && !error.isEmpty(),"hardware failure delivered after FPGA response"); ++completions;
    });
    require(completions==1,"temperature cannot complete at enqueue");
    waitUntil([&]{return completions==2;},"failed temperature final callback");
    require(wire->temperatureWrites.load()==1,"failed write never automatically retries");
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
    });
    waitUntil([&]{return completions==5;},"disconnect callback returned");
    backend.shutdown();
    return 0;
}
