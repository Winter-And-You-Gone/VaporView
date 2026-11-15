#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QVector>

namespace VaporView::Ground::Devices
{
class FpgaDeviceSession;

// Owns one exchange on an already configured UART. The controller must hold
// its ordinary-command lease until finished; motor safety traffic stays live.
class FpgaUartExchange final : public QObject
{
    Q_OBJECT
public:
    explicit FpgaUartExchange(FpgaDeviceSession* session, QObject* parent = nullptr);
    ~FpgaUartExchange() override;
    bool start(const QJsonObject& request);

signals:
    void finished(const QJsonObject& result);

private:
    enum class Phase { Idle, Configuration, InitialLevels, Transmit, Status, Levels, Receive };
    void read(quint32 address, quint16 count, Phase phase);
    void completeRead();
    void pollReceive();
    void receiveBytes(quint16 count);
    void finish(bool ok, const QString& detail);
    void cancelPending();

    QPointer<FpgaDeviceSession> session_;
    QTimer deadlineTimer_;
    QTimer receiveTimer_;
    QElapsedTimer quietTimer_;
    QSet<quint32> pending_;
    QVector<quint32> readValues_;
    QMap<quint32, quint32> receivedWords_;
    QByteArray transmit_;
    QByteArray received_;
    Phase phase_ = Phase::Idle;
    quint32 base_ = 0;
    quint32 expectedId_ = 0;
    quint32 initialDrops_ = 0;
    quint32 initialParity_ = 0;
    quint32 initialFraming_ = 0;
    quint32 remainingRx_ = 0;
    int channel_ = 0;
    int maxRx_ = 0;
    int quietMs_ = 20;
    int receivePollCount_ = 0;
    int receiveTimerTimeoutCount_ = 0;
    bool active_ = false;
    bool transmitted_ = false;
    bool transmitterBusy_ = false;
};
}
