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

    if (LogService *logService = LogService::instance())
    {
        connect(logService, &LogService::recordPublished, this,
                [this](const LogRecord& record) {
                    broadcastFrame(MsgType::LogEvent, TelemetryCodec::serializeLogRecord(record));
                });
    }

    if (runtime_)
    {
        connect(runtime_, &SkyRuntime::telemetryFrameReady, this, &SkyLocalIpcServer::broadcastRuntimeFrame);
        connect(runtime_, &SkyRuntime::logMessage, this, [this](const QString& message) {
            LogRecord record;
            if (LogService *logService = LogService::instance())
            {
                logService->publish(LogLevel::Info,
                                    QStringLiteral("SkyCore"),
                                    QStringLiteral("runtime"),
                                    message);
            }
            else
            {
                record.level = LogLevel::Info;
                record.source = QStringLiteral("SkyCore");
                record.category = QStringLiteral("runtime");
                record.message = message;
                broadcastFrame(MsgType::LogEvent, TelemetryCodec::serializeLogRecord(record));
            }
        });
    }
}

SkyLocalIpcServer::~SkyLocalIpcServer()
{
    close();
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
        emit logMessage(QStringLiteral("Sky local IPC listen failed on %1:%2: %3")
                            .arg(address.toString())
                            .arg(port)
                            .arg(server_->errorString()));
        return false;
    }

    status_timer_.start();
    emit logMessage(QStringLiteral("Sky local IPC listening on %1:%2")
                        .arg(server_->serverAddress().toString())
                        .arg(server_->serverPort()));
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
        connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) {
            emit logMessage(QStringLiteral("Sky local IPC client error: %1").arg(socket->errorString()));
        });

        emit logMessage(QStringLiteral("Sky local IPC client connected from %1:%2")
                            .arg(socket->peerAddress().toString())
                            .arg(socket->peerPort()));
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
            emit logMessage(QStringLiteral("Sky local IPC client disconnected"));
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
        emit logMessage(QStringLiteral("Sky local IPC shutdown requested"));
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
