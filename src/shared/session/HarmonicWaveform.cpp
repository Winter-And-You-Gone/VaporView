#include "shared/session/HarmonicWaveform.h"
#include "shared/session/UnifiedRawDat.h"
#include "FpgaHarmonics.h"

#include <QStringList>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace VaporView::Session
{
namespace
{
template<class T> void append(QByteArray& bytes, T value)
{
    const auto offset = bytes.size();
    bytes.resize(offset + sizeof(T));
    qToLittleEndian<T>(value, bytes.data() + offset);
}
void appendDouble(QByteArray& bytes, double value)
{
    quint64 bits;
    std::memcpy(&bits, &value, sizeof(bits));
    append(bytes, bits);
}
double sample(const char *bytes, HarmonicSampleType type)
{
    if (type == HarmonicSampleType::Int32) return qFromLittleEndian<qint32>(bytes);
    const auto bits = qFromLittleEndian<quint64>(bytes);
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
QString number(double value)
{
    return std::isfinite(value) ? QString::number(value, 'g', 17) : QString();
}
}

QJsonObject harmonicProcessingMetadata()
{
    return {{"algorithm", "pointwise_h2_over_h1"}, {"algorithm_version", 1},
        {"invalid_reference", "nonfinite_or_h1_le_zero"}, {"search_start_index", 0},
        {"search_end_index", 0}, {"search_end_semantics", "exclusive_zero_means_frame_end"},
        {"normalized_waveform_stored", false}, {"raw_forwarded_packets_stored", false},
        {"iq_only_magnitudes", "hypot_float64"}, {"fpga_tick_hz", "100000000"},
        {"quality_flags", QJsonObject{{"invalid_reference", 1}, {"no_valid_samples", 2},
            {"incomplete_cycle", 4}, {"overflow", 8}, {"continuity_error", 16}}}};
}

QByteArray encodeHarmonics(const FpgaWave::CompletedStream& s, quint64 segment)
{
    if ((s.source != 0x30 && s.source != 0x31) || s.preview || s.format > 2 ||
        !s.totalPoints || quint64(s.totalPoints) * 16 + kHarmonicHeaderSize > SessionRawDat::kMaxPayloadSize)
        return {};
    quint32 quality = 0;
    if (!s.complete || s.partial || s.dliaPoints.size() != s.totalPoints) quality |= IncompleteCycle;
    if (s.overflow) quality |= Overflow;
    if (s.continuityError) quality |= ContinuityError;
    const quint32 count = quality ? 0 : s.totalPoints;
    const auto type = s.format == 1 ? HarmonicSampleType::Float64 : HarmonicSampleType::Int32;
    QByteArray bytes;
    bytes.reserve(kHarmonicHeaderSize + qsizetype(count) * (type == HarmonicSampleType::Int32 ? 8 : 16));
    append<quint16>(bytes, 1); append<quint16>(bytes, kHarmonicHeaderSize);
    append(bytes, s.source); append(bytes, quint16(type));
    append(bytes, s.cycleId); append(bytes, s.rate); append(bytes, s.totalPoints); append(bytes, count);
    append(bytes, s.timestamp); append(bytes, segment); append(bytes, quality);
    append(bytes, s.format); append<quint16>(bytes, 0);
    for (quint32 i = 0; i < count; ++i) {
        const auto& p = s.dliaPoints[i];
        if ((type == HarmonicSampleType::Int32 && !p.hasHarmonics) ||
            (type == HarmonicSampleType::Float64 && !p.hasIq)) return {};
        if (type == HarmonicSampleType::Int32) { append(bytes, p.h1); append(bytes, p.h2); }
        else { appendDouble(bytes, std::hypot(double(p.i1), double(p.q1)));
               appendDouble(bytes, std::hypot(double(p.i2), double(p.q2))); }
    }
    return bytes;
}

bool parseHarmonicLayout(QByteArrayView bytes, quint32 payloadSize, HarmonicLayout& l)
{
    if (bytes.size() < kHarmonicHeaderSize || payloadSize > SessionRawDat::kMaxPayloadSize ||
        qFromLittleEndian<quint16>(bytes.data()) != 1 ||
        qFromLittleEndian<quint16>(bytes.data() + 2) != kHarmonicHeaderSize ||
        qFromLittleEndian<quint16>(bytes.data() + 46) != 0) return false;
    l.channel = qFromLittleEndian<quint16>(bytes.data() + 4);
    l.sampleType = HarmonicSampleType(qFromLittleEndian<quint16>(bytes.data() + 6));
    l.cycle = qFromLittleEndian<quint32>(bytes.data() + 8);
    l.rate = qFromLittleEndian<quint32>(bytes.data() + 12);
    l.originalPoints = qFromLittleEndian<quint32>(bytes.data() + 16);
    l.storedPoints = qFromLittleEndian<quint32>(bytes.data() + 20);
    l.fpgaTick = qFromLittleEndian<quint64>(bytes.data() + 24);
    l.segment = qFromLittleEndian<quint64>(bytes.data() + 32);
    l.quality = qFromLittleEndian<quint32>(bytes.data() + 40);
    l.inputFormat = qFromLittleEndian<quint16>(bytes.data() + 44);
    const quint32 width = l.sampleType == HarmonicSampleType::Int32 ? 8 : 16;
    const bool invalidCycle = l.quality & (IncompleteCycle | Overflow | ContinuityError);
    return (l.channel == 0x30 || l.channel == 0x31) && l.inputFormat <= 2 &&
        l.sampleType == (l.inputFormat == 1 ? HarmonicSampleType::Float64 : HarmonicSampleType::Int32) &&
        !(l.quality & ~quint32(31)) && l.originalPoints > 0 &&
        quint64(l.originalPoints) * 16 + kHarmonicHeaderSize <= SessionRawDat::kMaxPayloadSize &&
        (invalidCycle ? l.storedPoints == 0 : l.storedPoints == l.originalPoints) &&
        quint64(kHarmonicHeaderSize) + quint64(l.storedPoints) * width == payloadSize;
}

QVector<float> harmonicSamples(QByteArrayView payload, int component)
{
    HarmonicLayout l;
    if (!parseHarmonicLayout(payload, quint32(payload.size()), l) ||
        (component != 0 && component != 1 && component != 6)) return {};
    QVector<float> values(l.originalPoints, std::numeric_limits<float>::quiet_NaN());
    const int width = l.sampleType == HarmonicSampleType::Int32 ? 4 : 8;
    for (quint32 i = 0; i < l.storedPoints; ++i) {
        const char *p = payload.data() + kHarmonicHeaderSize + qsizetype(i) * width * 2;
        const double h1 = sample(p, l.sampleType), h2 = sample(p + width, l.sampleType);
        values[i] = float(component == 1 ? h1 : component == 0 ? h2 : FpgaWave::normalizedSecondHarmonic(h1, h2));
    }
    return values;
}

QByteArray harmonicFeatureCsv(quint64 host, quint64 sequence, QByteArrayView payload)
{
    HarmonicLayout l;
    if (!parseHarmonicLayout(payload, quint32(payload.size()), l)) return {};
    double peak = -std::numeric_limits<double>::infinity(), minimum = -peak, sum = 0, squares = 0;
    qint64 peakIndex = -1;
    quint32 valid = 0, quality = l.quality;
    const int width = l.sampleType == HarmonicSampleType::Int32 ? 4 : 8;
    for (quint32 i = 0; i < l.storedPoints; ++i) {
        const char *p = payload.data() + kHarmonicHeaderSize + qsizetype(i) * width * 2;
        const double value = FpgaWave::normalizedSecondHarmonic(sample(p, l.sampleType), sample(p + width, l.sampleType));
        if (!std::isfinite(value)) { quality |= InvalidReference; continue; }
        if (value > peak) { peak = value; peakIndex = i; }
        minimum = std::min(minimum, value); sum += value; squares += value * value; ++valid;
    }
    if (!valid) quality |= NoValidSamples;
    QStringList row{QString::number(host), "0", QString::number(l.originalPoints), "0", "0",
        QString::number(l.channel), number(peak), valid ? number(sum / valid) : QString(),
        valid ? number(std::sqrt(squares / valid)) : QString(), QString::number(peakIndex),
        peakIndex >= 0 ? QString::number(peakIndex) : QString(), number(minimum), number(peak),
        QString::number(quality), "fpga", "H2/H1", "1", QString::number(l.cycle),
        QString::number(l.segment), QString::number(l.fpgaTick), QString::number(sequence)};
    return (row.join(',') + '\n').toUtf8();
}
} // namespace VaporView::Session
