#ifndef VaporView_SKY_RUNTIME_H_
#define VaporView_SKY_RUNTIME_H_

#include "LogRecord.h"
#include "SerialTelemetryLink.h"
#include "SkyDashboardTypes.h"
#include "SkyDeviceManager.h"
#include "SkySessionRecorder.h"
#include "TelemetryCodec.h"
#include "TelemetryLink.h"

#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <memory>

namespace VaporView
{

struct SkyRuntimeOptions
{
    TelemetryTransportType telemetry_transport = TelemetryTransportType::Tcp;
    QString telemetry_host = QStringLiteral("0.0.0.0");
    int telemetry_tcp_port = 39100;
    QString telemetry_port;
    int telemetry_baud = 921600;
    QString config_path;
    bool simulate_data = false;
    QString wave_host = QStringLiteral("127.0.0.1");
    int wave_port = 8888;
};

struct SkyCommandResult
{
    CommandAck ack;
    bool send_status = false;
    bool send_sky_config = false;
    bool send_config_apply_result = false;
    QJsonObject config_apply_result;
    bool send_one_waveform = false;
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
    SkyDashboardSnapshot dashboardSnapshot() const;
    QVector<DownsampledWaveform> currentDownsampledWaveforms() const;

    SkyCommandResult executeCommand(const CommandMessage& command);

    void setWaveformStreamingEnabled(bool enabled);
    bool waveformStreamingEnabled() const;
    void sendOneWaveformNow();

signals:
    void logRecord(const VaporView::LogRecord& record);
    void runningChanged(bool running);
    void telemetryFrameReady(MsgType type, QByteArray payload);

private slots:
    void onBytesReceived(const QByteArray& bytes);
    void sendBasicTelemetry();
    void sendWaveformFeature();
    void sendDownsampledWaveform();
    void sendHeartbeat();
    void sendTelemetryStatus();
    void sendTemperatureControllerStatus();

private:
    void dispatchFrame(const TelemetryFrame& frame);
    void handleCommand(const CommandMessage& command);
    void sendCommandResultFrames(const SkyCommandResult& result);
    void sendFrame(MsgType type, const QByteArray& payload);
    void sendAck(const CommandAck& ack);
    void sendSkyConfig();
    void sendSkyConfigApplyResult(const QJsonObject& result);
    void sendDownsampledWaveformFrame(bool honorStreamingEnabled);
    void publishRuntimeLog(LogLevel level,
                           const QString& category,
                           const QString& event,
                           const QString& message,
                           QVariantMap fields = QVariantMap());
    void updateTimerIntervals();
    quint64 currentTimestampUs() const;
    QVector<float> waveformPreview(const QVector<float>& samples, int maxPoints) const;
    bool deviceStale(SkyDeviceId id, quint64 nowUs, quint64 timeoutUs) const;

    SkyRuntimeOptions options_;
    std::unique_ptr<TelemetryLink> link_;
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
    bool waveform_streaming_enabled_ = false;
    quint64 started_time_us_ = 0;
    quint64 last_sent_feature_time_us_ = 0;
    QVector<float> peak_trend_;
};

}  // namespace VaporView

#endif
