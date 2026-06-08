#ifndef VaporView_TCP_TELEMETRY_LINK_H_
#define VaporView_TCP_TELEMETRY_LINK_H_

#include "TelemetryLink.h"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

namespace VaporView
{

class TcpTelemetryLink : public TelemetryLink
{
    Q_OBJECT

public:
    enum class Role
    {
        Server,
        Client,
    };

    explicit TcpTelemetryLink(QObject *parent = nullptr);

    bool listen(const QString& host, quint16 port);
    bool connectToHost(const QString& host, quint16 port);
    void close() override;
    bool isOpen() const override;
    QString endpointDescription() const override;
    quint16 localPort() const;
    qint64 writeBytes(const QByteArray& bytes) override;

private slots:
    void onNewConnection();
    void onReadyRead();
    void onSocketDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);

private:
    void attachSocket(QTcpSocket *socket);
    void closeSocket();
    static QHostAddress addressFromString(const QString& host);

    QTcpServer server_;
    QTcpSocket *socket_ = nullptr;
    Role role_ = Role::Client;
    QString host_;
    quint16 port_ = 0;
    bool open_emitted_ = false;
};

}  // namespace VaporView

#endif
