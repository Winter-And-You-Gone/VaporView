#ifndef VaporView_TELEMETRY_CODEC_H_
#define VaporView_TELEMETRY_CODEC_H_

#include "TelemetryTypes.h"

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

private:
    quint32 max_payload_size_;
    QByteArray buffer_;
    quint64 crc_error_count_ = 0;
    quint64 dropped_frame_count_ = 0;
};

}  // namespace VaporView

#endif
