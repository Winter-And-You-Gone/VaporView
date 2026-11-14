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
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QPointer>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
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

int webDemo()
{
    const QUrl cloudUrl(QString::fromUtf8(qgetenv("GATEWAY_DEMO_URL")));
    const QString token = QString::fromUtf8(qgetenv("GATEWAY_DEMO_TOKEN"));
    const QString device = QString::fromUtf8(qgetenv("GATEWAY_DEMO_DEVICE"));
    const QString reportPath = QString::fromUtf8(qgetenv("GATEWAY_DEMO_REPORT"));
    const QString cancelPath = QString::fromUtf8(qgetenv("GATEWAY_DEMO_CANCEL"));
    QJsonArray checks;
    bool success = true;
    int skyConnections = 0, skyCommands = 0, localUpdates = 0, localStatusUpdates = 0;
    int commandAcks = 0, statusCommands = 0, offlineEvents = 0, onlineEvents = 0;
    bool sessionSynced = false;
    bool cancelled = false;
    QString sessionError;
    qint64 elapsedMs = 0;
    const auto addCheck = [&](const QString& name, bool passed, const QString& detail) {
        checks.append(QJsonObject{{"name", name}, {"passed", passed}, {"detail", detail}});
        success = success && passed;
    };
    const auto finish = [&] {
        const QJsonObject report{{"success", success}, {"passed", success}, {"checks", checks},
            {"skyConnections", skyConnections}, {"skyCommands", skyCommands}, {"localUpdates", localUpdates},
            {"localTelemetryUpdates", localUpdates}, {"localStatusUpdates", localStatusUpdates},
            {"sessionSynced", sessionSynced}, {"sessionCompleted", sessionSynced},
            {"commandAcknowledged", commandAcks > 0}, {"commandAcks", commandAcks},
            {"statusCommands", statusCommands}, {"offlineEvents", offlineEvents}, {"onlineEvents", onlineEvents},
            {"elapsedMs", elapsedMs}, {"sessionError", sessionError}, {"simulatedSkyCore", true}, {"cancelled", cancelled}};
        QSaveFile file(reportPath);
        const auto bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
        if (reportPath.isEmpty() || !QFileInfo(reportPath).isAbsolute() ||
            !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        {
            std::cerr << "FAIL: cannot write web demo report\n";
            return 1;
        }
        std::cout << "Gateway web demo " << (success ? "passed" : "failed") << ": "
                  << localUpdates << " local telemetry updates, " << skyConnections << " Sky socket, "
                  << commandAcks << " command ACKs\n";
        return success ? 0 : 1;
    };
    const bool loopback = cloudUrl.host() == QLatin1String("127.0.0.1") ||
        cloudUrl.host() == QLatin1String("::1") || cloudUrl.host() == QLatin1String("localhost");
    const bool validEnvironment = loopback && cloudUrl.isValid() && cloudUrl.userInfo().isEmpty() &&
        !cloudUrl.hasQuery() && !cloudUrl.hasFragment() &&
        (cloudUrl.scheme() == QLatin1String("http") || cloudUrl.scheme() == QLatin1String("https")) &&
        !token.isEmpty() && !device.isEmpty();
    addCheck("local_demo_environment", validEnvironment, QStringLiteral("Demo cloud must be a loopback server with an isolated token and device."));
    if (!validEnvironment) return finish();

    QTemporaryDir temporary;
    addCheck("isolated_runtime", temporary.isValid(), QStringLiteral("Cache and completed Session use a temporary directory."));
    if (!temporary.isValid()) return finish();
    const QString session = temporary.filePath("demo-session");
    const auto saveFixture = [](const QString& path, const QByteArray& bytes) {
        QSaveFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
    };
    const bool sessionReady = QDir().mkpath(session) &&
        saveFixture(session + "/session.json", QJsonDocument(QJsonObject{
            {"session_format", "vaporview.session"}, {"session_format_version", 1},
            {"state", "complete"}, {"recording_origin", "ground"}, {"session_name", "Gateway software demonstration"}
        }).toJson(QJsonDocument::Compact)) && saveFixture(session + "/wave.dat", QByteArray(600000, 's'));
    addCheck("completed_session_fixture", sessionReady, QStringLiteral("A completed v1 Session contains a 600000-byte file for multipart upload."));
    if (!sessionReady) return finish();

    QTcpServer sky;
    const bool listening = sky.listen(QHostAddress::LocalHost, 0);
    addCheck("simulated_skycore", listening, QStringLiteral("SkyCore is simulated on an ephemeral loopback TCP port."));
    if (!listening) return finish();
    QPointer<QTcpSocket> peer;
    TelemetryCodec encoder;
    quint16 frameSequence = 1;
    bool recording = false;
    QObject::connect(&sky, &QTcpServer::newConnection, &sky, [&] {
        while (auto *socket = sky.nextPendingConnection())
        {
            ++skyConnections;
            peer = socket;
            auto decoder = std::make_shared<TelemetryCodec>();
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, &sky, [&, socket, decoder] {
                for (const auto& frame : decoder->feedBytes(socket->readAll()))
                {
                    CommandMessage command;
                    if (frame.type != MsgType::Command || !TelemetryCodec::parseCommand(frame.payload, command)) continue;
                    ++skyCommands;
                    CommandAck ack;
                    ack.command_id = command.command_id;
                    ack.command_seq = command.command_seq;
                    ack.correlation_id = command.correlation_id;
                    ack.session_id = command.session_id;
                    if (command.command_id == CommandId::RequestStatus) ++statusCommands;
                    else if (command.command_id == CommandId::StartRecording) recording = true;
                    else if (command.command_id == CommandId::StopRecording) recording = false;
                    else ack.error_code = CommandErrorCode::UnknownCommand;
                    const quint64 timeUs = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000;
                    socket->write(encoder.encodeFrame(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(ack), frameSequence++, timeUs));
                }
            });
        }
    });
    GroundTelemetryService service;
    const bool connected = service.openTcp("127.0.0.1", sky.serverPort()) && waitUntil([&] { return !peer.isNull(); }, 3000);
    addCheck("shared_skycore_connection", connected, QStringLiteral("The existing telemetry service owns the only SkyCore connection."));
    if (!connected) return finish();
    QElapsedTimer duration;
    qint64 firstUpdateMs = -1, lastUpdateMs = -1, largestGapMs = 0;
    QObject::connect(&service, &GroundTelemetryService::basicTelemetryUpdated, &service, [&](const TelemetryBasic&) {
        const qint64 current = duration.elapsed();
        if (lastUpdateMs >= 0) largestGapMs = qMax(largestGapMs, current - lastUpdateMs);
        if (firstUpdateMs < 0) firstUpdateMs = current;
        lastUpdateMs = current;
        ++localUpdates;
    });
    QObject::connect(&service, &GroundTelemetryService::statusUpdated, &service, [&](const TelemetryStatus&) { ++localStatusUpdates; });
    QObject::connect(&service, &GroundTelemetryService::commandAckReceived, &service, [&](const CommandAck& ack) {
        if (ack.error_code == CommandErrorCode::Ok) ++commandAcks;
    });
    duration.start();
    {
        GatewayService gateway;
        gateway.attach(&service);
        QObject::connect(&gateway, &GatewayService::sessionSyncFinished, &gateway,
            [&](const QString&, bool passed, const QString& error) { sessionSynced = passed; sessionError = error; });
        QObject::connect(&gateway, &GatewayService::statusChanged, &gateway, [&](const QString& status) {
            if (status.contains(QStringLiteral("HTTP 503"))) ++offlineEvents;
            if (status.contains(QStringLiteral("Gateway 在线"))) ++onlineEvents;
        });
        GatewayConfig config;
        config.enabled = true;
        config.serverUrl = cloudUrl;
        config.token = token;
        config.deviceId = device;
        config.cacheDirectory = temporary.filePath("cache");
        config.allowRemoteCommands = true;
        config.allowedCommands = {static_cast<quint16>(CommandId::RequestStatus),
            static_cast<quint16>(CommandId::StartRecording), static_cast<quint16>(CommandId::StopRecording)};
        gateway.configure(config);
        gateway.enqueueSession(session);
        QTimer telemetry;
        telemetry.setInterval(1000);
        const auto sendTelemetry = [&] {
            if (!peer || peer->state() != QAbstractSocket::ConnectedState) return;
            const quint64 timeUs = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000;
            TelemetryBasic basic;
            basic.host_time_us = timeUs;
            basic.validity_flags = BasicHasTemperature | BasicHasPressure;
            basic.temperature_c = 25.0f + static_cast<float>(localUpdates % 10) * 0.1f;
            basic.pressure_hpa = 1013.25f;
            TelemetryStatus status;
            status.recording_state = recording ? 1 : 0;
            status.session_name = QStringLiteral("Software demonstration");
            status.telemetry_basic_rate_hz = status.status_rate_hz = 1.0f;
            status.rx_total_frames = static_cast<quint32>(localUpdates);
            peer->write(encoder.encodeFrame(MsgType::TelemetryBasic, TelemetryCodec::serializeBasicTelemetry(basic), frameSequence++, timeUs));
            peer->write(encoder.encodeFrame(MsgType::TelemetryStatus, TelemetryCodec::serializeTelemetryStatus(status), frameSequence++, timeUs));
        };
        QObject::connect(&telemetry, &QTimer::timeout, &telemetry, sendTelemetry);
        telemetry.start();
        sendTelemetry();
        waitUntil([&] { return duration.elapsed() >= 22000 || (!cancelPath.isEmpty() && QFile::exists(cancelPath)); }, 23000);
        cancelled = !cancelPath.isEmpty() && QFile::exists(cancelPath);
        telemetry.stop();
        elapsedMs = duration.elapsed();
    }
    service.close();
    if (peer) peer->abort();
    sky.close();
    addCheck("verification_completed", !cancelled, cancelled
        ? QStringLiteral("Demo shutdown requested; client stopped and cleaned its runtime state.")
        : QStringLiteral("The complete verification window finished."));
    addCheck("single_sky_socket", skyConnections == 1, QStringLiteral("Accepted %1 SkyCore TCP connection(s).").arg(skyConnections));
    addCheck("local_updates_continue", localUpdates >= 18 && localStatusUpdates >= 18 && lastUpdateMs - firstUpdateMs >= 18000 && largestGapMs <= 2500,
        QStringLiteral("Basic/status updates: %1/%2; largest local gap: %3 ms, including the cloud outage.").arg(localUpdates).arg(localStatusUpdates).arg(largestGapMs));
    addCheck("command_acknowledged", statusCommands > 0 && commandAcks > 0,
        QStringLiteral("Received %1 status command(s) and validated %2 ACK(s) on the shared SkyCore connection.").arg(statusCommands).arg(commandAcks));
    addCheck("session_synced", sessionSynced, sessionSynced ? QStringLiteral("The cloud confirmed all Session chunks and completion.") : sessionError);
    addCheck("offline_retry_recovery", offlineEvents > 0 && onlineEvents > 0,
        QStringLiteral("Observed %1 HTTP 503 event(s) and %2 confirmed upload(s).").arg(offlineEvents).arg(onlineEvents));
    return finish();
}
}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    if (app.arguments().value(1) == QLatin1String("--web-demo")) return webDemo();
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
