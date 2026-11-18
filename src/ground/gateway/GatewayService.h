#pragma once

#include "TelemetryTypes.h"
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QThread>
#include <QUrl>
#include <memory>

namespace VaporView { class GroundTelemetryService; }

namespace VaporView::Ground::Gateway
{

struct GatewayConfig
{
    bool enabled = false;
    QUrl serverUrl;
    QString deviceId;
    QString token;
    bool allowRemoteCommands = false;
    QSet<quint16> allowedCommands{static_cast<quint16>(CommandId::RequestStatus)};
    qint64 maxCacheBytes = 64 * 1024 * 1024;
    int retentionHours = 24;
    QString cacheDirectory;
};

class GatewayWorker;
struct GatewayIngress;

// Lives beside the existing controller. Network and disk work live on worker_.
class GatewayService final : public QObject
{
    Q_OBJECT
public:
    explicit GatewayService(QObject *parent = nullptr);
    ~GatewayService() override;
    void attach(GroundTelemetryService *service);
    void configure(const GatewayConfig& config);
    GatewayConfig config() const { return config_; }
    QString statusText() const { return status_; }
    void enqueueSession(const QString& directory);

signals:
    void statusChanged(const QString& status);
    void sessionSyncFinished(const QString& directory, bool success, const QString& error);

private:
    friend class GatewayWorker;
    void receiveCommand(const QJsonObject& command, quint64 generation);
    void completeCommand(quint16 sequence, const QString& status, const CommandAck *ack = nullptr);
    void postResult(const QJsonObject& result);
    GatewayConfig config_;
    QString status_ = QStringLiteral("Gateway 未启用");
    QPointer<GroundTelemetryService> service_;
    QList<QMetaObject::Connection> connections_;
    struct Pending { QString id; CommandId command; quint64 generation; };
    QHash<quint16, Pending> pending_;
    quint64 generation_ = 0;
    QThread thread_;
    GatewayWorker *worker_ = nullptr;
    std::shared_ptr<GatewayIngress> ingress_;
};

}  // namespace VaporView::Ground::Gateway
