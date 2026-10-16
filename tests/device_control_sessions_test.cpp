#include "test_settings_sandbox.h"
#include "ground/devices/RemoteConfigSession.h"
#include "ground/devices/FpgaControlSession.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPointer>
#include <QTcpServer>
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char *message)
{
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

template<class Predicate> bool waitUntil(Predicate predicate)
{
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < 3000) QCoreApplication::processEvents();
    return predicate();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace VaporView;
    using namespace VaporView::Ground::Devices;
    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost, 0), "listen isolated telemetry peer");
    RemoteSkyController remote;
    require(remote.openTcpAsync("127.0.0.1", server.serverPort()), "start asynchronous link");
    require(!remote.isOpen(), "connection does not block to completion");
    require(waitUntil([&] { return remote.isOpen(); }), "asynchronous link completes");

    RemoteConfigSession config;
    const auto read = config.beginRead(remote);
    require(read && config.reading && config.currentRead(&remote), "read is scoped to live connection");
    require(!config.beginRead(remote) && config.readSequence == read && config.reading,
            "duplicate read cannot discard the active transaction identity");
    require(!config.timeout(CommandId::GetSkyConfig, read + 1) && config.reading,
            "stale timeout cannot cancel current read");
    require(config.timeout(CommandId::GetSkyConfig, read) && !config.pending(), "matching timeout releases transaction");
    config.acceptRead(SkyConfig::defaults(), remote.linkGeneration());
    auto desired = config.desired;
    desired.sensor_transport_path = SensorTransportPath::FpgaRelay;
    config.desired = desired;
    config.completeApply(false, remote.linkGeneration());
    require(config.dirty && config.desired.sensor_transport_path == config.baseline.sensor_transport_path,
            "failed apply restores confirmed transport while retaining dirty draft");

    FpgaControlSession fpga;
    QString error;
    require(fpga.submitRemote(remote, {{"op","refresh"}}, 7, &error) == FpgaControlSession::Submission::Accepted,
            "session routes remote operation");
    const auto request = fpga.requestId;
    require(fpga.matches(request, remote), "response identity belongs to current link");
    require(fpga.submitRemote(remote, {{"op","refresh"}}, 8, &error) == FpgaControlSession::Submission::Busy,
            "one session serializes remote operations");
    remote.close();
    require(!config.currentRead(&remote) && !fpga.matches(request, remote), "closed link invalidates old completions");
    fpga.resetRemote();
    require(!fpga.requestId && !fpga.remoteReady(), "disconnect clears command and freshness state");

    RemoteSkyController cancelled;
    bool lateOpen = false;
    QObject::connect(&cancelled, &RemoteSkyController::linkOpenChanged, [&](bool open) { lateOpen |= open; });
    require(cancelled.openTcpAsync("127.0.0.1", server.serverPort()), "begin cancellable connection");
    cancelled.close();
    QCoreApplication::processEvents();
    require(!cancelled.isOpen() && !lateOpen, "cancelled connection cannot deliver a stale open event");

    QPointer<QThread> worker;
    {
        FpgaControlSession local;
        local.startWorker();
        worker = local.controller()->thread();
        require(worker->isRunning() && worker != app.thread(), "service owns a separate running USB worker");
        local.shutdown();
        require(!worker->isRunning() && !local.controller(), "shutdown joins worker before releasing controller");
    }
    require(!worker, "service destroys its worker on teardown");
}
