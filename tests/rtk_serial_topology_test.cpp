#include "ground/rtk/RtkStreamService.h"

#include <cstdlib>
#include <iostream>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

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
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    RtkStreamConfig config;
    config.server = QStringLiteral("127.0.0.1");
    config.port = QStringLiteral("2101");
    config.mountpoint = QStringLiteral("MOUNT");
    config.outputPort = QStringLiteral("COM3");
    QString error;
    require(validateRtkStreamConfig(config, &error), "ordinary RTK configuration remains valid");
    config.mountpoint = QString(1100, QLatin1Char('A'));
    RtkStreamService service;
    require(!service.start(config, &error) && !service.isRunning() && !error.isEmpty(),
            "oversized mountpoint is rejected before opening RTK streams");
    for (QString RtkStreamConfig::* field : {&RtkStreamConfig::server, &RtkStreamConfig::port,
             &RtkStreamConfig::username, &RtkStreamConfig::password, &RtkStreamConfig::mountpoint})
    {
        RtkStreamConfig candidate;
        candidate.*field = QString(255, QLatin1Char('A'));
        require(validateRtkStreamConfig(candidate), "255-byte component fits its C buffer");
        candidate.*field += QLatin1Char('A');
        require(!validateRtkStreamConfig(candidate), "256-byte component cannot be truncated");
        candidate.*field = QString(86, QChar(0x4E2D));
        require(!validateRtkStreamConfig(candidate), "component limits count UTF-8 bytes");
        candidate.*field = QString(QChar::Null);
        require(!validateRtkStreamConfig(candidate), "embedded null cannot truncate a C path");
    }
    config.mountpoint = QStringLiteral("MOUNT");
    config.outputPort = QString(124, QLatin1Char('P'));
    require(!validateRtkStreamConfig(config), "serial device prefix fits the C destination");
    config.outputPort = QStringLiteral("COM3");
    config.outputPathOverride = QStringLiteral("COM3:115200:8:n:1:") + QString(64, QLatin1Char('F'));
    require(!validateRtkStreamConfig(config), "serial flow control cannot overflow sscanf destination");
    config.outputMode = RtkStreamConfig::OutputMode::TcpClient;
    config.outputPathOverride = QString(1024, QLatin1Char('T'));
    require(!service.start(config, &error), "oversized output path is rejected before C API");
    config.outputPathOverride = QStringLiteral("127.0.0.1:2102");
    require(validateRtkStreamConfig(config), "TCP output override remains supported");

    require(serialPortNamesReferToSamePort(QStringLiteral("COM3"), QStringLiteral("com3")),
            "Windows COM names compare case-insensitively");
    require(serialPortNamesReferToSamePort(QStringLiteral("COM12"), QStringLiteral("\\\\.\\COM12")),
            "Win32 device prefix resolves to the same COM port");
    require(!serialPortNamesReferToSamePort(QStringLiteral("COM3"), QStringLiteral("COM4")),
            "different COM ports remain distinct");
    require(!serialPortNamesReferToSamePort(QString(), QStringLiteral("COM3")),
            "an empty port never conflicts");

    QTcpServer caster;
    QTcpServer destination;
    require(caster.listen(QHostAddress::LocalHost, 0) && destination.listen(QHostAddress::LocalHost, 0),
            "create isolated local NTRIP and output peers");
    config.port = QString::number(caster.serverPort());
    config.outputPathOverride = QStringLiteral("127.0.0.1:%1").arg(destination.serverPort());
    config.sendNmeaGga = true;
    config.nmeaLatitudeDeg = 22.0;
    config.nmeaLongitudeDeg = 114.0;
    config.nmeaHeightM = 10.0;
    require(service.start(config, &error), "start actual RTKLIB relay with local peers");
    auto awaitCondition = [](auto condition, int timeoutMs) {
        QElapsedTimer deadline;
        deadline.start();
        while (!condition() && deadline.elapsed() < timeoutMs) {
            QCoreApplication::processEvents();
            QThread::msleep(2);
        }
        return condition();
    };
    require(awaitCondition([&] { return caster.hasPendingConnections(); }, 3000), "RTK connects to caster");
    QTcpSocket *peer = caster.nextPendingConnection();
    QByteArray received;
    require(awaitCondition([&] { received += peer->readAll(); return received.contains("\r\n\r\n"); }, 3000),
            "NTRIP request arrives");
    peer->write("ICY 200 OK\r\n\r\n");
    received.clear();
    require(awaitCondition([&] { received += peer->readAll(); return received.contains(",2200.0000000,N,11400.0000000,E,"); }, 4000),
            "caster receives initial GGA position");
    service.updateNmeaPosition(true, 23.0, 115.0, 20.0);
    received.clear();
    require(awaitCondition([&] { received += peer->readAll(); return received.contains(",2300.0000000,N,11500.0000000,E,"); }, 2500),
            "running relay uses updated GGA without reconnect");
    service.updateNmeaPosition(false, 0, 0, 0);
    // Drain a GGA already in flight before invalidation, then observe a full cycle.
    QElapsedTimer quiet;
    quiet.start();
    while (quiet.elapsed() < 100) { QCoreApplication::processEvents(); peer->readAll(); QThread::msleep(2); }
    received.clear();
    require(!awaitCondition([&] { received += peer->readAll(); return received.contains("GGA"); }, 1300),
            "invalid position stops generated GGA");
    service.stop();
    std::cout << "rtk_serial_topology_test passed\n";
    return 0;
}
