#pragma once

#include "FpgaWaveformAssembler.h"

#include <QByteArrayView>
#include <QJsonObject>
#include <chrono>

namespace VaporView::Session
{

inline quint64 harmonicReceptionTimeUs()
{
    return quint64(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

// waveform source 5, record type 2. All fields and samples are little endian.
inline constexpr quint32 kHarmonicHeaderSize = 48;
enum class HarmonicSampleType : quint16 { Int32 = 1, Float64 = 2 };
enum HarmonicQuality : quint32 {
    InvalidReference = 1, NoValidSamples = 2, IncompleteCycle = 4,
    Overflow = 8, ContinuityError = 16
};

struct HarmonicLayout
{
    quint16 channel = 0;
    HarmonicSampleType sampleType = HarmonicSampleType::Int32;
    quint32 cycle = 0, rate = 0, originalPoints = 0, storedPoints = 0;
    quint64 fpgaTick = 0, segment = 0;
    quint32 quality = 0;
    quint16 inputFormat = 0;
};

QJsonObject harmonicProcessingMetadata();
QByteArray encodeHarmonics(const FpgaWave::CompletedStream& stream, quint64 segment);
bool parseHarmonicLayout(QByteArrayView prefix, quint32 payloadSize, HarmonicLayout& layout);
// Components follow the viewer's existing choices: 0=H2, 1=H1, 6=H2/H1.
QVector<float> harmonicSamples(QByteArrayView payload, int component);
QByteArray harmonicFeatureCsv(quint64 hostTimeUs, quint64 waveformSequence, QByteArrayView payload);

} // namespace VaporView::Session
