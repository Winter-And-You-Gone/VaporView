#pragma once

#include "TcpWaveEncoding.h"
#include "ground/session/SessionLoader.h"

#include <QVector>

#include <atomic>
#include <functional>
#include <memory>

class QTemporaryDir;

namespace VaporView::Ground
{

enum class SessionPeakFilterMode
{
    None,
    IqrOutlier,
    KeepRange,
    ExcludeRange
};

struct SessionPeakFilterSettings
{
    SessionPeakFilterMode mode = SessionPeakFilterMode::None;
    double minValue = 0.0;
    double maxValue = 0.0;
};

struct SessionWaveformSegment
{
    QString filename;
    quint64 startFrame = 0;
    quint64 frameCount = 0;
};

struct SessionRawTcpWaveFrame
{
    QString filename;
    quint64 harmonicPayloadOffset = 0;
    quint32 harmonicPayloadSize = 0;
    quint64 timestampUs = 0;
    TcpFloatEncoding floatEncoding = TcpFloatEncoding::Unknown;
};

struct SessionIndexedWaveformFrame
{
    QString filename;
    quint64 timestampUs = 0;
    quint32 pointCount = 0;
};

struct SessionFpgaWaveformChoice
{
    quint16 source = 0;
    int component = 0; // 0=H2/RAW, 1=H1, 2=I1, 3=Q1, 4=I2, 5=Q2
    QString label;
    QVector<quint64> timestampsUs;
    int pointsPerFrame = 0;
    QVector<SessionRawTcpWaveFrame> sampleFrames;
};

struct SessionWaveformCatalog
{
    QString sessionDirectory;
    QString waveformPeaksCsvFilename;
    QString waveformRawFilename;
    QVector<SessionWaveformSegment> legacySegments;
    QVector<SessionRawTcpWaveFrame> rawTcpFrames;
    QVector<SessionIndexedWaveformFrame> indexedFrames;
    QVector<quint64> fpgaTimestampsUs;
    quint16 fpgaSource = 0;
    int fpgaComponent = 0;
    QVector<SessionFpgaWaveformChoice> fpgaChoices;
    // Shared with catalog copies used by background peak calculations.
    std::shared_ptr<QTemporaryDir> fpgaSampleCache;
    int pointsPerFrame = 0;
    quint64 frameCount = 0;

    bool isEmpty() const;
    int sourceFileCount() const;
    QString sourceFilename(quint64 frameIndex) const;
};

struct SessionWaveformCatalogResult
{
    bool success = false;
    QString error;
    QString warning;
    SessionWaveformCatalog catalog;
};

struct SessionWaveformFrameResult
{
    bool success = false;
    QString error;
    quint64 timestampUs = 0;
    QVector<float> samples;
    QString sourceFilename;
};

struct SessionWaveformPeakSeriesResult
{
    bool success = false;
    bool cancelled = false;
    QString error;
    QVector<quint64> timestampsUs;
    QVector<float> peakValues;
};

class SessionWaveformRepository final
{
public:
    using ProgressCallback = std::function<void(quint64 completed, quint64 total)>;

    static SessionWaveformCatalogResult loadCatalog(
        const SessionMetadata& metadata,
        const ProgressCallback& progress = {});
    static SessionWaveformFrameResult readFrame(
        const SessionWaveformCatalog& catalog,
        quint64 frameIndex);
    static bool selectFpgaWaveform(SessionWaveformCatalog& catalog, int choice);

    static SessionWaveformPeakSeriesResult loadCachedPeakSeries(
        const SessionWaveformCatalog& catalog);
    static bool writeCachedPeakSeries(
        const SessionWaveformCatalog& catalog,
        const QVector<quint64>& timestampsUs,
        const QVector<float>& peakValues);
    static SessionWaveformPeakSeriesResult calculatePeakSeries(
        const SessionWaveformCatalog& catalog,
        int searchStartIndex,
        int searchEndIndex,
        const std::shared_ptr<std::atomic_bool>& cancelFlag = {},
        const ProgressCallback& progress = {});

    static QVector<float> applyPeakFilter(
        const QVector<float>& rawValues,
        const SessionPeakFilterSettings& settings);
    static float peakValue(
        const QVector<float>& samples,
        int searchStartIndex,
        int searchEndIndex);
    static bool isFullFramePeakSearch(int searchStartIndex, int searchEndIndex);
};

}  // namespace VaporView::Ground
