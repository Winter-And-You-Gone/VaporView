#ifndef VaporView_SKY_LOCAL_IPC_SERVER_H_
#define VaporView_SKY_LOCAL_IPC_SERVER_H_

#include "SkyRuntime.h"
#include "TelemetryCodec.h"

#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVector>

class QTcpServer;
class QTcpSocket;

namespace VaporView
{

class SkyLocalIpcServer : public QObject
{
    Q_OBJECT

public:
    explicit SkyLocalIpcServer(SkyRuntime *runtime, QObject *parent = nullptr);
    ~SkyLocalIpcServer() override;

    bool listen(const QString& host, quint16 port);
    void close();
    bool isListening() const;
    quint16 serverPort() const;

signals:
    void logMessage(const QString& message);

private slots:
    void onNewConnection();
    void onStatusTimer();

private:
    struct ClientState
    {
        QTcpSocket *socket = nullptr;
        TelemetryCodec codec;
    };

    ClientState *stateFor(QTcpSocket *socket) const;
    void removeClient(QTcpSocket *socket);
    void onReadyRead(QTcpSocket *socket);
    void dispatchClientFrame(QTcpSocket *socket, const TelemetryFrame& frame);
    void handleCommand(QTcpSocket *socket, const CommandMessage& command);
    void sendCommandResultFrames(QTcpSocket *socket, const SkyCommandResult& result);
    void sendStatus(QTcpSocket *socket);
    void sendSkyConfig(QTcpSocket *socket);
    void sendConfigApplyResult(QTcpSocket *socket, const QJsonObject& result);
    void sendCurrentWaveforms(QTcpSocket *socket);
    void broadcastRuntimeFrame(MsgType type, const QByteArray& payload);
    void broadcastFrame(MsgType type, const QByteArray& payload);
    void sendFrame(QTcpSocket *socket, MsgType type, const QByteArray& payload);
    quint64 currentTimestampUs() const;

    SkyRuntime *runtime_ = nullptr;
    QTcpServer *server_ = nullptr;
    QVector<ClientState*> clients_;
    TelemetryCodec encoder_;
    QTimer status_timer_;
    quint16 next_frame_seq_ = 1;
};

}  // namespace VaporView

#endif
