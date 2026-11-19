#include "ground/devices/GroundTelemetryService.h"
#include "ground/gateway/GatewayService.h"
#include "TelemetryCodec.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QTcpServer>
#include <QTcpSocket>
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
    QByteArray data;
    QByteArray contentType;
    QByteArray cacheControl;
};

QNetworkRequest request(const QUrl& base, const QString& path)
{
    QNetworkRequest result(base.resolved(QUrl(path)));
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
    require(waitUntil([reply] { return reply->isFinished(); }), "HTTP reply arrived");
    const auto data = reply->readAll();
    Response response{reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
                      QJsonDocument::fromJson(data).object(),
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
    GatewayConfig config;
    config.deviceId = QStringLiteral("simulated-ground");
    config.listener.enabled = true;
    config.listener.port = 0;

    GroundTelemetryService telemetry;
    GatewayService gateway;
    gateway.attach(&telemetry);
    gateway.configure(config);
    require(waitUntil([&] { return gateway.listenerPort() > 0; }), "listener starts with outbound disabled");
    const QUrl base(QStringLiteral("http://127.0.0.1:%1").arg(gateway.listenerPort()));
    QNetworkAccessManager network;
    network.setProxy(QNetworkProxy::NoProxy);
    const auto http = [&](const QString& path, std::optional<QJsonObject> body = std::nullopt) {
        return send(network, request(base, path), body);
    };
    const auto waitForCommandStatus = [&](const QString& id, const QString& status) {
        QElapsedTimer timeout;
        timeout.start();
        do
        {
            if (http("/api/v1/commands/" + id).body.value("status") == status) return true;
            QThread::msleep(100);
        } while (timeout.elapsed() < 5000);
        return false;
    };
    auto response = http("/api/v1/status");
    require(response.code == 200 && response.body.value("linkConnected") == QJsonValue(false),
        "status is available before SkyCore connects");
    require(http("/api/v1/measurements/latest").body.value("measurement").isNull(),
        "latest measurement is null before data");

    QUrl streamUrl(base);
    streamUrl.setScheme("ws");
    QWebSocket stream(base.toString());
    stream.setProxy(QNetworkProxy::NoProxy);
    QList<QJsonObject> events;
    QObject::connect(&stream, &QWebSocket::textMessageReceived, &app, [&](const QString& message) {
        events.append(QJsonDocument::fromJson(message.toUtf8()).object());
    });
    stream.open(request(streamUrl, "/api/v1/stream"));
    require(waitUntil([&] { return !events.isEmpty(); }) && events.first().value("snapshot").toBool(),
        "WS accepts same-origin connection and sends initial snapshot");

    // Exercise the browser bootstrap.
    const auto page = send(network, request(base, "/"));
    require(page.code == 200 && page.contentType.startsWith("text/html") && page.cacheControl == "no-store" &&
        page.data.contains("/app.js") && page.data.contains("/style.css"), "HTTP returns the browser page");
    const auto script = send(network, request(base, "/app.js"));
    require(script.code == 200 && script.contentType.startsWith("text/javascript") &&
        script.cacheControl == "no-store" && script.data.contains("/api/v1/stream"),
        "HTTP returns the embedded JavaScript");
    const auto stylesheet = send(network, request(base, "/style.css"));
    require(stylesheet.code == 200 && stylesheet.contentType.startsWith("text/css") &&
        stylesheet.cacheControl == "no-store" && !stylesheet.data.isEmpty(), "HTTP returns the embedded CSS");

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
    }), "legacy waveform feature reaches WS");
    require(events.at(events.size() - 2).value("recordingState").toInt(-1) == 1,
        "SkyCore recording state reaches WS");

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
        "full FPGA cycle feature reaches WS");
    const auto measurement = http("/api/v1/measurements/latest").body.value("measurement").toObject();
    require(measurement.value("value").toDouble() == 21.25 && measurement.value("channel").toInt() == 0x30 &&
        measurement.value("unit") == QLatin1String("native") && measurement.value("frameSequence").toInt() == 6,
        "HTTP returns the same native-peak measurement");

    QList<CommandMessage> forwarded;
    TelemetryCodec uplink;
    QObject::connect(aircraft, &QTcpSocket::readyRead, &app, [&] {
        for (const auto& frame : uplink.feedBytes(aircraft->readAll()))
        {
            CommandMessage message;
            if (frame.type != MsgType::Command || !TelemetryCodec::parseCommand(frame.payload, message)) continue;
            forwarded.append(message);
            if (message.command_id != CommandId::RequestStatus &&
                message.command_id != CommandId::ConnectDevice && message.command_id != CommandId::DisconnectDevice) continue;
            CommandAck ack;
            ack.command_id = message.command_id;
            ack.command_seq = message.command_seq;
            ack.error_code = message.command_id == CommandId::ConnectDevice
                ? CommandErrorCode::ConfigInvalid : CommandErrorCode::Ok;
            aircraft->write(codec.encodeFrame(MsgType::CommandAck, TelemetryCodec::serializeCommandAck(ack), 7, 4000));
        }
    });
    require(http("/api/v1/commands", command("forbidden", CommandId::PauseRecording)).code == 403,
        "command outside the allowed list is forbidden");
    require(http("/api/v1/commands", command("old", CommandId::RequestStatus, -1)).code == 400,
        "expired command is rejected");
    const auto statusCommand = command("query", CommandId::RequestStatus);
    auto browserCommand = request(base, "/api/v1/commands");
    QUrl otherOrigin(base);
    otherOrigin.setPort(base.port() == 80 ? 81 : 80);
    browserCommand.setRawHeader("Origin", otherOrigin.toEncoded());
    require(send(network, browserCommand, statusCommand).code == 403 && forwarded.isEmpty(),
        "command requests with a different browser origin are rejected");
    browserCommand.setRawHeader("Origin", base.toEncoded());
    require(send(network, browserCommand, statusCommand).code == 202,
        "same-origin browser command is accepted without credentials");
    require(waitUntil([&] { return forwarded.size() == 1; }), "command reaches SkyCore link");
    require(waitForCommandStatus("query", "acknowledged"),
        "result reports the SkyCore acknowledgment");
    require(http("/api/v1/commands", statusCommand).code == 200 && forwarded.size() == 1,
        "duplicate request returns original result without resending");
    auto conflict = statusCommand;
    conflict.insert("commandId", static_cast<int>(CommandId::StartRecording));
    require(http("/api/v1/commands", conflict).code == 409, "different command with the same ID conflicts");

    const auto calibrationError = QStringLiteral("缺少原 Session 的定标锁，请先处理异常会话");
    gateway.setListenerCommandPreparation([&](CommandId type, QByteArray&, QString& detail) -> QString {
        if (type != CommandId::StartRecording) return {};
        detail = calibrationError;
        return QStringLiteral("calibration-required");
    });
    const auto start = command("calibration-error", CommandId::StartRecording);
    require(http("/api/v1/commands", start).code == 202, "start request is accepted for preparation");
    require(waitForCommandStatus("calibration-error", "calibration-required"),
        "calibration rejection keeps the existing status");
    const auto rejected = http("/api/v1/commands/calibration-error");
    require(rejected.code == 200 && rejected.body.value("detail") == calibrationError && forwarded.size() == 1,
        "recording rejection carries the original detail without forwarding to SkyCore");
    const auto duplicate = http("/api/v1/commands", start);
    require(duplicate.code == 200 && duplicate.body == rejected.body,
        "duplicate start request returns the original rejection and detail");

    require(http("/api/v1/commands", command("no-ack", CommandId::StopRecording, 1)).code == 202,
        "timeout case is accepted for dispatch");
    require(waitForCommandStatus("no-ack", "timeout"),
        "missing acknowledgment results in timeout");

    // Complete an ordinary WS subscription, then pause reading at the TCP transport.
    QTcpSocket slow;
    slow.setProxy(QNetworkProxy::NoProxy);
    slow.setReadBufferSize(4096);
    slow.connectToHost(base.host(), gateway.listenerPort());
    require(waitUntil([&] { return slow.state() == QAbstractSocket::ConnectedState; }), "slow subscriber connects over TCP");
    slow.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 4096);
    const auto websocketKey = QUuid::createUuid().toRfc4122().toBase64();
    const auto upgrade = QByteArray("GET /api/v1/stream HTTP/1.1\r\nHost: 127.0.0.1:") +
        QByteArray::number(gateway.listenerPort()) + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n" +
        "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: " + websocketKey + "\r\n\r\n";
    require(slow.write(upgrade) == upgrade.size(), "slow subscriber sends WS upgrade");
    require(waitUntil([&] { return slow.peek(4096).contains("\r\n\r\n"); }) &&
        slow.readLine().startsWith("HTTP/1.1 101"), "slow subscriber completes WS upgrade");

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
            slowDisconnected = http("/api/v1/status").body.value("slowSubscribersDisconnected") == QLatin1String("1");
            if (slowDisconnected) break;
        }
    }
    require(slowDisconnected, "server disconnects the subscriber that fills its write budget");
    require(stream.state() == QAbstractSocket::ConnectedState && http("/api/v1/status").code == 200,
        "healthy WS and HTTP remain available after the slow subscriber disconnects");

    // Start a fresh HTTP rate-limit window after polling the slow subscriber.
    QThread::msleep(1100);
    const auto fpgaState = [](const QJsonObject& snapshot) {
        for (const auto& value : snapshot.value("devices").toArray())
        {
            const auto device = value.toObject();
            if (device.value("device").toInt() == int(SkyDeviceId::Fpga))
                return device.value("state").toInt(-1);
        }
        return -1;
    };
    require(fpgaState(events.last()) == int(DeviceState::Connected), "FPGA connection state is available to subscribers");
    const auto beforeFpga = forwarded.size();
    require(http("/api/v1/commands", command("fpga-busy", CommandId::DisconnectDevice)).code == 202,
        "FPGA disconnect request is accepted for validation");
    require(waitForCommandStatus("fpga-busy", "device-busy") && forwarded.size() == beforeFpga,
        "FPGA disconnect is blocked while recording");

    status.recording_state = 0;
    status.devices.clear();
    const auto publishStatus = [&] {
        const auto frameSequence = sequence++;
        aircraft->write(codec.encodeFrame(MsgType::TelemetryStatus, TelemetryCodec::serializeTelemetryStatus(status),
                                         frameSequence, 5000 + frameSequence));
        require(waitUntil([&] { return events.last().value("frameSequence").toInteger() == frameSequence; }),
            "simulated FPGA state reaches the gateway");
    };
    publishStatus();
    require(http("/api/v1/commands", command("fpga-unavailable", CommandId::ConnectDevice)).code == 202,
        "FPGA connect request is accepted for validation");
    require(waitForCommandStatus("fpga-unavailable", "fpga-unavailable") && forwarded.size() == beforeFpga,
        "FPGA control requires an advertised FPGA");
    DeviceStatusItem fpga;
    fpga.device_id = SkyDeviceId::Fpga;
    fpga.state = DeviceState::Connected;
    status.devices.append(fpga);
    publishStatus();

    const auto disconnect = command("fpga-disconnect", CommandId::DisconnectDevice);
    auto withPayload = disconnect;
    withPayload.insert("payloadBase64", "AQ==");
    require(http("/api/v1/commands", withPayload).code == 400 && forwarded.size() == beforeFpga,
        "caller cannot supply a device payload");
    require(http("/api/v1/commands", disconnect).code == 202 &&
        waitForCommandStatus("fpga-disconnect", "acknowledged"), "FPGA disconnect reports SkyCore acknowledgment");
    require(forwarded.size() == beforeFpga + 1 && forwarded.last().command_id == CommandId::DisconnectDevice &&
        forwarded.last().payload == TelemetryCodec::serializeDeviceCommand(SkyDeviceId::Fpga),
        "FPGA disconnect forwards the fixed FPGA selector");
    status.devices.front().state = DeviceState::Disconnected;
    publishStatus();
    require(fpgaState(http("/api/v1/status").body) == int(DeviceState::Disconnected),
        "the reported device state is visible in HTTP");

    require(http("/api/v1/commands", command("fpga-connect", CommandId::ConnectDevice)).code == 202 &&
        waitForCommandStatus("fpga-connect", "rejected"), "FPGA connect reports SkyCore failure");
    require(forwarded.size() == beforeFpga + 2 && forwarded.last().command_id == CommandId::ConnectDevice &&
        forwarded.last().payload == TelemetryCodec::serializeDeviceCommand(SkyDeviceId::Fpga) &&
        http("/api/v1/commands/fpga-connect").body.value("errorCode").toInt() == int(CommandErrorCode::ConfigInvalid),
        "FPGA connect forwards the fixed selector and retains the backend error code");

    const auto countBeforeClose = events.size();
    aircraft->disconnectFromHost();
    require(waitUntil([&] {
        return events.size() > countBeforeClose && events.last().value("linkConnected") == QJsonValue(false);
    }), "WS reports the loss of the SkyCore link");
    require(http("/api/v1/measurements/latest").body.value("measurement").isNull(),
        "latest measurement clears on link disconnect");
    stream.close();
    require(waitUntil([&] { return stream.state() == QAbstractSocket::UnconnectedState; }), "WS disconnects");
    const auto beforeReconnect = events.size();
    stream.open(request(streamUrl, "/api/v1/stream"));
    require(waitUntil([&] { return events.size() > beforeReconnect; }) && events.last().value("snapshot").toBool(),
        "stream can reconnect and receives a snapshot");
    config.listener.enabled = false;
    gateway.configure(config);
    require(waitUntil([&] { return stream.state() == QAbstractSocket::UnconnectedState; }),
        "disabling the listener disconnects active clients");
    std::cout << "Gateway HTTP and WS integration passed on loopback and simulated telemetry\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
