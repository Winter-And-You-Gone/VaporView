#ifndef VaporView_GROUND_TELEMETRY_SERVICE_H_
#define VaporView_GROUND_TELEMETRY_SERVICE_H_

#include "TelemetryCodec.h"
#include "TelemetryLink.h"

#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVector>
#include <memory>

namespace VaporView
{

class GroundTelemetryService : public QObject
{
    Q_OBJECT

public:
    struct ByteSample
    {
        qint64 time_ms = 0;
        qint64 bytes = 0;
    };

    explicit GroundTelemetryService(QObject *parent = nullptr);

    bool open(const QString& portName, int baudRate);
    bool openTcp(const QString& host, quint16 port);
    void close();
    bool isOpen() const;
    TelemetryTransportType transportType() const;
    QString endpointDescription() const;
    quint64 linkGeneration() const;
    double receiveBitsPerSecond() const;
    double transmitBitsPerSecond() const;
    quint64 totalReceivedBytes() const;
    quint64 totalTransmittedBytes() const;

    quint16 sendCommand(CommandId commandId, const QByteArray& payload = QByteArray());
    quint16 sendDeviceOperation(const DeviceOperationRequest& request);
    bool sendRtcmCorrectionData(const QByteArray& data);
    quint16 sendDeviceCommand(CommandId commandId, SkyDeviceId deviceId);
    quint16 sendRateCommand(CommandId commandId, quint16 hz);
    quint16 sendPeakSearchRangeCommand(quint32 startIndex, quint32 endIndex);
    quint16 requestSkyConfig();
    quint16 setSkyConfig(const QJsonObject& config);
    quint16 saveSkyConfig();

signals:
    void linkOpenChanged(bool open);
    void basicTelemetryUpdated(const TelemetryBasic& data);
    void waveformUpdated(const DownsampledWaveform& waveform);
    void waveformFeatureUpdated(const WaveformFeature& feature);
    void statusUpdated(const TelemetryStatus& status);
    void temperatureControllerStatusUpdated(const TemperatureControllerData& data);
    void ai8TemperatureControllerStatusUpdated(const Ai8TemperatureControllerProtocol::LiveData& data);
    void deviceOperationResponseReceived(const DeviceOperationResponse& response);
    void commandAckReceived(const CommandAck& ack);
    void commandTimedOut(CommandId commandId, quint16 commandSeq);
    void skyConfigReceived(const QJsonObject& config);
    void skyConfigApplyResultReceived(const QJsonObject& result);
    void serialPortDetectionResultReceived(const QJsonObject& result);

private slots:
    void onBytesReceived(const QByteArray& bytes);
    void onRetryTimer();

private:
    struct PendingCommand
    {
        CommandMessage command;
        QByteArray encodedPayload;
        int retry_count = 0;
        qint64 next_retry_ms = 0;
    };

    void dispatchFrame(const TelemetryFrame& frame);
    void sendCommandPayload(PendingCommand& pending);
    bool openLink(std::unique_ptr<TelemetryLink> link);
    void attachLinkSignals();
    void noteReceivedBytes(qint64 bytes);
    void noteTransmittedBytes(qint64 bytes);
    void publishTelemetryLog(LogLevel level,
                             const QString& category,
                             const QString& event,
                             const QString& message,
                             QVariantMap fields = QVariantMap());
    void reportProtocolDiagnostic(LogLevel level,
                                  const QString& category,
                                  const QString& event,
                                  const QString& message,
                                  const QVariantMap& fields = QVariantMap());

    std::unique_ptr<TelemetryLink> link_;
    TelemetryTransportType transport_type_ = TelemetryTransportType::Tcp;
    TelemetryCodec codec_;
    QTimer retry_timer_;
    quint64 link_generation_ = 0;
    quint16 next_frame_seq_ = 1;
    quint16 next_command_seq_ = 1;
    QHash<quint16, PendingCommand> pending_commands_;

    QVector<ByteSample> rx_byte_samples_;
    QVector<ByteSample> tx_byte_samples_;
    quint64 total_rx_bytes_ = 0;
    quint64 total_tx_bytes_ = 0;
    quint64 last_logged_crc_errors_ = 0;
    quint64 last_logged_dropped_frames_ = 0;
    qint64 last_decoder_diagnostic_ms_ = 0;
};

}  // namespace VaporView

#endif
