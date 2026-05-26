#ifndef VaporView_GROUND_TELEMETRY_SERVICE_H_
#define VaporView_GROUND_TELEMETRY_SERVICE_H_

#include "SerialTelemetryLink.h"
#include "TelemetryCodec.h"

#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVector>

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
    void close();
    bool isOpen() const;
    quint64 linkGeneration() const;
    double receiveBitsPerSecond() const;
    double transmitBitsPerSecond() const;
    quint64 totalReceivedBytes() const;
    quint64 totalTransmittedBytes() const;

    quint16 sendCommand(CommandId commandId, const QByteArray& payload = QByteArray());
    quint16 sendDeviceCommand(CommandId commandId, SkyDeviceId deviceId);
    quint16 sendRateCommand(CommandId commandId, quint16 hz);
    quint16 sendPeakSearchRangeCommand(quint32 startIndex, quint32 endIndex);
    quint16 requestSkyConfig();
    quint16 setSkyConfig(const QJsonObject& config);
    quint16 saveSkyConfig();

signals:
    void linkOpenChanged(bool open);
    void logMessage(const QString& message);
    void basicTelemetryUpdated(const TelemetryBasic& data);
    void waveformUpdated(const DownsampledWaveform& waveform);
    void waveformFeatureUpdated(const WaveformFeature& feature);
    void statusUpdated(const TelemetryStatus& status);
    void commandAckReceived(const CommandAck& ack);
    void commandTimedOut(CommandId commandId, quint16 commandSeq);
    void skyConfigReceived(const QJsonObject& config);
    void skyConfigApplyResultReceived(const QJsonObject& result);

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
    void noteReceivedBytes(qint64 bytes);
    void noteTransmittedBytes(qint64 bytes);

    SerialTelemetryLink link_;
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
};

}  // namespace VaporView

#endif
