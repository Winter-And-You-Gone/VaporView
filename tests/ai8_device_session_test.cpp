#include "ground/devices/Ai8DeviceSession.h"
#include "ground/devices/RemoteSkyController.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include <cmath>
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

bool waitForResult(QCoreApplication& app,
                   const QVector<VaporView::Ground::Devices::Ai8SessionResult>& results,
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

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace VaporView;
    using namespace VaporView::Ground::Devices;
    using Page = Ai8TemperatureControllerProtocol::Page;

    auto slowRead = std::make_shared<bool>(false);
    Ai8DeviceSession::LocalAdapter adapter;
    adapter.readPage = [slowRead](Page page,
                                  const Ai8TemperatureControllerProtocol::Selection& selection) {
        if (*slowRead)
        {
            QThread::msleep(80);
        }
        LocalAi8OperationResult result;
        result.success = true;
        result.data.page = page;
        result.data.selection = selection;
        result.data.channel.setpointC = 25.0;
        result.message = QStringLiteral("local read");
        return result;
    };
    adapter.writePage = [](const Ai8TemperatureControllerProtocol::PageData& requested) {
        LocalAi8OperationResult result;
        result.success = true;
        result.data = requested;
        result.message = QStringLiteral("local write");
        return result;
    };

    Ai8DeviceSession session(std::move(adapter), nullptr);
    session.setLocalAvailable(true, QStringLiteral("simulated local AI-8"));
    QVector<Ai8SessionResult> results;
    QObject::connect(&session, &Ai8DeviceSession::operationFinished,
                     [&results](const Ai8SessionResult& result) { results.push_back(result); });

    Ai8TemperatureControllerProtocol::Selection selection;
    selection.channel = 2;
    const quint64 readId = session.readPage(Page::Channel, selection);
    require(readId != 0 && waitForResult(app, results),
            "local session read completes asynchronously");
    require(results.back().request_id == readId && results.back().success() &&
                std::fabs(results.back().data.channel.setpointC - 25.0) < 0.001,
            "local session read returns structured page data");
    results.clear();

    Ai8TemperatureControllerProtocol::PageData writeData = results.isEmpty()
        ? Ai8TemperatureControllerProtocol::PageData{}
        : results.back().data;
    writeData.page = Page::Channel;
    writeData.selection = selection;
    writeData.channel.setpointC = 31.5;
    const quint64 writeId = session.writePage(writeData);
    require(writeId != 0 && waitForResult(app, results) && results.back().success() &&
                std::fabs(results.back().data.channel.setpointC - 31.5) < 0.001,
            "local session write returns read-back page data");
    results.clear();

    const quint64 resetId = session.restoreFactoryDefaults(Page::Channel, selection);
    require(resetId != 0 && waitForResult(app, results) &&
                results.back().outcome == Ai8OperationOutcome::Unsupported,
            "local factory reset reports explicit unsupported outcome");
    results.clear();

    session.setBackend(Ai8Backend::Remote);
    session.setRemoteAvailable(true, QStringLiteral("simulated remote AI-8"));
    const quint64 remoteId = session.readPage(Page::Channel, selection);
    require(remoteId != 0 && waitForResult(app, results) &&
                results.back().outcome == Ai8OperationOutcome::Disconnected,
            "remote session without an open controller reports disconnected");
    results.clear();

    session.setBackend(Ai8Backend::Local);
    session.setLocalAvailable(true, QStringLiteral("simulated local AI-8"));
    *slowRead = true;
    const quint64 staleId = session.readPage(Page::Channel, selection);
    require(staleId != 0, "slow local session read starts");
    session.setBackend(Ai8Backend::Remote);
    require(!results.isEmpty() && results.back().request_id == staleId &&
                results.back().outcome == Ai8OperationOutcome::Disconnected,
            "backend switch completes the active request as disconnected");
    const int resultCountAfterSwitch = results.size();
    QThread::msleep(120);
    app.processEvents(QEventLoop::AllEvents, 50);
    require(results.size() == resultCountAfterSwitch,
            "stale local completion is ignored after backend generation changes");

    QTcpServer timeoutServer;
    require(timeoutServer.listen(QHostAddress::LocalHost),
            "timeout server listens on localhost");
    QTcpSocket *timeoutSocket = nullptr;
    QObject::connect(&timeoutServer, &QTcpServer::newConnection, [&]() {
        timeoutSocket = timeoutServer.nextPendingConnection();
    });
    RemoteSkyController remoteController;
    Ai8DeviceSession remoteSession({}, &remoteController);
    remoteSession.setBackend(Ai8Backend::Remote);
    remoteSession.setRemoteAvailable(true, QStringLiteral("simulated remote AI-8"));
    QVector<Ai8SessionResult> timeoutResults;
    QObject::connect(&remoteSession, &Ai8DeviceSession::operationFinished,
                     [&timeoutResults](const Ai8SessionResult& result) {
                         timeoutResults.push_back(result);
                     });
    require(remoteController.openTcp(QStringLiteral("127.0.0.1"), timeoutServer.serverPort()),
            "remote timeout controller opens TCP link");
    QElapsedTimer linkTimer;
    linkTimer.start();
    while ((!remoteController.isOpen() || timeoutSocket == nullptr) && linkTimer.elapsed() < 3000)
    {
        app.processEvents(QEventLoop::AllEvents, 20);
    }
    require(remoteController.isOpen() && timeoutSocket != nullptr,
            "remote timeout TCP link is established");
    const quint64 timeoutId = remoteSession.readPage(Page::Channel, selection);
    require(timeoutId != 0 && waitForResult(app, timeoutResults, 5000) &&
                timeoutResults.back().request_id == timeoutId &&
                timeoutResults.back().outcome == Ai8OperationOutcome::Timeout,
            "remote operation reports timeout when Sky returns no response");
    remoteController.close();
    timeoutServer.close();
    if (timeoutSocket)
    {
        timeoutSocket->deleteLater();
    }

    std::cout << "ai8_device_session_test passed\n";
    return 0;
}
