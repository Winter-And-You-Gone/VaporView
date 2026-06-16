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
constexpr int kPayloadOrderMinimumSamples = 64;
constexpr int kPayloadOrderMaxSamples = 4096;
constexpr double kPayloadOrderMinimumRoughnessRatio = 10.0;

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

double percentileValue(QVector<double> values, double percentile)
{
    if (values.isEmpty())
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    std::sort(values.begin(), values.end());
    const double clampedPercentile = std::clamp(percentile, 0.0, 1.0);
    const double scaledIndex = clampedPercentile * static_cast<double>(values.size() - 1);
    const int lowerIndex = static_cast<int>(std::floor(scaledIndex));
    const int upperIndex = static_cast<int>(std::ceil(scaledIndex));
    if (lowerIndex == upperIndex)
    {
        return values.at(lowerIndex);
    }

    const double fraction = scaledIndex - static_cast<double>(lowerIndex);
    return values.at(lowerIndex) * (1.0 - fraction) + values.at(upperIndex) * fraction;
}

double meanValue(const QVector<double>& values)
{
    if (values.isEmpty())
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    double sum = 0.0;
    for (double value : values)
    {
        sum += value;
    }
    return sum / static_cast<double>(values.size());
}

double payloadRoughness(const QByteArray& payload, TcpFloatEncoding encoding)
{
    const int sampleCount = std::min(static_cast<int>(payload.size() / kFloatSize), kPayloadOrderMaxSamples);
    if (sampleCount < kPayloadOrderMinimumSamples)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    QVector<double> values;
    values.reserve(sampleCount);
    for (int i = 0; i < sampleCount; ++i)
    {
        const float decoded = decodeTcpFloatSample(payload.constData() + i * kFloatSize, encoding);
        if (std::isfinite(decoded))
        {
            values.push_back(static_cast<double>(decoded));
        }
    }

    if (values.size() < kPayloadOrderMinimumSamples)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double p05 = percentileValue(values, 0.05);
    const double p95 = percentileValue(values, 0.95);
    const auto [minIt, maxIt] = std::minmax_element(values.cbegin(), values.cend());
    const double robustRange = p95 - p05;
    const double fullRange = *maxIt - *minIt;
    const double scale = std::max({std::fabs(robustRange), std::fabs(fullRange) * 0.1, 1.0e-6});

    QVector<double> firstDiffs;
    QVector<double> secondDiffs;
    firstDiffs.reserve(values.size() - 1);
    secondDiffs.reserve(values.size() - 2);
    for (int i = 1; i < values.size(); ++i)
    {
        firstDiffs.push_back(std::fabs(values.at(i) - values.at(i - 1)));
    }
    for (int i = 2; i < values.size(); ++i)
    {
        secondDiffs.push_back(std::fabs(values.at(i) - 2.0 * values.at(i - 1) + values.at(i - 2)));
    }

    const double meanSecondDiff = meanValue(secondDiffs);
    const double meanFirstDiff = meanValue(firstDiffs);
    if (!std::isfinite(meanSecondDiff) || !std::isfinite(meanFirstDiff))
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return (meanSecondDiff + meanFirstDiff * 0.25) / scale;
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
        int finiteCount = 0;
        int tinyCount = 0;
        double minValue = std::numeric_limits<double>::infinity();
        double maxValue = -std::numeric_limits<double>::infinity();
        double maxMagnitude = 0.0;
        for (int i = 0; i < sampleCount; ++i)
        {
            const float value = decodeTcpFloatSample(payload.constData() + i * kFloatSize, encoding);
            if (!std::isfinite(value))
            {
                score -= 1000.0;
                continue;
            }

            const double magnitude = std::fabs(static_cast<double>(value));
            ++finiteCount;
            if (magnitude < 1.0e-20)
            {
                ++tinyCount;
            }
            minValue = std::min(minValue, static_cast<double>(value));
            maxValue = std::max(maxValue, static_cast<double>(value));
            maxMagnitude = std::max(maxMagnitude, magnitude);
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

        if (finiteCount > 0)
        {
            const double dynamicRange = maxValue - minValue;
            const double tinyRatio = static_cast<double>(tinyCount) / static_cast<double>(finiteCount);
            if (tinyRatio > 0.9 && maxMagnitude < 1.0e-12)
            {
                score -= 200.0 * finiteCount;
            }
            if (dynamicRange > 1.0e-6)
            {
                score += 30.0 * finiteCount;
            }
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

TcpWavePayloadOrderAnalysis analyzeTcpWavePayloadOrder(const QByteArray& firstPayload,
                                                       const QByteArray& secondPayload,
                                                       TcpFloatEncoding encoding)
{
    const TcpFloatEncoding effectiveEncoding = encoding == TcpFloatEncoding::Unknown
        ? autoDetectTcpFloatEncoding(firstPayload.size() >= secondPayload.size() ? firstPayload : secondPayload)
        : encoding;
    TcpWavePayloadOrderAnalysis analysis;
    analysis.first_roughness = payloadRoughness(firstPayload, effectiveEncoding);
    analysis.second_roughness = payloadRoughness(secondPayload, effectiveEncoding);

    if (!std::isfinite(analysis.first_roughness) ||
        !std::isfinite(analysis.second_roughness) ||
        analysis.first_roughness <= 0.0 ||
        analysis.second_roughness <= 0.0)
    {
        return analysis;
    }

    const double rougher = std::max(analysis.first_roughness, analysis.second_roughness);
    const double smoother = std::min(analysis.first_roughness, analysis.second_roughness);
    analysis.confidence = rougher / smoother;
    if (analysis.confidence < kPayloadOrderMinimumRoughnessRatio)
    {
        return analysis;
    }

    analysis.order = analysis.first_roughness > analysis.second_roughness
        ? TcpWavePayloadOrder::RawThenHarmonic
        : TcpWavePayloadOrder::HarmonicThenRaw;
    return analysis;
}

}  // namespace VaporView
