#include "ground/devices/EpsilonDeviceSession.h"
#include "ground/devices/RemoteSkyController.h"
#include "TelemetryCodec.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool waitForResult(
    QCoreApplication& app,
    const QVector<VaporView::Ground::Devices::EpsilonSessionResult>& results,
    int timeoutMs = 1500)
{
    QElapsedTimer timer;
    timer.start();
    while (results.isEmpty() && timer.elapsed() < timeoutMs)
    {
        app.processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }
    return !results.isEmpty();
}

VaporView::Ground::EpsilonConfigurationResult successResult()
{
    VaporView::Ground::EpsilonConfigurationResult result;
    result.command_succeeded = true;
    result.live_stream_restarted = true;
    return result;
}

VaporView::EpsilonPacketRatesOperation packetRateOperation()
{
    VaporView::EpsilonPacketRatesOperation operation;
    operation.output_rate_hz = 100;
    operation.callback_rate_hz = 250;
    operation.packet_rates = {{0x40, 250}, {0x50, 100}, {0x5C, 10}};
    operation.packet_rate_signature = QStringLiteral("40=250;50=100;5C=10");
    return operation;
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace VaporView;
    using namespace VaporView::Ground::Devices;

    auto slowPacketRates = std::make_shared<bool>(false);
    EpsilonDeviceSession::LocalAdapter adapter;
    adapter.configurePacketRates = [slowPacketRates](
        const EpsilonPacketRatesOperation& operation,
        const VaporView::Ground::EpsilonDeviceOperation& deviceOperation) {
        require(deviceOperation.port == QStringLiteral("COM42"),
                "local packet-rate adapter receives local device context");
        if (*slowPacketRates)
        {
            QThread::msleep(80);
        }
        require(operation.packet_rates.count(0x40) == 1,
                "local packet-rate adapter receives structured payload");
        return successResult();
    };
    adapter.configureMainAntennaLeverArm = [](
        const EpsilonMainAntennaLeverArmOperation& operation,
        const VaporView::Ground::EpsilonDeviceOperation&) {
        require(operation.x_m == 1.0 && operation.z_m == -0.25,
                "local lever-arm adapter receives structured payload");
        return successResult();
    };
    adapter.configureRtcmInput = [](
        const EpsilonRtcmInputOperation& operation,
        const VaporView::Ground::EpsilonDeviceOperation&) {
        require(operation.device_port_index == 3 && operation.forward_baud == 230400,
                "local RTCM adapter receives structured payload");
        return successResult();
    };

    adapter.readSettings = [](EpsilonSettingsGroup group, const VaporView::Ground::EpsilonDeviceOperation&) {
        auto result = successResult();
        result.settings_snapshot.group = group;
        result.settings_snapshot.values = {{"GNSS_L_IMU_ANT1_X", 1.25}};
        return result;
    };
    adapter.applySettings = [](const EpsilonSettingsOperation& operation, const VaporView::Ground::EpsilonDeviceOperation&) {
        auto result = successResult();
        result.settings_snapshot.group = operation.group;
        result.settings_snapshot.values = operation.values;
        result.settings_snapshot.saved = true;
        result.settings_snapshot.readback_verified = true;
        return result;
    };
    adapter.restartDevice = [](const VaporView::Ground::EpsilonDeviceOperation&) { return successResult(); };
    adapter.readDgnss = [](const VaporView::Ground::EpsilonDeviceOperation&) {
        auto result = successResult();
        result.dgnss_snapshot.values = {{"NTRIP_SVR_DOMAIN", "synthetic.invalid"}};
        return result;
    };
    adapter.applyDgnss = [](const EpsilonDgnssOperation& settings, const VaporView::Ground::EpsilonDeviceOperation&) {
        auto result = successResult();
        result.dgnss_snapshot.values = settings.values;
        result.dgnss_snapshot.saved = true;
        result.dgnss_snapshot.readback_verified = true;
        result.dgnss_snapshot.restart_required = true;
        return result;
    };
    adapter.calibrate = [](EpsilonMaintenanceAction action, const VaporView::Ground::EpsilonDeviceOperation& operation) {
        auto result = successResult();
        result.maintenance_result.action = action;
        result.maintenance_result.status = EpsilonMaintenanceStatus::Acknowledged;
        result.maintenance_result.saved = true;
        result.maintenance_result.restart_required = true;
        if (action == EpsilonMaintenanceAction::Magnetic2D || action == EpsilonMaintenanceAction::Magnetic3D)
        {
            result.maintenance_result.status = EpsilonMaintenanceStatus::Running;
            result.maintenance_result.progress_known = true;
            result.maintenance_result.progress_percent = 25;
            if (operation.maintenance_progress) operation.maintenance_progress(result.maintenance_result);
            for (int attempt = 0; attempt < 1000 && !operation.maintenance_cancel->load(); ++attempt) QThread::msleep(1);
            require(operation.maintenance_cancel->load(), "local magnetic worker receives cancellation token");
            result.maintenance_result.status = EpsilonMaintenanceStatus::Cancelled;
            result.command_succeeded = false;
        }
        return result;
    };

    EpsilonDeviceSession session(std::move(adapter), nullptr);
    session.setLocalAvailable(true, QStringLiteral("simulated local EPSILON"));
    QVector<EpsilonSessionResult> results;
    QObject::connect(&session, &EpsilonDeviceSession::operationFinished,
                     [&results](const EpsilonSessionResult& result) {
                         results.push_back(result);
                     });

    VaporView::Ground::EpsilonDeviceOperation localDevice;
    localDevice.port = QStringLiteral("COM42");
    const quint64 packetId = session.configurePacketRates(packetRateOperation(), localDevice);
    require(packetId != 0 && waitForResult(app, results) &&
                results.back().request_id == packetId &&
                results.back().success(),
            "local packet-rate operation completes asynchronously");
    results.clear();

    EpsilonMainAntennaLeverArmOperation leverArm;
    leverArm.x_m = 1.0;
    leverArm.y_m = 0.5;
    leverArm.z_m = -0.25;
    const quint64 leverId = session.configureMainAntennaLeverArm(leverArm);
    require(leverId != 0 && waitForResult(app, results) &&
                results.back().operation == EpsilonOperation::ConfigureMainAntennaLeverArm &&
                results.back().success(),
            "local lever-arm operation completes");
    results.clear();

    EpsilonRtcmInputOperation rtcm;
    rtcm.device_port_index = 3;
    rtcm.forward_port = QStringLiteral("COM99");
    rtcm.forward_baud = 230400;
    const quint64 rtcmId = session.configureRtcmInput(rtcm);
    require(rtcmId != 0 && waitForResult(app, results) &&
                results.back().operation == EpsilonOperation::ConfigureRtcmInput &&
                results.back().success(),
            "local RTCM operation completes");
    results.clear();

    session.readSettings(EpsilonSettingsGroup::Installation);
    require(waitForResult(app, results) && results.back().success() &&
            results.back().local_result.settings_snapshot.values.count("GNSS_L_IMU_ANT1_X"), "local settings snapshot survives worker delivery");
    results.clear();
    session.applySettings({EpsilonSettingsGroup::Installation, {{"GNSS_L_IMU_ANT1_X", 2.5}}});
    require(waitForResult(app, results) && results.back().success() && results.back().local_result.settings_snapshot.readback_verified,
            "local settings apply carries verification");
    results.clear();
    session.restartDevice();
    require(waitForResult(app, results) && results.back().success(), "local restart completes on worker");
    results.clear();
    session.calibrate(EpsilonMaintenanceAction::Accelerometer);
    require(waitForResult(app, results) && results.back().success() &&
            results.back().local_result.maintenance_result.status == EpsilonMaintenanceStatus::Acknowledged &&
            !results.back().local_result.maintenance_result.succeeded(),
            "local maintenance ACK and stream recovery do not imply calibration completion");
    results.clear();

    session.readDgnss();
    require(waitForResult(app, results) && results.back().success() && !results.back().local_result.dgnss_snapshot.values.empty(),
            "local DGNSS snapshot passes through asynchronous session");
    results.clear();
    QVector<EpsilonMaintenanceResult> localProgress;
    QObject::connect(&session, &EpsilonDeviceSession::maintenanceProgress, [&](const EpsilonMaintenanceResult& progress) {
        localProgress.push_back(progress);
        session.cancelMaintenance();
    });
    session.calibrate(EpsilonMaintenanceAction::Magnetic2D);
    require(waitForResult(app, results) && !localProgress.empty() && !results.back().success() &&
            results.back().local_result.maintenance_result.status == EpsilonMaintenanceStatus::Cancelled &&
            results.back().local_result.maintenance_result.restart_required && !session.operationPending(),
            "local cancellation waits for final worker result and preserves restart facts");
    results.clear();

    *slowPacketRates = true;
    EpsilonDeviceSession::LocalAdapter fakeMagneticAdapter;
    fakeMagneticAdapter.calibrate = [](EpsilonMaintenanceAction action, const VaporView::Ground::EpsilonDeviceOperation&) {
        auto result = successResult();
        result.maintenance_result.action = action;
        result.maintenance_result.status = EpsilonMaintenanceStatus::Completed;
        result.maintenance_result.saved = true;
        result.maintenance_result.restart_required = true;
        return result;
    };
    EpsilonDeviceSession fakeMagneticSession(std::move(fakeMagneticAdapter), nullptr);
    fakeMagneticSession.setLocalAvailable(true);
    QVector<EpsilonSessionResult> fakeMagneticResults;
    QObject::connect(&fakeMagneticSession, &EpsilonDeviceSession::operationFinished,
        [&](const EpsilonSessionResult& result) { fakeMagneticResults.push_back(result); });
    fakeMagneticSession.calibrate(EpsilonMaintenanceAction::Magnetic2D);
    require(waitForResult(app, fakeMagneticResults) && !fakeMagneticResults.back().success(),
            "2D Completed without measured progress 100 cannot succeed");
    fakeMagneticResults.clear();
    fakeMagneticSession.calibrate(EpsilonMaintenanceAction::Magnetic3D);
    require(waitForResult(app, fakeMagneticResults) && !fakeMagneticResults.back().success(),
            "3D Completed without current High fit below 3 cannot succeed");
    const quint64 staleId = session.configurePacketRates(packetRateOperation(), localDevice);
    require(staleId != 0 && session.operationPending(),
            "slow local EPSILON operation remains pending while the worker is busy");
    session.setBackend(EpsilonBackend::Remote);
    require(!results.isEmpty() && results.back().request_id == staleId &&
                results.back().outcome == EpsilonOperationOutcome::Disconnected,
            "backend switch completes the active EPSILON request as disconnected");
    require(session.operationPending() && !session.operationsAvailable(), "cancelled local worker retains serial ownership until completion");
    const int resultCountAfterSwitch = results.size();
    QThread::msleep(120);
    app.processEvents(QEventLoop::AllEvents, 50);
    require(results.size() == resultCountAfterSwitch,
            "stale EPSILON local completion is ignored after generation changes");

    QTcpServer timeoutServer;
    require(timeoutServer.listen(QHostAddress::LocalHost),
            "EPSILON timeout server listens on localhost");
    QTcpSocket *timeoutSocket = nullptr;
    QObject::connect(&timeoutServer, &QTcpServer::newConnection, [&]() {
        timeoutSocket = timeoutServer.nextPendingConnection();
    });
    RemoteSkyController remoteController;
    EpsilonDeviceSession remoteSession({}, &remoteController);
    remoteSession.setBackend(EpsilonBackend::Remote);
    remoteSession.setRemoteAvailable(true, QStringLiteral("simulated remote EPSILON"));
    QVector<EpsilonSessionResult> timeoutResults;
    QObject::connect(&remoteSession, &EpsilonDeviceSession::operationFinished,
                     [&timeoutResults](const EpsilonSessionResult& result) {
                         timeoutResults.push_back(result);
                     });
    require(remoteController.openTcp(QStringLiteral("127.0.0.1"), timeoutServer.serverPort()),
            "EPSILON timeout controller opens TCP link");
    QElapsedTimer linkTimer;
    linkTimer.start();
    while ((!remoteController.isOpen() || timeoutSocket == nullptr) && linkTimer.elapsed() < 3000)
    {
        app.processEvents(QEventLoop::AllEvents, 20);
    }
    require(remoteController.isOpen() && timeoutSocket != nullptr,
            "EPSILON timeout TCP link is established");
    remoteSession.applySettings({EpsilonSettingsGroup::Installation, {{"GNSS_L_IMU_ANT1_X", 2.5}}});
    require(!timeoutResults.isEmpty() && timeoutResults.back().outcome == EpsilonOperationOutcome::Unsupported,
            "legacy detailed-operation support cannot enable unknown settings writes");
    timeoutResults.clear();
    TelemetryCodec commandCodec;
    remoteSession.readSettings(EpsilonSettingsGroup::Installation);
    DeviceOperationRequest settingsRequest;
    QElapsedTimer requestTimer;
    requestTimer.start();
    while (settingsRequest.request_id == 0 && requestTimer.elapsed() < 1500)
    {
        app.processEvents(QEventLoop::AllEvents, 20);
        for (const auto& frame : commandCodec.feedBytes(timeoutSocket->readAll()))
        {
            CommandMessage command;
            if (frame.type == MsgType::Command && TelemetryCodec::parseCommand(frame.payload, command))
                TelemetryCodec::parseDeviceOperationRequest(command.payload, settingsRequest);
        }
        QThread::msleep(1);
    }
    require(settingsRequest.request_id != 0, "remote settings read dispatches typed request");
    remoteSession.setRemoteAvailable(false, QStringLiteral("navigation stale during settings"));
    require(remoteSession.operationPending(), "navigation staleness does not cancel an active settings transaction");
    DeviceOperationResponse settingsResponse;
    settingsResponse.request_id = settingsRequest.request_id;
    settingsResponse.device_id = SkyDeviceId::Epsilon;
    settingsResponse.operation = DeviceOperation::ReadEpsilonSettings;
    EpsilonSettingsSnapshot snapshot;
    snapshot.values = {{"GNSS_L_IMU_ANT1_X", 1.25}};
    settingsResponse.payload = TelemetryCodec::serializeEpsilonSettingsSnapshot(snapshot);
    auto wrongDeviceResponse = settingsResponse;
    wrongDeviceResponse.device_id = SkyDeviceId::Ai8TemperatureController;
    timeoutSocket->write(commandCodec.encodeFrame(MsgType::DeviceOperationResponse,
        TelemetryCodec::serializeDeviceOperationResponse(wrongDeviceResponse), 1, 0));
    timeoutSocket->flush();
    requestTimer.restart();
    while (requestTimer.elapsed() < 100) app.processEvents(QEventLoop::AllEvents, 20);
    require(timeoutResults.empty() && remoteSession.operationPending(),
            "wrong-device EPSILON response cannot consume the active request");
    timeoutSocket->write(commandCodec.encodeFrame(MsgType::DeviceOperationResponse,
        TelemetryCodec::serializeDeviceOperationResponse(settingsResponse), 1, 0));
    timeoutSocket->flush();
    require(waitForResult(app, timeoutResults) && timeoutResults.back().success() &&
            timeoutResults.back().local_result.settings_snapshot.values == snapshot.values &&
            remoteController.epsilonSettingsSupport() == DeviceOperationSupport::Supported,
            "remote snapshot response confirms separate settings capability");
    int staleEpsilonResponses = 0;
    const auto staleConnection = QObject::connect(&remoteController, &RemoteSkyController::deviceOperationResponseReceived,
        [&](const DeviceOperationResponse&) { ++staleEpsilonResponses; });
    timeoutSocket->write(commandCodec.encodeFrame(MsgType::DeviceOperationResponse,
        TelemetryCodec::serializeDeviceOperationResponse(settingsResponse), 1, 0));
    timeoutSocket->flush();
    requestTimer.restart();
    while (requestTimer.elapsed() < 100) app.processEvents(QEventLoop::AllEvents, 20);
    require(staleEpsilonResponses == 0, "completed EPSILON request rejects late typed responses");
    QObject::disconnect(staleConnection);
    timeoutResults.clear();
    remoteSession.readSettings(EpsilonSettingsGroup::Installation);
    requestTimer.restart();
    settingsRequest.request_id = 0;
    while (settingsRequest.request_id == 0 && requestTimer.elapsed() < 1500)
    {
        app.processEvents(QEventLoop::AllEvents, 20);
        for (const auto& frame : commandCodec.feedBytes(timeoutSocket->readAll()))
        {
            CommandMessage command;
            if (frame.type == MsgType::Command && TelemetryCodec::parseCommand(frame.payload, command))
                TelemetryCodec::parseDeviceOperationRequest(command.payload, settingsRequest);
        }
        QThread::msleep(1);
    }
    settingsResponse.request_id = settingsRequest.request_id;
    settingsResponse.payload = "{}";
    timeoutSocket->write(commandCodec.encodeFrame(MsgType::DeviceOperationResponse,
        TelemetryCodec::serializeDeviceOperationResponse(settingsResponse), 2, 0));
    timeoutSocket->flush();
    require(waitForResult(app, timeoutResults) && !timeoutResults.back().success() &&
            timeoutResults.back().error_code == CommandErrorCode::InvalidPayload,
            "OK response cannot hide malformed settings snapshot");
    timeoutResults.clear();
    const auto maintenanceRoundTrip = [&](EpsilonMaintenanceStatus status, EpsilonMaintenanceAction responseAction,
                                         CommandErrorCode responseError = CommandErrorCode::Ok) {
        remoteSession.calibrate(EpsilonMaintenanceAction::Gyroscope);
        requestTimer.restart();
        DeviceOperationRequest request;
        while (request.request_id == 0 && requestTimer.elapsed() < 1500)
        {
            app.processEvents(QEventLoop::AllEvents, 20);
            for (const auto& frame : commandCodec.feedBytes(timeoutSocket->readAll()))
            {
                CommandMessage command;
                if (frame.type == MsgType::Command && TelemetryCodec::parseCommand(frame.payload, command))
                    TelemetryCodec::parseDeviceOperationRequest(command.payload, request);
            }
            QThread::msleep(1);
        }
        require(request.request_id != 0 && request.operation == DeviceOperation::CalibrateEpsilonGyroscope,
                "remote calibration dispatches a typed action");
        DeviceOperationResponse response;
        response.request_id = request.request_id;
        response.device_id = SkyDeviceId::Epsilon;
        response.operation = request.operation;
        response.error_code = responseError;
        EpsilonMaintenanceResult maintenance;
        maintenance.action = responseAction;
        maintenance.status = status;
        maintenance.saved = true;
        maintenance.restart_required = true;
        response.payload = TelemetryCodec::serializeEpsilonMaintenanceResult(maintenance);
        timeoutSocket->write(commandCodec.encodeFrame(MsgType::DeviceOperationResponse,
            TelemetryCodec::serializeDeviceOperationResponse(response), 3, 0));
        timeoutSocket->flush();
        require(waitForResult(app, timeoutResults), "remote maintenance response arrives");
    };
    maintenanceRoundTrip(EpsilonMaintenanceStatus::Acknowledged, EpsilonMaintenanceAction::Gyroscope);
    require(timeoutResults.back().success() && !timeoutResults.back().local_result.maintenance_result.succeeded(),
            "remote calibration ACK is delivered without claiming completion");
    timeoutResults.clear();
    maintenanceRoundTrip(EpsilonMaintenanceStatus::Completed, EpsilonMaintenanceAction::Gyroscope);
    require(!timeoutResults.back().success() && timeoutResults.back().error_code == CommandErrorCode::InvalidPayload,
            "unsupported calibration completion claim is rejected");
    timeoutResults.clear();
    maintenanceRoundTrip(EpsilonMaintenanceStatus::SentUnverified, EpsilonMaintenanceAction::Gyroscope);
    require(timeoutResults.back().success() &&
            timeoutResults.back().local_result.maintenance_result.status == EpsilonMaintenanceStatus::SentUnverified &&
            timeoutResults.back().local_result.maintenance_result.saved,
            "unconfirmed action with verified Flash save remains explicitly unconfirmed");
    timeoutResults.clear();
    maintenanceRoundTrip(EpsilonMaintenanceStatus::Acknowledged, EpsilonMaintenanceAction::Level);
    require(!timeoutResults.back().success() && timeoutResults.back().error_code == CommandErrorCode::InvalidPayload,
            "calibration response must match the requested action");
    timeoutResults.clear();
    maintenanceRoundTrip(EpsilonMaintenanceStatus::Failed, EpsilonMaintenanceAction::Gyroscope, CommandErrorCode::InternalError);
    require(!timeoutResults.back().success() && timeoutResults.back().local_result.maintenance_result.restart_required &&
            timeoutResults.back().error_code == CommandErrorCode::InternalError,
            "maintenance stream recovery failure retains pending restart state");
    timeoutResults.clear();
    maintenanceRoundTrip(EpsilonMaintenanceStatus::Failed, EpsilonMaintenanceAction::Level, CommandErrorCode::InternalError);
    require(!timeoutResults.back().success() && !timeoutResults.back().local_result.maintenance_result.restart_required &&
            timeoutResults.back().error_code == CommandErrorCode::InvalidPayload,
            "failed response with another action cannot contaminate pending restart state");
    timeoutResults.clear();
    QVector<EpsilonMaintenanceResult> remoteProgress;
    QObject::connect(&remoteSession, &EpsilonDeviceSession::maintenanceProgress,
        [&](const EpsilonMaintenanceResult& progress) { remoteProgress.push_back(progress); });
    remoteSession.calibrate(EpsilonMaintenanceAction::Magnetic3D);
    DeviceOperationRequest magneticRequest;
    quint16 magneticCommandSequence = 0;
    requestTimer.restart();
    while (!magneticRequest.request_id && requestTimer.elapsed() < 1500)
    {
        app.processEvents(QEventLoop::AllEvents, 20);
        for (const auto& frame : commandCodec.feedBytes(timeoutSocket->readAll()))
        {
            CommandMessage command;
            if (frame.type == MsgType::Command && TelemetryCodec::parseCommand(frame.payload, command))
            {
                TelemetryCodec::parseDeviceOperationRequest(command.payload, magneticRequest);
                magneticCommandSequence = command.command_seq;
            }
        }
    }
    require(magneticRequest.operation == DeviceOperation::CalibrateEpsilonMagnetic3D && magneticRequest.request_id,
            "magnetic calibration dispatches typed asynchronous request");
    DeviceOperationResponse magneticResponse;
    magneticResponse.request_id = magneticRequest.request_id;
    magneticResponse.device_id = SkyDeviceId::Epsilon;
    magneticResponse.operation = magneticRequest.operation;
    EpsilonMaintenanceResult magneticProgress;
    magneticProgress.action = EpsilonMaintenanceAction::Magnetic3D;
    magneticProgress.status = EpsilonMaintenanceStatus::Running;
    magneticProgress.progress_known = true;
    magneticProgress.progress_percent = 35;
    magneticProgress.restart_required = true;
    magneticResponse.payload = TelemetryCodec::serializeEpsilonMaintenanceResult(magneticProgress);
    timeoutSocket->write(commandCodec.encodeFrame(MsgType::DeviceOperationResponse,
        TelemetryCodec::serializeDeviceOperationResponse(magneticResponse), 4, 0));
    timeoutSocket->flush();
    requestTimer.restart();
    while (remoteProgress.empty() && requestTimer.elapsed() < 1500) app.processEvents(QEventLoop::AllEvents, 20);
    require(!remoteProgress.empty() && remoteSession.operationPending() && timeoutResults.empty(),
            "Running progress never completes or removes remote request");
    remoteSession.cancelMaintenance();
    magneticProgress.status = EpsilonMaintenanceStatus::Cancelled;
    magneticResponse.error_code = CommandErrorCode::ConfigApplyFailed;
    CommandAck magneticFailureAck{CommandId::DeviceOperation, magneticCommandSequence, 1, CommandErrorCode::ConfigApplyFailed};
    timeoutSocket->write(commandCodec.encodeFrame(MsgType::CommandAck,
        TelemetryCodec::serializeCommandAck(magneticFailureAck), 5, 0));
    timeoutSocket->flush();
    requestTimer.restart();
    while (requestTimer.elapsed() < 100) app.processEvents(QEventLoop::AllEvents, 20);
    require(timeoutResults.empty() && remoteSession.operationPending(),
            "failure ACK waits for typed magnetic risk result");
    magneticResponse.payload = TelemetryCodec::serializeEpsilonMaintenanceResult(magneticProgress);
    timeoutSocket->write(commandCodec.encodeFrame(MsgType::DeviceOperationResponse,
        TelemetryCodec::serializeDeviceOperationResponse(magneticResponse), 5, 0));
    timeoutSocket->flush();
    require(waitForResult(app, timeoutResults) && !remoteSession.operationPending() &&
            timeoutResults.back().local_result.maintenance_result.status == EpsilonMaintenanceStatus::Cancelled &&
            timeoutResults.back().local_result.maintenance_result.restart_required,
            "remote cancellation only finishes after final response and retains risk state");
    timeoutResults.clear();
    for (double fit : {3.1, 0.5})
    {
        remoteSession.calibrate(EpsilonMaintenanceAction::Magnetic3D);
        magneticRequest.request_id = 0;
        requestTimer.restart();
        while (!magneticRequest.request_id && requestTimer.elapsed() < 1500)
        {
            app.processEvents(QEventLoop::AllEvents, 20);
            for (const auto& frame : commandCodec.feedBytes(timeoutSocket->readAll()))
            {
                CommandMessage command;
                DeviceOperationRequest candidate;
                if (frame.type == MsgType::Command && TelemetryCodec::parseCommand(frame.payload, command) &&
                    TelemetryCodec::parseDeviceOperationRequest(command.payload, candidate) &&
                    candidate.operation == DeviceOperation::CalibrateEpsilonMagnetic3D) magneticRequest = candidate;
            }
        }
        require(magneticRequest.request_id, "3D completion verification request dispatches");
        magneticResponse.request_id = magneticRequest.request_id;
        magneticResponse.error_code = CommandErrorCode::Ok;
        magneticProgress.status = EpsilonMaintenanceStatus::Completed;
        magneticProgress.saved = true;
        magneticProgress.fit_error_known = true;
        magneticProgress.fit_error = fit;
        magneticProgress.algorithm = "High";
        magneticResponse.payload = TelemetryCodec::serializeEpsilonMaintenanceResult(magneticProgress);
        timeoutSocket->write(commandCodec.encodeFrame(MsgType::DeviceOperationResponse,
            TelemetryCodec::serializeDeviceOperationResponse(magneticResponse), 7, 0));
        timeoutSocket->flush();
        require(waitForResult(app, timeoutResults) && timeoutResults.back().success() == (fit < 3),
                "remote 3D completion requires current High algorithm fit strictly below 3");
        timeoutResults.clear();
    }
    remoteSession.readDgnss();
    DeviceOperationRequest dgnssRequest;
    quint16 dgnssCommandSequence = 0;
    requestTimer.restart();
    while (!dgnssRequest.request_id && requestTimer.elapsed() < 1500)
    {
        app.processEvents(QEventLoop::AllEvents, 20);
        for (const auto& frame : commandCodec.feedBytes(timeoutSocket->readAll()))
        {
            CommandMessage command;
            if (frame.type == MsgType::Command && TelemetryCodec::parseCommand(frame.payload, command))
            {
                DeviceOperationRequest candidate;
                if (TelemetryCodec::parseDeviceOperationRequest(command.payload, candidate) && candidate.operation == DeviceOperation::ReadEpsilonDgnss)
                {
                    dgnssRequest = candidate;
                    dgnssCommandSequence = command.command_seq;
                }
            }
        }
    }
    require(dgnssRequest.operation == DeviceOperation::ReadEpsilonDgnss, "remote DGNSS read has distinct operation ID");
    DeviceOperationResponse dgnssResponse;
    dgnssResponse.request_id = dgnssRequest.request_id;
    dgnssResponse.device_id = SkyDeviceId::Epsilon;
    dgnssResponse.operation = dgnssRequest.operation;
    EpsilonDgnssSnapshot dgnssSnapshot;
    dgnssSnapshot.values = {{"NTRIP_SVR_DOMAIN", "synthetic.invalid"}};
    dgnssSnapshot.saved = true;
    dgnssSnapshot.restart_required = true;
    dgnssResponse.error_code = CommandErrorCode::InternalError;
    CommandAck dgnssFailureAck{CommandId::DeviceOperation, dgnssCommandSequence, 1, CommandErrorCode::InternalError};
    timeoutSocket->write(commandCodec.encodeFrame(MsgType::CommandAck,
        TelemetryCodec::serializeCommandAck(dgnssFailureAck), 6, 0));
    timeoutSocket->flush();
    requestTimer.restart();
    while (requestTimer.elapsed() < 100) app.processEvents(QEventLoop::AllEvents, 20);
    require(timeoutResults.empty() && remoteSession.operationPending(), "DGNSS failure ACK does not discard partial snapshot");
    dgnssResponse.payload = TelemetryCodec::serializeEpsilonDgnssSnapshot(dgnssSnapshot);
    timeoutSocket->write(commandCodec.encodeFrame(MsgType::DeviceOperationResponse,
        TelemetryCodec::serializeDeviceOperationResponse(dgnssResponse), 6, 0));
    timeoutSocket->flush();
    require(waitForResult(app, timeoutResults) && !timeoutResults.back().success() &&
            timeoutResults.back().local_result.dgnss_snapshot.values == dgnssSnapshot.values &&
            timeoutResults.back().local_result.dgnss_snapshot.saved &&
            timeoutResults.back().local_result.dgnss_snapshot.restart_required,
            "remote DGNSS failure after ACK retains partial values and restart/save state");
    timeoutResults.clear();
    remoteSession.readDgnss();
    dgnssCommandSequence = 0;
    requestTimer.restart();
    while (!dgnssCommandSequence && requestTimer.elapsed() < 1500)
    {
        app.processEvents(QEventLoop::AllEvents, 20);
        for (const auto& frame : commandCodec.feedBytes(timeoutSocket->readAll()))
        {
            CommandMessage command;
            if (frame.type == MsgType::Command && TelemetryCodec::parseCommand(frame.payload, command))
                dgnssCommandSequence = command.command_seq;
        }
    }
    require(dgnssCommandSequence != 0, "ACK-only legacy failure request dispatches");
    dgnssFailureAck.command_seq = dgnssCommandSequence;
    timeoutSocket->write(commandCodec.encodeFrame(MsgType::CommandAck,
        TelemetryCodec::serializeCommandAck(dgnssFailureAck), 8, 0));
    timeoutSocket->flush();
    require(waitForResult(app, timeoutResults, 35000) && !remoteSession.operationPending() &&
            timeoutResults.back().outcome == EpsilonOperationOutcome::Failed,
            "ACK-only failure falls back to rejection without waiting the long device timeout");
    timeoutResults.clear();
    const quint64 timeoutId = remoteSession.configurePacketRates(packetRateOperation());
    require(timeoutId != 0 && !waitForResult(app, timeoutResults, 5000),
            "slow EPSILON operation is not timed out during its reboot window");
    require(waitForResult(app, timeoutResults, 60000) &&
                timeoutResults.back().request_id == timeoutId &&
                timeoutResults.back().outcome == EpsilonOperationOutcome::Timeout,
            "remote EPSILON operation reports timeout when Sky returns no response");
    remoteController.close();
    timeoutServer.close();
    if (timeoutSocket)
    {
        timeoutSocket->deleteLater();
    }

    std::cout << "epsilon_device_session_test passed\n";
    return 0;
}
