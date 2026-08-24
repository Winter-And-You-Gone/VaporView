#include "ground/devices/EpsilonDeviceSession.h"
#include "ground/devices/RemoteSkyController.h"

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

    *slowPacketRates = true;
    const quint64 staleId = session.configurePacketRates(packetRateOperation(), localDevice);
    require(staleId != 0 && session.operationPending(),
            "slow local EPSILON operation remains pending while the worker is busy");
    session.setBackend(EpsilonBackend::Remote);
    require(!results.isEmpty() && results.back().request_id == staleId &&
                results.back().outcome == EpsilonOperationOutcome::Disconnected,
            "backend switch completes the active EPSILON request as disconnected");
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
    const quint64 timeoutId = remoteSession.configurePacketRates(packetRateOperation());
    require(timeoutId != 0 && waitForResult(app, timeoutResults, 5000) &&
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
