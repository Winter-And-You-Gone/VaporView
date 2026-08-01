#ifndef VaporView_SKY_LOCAL_IPC_CLIENT_H_
#define VaporView_SKY_LOCAL_IPC_CLIENT_H_

#include "SkyConfig.h"
#include "SkyDashboardTypes.h"
#include "TelemetryCodec.h"

#include <QObject>
#include <QAbstractSocket>
#include <QTcpSocket>
#include <QTimer>
#include <QVariantMap>

namespace VaporView
{

class SkyLocalIpcClient : public QObject
{
    Q_OBJECT

public:
    explicit SkyLocalIpcClient(QObject *parent = nullptr);

    void connectToCore(const QString& host, quint16 port);
    void disconnectFromCore();
    bool isConnected() const;
    void setAutoReconnectEnabled(bool enabled, int intervalMs = 1000);
    bool autoReconnectEnabled() const;

    TelemetryStatus currentStatus() const;
    SkyConfig currentConfig() const;
    SkyDashboardSnapshot dashboardSnapshot() const;
    bool waveformStreamingEnabled() const;

    quint16 requestStatus();
    quint16 startRecording();
    quint16 pauseRecording();
    quint16 stopRecording();
    quint16 connectDevice(SkyDeviceId id);
    quint16 disconnectDevice(SkyDeviceId id);
    quint16 reconnectDevice(SkyDeviceId id);
    quint16 connectAllDevices();
    quint16 disconnectAllDevices();
    quint16 reconnectAllDevices();
    quint16 enableWaveformStreaming();
    quint16 disableWaveformStreaming();
    quint16 requestOneWaveform();
    quint16 getConfig();
    quint16 setConfig(const SkyConfig& config);
    quint16 saveConfig();
    quint16 requestCoreShutdown();

signals:
    void connectedChanged(bool connected);
    void logMessage(const QString& message);
    void logRecordGenerated(const VaporView::LogRecord& record);
    void logRecordReceived(const VaporView::LogRecord& record);
    void statusReceived(const TelemetryStatus& status);
    void basicReceived(const TelemetryBasic& basic);
    void featureReceived(const WaveformFeature& feature);
    void waveformReceived(const DownsampledWaveform& waveform);
    void ackReceived(const CommandAck& ack);
    void configReceived(const SkyConfig& config);
    void dashboardUpdated();

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onErrorOccurred(QAbstractSocket::SocketError error);

private:
    quint16 sendCommand(CommandId commandId, const QByteArray& payload = QByteArray());
    void publishClientLog(LogLevel level,
                          const QString& category,
                          const QString& event,
                          const QString& message,
                          QVariantMap fields = QVariantMap());
    void dispatchFrame(const TelemetryFrame& frame);
    void attemptReconnect();
    void scheduleReconnect();
    void updateFromBasic(const TelemetryBasic& basic);
    void updateFromStatus(const TelemetryStatus& status);
    void updateFromFeature(const WaveformFeature& feature);
    void updateFromWaveform(const DownsampledWaveform& waveform);
    void updateDashboardRates();
    void updateDeviceFreshness();
    QVector<float> waveformPreview(const QVector<float>& samples, int maxPoints) const;
    quint64 currentTimestampUs() const;

    QTcpSocket socket_;
    QTimer reconnect_timer_;
    TelemetryCodec decoder_;
    TelemetryCodec encoder_;
    QString core_host_;
    quint16 core_port_ = 0;
    bool auto_reconnect_enabled_ = false;
    bool user_disconnect_requested_ = false;
    quint16 next_frame_seq_ = 1;
    quint16 next_command_seq_ = 1;
    TelemetryStatus status_;
    SkyConfig config_ = SkyConfig::defaults();
    SkyDashboardSnapshot dashboard_;
    bool waveform_streaming_enabled_ = false;
    quint64 connected_time_us_ = 0;
};

}  // namespace VaporView

#endif
