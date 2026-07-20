#pragma once

#include "TcpWaveEncoding.h"
#include "ground/session/SessionLoader.h"

#include <QVector>

#include <atomic>
#include <functional>
#include <memory>

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

struct SessionWaveformCatalog
{
    QString waveformPeakIndexFilename;
    QString rawTcpWaveFilename;
    QVector<SessionWaveformSegment> legacySegments;
    QVector<SessionRawTcpWaveFrame> rawTcpFrames;
    QVector<SessionIndexedWaveformFrame> indexedFrames;
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
