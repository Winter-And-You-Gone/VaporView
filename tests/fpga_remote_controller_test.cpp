#include "ground/devices/RemoteSkyController.h"
#include "FpgaControlConfig.h"
#include "TcpTelemetryLink.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition,const char *message) {
    if(!condition){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}
}
template<typename Predicate> bool waitUntil(Predicate predicate) {
    QElapsedTimer timer;timer.start();
    while(timer.elapsed()<3000){QCoreApplication::processEvents(QEventLoop::AllEvents,20);if(predicate())return true;}
    return predicate();
}
}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    using namespace VaporView;
    using namespace VaporView::Ground::Devices;
    TcpTelemetryLink server;require(server.listen("127.0.0.1",0),"listen");
    RemoteSkyController controller;TelemetryCodec decoder;DeviceOperationRequest received;quint16 commandSeq=0;int commands=0;
    QObject::connect(&server,&TelemetryLink::bytesReceived,[&](const QByteArray& bytes){
        for(const auto& frame:decoder.feedBytes(bytes)) {
            CommandMessage command;
            if(frame.type==MsgType::Command && TelemetryCodec::parseCommand(frame.payload,command) &&
                command.command_id==CommandId::DeviceOperation && TelemetryCodec::parseDeviceOperationRequest(command.payload,received)) {
                commandSeq=command.command_seq;++commands;
            }
        }
    });
    require(controller.openTcp("127.0.0.1",server.localPort()),"open controller");
    require(waitUntil([&]{return controller.isOpen();}),"connected");
    int responses=0,statuses=0,sensors=0,waves=0;
    QObject::connect(&controller,&RemoteSkyController::deviceOperationResponseReceived,[&](const auto&){++responses;});
    QObject::connect(&controller,&RemoteSkyController::fpgaStatusUpdated,[&](const auto&){++statuses;});
    QObject::connect(&controller,&RemoteSkyController::fpgaSensorUpdated,[&](const auto&){++sensors;});
    QObject::connect(&controller,&RemoteSkyController::fpgaWaveformUpdated,[&](const auto&){++waves;});
    require(controller.sendFpgaControl({{"op","write_register"}})==0,"invalid command locally rejected");
    const auto id=controller.sendFpgaControl({{"op","refresh"}});
    require(id!=0 && waitUntil([&]{return commands==1;}),"valid control delivered");
    require(received.request_id==id && received.device_id==SkyDeviceId::Fpga && received.operation==DeviceOperation::FpgaControl,"typed control request");
    TelemetryCodec encoder;
    auto write=[&](MsgType type,const QByteArray& payload){server.writeBytes(encoder.encodeFrame(type,payload,1,1));};
    CommandAck ack;ack.command_id=CommandId::DeviceOperation;ack.command_seq=commandSeq;
    QElapsedTimer noRetry;noRetry.start();
    while(noRetry.elapsed()<1100) QCoreApplication::processEvents(QEventLoop::AllEvents,20);
    require(commands==1,"missing ACK never causes automatic FPGA control replay");
    write(MsgType::CommandAck,TelemetryCodec::serializeCommandAck(ack));
    DeviceOperationResponse response;response.request_id=id;response.device_id=SkyDeviceId::Fpga;response.operation=DeviceOperation::FpgaControl;
    write(MsgType::DeviceOperationResponse,TelemetryCodec::serializeDeviceOperationResponse(response));
    require(waitUntil([&]{return responses==1;}),"FPGA response not misclassified as EPSILON");
    require(controller.epsilonSettingsSupport()==DeviceOperationSupport::Unknown,"FPGA operation leaves EPSILON capability untouched");
    QJsonObject status{{"connected",true},{"ready",true},{"busy",false},{"detail","ready"},{"configuration",FpgaControlConfig{}.toJson()},
        {"registers",QJsonObject{}},{"archived_records","0"}};
    write(MsgType::FpgaStatus,FpgaRemote::encodeStatus(status));
    FpgaSensor::Reading sensor;sensor.source=0x44;sensor.schema=1;sensor.message=0x1100;
    write(MsgType::FpgaSensor,FpgaRemote::encodeSensor(sensor,1));
    FpgaWave::CompletedStream wave;wave.source=0x20;wave.message=0x1000;wave.schema=2;wave.format=0;wave.rate=1000;wave.totalPoints=1;wave.bytesPerPoint=4;wave.adcBits=24;wave.signed32Samples.append(-1);
    write(MsgType::FpgaWaveformPreview,FpgaRemote::encodeWaveformPreview(wave));
    require(waitUntil([&]{return statuses==1 && sensors==1 && waves==1;}),"all typed FPGA telemetry delivered");
    auto service=controller.telemetryService();
    emit service->fpgaStatusUpdated(status); // Queue a prior-generation event, then close before delivery.
    emit service->fpgaSensorUpdated(sensor);
    emit service->fpgaWaveformUpdated(wave);
    controller.close();QCoreApplication::processEvents();
    require(statuses==1 && sensors==1 && waves==1,"stale queued FPGA telemetry rejected after close");
    server.close();
    std::cout<<"fpga_remote_controller_test passed\n";
}
