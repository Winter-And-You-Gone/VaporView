#ifndef VaporView_TCP_WAVE_ENCODING_H_
#define VaporView_TCP_WAVE_ENCODING_H_

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace VaporView
{

enum class TcpFloatEncoding : quint8
{
    Unknown = 0,
    LittleEndian = 1,
    BigEndian = 2,
    WordSwappedLittleEndian = 3,
};

enum class TcpWavePayloadOrder : quint8
{
    Unknown = 0,
    RawThenHarmonic = 1,
    HarmonicThenRaw = 2,
};

struct TcpWavePayloadOrderAnalysis
{
    TcpWavePayloadOrder order = TcpWavePayloadOrder::Unknown;
    double first_roughness = 0.0;
    double second_roughness = 0.0;
    double confidence = 0.0;
};

constexpr quint32 kTcpWaveFloatEncodingFlagShift = 8u;
constexpr quint32 kTcpWaveFloatEncodingFlagMask = 0x00000300u;

quint32 tcpFloatEncodingToRawDatFlags(TcpFloatEncoding encoding);
TcpFloatEncoding tcpFloatEncodingFromRawDatFlags(quint32 flags);
QString tcpFloatEncodingLabel(bool english, TcpFloatEncoding encoding);

float decodeTcpFloatSample(const char *raw, TcpFloatEncoding encoding);
TcpFloatEncoding autoDetectTcpFloatEncoding(const QByteArray& payload);
QVector<float> decodeTcpFloatPayload(const QByteArray& payload, TcpFloatEncoding encoding);
TcpWavePayloadOrderAnalysis analyzeTcpWavePayloadOrder(const QByteArray& firstPayload,
                                                       const QByteArray& secondPayload,
                                                       TcpFloatEncoding encoding);

}  // namespace VaporView

Q_DECLARE_METATYPE(VaporView::TcpFloatEncoding)

#endif
