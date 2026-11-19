#include "FpgaDeviceController.h"
#include "FpgaDeviceSession.h"
#include "FpgaVirtualTcpTransport.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QtEndian>

#include <cstdio>
#include <cstdlib>

using namespace VaporView;
using namespace VaporView::Ground::Devices;

namespace
{
void require(bool condition, const char *message)
{
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

template<typename Predicate>
void waitFor(Predicate predicate, const char *message)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 2000) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    require(predicate(), message);
}

QByteArray wireFrame(FpgaVlp1::FrameType type, quint32 sequence, quint16 source,
                     quint16 message, const QByteArray &payload)
{
    const auto bytes = FpgaVlp1::buildFrame(type, sequence, source, message,
        {payload.begin(), payload.end()}, true);
    return QByteArray(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size()));
}

void exchange(FpgaDeviceSession &session, QTcpSocket &server)
{
    int pings = 0;
    int events = 0;
    QObject::connect(&session, &FpgaDeviceSession::commandCompleted, &server,
        [&](quint32, quint16 message, bool success, quint32 status, const QByteArray &payload) {
            require(message == 0xfe && success && status == 0 && payload.mid(4) == "hello", "PING reply preserved");
            ++pings;
        });
    QObject::connect(&session, &FpgaDeviceSession::frameReceived, &server,
        [&](quint8, quint32 sequence, quint16 source, quint16, quint64, quint32, const QByteArray &) {
            if (source == 0x44 && (sequence == 90 || sequence == 91)) ++events;
        });
    const auto sequence = session.ping("hello");
    require(sequence != 0, "send PING through session");
    const auto expected = wireFrame(FpgaVlp1::FrameType::Command, sequence, 1, 0xfe, "hello");
    waitFor([&] { return server.bytesAvailable() >= expected.size(); }, "receive client VLP1 wire bytes");
    require(server.readAll() == expected, "wire is exactly VLP1, including existing padding");

    const auto reply = wireFrame(FpgaVlp1::FrameType::Response, sequence, 1, 0xfe, QByteArray(4, 0) + "hello");
    require(server.write(reply.left(3)) == 3, "send a split VLP1 magic");
    waitFor([&] { return session.transport()->diagnostic().bytesRead == 3; }, "consume first partial chunk");
    require(pings == 0 && events == 0, "partial frame does not complete a command");
    require(server.write(reply.mid(3, reply.size() - 4)) == reply.size() - 4, "send partial frame padding");
    waitFor([&] { return session.transport()->diagnostic().bytesRead == quint64(reply.size() - 1); }, "consume incomplete padding");
    require(pings == 0, "buffer final padding byte");
    const auto combined = reply.right(1) + wireFrame(FpgaVlp1::FrameType::Event, 90, 0x44, 1, {}) +
        wireFrame(FpgaVlp1::FrameType::Event, 91, 0x44, 2, {});
    require(server.write(combined) == combined.size(), "send reply tail and coalesced frames together");
    waitFor([&] { return pings == 1 && events == 2; }, "reassemble fragmented and adjacent frames");
    require(session.transport()->diagnostic().bytesWritten == quint64(expected.size()), "count bytes queued to TCP");
    QObject::disconnect(&session, nullptr, &server, nullptr);
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    auto defaultTransport = makeFpgaVirtualTcpTransport();
    require(defaultTransport->discoverDevices().value("devices").toArray().first().toObject().value("locator").toString()
        == QStringLiteral("tcp://127.0.0.1:39181"), "default endpoint");
    require(!defaultTransport->open("tcp://192.0.2.1:39181"), "reject non-loopback locator before networking");
    require(!defaultTransport->open("winusb://physical-device"), "reject USB locator");

    QTcpServer server;
    server.setProxy(QNetworkProxy::NoProxy);
    require(server.listen(QHostAddress::LocalHost), "listen on an ephemeral loopback port");
    const auto port = server.serverPort();
    FpgaDeviceController controller({}, nullptr, port);
    const auto device = controller.discoverDevices("winusb").value("devices").toArray().first().toObject();
    const auto locator = device.value("locator").toString();
    require(device.value("backend").toString() == QStringLiteral("virtual-tcp"), "virtual controller discovery ignores hardware backend");
    controller.connectDevice("winusb://physical-device", "winusb");
    require(!controller.connected(), "virtual controller rejects a USB locator");
    FpgaDeviceSession session(makeFpgaVirtualTcpTransport(port));
    require(session.open(locator), "open exact loopback locator");
    waitFor([&] { return server.hasPendingConnections(); }, "accept local connection");
    auto *peer = server.nextPendingConnection();
    require(peer->peerAddress() == QHostAddress::LocalHost, "IPv4 loopback only");
    exchange(session, *peer);

    QString disconnectError;
    QObject::connect(&session, &FpgaDeviceSession::errorOccurred, &session,
        [&](const QString &message) { disconnectError = message; });
    peer->disconnectFromHost();
    waitFor([&] { return !session.isOpen() && !disconnectError.isEmpty(); }, "surface peer disconnect to common session");
    require(disconnectError.contains("Virtual FPGA TCP connection lost"), "identify TCP disconnect");
    require(session.open(), "reconnect with default locator for configured port");
    waitFor([&] { return server.hasPendingConnections(); }, "accept second connection");
    exchange(session, *server.nextPendingConnection());
    session.close();
    server.close();
    QString openError;
    require(!session.open(locator, &openError) && openError.contains("Cannot connect to virtual FPGA"),
            "connection failure has virtual diagnostic and no fallback");
}
