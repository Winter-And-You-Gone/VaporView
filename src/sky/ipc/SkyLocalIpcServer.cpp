#include "SkyLocalIpcServer.h"
#include "LogService.h"

#include <QAbstractSocket>
#include <QCoreApplication>
#include <QDateTime>
#include <QHostAddress>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace VaporView
{
namespace
{
bool shouldBroadcastRuntimeFrame(MsgType type)
{
    switch (type)
    {
    case MsgType::TelemetryBasic:
    case MsgType::WaveformDownsampled:
    case MsgType::WaveformFeature:
    case MsgType::TelemetryStatus:
    case MsgType::SkyConfig:
    case MsgType::SkyConfigApplyResult:
    case MsgType::Heartbeat:
    case MsgType::LogEvent:
    case MsgType::Error:
        return true;
    case MsgType::Command:
    case MsgType::CommandAck:
        return false;
    }
    return false;
}

}  // namespace

SkyLocalIpcServer::SkyLocalIpcServer(SkyRuntime *runtime, QObject *parent)
    : QObject(parent)
    , runtime_(runtime)
    , server_(new QTcpServer(this))
{
    connect(server_, &QTcpServer::newConnection, this, &SkyLocalIpcServer::onNewConnection);
    connect(&status_timer_, &QTimer::timeout, this, &SkyLocalIpcServer::onStatusTimer);
    status_timer_.setInterval(500);
    status_timer_.setTimerType(Qt::CoarseTimer);

    LogService::withCurrentInstance([this](LogService& logService) {
        connect(&logService, &LogService::recordPublished, this,
                [this](const LogRecord& record) {
                    if (record.source == QStringLiteral("SkyTui"))
                    {
                        return;
                    }
                    broadcastFrame(MsgType::LogEvent, TelemetryCodec::serializeLogRecord(record));
                });
    });

    if (runtime_)
    {
        connect(runtime_, &SkyRuntime::telemetryFrameReady, this, &SkyLocalIpcServer::broadcastRuntimeFrame);
    }
}

SkyLocalIpcServer::~SkyLocalIpcServer()
{
    close();
}

void SkyLocalIpcServer::publishIpcLog(LogLevel level,
                                      const QString& event,
                                      const QString& message,
                                      QVariantMap fields)
{
    fields.insert(QStringLiteral("event"), event);
    if (level >= LogLevel::Error &&
        !fields.contains(QStringLiteral("error_code")) &&
        !fields.contains(QStringLiteral("reason_code")))
    {
        fields.insert(QStringLiteral("error_code"), QStringLiteral("SKY_IPC_ERROR"));
    }

    const bool published = LogService::withCurrentInstance([&](LogService& logService) {
        logService.publish(level,
                           QStringLiteral("SkyCore"),
                           QStringLiteral("ipc"),
                           message,
                           fields);
    });
    emit logMessage(message);
    if (published)
    {
        return;
    }

    LogRecord fallback;
    fallback.level = level;
    fallback.source = QStringLiteral("SkyCore");
    fallback.category = QStringLiteral("ipc");
    fallback.message = message;
    fallback.fields = fields;
    broadcastFrame(MsgType::LogEvent, TelemetryCodec::serializeLogRecord(fallback));
}

bool SkyLocalIpcServer::listen(const QString& host, quint16 port)
{
    close();

    QHostAddress address(host);
    if (address.isNull())
    {
        address = QHostAddress::LocalHost;
    }

    if (!server_->listen(address, port))
    {
        publishIpcLog(LogLevel::Error,
                      QStringLiteral("sky_ipc_listen_failed"),
                      QStringLiteral("本地 IPC 服务监听失败。"),
                      {{QStringLiteral("error_code"), QStringLiteral("SKY_IPC_LISTEN_FAILED")},
                       {QStringLiteral("host"), address.toString()},
                       {QStringLiteral("port"), port},
                       {QStringLiteral("system_error"), server_->errorString()}});
        return false;
    }

    status_timer_.start();
    publishIpcLog(LogLevel::Info,
                  QStringLiteral("sky_ipc_listening"),
                  QStringLiteral("本地 IPC 服务已开始监听。"),
                  {{QStringLiteral("host"), server_->serverAddress().toString()},
                   {QStringLiteral("port"), server_->serverPort()}});
    return true;
}

void SkyLocalIpcServer::close()
{
    status_timer_.stop();
    if (server_->isListening())
    {
        server_->close();
    }

    const QVector<ClientState*> clients = clients_;
    for (ClientState *state : clients)
    {
        if (state && state->socket)
        {
            state->socket->disconnect(this);
            state->socket->disconnectFromHost();
        }
        delete state;
    }
    clients_.clear();
}

bool SkyLocalIpcServer::isListening() const
{
    return server_->isListening();
}

quint16 SkyLocalIpcServer::serverPort() const
{
    return server_->serverPort();
}

void SkyLocalIpcServer::onNewConnection()
{
    while (QTcpSocket *socket = server_->nextPendingConnection())
    {
        auto *state = new ClientState;
        state->socket = socket;
        clients_.push_back(state);

        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            onReadyRead(socket);
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            removeClient(socket);
        });
        connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError error) {
            publishIpcLog(LogLevel::Warning,
                          QStringLiteral("sky_ipc_client_error"),
                          QStringLiteral("本地 IPC 客户端连接异常。"),
                          {{QStringLiteral("reason_code"), QStringLiteral("SKY_IPC_CLIENT_SOCKET_ERROR")},
                           {QStringLiteral("socket_error"), static_cast<int>(error)},
                           {QStringLiteral("system_error"), socket->errorString()}});
        });

        publishIpcLog(LogLevel::Info,
                      QStringLiteral("sky_ipc_client_connected"),
                      QStringLiteral("本地 IPC 客户端已连接。"),
                      {{QStringLiteral("peer_host"), socket->peerAddress().toString()},
                       {QStringLiteral("peer_port"), socket->peerPort()}});
        sendStatus(socket);
        sendSkyConfig(socket);
    }
}

void SkyLocalIpcServer::onStatusTimer()
{
    broadcastFrame(MsgType::TelemetryStatus, TelemetryCodec::serializeTelemetryStatus(runtime_->currentStatus()));
}

SkyLocalIpcServer::ClientState *SkyLocalIpcServer::stateFor(QTcpSocket *socket) const
{
    for (ClientState *state : clients_)
    {
        if (state && state->socket == socket)
        {
            return state;
        }
    }
    return nullptr;
}

void SkyLocalIpcServer::removeClient(QTcpSocket *socket)
{
    for (int i = 0; i < clients_.size(); ++i)
    {
        ClientState *state = clients_.at(i);
        if (state && state->socket == socket)
        {
            clients_.removeAt(i);
            publishIpcLog(LogLevel::Info,
                          QStringLiteral("sky_ipc_client_disconnected"),
                          QStringLiteral("本地 IPC 客户端已断开。"));
            socket->deleteLater();
            delete state;
            return;
        }
    }
}

void SkyLocalIpcServer::onReadyRead(QTcpSocket *socket)
{
    ClientState *state = stateFor(socket);
    if (!state)
    {
        return;
    }

    const QVector<TelemetryFrame> frames = state->codec.feedBytes(socket->readAll());
    for (const TelemetryFrame& frame : frames)
    {
        dispatchClientFrame(socket, frame);
    }
}

void SkyLocalIpcServer::dispatchClientFrame(QTcpSocket *socket, const TelemetryFrame& frame)
{
    if (frame.type != MsgType::Command)
    {
        return;
    }

    CommandMessage command;
    if (!TelemetryCodec::parseCommand(frame.payload, command))
    {
        CommandAck ack;
        ack.command_id = CommandId::RequestStatus;
        ack.command_seq = 0;
        ack.result = 1;
        ack.error_code = CommandErrorCode::InvalidPayload;
        sendFrame(socket, MsgType::CommandAck, TelemetryCodec::serializeCommandAck(ack));
        return;
    }
    handleCommand(socket, command);
}

void SkyLocalIpcServer::handleCommand(QTcpSocket *socket, const CommandMessage& command)
{
    if (command.command_id == CommandId::ShutdownCore)
    {
        CommandAck ack;
        ack.command_id = command.command_id;
        ack.command_seq = command.command_seq;
        ack.result = 0;
        ack.error_code = CommandErrorCode::Ok;
        sendFrame(socket, MsgType::CommandAck, TelemetryCodec::serializeCommandAck(ack));
        if (socket)
        {
            socket->flush();
        }
        publishIpcLog(LogLevel::Info,
                      QStringLiteral("sky_ipc_shutdown_requested"),
                      QStringLiteral("已收到本地 IPC 停止请求。"),
                      {{QStringLiteral("command_id"), static_cast<quint16>(command.command_id)},
                       {QStringLiteral("command_seq"), command.command_seq}});
        QTimer::singleShot(100, QCoreApplication::instance(), []() {
            QCoreApplication::quit();
        });
        return;
    }

    if (!runtime_)
    {
        CommandAck ack;
        ack.command_id = command.command_id;
        ack.command_seq = command.command_seq;
        ack.result = 1;
        ack.error_code = CommandErrorCode::InternalError;
        sendFrame(socket, MsgType::CommandAck, TelemetryCodec::serializeCommandAck(ack));
        return;
    }

    const SkyCommandResult result = runtime_->executeCommand(command);
    sendFrame(socket, MsgType::CommandAck, TelemetryCodec::serializeCommandAck(result.ack));
    sendCommandResultFrames(socket, result);
}

void SkyLocalIpcServer::sendCommandResultFrames(QTcpSocket *socket, const SkyCommandResult& result)
{
    if (result.send_status)
    {
        broadcastFrame(MsgType::TelemetryStatus, TelemetryCodec::serializeTelemetryStatus(runtime_->currentStatus()));
    }
    if (result.send_sky_config)
    {
        sendSkyConfig(socket);
    }
    if (result.send_config_apply_result)
    {
        sendConfigApplyResult(socket, result.config_apply_result);
    }
    if (result.send_one_waveform)
    {
        sendCurrentWaveforms(socket);
    }
}

void SkyLocalIpcServer::sendStatus(QTcpSocket *socket)
{
    sendFrame(socket, MsgType::TelemetryStatus, TelemetryCodec::serializeTelemetryStatus(runtime_->currentStatus()));
}

void SkyLocalIpcServer::sendSkyConfig(QTcpSocket *socket)
{
    sendFrame(socket,
              MsgType::SkyConfig,
              QJsonDocument(runtime_->currentConfig().toJson()).toJson(QJsonDocument::Compact));
}

void SkyLocalIpcServer::sendConfigApplyResult(QTcpSocket *socket, const QJsonObject& result)
{
    sendFrame(socket, MsgType::SkyConfigApplyResult, QJsonDocument(result).toJson(QJsonDocument::Compact));
}

void SkyLocalIpcServer::sendCurrentWaveforms(QTcpSocket *socket)
{
    const QVector<DownsampledWaveform> waveforms = runtime_->currentDownsampledWaveforms();
    for (const DownsampledWaveform& waveform : waveforms)
    {
        sendFrame(socket, MsgType::WaveformDownsampled, TelemetryCodec::serializeDownsampledWaveform(waveform));
    }
}

void SkyLocalIpcServer::broadcastRuntimeFrame(MsgType type, const QByteArray& payload)
{
    if (shouldBroadcastRuntimeFrame(type))
    {
        broadcastFrame(type, payload);
    }
}

void SkyLocalIpcServer::broadcastFrame(MsgType type, const QByteArray& payload)
{
    const QVector<ClientState*> clients = clients_;
    for (ClientState *state : clients)
    {
        if (state && state->socket && state->socket->state() == QAbstractSocket::ConnectedState)
        {
            sendFrame(state->socket, type, payload);
        }
    }
}

void SkyLocalIpcServer::sendFrame(QTcpSocket *socket, MsgType type, const QByteArray& payload)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState)
    {
        return;
    }
    socket->write(encoder_.encodeFrame(type, payload, next_frame_seq_++, currentTimestampUs()));
}

quint64 SkyLocalIpcServer::currentTimestampUs() const
{
    return static_cast<quint64>(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) * 1000ULL;
}

}  // namespace VaporView
