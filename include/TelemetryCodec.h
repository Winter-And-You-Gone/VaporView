#ifndef VaporView_TELEMETRY_CODEC_H_
#define VaporView_TELEMETRY_CODEC_H_

#include "Ai8TemperatureControllerProtocol.h"
#include "TelemetryTypes.h"
#include "data_types.h"
#include "LogRecord.h"

#include <QByteArray>
#include <QVector>

namespace VaporView
{

class TelemetryCodec
{
public:
    static constexpr quint8 kProtocolVersion = 1;
    static constexpr int kHeaderSizeWithoutSof = 1 + 1 + 1 + 2 + 8 + 4;
    static constexpr quint32 kDefaultMaxPayloadSize = 1024u * 1024u;

    explicit TelemetryCodec(quint32 maxPayloadSize = kDefaultMaxPayloadSize);

    QByteArray encodeFrame(MsgType type,
                           const QByteArray& payload,
                           quint16 seq,
                           quint64 timeUs,
                           quint8 flags = 0) const;

    QVector<TelemetryFrame> feedBytes(const QByteArray& bytes);
    void reset();

    quint64 crcErrorCount() const;
    quint64 droppedFrameCount() const;

    static quint16 crc16Ccitt(const char *data, qsizetype size);
    static QByteArray serializeBasicTelemetry(const TelemetryBasic& data);
    static bool parseBasicTelemetry(const QByteArray& payload, TelemetryBasic& data);
    static QByteArray serializeWaveformFeature(const WaveformFeature& feature);
    static bool parseWaveformFeature(const QByteArray& payload, WaveformFeature& feature);
    static QByteArray serializeDownsampledWaveform(const DownsampledWaveform& waveform);
    static bool parseDownsampledWaveform(const QByteArray& payload, DownsampledWaveform& waveform);
    static QByteArray serializeTelemetryStatus(const TelemetryStatus& status);
    static bool parseTelemetryStatus(const QByteArray& payload, TelemetryStatus& status);
    static QByteArray serializeCommand(const CommandMessage& command);
    static bool parseCommand(const QByteArray& payload, CommandMessage& command);
    static QByteArray serializeCommandAck(const CommandAck& ack);
    static bool parseCommandAck(const QByteArray& payload, CommandAck& ack);
    static QByteArray serializeDeviceCommand(SkyDeviceId id);
    static bool parseDeviceCommand(const QByteArray& payload, SkyDeviceId& id);
    static QByteArray serializeRatePayload(quint16 hz);
    static bool parseRatePayload(const QByteArray& payload, quint16& hz);
    static QByteArray serializePeakSearchRange(const PeakSearchRange& range);
    static bool parsePeakSearchRange(const QByteArray& payload, PeakSearchRange& range);
    static QByteArray serializeTemperatureControllerStatus(const TemperatureControllerData& data);
    static bool parseTemperatureControllerStatus(const QByteArray& payload, TemperatureControllerData& data);
    static QByteArray serializeAi8TemperatureControllerStatus(
        const Ai8TemperatureControllerProtocol::LiveData& data);
    static bool parseAi8TemperatureControllerStatus(
        const QByteArray& payload,
        Ai8TemperatureControllerProtocol::LiveData& data);
    static QByteArray serializeTemperatureControllerCommand(const TemperatureControllerCommand& command);
    static bool parseTemperatureControllerCommand(const QByteArray& payload, TemperatureControllerCommand& command);
    static QByteArray serializeAi8PageData(const Ai8TemperatureControllerProtocol::PageData& data);
    static bool parseAi8PageData(const QByteArray& payload,
                                 Ai8TemperatureControllerProtocol::PageData& data);
    static QByteArray serializeEpsilonPacketRatesOperation(
        const EpsilonPacketRatesOperation& operation);
    static bool parseEpsilonPacketRatesOperation(
        const QByteArray& payload,
        EpsilonPacketRatesOperation& operation);
    static QByteArray serializeEpsilonMainAntennaLeverArmOperation(
        const EpsilonMainAntennaLeverArmOperation& operation);
    static bool parseEpsilonMainAntennaLeverArmOperation(
        const QByteArray& payload,
        EpsilonMainAntennaLeverArmOperation& operation);
    static QByteArray serializeEpsilonRtcmInputOperation(
        const EpsilonRtcmInputOperation& operation);
    static bool parseEpsilonRtcmInputOperation(
        const QByteArray& payload,
        EpsilonRtcmInputOperation& operation);
    static QByteArray serializeRtcmCorrectionData(const QByteArray& data);
    static bool parseRtcmCorrectionData(const QByteArray& payload, QByteArray& data);
    static QByteArray serializeDeviceOperationRequest(const DeviceOperationRequest& request);
    static bool parseDeviceOperationRequest(const QByteArray& payload,
                                            DeviceOperationRequest& request);
    static QByteArray serializeDeviceOperationResponse(const DeviceOperationResponse& response);
    static bool parseDeviceOperationResponse(const QByteArray& payload,
                                             DeviceOperationResponse& response);
    static QByteArray serializeLogRecord(const LogRecord& record);
    static bool parseLogRecord(const QByteArray& payload, LogRecord& record);

private:
    quint32 max_payload_size_;
    QByteArray buffer_;
    quint64 crc_error_count_ = 0;
    quint64 dropped_frame_count_ = 0;
};

}  // namespace VaporView

#endif
