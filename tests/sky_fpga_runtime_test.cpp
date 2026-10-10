#include "SkyRuntime.h"
#include "FpgaTelemetry.h"
#include "sky_fpga_test_transport.h"
#include <QTemporaryDir>
#include <QTcpSocket>
#include <cmath>

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    QTemporaryDir directory; require(directory.isValid(),"isolated config directory");
    auto config=SkyConfig::defaults(); config.fpga.enabled=true; config.fpga.locator="mock";
    config.epsilon_rtcm.enabled=true; config.epsilon_rtcm.forward_port="FORBIDDEN_DIRECT_SERIAL";
    auto file=directory.filePath("sky.json"); require(config.saveToFile(file),"save FPGA deployment configuration");
    SkyRuntimeOptions options; options.config_path=file; options.telemetry_tcp_port=0;
    SkyRuntime runtime(options);
    quint16 telemetryPort=0;int connectedPeers=0,disconnectedPeers=0;
    QObject::connect(&runtime,&SkyRuntime::logRecord,[&](const LogRecord& record) {
        const QString event=record.fields.value("event").toString();
        if(event=="telemetry_tcp_server_listening") telemetryPort=quint16(record.fields.value("local_port").toUInt());
        if(event=="telemetry_tcp_client_connected") ++connectedPeers;
        if(event=="telemetry_tcp_client_disconnected") ++disconnectedPeers;
    });
    auto mock=std::make_unique<ScriptedTransport>();auto *wire=mock.get();
    std::vector<std::uint8_t> pressure;append16(pressure,1);append16(pressure,1);append16(pressure,0x12);
    pressure.push_back(7);pressure.push_back(4);append32(pressure,101459000);
    const auto sensor=FpgaVlp1::buildFrame(FpgaVlp1::FrameType::Data,99,0x40,0x1100,pressure,true,3,100000000,0);
    mock->sensorWire=QByteArray(reinterpret_cast<const char*>(sensor.data()),qsizetype(sensor.size()));
    bool pressureReceived=false,fpgaSensorReceived=false;
    QObject::connect(&runtime,&SkyRuntime::telemetryFrameReady,[&](MsgType type,const QByteArray& bytes) {
        if(type==MsgType::TelemetryBasic) { TelemetryBasic data;
            if(TelemetryCodec::parseBasicTelemetry(bytes,data) && (data.validity_flags&BasicHasPressure))
                pressureReceived=std::abs(data.pressure_hpa-1014.59)<0.02;
        } else if(type==MsgType::FpgaSensor) { FpgaSensor::Reading reading;
            fpgaSensorReceived=FpgaRemote::parseSensor(bytes,reading) && reading.source==0x40 && reading.pressurePa.has_value();
        }
    });
    runtime.setFpgaTransportForTesting(std::move(mock));
    require(runtime.start(),"start FPGA-only SkyCore");
    waitUntil([&]{return runtime.currentFpgaStatus().value("ready").toBool();},"Sky FPGA handshake ready");
    require(wire->openCount.load()==1,"startup owns one USB receive loop");
    DeviceOperationRequest operation;operation.request_id=7;operation.device_id=SkyDeviceId::Fpga;operation.operation=DeviceOperation::FpgaControl;
    operation.payload=FpgaRemote::encodeControl({{"op","temperature"},{"celsius",26.0}});
    CommandMessage command;command.command_id=CommandId::DeviceOperation;command.command_seq=12;
    command.payload=TelemetryCodec::serializeDeviceOperationRequest(operation);
    int completions=0;
    auto finished=[&](const SkyCommandResult& result) {
        require(result.ack.error_code==CommandErrorCode::Ok,"temperature readback succeeds");
        require(result.device_operation_response.request_id==7,"logical request ID retained");++completions;
    };
    runtime.submitCommand(command,finished,"ipc-A");
    auto retransmission=command;retransmission.command_seq=13;
    runtime.submitCommand(retransmission,[&](const SkyCommandResult& result) {
        require(result.ack.command_seq==13,"pending retransmission ACK uses its new sequence");finished(result);
    },"ipc-A");
    require(completions==0,"SkyCore ACK follows hardware final result");
    auto conflicting=operation;conflicting.payload=FpgaRemote::encodeControl({{"op","temperature"},{"celsius",27.0}});
    auto conflictCommand=command;conflictCommand.command_seq=14;
    conflictCommand.payload=TelemetryCodec::serializeDeviceOperationRequest(conflicting);
    int conflicts=0;
    auto conflict=[&](const SkyCommandResult& result) {
        require(result.ack.error_code==CommandErrorCode::InvalidPayload && result.ack.command_seq==14,
            "same request ID with changed payload rejected with current ACK sequence");++conflicts;
    };
    runtime.submitCommand(conflictCommand,conflict,"ipc-A");
    require(conflicts==1,"pending request payload conflicts are rejected before Busy handling");
    bool rejected=false;
    runtime.submitCommand(command,[&](const SkyCommandResult& result) {
        require(result.ack.error_code==CommandErrorCode::DeviceOperationBusy,"competing client scope receives Busy"); rejected=true;
    },"telemetry");
    require(rejected,"concurrent clients cannot submit second USB transaction");
    CommandMessage status;status.command_id=CommandId::RequestStatus;bool statusServed=false;
    runtime.submitCommand(status,[&](const SkyCommandResult& result){statusServed=result.ack.error_code==CommandErrorCode::Ok;});
    require(statusServed,"status stays responsive during hardware transaction");
    waitUntil([&]{return completions==2;},"duplicates complete from one operation");
    retransmission.command_seq=15;
    runtime.submitCommand(retransmission,[&](const SkyCommandResult& result) {
        require(result.ack.command_seq==15,"cached retransmission ACK uses latest sequence");finished(result);
    },"ipc-A");require(completions==3,"completed retry with new sequence reuses result cache");
    runtime.submitCommand(conflictCommand,conflict,"ipc-A");require(conflicts==2,"completed request ID cannot be reused with changed payload");
    require(wire->temperatureWrites.load()==1,"retransmitted requests do not repeat physical writes");
    waitUntil([&]{return pressureReceived && fpgaSensorReceived;},"FPGA pressure reaches both standardized telemetry and diagnostic sensor message");
    CommandErrorCode error;
    SkyDeviceManager manager;manager.loadConfig(config);
    require(!manager.receiveRtcmCorrectionData("rtcm",&error) && error==CommandErrorCode::UnknownCommand,"FPGA topology refuses unsupported independent RTCM serial path");
    require(!manager.connectDevice(SkyDeviceId::Epsilon,&error),"FPGA topology blocks direct sensor collector");
    require(telemetryPort!=0,"ephemeral telemetry port reported");
    QTcpSocket radio;TelemetryCodec radioCodec;int acknowledgements=0;
    QObject::connect(&radio,&QTcpSocket::readyRead,[&] {
        for(const auto& frame:radioCodec.feedBytes(radio.readAll())) if(frame.type==MsgType::CommandAck) {
            CommandAck ack;require(TelemetryCodec::parseCommandAck(frame.payload,ack),"parse actual radio ACK");
            require(ack.error_code==CommandErrorCode::Ok,"reconnected radio request executes instead of conflicting with old cache");
            require(ack.command_seq==21,"actual radio ACK sequence retained");++acknowledgements;
        }
    });
    radio.connectToHost("127.0.0.1",telemetryPort);
    waitUntil([&]{return connectedPeers==1;},"radio connection accepted");
    operation.request_id=1;operation.payload=FpgaRemote::encodeControl({{"op","temperature"},{"celsius",28.0}});
    command.command_seq=21;command.payload=TelemetryCodec::serializeDeviceOperationRequest(operation);
    TelemetryCodec encoder;
    radio.write(encoder.encodeFrame(MsgType::Command,TelemetryCodec::serializeCommand(command),1,1));
    waitUntil([&]{return acknowledgements==1;},"first radio controller request completes");
    radio.disconnectFromHost();waitUntil([&]{return disconnectedPeers==1;},"radio disconnect resets stream generation");
    radioCodec.reset();radio.connectToHost("127.0.0.1",telemetryPort);
    waitUntil([&]{return connectedPeers==2;},"new radio controller connects");
    operation.payload=FpgaRemote::encodeControl({{"op","temperature"},{"celsius",29.0}});
    command.payload=TelemetryCodec::serializeDeviceOperationRequest(operation);
    radio.write(encoder.encodeFrame(MsgType::Command,TelemetryCodec::serializeCommand(command),1,1));
    waitUntil([&]{return acknowledgements==2;},"new link generation may restart logical request ID at one");
    require(wire->temperatureWrites.load()==3,"new link generation executes fresh request instead of cached previous controller result");
    radio.disconnectFromHost();waitUntil([&]{return disconnectedPeers==2;},"test radio socket cleaned up");
    runtime.stop();
    auto producerClosed=std::make_shared<std::atomic_bool>(false);
    auto stoppedMock=std::make_unique<ScriptedTransport>();stoppedMock->destroyedClosed=producerClosed;
    runtime.setFpgaTransportForTesting(std::move(stoppedMock));
    operation.request_id=500;
    operation.payload=FpgaRemote::encodeControl({{"op","connect"},{"backend","auto"},{"locator","mock"}});
    command.payload=TelemetryCodec::serializeDeviceOperationRequest(operation);
    runtime.submitCommand(command,[](const SkyCommandResult&) {},"ipc-after-stop");
    runtime.stop();
    require(producerClosed->load(),"stop joins and closes USB producer even when runtime was already stopped");
    return 0;
}
