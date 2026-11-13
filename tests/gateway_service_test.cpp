#include "test_settings_sandbox.h"
#include "ground/gateway/GatewayService.h"
#include "ground/devices/GroundTelemetryService.h"
#include "TelemetryCodec.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <cstdlib>
#include <iostream>
#include <memory>

using namespace VaporView;
using namespace VaporView::Ground::Gateway;

namespace
{
void require(bool condition, const char *message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
template<typename Predicate> bool waitUntil(Predicate predicate, int timeout = 6000)
{
    QElapsedTimer timer;
    timer.start();
    do
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (predicate()) return true;
        QThread::msleep(1);
    } while (timer.elapsed() < timeout);
    return predicate();
}
void settle(int milliseconds)
{
    QElapsedTimer timer;
    timer.start();
    waitUntil([&] { return timer.elapsed() >= milliseconds; }, milliseconds + 1000);
}
void write(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write test file");
}

class Cloud final : public QTcpServer
{
public:
    Cloud()
    {
        require(listen(QHostAddress::LocalHost, 0), "mock cloud listen");
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (auto *socket = nextPendingConnection())
            {
                auto buffer = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
                    buffer->append(socket->readAll());
                    const qsizetype end = buffer->indexOf("\r\n\r\n");
                    if (end < 0) return;
                    const QByteArray headers = buffer->left(end);
                    qint64 length = 0;
                    for (const auto& header : headers.split('\n'))
                        if (header.toLower().startsWith("content-length:")) length = header.mid(15).trimmed().toLongLong();
                    if (buffer->size() < end + 4 + length) return;
                    require(headers.contains("Authorization: Bearer " + token.toUtf8()), "bearer header");
                    ++requests;
                    const QByteArray target = headers.split('\n').first().split(' ').value(1);
                    const QJsonObject body = QJsonDocument::fromJson(buffer->mid(end + 4, length)).object();
                    QJsonObject response;
                    bool failure = offline;
                    if (target.startsWith("/gateway/v1/commands"))
                    {
                        if (failPoll) failure = true;
                        ++polls;
                        response.insert("commands", commands);
                    }
                    else if (target == "/gateway/v1/telemetry" || target == "/gateway/v1/command-results")
                    {
                        QJsonArray accepted;
                        for (const auto& item : body.value("items").toArray())
                        {
                            const auto object = item.toObject();
                            accepted.append(object.value("id"));
                            if (!failure)
                            {
                                if (target.endsWith("telemetry")) telemetry.append(object);
                                else results.append(object);
                            }
                        }
                        response.insert("acceptedIds", accepted);
                    }
                    else if (target == "/gateway/v1/sessions")
                    {
                        if (blockSessionAfterFirst && sessionChunks >= 1) failure = true;
                        if (!failure)
                        {
                            if (body.value("complete").toBool()) ++sessionsCompleted;
                            else
                            {
                                const QString path = body.value("relativePath").toString();
                                const QByteArray data = QByteArray::fromBase64(body.value("dataBase64").toString().toLatin1());
                                const qint64 offset = body.value("offset").toString().toLongLong();
                                require(offset == sessionData[path].size(), "session resumes at confirmed offset");
                                sessionData[path].append(data);
                                ++sessionChunks;
                                response.insert("nextOffset", QString::number(offset + data.size()));
                            }
                            response.insert("accepted", true);
                        }
                    }
                    else require(false, "known v1 endpoint");
                    const QByteArray payload = QJsonDocument(response).toJson(QJsonDocument::Compact);
                    const QByteArray status = failure ? "503 Service Unavailable" : "200 OK";
                    socket->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
                                  QByteArray::number(payload.size()) + "\r\n\r\n" + payload);
                    socket->disconnectFromHost();
                    buffer->clear();
                });
            }
        });
    }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort())); }
    bool offline = true;
    QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    bool blockSessionAfterFirst = false;
    bool failPoll = false;
    int requests = 0, polls = 0, sessionChunks = 0, sessionsCompleted = 0;
    QJsonArray commands;
    QList<QJsonObject> telemetry, results;
    QHash<QString, QByteArray> sessionData;
};

QJsonObject command(const QString& id, int type, const QString& device, int seconds)
{
    return {{"id", id}, {"commandId", type}, {"payloadBase64", ""}, {"deviceId", device},
            {"expiresAt", QDateTime::currentDateTimeUtc().addSecs(seconds).toString(Qt::ISODateWithMs)}};
}
bool hasResult(const Cloud& cloud, const QString& id, const QString& status)
{
    for (const auto& result : cloud.results)
        if (result.value("commandId") == id && result.value("status") == status) return true;
    return false;
}
}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary cache");
    Cloud cloud;
    QTcpServer sky;
    require(sky.listen(QHostAddress::LocalHost, 0), "Sky listen");
    QPointer<QTcpSocket> peer;
    TelemetryCodec decoder, encoder;
    int skyConnections = 0, skyCommands = 0, localUpdates = 0;
    bool acknowledge = true;
    CommandMessage lastCommand;
    QObject::connect(&sky, &QTcpServer::newConnection, &sky, [&] {
        ++skyConnections;
        peer = sky.nextPendingConnection();
        QObject::connect(peer, &QTcpSocket::disconnected, peer, &QObject::deleteLater);
        QObject::connect(peer, &QTcpSocket::readyRead, &sky, [&] {
            for (const auto& frame : decoder.feedBytes(peer->readAll()))
            {
                CommandMessage message;
                if (frame.type != MsgType::Command || !TelemetryCodec::parseCommand(frame.payload, message)) continue;
                lastCommand = message;
                ++skyCommands;
                if (acknowledge)
                {
                    CommandAck ack;
                    ack.command_id = message.command_id;
                    ack.command_seq = message.command_seq;
                    ack.correlation_id = message.correlation_id;
                    peer->write(encoder.encodeFrame(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(ack), 1, 1));
                }
            }
        });
    });
    GroundTelemetryService service;
    require(service.openTcp("127.0.0.1", sky.serverPort()), "reuse existing Sky connection");
    require(waitUntil([&] { return !peer.isNull(); }), "Sky accepted");
    QObject::connect(&service, &GroundTelemetryService::basicTelemetryUpdated, &service, [&](const auto&) { ++localUpdates; });
    GatewayConfig config;
    config.enabled = true;
    config.serverUrl = cloud.url();
    config.deviceId = "test-aircraft";
    config.token = cloud.token;
    config.cacheDirectory = temporary.filePath("gateway-cache");
    config.allowRemoteCommands = true;

    // Data is buffered while the cloud is unavailable, while local display keeps receiving.
    {
        GatewayService gateway;
        gateway.attach(&service);
        settle(250);
        require(cloud.requests == 0, "disabled Gateway performs no network access");
        gateway.configure(config);
        settle(300);
        const QByteArray basic = TelemetryCodec::serializeBasicTelemetry(TelemetryBasic{});
        peer->write(encoder.encodeFrame(MsgType::TelemetryBasic, basic, 11, 123456789));
        peer->write(encoder.encodeFrame(MsgType::SkyConfig,
            QJsonDocument(QJsonObject{{"token", cloud.token}}).toJson(QJsonDocument::Compact), 12, 1));
        require(waitUntil([&] { return localUpdates == 1; }), "offline upload never blocks local parsing");
        settle(500);
        require(cloud.telemetry.isEmpty(), "cloud offline");
    }
    cloud.offline = false;
    cloud.commands = {command("valid", 10, config.deviceId, 120), command("expired", 10, config.deviceId, -1),
                      command("forbidden", 11, config.deviceId, 120), command("wrong-target", 10, "someone-else", 120)};
    {
        GatewayService gateway;
        gateway.attach(&service);
        gateway.configure(config);
        require(waitUntil([&] { return !cloud.telemetry.isEmpty() && hasResult(cloud, "valid", "acknowledged") &&
            hasResult(cloud, "expired", "expired") && hasResult(cloud, "forbidden", "forbidden") &&
            hasResult(cloud, "wrong-target", "wrong-device"); }, 10000), "backlog restored and commands report validated outcomes");
        require(skyCommands == 1 && skyConnections == 1, "one shared Sky socket and one permitted command");
        int forwarded = 0;
        for (const auto& batch : cloud.telemetry)
            for (const auto& value : batch.value("frames").toArray())
            {
                const auto frame = value.toObject();
                require(frame.value("type").toInt() == static_cast<int>(MsgType::TelemetryBasic), "config secrets excluded from forwarding");
                require(frame.value("timeUs").toString() == "123456789", "microseconds preserved as string");
                ++forwarded;
            }
        require(forwarded == 1, "one parsed frame forwarded once");
    }
    {
        GatewayService gateway;
        gateway.attach(&service);
        const int polls = cloud.polls;
        gateway.configure(config);
        require(waitUntil([&] { return cloud.polls > polls; }), "dedup claims survive Gateway restart");
        settle(500);
        require(skyCommands == 1, "restarted Gateway never executes claimed command twice");
        cloud.commands = {};
        const QString session = temporary.filePath("closed-session");
        require(QDir().mkpath(session), "create session");
        write(session + "/session.json", "{\"session_format\":\"vaporview.session\",\"session_format_version\":1,\"state\":\"complete\"}");
        write(session + "/wave.dat", QByteArray(600000, 's'));
        cloud.blockSessionAfterFirst = true;
        gateway.enqueueSession(session);
        require(waitUntil([&] { return cloud.sessionChunks == 1; }), "first session chunk confirmed");
        settle(300);
    }
    cloud.blockSessionAfterFirst = false;
    {
        GatewayService gateway;
        gateway.attach(&service);
        bool completed = false;
        QObject::connect(&gateway, &GatewayService::sessionSyncFinished, &gateway,
                         [&](const QString&, bool success, const QString&) { completed = success; });
        gateway.configure(config);
        require(waitUntil([&] { return completed; }, 10000), "Session resumes from persistent cursor after restart");
        require(cloud.sessionsCompleted == 1 && cloud.sessionData.value("wave.dat") == QByteArray(600000, 's'), "Session bytes are complete");
        const int requests = cloud.requests;
        GatewayConfig invalid = config;
        invalid.serverUrl = QUrl("http://example.com");
        gateway.configure(invalid);
        settle(500);
        require(cloud.requests == requests && gateway.statusText().contains(QStringLiteral("配置无效")), "remote cleartext endpoint refused");
    }
    // A failing command API must not hold up healthy telemetry uploads.
    {
        cloud.failPoll = true;
        const auto before = cloud.telemetry.size();
        GatewayService gateway;
        gateway.attach(&service);
        gateway.configure(config);
        settle(300);
        peer->write(encoder.encodeFrame(MsgType::TelemetryBasic, TelemetryCodec::serializeBasicTelemetry(TelemetryBasic{}), 13, 2));
        require(waitUntil([&] { return cloud.telemetry.size() > before; }, 2500), "command polling failure cannot starve telemetry");
        cloud.failPoll = false;
    }
    // Saving identical settings preserves the live ACK association.
    {
        acknowledge = false;
        cloud.commands = {command("same-settings", 10, config.deviceId, 120)};
        const int before = skyCommands;
        GatewayService gateway;
        gateway.attach(&service);
        gateway.configure(config);
        require(waitUntil([&] { return skyCommands == before + 1; }), "command waits for ACK");
        gateway.configure(config);
        CommandAck ack;
        ack.command_id = lastCommand.command_id;
        ack.command_seq = lastCommand.command_seq;
        ack.correlation_id = lastCommand.correlation_id;
        peer->write(encoder.encodeFrame(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(ack), 14, 3));
        require(waitUntil([&] { return hasResult(cloud, "same-settings", "acknowledged"); }), "same config does not lose pending ACK");
        acknowledge = true;
        cloud.commands = {};
    }
    // Exit must drain the entire bounded ingress, not just its first 256 KiB.
    {
        cloud.offline = true;
        GatewayService gateway;
        gateway.attach(&service);
        gateway.configure(config);
        settle(300);
        TelemetryFrame frame;
        frame.type = MsgType::FpgaStatus;
        frame.time_us = 987654321;
        frame.payload = QByteArray(100000, 'x');
        for (int i = 0; i < 18; ++i) { frame.seq = static_cast<quint16>(i); service.frameReceived(frame); }
    }
    cloud.offline = false;
    {
        GatewayService gateway;
        gateway.attach(&service);
        gateway.configure(config);
        require(waitUntil([&] {
            int recovered = 0;
            for (const auto& batch : cloud.telemetry)
                for (const auto& value : batch.value("frames").toArray())
                    if (value.toObject().value("timeUs").toString() == "987654321") ++recovered;
            return recovered == 18;
        }, 10000), "all ingress batches persist on shutdown");
    }
    // A damaged dedup file must disable remote execution instead of forgetting claims.
    {
        GatewayConfig damaged = config;
        damaged.deviceId = "damaged-ledger";
        const QByteArray identity = QCryptographicHash::hash(damaged.serverUrl.toEncoded() + '\n' + damaged.deviceId.toUtf8(),
                                                            QCryptographicHash::Sha256).toHex();
        const QString directory = QDir(damaged.cacheDirectory).filePath(QString::fromLatin1(identity));
        require(QDir().mkpath(directory), "create damaged ledger namespace");
        write(directory + "/commands.json", "{broken-json");
        cloud.commands = {command("must-not-replay", 10, damaged.deviceId, 120)};
        const int before = skyCommands, polls = cloud.polls;
        GatewayService gateway;
        gateway.attach(&service);
        gateway.configure(damaged);
        require(waitUntil([&] { return cloud.polls > polls; }), "damaged ledger polls safely");
        settle(300);
        require(skyCommands == before && gateway.statusText().contains(QStringLiteral("去重记录损坏")), "damaged ledger fails closed");
        cloud.commands = {};
    }
    // The existing retry layer must honor remote expiry after the first send.
    acknowledge = false;
    const int before = skyCommands;
    int expiredTimeouts = 0;
    QObject::connect(&service, &GroundTelemetryService::commandTimedOut, &service, [&](CommandId, quint16) { ++expiredTimeouts; });
    require(service.sendCommand(CommandId::RequestStatus, {}, nullptr, QDateTime::currentMSecsSinceEpoch() + 100) != 0, "send command with deadline");
    require(waitUntil([&] { return skyCommands == before + 1; }), "initial command sent");
    require(waitUntil([&] { return expiredTimeouts == 1; }, 2000), "deadline terminates retry state");
    settle(1000);
    require(skyCommands == before + 1, "no retries after expiry");
    require(skyConnections == 1, "Gateway creates no second Sky connection");
    service.close();
    std::cout << "Gateway lifecycle, offline replay, command validation and Session resume passed\n";
    return 0;
}
