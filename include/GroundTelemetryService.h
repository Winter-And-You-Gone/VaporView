#ifndef VaporView_GROUND_TELEMETRY_SERVICE_H_
#define VaporView_GROUND_TELEMETRY_SERVICE_H_

#include "SerialTelemetryLink.h"
#include "TelemetryCodec.h"

#include <QHash>
#include <QObject>
#include <QTimer>

namespace VaporView
{

class GroundTelemetryService : public QObject
{
    Q_OBJECT

public:
    explicit GroundTelemetryService(QObject *parent = nullptr);

    bool open(const QString& portName, int baudRate);
    void close();
    bool isOpen() const;

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

    SerialTelemetryLink link_;
    TelemetryCodec codec_;
    QTimer retry_timer_;
    quint16 next_frame_seq_ = 1;
    quint16 next_command_seq_ = 1;
    QHash<quint16, PendingCommand> pending_commands_;
};

}  // namespace VaporView

#endif
