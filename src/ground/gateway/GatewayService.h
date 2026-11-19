#pragma once

#include "TelemetryTypes.h"
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QThread>
#include <QUrl>
#include <functional>
#include <memory>
#include <optional>

namespace VaporView { class GroundTelemetryService; }

namespace VaporView::Ground::Gateway
{

struct GatewayListenerConfig
{
    bool enabled = false;
    QString address = QStringLiteral("127.0.0.1");
    quint16 port = 8443;

    bool operator==(const GatewayListenerConfig& other) const;
    QString validationError() const;
};

bool isListenerCommand(int commandId);
bool isListenerFpgaCommand(int commandId);

bool isGatewayServerUrlValid(const QUrl& url);

struct GatewayConfig
{
    bool enabled = false;
    QUrl serverUrl;
    QString deviceId;
    bool allowRemoteCommands = false;
    QSet<quint16> allowedCommands{static_cast<quint16>(CommandId::RequestStatus)};
    qint64 maxCacheBytes = 64 * 1024 * 1024;
    int retentionHours = 24;
    QString cacheDirectory;
    GatewayListenerConfig listener;
};

class GatewayWorker;
struct GatewayIngress;
struct GatewayListenerIngress;

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
    QString listenerStatusText() const { return listenerStatus_; }
    quint16 listenerPort() const { return listenerPort_; }
    using ListenerCommandPreparation = std::function<QString(CommandId, QByteArray&, QString& detail)>;
    void setListenerCommandPreparation(ListenerCommandPreparation preparation);
    void enqueueSession(const QString& directory);

signals:
    void statusChanged(const QString& status);
    void listenerStatusChanged(const QString& status, quint16 port);
    void listenerStartNotSent();
    void sessionSyncFinished(const QString& directory, bool success, const QString& error);

private:
    friend class GatewayWorker;
    void receiveCommand(const QJsonObject& command, quint64 generation, bool listener = false);
    void completeCommand(quint16 sequence, const QString& status, const CommandAck *ack = nullptr);
    void postResult(const QJsonObject& result, bool listener);
    GatewayConfig config_;
    QString status_ = QStringLiteral("Gateway 未启用");
    QString listenerStatus_ = QStringLiteral("入站网关未启用");
    quint16 listenerPort_ = 0;
    QPointer<GroundTelemetryService> service_;
    QList<QMetaObject::Connection> connections_;
    std::optional<TelemetryStatus> skyStatus_;
    struct Pending { QString id; CommandId command; quint64 generation; bool listener; };
    QHash<quint16, Pending> pending_;
    quint64 generation_ = 0;
    quint64 listenerGeneration_ = 0;
    ListenerCommandPreparation listenerPreparation_;
    QThread thread_;
    GatewayWorker *worker_ = nullptr;
    std::shared_ptr<GatewayIngress> ingress_;
    std::shared_ptr<GatewayListenerIngress> listenerIngress_;
};

}  // namespace VaporView::Ground::Gateway
