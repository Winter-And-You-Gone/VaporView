#include "ground/devices/RemoteSkyController.h"
#include "FpgaControlConfig.h"
#include "TcpTelemetryLink.h"
#include "LogService.h"
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QJsonDocument>
#include <QPointer>
#include <cstdlib>
#include <iostream>

namespace VaporView {
class GroundTelemetryServiceTestAccess {
public:
    static void nextSequence(GroundTelemetryService& service, quint16 sequence) { service.next_command_seq_ = sequence; }
    static void disableExtendedAck(GroundTelemetryService& service, quint16 sequence) {
        service.pending_commands_[sequence].command.extended_ack_context_requested = false;
    }
    static void disableExtendedResult(GroundTelemetryService& service, quint16 sequence) {
        service.pending_commands_[sequence].command.extended_result_context_requested = false;
    }
    static void expire(GroundTelemetryService& service, quint16 sequence) {
        auto it = service.pending_commands_.find(sequence);
        if (it != service.pending_commands_.end()) {
            it->retry_count = 3;
            it->next_retry_ms = 0;
        }
    }
};
}

namespace {
class InspectableGroundTelemetryService : public VaporView::GroundTelemetryService {
public:
    QObject *signalSender() const { return sender(); }
};
void require(bool condition,const char *message) {
    if(!condition){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}
}
template<typename Predicate> bool waitUntil(Predicate predicate, int timeoutMs = 3000) {
    QElapsedTimer timer;timer.start();
    while(timer.elapsed()<timeoutMs){QCoreApplication::processEvents(QEventLoop::AllEvents,20);if(predicate())return true;}
    return predicate();
}
}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    QVector<VaporView::LogRecord> operationLogs;
    QTemporaryDir logDirectory;
    VaporView::LogService logs(QStringLiteral("fpga-context-test"), nullptr, logDirectory.path());
    QObject::connect(&logs, &VaporView::LogService::recordPublished, &app,
                     [&](const VaporView::LogRecord& record) { operationLogs.push_back(record); });
    using namespace VaporView;
    using namespace VaporView::Ground::Devices;
    {
        GroundTelemetryService service;
        TelemetryCodec framing;
        QString ids[3];
        for (int i = 0; i < 3; ++i) {
            const auto seq = service.sendCommand(CommandId::SetSkyConfig, QByteArrayLiteral("{}"));
            ids[i] = operationLogs.constLast().correlation_id;
            CommandAck ack;
            ack.command_id = CommandId::SetSkyConfig;
            ack.command_seq = seq;
            ack.correlation_id = ids[i];
            ack.error_code = CommandErrorCode::ConfigInvalid;
            if (i < 2) ack.config_result_context_expected = (i == 1);
            const auto frame = framing.encodeFrame(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(ack), 1, 1);
            require(QMetaObject::invokeMethod(&service, "onBytesReceived", Qt::DirectConnection, Q_ARG(QByteArray, frame)),
                    "config expectation ACK injected");
        }
        const auto before = operationLogs.size();
        service.close();
        bool reported[3] = {};
        for (qsizetype i = before; i < operationLogs.size(); ++i)
            for (int j = 0; j < 3; ++j)
                if (operationLogs[i].correlation_id == ids[j] &&
                    operationLogs[i].fields.value(QStringLiteral("stage")) == QStringLiteral("awaiting_config_result"))
                    reported[j] = true;
        require(!reported[0] && reported[1] && reported[2],
                "explicit absent result is not awaited; failed result and legacy ACK remain tracked");
    }
    {
        GroundTelemetryService service;
        TelemetryCodec framing;
        DeviceOperationRequest request;
        request.device_id = SkyDeviceId::Fpga;
        request.operation = DeviceOperation::FpgaControl;
        request.request_id = 9701;
        const quint16 acknowledged = service.sendDeviceOperation(request);
        require(acknowledged != 0, "result timeout operation accepted");
        const QString acknowledgedId = operationLogs.constLast().correlation_id;
        CommandAck ack;
        ack.command_id = CommandId::DeviceOperation;
        ack.command_seq = acknowledged;
        ack.correlation_id = acknowledgedId;
        ack.session_id = QStringLiteral("actual-session-at-ack");
        ack.typed_response_context_supported = true;
        const QByteArray frame = framing.encodeFrame(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(ack), 1, 1);
        require(QMetaObject::invokeMethod(&service, "onBytesReceived", Qt::DirectConnection, Q_ARG(QByteArray, frame)),
                "result timeout ACK injected");
        request.request_id = 9702;
        const quint16 unacknowledged = service.sendDeviceOperation(request);
        require(unacknowledged != 0, "ACK timeout operation accepted");
        const QString unacknowledgedId = operationLogs.constLast().correlation_id;
        GroundTelemetryServiceTestAccess::expire(service, acknowledged);
        GroundTelemetryServiceTestAccess::expire(service, unacknowledged);
        int timeouts = 0;
        QObject::connect(&service, &GroundTelemetryService::commandTimedOut, &service,
                         [&](CommandId, quint16) { ++timeouts; });
        const qsizetype before = operationLogs.size();
        require(QMetaObject::invokeMethod(&service, "onRetryTimer", Qt::DirectConnection), "expire pending operations");
        bool resultTimeout = false, ackTimeout = false, wrongAckTimeout = false;
        for (qsizetype i = before; i < operationLogs.size(); ++i) {
            const auto& record = operationLogs[i];
            const QString event = record.fields.value(QStringLiteral("event")).toString();
            if (record.correlation_id == acknowledgedId && event == QStringLiteral("telemetry_device_result_timeout"))
                resultTimeout = record.session_id == ack.session_id &&
                    record.fields.value(QStringLiteral("reason_code")) == QStringLiteral("TELEMETRY_DEVICE_RESULT_TIMEOUT");
            if (record.correlation_id == acknowledgedId && event == QStringLiteral("telemetry_command_ack_timeout")) wrongAckTimeout = true;
            if (record.correlation_id == unacknowledgedId && event == QStringLiteral("telemetry_command_ack_timeout"))
                ackTimeout = record.session_id.isEmpty();
        }
        require(resultTimeout && ackTimeout && !wrongAckTimeout && timeouts == 2,
                "ACK and result timeouts preserve context and existing timeout signals");
    }
    TcpTelemetryLink server;require(server.listen("127.0.0.1",0),"listen");
    RemoteSkyController controller;TelemetryCodec decoder;DeviceOperationRequest received;quint16 commandSeq=0;int commands=0;
    QString correlation;
    QObject::connect(&server,&TelemetryLink::bytesReceived,[&](const QByteArray& bytes){
        for(const auto& frame:decoder.feedBytes(bytes)) {
            CommandMessage command;
            if(frame.type==MsgType::Command && TelemetryCodec::parseCommand(frame.payload,command) &&
                command.command_id==CommandId::DeviceOperation && TelemetryCodec::parseDeviceOperationRequest(command.payload,received)) {
                commandSeq=command.command_seq;correlation=command.correlation_id;++commands;
            }
        }
    });
    require(controller.openTcp("127.0.0.1",server.localPort()),"open controller");
    require(waitUntil([&]{return controller.isOpen();}),"connected");
    int responses=0,statuses=0,sensors=0,waves=0;
    QString responseCorrelation, responseSession;
    QObject::connect(&controller,&RemoteSkyController::deviceOperationResponseReceived,[&](const DeviceOperationResponse& value){++responses;responseCorrelation=value.correlation_id;responseSession=value.session_id;});
    QObject::connect(&controller,&RemoteSkyController::fpgaStatusUpdated,[&](const auto&){++statuses;});
    QObject::connect(&controller,&RemoteSkyController::fpgaSensorUpdated,[&](const auto&){++sensors;});
    QObject::connect(&controller,&RemoteSkyController::fpgaWaveformUpdated,[&](const auto&){++waves;});
    int acknowledgments = 0;
    QObject::connect(controller.telemetryService(), &GroundTelemetryService::commandAckReceived,
                     [&](const CommandAck&) { ++acknowledgments; });
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
    require(!correlation.isEmpty(), "operation has a correlation ID");
    ack.correlation_id = QStringLiteral("stale-other-operation");
    write(MsgType::CommandAck,TelemetryCodec::serializeCommandAck(ack));
    QElapsedTimer mismatchedWait; mismatchedWait.start();
    while(mismatchedWait.elapsed()<100) QCoreApplication::processEvents(QEventLoop::AllEvents,20);
    require(acknowledgments==0,"mismatched ACK cannot finish pending command");
    ack.correlation_id.clear();
    ack.command_id = CommandId::SaveSkyConfig;
    write(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(ack));
    mismatchedWait.restart();
    while(mismatchedWait.elapsed()<100) QCoreApplication::processEvents(QEventLoop::AllEvents,20);
    require(acknowledgments==0,"legacy ACK with different command ID cannot finish pending command");
    ack.command_id = CommandId::DeviceOperation;
    ack.correlation_id = correlation;
    ack.session_id = QStringLiteral("active-session");
    write(MsgType::CommandAck,TelemetryCodec::serializeCommandAck(ack));
    require(waitUntil([&]{return acknowledgments==1;}),"matching ACK remains accepted after mismatch");
    DeviceOperationResponse response;response.request_id=id;response.device_id=SkyDeviceId::Fpga;response.operation=DeviceOperation::FpgaControl;
    write(MsgType::DeviceOperationResponse,TelemetryCodec::serializeDeviceOperationResponse(response));
    require(waitUntil([&]{return responses==1;}),"FPGA response not misclassified as EPSILON");
    require(responseCorrelation == correlation && responseSession == QStringLiteral("active-session"),
            "legacy typed wire result restores request correlation and ACK session");
    require(controller.epsilonSettingsSupport()==DeviceOperationSupport::Unknown,"FPGA operation leaves EPSILON capability untouched");
    QString rejectedResultCorrelation;
    QObject::connect(controller.telemetryService(), &GroundTelemetryService::deviceOperationResponseReceived,
                     [&](const DeviceOperationResponse& value) {
                         if (value.error_code == CommandErrorCode::InvalidPayload) rejectedResultCorrelation = value.correlation_id;
                     });
    const auto rejectedId = controller.sendFpgaControl({{"op","refresh"}});
    require(rejectedId != 0 && waitUntil([&]{return commands==2;}), "operation sent for terminal error check");
    const QString rejectedCorrelation = correlation;
    ack.command_seq = commandSeq; ack.correlation_id = correlation; ack.error_code = CommandErrorCode::InvalidPayload;
    write(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(ack));
    require(waitUntil([&]{return acknowledgments==2;}), "terminal error ACK delivered");
    response.request_id = rejectedId; response.error_code = CommandErrorCode::InvalidPayload;
    write(MsgType::DeviceOperationResponse, TelemetryCodec::serializeDeviceOperationResponse(response));
    require(waitUntil([&]{return !rejectedResultCorrelation.isEmpty();}) && rejectedResultCorrelation == rejectedCorrelation,
            "typed failure after terminal ACK retains original operation correlation");
    int typedContexts = 0;
    QString typedCorrelation, typedSession;
    QObject::connect(controller.telemetryService(), &GroundTelemetryService::deviceOperationResponseReceived,
                     [&](const DeviceOperationResponse& value) {
                         if (value.request_id >= 700 && value.request_id <= 701) {
                             ++typedContexts; typedCorrelation = value.correlation_id; typedSession = value.session_id;
                         }
                     });
    for (int index = 0; index != 2; ++index) {
        DeviceOperationRequest operation;
        operation.request_id = 700 + index;
        operation.device_id = index == 0 ? SkyDeviceId::Ai8TemperatureController : SkyDeviceId::Epsilon;
        operation.operation = index == 0 ? DeviceOperation::ReadParameters : DeviceOperation::ConfigureEpsilonRtcmInput;
        const int expectedCommands = commands + 1;
        const auto sequence = controller.telemetryService()->sendCommand(CommandId::DeviceOperation, TelemetryCodec::serializeDeviceOperationRequest(operation));
        require(waitUntil([&] { return commands == expectedCommands; }), "typed operation sent");
        const QString operationCorrelation = correlation;
        CommandAck operationAck;
        operationAck.command_id = CommandId::DeviceOperation; operationAck.command_seq = sequence;
        operationAck.correlation_id = operationCorrelation;
        operationAck.session_id = index == 0 ? QStringLiteral("active-session") : QString();
        const int expectedAcks = acknowledgments + 1;
        write(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(operationAck));
        require(waitUntil([&] { return acknowledgments == expectedAcks; }), "typed operation ACK delivered");
        DeviceOperationResponse result;
        result.request_id = operation.request_id; result.device_id = operation.device_id; result.operation = operation.operation;
        write(MsgType::DeviceOperationResponse, TelemetryCodec::serializeDeviceOperationResponse(result));
        require(waitUntil([&] { return typedContexts == index + 1; }) && typedCorrelation == operationCorrelation && typedSession == operationAck.session_id,
                "typed response retains ACK session without inheriting a prior session");
    }
    DeviceOperationRequest awaitingResult;
    awaitingResult.request_id = 702;
    awaitingResult.device_id = SkyDeviceId::Ai8TemperatureController;
    awaitingResult.operation = DeviceOperation::ReadParameters;
    const int expectedCachedCommands = commands + 1;
    const auto cachedSequence = controller.telemetryService()->sendDeviceOperation(awaitingResult);
    require(waitUntil([&] { return commands == expectedCachedCommands; }), "operation waiting for typed result sent");
    const QString cachedCorrelation = correlation;
    CommandAck cachedAck;
    cachedAck.command_id = CommandId::DeviceOperation;
    cachedAck.command_seq = cachedSequence;
    cachedAck.correlation_id = cachedCorrelation;
    cachedAck.session_id = QStringLiteral("active-session");
    write(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(cachedAck));
    require(waitUntil([&] { return acknowledgments == 5; }), "operation waiting for typed result acknowledged");
    const int expectedCommands = commands + 1;
    require(controller.sendFpgaControl({{"op","refresh"}})!=0 && waitUntil([&]{return commands==expectedCommands;}),
            "third operation sent for legacy ACK check");
    ack.command_seq = commandSeq; ack.correlation_id.clear(); ack.error_code = CommandErrorCode::Ok;
    write(MsgType::CommandAck,TelemetryCodec::serializeCommandAck(ack));
    require(waitUntil([&]{return acknowledgments==6;}),"legacy ACK without correlation remains accepted");
    QJsonObject status{{"connected",true},{"ready",true},{"busy",false},{"detail","ready"},{"configuration",FpgaControlConfig{}.toJson()},
        {"registers",QJsonObject{}},{"archived_records","0"}};
    write(MsgType::FpgaStatus,FpgaRemote::encodeStatus(status));
    FpgaSensor::Reading sensor;sensor.source=0x44;sensor.schema=1;sensor.message=0x1100;
    write(MsgType::FpgaSensor,FpgaRemote::encodeSensor(sensor,1));
    FpgaWave::CompletedStream wave;wave.source=0x20;wave.message=0x1000;wave.schema=2;wave.format=0;wave.rate=1000;wave.totalPoints=1;wave.bytesPerPoint=4;wave.adcBits=24;wave.signed32Samples.append(-1);
    write(MsgType::FpgaWaveformPreview,FpgaRemote::encodeWaveformPreview(wave));
    require(waitUntil([&]{return statuses==1 && sensors==1 && waves==1;}),"all typed FPGA telemetry delivered");
    require(controller.waveformPacketRate(1, true) < 0, "ADC preview does not count as H1/H2");
    wave.source = 0x30; wave.message = 0x1001; wave.adcBits = 0; wave.complete = true; wave.bytesPerPoint = 8; wave.format = 0;
    wave.signed32Samples.clear(); wave.dliaPoints.clear();
    FpgaWave::CompletedStream::DliaPoint point; point.h1 = 10; point.h2 = 5; point.hasHarmonics = true;
    wave.dliaPoints.append(point);
    const int beforePreview = waves;
    write(MsgType::FpgaWaveformPreview, FpgaRemote::encodeWaveformPreview(wave));
    require(waitUntil([&]{ return waves == beforePreview + 1; }), "DLIA preview delivered");
    require(controller.waveformPacketRate(1, true) >= 0 && controller.waveformPacketRate(4, true) >= 0,
            "DLIA preview counts both H1/H2 and valid normalized updates");
    write(MsgType::FpgaWaveformPreview, FpgaRemote::encodeWaveformPreview(wave));
    require(waitUntil([&]{ return waves == beforePreview + 2; }), "duplicate preview still delivered to existing consumers");
    require(controller.waveformPacketRate(1) == 0, "duplicate cycle does not increment preview frequency");
    wave.source = 0x31; wave.cycleId = 1; wave.dliaPoints[0].h1 = 0;
    write(MsgType::FpgaWaveformPreview, FpgaRemote::encodeWaveformPreview(wave));
    require(waitUntil([&]{ return waves == beforePreview + 3; }), "second DLIA channel delivered");
    require(controller.waveformPacketRate(4) == 0, "invalid normalization does not count as an update");
    require(controller.waveformPacketRate(1, true) == 0.4 && controller.waveformPacketRate(4, true) == 0.2,
            "five-second totals include both DLIA channels, exclude duplicate and invalid normalized events");
    auto service=controller.telemetryService();
    DeviceOperationRequest duplicateRequest;
    duplicateRequest.request_id = 703;
    duplicateRequest.device_id = SkyDeviceId::Ai8TemperatureController;
    duplicateRequest.operation = DeviceOperation::ReadParameters;
    bool reentered = false;
    quint16 reentrantResult = 1;
    const auto reentryConnection = QObject::connect(&logs, &LogService::recordPublished, &logs,
        [&](const LogRecord& record) {
            if (!reentered && record.fields.value(QStringLiteral("event")) == QStringLiteral("ground_command_send")) {
                reentered = true;
                reentrantResult = service->sendDeviceOperation(duplicateRequest);
            }
        }, Qt::DirectConnection);
    const int beforeDuplicate = commands;
    const auto duplicateSeq = service->sendDeviceOperation(duplicateRequest);
    QObject::disconnect(reentryConnection);
    require(duplicateSeq != 0 && reentered && reentrantResult == 0, "synchronous reentry cannot reuse an in-flight request ID");
    require(service->sendDeviceOperation(duplicateRequest) == 0, "duplicate pending request ID is rejected");
    auto independentRequest = duplicateRequest;
    independentRequest.request_id = 704;
    require(service->sendDeviceOperation(independentRequest) != 0, "independent request ID remains accepted");
    require(waitUntil([&] { return commands == beforeDuplicate + 2; }), "duplicate request never reaches the link");
    CommandAck duplicateAck;
    duplicateAck.command_id = CommandId::DeviceOperation;
    duplicateAck.command_seq = duplicateSeq;
    // Legacy ACK exercises the same result-waiting cache.
    const int beforeAck = acknowledgments;
    write(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(duplicateAck));
    require(waitUntil([&] { return acknowledgments > beforeAck; }), "first request acknowledged");
    require(service->sendDeviceOperation(duplicateRequest) == 0, "request ID awaiting typed result is rejected");
    DeviceOperationResponse duplicateResponse;
    duplicateResponse.request_id = duplicateRequest.request_id;
    duplicateResponse.device_id = duplicateRequest.device_id;
    duplicateResponse.operation = duplicateRequest.operation;
    bool duplicateResponseReceived = false;
    QObject::connect(service, &GroundTelemetryService::deviceOperationResponseReceived, service,
                     [&](const DeviceOperationResponse& value) {
                         if (value.request_id == duplicateRequest.request_id) duplicateResponseReceived = true;
                     });
    write(MsgType::DeviceOperationResponse, TelemetryCodec::serializeDeviceOperationResponse(duplicateResponse));
    require(waitUntil([&] { return duplicateResponseReceived; }), "first typed result received");
    require(service->sendDeviceOperation(duplicateRequest) != 0, "request ID is released after typed result");
    emit service->fpgaStatusUpdated(status); // Queue a prior-generation event, then close before delivery.
    emit service->fpgaSensorUpdated(sensor);
    emit service->fpgaWaveformUpdated(wave);
    const QString pendingCorrelation = correlation;
    controller.close();QCoreApplication::processEvents();
    bool disconnectLogged = false, cachedDisconnectLogged = false;
    for (const auto& record : operationLogs)
        if (record.fields.value(QStringLiteral("event")).toString() == QStringLiteral("ground_command_disconnected") &&
            record.correlation_id == pendingCorrelation) disconnectLogged = true;
        else if (record.fields.value(QStringLiteral("event")).toString() == QStringLiteral("ground_command_disconnected") &&
                 record.correlation_id == cachedCorrelation && record.session_id == QStringLiteral("active-session") &&
                 record.fields.value(QStringLiteral("stage")).toString() == QStringLiteral("awaiting_device_result")) cachedDisconnectLogged = true;
    require(disconnectLogged, "disconnect logs unfinished command with its original correlation");
    require(cachedDisconnectLogged, "disconnect logs acknowledged operation still awaiting typed result");
    require(statuses==1 && sensors==1 && waves==beforePreview+3,"stale queued FPGA telemetry rejected after close");
    server.close();
    {
        TcpTelemetryLink peer;
        require(peer.listen("127.0.0.1", 0), "send reentry peer listens");
        GroundTelemetryService service;
        require(service.openTcp("127.0.0.1", peer.localPort()) && waitUntil([&] { return service.isOpen(); }), "send reentry service connects");
        bool closed = false;
        QObject::connect(&logs, &LogService::recordPublished, &service, [&](const LogRecord& record) {
            if (!closed && record.fields.value(QStringLiteral("event")) == QStringLiteral("ground_command_send")) {
                closed = true;
                service.close();
            }
        }, Qt::DirectConnection);
        require(service.sendCommand(CommandId::SaveSkyConfig) == 0 && closed && !service.isOpen(), "send aborts when a log callback closes its link");
        peer.close();
    }
    {
        TcpTelemetryLink peer;
        require(peer.listen("127.0.0.1", 0), "retry reentry peer listens");
        GroundTelemetryService service;
        require(service.openTcp("127.0.0.1", peer.localPort()) && waitUntil([&] { return service.isOpen(); }), "retry reentry service connects");
        bool closed = false;
        QObject::connect(&logs, &LogService::recordPublished, &service, [&](const LogRecord& record) {
            if (!closed && record.fields.value(QStringLiteral("event")) == QStringLiteral("ground_command_retry")) {
                closed = true;
                service.close();
            }
        }, Qt::DirectConnection);
        require(service.sendCommand(CommandId::SaveSkyConfig) != 0, "retry reentry command sent");
        require(waitUntil([&] { return closed; }) && !service.isOpen(), "retry callback may close without invalidating pending traversal");
        peer.close();
    }
    for (int scenario = 0; scenario != 2; ++scenario) {
        TcpTelemetryLink originalPeer, replacementPeer;
        require(originalPeer.listen("127.0.0.1", 0) && replacementPeer.listen("127.0.0.1", 0),
                "disconnect reentry peers listen");
        InspectableGroundTelemetryService service;
        require(service.openTcp("127.0.0.1", originalPeer.localPort()) && waitUntil([&] { return service.isOpen(); }),
                "disconnect reentry service connects");
        bool reconnecting = false, reopened = false, staleClose = false, replacementReceived = false;
        QObject::connect(&service, &GroundTelemetryService::linkOpenChanged, &service, [&](bool open) {
            if (reopened && !open) staleClose = true;
        });
        QObject::connect(&replacementPeer, &TelemetryLink::bytesReceived, &service, [&](const QByteArray&) {
            replacementReceived = true;
        });
        QPointer<TelemetryLink> previousLink;
        const auto reconnectConnection = QObject::connect(&logs, &LogService::recordPublished, &app, [&](const LogRecord& record) {
            if (!reconnecting && record.fields.value(QStringLiteral("event")) == QStringLiteral("ground_command_disconnected")) {
                reconnecting = true;
                if (scenario == 1) {
                    previousLink = qobject_cast<TelemetryLink *>(service.signalSender());
                    require(previousLink, "natural disconnect retains the emitting link");
                }
                require(service.openTcp("127.0.0.1", replacementPeer.localPort()), "replacement link starts during disconnect log");
                if (scenario == 1) require(previousLink, "emitting link survives nested reopen");
                reopened = true;
            }
        }, Qt::DirectConnection);
        require(service.sendCommand(CommandId::SaveSkyConfig) != 0, "disconnect reentry operation sent");
        if (scenario == 0) service.close();
        else originalPeer.close();
        require(waitUntil([&] { return reopened && service.isOpen(); }), "replacement link survives old close");
        require(service.sendCommand(CommandId::SaveSkyConfig) != 0 && waitUntil([&] { return replacementReceived; }),
                "replacement link carries a new command");
        require(!staleClose, "old disconnect does not publish closed status after replacement");
        if (scenario == 1) {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            require(!previousLink, "old link is destroyed after signal delivery");
        }
        QObject::disconnect(reconnectConnection);
        service.close();
        originalPeer.close();
        replacementPeer.close();
    }
    for (int scenario = 0; scenario != 2; ++scenario) {
        TcpTelemetryLink peer;
        require(peer.listen("127.0.0.1", 0), "destruction reentry peer listens");
        QPointer<GroundTelemetryService> service = new GroundTelemetryService;
        require(service->openTcp("127.0.0.1", peer.localPort()) && waitUntil([&] { return service && service->isOpen(); }),
                "destruction reentry service connects");
        require(service->sendCommand(CommandId::SaveSkyConfig) != 0, "destruction reentry command sent");
        const QString id = operationLogs.constLast().correlation_id;
        bool deleted = false;
        const auto connection = QObject::connect(&logs, &LogService::recordPublished, &app, [&](const LogRecord& record) {
            if (!deleted && record.correlation_id == id &&
                record.fields.value(QStringLiteral("event")) == QStringLiteral("ground_command_disconnected")) {
                deleted = true;
                if (scenario == 0) delete service.data();
                else service->deleteLater();
            }
        }, Qt::DirectConnection);
        if (scenario == 0) service->close();
        else peer.close();
        require(waitUntil([&] { return deleted; }), "disconnect log requests service destruction");
        if (scenario == 1) QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        require(!service, "service is destroyed after disconnect processing");
        QObject::disconnect(connection);
        peer.close();
    }
    for (int scenario = 0; scenario != 2; ++scenario) {
        TcpTelemetryLink originalPeer, requestedPeer, replacementPeer;
        require(originalPeer.listen("127.0.0.1", 0) && requestedPeer.listen("127.0.0.1", 0) &&
                replacementPeer.listen("127.0.0.1", 0), "nested open peers listen");
        GroundTelemetryService service;
        require(service.openTcp("127.0.0.1", originalPeer.localPort()) && waitUntil([&] { return service.isOpen(); }),
                "nested open service connects");
        require(service.sendCommand(CommandId::SaveSkyConfig) != 0, "nested open command sent");
        const QString id = operationLogs.constLast().correlation_id;
        bool reopened = false, replacementReceived = false;
        QObject::connect(&replacementPeer, &TelemetryLink::bytesReceived, &service, [&](const QByteArray&) {
            replacementReceived = true;
        });
        const auto connection = QObject::connect(&logs, &LogService::recordPublished, &service, [&](const LogRecord& record) {
            if (!reopened && record.correlation_id == id &&
                record.fields.value(QStringLiteral("event")) == QStringLiteral("ground_command_disconnected")) {
                reopened = true;
                require(service.openTcp("127.0.0.1", replacementPeer.localPort()), "nested replacement connects");
            }
        }, Qt::DirectConnection);
        const bool opened = scenario == 0 ? service.openTcp("127.0.0.1", requestedPeer.localPort())
                                          : service.openTcpAsync("127.0.0.1", requestedPeer.localPort());
        require(reopened && !opened && service.isOpen(), "outer open cannot replace nested link");
        require(service.sendCommand(CommandId::SaveSkyConfig) != 0 && waitUntil([&] { return replacementReceived; }),
                "nested replacement remains usable");
        QObject::disconnect(connection);
        service.close();
        originalPeer.close(); requestedPeer.close(); replacementPeer.close();
    }
    for (int scenario = 0; scenario != 2; ++scenario) {
        TcpTelemetryLink originalPeer, requestedPeer;
        require(originalPeer.listen("127.0.0.1", 0) && requestedPeer.listen("127.0.0.1", 0), "open destruction peers listen");
        QPointer<GroundTelemetryService> service = new GroundTelemetryService;
        require(service->openTcp("127.0.0.1", originalPeer.localPort()) && waitUntil([&] { return service && service->isOpen(); }),
                "open destruction service connects");
        require(service->sendCommand(CommandId::SaveSkyConfig) != 0, "open destruction command sent");
        const QString id = operationLogs.constLast().correlation_id;
        const auto connection = QObject::connect(&logs, &LogService::recordPublished, &app, [&](const LogRecord& record) {
            if (service && record.correlation_id == id &&
                record.fields.value(QStringLiteral("event")) == QStringLiteral("ground_command_disconnected"))
                delete service.data();
        }, Qt::DirectConnection);
        const bool opened = scenario == 0 ? service->openTcp("127.0.0.1", requestedPeer.localPort())
                                          : service->openTcpAsync("127.0.0.1", requestedPeer.localPort());
        require(!opened && !service, "open tolerates destruction during disconnect log");
        QObject::disconnect(connection);
        originalPeer.close(); requestedPeer.close();
    }
    for (int scenario = 0; scenario != 3; ++scenario) {
        TcpTelemetryLink peer;
        require(peer.listen("127.0.0.1", 0), "reentrant peer listens");
        GroundTelemetryService service;
        TelemetryCodec parser, framing;
        CommandMessage sent;
        QObject::connect(&peer, &TelemetryLink::bytesReceived, &service, [&](const QByteArray& bytes) {
            for (const auto& frame : parser.feedBytes(bytes))
                if (frame.type == MsgType::Command) TelemetryCodec::parseCommand(frame.payload, sent);
        });
        require(service.openTcp("127.0.0.1", peer.localPort()) && waitUntil([&] { return service.isOpen(); }), "reentrant service connects");
        DeviceOperationRequest request;
        request.request_id = 810 + scenario;
        request.device_id = scenario == 2 ? SkyDeviceId::Ai8TemperatureController : SkyDeviceId::Fpga;
        request.operation = scenario == 2 ? DeviceOperation::ReadParameters : DeviceOperation::FpgaControl;
        const quint16 sequence = service.sendDeviceOperation(request);
        require(sequence && waitUntil([&] { return sent.command_seq == sequence; }), "reentrant operation sent");
        int delivered = 0;
        QObject::connect(&service, &GroundTelemetryService::commandAckReceived, &service, [&](const CommandAck&) { ++delivered; });
        QObject::connect(&service, &GroundTelemetryService::deviceOperationResponseReceived, &service, [&](const DeviceOperationResponse&) { ++delivered; });
        CommandAck responseAck;
        responseAck.command_id = CommandId::DeviceOperation;
        responseAck.command_seq = sequence;
        responseAck.correlation_id = sent.correlation_id;
        if (scenario == 2) {
            peer.writeBytes(framing.encodeFrame(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(responseAck), 1, 1));
            require(waitUntil([&] { return delivered == 1; }), "cache setup ACK delivered");
            delivered = 0;
        }
        bool closedFromLog = false;
        QObject::connect(&logs, &LogService::recordPublished, &service, [&](const LogRecord& record) {
            const auto expected = scenario == 0 ? QStringLiteral("ground_command_ack") : QStringLiteral("ground_device_operation_response");
            if (!closedFromLog && record.correlation_id == sent.correlation_id && record.fields.value(QStringLiteral("event")) == expected) {
                closedFromLog = true;
                service.close();
            }
        }, Qt::DirectConnection);
        if (scenario == 0) {
            peer.writeBytes(framing.encodeFrame(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(responseAck), 2, 2));
        } else {
            DeviceOperationResponse result;
            result.request_id = request.request_id; result.device_id = request.device_id; result.operation = request.operation;
            peer.writeBytes(framing.encodeFrame(MsgType::DeviceOperationResponse, TelemetryCodec::serializeDeviceOperationResponse(result), 2, 2));
        }
        require(waitUntil([&] { return closedFromLog; }), "response log can synchronously close the link");
        require(delivered == 0 && !service.isOpen(), "old-generation response is not delivered after reentrant close");
        peer.close();
    }
    {
        GroundTelemetryService service;
        TelemetryCodec framing;
        QHash<quint32, QString> originalContexts;
        quint32 evictedId = 0;
        quint16 reenteredSequence = 0;
        LogRecord eviction;
        const auto connection = QObject::connect(&logs, &LogService::recordPublished, &service, [&](const LogRecord& record) {
            if (record.fields.value(QStringLiteral("event")) != QStringLiteral("ground_device_result_context_evicted")) return;
            eviction = record;
            evictedId = record.fields.value(QStringLiteral("request_id")).toUInt();
            DeviceOperationRequest replacement;
            replacement.request_id = evictedId;
            replacement.device_id = SkyDeviceId::Ai8TemperatureController;
            replacement.operation = DeviceOperation::ReadParameters;
            reenteredSequence = service.sendDeviceOperation(replacement);
        }, Qt::DirectConnection);
        for (quint32 id = 1000; id != 1257; ++id) {
            DeviceOperationRequest request;
            request.request_id = id;
            request.device_id = SkyDeviceId::Ai8TemperatureController;
            request.operation = DeviceOperation::ReadParameters;
            const quint16 sequence = service.sendDeviceOperation(request);
            require(sequence != 0, "capacity test operation accepted");
            originalContexts.insert(id, operationLogs.constLast().correlation_id);
            CommandAck reply;
            reply.command_id = CommandId::DeviceOperation;
            reply.command_seq = sequence;
            reply.correlation_id = originalContexts.value(id);
            reply.session_id = QStringLiteral("capacity-session");
            const QByteArray bytes = framing.encodeFrame(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(reply), 1, 1);
            require(QMetaObject::invokeMethod(&service, "onBytesReceived", Qt::DirectConnection, Q_ARG(QByteArray, bytes)), "capacity ACK injected");
        }
        QObject::disconnect(connection);
        require(evictedId >= 1000 && evictedId < 1256 && reenteredSequence != 0, "eviction logs after removing the old context");
        require(eviction.correlation_id == originalContexts.value(evictedId) && eviction.session_id == QStringLiteral("capacity-session"),
                "eviction retains the affected request correlation and session");
        DeviceOperationRequest replacement;
        replacement.request_id = evictedId;
        replacement.device_id = SkyDeviceId::Ai8TemperatureController;
        replacement.operation = DeviceOperation::ReadParameters;
        require(service.sendDeviceOperation(replacement) == 0, "reentrant replacement remains pending after eviction logging");
    }
    {
        GroundTelemetryService service;
        TelemetryCodec framing;
        QHash<quint32, QString> originalContexts;
        QHash<quint32, LogRecord> expirations;
        QHash<quint32, quint16> replacements;
        const auto connection = QObject::connect(&logs, &LogService::recordPublished, &service, [&](const LogRecord& record) {
            if (record.fields.value(QStringLiteral("event")) != QStringLiteral("ground_device_result_context_expired")) return;
            const quint32 id = record.fields.value(QStringLiteral("request_id")).toUInt();
            expirations.insert(id, record);
            DeviceOperationRequest replacement;
            replacement.request_id = id;
            replacement.device_id = id == 1300 ? SkyDeviceId::Ai8TemperatureController : SkyDeviceId::Fpga;
            replacement.operation = id == 1300 ? DeviceOperation::ReadParameters : DeviceOperation::FpgaControl;
            replacements.insert(id, service.sendDeviceOperation(replacement));
        }, Qt::DirectConnection);
        for (quint32 id = 1300; id != 1302; ++id) {
            DeviceOperationRequest request;
            request.request_id = id;
            request.device_id = id == 1300 ? SkyDeviceId::Ai8TemperatureController : SkyDeviceId::Fpga;
            request.operation = id == 1300 ? DeviceOperation::ReadParameters : DeviceOperation::FpgaControl;
            const quint16 sequence = service.sendDeviceOperation(request);
            require(sequence != 0, "expiry test operation accepted");
            originalContexts.insert(id, operationLogs.constLast().correlation_id);
            CommandAck reply;
            reply.command_id = CommandId::DeviceOperation;
            reply.command_seq = sequence;
            reply.correlation_id = originalContexts.value(id);
            reply.session_id = QStringLiteral("expiry-session");
            if (id == 1301) reply.error_code = CommandErrorCode::InternalError;
            const QByteArray bytes = framing.encodeFrame(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(reply), 1, 1);
            require(QMetaObject::invokeMethod(&service, "onBytesReceived", Qt::DirectConnection, Q_ARG(QByteArray, bytes)), "expiry ACK injected");
        }
        require(waitUntil([&] { return expirations.size() == 2; }, 35000), "both result context TTLs expire");
        QObject::disconnect(connection);
        for (quint32 id = 1300; id != 1302; ++id) {
            const LogRecord record = expirations.value(id);
            require(record.correlation_id == originalContexts.value(id) && record.session_id == QStringLiteral("expiry-session"),
                    "expiry retains the affected request correlation and session");
            require(replacements.value(id) != 0, "expiry callback can reuse released request ID");
            require(record.fields.value(QStringLiteral("stage")) ==
                    (id == 1300 ? QStringLiteral("awaiting_device_result") : QStringLiteral("awaiting_failed_device_result")),
                    "expiry identifies its waiting state");
        }
    }
    {
        GroundTelemetryService service;
        TelemetryCodec framing;
        quint16 frameSequence = 1;
        QVector<DeviceOperationResponse> delivered;
        QVector<CommandAck> deliveredAcks;
        QObject::connect(&service, &GroundTelemetryService::deviceOperationResponseReceived,
                         &service, [&](const DeviceOperationResponse& result) { delivered.push_back(result); });
        QObject::connect(&service, &GroundTelemetryService::commandAckReceived,
                         &service, [&](const CommandAck& ack) { deliveredAcks.push_back(ack); });
        const auto inject = [&](MsgType type, const QByteArray& payload) {
            const auto frame = framing.encodeFrame(type, payload, frameSequence++, 1);
            require(QMetaObject::invokeMethod(&service, "onBytesReceived", Qt::DirectConnection,
                                              Q_ARG(QByteArray, frame)), "context test frame injected");
        };
        DeviceOperationRequest operation;
        operation.request_id = 2500; operation.device_id = SkyDeviceId::Fpga;
        operation.operation = DeviceOperation::FpgaControl;
        const auto start = [&] {
            const quint16 sequence = service.sendDeviceOperation(operation);
            require(sequence != 0, "context request accepted");
            return qMakePair(sequence, operationLogs.constLast().correlation_id);
        };
        const auto acknowledge = [&](quint16 sequence, const QString& id, bool supported) {
            CommandAck ack; ack.command_id = CommandId::DeviceOperation; ack.command_seq = sequence;
            ack.correlation_id = id; ack.session_id = QStringLiteral("context-session");
            ack.typed_response_context_supported = supported;
            inject(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(ack));
        };
        DeviceOperationResponse result;
        result.request_id = operation.request_id; result.device_id = operation.device_id; result.operation = operation.operation;
        const auto first = start();
        acknowledge(first.first, first.second, true);
        result.correlation_id = first.second; result.session_id = QStringLiteral("context-session");
        const auto oldPayload = TelemetryCodec::serializeDeviceOperationResponseContext(result);
        inject(MsgType::DeviceOperationResponseContext, oldPayload);
        require(delivered.size() == 1 && delivered.last().correlation_id == first.second, "negotiated result delivered");
        const auto second = start();
        require(second.second != first.second, "reused request has fresh correlation");
        acknowledge(second.first, second.second, true);
        inject(MsgType::DeviceOperationResponseContext, oldPayload);
        inject(MsgType::DeviceOperationResponse, TelemetryCodec::serializeDeviceOperationResponse(result));
        require(delivered.size() == 1, "stale contextual and legacy results cannot finish negotiated request");
        result.correlation_id = second.second;
        result.session_id = QStringLiteral("wrong-session");
        inject(MsgType::DeviceOperationResponseContext, TelemetryCodec::serializeDeviceOperationResponseContext(result));
        require(delivered.size() == 1, "session mismatch cannot finish negotiated request");
        result.session_id = QStringLiteral("context-session");
        inject(MsgType::DeviceOperationResponseContext, TelemetryCodec::serializeDeviceOperationResponseContext(result));
        require(delivered.size() == 2 && delivered.last().correlation_id == second.second, "matching reused request completes");
        operation.request_id = 2501;
        const auto early = start();
        result.request_id = operation.request_id; result.correlation_id = early.second;
        inject(MsgType::DeviceOperationResponseContext, TelemetryCodec::serializeDeviceOperationResponseContext(result));
        require(delivered.size() == 3 && delivered.last().correlation_id == early.second &&
                delivered.last().session_id == QStringLiteral("context-session"),
                "contextual result is delivered with its own session before a missing ACK");
        const int beforeLateAck = deliveredAcks.size();
        acknowledge(early.first, QStringLiteral("stale-correlation"), true);
        require(deliveredAcks.size() == beforeLateAck, "late ACK with another correlation is rejected");
        acknowledge(early.first, early.second, true);
        require(deliveredAcks.size() == beforeLateAck + 1 && deliveredAcks.last().correlation_id == early.second &&
                deliveredAcks.last().session_id == QStringLiteral("context-session"),
                "late ACK retains the completed result context");
        acknowledge(early.first, early.second, true);
        require(deliveredAcks.size() == beforeLateAck + 1 && delivered.size() == 3,
                "duplicate late ACK does not repeat the result or ACK");
        operation.request_id = 2502;
        const auto legacy = start();
        result.request_id = operation.request_id;
        inject(MsgType::DeviceOperationResponse, TelemetryCodec::serializeDeviceOperationResponse(result));
        require(delivered.size() == 4 && delivered.last().correlation_id == legacy.second,
                "legacy result before ACK retains existing immediate behavior");
        operation.request_id = 2503; operation.device_id = SkyDeviceId::Epsilon;
        operation.operation = DeviceOperation::CalibrateEpsilonMagnetic3D;
        const auto progressRequest = start();
        result.request_id = operation.request_id; result.device_id = operation.device_id;
        result.operation = operation.operation; result.correlation_id = progressRequest.second;
        result.session_id = QStringLiteral("context-session");
        EpsilonMaintenanceResult progress; progress.action = EpsilonMaintenanceAction::Magnetic3D;
        progress.status = EpsilonMaintenanceStatus::Running;
        result.payload = TelemetryCodec::serializeEpsilonMaintenanceResult(progress);
        inject(MsgType::DeviceOperationResponseContext, TelemetryCodec::serializeDeviceOperationResponseContext(result));
        inject(MsgType::DeviceOperationResponseContext, TelemetryCodec::serializeDeviceOperationResponseContext(result));
        require(delivered.size() == 6, "multiple contextual progress updates arrive without an ACK");
        const int beforeProgressAck = deliveredAcks.size();
        acknowledge(progressRequest.first, progressRequest.second, false);
        require(deliveredAcks.size() == beforeProgressAck, "legacy ACK cannot downgrade observed contextual progress");
        result.session_id = QStringLiteral("wrong-session");
        inject(MsgType::DeviceOperationResponseContext, TelemetryCodec::serializeDeviceOperationResponseContext(result));
        require(delivered.size() == 6, "progress cannot switch its observed session");
        result.session_id = QStringLiteral("context-session");
        acknowledge(progressRequest.first, progressRequest.second, true);
        require(deliveredAcks.size() == beforeProgressAck + 1, "matching ACK after progress remains accepted");
        progress.status = EpsilonMaintenanceStatus::Completed;
        result.payload = TelemetryCodec::serializeEpsilonMaintenanceResult(progress);
        inject(MsgType::DeviceOperationResponseContext, TelemetryCodec::serializeDeviceOperationResponseContext(result));
        require(delivered.size() == 7 && delivered.last().correlation_id == progressRequest.second,
                "contextual completion follows progress under the same operation");
        operation.device_id = SkyDeviceId::Fpga; operation.operation = DeviceOperation::FpgaControl;
        operation.request_id = 2504; const auto concurrentA = start();
        operation.request_id = 2505; const auto concurrentB = start();
        acknowledge(concurrentB.first, concurrentB.second, true);
        result.device_id = SkyDeviceId::Fpga; result.operation = DeviceOperation::FpgaControl;
        result.payload.clear(); result.request_id = 2505; result.correlation_id = concurrentB.second;
        inject(MsgType::DeviceOperationResponseContext, TelemetryCodec::serializeDeviceOperationResponseContext(result));
        result.request_id = 2504; result.correlation_id = concurrentA.second;
        inject(MsgType::DeviceOperationResponseContext, TelemetryCodec::serializeDeviceOperationResponseContext(result));
        require(delivered.size() == 9 && delivered[7].correlation_id == concurrentB.second &&
                delivered[8].correlation_id == concurrentA.second && delivered[8].session_id == QStringLiteral("context-session"),
                "concurrent results remain associated even when one ACK is missing");
        operation.request_id = 2504;
        const auto reused = start();
        require(reused.first != concurrentA.first && reused.second != concurrentA.second,
                "completed request ID is reusable while its previous command sequence remains reserved");
        const int beforeConcurrentAck = deliveredAcks.size();
        acknowledge(concurrentA.first, concurrentA.second, true);
        require(deliveredAcks.size() == beforeConcurrentAck + 1 && deliveredAcks.last().correlation_id == concurrentA.second,
                "late ACK is attributed to the completed operation after request ID reuse");
        result.correlation_id = concurrentA.second;
        inject(MsgType::DeviceOperationResponseContext, TelemetryCodec::serializeDeviceOperationResponseContext(result));
        require(delivered.size() == 9, "late result cannot finish the reused request");
        result.correlation_id = reused.second;
        inject(MsgType::DeviceOperationResponseContext, TelemetryCodec::serializeDeviceOperationResponseContext(result));
        require(delivered.size() == 10 && delivered.last().correlation_id == reused.second,
                "reused request completes independently before its ACK");
    }
    {
        GroundTelemetryService service;
        TelemetryCodec framing;
        int ackCount = 0;
        CommandAck lastConfigAck;
        QObject::connect(&service, &GroundTelemetryService::commandAckReceived, &service, [&](const CommandAck& ack) { ++ackCount; lastConfigAck = ack; });
        const auto inject = [&](MsgType type, const QByteArray& payload) {
            const auto frame = framing.encodeFrame(type, payload, 1, 1);
            require(QMetaObject::invokeMethod(&service, "onBytesReceived", Qt::DirectConnection,
                                             Q_ARG(QByteArray, frame)), "config context frame injected");
        };
        const quint16 first = service.sendCommand(CommandId::SetSkyConfig, QByteArrayLiteral("{}"));
        const QString firstId = operationLogs.constLast().correlation_id;
        const auto context = [&](quint16 sequence, const QString& id) {
            inject(MsgType::SkyConfigApplyResultContext,
                   TelemetryCodec::serializeSkyConfigApplyResultContext({{"success", true}}, id, QStringLiteral("session"), sequence));
        };
        const auto acknowledge = [&](quint16 sequence, const QString& id) {
            CommandAck ack; ack.command_id = CommandId::SetSkyConfig; ack.command_seq = sequence;
            ack.correlation_id = id; ack.session_id = QStringLiteral("session");
            inject(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(ack));
        };
        context(first, firstId);
        GroundTelemetryServiceTestAccess::nextSequence(service, first);
        const quint16 reserved = service.sendCommand(CommandId::RequestStatus);
        require(reserved != first, "completed config reserves its sequence until ACK");
        acknowledge(first, firstId);
        require(ackCount == 1, "matching late config ACK accepted");
        GroundTelemetryServiceTestAccess::nextSequence(service, first);
        const quint16 reused = service.sendCommand(CommandId::SetSkyConfig, QByteArrayLiteral("{}"));
        const QString reusedId = operationLogs.constLast().correlation_id;
        require(reused == first && reusedId != firstId, "config sequence can be reused for a distinct operation");
        const auto countBeforeStale = operationLogs.size();
        context(reused, firstId);
        acknowledge(reused, firstId);
        require(ackCount == 1 && operationLogs.size() == countBeforeStale + 1 &&
                operationLogs.constLast().fields.value(QStringLiteral("event")) == QStringLiteral("ground_command_ack_context_mismatch"),
                "old result and ACK cannot complete or attribute a reused sequence");
        context(reused, reusedId);
        require(operationLogs.constLast().fields.value(QStringLiteral("event")) == QStringLiteral("ground_config_apply_result_context") &&
                operationLogs.constLast().correlation_id == reusedId, "new matching config result completes reused sequence");
        acknowledge(reused, reusedId);
        require(ackCount == 2, "new matching late ACK is accepted");

        const quint16 longSessionSequence = service.sendCommand(CommandId::SetSkyConfig, QByteArrayLiteral("{}"));
        const QString longSessionId = operationLogs.constLast().correlation_id;
        const QString longSession(1300, QLatin1Char('s'));
        CommandAck longAck; longAck.command_id = CommandId::SetSkyConfig; longAck.command_seq = longSessionSequence;
        longAck.correlation_id = longSessionId; longAck.session_id = longSession;
        CommandAck decodedAck;
        require(TelemetryCodec::parseCommandAck(TelemetryCodec::serializeCommandAck(longAck), decodedAck) &&
                decodedAck.correlation_id.isEmpty(), "oversized ACK context falls back to legacy format");
        inject(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(longAck));
        require(ackCount == 3, "legacy ACK still reaches the existing consumer");
        const auto beforeWrongContext = operationLogs.size();
        inject(MsgType::SkyConfigApplyResultContext,
               TelemetryCodec::serializeSkyConfigApplyResultContext({{"success", true}}, QStringLiteral("wrong-id"), longSession, longSessionSequence));
        require(operationLogs.size() == beforeWrongContext, "legacy ACK cannot authorize another operation id");
        inject(MsgType::SkyConfigApplyResultContext,
               TelemetryCodec::serializeSkyConfigApplyResultContext({{"success", true}}, longSessionId, longSession, longSessionSequence));
        require(operationLogs.constLast().fields.value(QStringLiteral("event")) == QStringLiteral("ground_config_apply_result_context") &&
                operationLogs.constLast().correlation_id == longSessionId && operationLogs.constLast().session_id == longSession,
                "context result after legacy ACK retains long session and original operation");

        const quint16 lateSequence = service.sendCommand(CommandId::SetSkyConfig, QByteArrayLiteral("{}"));
        const QString lateId = operationLogs.constLast().correlation_id;
        inject(MsgType::SkyConfigApplyResultContext,
               TelemetryCodec::serializeSkyConfigApplyResultContext({{"success", true}}, lateId, longSession, lateSequence));
        CommandAck lateAck; lateAck.command_id = CommandId::SetSkyConfig; lateAck.command_seq = lateSequence;
        lateAck.session_id = longSession; lateAck.allow_session_id_omission = true;
        inject(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(lateAck));
        require(ackCount == 3, "late legacy ACK without identity is rejected");
        lateAck.correlation_id = QStringLiteral("wrong-id");
        inject(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(lateAck));
        require(ackCount == 3, "late omitted-session ACK with wrong identity is rejected");
        lateAck.correlation_id = lateId;
        inject(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(lateAck));
        require(ackCount == 4 && lastConfigAck.session_id_omitted && lastConfigAck.session_id == longSession &&
                operationLogs.constLast().correlation_id == lateId && operationLogs.constLast().session_id == longSession,
                "sidecar-first omitted-session ACK uses the verified session");

        const quint16 earlySequence = service.sendCommand(CommandId::SetSkyConfig, QByteArrayLiteral("{}"));
        const QString earlyId = operationLogs.constLast().correlation_id;
        longAck.command_seq = earlySequence; longAck.correlation_id = earlyId; longAck.allow_session_id_omission = true;
        inject(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(longAck));
        require(ackCount == 5 && lastConfigAck.session_id_omitted && lastConfigAck.session_id.isEmpty(),
                "ACK-first omitted session remains unknown");
        inject(MsgType::SkyConfigApplyResultContext,
               TelemetryCodec::serializeSkyConfigApplyResultContext({{"success", true}}, earlyId, longSession, earlySequence));
        require(operationLogs.constLast().fields.value(QStringLiteral("event")) == QStringLiteral("ground_config_apply_result_context") &&
                operationLogs.constLast().correlation_id == earlyId && operationLogs.constLast().session_id == longSession,
                "ACK-first sidecar supplies authoritative session");

        DeviceOperationRequest device; device.request_id = 92101; device.device_id = SkyDeviceId::Fpga;
        device.operation = DeviceOperation::FpgaControl;
        const quint16 deviceSequence = service.sendDeviceOperation(device);
        const QString deviceId = operationLogs.constLast().correlation_id;
        DeviceOperationResponse deviceResult; deviceResult.request_id = device.request_id;
        deviceResult.device_id = device.device_id; deviceResult.operation = device.operation;
        deviceResult.correlation_id = deviceId; deviceResult.session_id = QStringLiteral("device-session");
        inject(MsgType::DeviceOperationResponseContext, TelemetryCodec::serializeDeviceOperationResponseContext(deviceResult));
        for (int i = 0; i < 257; ++i) {
            const quint16 sequence = service.sendCommand(CommandId::SetSkyConfig, QByteArrayLiteral("{}"));
            const QString id = operationLogs.constLast().correlation_id;
            context(sequence, id);
        }
        CommandAck deviceAck; deviceAck.command_id = CommandId::DeviceOperation; deviceAck.command_seq = deviceSequence;
        deviceAck.correlation_id = deviceId; deviceAck.session_id = QStringLiteral("device-session");
        deviceAck.typed_response_context_supported = true;
        inject(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(deviceAck));
        require(ackCount == 6 && operationLogs.constLast().correlation_id == deviceId &&
                operationLogs.constLast().fields.value(QStringLiteral("event")) == QStringLiteral("ground_command_ack"),
                "configuration cache pressure cannot evict an outstanding device ACK context");
    }
    {
        GroundTelemetryService service;
        TelemetryCodec framing;
        QList<CommandAck> acknowledgements;
        QObject::connect(&service, &GroundTelemetryService::commandAckReceived, &service,
                         [&](const CommandAck& ack) { acknowledgements.append(ack); });
        const auto inject = [&](const CommandAck& ack) {
            const auto frame = framing.encodeFrame(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(ack), 1, 1);
            require(QMetaObject::invokeMethod(&service, "onBytesReceived", Qt::DirectConnection,
                                             Q_ARG(QByteArray, frame)), "extended ACK injected");
        };
        const quint16 a = service.sendCommand(CommandId::SetSkyConfig, QByteArrayLiteral("{}"));
        const QString idA = operationLogs.constLast().correlation_id;
        const quint16 b = service.sendCommand(CommandId::SetSkyConfig, QByteArrayLiteral("{}"));
        const QString idB = operationLogs.constLast().correlation_id;
        const QString session(1300, QLatin1Char('s'));
        CommandAck ack; ack.command_id = CommandId::SetSkyConfig; ack.command_seq = a;
        ack.correlation_id = idB; ack.session_id = session; ack.extended_ack_context_allowed = true;
        ack.config_result_context_expected = false;
        inject(ack);
        require(acknowledgements.isEmpty(), "extended ACK cannot cross concurrent identities");
        ack.command_seq = b;
        inject(ack);
        require(acknowledgements.size() == 1 && acknowledgements.last().extended_ack_context &&
                acknowledgements.last().correlation_id == idB && acknowledgements.last().session_id == session,
                "extended ACK preserves long session without a result sidecar");
        GroundTelemetryServiceTestAccess::disableExtendedAck(service, a);
        ack.command_seq = a; ack.correlation_id = idA;
        inject(ack);
        require(acknowledgements.size() == 1, "unnegotiated extended ACK is rejected");
        ack.extended_ack_context_allowed = false; ack.session_id.clear();
        inject(ack);
        require(acknowledgements.size() == 2 && acknowledgements.last().correlation_id == idA,
                "legacy ACK remains accepted for the pending operation");

        const quint16 late = service.sendCommand(CommandId::SetSkyConfig, QByteArrayLiteral("{}"));
        const QString lateId = operationLogs.constLast().correlation_id;
        const QByteArray contextPayload = TelemetryCodec::serializeSkyConfigApplyResultContext(
            {{"success", true}}, lateId, session, late);
        const QByteArray contextFrame = framing.encodeFrame(MsgType::SkyConfigApplyResultContext, contextPayload, 1, 1);
        require(QMetaObject::invokeMethod(&service, "onBytesReceived", Qt::DirectConnection,
                                         Q_ARG(QByteArray, contextFrame)), "long-session result injected before ACK");
        ack.command_seq = late; ack.correlation_id = QStringLiteral("wrong-id");
        ack.session_id = session; ack.extended_ack_context_allowed = true;
        inject(ack);
        require(acknowledgements.size() == 2, "late extended ACK still requires its original identity");
        ack.correlation_id = lateId;
        inject(ack);
        require(acknowledgements.size() == 3 && acknowledgements.last().extended_ack_context &&
                acknowledgements.last().session_id == session,
                "late extended ACK matches the previously verified result");
    }
    {
        GroundTelemetryService service;
        TelemetryCodec framing;
        QVector<CommandAck> acks;
        QVector<DeviceOperationResponse> responses;
        QObject::connect(&service, &GroundTelemetryService::commandAckReceived, &service,
                         [&](const CommandAck& ack) { acks.append(ack); });
        QObject::connect(&service, &GroundTelemetryService::deviceOperationResponseReceived, &service,
                         [&](const DeviceOperationResponse& response) { responses.append(response); });
        const auto inject = [&](MsgType type, const QByteArray& payload) {
            const auto frame = framing.encodeFrame(type, payload, 1, 1);
            require(QMetaObject::invokeMethod(&service, "onBytesReceived", Qt::DirectConnection,
                                             Q_ARG(QByteArray, frame)), "extended result frame injected");
        };
        const QString session(5000, QLatin1Char('s'));
        const auto configResult = [&](quint16 sequence, const QString& id, const QString& value) {
            const auto payload = TelemetryCodec::serializeSkyConfigApplyResultContext({{"success", true}}, id, value, sequence, true);
            require(!payload.isEmpty(), "extended config context encodes");
            inject(MsgType::SkyConfigApplyResultContext, payload);
        };
        const auto configAck = [&](quint16 sequence, const QString& id) {
            CommandAck ack; ack.command_id = CommandId::SetSkyConfig; ack.command_seq = sequence;
            ack.correlation_id = id; ack.session_id = session; ack.extended_ack_context_allowed = true;
            ack.config_result_context_expected = true;
            inject(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(ack));
        };
        const quint16 a = service.sendCommand(CommandId::SetSkyConfig, QByteArrayLiteral("{}"));
        const QString idA = operationLogs.constLast().correlation_id;
        const quint16 b = service.sendCommand(CommandId::SetSkyConfig, QByteArrayLiteral("{}"));
        const QString idB = operationLogs.constLast().correlation_id;
        const auto beforeWrong = operationLogs.size();
        configResult(a, idB, session);
        require(operationLogs.size() == beforeWrong, "extended config result cannot cross concurrent identities");
        GroundTelemetryServiceTestAccess::disableExtendedResult(service, a);
        configResult(a, idA, session);
        require(operationLogs.size() == beforeWrong, "unnegotiated extended config result is rejected");
        configResult(b, idB, session);
        require(operationLogs.constLast().correlation_id == idB && operationLogs.constLast().session_id == session,
                "result-first config retains extended session");
        configAck(b, idB);
        require(acks.size() == 1 && acks.last().session_id == session, "late extended ACK matches config result");
        configAck(a, idA);
        require(acks.size() == 2 && acks.last().session_id == session, "extended ACK precedes result");
        const auto beforeUnnegotiated = operationLogs.size();
        configResult(a, idA, session);
        require(operationLogs.size() == beforeUnnegotiated, "ACK-first cache rejects unnegotiated result");

        const quint16 c = service.sendCommand(CommandId::SetSkyConfig, QByteArrayLiteral("{}"));
        const QString idC = operationLogs.constLast().correlation_id;
        configAck(c, idC);
        const auto beforeMismatch = operationLogs.size();
        configResult(c, idC, session + QLatin1Char('x'));
        require(operationLogs.size() == beforeMismatch, "ACK-first result cannot change the known session");
        configResult(c, idC, session);
        require(operationLogs.constLast().correlation_id == idC && operationLogs.constLast().session_id == session,
                "ACK-first extended config result retains session");

        DeviceOperationRequest request; request.request_id = 98001; request.device_id = SkyDeviceId::Fpga;
        request.operation = DeviceOperation::FpgaControl;
        const quint16 firstDevice = service.sendDeviceOperation(request);
        const QString firstDeviceId = operationLogs.constLast().correlation_id;
        DeviceOperationResponse response; response.request_id = request.request_id; response.device_id = request.device_id;
        response.operation = request.operation; response.correlation_id = firstDeviceId; response.session_id = session;
        auto payload = TelemetryCodec::serializeDeviceOperationResponseContext(response, true);
        require(!payload.isEmpty(), "extended device context encodes");
        inject(MsgType::DeviceOperationResponseContext, payload);
        require(responses.size() == 1 && responses.last().session_id == session, "result-first device retains long session");
        CommandAck deviceAck; deviceAck.command_id = CommandId::DeviceOperation; deviceAck.command_seq = firstDevice;
        deviceAck.correlation_id = firstDeviceId; deviceAck.session_id = session;
        deviceAck.typed_response_context_supported = true; deviceAck.extended_ack_context_allowed = true;
        inject(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(deviceAck));
        require(acks.size() == 4 && acks.last().session_id == session, "late device ACK matches result session");
        request.request_id = 98002;
        const quint16 secondDevice = service.sendDeviceOperation(request);
        const QString secondDeviceId = operationLogs.constLast().correlation_id;
        deviceAck.command_seq = secondDevice; deviceAck.correlation_id = secondDeviceId;
        inject(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(deviceAck));
        response.request_id = request.request_id; response.correlation_id = secondDeviceId;
        response.session_id = session + QLatin1Char('x');
        inject(MsgType::DeviceOperationResponseContext, TelemetryCodec::serializeDeviceOperationResponseContext(response, true));
        require(responses.size() == 1, "device result cannot change ACK session");
        response.session_id = session;
        inject(MsgType::DeviceOperationResponseContext, TelemetryCodec::serializeDeviceOperationResponseContext(response, true));
        require(responses.size() == 2 && responses.last().correlation_id == secondDeviceId &&
                responses.last().session_id == session, "ACK-first device result retains session");
    }
    {
        GroundTelemetryService service;
        TelemetryCodec framing;
        const auto injectAck = [&](quint16 sequence, const QString& id, const QString& session) {
            CommandAck ack;
            ack.command_id = CommandId::SetSkyConfig;
            ack.command_seq = sequence;
            ack.correlation_id = id;
            ack.session_id = session;
            const auto frame = framing.encodeFrame(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(ack), 1, 1);
            require(QMetaObject::invokeMethod(&service, "onBytesReceived", Qt::DirectConnection,
                                              Q_ARG(QByteArray, frame)), "config ACK injected before disconnect");
        };
        const quint16 first = service.sendCommand(CommandId::SetSkyConfig, QByteArrayLiteral("{}"));
        const QString firstId = operationLogs.constLast().correlation_id;
        const quint16 second = service.sendCommand(CommandId::SetSkyConfig, QByteArrayLiteral("{}"));
        const QString secondId = operationLogs.constLast().correlation_id;
        require(first && second && firstId != secondId, "concurrent configurations have distinct identities");
        injectAck(first, firstId, QStringLiteral("session-one"));
        injectAck(second, secondId, QStringLiteral("session-two"));

        bool reentered = false;
        QString replacementId;
        const auto reentry = QObject::connect(&logs, &LogService::recordPublished, &service,
            [&](const LogRecord& record) {
                if (!reentered && record.fields.value(QStringLiteral("stage")) == QStringLiteral("awaiting_config_result")) {
                    reentered = true;
                    service.close();
                    require(service.sendCommand(CommandId::SaveSkyConfig) != 0, "new operation survives nested disconnect");
                    replacementId = operationLogs.constLast().correlation_id;
                }
            }, Qt::DirectConnection);
        const auto beforeDisconnect = operationLogs.size();
        service.close();
        QObject::disconnect(reentry);
        int firstDisconnect = 0, secondDisconnect = 0, replacementDisconnect = 0;
        for (qsizetype i = beforeDisconnect; i < operationLogs.size(); ++i) {
            const auto& record = operationLogs[i];
            if (record.fields.value(QStringLiteral("event")) != QStringLiteral("ground_command_disconnected")) continue;
            if (record.correlation_id == firstId && record.session_id == QStringLiteral("session-one") &&
                record.fields.value(QStringLiteral("stage")) == QStringLiteral("awaiting_config_result")) ++firstDisconnect;
            if (record.correlation_id == secondId && record.session_id == QStringLiteral("session-two") &&
                record.fields.value(QStringLiteral("stage")) == QStringLiteral("awaiting_config_result")) ++secondDisconnect;
            if (record.correlation_id == replacementId) ++replacementDisconnect;
        }
        require(reentered && !replacementId.isEmpty() && firstDisconnect == 1 && secondDisconnect == 1 &&
                    replacementDisconnect == 0,
                "disconnect preserves each awaiting configuration context across synchronous reentry");
        service.close();
        require(operationLogs.constLast().correlation_id == replacementId &&
                    operationLogs.constLast().fields.value(QStringLiteral("event")) == QStringLiteral("ground_command_disconnected"),
                "replacement operation remains pending until its own disconnect");
    }
    {
        GroundTelemetryService service;
        TelemetryCodec framing;
        QHash<QString, QString> sessions;
        const auto before = operationLogs.size();
        for (int i = 0; i < 257; ++i) {
            const quint16 sequence = service.sendCommand(CommandId::SetSkyConfig, QByteArrayLiteral("{}"));
            const QString id = operationLogs.constLast().correlation_id;
            const QString session = QStringLiteral("config-session-%1").arg(i);
            sessions.insert(id, session);
            CommandAck ack;
            ack.command_id = CommandId::SetSkyConfig;
            ack.command_seq = sequence;
            ack.correlation_id = id;
            ack.session_id = session;
            const auto frame = framing.encodeFrame(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(ack), 1, 1);
            require(QMetaObject::invokeMethod(&service, "onBytesReceived", Qt::DirectConnection,
                                              Q_ARG(QByteArray, frame)), "config cache ACK injected");
        }
        QString evictedId;
        for (qsizetype i = before; i < operationLogs.size(); ++i) {
            const auto& record = operationLogs[i];
            if (record.fields.value(QStringLiteral("event")) == QStringLiteral("ground_config_result_context_evicted")) {
                require(evictedId.isEmpty() && sessions.value(record.correlation_id) == record.session_id &&
                            record.fields.value(QStringLiteral("stage")) == QStringLiteral("awaiting_config_result"),
                        "capacity eviction logs the original config context");
                evictedId = record.correlation_id;
            }
        }
        require(!evictedId.isEmpty(), "capacity eviction is observable");
        require(waitUntil([&] {
            for (qsizetype i = before; i < operationLogs.size(); ++i) {
                const auto& record = operationLogs[i];
                if (record.fields.value(QStringLiteral("event")) == QStringLiteral("ground_config_result_context_expired"))
                    return record.correlation_id != evictedId && sessions.value(record.correlation_id) == record.session_id &&
                           record.fields.value(QStringLiteral("stage")) == QStringLiteral("awaiting_config_result");
            }
            return false;
        }, 34000), "real timer expiration logs the matching retained config context");
    }
    {
        GroundTelemetryService sequenceTest;
        for (unsigned expected = 1; expected <= 65535; ++expected)
            require(sequenceTest.sendCommand(CommandId::RequestStatus) == expected, "command sequence stays nonzero and unique");
        require(sequenceTest.sendCommand(CommandId::RequestStatus) == 0, "exhausted command sequence space rejects another request");
    }
    std::cout<<"fpga_remote_controller_test passed\n";
}
