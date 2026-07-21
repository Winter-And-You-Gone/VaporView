#include "ground/session/SessionWaveformRepository.h"

#include "ground/session/SessionCsv.h"
#include "shared/session/UnifiedRawDat.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringConverter>
#include <QTextStream>
#include <QThread>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <future>
#include <limits>
#include <utility>
#include <vector>

namespace
{

using VaporView::Ground::SessionCsv::csvValueAt;
using VaporView::Ground::SessionCsv::findHeaderIndex;
using VaporView::Ground::SessionCsv::parseCsvLine;

constexpr quint64 kTimestampBytes = sizeof(quint64);
constexpr quint64 kFloatBytes = sizeof(float);
constexpr qsizetype kPeakPayloadChunkBytes = 32 * 1024 * 1024;
struct PeakPayload
{
    quint64 timestampUs = 0;
    QByteArray payload;
    VaporView::TcpFloatEncoding encoding = VaporView::TcpFloatEncoding::LittleEndian;
};

bool isCancelled(const std::shared_ptr<std::atomic_bool>& cancelFlag)
{
    return cancelFlag && cancelFlag->load(std::memory_order_relaxed);
}

std::pair<int, int> peakSearchRange(int sampleCount, int searchStartIndex, int searchEndIndex)
{
    if (sampleCount <= 0)
    {
        return {0, 0};
    }
    const int startIndex = std::clamp(searchStartIndex, 0, sampleCount);
    const int endIndex = searchEndIndex <= 0
        ? sampleCount
        : std::clamp(searchEndIndex, 0, sampleCount);
    return {startIndex, std::max(startIndex, endIndex)};
}

bool readPeakPayload(QFile& file,
                     quint64 samplePayloadOffset,
                     int sampleCount,
                     int searchStartIndex,
                     int searchEndIndex,
                     QByteArray& payload)
{
    payload.clear();
    const auto [startIndex, endIndex] = peakSearchRange(
        sampleCount,
        searchStartIndex,
        searchEndIndex);
    if (startIndex >= endIndex)
    {
        return true;
    }

    const quint64 byteOffset = samplePayloadOffset + static_cast<quint64>(startIndex) * kFloatBytes;
    const quint64 byteCount = static_cast<quint64>(endIndex - startIndex) * kFloatBytes;
    if (byteOffset > static_cast<quint64>(std::numeric_limits<qint64>::max()) ||
        byteCount > static_cast<quint64>(std::numeric_limits<qint64>::max()) ||
        !file.seek(static_cast<qint64>(byteOffset)))
    {
        return false;
    }
    payload = file.read(static_cast<qint64>(byteCount));
    return payload.size() == static_cast<qsizetype>(byteCount);
}

float peakFromPayload(const QByteArray& payload, VaporView::TcpFloatEncoding encoding)
{
    if (payload.isEmpty() || payload.size() % static_cast<qsizetype>(kFloatBytes) != 0)
    {
        return std::numeric_limits<float>::quiet_NaN();
    }

    const VaporView::TcpFloatEncoding effectiveEncoding =
        encoding == VaporView::TcpFloatEncoding::Unknown
            ? VaporView::autoDetectTcpFloatEncoding(payload)
            : encoding;
    bool hasPeak = false;
    float peak = std::numeric_limits<float>::lowest();
    const int sampleCount = static_cast<int>(payload.size() / static_cast<qsizetype>(kFloatBytes));
    for (int index = 0; index < sampleCount; ++index)
    {
        const float value = VaporView::decodeTcpFloatSample(
            payload.constData() + index * static_cast<int>(kFloatBytes),
            effectiveEncoding);
        if (!std::isfinite(value))
        {
            continue;
        }
        hasPeak = true;
        peak = std::max(peak, value);
    }
    return hasPeak ? peak : std::numeric_limits<float>::quiet_NaN();
}

void appendPeakPayloads(const QVector<PeakPayload>& payloads,
                        VaporView::Ground::SessionWaveformPeakSeriesResult& result,
                        const std::shared_ptr<std::atomic_bool>& cancelFlag)
{
    if (payloads.isEmpty() || isCancelled(cancelFlag))
    {
        return;
    }

    QVector<float> peaks(payloads.size());
    const int workerCount = std::clamp(
        std::max(1, QThread::idealThreadCount()),
        1,
        static_cast<int>(payloads.size()));
    const int blockSize = (static_cast<int>(payloads.size()) + workerCount - 1) / workerCount;
    std::vector<std::future<void>> futures;
    futures.reserve(static_cast<size_t>(workerCount));
    for (int worker = 0; worker < workerCount; ++worker)
    {
        const int begin = worker * blockSize;
        const int end = std::min(static_cast<int>(payloads.size()), begin + blockSize);
        if (begin >= end)
        {
            continue;
        }
        futures.emplace_back(std::async(std::launch::async, [&payloads, &peaks, cancelFlag, begin, end]() {
            for (int index = begin; index < end && !isCancelled(cancelFlag); ++index)
            {
                peaks[index] = peakFromPayload(payloads.at(index).payload, payloads.at(index).encoding);
            }
        }));
    }
    for (std::future<void>& future : futures)
    {
        future.get();
    }
    if (isCancelled(cancelFlag))
    {
        return;
    }

    result.timestampsUs.reserve(result.timestampsUs.size() + payloads.size());
    result.peakValues.reserve(result.peakValues.size() + payloads.size());
    for (int index = 0; index < payloads.size(); ++index)
    {
        result.timestampsUs.push_back(payloads.at(index).timestampUs);
        result.peakValues.push_back(peaks.at(index));
    }
}

double percentile(QVector<double> values, double fraction)
{
    if (values.isEmpty())
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    std::sort(values.begin(), values.end());
    const double scaledIndex = std::clamp(fraction, 0.0, 1.0) * (values.size() - 1);
    const int lower = static_cast<int>(std::floor(scaledIndex));
    const int upper = static_cast<int>(std::ceil(scaledIndex));
    if (lower == upper)
    {
        return values.at(lower);
    }
    const double ratio = scaledIndex - lower;
    return values.at(lower) * (1.0 - ratio) + values.at(upper) * ratio;
}

enum class UnifiedRawCatalogStatus
{
    Missing,
    NotUnified,
    Loaded,
    Error
};

struct UnifiedRawCatalogResult
{
    UnifiedRawCatalogStatus status = UnifiedRawCatalogStatus::Missing;
    QString error;
    QString warning;
};

UnifiedRawCatalogResult loadUnifiedRawCatalog(
    const QString& filename,
    VaporView::Ground::SessionWaveformCatalog& catalog,
    const VaporView::Ground::SessionWaveformRepository::ProgressCallback& progress)
{
    UnifiedRawCatalogResult result;
    if (filename.isEmpty() || !QFileInfo::exists(filename))
    {
        return result;
    }
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly))
    {
        result.status = UnifiedRawCatalogStatus::Error;
        result.error = QStringLiteral("Failed to open raw TCP wave file: %1").arg(filename);
        return result;
    }

    VaporView::SessionRawDat::RawScanOptions scanOptions;
    scanOptions.expectedSourceId = VaporView::SessionRawDat::kSourceWaveform;
    scanOptions.progress = progress;
    const VaporView::SessionRawDat::RawScanResult scanResult =
        VaporView::SessionRawDat::scan(file, scanOptions);
    if (scanResult.status == VaporView::SessionRawDat::RawReadStatus::NotUnifiedFormat)
    {
        result.status = UnifiedRawCatalogStatus::NotUnified;
        result.error = QStringLiteral("Raw TCP wave file is not unified raw DAT: %1").arg(filename);
        return result;
    }
    if (!scanResult.success())
    {
        result.status = UnifiedRawCatalogStatus::Error;
        result.error = QStringLiteral("%1: %2").arg(scanResult.error, filename);
        return result;
    }
    result.status = UnifiedRawCatalogStatus::Loaded;
    result.warning = scanResult.warning;

    for (const VaporView::SessionRawDat::RawRecordIndex& record : scanResult.records)
    {
        const auto& header = record.header;
        if ((header.flags & VaporView::SessionRawDat::kWaveformCombinedPayloadFlag) != 0 &&
            header.payloadSize >= sizeof(quint32) * 2)
        {
            if (record.payloadOffset > static_cast<quint64>(std::numeric_limits<qint64>::max()) ||
                !file.seek(static_cast<qint64>(record.payloadOffset)))
            {
                result.status = UnifiedRawCatalogStatus::Error;
                result.error = QStringLiteral("Failed to seek raw TCP wave payload: %1").arg(filename);
                return result;
            }
            const QByteArray prefix = file.read(VaporView::SessionRawDat::kWaveformPayloadPrefixSize);
            VaporView::SessionRawDat::WaveformPayloadLayout layout;
            QString layoutError;
            if (!VaporView::SessionRawDat::parseWaveformPayloadLayout(
                    prefix,
                    header.payloadSize,
                    &layout,
                    &layoutError))
            {
                result.status = UnifiedRawCatalogStatus::Error;
                result.error = QStringLiteral("Invalid raw TCP wave sub-payload sizes at offset %1: %2 (%3)")
                                   .arg(record.recordOffset)
                                   .arg(filename)
                                   .arg(layoutError);
                return result;
            }
            if (layout.harmonicSize > 0 && layout.harmonicSize % kFloatBytes != 0)
            {
                result.status = UnifiedRawCatalogStatus::Error;
                result.error = QStringLiteral("Invalid raw TCP wave harmonic payload size %1 at offset %2: %3")
                                   .arg(layout.harmonicSize)
                                   .arg(record.recordOffset)
                                   .arg(filename);
                return result;
            }
            if (layout.harmonicSize > 0)
            {
                VaporView::Ground::SessionRawTcpWaveFrame frame;
                frame.filename = filename;
                frame.harmonicPayloadOffset = record.payloadOffset + layout.harmonicOffset;
                frame.harmonicPayloadSize = layout.harmonicSize;
                frame.timestampUs = header.hostTimestampUs;
                frame.floatEncoding = scanResult.fileHeader.version == 1u
                    ? VaporView::TcpFloatEncoding::Unknown
                    : VaporView::tcpFloatEncodingFromRawDatFlags(header.flags);
                catalog.rawTcpFrames.push_back(std::move(frame));
                if (catalog.pointsPerFrame <= 0)
                {
                    catalog.pointsPerFrame = static_cast<int>(layout.harmonicSize / kFloatBytes);
                }
            }
        }
    }
    return result;
}

bool loadIndexedCatalog(const VaporView::Ground::SessionMetadata& metadata,
                        VaporView::Ground::SessionWaveformCatalog& catalog,
                        QString& error)
{
    if (metadata.waveformIndexFilename.isEmpty() ||
        !QFileInfo::exists(metadata.waveformIndexFilename))
    {
        return true;
    }
    QFile file(metadata.waveformIndexFilename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        error = QStringLiteral("Failed to open waveform index: %1")
                    .arg(metadata.waveformIndexFilename);
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    if (stream.atEnd())
    {
        return true;
    }
    const QStringList headers = parseCsvLine(stream.readLine());
    const int hostTimeIndex = findHeaderIndex(headers, {QStringLiteral("host_time_us")});
    const int epsilonTimeIndex = findHeaderIndex(headers, {QStringLiteral("epsilon_time_us")});
    const int pointCountIndex = findHeaderIndex(headers, {QStringLiteral("point_count")});
    const int filenameIndex = findHeaderIndex(headers, {QStringLiteral("filename")});
    if ((hostTimeIndex < 0 && epsilonTimeIndex < 0) || pointCountIndex < 0 || filenameIndex < 0)
    {
        error = QStringLiteral("Invalid waveform index header: %1")
                    .arg(metadata.waveformIndexFilename);
        return false;
    }

    const QDir sessionDir(metadata.sessionDirectory);
    while (!stream.atEnd())
    {
        const QString line = stream.readLine();
        if (line.trimmed().isEmpty())
        {
            continue;
        }
        const QStringList fields = parseCsvLine(line);
        bool timeOk = false;
        bool countOk = false;
        const quint64 timestampUs = csvValueAt(
            fields,
            hostTimeIndex >= 0 ? hostTimeIndex : epsilonTimeIndex).toULongLong(&timeOk);
        const quint32 pointCount = csvValueAt(fields, pointCountIndex).toUInt(&countOk);
        const QString relativeFilename = csvValueAt(fields, filenameIndex).trimmed();
        if (!timeOk || !countOk || pointCount == 0 || relativeFilename.isEmpty())
        {
            continue;
        }

        const QString absolutePath = sessionDir.filePath(relativeFilename);
        const QFileInfo info(absolutePath);
        const quint64 expectedBytes = static_cast<quint64>(pointCount) * kFloatBytes;
        if (!info.exists() || info.size() < 0 || static_cast<quint64>(info.size()) < expectedBytes)
        {
            continue;
        }
        catalog.indexedFrames.push_back({absolutePath, timestampUs, pointCount});
    }
    if (!catalog.indexedFrames.isEmpty() && catalog.pointsPerFrame <= 0)
    {
        catalog.pointsPerFrame = static_cast<int>(catalog.indexedFrames.first().pointCount);
    }
    return true;
}

void loadLegacyCatalog(const VaporView::Ground::SessionMetadata& metadata,
                       VaporView::Ground::SessionWaveformCatalog& catalog,
                       const VaporView::Ground::SessionWaveformRepository::ProgressCallback& progress)
{
    QDir directory(metadata.waveformDirectory);
    if (!directory.exists() || catalog.pointsPerFrame <= 0)
    {
        return;
    }
    const QStringList files = directory.entryList(
        {QStringLiteral("*.dat")},
        QDir::Files,
        QDir::Name);
    const quint64 frameBytes = kTimestampBytes +
        static_cast<quint64>(catalog.pointsPerFrame) * kFloatBytes;
    for (int fileIndex = 0; fileIndex < files.size(); ++fileIndex)
    {
        const QString absolutePath = directory.filePath(files.at(fileIndex));
        const QFileInfo info(absolutePath);
        if (frameBytes == 0 || info.size() < static_cast<qint64>(frameBytes))
        {
            continue;
        }
        const quint64 frameCount = static_cast<quint64>(info.size()) / frameBytes;
        if (frameCount == 0)
        {
            continue;
        }
        catalog.legacySegments.push_back({absolutePath, catalog.frameCount, frameCount});
        catalog.frameCount += frameCount;
        if (progress && (fileIndex + 1) % 20 == 0)
        {
            progress(static_cast<quint64>(fileIndex + 1), static_cast<quint64>(files.size()));
        }
    }
}

}  // namespace

namespace VaporView::Ground
{

bool SessionWaveformCatalog::isEmpty() const
{
    return rawTcpFrames.isEmpty() && indexedFrames.isEmpty() && legacySegments.isEmpty();
}

int SessionWaveformCatalog::sourceFileCount() const
{
    if (!rawTcpFrames.isEmpty())
    {
        return 1;
    }
    return !indexedFrames.isEmpty() ? indexedFrames.size() : legacySegments.size();
}

QString SessionWaveformCatalog::sourceFilename(quint64 frameIndex) const
{
    if (!rawTcpFrames.isEmpty())
    {
        return frameIndex < static_cast<quint64>(rawTcpFrames.size())
            ? rawTcpFrames.at(static_cast<int>(frameIndex)).filename
            : QString();
    }
    if (!indexedFrames.isEmpty())
    {
        return frameIndex < static_cast<quint64>(indexedFrames.size())
            ? indexedFrames.at(static_cast<int>(frameIndex)).filename
            : QString();
    }
    const auto it = std::find_if(
        legacySegments.cbegin(),
        legacySegments.cend(),
        [frameIndex](const SessionWaveformSegment& segment) {
            return frameIndex >= segment.startFrame &&
                frameIndex < segment.startFrame + segment.frameCount;
        });
    return it == legacySegments.cend() ? QString() : it->filename;
}

SessionWaveformCatalogResult SessionWaveformRepository::loadCatalog(
    const SessionMetadata& metadata,
    const ProgressCallback& progress)
{
    SessionWaveformCatalogResult result;
    result.catalog.waveformPeaksCsvFilename = metadata.waveformPeaksCsvFilename;
    result.catalog.waveformRawFilename = metadata.waveformRawFilename;
    result.catalog.pointsPerFrame = metadata.waveformPointsPerFrame;

    const UnifiedRawCatalogResult rawResult = loadUnifiedRawCatalog(
        metadata.waveformRawFilename,
        result.catalog,
        progress);
    if (rawResult.status == UnifiedRawCatalogStatus::Loaded)
    {
        result.catalog.frameCount = static_cast<quint64>(result.catalog.rawTcpFrames.size());
        result.success = true;
        result.warning = rawResult.warning;
        return result;
    }
    if (rawResult.status == UnifiedRawCatalogStatus::Error)
    {
        result.error = rawResult.error;
        return result;
    }
    result.catalog.rawTcpFrames.clear();

    QString indexError;
    if (loadIndexedCatalog(metadata, result.catalog, indexError) &&
        !result.catalog.indexedFrames.isEmpty())
    {
        result.catalog.frameCount = static_cast<quint64>(result.catalog.indexedFrames.size());
        result.success = true;
        return result;
    }
    result.catalog.indexedFrames.clear();

    loadLegacyCatalog(metadata, result.catalog, progress);
    if (!result.catalog.legacySegments.isEmpty())
    {
        result.success = true;
        return result;
    }

    if (!indexError.isEmpty())
    {
        result.error = indexError;
        return result;
    }
    if (rawResult.status == UnifiedRawCatalogStatus::NotUnified &&
        !QFileInfo::exists(metadata.waveformDirectory))
    {
        result.error = rawResult.error;
        return result;
    }
    result.success = true;
    return result;
}

SessionWaveformFrameResult SessionWaveformRepository::readFrame(
    const SessionWaveformCatalog& catalog,
    quint64 frameIndex)
{
    SessionWaveformFrameResult result;
    if (frameIndex >= catalog.frameCount)
    {
        result.error = QStringLiteral("Waveform frame index is out of range");
        return result;
    }

    if (!catalog.rawTcpFrames.isEmpty())
    {
        const SessionRawTcpWaveFrame& frame = catalog.rawTcpFrames.at(static_cast<int>(frameIndex));
        QFile file(frame.filename);
        if (!file.open(QIODevice::ReadOnly) ||
            !file.seek(static_cast<qint64>(frame.harmonicPayloadOffset)))
        {
            result.error = QStringLiteral("Failed to read raw TCP wave file: %1").arg(frame.filename);
            return result;
        }
        const QByteArray payload = file.read(static_cast<qint64>(frame.harmonicPayloadSize));
        if (payload.size() != static_cast<qsizetype>(frame.harmonicPayloadSize) ||
            payload.size() % static_cast<qsizetype>(kFloatBytes) != 0)
        {
            result.error = QStringLiteral("Incomplete raw TCP wave frame: %1").arg(frame.filename);
            return result;
        }
        const TcpFloatEncoding encoding = frame.floatEncoding == TcpFloatEncoding::Unknown
            ? autoDetectTcpFloatEncoding(payload)
            : frame.floatEncoding;
        result.samples = decodeTcpFloatPayload(payload, encoding);
        result.timestampUs = frame.timestampUs;
        result.sourceFilename = frame.filename;
        result.success = true;
        return result;
    }

    if (!catalog.indexedFrames.isEmpty())
    {
        const SessionIndexedWaveformFrame& frame = catalog.indexedFrames.at(static_cast<int>(frameIndex));
        QFile file(frame.filename);
        if (!file.open(QIODevice::ReadOnly))
        {
            result.error = QStringLiteral("Failed to read indexed waveform file: %1").arg(frame.filename);
            return result;
        }
        const quint64 sampleBytes = static_cast<quint64>(frame.pointCount) * kFloatBytes;
        const QByteArray payload = file.read(static_cast<qint64>(sampleBytes));
        if (payload.size() != static_cast<qsizetype>(sampleBytes))
        {
            result.error = QStringLiteral("Incomplete indexed waveform file: %1").arg(frame.filename);
            return result;
        }
        result.samples = decodeTcpFloatPayload(payload, TcpFloatEncoding::LittleEndian);
        result.timestampUs = frame.timestampUs;
        result.sourceFilename = frame.filename;
        result.success = true;
        return result;
    }

    const auto segmentIt = std::find_if(
        catalog.legacySegments.cbegin(),
        catalog.legacySegments.cend(),
        [frameIndex](const SessionWaveformSegment& segment) {
            return frameIndex >= segment.startFrame &&
                frameIndex < segment.startFrame + segment.frameCount;
        });
    if (segmentIt == catalog.legacySegments.cend() || catalog.pointsPerFrame <= 0)
    {
        result.error = QStringLiteral("Legacy waveform frame is not indexed");
        return result;
    }

    const quint64 frameBytes = kTimestampBytes +
        static_cast<quint64>(catalog.pointsPerFrame) * kFloatBytes;
    const quint64 offset = (frameIndex - segmentIt->startFrame) * frameBytes;
    QFile file(segmentIt->filename);
    if (!file.open(QIODevice::ReadOnly) ||
        offset > static_cast<quint64>(std::numeric_limits<qint64>::max()) ||
        !file.seek(static_cast<qint64>(offset)))
    {
        result.error = QStringLiteral("Failed to read waveform file: %1").arg(segmentIt->filename);
        return result;
    }
    const QByteArray block = file.read(static_cast<qint64>(frameBytes));
    if (block.size() != static_cast<qsizetype>(frameBytes))
    {
        result.error = QStringLiteral("Incomplete waveform frame: %1").arg(segmentIt->filename);
        return result;
    }
    quint64 timestampLe = 0;
    std::memcpy(&timestampLe, block.constData(), sizeof(timestampLe));
    result.timestampUs = qFromLittleEndian(timestampLe);
    result.samples = decodeTcpFloatPayload(
        block.sliced(static_cast<qsizetype>(kTimestampBytes)),
        TcpFloatEncoding::LittleEndian);
    result.sourceFilename = segmentIt->filename;
    result.success = true;
    return result;
}

SessionWaveformPeakSeriesResult SessionWaveformRepository::loadCachedPeakSeries(
    const SessionWaveformCatalog& catalog)
{
    SessionWaveformPeakSeriesResult result;
    if (catalog.waveformPeaksCsvFilename.isEmpty() ||
        !QFileInfo::exists(catalog.waveformPeaksCsvFilename))
    {
        result.error = QStringLiteral("Waveform peak index is unavailable");
        return result;
    }
    QFile file(catalog.waveformPeaksCsvFilename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        result.error = QStringLiteral("Failed to open waveform peak index: %1")
                           .arg(catalog.waveformPeaksCsvFilename);
        return result;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    if (stream.atEnd())
    {
        result.error = QStringLiteral("Waveform peak index is empty");
        return result;
    }

    const QStringList headers = parseCsvLine(stream.readLine());
    const int hostTimeIndex = findHeaderIndex(
        headers,
        {QStringLiteral("host_time_us"), QStringLiteral("timestamp_us")});
    const int peakValueIndex = findHeaderIndex(
        headers,
        {QStringLiteral("peak_value"), QStringLiteral("peak")});
    const int searchStartIndex = findHeaderIndex(headers, {QStringLiteral("search_start_index")});
    const int searchEndIndex = findHeaderIndex(headers, {QStringLiteral("search_end_index")});
    if (hostTimeIndex < 0 || peakValueIndex < 0)
    {
        result.error = QStringLiteral("Invalid waveform peak index header");
        return result;
    }

    while (!stream.atEnd())
    {
        const QString line = stream.readLine();
        if (line.trimmed().isEmpty())
        {
            continue;
        }
        const QStringList fields = parseCsvLine(line);
        if (searchStartIndex >= 0)
        {
            bool ok = false;
            const int rowStart = csvValueAt(fields, searchStartIndex).toInt(&ok);
            if (ok && rowStart != 0)
            {
                result.error = QStringLiteral("Cached peak search range is incompatible");
                return result;
            }
        }
        if (searchEndIndex >= 0)
        {
            bool ok = false;
            const int rowEnd = csvValueAt(fields, searchEndIndex).toInt(&ok);
            if (ok && rowEnd > 0)
            {
                result.error = QStringLiteral("Cached peak search range is incompatible");
                return result;
            }
        }
        bool timestampOk = false;
        const quint64 timestampUs = csvValueAt(fields, hostTimeIndex).toULongLong(&timestampOk);
        if (!timestampOk)
        {
            continue;
        }
        const QString peakText = csvValueAt(fields, peakValueIndex).trimmed();
        bool peakOk = false;
        const float peak = peakText.toFloat(&peakOk);
        result.timestampsUs.push_back(timestampUs);
        result.peakValues.push_back(peakText.isEmpty() || !peakOk
            ? std::numeric_limits<float>::quiet_NaN()
            : peak);
    }
    if (result.peakValues.isEmpty() ||
        (catalog.frameCount > 0 &&
         static_cast<quint64>(result.peakValues.size()) != catalog.frameCount))
    {
        result.timestampsUs.clear();
        result.peakValues.clear();
        result.error = QStringLiteral("Waveform peak index frame count is incompatible");
        return result;
    }
    result.success = true;
    return result;
}

bool SessionWaveformRepository::writeCachedPeakSeries(
    const SessionWaveformCatalog& catalog,
    const QVector<quint64>& timestampsUs,
    const QVector<float>& peakValues)
{
    if (catalog.waveformPeaksCsvFilename.isEmpty() || timestampsUs.isEmpty() ||
        timestampsUs.size() != peakValues.size())
    {
        return false;
    }
    const QFileInfo info(catalog.waveformPeaksCsvFilename);
    if (!QDir().mkpath(info.absolutePath()))
    {
        return false;
    }
    QSaveFile file(catalog.waveformPeaksCsvFilename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return false;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << "host_time_us,peak_value,peak_index,point_count,search_start_index,search_end_index\n";
    for (int index = 0; index < timestampsUs.size(); ++index)
    {
        stream << timestampsUs.at(index) << ',';
        if (std::isfinite(peakValues.at(index)))
        {
            stream << QString::number(static_cast<double>(peakValues.at(index)), 'g', 9);
        }
        stream << ",-1," << catalog.pointsPerFrame << ",0,0\n";
    }
    return file.commit();
}

SessionWaveformPeakSeriesResult SessionWaveformRepository::calculatePeakSeries(
    const SessionWaveformCatalog& catalog,
    int searchStartIndex,
    int searchEndIndex,
    const std::shared_ptr<std::atomic_bool>& cancelFlag,
    const ProgressCallback& progress)
{
    SessionWaveformPeakSeriesResult result;
    result.success = true;
    if (catalog.isEmpty())
    {
        return result;
    }

    QVector<PeakPayload> payloads;
    qsizetype chunkBytes = 0;
    quint64 completed = 0;
    auto flush = [&]() {
        appendPeakPayloads(payloads, result, cancelFlag);
        payloads.clear();
        chunkBytes = 0;
    };
    auto append = [&](PeakPayload payload) {
        chunkBytes += payload.payload.size();
        payloads.push_back(std::move(payload));
        if (chunkBytes >= kPeakPayloadChunkBytes)
        {
            flush();
        }
        ++completed;
        if (progress)
        {
            progress(completed, catalog.frameCount);
        }
    };
    auto fail = [&](const QString& filename) {
        result.success = false;
        result.error = filename;
    };

    if (!catalog.rawTcpFrames.isEmpty())
    {
        QFile file;
        QString openFilename;
        for (const SessionRawTcpWaveFrame& frame : catalog.rawTcpFrames)
        {
            if (isCancelled(cancelFlag))
            {
                break;
            }
            if (openFilename != frame.filename)
            {
                file.close();
                file.setFileName(frame.filename);
                if (!file.open(QIODevice::ReadOnly))
                {
                    fail(frame.filename);
                    break;
                }
                openFilename = frame.filename;
            }
            const int sampleCount = static_cast<int>(std::min<quint64>(
                frame.harmonicPayloadSize / kFloatBytes,
                static_cast<quint64>(std::numeric_limits<int>::max())));
            PeakPayload payload;
            payload.timestampUs = frame.timestampUs;
            payload.encoding = frame.floatEncoding;
            if (!readPeakPayload(
                    file,
                    frame.harmonicPayloadOffset,
                    sampleCount,
                    searchStartIndex,
                    searchEndIndex,
                    payload.payload))
            {
                fail(frame.filename);
                break;
            }
            append(std::move(payload));
        }
    }
    else if (!catalog.indexedFrames.isEmpty())
    {
        for (const SessionIndexedWaveformFrame& frame : catalog.indexedFrames)
        {
            if (isCancelled(cancelFlag))
            {
                break;
            }
            QFile file(frame.filename);
            PeakPayload payload;
            payload.timestampUs = frame.timestampUs;
            if (!file.open(QIODevice::ReadOnly) ||
                !readPeakPayload(
                    file,
                    0,
                    static_cast<int>(frame.pointCount),
                    searchStartIndex,
                    searchEndIndex,
                    payload.payload))
            {
                fail(frame.filename);
                break;
            }
            append(std::move(payload));
        }
    }
    else
    {
        const quint64 frameBytes = kTimestampBytes +
            static_cast<quint64>(catalog.pointsPerFrame) * kFloatBytes;
        for (const SessionWaveformSegment& segment : catalog.legacySegments)
        {
            QFile file(segment.filename);
            if (!file.open(QIODevice::ReadOnly))
            {
                fail(segment.filename);
                break;
            }
            for (quint64 localFrame = 0; localFrame < segment.frameCount; ++localFrame)
            {
                if (isCancelled(cancelFlag))
                {
                    break;
                }
                const quint64 offset = localFrame * frameBytes;
                if (offset > static_cast<quint64>(std::numeric_limits<qint64>::max()) ||
                    !file.seek(static_cast<qint64>(offset)))
                {
                    fail(segment.filename);
                    break;
                }
                quint64 timestampLe = 0;
                if (file.read(reinterpret_cast<char*>(&timestampLe), sizeof(timestampLe)) !=
                    static_cast<qint64>(sizeof(timestampLe)))
                {
                    fail(segment.filename);
                    break;
                }
                PeakPayload payload;
                payload.timestampUs = qFromLittleEndian(timestampLe);
                if (!readPeakPayload(
                        file,
                        offset + kTimestampBytes,
                        catalog.pointsPerFrame,
                        searchStartIndex,
                        searchEndIndex,
                        payload.payload))
                {
                    fail(segment.filename);
                    break;
                }
                append(std::move(payload));
            }
            if (!result.success || isCancelled(cancelFlag))
            {
                break;
            }
        }
    }

    if (result.success && !isCancelled(cancelFlag))
    {
        flush();
    }
    if (isCancelled(cancelFlag))
    {
        result.success = false;
        result.cancelled = true;
        result.timestampsUs.clear();
        result.peakValues.clear();
    }
    return result;
}

QVector<float> SessionWaveformRepository::applyPeakFilter(
    const QVector<float>& rawValues,
    const SessionPeakFilterSettings& settings)
{
    QVector<double> finiteValues;
    finiteValues.reserve(rawValues.size());
    for (float value : rawValues)
    {
        if (std::isfinite(value))
        {
            finiteValues.push_back(value);
        }
    }

    double lowerBound = -std::numeric_limits<double>::infinity();
    double upperBound = std::numeric_limits<double>::infinity();
    if (settings.mode == SessionPeakFilterMode::IqrOutlier && finiteValues.size() >= 4)
    {
        const double q1 = percentile(finiteValues, 0.25);
        const double q3 = percentile(finiteValues, 0.75);
        const double padding = std::max(1e-6, (q3 - q1) * 1.5);
        lowerBound = q1 - padding;
        upperBound = q3 + padding;
    }
    const double rangeMin = std::min(settings.minValue, settings.maxValue);
    const double rangeMax = std::max(settings.minValue, settings.maxValue);

    QVector<float> filtered;
    filtered.reserve(rawValues.size());
    for (float rawValue : rawValues)
    {
        bool keep = std::isfinite(rawValue);
        if (keep)
        {
            switch (settings.mode)
            {
            case SessionPeakFilterMode::IqrOutlier:
                keep = rawValue >= lowerBound && rawValue <= upperBound;
                break;
            case SessionPeakFilterMode::KeepRange:
                keep = rawValue >= rangeMin && rawValue <= rangeMax;
                break;
            case SessionPeakFilterMode::ExcludeRange:
                keep = !(rawValue >= rangeMin && rawValue <= rangeMax);
                break;
            case SessionPeakFilterMode::None:
                break;
            }
        }
        filtered.push_back(keep ? rawValue : std::numeric_limits<float>::quiet_NaN());
    }
    return filtered;
}

float SessionWaveformRepository::peakValue(
    const QVector<float>& samples,
    int searchStartIndex,
    int searchEndIndex)
{
    const auto [startIndex, endIndex] = peakSearchRange(
        samples.size(),
        searchStartIndex,
        searchEndIndex);
    bool hasPeak = false;
    float peak = std::numeric_limits<float>::lowest();
    for (int index = startIndex; index < endIndex; ++index)
    {
        if (!std::isfinite(samples.at(index)))
        {
            continue;
        }
        hasPeak = true;
        peak = std::max(peak, samples.at(index));
    }
    return hasPeak ? peak : std::numeric_limits<float>::quiet_NaN();
}

bool SessionWaveformRepository::isFullFramePeakSearch(int searchStartIndex, int searchEndIndex)
{
    return searchStartIndex == 0 && searchEndIndex <= 0;
}

}  // namespace VaporView::Ground
