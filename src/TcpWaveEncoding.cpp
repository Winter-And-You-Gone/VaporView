#include "TcpWaveEncoding.h"

#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace VaporView
{
namespace
{
constexpr int kFloatSize = 4;

quint32 tcpFloatEncodingCode(TcpFloatEncoding encoding)
{
    switch (encoding)
    {
    case TcpFloatEncoding::LittleEndian:
        return 1u;
    case TcpFloatEncoding::BigEndian:
        return 2u;
    case TcpFloatEncoding::WordSwappedLittleEndian:
        return 3u;
    case TcpFloatEncoding::Unknown:
    default:
        return 0u;
    }
}
}  // namespace

quint32 tcpFloatEncodingToRawDatFlags(TcpFloatEncoding encoding)
{
    return (tcpFloatEncodingCode(encoding) << kTcpWaveFloatEncodingFlagShift) & kTcpWaveFloatEncodingFlagMask;
}

TcpFloatEncoding tcpFloatEncodingFromRawDatFlags(quint32 flags)
{
    const quint32 code = (flags & kTcpWaveFloatEncodingFlagMask) >> kTcpWaveFloatEncodingFlagShift;
    switch (code)
    {
    case 1u:
        return TcpFloatEncoding::LittleEndian;
    case 2u:
        return TcpFloatEncoding::BigEndian;
    case 3u:
        return TcpFloatEncoding::WordSwappedLittleEndian;
    case 0u:
    default:
        return TcpFloatEncoding::Unknown;
    }
}

QString tcpFloatEncodingLabel(bool english, TcpFloatEncoding encoding)
{
    switch (encoding)
    {
    case TcpFloatEncoding::LittleEndian:
        return english ? "little-endian float32" : "小端 float32";
    case TcpFloatEncoding::BigEndian:
        return english ? "big-endian float32" : "大端 float32";
    case TcpFloatEncoding::WordSwappedLittleEndian:
        return english ? "word-swapped float32" : "16位字交换 float32";
    case TcpFloatEncoding::Unknown:
    default:
        return english ? "unknown float32" : "未知 float32";
    }
}

float decodeTcpFloatSample(const char *raw, TcpFloatEncoding encoding)
{
    quint32 bits = 0;
    switch (encoding)
    {
    case TcpFloatEncoding::LittleEndian:
        bits = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(raw));
        break;
    case TcpFloatEncoding::BigEndian:
        bits = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(raw));
        break;
    case TcpFloatEncoding::WordSwappedLittleEndian:
    {
        const uchar ordered[4] = {
            static_cast<uchar>(raw[0]),
            static_cast<uchar>(raw[1]),
            static_cast<uchar>(raw[3]),
            static_cast<uchar>(raw[2]),
        };
        bits = qFromLittleEndian<quint32>(ordered);
        break;
    }
    case TcpFloatEncoding::Unknown:
    default:
        bits = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(raw));
        break;
    }

    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(float));
    return value;
}

TcpFloatEncoding autoDetectTcpFloatEncoding(const QByteArray& payload)
{
    const TcpFloatEncoding candidates[] = {
        TcpFloatEncoding::LittleEndian,
        TcpFloatEncoding::BigEndian,
        TcpFloatEncoding::WordSwappedLittleEndian,
    };
    const int sampleCount = std::min(static_cast<int>(payload.size() / kFloatSize), 1024);
    double bestScore = -std::numeric_limits<double>::infinity();
    TcpFloatEncoding bestEncoding = TcpFloatEncoding::LittleEndian;

    for (TcpFloatEncoding encoding : candidates)
    {
        double score = 0.0;
        float previous = 0.0f;
        bool hasPrevious = false;
        for (int i = 0; i < sampleCount; ++i)
        {
            const float value = decodeTcpFloatSample(payload.constData() + i * kFloatSize, encoding);
            if (!std::isfinite(value))
            {
                score -= 1000.0;
                continue;
            }

            const double magnitude = std::fabs(static_cast<double>(value));
            score += 100.0;
            if (magnitude < 10.0)
            {
                score += 20.0;
            }
            else if (magnitude < 1000.0)
            {
                score += 5.0;
            }
            else if (magnitude > 1.0e6)
            {
                score -= 200.0;
            }

            if (hasPrevious)
            {
                const double delta = std::fabs(static_cast<double>(value) - static_cast<double>(previous));
                if (delta < 0.1)
                {
                    score += 5.0;
                }
                else if (delta < 1.0)
                {
                    score += 3.0;
                }
                else if (delta < 10.0)
                {
                    score += 1.0;
                }
                else if (delta > 1.0e4)
                {
                    score -= 25.0;
                }
            }

            previous = value;
            hasPrevious = true;
        }

        if (score > bestScore)
        {
            bestScore = score;
            bestEncoding = encoding;
        }
    }

    return bestEncoding;
}

QVector<float> decodeTcpFloatPayload(const QByteArray& payload, TcpFloatEncoding encoding)
{
    QVector<float> values;
    const int count = payload.size() / kFloatSize;
    values.resize(count);
    const TcpFloatEncoding effectiveEncoding = encoding == TcpFloatEncoding::Unknown
        ? TcpFloatEncoding::LittleEndian
        : encoding;
    for (int i = 0; i < count; ++i)
    {
        values[i] = decodeTcpFloatSample(payload.constData() + i * kFloatSize, effectiveEncoding);
    }
    return values;
}

}  // namespace VaporView
