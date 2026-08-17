#include "TcpTelemetryLink.h"
#include "TelemetryCodec.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QStringList>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

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

template <typename Predicate>
bool waitUntil(Predicate predicate, int timeoutMs = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (predicate())
        {
            return true;
        }
    }
    return predicate();
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    VaporView::TcpTelemetryLink server;
    bool serverOpen = false;
    bool serverClosed = false;
    std::vector<VaporView::LogRecord> serverRecords;
    QObject::connect(&server, &VaporView::TelemetryLink::openChanged, [&](bool open) {
        serverOpen = open;
        if (!open)
        {
            serverClosed = true;
        }
    });
    QObject::connect(&server, &VaporView::TelemetryLink::logRecordGenerated, [&](const VaporView::LogRecord& record) {
        serverRecords.push_back(record);
    });
    require(server.listen(QStringLiteral("127.0.0.1"), 0), "server listen");
    const quint16 port = server.localPort();
    require(port > 0, "server test port");
    require(serverOpen, "server open signal");
    const QString expectedListenMessage =
        QStringLiteral("TCP 遥测服务端已开始监听：127.0.0.1:%1。").arg(port);
    require(std::any_of(serverRecords.begin(), serverRecords.end(), [&](const VaporView::LogRecord& record) {
        return record.fields.value(QStringLiteral("event")).toString() ==
                   QStringLiteral("telemetry_tcp_server_listening") &&
               record.message == expectedListenMessage &&
               record.fields.value(QStringLiteral("local_port")).toUInt() == port;
    }), "server startup log shows the actual listen endpoint");

    VaporView::TcpTelemetryLink client;
    bool clientOpen = false;
    QObject::connect(&client, &VaporView::TelemetryLink::openChanged, [&](bool open) {
        clientOpen = open;
    });
    QByteArray serverBytes;
    QByteArray clientBytes;
    QObject::connect(&server, &VaporView::TelemetryLink::bytesReceived, [&](const QByteArray& bytes) {
        serverBytes += bytes;
    });
    QObject::connect(&client, &VaporView::TelemetryLink::bytesReceived, [&](const QByteArray& bytes) {
        clientBytes += bytes;
    });

    require(client.connectToHost(QStringLiteral("127.0.0.1"), port), "client connect");
    require(waitUntil([&]() { return clientOpen; }), "client open signal");
    require(waitUntil([&]() {
        return std::any_of(serverRecords.begin(), serverRecords.end(), [](const VaporView::LogRecord& record) {
            return record.source == QStringLiteral("TelemetryLink") &&
                   record.category == QStringLiteral("telemetry.link") &&
                   record.fields.value(QStringLiteral("event")).toString() ==
                       QStringLiteral("telemetry_tcp_client_connected");
        });
    }), "server logs client connect");
    require(waitUntil([&]() { return server.writeBytes(QByteArrayLiteral("sky")) > 0; }), "server write");
    require(waitUntil([&]() { return clientBytes == QByteArrayLiteral("sky"); }), "client receives bytes");
    require(client.writeBytes(QByteArrayLiteral("ground")) > 0, "client write");
    require(waitUntil([&]() { return serverBytes == QByteArrayLiteral("ground"); }), "server receives bytes");

    VaporView::TelemetryCodec encoder;
    VaporView::TelemetryCodec decoder;
    VaporView::TelemetryBasic basic;
    basic.host_time_us = 11;
    basic.latitude_deg = 31.2;
    basic.longitude_deg = 121.4;
    basic.validity_flags = VaporView::BasicHasPosition;
    const QByteArray frame = encoder.encodeFrame(
        VaporView::MsgType::TelemetryBasic,
        VaporView::TelemetryCodec::serializeBasicTelemetry(basic),
        7,
        99);
    clientBytes.clear();
    require(server.writeBytes(frame) > 0, "server write frame");
    QVector<VaporView::TelemetryFrame> frames;
    require(waitUntil([&]() {
        const QVector<VaporView::TelemetryFrame> next = decoder.feedBytes(clientBytes);
        clientBytes.clear();
        if (!next.isEmpty())
        {
            frames += next;
        }
        return !frames.isEmpty();
    }), "client decodes frame");
    VaporView::TelemetryBasic parsed;
    require(frames.front().type == VaporView::MsgType::TelemetryBasic, "frame type");
    require(VaporView::TelemetryCodec::parseBasicTelemetry(frames.front().payload, parsed), "parse basic");
    require(parsed.validity_flags == VaporView::BasicHasPosition, "basic flags");

    VaporView::TcpTelemetryLink replacement;
    bool replacementOpen = false;
    QByteArray replacementBytes;
    QObject::connect(&replacement, &VaporView::TelemetryLink::openChanged, [&](bool open) {
        replacementOpen = open;
    });
    QObject::connect(&replacement, &VaporView::TelemetryLink::bytesReceived, [&](const QByteArray& bytes) {
        replacementBytes += bytes;
    });
    require(replacement.connectToHost(QStringLiteral("127.0.0.1"), port), "replacement connect");
    require(waitUntil([&]() { return replacementOpen; }), "replacement open signal");
    require(waitUntil([&]() { return !clientOpen; }), "old client closed on replacement");
    require(waitUntil([&]() {
        return std::any_of(serverRecords.begin(), serverRecords.end(), [](const VaporView::LogRecord& record) {
            return record.source == QStringLiteral("TelemetryLink") &&
                   record.category == QStringLiteral("telemetry.link") &&
                   record.fields.value(QStringLiteral("event")).toString() ==
                       QStringLiteral("telemetry_tcp_client_replaced");
        });
    }), "server logs client replacement");
    require(waitUntil([&]() { return server.writeBytes(QByteArrayLiteral("probe")) > 0; }), "replacement attached");
    replacementBytes.clear();
    require(waitUntil([&]() {
        server.writeBytes(QByteArrayLiteral("new"));
        return replacementBytes.contains(QByteArrayLiteral("new"));
    }), "replacement receives bytes");

    server.close();
    require(serverClosed, "server close signal");
    require(waitUntil([&]() { return !replacementOpen; }), "replacement close signal");

    std::cout << "telemetry_tcp_link_test passed\n";
    return 0;
}
