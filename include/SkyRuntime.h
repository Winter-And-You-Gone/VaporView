#ifndef VaporView_SKY_RUNTIME_H_
#define VaporView_SKY_RUNTIME_H_

#include "SerialTelemetryLink.h"
#include "SkyDeviceManager.h"
#include "SkySessionRecorder.h"
#include "TelemetryCodec.h"

#include <QObject>
#include <QTimer>

namespace VaporView
{

struct SkyRuntimeOptions
{
    QString telemetry_port;
    int telemetry_baud = 921600;
    QString config_path;
    bool simulate_data = false;
    QString wave_host = QStringLiteral("127.0.0.1");
    int wave_port = 8888;
};

class SkyRuntime : public QObject
{
    Q_OBJECT

public:
    explicit SkyRuntime(const SkyRuntimeOptions& options, QObject *parent = nullptr);
    ~SkyRuntime() override;

    bool start();
    void stop();
    bool isRunning() const;

    bool connectDevice(SkyDeviceId id, CommandErrorCode *error = nullptr);
    bool disconnectDevice(SkyDeviceId id, CommandErrorCode *error = nullptr);
    bool reconnectDevice(SkyDeviceId id, CommandErrorCode *error = nullptr);

    void connectAllDevices();
    void disconnectAllDevices();
    void reconnectAllDevices();

    bool startRecording(QString *error = nullptr);
    bool pauseRecording(QString *error = nullptr);
    bool stopRecording(QString *error = nullptr);

    TelemetryStatus currentStatus() const;
    SkyConfig currentConfig() const;

    void setWaveformStreamingEnabled(bool enabled);
    bool waveformStreamingEnabled() const;
    void sendOneWaveformNow();

signals:
    void logMessage(const QString& message);
    void runningChanged(bool running);

private slots:
    void onBytesReceived(const QByteArray& bytes);
    void sendBasicTelemetry();
    void sendWaveformFeature();
    void sendDownsampledWaveform();
    void sendHeartbeat();
    void sendTelemetryStatus();

private:
    void dispatchFrame(const TelemetryFrame& frame);
    void handleCommand(const CommandMessage& command);
    void sendFrame(MsgType type, const QByteArray& payload);
    void sendAck(const CommandMessage& command, CommandErrorCode errorCode = CommandErrorCode::Ok);
    void sendSkyConfig();
    void sendSkyConfigApplyResult(const QJsonObject& result);
    void sendDownsampledWaveformFrame(bool honorStreamingEnabled);
    void updateTimerIntervals();
    quint64 currentTimestampUs() const;

    SkyRuntimeOptions options_;
    SerialTelemetryLink link_;
    TelemetryCodec codec_;
    SkyDeviceManager device_manager_;
    QTimer basic_timer_;
    QTimer feature_timer_;
    QTimer waveform_timer_;
    QTimer heartbeat_timer_;
    QTimer status_timer_;
    quint16 next_frame_seq_ = 1;
    quint32 rx_total_frames_ = 0;
    quint64 last_frame_time_us_ = 0;
    SkySessionRecorder session_recorder_;
    bool running_ = false;
    bool waveform_streaming_enabled_ = true;
};

}  // namespace VaporView

#endif
