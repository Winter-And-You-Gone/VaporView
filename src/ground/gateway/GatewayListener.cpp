#include "ground/gateway/GatewayListener.h"
#include "FpgaTelemetry.h"
#include "TelemetryCodec.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QHttpServer>
#include <QHttpServerConfiguration>
#include <QHttpServerWebSocketUpgradeResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QRegularExpression>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
#include <QUuid>
#include <QWebSocket>

#include <algorithm>
#include <cmath>
#include <utility>

namespace VaporView::Ground::Gateway
{
namespace
{
using HttpStatus = QHttpServerResponse::StatusCode;
using Method = QHttpServerRequest::Method;
constexpr auto kStreamPath = "/api/v1/stream";
constexpr qsizetype kQueuedBytes = 256 * 1024;
constexpr int kMaxSubscribers = 16;
constexpr int kMaxCommands = 4096;
constexpr qint64 kRetentionMs = 24 * 60 * 60 * 1000;
constexpr qint64 kMaxCommandLifetimeMs = 5 * 60 * 1000;

QHttpServerResponse jsonResponse(const QJsonObject& body, HttpStatus status = HttpStatus::Ok)
{
    QHttpServerResponse response(body, status);
    auto headers = response.headers();
    headers.append(QHttpHeaders::WellKnownHeader::CacheControl, "no-store");
    response.setHeaders(std::move(headers));
    return response;
}

QHttpServerResponse errorResponse(const QString& error, HttpStatus status)
{
    return jsonResponse({{"error", error}}, status);
}

QJsonObject frameFields(const TelemetryFrame& frame)
{
    return {{"frameSequence", frame.seq}, {"timeUs", QString::number(frame.time_us)}};
}

QJsonObject statusFields(const TelemetryStatus& status)
{
    QJsonArray devices;
    for (const auto& device : status.devices)
    {
        devices.append(QJsonObject{
            {"device", static_cast<int>(device.device_id)}, {"state", static_cast<int>(device.state)},
            {"errorCode", device.error_code}, {"receivedCount", qint64(device.rx_count)},
            {"errorCount", qint64(device.error_count)}, {"lastDataTimeUs", QString::number(device.last_data_time_us)}});
    }
    return {{"recordingState", status.recording_state}, {"sessionName", status.session_name}, {"devices", devices}};
}

QJsonValue finiteValue(double value)
{
    return std::isfinite(value) ? QJsonValue(value) : QJsonValue(QJsonValue::Null);
}

QByteArray readPem(const QString& path)
{
    constexpr auto maxPemBytes = 256 * 1024;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maxPemBytes) return {};
    return file.read(maxPemBytes);
}
}  // namespace

bool GatewayListenerConfig::operator==(const GatewayListenerConfig& other) const
{
    return enabled == other.enabled && address == other.address && port == other.port &&
        certificateFile == other.certificateFile && privateKeyFile == other.privateKeyFile &&
        token == other.token && allowCommands == other.allowCommands && allowedCommands == other.allowedCommands;
}

QString GatewayListenerConfig::validationError() const
{
    if (QHostAddress(address).isNull()) return QStringLiteral("请输入有效的监听 IP 地址");
    if (certificateFile.isEmpty() || privateKeyFile.isEmpty())
        return QStringLiteral("请选择 TLS 证书链和私钥 PEM 文件");
    if (token.size() < 32 || token.size() > 256 ||
        std::any_of(token.cbegin(), token.cend(), [](QChar ch) {
            return ch.unicode() < 33 || ch.unicode() > 126;
        }))
        return QStringLiteral("入站令牌须为 32 至 256 个可见 ASCII 字符");
    return {};
}

void GatewayListenerIngress::enqueue(const TelemetryFrame& frame)
{
    if (frame.type != MsgType::TelemetryStatus && frame.type != MsgType::WaveformFeature &&
        frame.type != MsgType::FpgaCycleFeature) return;
    const auto size = frame.payload.size() + 64;
    QMutexLocker lock(&mutex);
    if (frames.size() >= 256 || size + bytes > kQueuedBytes)
    {
        ++dropped;
        return;
    }
    frames.append(frame);
    bytes += size;
}

void GatewayListenerIngress::setConnected(bool open)
{
    QMutexLocker lock(&mutex);
    connected = open;
    ++linkRevision;
    frames.clear();
    bytes = 0;
}

GatewayListener::GatewayListener(const GatewayListenerConfig& config, QString deviceId,
                                 std::shared_ptr<GatewayListenerIngress> ingress, Dispatch dispatch)
    : config_(config), deviceId_(std::move(deviceId)),
      authorizationHash_(QCryptographicHash::hash("Bearer " + config.token.toLatin1(), QCryptographicHash::Sha256)),
      ingress_(std::move(ingress)), dispatch_(std::move(dispatch)),
      streamId_(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    timer_.setInterval(40);
    connect(&timer_, &QTimer::timeout, this, &GatewayListener::drain);
}

GatewayListener::~GatewayListener()
{
    for (auto *socket : subscribers_.keys())
    {
        disconnect(socket, nullptr, this, nullptr);
        socket->abort();
    }
}

QString GatewayListener::start()
{
    const auto invalid = config_.validationError();
    if (!invalid.isEmpty()) return invalid;
    if (deviceId_.isEmpty() || deviceId_.size() > 128)
        return QStringLiteral("请输入 1 至 128 个字符的设备标识");
    if (!QSslSocket::supportsSsl()) return QStringLiteral("Qt TLS 后端不可用");
    const auto certificates = QSslCertificate::fromData(readPem(config_.certificateFile), QSsl::Pem);
    if (certificates.isEmpty()) return QStringLiteral("无法读取 TLS 证书链");
    const auto now = QDateTime::currentDateTimeUtc();
    if (certificates.first().effectiveDate() > now || certificates.first().expiryDate() <= now)
        return QStringLiteral("TLS 证书不在有效期内");
    const auto keyData = readPem(config_.privateKeyFile);
    auto key = QSslKey(keyData, QSsl::Rsa);
    if (key.isNull()) key = QSslKey(keyData, QSsl::Ec);
    if (key.isNull()) return QStringLiteral("无法读取 TLS 私钥，须使用未加密 RSA 或 EC PEM");

    auto tls = QSslConfiguration::defaultConfiguration();
    tls.setLocalCertificateChain(certificates);
    tls.setPrivateKey(key);
    tls.setProtocol(QSsl::TlsV1_2OrLater);
    tls.setPeerVerifyMode(QSslSocket::VerifyNone);
    tls.setAllowedNextProtocols({});
    auto listener = std::make_unique<QSslServer>();
    listener->setSslConfiguration(tls);
    listener->setHandshakeTimeout(5000);
    listener->setProxy(QNetworkProxy::NoProxy);
    connect(listener.get(), &QSslServer::startedEncryptionHandshake, this, [this](QSslSocket *socket) {
        // The HTTP-to-WebSocket handoff can disconnect signals on the socket.
        tlsConnections_.removeIf([](const auto& connection) { return connection.isNull(); });
        tlsConnections_.append(socket);
    });

    server_ = std::make_unique<QHttpServer>();
    QHttpServerConfiguration limits;
    limits.setKeepAliveTimeout(std::chrono::seconds(15));
    limits.setRateLimitPerSecond(20);
    server_->setConfiguration(limits);
    server_->route("/api/v1/status", Method::Get, [this](const QHttpServerRequest& request) {
        if (!authorized(request)) return errorResponse("unauthorized", HttpStatus::Unauthorized);
        auto status = statusMessage();
        status.insert("streamId", streamId_);
        status.insert("ingressDropped", QString::number(dropped_));
        status.insert("slowSubscribersDisconnected", QString::number(slowSubscribersDisconnected_));
        return jsonResponse(status);
    });
    server_->route("/api/v1/measurements/latest", Method::Get, [this](const QHttpServerRequest& request) {
        if (!authorized(request)) return errorResponse("unauthorized", HttpStatus::Unauthorized);
        return jsonResponse({{"deviceId", deviceId_}, {"linkConnected", connected_}, {"measurement",
            measurement_.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(measurement_)}});
    });
    server_->route("/api/v1/commands", Method::Post, [this](const QHttpServerRequest& request) {
        return submit(request);
    });
    server_->route("/api/v1/commands/<arg>", Method::Get, [this](const QString& id, const QHttpServerRequest& request) {
        if (!authorized(request)) return errorResponse("unauthorized", HttpStatus::Unauthorized);
        const auto it = commands_.constFind(id);
        if (it == commands_.cend()) return errorResponse("command-not-found", HttpStatus::NotFound);
        return jsonResponse(it->result);
    });
    server_->addWebSocketUpgradeVerifier(this, [this](const QHttpServerRequest& request) {
        using Upgrade = QHttpServerWebSocketUpgradeResponse;
        if (request.url().path() != QLatin1String(kStreamPath)) return Upgrade::deny(404, "Unknown path");
        if (!authorized(request)) return Upgrade::deny(401, "Unauthorized");
        if (subscribers_.size() >= kMaxSubscribers) return Upgrade::deny(503, "Subscription limit reached");
        return Upgrade::accept();
    });
    connect(server_.get(), &QHttpServer::newWebSocketConnection, this, &GatewayListener::acceptSubscriber);
    if (!listener->listen(QHostAddress(config_.address), config_.port))
        return QStringLiteral("TLS 监听失败：") + listener->errorString();
    if (!server_->bind(listener.get())) return QStringLiteral("无法绑定 HTTPS 服务");
    port_ = listener.release()->serverPort();
    drain();
    timer_.start();
    return {};
}

bool GatewayListener::authorized(const QHttpServerRequest& request) const
{
    const auto values = request.headers().values(QHttpHeaders::WellKnownHeader::Authorization);
    if (values.size() != 1 || values.first().size() > 263) return false;
    const auto hash = QCryptographicHash::hash(values.first(), QCryptographicHash::Sha256);
    unsigned char differences = 0;
    for (qsizetype i = 0; i < hash.size(); ++i)
        differences |= static_cast<unsigned char>(hash[i] ^ authorizationHash_[i]);
    return differences == 0;
}

QHttpServerResponse GatewayListener::submit(const QHttpServerRequest& request)
{
    if (!authorized(request)) return errorResponse("unauthorized", HttpStatus::Unauthorized);
    if (!config_.allowCommands) return errorResponse("forbidden", HttpStatus::Forbidden);
    if (request.body().size() > 2048) return errorResponse("body-too-large", HttpStatus::PayloadTooLarge);
    const auto body = QJsonDocument::fromJson(request.body()).object();
    const QString id = body.value("id").toString();
    const int command = body.value("commandId").toInt(-1);
    const QString expires = body.value("expiresAt").toString();
    const auto expiryDate = QDateTime::fromString(expires, Qt::ISODateWithMs);
    static const QRegularExpression idPattern(QStringLiteral("\\A[A-Za-z0-9_-]{1,64}\\z"));
    if (body.size() != 3 || !idPattern.match(id).hasMatch() || command < 0 || command > 65535 ||
        !expires.endsWith(QLatin1Char('Z')) || !expiryDate.isValid())
        return errorResponse("invalid-command", HttpStatus::BadRequest);
    if ((command != int(CommandId::RequestStatus) && command != int(CommandId::StartRecording) &&
         command != int(CommandId::StopRecording)) || !config_.allowedCommands.contains(quint16(command)))
        return errorResponse("forbidden", HttpStatus::Forbidden);

    const auto expiry = expiryDate.toMSecsSinceEpoch();
    const auto now = QDateTime::currentMSecsSinceEpoch();
    const auto existing = commands_.constFind(id);
    if (existing != commands_.cend())
    {
        if (existing->commandId != command || existing->expiresAt != expiry)
            return errorResponse("command-id-conflict", HttpStatus::Conflict);
        return jsonResponse(existing->result, existing->result.value("status") == QLatin1String("accepted")
            ? HttpStatus::Accepted : HttpStatus::Ok);
    }
    if (expiry <= now || expiry > now + kMaxCommandLifetimeMs)
        return errorResponse("expired-or-expiry-out-of-range", HttpStatus::BadRequest);
    for (auto it = commands_.begin(); it != commands_.end();)
    {
        if (it->expiresAt + kRetentionMs < now) it = commands_.erase(it);
        else ++it;
    }
    if (commands_.size() >= kMaxCommands)
        return errorResponse("command-capacity", HttpStatus::ServiceUnavailable);
    QJsonObject result{{"id", id}, {"deviceId", deviceId_}, {"skyCommandId", command}, {"status", "accepted"}};
    commands_.insert(id, {command, expiry, result});
    dispatch_(body);
    return jsonResponse(result, HttpStatus::Accepted);
}

void GatewayListener::complete(QJsonObject result)
{
    const auto it = commands_.find(result.value("id").toString());
    if (it == commands_.end()) return;
    result.insert("deviceId", deviceId_);
    result.insert("skyCommandId", it->commandId);
    it->result = std::move(result);
}

void GatewayListener::acceptSubscriber()
{
    auto socket = server_->nextPendingWebSocketConnection();
    if (!socket) return;
    auto *client = socket.release();
    client->setParent(this);
    client->setMaxAllowedIncomingFrameSize(1024);
    client->setMaxAllowedIncomingMessageSize(1024);
    const auto transport = std::find_if(tlsConnections_.cbegin(), tlsConnections_.cend(), [client](const auto& tls) {
        return tls && tls->peerPort() == client->peerPort() && tls->peerAddress() == client->peerAddress() &&
            tls->localAddress() == client->localAddress();
    });
    if (transport == tlsConnections_.cend())
    {
        client->abort();
        client->deleteLater();
        return;
    }
    // Keep kernel buffering comparable to the application budget when a subscriber stops reading.
    (*transport)->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, kQueuedBytes);
    subscribers_.insert(client, *transport);
    connect(client, &QWebSocket::disconnected, this, [this, client] {
        subscribers_.remove(client);
        client->deleteLater();
    });
    connect(client, &QWebSocket::textMessageReceived, this, [client] { client->abort(); });
    connect(client, &QWebSocket::binaryMessageReceived, this, [client] { client->abort(); });
    auto snapshot = statusMessage();
    snapshot.insert("streamId", streamId_);
    snapshot.insert("sequence", QString::number(eventSequence_));
    snapshot.insert("ingressDropped", QString::number(dropped_));
    snapshot.insert("snapshot", true);
    send(client, QString::fromUtf8(QJsonDocument(snapshot).toJson(QJsonDocument::Compact)));
}

QJsonObject GatewayListener::statusMessage() const
{
    auto message = status_;
    message.insert("kind", "status");
    message.insert("deviceId", deviceId_);
    message.insert("linkConnected", connected_);
    message.insert("telemetryAvailable", !status_.isEmpty());
    return message;
}

void GatewayListener::publish(QJsonObject& message)
{
    message.insert("deviceId", deviceId_);
    message.insert("streamId", streamId_);
    message.insert("sequence", QString::number(++eventSequence_));
    message.insert("ingressDropped", QString::number(dropped_));
    if (subscribers_.isEmpty()) return;
    const auto encoded = QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact));
    for (auto *socket : subscribers_.keys()) send(socket, encoded);
}

void GatewayListener::send(QWebSocket *socket, const QString& message)
{
    const auto transport = subscribers_.value(socket);
    // QWebSocket::bytesToWrite() counts plaintext only for a TLS socket.
    if (transport && socket->bytesToWrite() + transport->encryptedBytesToWrite() +
        4 * message.size() + 32 > kQueuedBytes)
    {
        ++slowSubscribersDisconnected_;
        socket->abort();
        return;
    }
    if (!transport)
    {
        socket->abort();
        return;
    }
    if (socket->sendTextMessage(message) < 0) socket->abort();
}

void GatewayListener::drain()
{
    QList<TelemetryFrame> frames;
    bool linkChanged;
    {
        QMutexLocker lock(&ingress_->mutex);
        frames.swap(ingress_->frames);
        ingress_->bytes = 0;
        dropped_ = ingress_->dropped;
        linkChanged = linkRevision_ != ingress_->linkRevision;
        linkRevision_ = ingress_->linkRevision;
        connected_ = ingress_->connected;
    }
    if (linkChanged)
    {
        status_ = {};
        measurement_ = {};
        auto message = statusMessage();
        publish(message);
    }
    for (const auto& frame : frames)
    {
        if (frame.type == MsgType::TelemetryStatus)
        {
            TelemetryStatus status;
            if (!TelemetryCodec::parseTelemetryStatus(frame.payload, status)) continue;
            status_ = frameFields(frame);
            const auto fields = statusFields(status);
            for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) status_.insert(it.key(), it.value());
            auto message = statusMessage();
            publish(message);
        }
        else if (frame.type == MsgType::WaveformFeature)
        {
            WaveformFeature feature;
            if (!TelemetryCodec::parseWaveformFeature(frame.payload, feature)) continue;
            measurement_ = frameFields(frame);
            measurement_.insert("kind", "measurement");
            measurement_.insert("source", "waveform-feature");
            measurement_.insert("channel", feature.channel_id);
            const bool valid = feature.quality_flags == 0 && std::isfinite(feature.peak);
            measurement_.insert("valid", valid);
            measurement_.insert("value", valid ? QJsonValue(double(feature.peak)) : QJsonValue(QJsonValue::Null));
            measurement_.insert("unit", "native");
            measurement_.insert("qualityFlags", qint64(feature.quality_flags));
            measurement_.insert("peakIndex", finiteValue(feature.peak_index));
            measurement_.insert("mean", finiteValue(feature.mean));
            measurement_.insert("rms", finiteValue(feature.rms));
            publish(measurement_);
        }
        else
        {
            quint64 hostTimeUs = 0;
            FpgaWave::CompletedStream stream;
            if (!FpgaRemote::parseCycleFeature(frame.payload, hostTimeUs, stream) || !stream.normalizedFeature)
                continue;
            const auto& feature = *stream.normalizedFeature;
            measurement_ = frameFields(frame);
            measurement_.insert("kind", "measurement");
            measurement_.insert("source", "fpga-cycle-feature");
            measurement_.insert("channel", stream.source);
            measurement_.insert("hostTimeUs", QString::number(hostTimeUs));
            measurement_.insert("cycleId", qint64(stream.cycleId));
            measurement_.insert("validPoints", qint64(feature.validPoints));
            const bool valid = feature.validPoints > 0 && std::isfinite(feature.peak);
            measurement_.insert("valid", valid);
            measurement_.insert("value", valid ? QJsonValue(feature.peak) : QJsonValue(QJsonValue::Null));
            measurement_.insert("unit", "native");
            measurement_.insert("qualityFlags", qint64(feature.quality));
            measurement_.insert("peakIndex", feature.peakIndex);
            measurement_.insert("mean", finiteValue(feature.mean));
            measurement_.insert("rms", finiteValue(feature.rms));
            publish(measurement_);
        }
    }
}

}  // namespace VaporView::Ground::Gateway
