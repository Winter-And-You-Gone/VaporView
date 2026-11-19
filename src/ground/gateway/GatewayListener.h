#pragma once

#include "ground/gateway/GatewayService.h"

#include <QDeadlineTimer>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QPointer>
#include <QTimer>

class QHttpServer;
class QHttpServerRequest;
class QHttpServerResponse;
class QSslSocket;
class QWebSocket;

namespace VaporView::Ground::Gateway
{

// Shared between the telemetry producer and the network worker.
struct GatewayListenerIngress
{
    QMutex mutex;
    QList<TelemetryFrame> frames;
    qsizetype bytes = 0;
    quint64 dropped = 0;
    quint64 linkRevision = 0;
    bool connected = false;

    void enqueue(const TelemetryFrame& frame);
    void setConnected(bool open);
};

class GatewayListener final : public QObject
{
public:
    using Dispatch = std::function<void(const QJsonObject&)>;
    GatewayListener(const GatewayListenerConfig& config, QString deviceId,
                    std::shared_ptr<GatewayListenerIngress> ingress, Dispatch dispatch);
    ~GatewayListener() override;
    QString start();
    quint16 port() const { return port_; }
    void complete(QJsonObject result);

private:
    bool authorized(const QHttpServerRequest& request) const;
    QHttpServerResponse issueStreamTicket(const QHttpServerRequest& request);
    bool consumeStreamTicket(const QHttpServerRequest& request);
    QHttpServerResponse submit(const QHttpServerRequest& request);
    void acceptSubscriber();
    void drain();
    QJsonObject statusMessage() const;
    void publish(QJsonObject& message);
    void send(QWebSocket *socket, const QString& message);

    GatewayListenerConfig config_;
    QString deviceId_;
    QByteArray authorizationHash_;
    std::shared_ptr<GatewayListenerIngress> ingress_;
    Dispatch dispatch_;
    QTimer timer_;
    QList<QPointer<QSslSocket>> tlsConnections_;
    QHash<QWebSocket *, QPointer<QSslSocket>> subscribers_;
    std::unique_ptr<QHttpServer> server_;
    QJsonObject status_;
    QJsonObject measurement_;
    QString streamId_;
    quint64 eventSequence_ = 0;
    quint64 linkRevision_ = 0;
    quint64 dropped_ = 0;
    quint64 slowSubscribersDisconnected_ = 0;
    bool connected_ = false;
    quint16 port_ = 0;
    struct CommandEntry
    {
        int commandId;
        qint64 expiresAt;
        QJsonObject result;
    };
    QHash<QString, CommandEntry> commands_;
    struct StreamTicket
    {
        QByteArray origin;
        QDeadlineTimer expiry;
    };
    QHash<QString, StreamTicket> streamTickets_;
};

}  // namespace VaporView::Ground::Gateway
