#include "TcpTelemetryLink.h"

#include <utility>

namespace VaporView
{

TcpTelemetryLink::TcpTelemetryLink(QObject *parent)
    : TelemetryLink(parent)
{
    connect(&server_, &QTcpServer::newConnection, this, &TcpTelemetryLink::onNewConnection);
}

bool TcpTelemetryLink::listen(const QString& host, quint16 port)
{
    close();
    role_ = Role::Server;
    host_ = host.trimmed().isEmpty() ? QStringLiteral("0.0.0.0") : host.trimmed();
    port_ = port;
    if (!server_.listen(addressFromString(host_), port_))
    {
        emit errorOccurred(server_.errorString());
        return false;
    }
    open_emitted_ = true;
    emit openChanged(true);
    emitTcpLog(LogLevel::Info,
               QStringLiteral("telemetry_tcp_server_listening"),
               QStringLiteral("TCP 遥测服务端已开始监听。"),
               {{QStringLiteral("role"), QStringLiteral("server")},
                {QStringLiteral("host"), host_},
                {QStringLiteral("requested_port"), port_},
                {QStringLiteral("local_port"), server_.serverPort()},
                {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    return true;
}

bool TcpTelemetryLink::connectToHost(const QString& host, quint16 port)
{
    close();
    role_ = Role::Client;
    host_ = host.trimmed();
    port_ = port;
    auto *socket = new QTcpSocket(this);
    attachSocket(socket);
    socket->connectToHost(host_, port_);
    if (!socket->waitForConnected(3000))
    {
        emit errorOccurred(socket->errorString());
        closeSocket();
        return false;
    }
    if (!open_emitted_)
    {
        open_emitted_ = true;
        emit openChanged(true);
    }
    emitTcpLog(LogLevel::Info,
               QStringLiteral("telemetry_tcp_connected"),
               QStringLiteral("TCP 遥测客户端已连接。"),
               {{QStringLiteral("role"), QStringLiteral("client")},
                {QStringLiteral("host"), host_},
                {QStringLiteral("port"), port_},
                {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    return true;
}

void TcpTelemetryLink::close()
{
    const bool wasOpen = isOpen();
    server_.close();
    closeSocket();
    if (wasOpen || open_emitted_)
    {
        open_emitted_ = false;
        emit openChanged(false);
    }
}

bool TcpTelemetryLink::isOpen() const
{
    if (role_ == Role::Server)
    {
        return server_.isListening();
    }
    return socket_ && socket_->state() == QAbstractSocket::ConnectedState;
}

QString TcpTelemetryLink::endpointDescription() const
{
    return QStringLiteral("tcp://%1:%2").arg(host_).arg(port_);
}

quint16 TcpTelemetryLink::localPort() const
{
    if (role_ == Role::Server && server_.isListening())
    {
        return server_.serverPort();
    }
    if (socket_)
    {
        return socket_->localPort();
    }
    return 0;
}

qint64 TcpTelemetryLink::writeBytes(const QByteArray& bytes)
{
    if (!socket_ || socket_->state() != QAbstractSocket::ConnectedState)
    {
        return -1;
    }
    const qint64 written = socket_->write(bytes);
    if (written > 0)
    {
        socket_->flush();
    }
    return written;
}

void TcpTelemetryLink::onNewConnection()
{
    while (server_.hasPendingConnections())
    {
        QTcpSocket *next = server_.nextPendingConnection();
        if (!next)
        {
            continue;
        }
        const bool hadClient = socket_ && socket_->state() == QAbstractSocket::ConnectedState;
        if (hadClient)
        {
            emitTcpLog(LogLevel::Info,
                       QStringLiteral("telemetry_tcp_client_replaced"),
                       QStringLiteral("TCP 遥测客户端连接已被新连接替换。"),
                       {{QStringLiteral("role"), QStringLiteral("server")},
                        {QStringLiteral("peer_host"), next->peerAddress().toString()},
                        {QStringLiteral("peer_port"), next->peerPort()},
                        {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
        }
        closeSocket();
        attachSocket(next);
        emitTcpLog(LogLevel::Info,
                   QStringLiteral("telemetry_tcp_client_connected"),
                   QStringLiteral("TCP 遥测客户端已接入。"),
                   {{QStringLiteral("role"), QStringLiteral("server")},
                    {QStringLiteral("peer_host"), next->peerAddress().toString()},
                    {QStringLiteral("peer_port"), next->peerPort()},
                    {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    }
}

void TcpTelemetryLink::onReadyRead()
{
    auto *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket || socket != socket_)
    {
        return;
    }
    const QByteArray bytes = socket->readAll();
    if (!bytes.isEmpty())
    {
        emit bytesReceived(bytes);
    }
}

void TcpTelemetryLink::onSocketDisconnected()
{
    auto *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket || socket != socket_)
    {
        return;
    }
    const QString peer = QStringLiteral("%1:%2").arg(socket_->peerAddress().toString()).arg(socket_->peerPort());
    socket_->deleteLater();
    socket_ = nullptr;
    emitTcpLog(LogLevel::Info,
               QStringLiteral("telemetry_tcp_client_disconnected"),
               QStringLiteral("TCP 遥测客户端已断开。"),
               {{QStringLiteral("role"), role_ == Role::Server ? QStringLiteral("server") : QStringLiteral("client")},
                {QStringLiteral("peer"), peer},
                {QStringLiteral("reason_code"), QStringLiteral("REMOTE_DISCONNECTED")},
                {QStringLiteral("ui_visibility"), QStringLiteral("details")}});
    if (role_ == Role::Client && open_emitted_)
    {
        open_emitted_ = false;
        emit openChanged(false);
    }
}

void TcpTelemetryLink::emitTcpLog(LogLevel level,
                                  const QString& event,
                                  const QString& message,
                                  QVariantMap fields)
{
    fields.insert(QStringLiteral("event"), event);

    LogRecord record;
    record.level = level;
    record.source = QStringLiteral("TelemetryLink");
    record.category = QStringLiteral("telemetry.link");
    record.message = message;
    record.fields = std::move(fields);
    emit logRecordGenerated(record);
}

void TcpTelemetryLink::onSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    auto *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket)
    {
        return;
    }
    emit errorOccurred(socket->errorString());
}

void TcpTelemetryLink::attachSocket(QTcpSocket *socket)
{
    socket_ = socket;
    socket_->setParent(this);
    socket_->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(socket_, &QTcpSocket::readyRead, this, &TcpTelemetryLink::onReadyRead);
    connect(socket_, &QTcpSocket::disconnected, this, &TcpTelemetryLink::onSocketDisconnected);
    connect(socket_, &QTcpSocket::connected, this, [this]() {
        if (!open_emitted_)
        {
            open_emitted_ = true;
            emit openChanged(true);
        }
    });
    connect(socket_, &QTcpSocket::errorOccurred, this, &TcpTelemetryLink::onSocketError);
}

void TcpTelemetryLink::closeSocket()
{
    if (!socket_)
    {
        return;
    }
    QTcpSocket *socket = socket_;
    socket_ = nullptr;
    socket->disconnect(this);
    socket->close();
    socket->deleteLater();
}

QHostAddress TcpTelemetryLink::addressFromString(const QString& host)
{
    if (host.trimmed().isEmpty() ||
        host == QStringLiteral("0.0.0.0") ||
        host.compare(QStringLiteral("any"), Qt::CaseInsensitive) == 0)
    {
        return QHostAddress::AnyIPv4;
    }
    QHostAddress address;
    if (address.setAddress(host))
    {
        return address;
    }
    return QHostAddress::AnyIPv4;
}

}  // namespace VaporView
