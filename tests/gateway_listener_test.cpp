#include "ground/devices/GroundTelemetryService.h"
#include "ground/gateway/GatewayService.h"
#include "TelemetryCodec.h"
#include "gateway_test_certificate.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <QWebSocket>

#include <iostream>
#include <optional>
#include <stdexcept>

using namespace VaporView;
using namespace VaporView::Ground::Gateway;

namespace
{
void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

template<class Predicate> bool waitUntil(Predicate predicate, int timeout = 6000)
{
    QElapsedTimer timer;
    timer.start();
    do
    {
        QCoreApplication::processEvents();
        if (predicate()) return true;
        QThread::msleep(2);
    } while (timer.elapsed() < timeout);
    return false;
}

struct Response
{
    int code;
    QJsonObject body;
    QNetworkReply::NetworkError error;
    QString errorString;
    QByteArray data;
    QByteArray contentType;
    QByteArray cacheControl;
};

QNetworkRequest request(const QUrl& base, const QString& path, const QByteArray& token,
                        const QSslConfiguration& tls)
{
    QNetworkRequest result(base.resolved(QUrl(path)));
    if (!token.isEmpty()) result.setRawHeader("Authorization", "Bearer " + token);
    result.setSslConfiguration(tls);
    result.setTransferTimeout(4000);
    return result;
}

Response send(QNetworkAccessManager& network, QNetworkRequest req,
              std::optional<QJsonObject> body = std::nullopt)
{
    QByteArray payload;
    if (body && !body->isEmpty())
    {
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        payload = QJsonDocument(*body).toJson(QJsonDocument::Compact);
    }
    auto *reply = body ? network.post(req, payload) : network.get(req);
    require(waitUntil([reply] { return reply->isFinished(); }), "HTTPS reply arrived");
    const auto data = reply->readAll();
    Response response{reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
                      QJsonDocument::fromJson(data).object(), reply->error(), reply->errorString(),
                      data, reply->rawHeader("Content-Type"), reply->rawHeader("Cache-Control")};
    reply->deleteLater();
    return response;
}

QJsonObject command(const QString& id, CommandId type, qint64 seconds = 45)
{
    return {{"id", id}, {"commandId", static_cast<int>(type)},
            {"expiresAt", QDateTime::currentDateTimeUtc().addSecs(seconds).toString(Qt::ISODateWithMs)}};
}
}  // namespace

int main(int argc, char **argv)
try
{
    QCoreApplication app(argc, argv);
    const auto openssl = Test::gatewayOpenSslExecutable();
    if (openssl.isEmpty())
    {
        std::cout << "SKIP: openssl CLI is needed for an ephemeral TLS certificate\n";
        return 77;
    }
    QTemporaryDir tlsFiles;
    require(tlsFiles.isValid(), "temporary TLS certificate directory");
    GatewayConfig config;
    config.deviceId = QStringLiteral("simulated-ground");
    config.listener.enabled = true;
    config.listener.port = 0;
    config.listener.certificateFile = tlsFiles.filePath(QStringLiteral("certificate.pem"));
    config.listener.privateKeyFile = tlsFiles.filePath(QStringLiteral("key.pem"));
    config.listener.token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    config.listener.allowCommands = true;
    config.listener.allowedCommands.insert(static_cast<quint16>(CommandId::StartRecording));
    config.listener.allowedCommands.insert(static_cast<quint16>(CommandId::StopRecording));

    const auto certificateError = Test::generateGatewayTestCertificate(openssl,
        tlsFiles.filePath(QStringLiteral("openssl.cnf")), config.listener.certificateFile,
        config.listener.privateKeyFile);
    if (!certificateError.isEmpty()) std::cerr << certificateError.toStdString() << '\n';
    require(certificateError.isEmpty(), "OpenSSL generated the test key and certificate");
    const auto chain = QSslCertificate::fromPath(config.listener.certificateFile);
    require(chain.size() == 1, "test certificate is readable");
    auto tls = QSslConfiguration::defaultConfiguration();
    tls.addCaCertificates(chain);

    GroundTelemetryService telemetry;
    GatewayService gateway;
    gateway.attach(&telemetry);
    gateway.configure(config);
    require(waitUntil([&] { return gateway.listenerPort() > 0; }), "listener starts with outbound disabled");
    const QUrl base(QStringLiteral("https://127.0.0.1:%1").arg(gateway.listenerPort()));
    const QByteArray token = config.listener.token.toLatin1();
    QNetworkAccessManager network;
    network.setProxy(QNetworkProxy::NoProxy);
    const auto https = [&](const QString& path, std::optional<QJsonObject> body = std::nullopt) {
        return send(network, request(base, path, token, tls), body);
    };
    const auto waitForCommandStatus = [&](const QString& id, const QString& status) {
        QElapsedTimer timeout;
        timeout.start();
        do
        {
            if (https("/api/v1/commands/" + id).body.value("status") == status) return true;
            QThread::msleep(100);
        } while (timeout.elapsed() < 5000);
        return false;
    };
    QNetworkAccessManager untrustedNetwork;
    untrustedNetwork.setProxy(QNetworkProxy::NoProxy);
    const auto untrusted = send(untrustedNetwork,
        request(base, "/api/v1/status", token, QSslConfiguration::defaultConfiguration()));
    require(untrusted.error == QNetworkReply::SslHandshakeFailedError, "TLS peer verification rejects untrusted certificate");
    const auto unauthenticated = send(network, request(base, "/api/v1/status", {}, tls));
    if (unauthenticated.code != 401)
        std::cerr << "Expected HTTP 401, got " << unauthenticated.code << " ("
                  << unauthenticated.errorString.toStdString() << ")\n";
    require(unauthenticated.code == 401, "status rejects missing bearer token");
    auto response = https("/api/v1/status");
    require(response.code == 200 && response.body.value("linkConnected") == QJsonValue(false),
        "authorized status before SkyCore connects");
    require(https("/api/v1/measurements/latest").body.value("measurement").isNull(),
        "latest measurement is null before data");

    QUrl streamUrl(base);
    streamUrl.setScheme("wss");
    QWebSocket unauthorized;
    unauthorized.setProxy(QNetworkProxy::NoProxy);
    unauthorized.setSslConfiguration(tls);
    bool refused = false;
    QObject::connect(&unauthorized, &QWebSocket::errorOccurred, &app, [&] { refused = true; });
    unauthorized.open(request(streamUrl, "/api/v1/stream", {}, tls));
    require(waitUntil([&] { return refused; }), "WSS rejects unauthorized upgrade");

    QWebSocket stream;
    stream.setProxy(QNetworkProxy::NoProxy);
    stream.setSslConfiguration(tls);
    QList<QJsonObject> events;
    QObject::connect(&stream, &QWebSocket::textMessageReceived, &app, [&](const QString& message) {
        events.append(QJsonDocument::fromJson(message.toUtf8()).object());
    });
    stream.open(request(streamUrl, "/api/v1/stream", token, tls));
    require(waitUntil([&] { return !events.isEmpty(); }) && events.first().value("snapshot").toBool(),
        "WSS accepts verified connection and sends initial snapshot");


    // Exercise the browser's HTTP bootstrap and WSS upgrade over verified TLS.
    const auto page = send(network, request(base, "/", {}, tls));
    require(page.code == 200 && page.contentType.startsWith("text/html") && page.cacheControl == "no-store" &&
        page.data.contains("/app.js") && page.data.contains("/style.css"), "HTTPS returns the browser page");
    const auto script = send(network, request(base, "/app.js", {}, tls));
    require(script.code == 200 && script.contentType.startsWith("text/javascript") &&
        script.cacheControl == "no-store" && script.data.contains("/api/v1/stream-ticket"),
        "HTTPS returns the embedded JavaScript");
    const auto stylesheet = send(network, request(base, "/style.css", {}, tls));
    require(stylesheet.code == 200 && stylesheet.contentType.startsWith("text/css") &&
        stylesheet.cacheControl == "no-store" && !stylesheet.data.isEmpty(), "HTTPS returns the embedded CSS");

    const auto ticketPath = QStringLiteral("/api/v1/stream-ticket");
    require(send(network, request(base, ticketPath, {}, tls), QJsonObject{}).code == 401,
        "ticket issuance rejects a missing bearer token");
    const auto issued = https(ticketPath, QJsonObject{});
    const auto ticket = issued.body.value("ticket").toString();
    require(issued.code == 200 && issued.cacheControl == "no-store" && !ticket.isEmpty(),
        "ticket issuance accepts a bearer token and prevents caching");
    require(send(network, request(base, "/api/v1/commands", ticket.toLatin1(), tls),
        command("ticket-only", CommandId::RequestStatus)).code == 401, "stream ticket cannot authorize a command");

    const auto ticketStreamPath = QStringLiteral("/api/v1/stream?ticket=") + ticket;
    const auto verifyTicket = [&](const QString& origin, bool expectSnapshot, const char *failure) {
        QWebSocket browser(origin);
        browser.setProxy(QNetworkProxy::NoProxy);
        browser.setSslConfiguration(tls);
        bool rejected = false;
        bool snapshot = false;
        QObject::connect(&browser, &QWebSocket::errorOccurred, &app, [&] { rejected = true; });
        QObject::connect(&browser, &QWebSocket::textMessageReceived, &app, [&](const QString& message) {
            snapshot = QJsonDocument::fromJson(message.toUtf8()).object().value("snapshot").toBool();
        });
        browser.open(request(streamUrl, ticketStreamPath, {}, tls));
        require(waitUntil([&] { return snapshot || rejected; }) &&
            (expectSnapshot ? snapshot : rejected), failure);
        if (expectSnapshot)
        {
            browser.close();
            require(waitUntil([&] { return browser.state() == QAbstractSocket::UnconnectedState; }),
                "browser subscription closes cleanly");
        }
    };
    verifyTicket({}, false, "ticket upgrade rejects a missing origin");
    QUrl foreignOrigin(base);
    foreignOrigin.setHost(QStringLiteral("localhost"));
    verifyTicket(foreignOrigin.toString(), false, "ticket upgrade rejects a different host in the origin");
    foreignOrigin = base;
    foreignOrigin.setPort(base.port() == 443 ? 444 : 443);
    verifyTicket(foreignOrigin.toString(), false, "ticket upgrade rejects a different port in the origin");
    verifyTicket(base.toString(), true, "same-origin ticket upgrade receives the initial snapshot");
    verifyTicket(base.toString(), false, "a consumed ticket rejects further subscriptions");

    QTcpServer sky;
    require(sky.listen(QHostAddress::LocalHost), "simulated SkyCore listens");
    require(telemetry.openTcpAsync(QStringLiteral("127.0.0.1"), sky.serverPort()), "ground starts the telemetry link");
    require(waitUntil([&] { return telemetry.isOpen() && sky.hasPendingConnections(); }), "telemetry is connected");
    auto *aircraft = sky.nextPendingConnection();
    TelemetryCodec codec;
    TelemetryStatus status;
    status.recording_state = 1;
    status.session_name = QStringLiteral("test-session");
    aircraft->write(codec.encodeFrame(MsgType::TelemetryStatus, TelemetryCodec::serializeTelemetryStatus(status), 4, 1000));
    WaveformFeature wave;
    wave.channel_id = 2;
    wave.peak = 14.5f;
    wave.quality_flags = 1;
    aircraft->write(codec.encodeFrame(MsgType::WaveformFeature, TelemetryCodec::serializeWaveformFeature(wave), 5, 2000));
    require(waitUntil([&] {
        return !events.isEmpty() && events.last().value("source") == QLatin1String("waveform-feature");
    }), "legacy waveform feature reaches WSS");
    require(events.at(events.size() - 2).value("recordingState").toInt(-1) == 1,
        "SkyCore recording state reaches WSS");

    FpgaWave::CompletedStream cycle;
    cycle.source = 0x30;
    cycle.cycleId = 7;
    cycle.totalPoints = 32;
    cycle.normalizedFeature.emplace();
    cycle.normalizedFeature->peak = 21.25;
    cycle.normalizedFeature->peakIndex = 4;
    cycle.normalizedFeature->validPoints = 32;
    aircraft->write(codec.encodeFrame(MsgType::FpgaCycleFeature, FpgaRemote::encodeCycleFeature(3000, cycle), 6, 3000));
    require(waitUntil([&] { return events.last().value("source") == QLatin1String("fpga-cycle-feature"); }),
        "full FPGA cycle feature reaches WSS");
    const auto measurement = https("/api/v1/measurements/latest").body.value("measurement").toObject();
    require(measurement.value("value").toDouble() == 21.25 && measurement.value("channel").toInt() == 0x30 &&
        measurement.value("unit") == QLatin1String("native") && measurement.value("frameSequence").toInt() == 6,
        "HTTPS returns the same native-peak measurement");

    QList<CommandMessage> forwarded;
    TelemetryCodec uplink;
    QObject::connect(aircraft, &QTcpSocket::readyRead, &app, [&] {
        for (const auto& frame : uplink.feedBytes(aircraft->readAll()))
        {
            CommandMessage message;
            if (frame.type != MsgType::Command || !TelemetryCodec::parseCommand(frame.payload, message)) continue;
            forwarded.append(message);
            if (message.command_id != CommandId::RequestStatus) continue;
            CommandAck ack;
            ack.command_id = message.command_id;
            ack.command_seq = message.command_seq;
            ack.error_code = CommandErrorCode::Ok;
            aircraft->write(codec.encodeFrame(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(ack), 7, 4000));
        }
    });
    require(https("/api/v1/commands", command("forbidden", CommandId::PauseRecording)).code == 403,
        "command outside the allowed list is forbidden");
    require(https("/api/v1/commands", command("old", CommandId::RequestStatus, -1)).code == 400,
        "expired command is rejected");
    const auto statusCommand = command("query", CommandId::RequestStatus);
    require(https("/api/v1/commands", statusCommand).code == 202, "command is accepted for delivery");
    require(waitUntil([&] { return forwarded.size() == 1; }), "command reaches SkyCore link");
    require(waitForCommandStatus("query", "acknowledged"),
        "result reports the SkyCore acknowledgment");
    require(https("/api/v1/commands", statusCommand).code == 200 && forwarded.size() == 1,
        "duplicate request returns original result without resending");
    auto conflict = statusCommand;
    conflict.insert("commandId", static_cast<int>(CommandId::StartRecording));
    require(https("/api/v1/commands", conflict).code == 409, "different command with the same ID conflicts");

    require(https("/api/v1/commands", command("no-ack", CommandId::StopRecording, 1)).code == 202,
        "timeout case is accepted for dispatch");
    require(waitForCommandStatus("no-ack", "timeout"),
        "missing acknowledgment results in timeout");

    // Complete an ordinary WSS subscription, then pause reading at the TLS transport.
    QSslSocket slow;
    slow.setProxy(QNetworkProxy::NoProxy);
    slow.setSslConfiguration(tls);
    slow.setReadBufferSize(4096);
    slow.connectToHostEncrypted(base.host(), gateway.listenerPort());
    require(waitUntil([&] { return slow.isEncrypted(); }), "slow subscriber verifies TLS");
    slow.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 4096);
    const auto websocketKey = QUuid::createUuid().toRfc4122().toBase64();
    const auto upgrade = QByteArray("GET /api/v1/stream HTTP/1.1\r\nHost: 127.0.0.1:") +
        QByteArray::number(gateway.listenerPort()) + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n" +
        "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: " + websocketKey + "\r\nAuthorization: Bearer " + token + "\r\n\r\n";
    require(slow.write(upgrade) == upgrade.size(), "slow subscriber sends WSS upgrade");
    require(waitUntil([&] { return slow.peek(4096).contains("\r\n\r\n"); }) &&
        slow.readLine().startsWith("HTTP/1.1 101"), "slow subscriber completes WSS upgrade");

    for (int id = int(SkyDeviceId::Epsilon); id <= int(SkyDeviceId::Sht45); ++id)
    {
        DeviceStatusItem device;
        device.device_id = SkyDeviceId(id);
        device.state = DeviceState::Connected;
        status.devices.append(device);
    }
    const auto statusPayload = TelemetryCodec::serializeTelemetryStatus(status);
    TelemetryStatus decodedStatus;
    require(TelemetryCodec::parseTelemetryStatus(statusPayload, decodedStatus), "valid simulated device status frame");
    quint32 sequence = 100;
    bool slowDisconnected = false;
    for (int batch = 0; batch < 128; ++batch)
    {
        QByteArray burst;
        for (int frame = 0; frame < 32; ++frame, ++sequence)
        {
            burst += codec.encodeFrame(MsgType::TelemetryStatus, statusPayload, sequence, 5000 + sequence);
        }
        require(aircraft->write(burst) == burst.size(), "simulated SkyCore sends status updates");
        require(waitUntil([&] { return events.last().value("frameSequence").toInteger() == sequence - 1; }),
            "healthy subscriber keeps receiving while the slow subscriber pauses");
        if ((batch + 1) % 16 == 0)
        {
            slowDisconnected = https("/api/v1/status").body.value("slowSubscribersDisconnected") == QLatin1String("1");
            if (slowDisconnected) break;
        }
    }
    require(slowDisconnected, "server disconnects the subscriber that fills its write budget");
    require(stream.state() == QAbstractSocket::ConnectedState && https("/api/v1/status").code == 200,
        "healthy WSS and HTTPS remain available after the slow subscriber disconnects");

    const auto countBeforeClose = events.size();
    aircraft->disconnectFromHost();
    require(waitUntil([&] {
        return events.size() > countBeforeClose && events.last().value("linkConnected") == QJsonValue(false);
    }), "WSS reports the loss of the SkyCore link");
    require(https("/api/v1/measurements/latest").body.value("measurement").isNull(),
        "latest measurement clears on link disconnect");
    stream.close();
    require(waitUntil([&] { return stream.state() == QAbstractSocket::UnconnectedState; }), "WSS disconnects");
    const auto beforeReconnect = events.size();
    stream.open(request(streamUrl, "/api/v1/stream", token, tls));
    require(waitUntil([&] { return events.size() > beforeReconnect; }) && events.last().value("snapshot").toBool(),
        "stream can reconnect and receives a snapshot");
    config.listener.enabled = false;
    gateway.configure(config);
    require(waitUntil([&] { return stream.state() == QAbstractSocket::UnconnectedState; }),
        "disabling the listener disconnects active clients");
    std::cout << "Gateway HTTPS and WSS integration passed on loopback and simulated telemetry\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
