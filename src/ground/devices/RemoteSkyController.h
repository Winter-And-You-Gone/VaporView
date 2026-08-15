#pragma once

#include "ground/devices/GroundTelemetryService.h"
#include "ground/devices/RemoteTelemetryState.h"

#include <QHash>
#include <QObject>

namespace VaporView::Ground::Devices
{

enum class DeviceOperationSupport
{
    Unknown,
    Supported,
    Unsupported,
};

class RemoteSkyController final : public QObject
{
    Q_OBJECT

public:
    explicit RemoteSkyController(QObject *parent = nullptr);

    bool openSerial(const QString& port, int baud);
    bool open(const QString& port, int baud) { return openSerial(port, baud); }
    bool openTcp(const QString& host, quint16 port);
    void close();
    bool isOpen() const;
    quint64 linkGeneration() const;
    double receiveBitsPerSecond() const;
    double transmitBitsPerSecond() const;

    quint16 sendCommand(CommandId command, const QByteArray& payload = QByteArray());
    quint32 readAi8Page(Ai8TemperatureControllerProtocol::Page page,
                        const Ai8TemperatureControllerProtocol::Selection& selection);
    quint32 writeAi8Page(const Ai8TemperatureControllerProtocol::PageData& data);
    quint32 restoreAi8FactoryDefaults(Ai8TemperatureControllerProtocol::Page page,
                                      const Ai8TemperatureControllerProtocol::Selection& selection);
    quint32 configureEpsilonPacketRates(const EpsilonPacketRatesOperation& operation);
    quint32 configureEpsilonMainAntennaLeverArm(
        const EpsilonMainAntennaLeverArmOperation& operation);
    quint32 configureEpsilonRtcmInput(const EpsilonRtcmInputOperation& operation);
    bool sendRtcmCorrectionData(const QByteArray& data);
    DeviceOperationSupport deviceOperationSupport() const;
    quint16 sendDeviceCommand(CommandId command, SkyDeviceId device);
    quint16 sendRateCommand(CommandId command, quint16 rateHz);
    quint16 sendPeakSearchRangeCommand(quint32 startIndex, quint32 endIndex);
    quint16 requestSkyConfig();
    GroundTelemetryService *telemetryService();

    void resetState();
    void reset() { resetState(); }
    void markLinkClosed();
    void setDeviceState(SkyDeviceId device, DeviceState state);
    void noteDeviceData(SkyDeviceId device, qint64 nowMs);
    void clearDeviceData(SkyDeviceId device);
    void noteStatus(qint64 nowMs);
    void notePacket(MsgType type, qint64 nowMs);
    void noteWaveformPacket(quint16 channelId, qint64 nowMs);
    DeviceState deviceState(SkyDeviceId device) const;
    bool statusFresh(qint64 nowMs, qint64 timeoutMs = 3000) const;
    qint64 lastStatusMs() const;
    bool deviceDataFresh(SkyDeviceId device, qint64 nowMs, qint64 timeoutMs) const;
    qint64 lastDeviceDataMs(SkyDeviceId device) const;
    double packetRate(MsgType type) const;
    double waveformPacketRate(quint16 channelId) const;

signals:
    void linkOpenChanged(bool open);
    void basicTelemetryUpdated(const TelemetryBasic& telemetry);
    void waveformUpdated(const DownsampledWaveform& waveform);
    void waveformFeatureUpdated(const WaveformFeature& feature);
    void statusUpdated(const TelemetryStatus& status);
    void temperatureControllerStatusUpdated(const TemperatureControllerData& data);
    void ai8TemperatureControllerStatusUpdated(const Ai8TemperatureControllerProtocol::LiveData& data);
    void deviceOperationResponseReceived(const DeviceOperationResponse& response);
    void deviceOperationRejected(quint32 requestId, const CommandAck& ack);
    void deviceOperationTimedOut(quint32 requestId);
    void deviceOperationSupportChanged(DeviceOperationSupport support);
    void commandAckReceived(const CommandAck& ack);
    void commandTimedOut(CommandId command, quint16 sequence);

private:
    bool isCurrentEvent(quint64 generation) const;
    bool isCurrentOpenEvent(quint64 generation) const;
    void updateBasicState(const TelemetryBasic& telemetry);
    void updateStatusState(const TelemetryStatus& status);
    quint32 sendDeviceOperation(SkyDeviceId device,
                                DeviceOperation operation,
                                const QByteArray& payload);
    quint32 sendAi8Operation(DeviceOperation operation,
                             const Ai8TemperatureControllerProtocol::PageData& data);

    GroundTelemetryService service_;
    RemoteTelemetryState state_;
    QHash<quint16, quint32> device_operation_requests_;
    QHash<quint32, quint16> device_operation_commands_;
    quint32 next_device_operation_request_id_ = 1;
    DeviceOperationSupport device_operation_support_ = DeviceOperationSupport::Unknown;
};

}  // namespace VaporView::Ground::Devices
