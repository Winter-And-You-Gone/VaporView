#include "ground/session/GroundRecordingService.h"

#include "ground/session/RecordingSessionLayout.h"
#include "shared/session/SessionSensorCsv.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>
#include <QtEndian>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

namespace VaporView::Ground::Session
{
namespace
{

constexpr char kUnifiedRawMagic[8] = {'V', 'V', 'R', 'A', 'W', 'D', 'A', 'T'};
constexpr quint32 kUnifiedRawFormatVersion = 2u;
constexpr quint32 kUnifiedRawRecordMarker = 0x44525756u;
constexpr quint16 kRawSourceEpsilon = 1u;
constexpr quint16 kRawSourcePtb = 2u;
constexpr quint16 kRawSourceHmp = 3u;
constexpr quint16 kRawSourceLidar = 4u;
constexpr quint16 kRawSourceTcpWave = 5u;
constexpr quint16 kRawRecordTypeGeneric = 1u;
constexpr quint32 kRawTcpWaveCombinedPayloadFlag = 0x00000001u;
constexpr qint64 kTcpRecordingStatusRefreshMs = 500;
constexpr quint64 kTcpRawRecordQueueWarningBytes = 32ULL * 1024ULL * 1024ULL;
constexpr quint64 kTcpRawRecordQueueMaxBytes = 256ULL * 1024ULL * 1024ULL;
constexpr qint64 kTcpRawRecordQueueWarningIntervalMs = 5000;
constexpr const char *kTcpWavePeakIndexCsvHeader =
    "host_time_us,peak_value,peak_index,point_count,search_start_index,search_end_index\n";

#pragma pack(push, 1)
struct UnifiedRawFileHeader
{
    char magic[8];
    quint32 version;
    quint32 headerSize;
    quint16 sourceId;
    quint16 reserved;
};

struct UnifiedRawRecordHeader
{
    quint32 marker;
    quint32 headerSize;
    quint64 hostTimestampUs;
    quint32 payloadSize;
    quint16 sourceId;
    quint16 recordType;
    quint32 flags;
    quint64 sequence;
};
#pragma pack(pop)

struct TcpRawRecord
{
    quint64 timestampUs = 0;
    quint32 flags = 0;
    QByteArray payload;
};

struct TcpWavePeakSummary
{
    float value = std::numeric_limits<float>::quiet_NaN();
    int index = -1;
    quint32 pointCount = 0;
};

QString timestampUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString sessionDirectoryTimestamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
}

bool writeJsonFileAtomically(const QString& filename,
                             const QJsonObject& object,
                             QString *errorMessage)
{
    QSaveFile file(filename);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }

    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size())
    {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    if (!file.commit())
    {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    return true;
}

TcpWavePeakSummary summarizeTcpWavePeakSamples(const char *samples,
                                               qsizetype byteCount,
                                               TcpFloatEncoding encoding)
{
    TcpWavePeakSummary summary;
    if (!samples || byteCount <= 0 || byteCount % static_cast<qsizetype>(sizeof(float)) != 0)
    {
        return summary;
    }

    const qsizetype sampleCount = byteCount / static_cast<qsizetype>(sizeof(float));
    summary.pointCount = static_cast<quint32>(std::min<quint64>(
        static_cast<quint64>(sampleCount),
        static_cast<quint64>(std::numeric_limits<quint32>::max())));
    const TcpFloatEncoding effectiveEncoding = encoding == TcpFloatEncoding::Unknown
        ? autoDetectTcpFloatEncoding(QByteArray(samples, byteCount))
        : encoding;

    bool hasPeak = false;
    float peakValue = std::numeric_limits<float>::lowest();
    const int scanCount = static_cast<int>(std::min<quint64>(
        static_cast<quint64>(sampleCount),
        static_cast<quint64>(std::numeric_limits<int>::max())));
    for (int index = 0; index < scanCount; ++index)
    {
        const float value = decodeTcpFloatSample(
            samples + index * static_cast<int>(sizeof(float)),
            effectiveEncoding);
        if (!std::isfinite(value))
        {
            continue;
        }
        if (!hasPeak || value > peakValue)
        {
            hasPeak = true;
            peakValue = value;
            summary.index = index;
        }
    }
    if (hasPeak)
    {
        summary.value = peakValue;
    }
    return summary;
}

TcpWavePeakSummary summarizeTcpWavePeakRecordPayload(const QByteArray& payload, quint32 flags)
{
    if (payload.size() < static_cast<qsizetype>(sizeof(quint32) * 2))
    {
        return {};
    }

    quint32 rawSizeLe = 0;
    quint32 harmonicSizeLe = 0;
    const char *cursor = payload.constData();
    std::memcpy(&rawSizeLe, cursor, sizeof(rawSizeLe));
    cursor += sizeof(rawSizeLe);
    std::memcpy(&harmonicSizeLe, cursor, sizeof(harmonicSizeLe));

    const quint32 rawSize = qFromLittleEndian(rawSizeLe);
    const quint32 harmonicSize = qFromLittleEndian(harmonicSizeLe);
    const quint64 requiredBytes = static_cast<quint64>(sizeof(quint32) * 2) + rawSize + harmonicSize;
    if (requiredBytes > static_cast<quint64>(payload.size()) ||
        harmonicSize == 0 ||
        harmonicSize % static_cast<quint32>(sizeof(float)) != 0)
    {
        return {};
    }

    return summarizeTcpWavePeakSamples(
        payload.constData() + sizeof(quint32) * 2 + rawSize,
        static_cast<qsizetype>(harmonicSize),
        tcpFloatEncodingFromRawDatFlags(flags));
}

QString peakValueCsvText(float value)
{
    return std::isfinite(value)
        ? QString::number(static_cast<double>(value), 'g', 9)
        : QString();
}

QJsonObject serialConfigJson(const GroundRecordingSerialConfig& config)
{
    QJsonObject object;
    object[QStringLiteral("port")] = config.port;
    object[QStringLiteral("baud")] = config.baud;
    object[QStringLiteral("rate_hz")] = config.rateHz;
    return object;
}

}  // namespace

class GroundRecordingService::Impl
{
public:
    Impl()
        : steadyClockAnchor(std::chrono::steady_clock::now())
        , systemClockAnchor(std::chrono::system_clock::now())
    {
    }

    ~Impl()
    {
        stop();
    }

    bool start(const GroundRecordingOptions& requestedOptions,
               GroundRecordingStartError *startError,
               QString *errorMessage)
    {
        if (startError) *startError = GroundRecordingStartError::None;
        if (isSessionOpen())
        {
            if (!paused.load())
            {
                return true;
            }
            startWorkers();
            notifyStatus();
            return true;
        }

        options = requestedOptions;
        options.exportRateHz = std::clamp(options.exportRateHz, 1, 200);
        const QString sessionName = QStringLiteral("session_%1").arg(sessionDirectoryTimestamp());
        const auto createdLayout = createRecordingSessionLayout(options.baseDirectory, sessionName);
        if (!createdLayout)
        {
            if (startError) *startError = GroundRecordingStartError::CreateSessionLayout;
            if (errorMessage) *errorMessage = QStringLiteral("failed to create session layout");
            return false;
        }
        layout = *createdLayout;

        sensorsFile = std::make_unique<QFile>(layout.sensorsFilename);
        eventLogFile = std::make_unique<QFile>(layout.eventLogFilename);
        errorLogFile = std::make_unique<QFile>(layout.errorLogFilename);
        rawTcpWavePeakIndexFile = std::make_unique<QFile>(layout.rawTcpWavePeakIndexFilename);
        if (!sensorsFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
            !eventLogFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
            !errorLogFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
            !rawTcpWavePeakIndexFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
            !openRawFile(rawEpsilonFile, layout.rawEpsilonFilename, kRawSourceEpsilon) ||
            !openRawFile(rawPtbFile, layout.rawPtbFilename, kRawSourcePtb) ||
            !openRawFile(rawHmpFile, layout.rawHmpFilename, kRawSourceHmp) ||
            !openRawFile(rawLidarFile, layout.rawLidarFilename, kRawSourceLidar) ||
            !openRawFile(rawTcpWaveFile, layout.rawTcpWaveFilename, kRawSourceTcpWave))
        {
            if (startError) *startError = GroundRecordingStartError::OpenSessionFiles;
            if (errorMessage) *errorMessage = QStringLiteral("failed to open session files");
            closeFiles();
            resetFiles();
            layout = {};
            return false;
        }

        sessionStartTimeUtc = timestampUtc();
        sessionStartTimeUs = GroundRecordingService::currentTimestampUs();
        resetCurrentCounts();
        {
            QTextStream out(eventLogFile.get());
            out.setEncoding(QStringConverter::Utf8);
            out << "timestamp_utc,timestamp_us,level,message\n";
            out.flush();
        }
        {
            QTextStream out(rawTcpWavePeakIndexFile.get());
            out.setEncoding(QStringConverter::Utf8);
            out << kTcpWavePeakIndexCsvHeader;
            out.flush();
        }
        {
            QTextStream out(sensorsFile.get());
            out.setEncoding(QStringConverter::Utf8);
            out.setGenerateByteOrderMark(true);
            out << SessionSensorCsv::header();
            out.flush();
        }

        if (!copyRawFormatDocument())
        {
            warn(GroundRecordingWarning::RawFormatDocumentCopyFailed, 0);
        }
        QString metadataError;
        if (!writeSessionMetadata(QString(), &metadataError))
        {
            if (startError) *startError = GroundRecordingStartError::WriteSessionMetadata;
            if (errorMessage) *errorMessage = metadataError;
            closeFiles();
            resetFiles();
            layout = {};
            return false;
        }
        if (!writeDeviceConfigSnapshot())
        {
            warn(GroundRecordingWarning::DeviceConfigSnapshotFailed, 0);
        }

        startWorkers();
        notifyStatus();
        return true;
    }

    bool pause()
    {
        if (!isSessionOpen() || paused.load())
        {
            return false;
        }
        stopWorkers();
        paused.store(true);
        if (!writeSessionMetadata())
        {
            warn(GroundRecordingWarning::MetadataUpdateFailed, 0);
        }
        notifyStatus();
        return true;
    }

    GroundRecordingStopSummary stop()
    {
        GroundRecordingStopSummary summary;
        summary.hadOpenSession = isSessionOpen();
        stopWorkers();
        if (!summary.hadOpenSession)
        {
            paused.store(false);
            notifyStatus();
            return summary;
        }

        summary.sessionDirectory = layout.sessionDirectory;
        summary.sensorRows = sensorRows.load();
        summary.waveformFrames = waveformFrames.load();
        lastStatus = currentStatus();
        lastStatus.sessionOpen = false;
        lastStatus.active = false;
        lastStatus.paused = false;

        if (!writeSessionMetadata(timestampUtc()))
        {
            warn(GroundRecordingWarning::MetadataUpdateFailed, 0);
        }
        closeFiles();
        resetFiles();
        resetCurrentCounts();
        paused.store(false);
        layout = {};
        sessionStartTimeUtc.clear();
        sessionStartTimeUs = 0;
        notifyStatus();
        return summary;
    }

    GroundRecordingStatus status() const
    {
        return isSessionOpen() ? currentStatus() : lastStatus;
    }

    GroundRecordingStatus currentStatus() const
    {
        GroundRecordingStatus result;
        result.sessionOpen = isSessionOpen();
        result.active = workerRunning.load();
        result.paused = paused.load();
        result.sessionName = layout.sessionName;
        result.sessionDirectory = layout.sessionDirectory;
        result.sensorRows = sensorRows.load();
        result.waveformFrames = waveformFrames.load();
        result.rawEpsilonRecords = rawEpsilonRecordCount.load();
        result.rawPtbRecords = rawPtbRecordCount.load();
        result.rawHmpRecords = rawHmpRecordCount.load();
        result.rawLidarRecords = rawLidarRecordCount.load();
        result.rawTcpWaveRecords = rawTcpWaveRecordCount.load();
        return result;
    }

    bool isSessionOpen() const
    {
        std::lock_guard<std::mutex> lock(filesMutex);
        return sensorsFile && sensorsFile->isOpen();
    }

    bool recordRaw(QFile *file,
                   std::atomic<quint64>& recordCount,
                   quint16 sourceId,
                   quint16 recordType,
                   quint32 flags,
                   quint64 timestampUs,
                   const void *data,
                   size_t size)
    {
        if (!workerRunning.load())
        {
            return false;
        }
        return writeRawRecord(file,
                              recordCount,
                              sourceId,
                              recordType,
                              flags,
                              timestampUs,
                              data,
                              size);
    }

    bool enqueueTcpRawRecord(TcpRawRecord record)
    {
        const quint64 payloadBytes = static_cast<quint64>(record.payload.size());
        bool enqueued = false;
        GroundRecordingWarning warning = GroundRecordingWarning::TcpQueueBacklog;
        quint64 warningValue = 0;
        bool hasWarning = false;
        {
            std::lock_guard<std::mutex> lock(tcpQueueMutex);
            if (!tcpWorkerRunning)
            {
                return false;
            }

            const quint64 nextBytes = tcpQueueBytes + payloadBytes;
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            if (nextBytes > kTcpRawRecordQueueMaxBytes)
            {
                ++tcpDroppedCount;
                if (lastTcpQueueWarningMs <= 0 ||
                    nowMs - lastTcpQueueWarningMs >= kTcpRawRecordQueueWarningIntervalMs)
                {
                    lastTcpQueueWarningMs = nowMs;
                    warning = GroundRecordingWarning::TcpQueueFull;
                    warningValue = tcpQueueBytes / (1024ULL * 1024ULL);
                    hasWarning = true;
                }
            }
            else
            {
                tcpQueueBytes = nextBytes;
                tcpQueue.push_back(std::move(record));
                enqueued = true;
                if (nextBytes >= kTcpRawRecordQueueWarningBytes &&
                    (lastTcpQueueWarningMs <= 0 ||
                     nowMs - lastTcpQueueWarningMs >= kTcpRawRecordQueueWarningIntervalMs))
                {
                    lastTcpQueueWarningMs = nowMs;
                    warningValue = nextBytes / (1024ULL * 1024ULL);
                    hasWarning = true;
                }
            }
        }
        if (hasWarning)
        {
            warn(warning, warningValue);
        }
        if (enqueued)
        {
            tcpQueueCv.notify_one();
        }
        return enqueued;
    }

    void appendEvent(const QString& level, const QString& message)
    {
        std::lock_guard<std::mutex> lock(filesMutex);
        if (!eventLogFile || !eventLogFile->isOpen())
        {
            return;
        }
        QTextStream out(eventLogFile.get());
        out.setEncoding(QStringConverter::Utf8);
        out << SessionSensorCsv::escape(timestampUtc()) << ','
            << GroundRecordingService::currentTimestampUs() << ','
            << SessionSensorCsv::escape(level) << ','
            << SessionSensorCsv::escape(message) << '\n';
        out.flush();
    }

    void appendError(const QString& message)
    {
        std::lock_guard<std::mutex> lock(filesMutex);
        if (!errorLogFile || !errorLogFile->isOpen())
        {
            return;
        }
        QTextStream out(errorLogFile.get());
        out.setEncoding(QStringConverter::Utf8);
        out << '[' << timestampUtc() << "] " << message << '\n';
        out.flush();
    }

    quint64 steadyToEpochUs(const std::chrono::steady_clock::time_point& timePoint) const
    {
        if (timePoint == std::chrono::steady_clock::time_point{})
        {
            return 0;
        }
        const auto delta = timePoint - steadyClockAnchor;
        const auto systemPoint = systemClockAnchor +
            std::chrono::duration_cast<std::chrono::system_clock::duration>(delta);
        return static_cast<quint64>(std::chrono::duration_cast<std::chrono::microseconds>(
            systemPoint.time_since_epoch()).count());
    }

    void notifyStatus() const
    {
        if (statusCallback)
        {
            statusCallback();
        }
    }

    void warn(GroundRecordingWarning warning, quint64 value) const
    {
        if (warningCallback)
        {
            warningCallback(warning, value);
        }
    }

private:
    bool openRawFile(std::unique_ptr<QFile>& file, const QString& filename, quint16 sourceId)
    {
        file = std::make_unique<QFile>(filename);
        if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            file.reset();
            return false;
        }

        UnifiedRawFileHeader header{};
        std::memcpy(header.magic, kUnifiedRawMagic, sizeof(header.magic));
        header.version = qToLittleEndian(kUnifiedRawFormatVersion);
        header.headerSize = qToLittleEndian(static_cast<quint32>(sizeof(UnifiedRawFileHeader)));
        header.sourceId = qToLittleEndian(sourceId);
        if (file->write(reinterpret_cast<const char *>(&header), sizeof(header)) !=
            static_cast<qint64>(sizeof(header)))
        {
            file->close();
            file.reset();
            return false;
        }
        file->flush();
        return true;
    }

    bool writeRawRecord(QFile *file,
                        std::atomic<quint64>& recordCount,
                        quint16 sourceId,
                        quint16 recordType,
                        quint32 flags,
                        quint64 timestampUs,
                        const void *data,
                        size_t size)
    {
        if (!file || !file->isOpen() || (size > 0 && !data) ||
            size > static_cast<size_t>(std::numeric_limits<quint32>::max()))
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(filesMutex);
        if (!file->isOpen())
        {
            return false;
        }

        const quint64 sequence = recordCount.load(std::memory_order_relaxed);
        UnifiedRawRecordHeader header{};
        header.marker = qToLittleEndian(kUnifiedRawRecordMarker);
        header.headerSize = qToLittleEndian(static_cast<quint32>(sizeof(UnifiedRawRecordHeader)));
        header.hostTimestampUs = qToLittleEndian(timestampUs);
        header.payloadSize = qToLittleEndian(static_cast<quint32>(size));
        header.sourceId = qToLittleEndian(sourceId);
        header.recordType = qToLittleEndian(recordType);
        header.flags = qToLittleEndian(flags);
        header.sequence = qToLittleEndian(sequence);
        if (file->write(reinterpret_cast<const char *>(&header), sizeof(header)) !=
            static_cast<qint64>(sizeof(header)))
        {
            return false;
        }
        if (size > 0 && file->write(reinterpret_cast<const char *>(data), static_cast<qint64>(size)) !=
            static_cast<qint64>(size))
        {
            return false;
        }
        recordCount.store(sequence + 1, std::memory_order_relaxed);
        return true;
    }

    void startWorkers()
    {
        if (workerRunning.load())
        {
            return;
        }
        paused.store(false);
        startTcpWorker();
        QFile *file = sensorsFile.get();
        workerRunning.store(true);
        sensorThread = std::thread([this, file]() {
            const auto period = std::chrono::microseconds(1'000'000 / options.exportRateHz);
            auto nextTick = std::chrono::steady_clock::now();
            while (workerRunning.load())
            {
                GroundSensorSnapshot snapshot;
                if (sensorSnapshotProvider)
                {
                    snapshot = sensorSnapshotProvider();
                }
                const quint64 recordTimestampUs = GroundRecordingService::currentTimestampUs();
                const QString row = SessionSensorCsv::formatRow(
                    recordTimestampUs,
                    steadyToEpochUs(snapshot.epsilon.timestamp),
                    snapshot.epsilon,
                    snapshot.hasEpsilon,
                    snapshot.ptb,
                    snapshot.hasPtb,
                    snapshot.hmp,
                    snapshot.hasHmp,
                    snapshot.lidar,
                    snapshot.hasLidar);
                {
                    std::lock_guard<std::mutex> lock(filesMutex);
                    if (file && file->isOpen())
                    {
                        QTextStream out(file);
                        out.setEncoding(QStringConverter::Utf8);
                        out << row;
                        out.flush();
                        sensorRows.fetch_add(1);
                    }
                }
                notifyStatus();
                nextTick += period;
                std::this_thread::sleep_until(nextTick);
            }
        });
    }

    void stopWorkers()
    {
        workerRunning.store(false);
        if (sensorThread.joinable())
        {
            sensorThread.join();
        }
        stopTcpWorker();
    }

    void startTcpWorker()
    {
        stopTcpWorker();
        {
            std::lock_guard<std::mutex> lock(tcpQueueMutex);
            tcpQueue.clear();
            tcpQueueBytes = 0;
            tcpDroppedCount = 0;
            lastTcpQueueWarningMs = 0;
            tcpWorkerRunning = true;
        }
        tcpThread = std::thread([this]() {
            while (true)
            {
                TcpRawRecord record;
                {
                    std::unique_lock<std::mutex> lock(tcpQueueMutex);
                    tcpQueueCv.wait(lock, [this]() {
                        return !tcpQueue.empty() || !tcpWorkerRunning;
                    });
                    if (tcpQueue.empty() && !tcpWorkerRunning)
                    {
                        break;
                    }
                    record = std::move(tcpQueue.front());
                    tcpQueue.pop_front();
                    tcpQueueBytes -= static_cast<quint64>(record.payload.size());
                }

                if (writeRawRecord(rawTcpWaveFile.get(),
                                   rawTcpWaveRecordCount,
                                   kRawSourceTcpWave,
                                   kRawRecordTypeGeneric,
                                   record.flags,
                                   record.timestampUs,
                                   record.payload.constData(),
                                   static_cast<size_t>(record.payload.size())))
                {
                    appendTcpPeakIndex(record);
                    waveformFrames.fetch_add(1);
                    waveformFileCount.store(1);
                    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                    const qint64 lastUpdate = lastTcpStatusUpdateMs.load();
                    if (lastUpdate <= 0 || nowMs - lastUpdate >= kTcpRecordingStatusRefreshMs)
                    {
                        lastTcpStatusUpdateMs.store(nowMs);
                        notifyStatus();
                    }
                }
            }
        });
    }

    void stopTcpWorker()
    {
        {
            std::lock_guard<std::mutex> lock(tcpQueueMutex);
            tcpWorkerRunning = false;
        }
        tcpQueueCv.notify_all();
        if (tcpThread.joinable())
        {
            tcpThread.join();
        }

        quint64 dropped = 0;
        {
            std::lock_guard<std::mutex> lock(tcpQueueMutex);
            dropped = tcpDroppedCount;
            tcpQueue.clear();
            tcpQueueBytes = 0;
            tcpDroppedCount = 0;
            lastTcpQueueWarningMs = 0;
        }
        if (dropped > 0)
        {
            warn(GroundRecordingWarning::TcpFramesDropped, dropped);
        }
    }

    void appendTcpPeakIndex(const TcpRawRecord& record)
    {
        const TcpWavePeakSummary summary = summarizeTcpWavePeakRecordPayload(record.payload, record.flags);
        std::lock_guard<std::mutex> lock(filesMutex);
        if (!rawTcpWavePeakIndexFile || !rawTcpWavePeakIndexFile->isOpen())
        {
            return;
        }
        QTextStream out(rawTcpWavePeakIndexFile.get());
        out.setEncoding(QStringConverter::Utf8);
        out << record.timestampUs << ','
            << peakValueCsvText(summary.value) << ','
            << summary.index << ','
            << summary.pointCount << ",0,0\n";
        out.flush();
    }

    bool copyRawFormatDocument() const
    {
        QDir directory(QCoreApplication::applicationDirPath());
        QString repositoryRoot;
        for (int depth = 0; depth < 6; ++depth)
        {
            if (QFileInfo::exists(directory.filePath(QStringLiteral("CMakeLists.txt"))) &&
                QFileInfo::exists(directory.filePath(QStringLiteral("README.md"))))
            {
                repositoryRoot = directory.path();
                break;
            }
            if (!directory.cdUp())
            {
                break;
            }
        }
        if (repositoryRoot.isEmpty())
        {
            return false;
        }
        const QString source = QDir(repositoryRoot).filePath(QStringLiteral("docs/raw_dat_format.md"));
        if (!QFileInfo::exists(source))
        {
            return false;
        }
        QFile::remove(layout.rawDatDocumentFilename);
        return QFile::copy(source, layout.rawDatDocumentFilename);
    }

    bool writeSessionMetadata(const QString& endTimeUtc = QString(), QString *errorMessage = nullptr) const
    {
        if (layout.sessionMetadataFilename.isEmpty() || layout.sessionDirectory.isEmpty())
        {
            if (errorMessage) *errorMessage = QStringLiteral("session metadata path is empty");
            return false;
        }

        QDir sessionDir(layout.sessionDirectory);
        QJsonObject root;
        root[QStringLiteral("session_name")] = layout.sessionName;
        root[QStringLiteral("start_time_utc")] = sessionStartTimeUtc;
        root[QStringLiteral("start_time_us")] = QString::number(sessionStartTimeUs);
        root[QStringLiteral("end_time_utc")] = endTimeUtc;
        root[QStringLiteral("software_version")] = QCoreApplication::applicationVersion().isEmpty()
            ? QStringLiteral("dev")
            : QCoreApplication::applicationVersion();
        root[QStringLiteral("epsilon_schema_version")] = QStringLiteral("epsilon.v1");
        root[QStringLiteral("waveform_points_per_frame")] = 50000;
        root[QStringLiteral("sensor_export_rate_hz")] = options.exportRateHz;
        root[QStringLiteral("other_devices_export_rate_hz")] = options.exportRateHz;
        root[QStringLiteral("raw_export_mode")] = QStringLiteral("unified_raw_dat");
        root[QStringLiteral("raw_dat_format_version")] = static_cast<int>(kUnifiedRawFormatVersion);
        root[QStringLiteral("waveform_export_rate_hz")] = 0;
        root[QStringLiteral("waveform_export_mode")] = QStringLiteral("per_frame");
        root[QStringLiteral("waveform_value_type")] = QStringLiteral("float32");
        root[QStringLiteral("waveform_timestamp_type")] = QStringLiteral("uint64");
        root[QStringLiteral("timestamp_unit")] = QStringLiteral("microseconds");
        root[QStringLiteral("sensor_rows")] = QString::number(sensorRows.load());
        root[QStringLiteral("waveform_frames")] = QString::number(waveformFrames.load());
        root[QStringLiteral("waveform_file_count")] = QString::number(waveformFileCount.load());

        QJsonObject rawFiles;
        auto addRawFile = [&rawFiles, &sessionDir](const QString& name,
                                                   const QString& filename,
                                                   quint16 sourceId,
                                                   quint64 recordCount) {
            QJsonObject raw;
            raw[QStringLiteral("path")] = sessionDir.relativeFilePath(filename);
            raw[QStringLiteral("source_id")] = static_cast<int>(sourceId);
            raw[QStringLiteral("format_version")] = static_cast<int>(kUnifiedRawFormatVersion);
            raw[QStringLiteral("record_count")] = QString::number(recordCount);
            rawFiles[name] = raw;
        };
        addRawFile(QStringLiteral("epsilon"), layout.rawEpsilonFilename, kRawSourceEpsilon, rawEpsilonRecordCount.load());
        addRawFile(QStringLiteral("ptb"), layout.rawPtbFilename, kRawSourcePtb, rawPtbRecordCount.load());
        addRawFile(QStringLiteral("hmp"), layout.rawHmpFilename, kRawSourceHmp, rawHmpRecordCount.load());
        addRawFile(QStringLiteral("lidar"), layout.rawLidarFilename, kRawSourceLidar, rawLidarRecordCount.load());
        addRawFile(QStringLiteral("tcp_wave"), layout.rawTcpWaveFilename, kRawSourceTcpWave, rawTcpWaveRecordCount.load());
        root[QStringLiteral("raw_files")] = rawFiles;

        QJsonObject paths;
        paths[QStringLiteral("raw_directory")] = QStringLiteral("raw");
        paths[QStringLiteral("devices_csv")] = sessionDir.relativeFilePath(layout.sensorsFilename);
        paths[QStringLiteral("waveform_peak_index")] = sessionDir.relativeFilePath(layout.rawTcpWavePeakIndexFilename);
        paths[QStringLiteral("raw_format_doc")] = sessionDir.relativeFilePath(layout.rawDatDocumentFilename);
        paths[QStringLiteral("event_log")] = sessionDir.relativeFilePath(layout.eventLogFilename);
        paths[QStringLiteral("error_log")] = sessionDir.relativeFilePath(layout.errorLogFilename);
        paths[QStringLiteral("device_config")] = sessionDir.relativeFilePath(layout.deviceConfigFilename);
        root[QStringLiteral("paths")] = paths;

        return writeJsonFileAtomically(layout.sessionMetadataFilename, root, errorMessage);
    }

    bool writeDeviceConfigSnapshot() const
    {
        QJsonObject root;
        root[QStringLiteral("recording_directory")] = options.baseDirectory;
        root[QStringLiteral("session_directory")] = layout.sessionDirectory;
        root[QStringLiteral("epsilon_schema_version")] = QStringLiteral("epsilon.v1");
        root[QStringLiteral("sensor_export_rate_hz")] = options.exportRateHz;
        root[QStringLiteral("other_devices_export_rate_hz")] = options.exportRateHz;
        root[QStringLiteral("raw_export_mode")] = QStringLiteral("unified_raw_dat");
        root[QStringLiteral("raw_dat_format_version")] = static_cast<int>(kUnifiedRawFormatVersion);
        root[QStringLiteral("waveform_export_rate_hz")] = 0;
        root[QStringLiteral("waveform_export_mode")] = QStringLiteral("per_frame");

        QJsonObject waveform;
        waveform[QStringLiteral("host")] = options.deviceConfig.waveformHost;
        waveform[QStringLiteral("port")] = options.deviceConfig.waveformPort;
        waveform[QStringLiteral("frame_rate_hz")] = 0;
        waveform[QStringLiteral("frame_rate_mode")] = QStringLiteral("per_frame");
        waveform[QStringLiteral("points_per_frame")] = 50000;
        waveform[QStringLiteral("value_type")] = QStringLiteral("float32");
        waveform[QStringLiteral("timestamp_type")] = QStringLiteral("uint64");
        root[QStringLiteral("waveform")] = waveform;

        QJsonObject raw;
        raw[QStringLiteral("directory")] = QStringLiteral("raw");
        raw[QStringLiteral("format_doc")] = QStringLiteral("raw_dat_format.md");
        raw[QStringLiteral("mode")] = QStringLiteral("per_verified_raw_frame_or_response");
        root[QStringLiteral("raw_dat")] = raw;

        QJsonObject sensors;
        sensors[QStringLiteral("epsilon")] = serialConfigJson(options.deviceConfig.epsilon);
        sensors[QStringLiteral("ptb")] = serialConfigJson(options.deviceConfig.ptb);
        sensors[QStringLiteral("hmp")] = serialConfigJson(options.deviceConfig.hmp);
        sensors[QStringLiteral("lidar")] = serialConfigJson(options.deviceConfig.lidar);
        sensors[QStringLiteral("rd105")] = serialConfigJson(options.deviceConfig.temperatureController);
        root[QStringLiteral("sensors")] = sensors;

        return writeJsonFileAtomically(layout.deviceConfigFilename, root, nullptr);
    }

    void closeFiles()
    {
        std::lock_guard<std::mutex> lock(filesMutex);
        for (QFile *file : {rawEpsilonFile.get(),
                            rawPtbFile.get(),
                            rawHmpFile.get(),
                            rawLidarFile.get(),
                            rawTcpWaveFile.get(),
                            rawTcpWavePeakIndexFile.get(),
                            sensorsFile.get(),
                            eventLogFile.get(),
                            errorLogFile.get()})
        {
            if (file && file->isOpen())
            {
                file->flush();
                file->close();
            }
        }
    }

    void resetFiles()
    {
        sensorsFile.reset();
        rawEpsilonFile.reset();
        rawPtbFile.reset();
        rawHmpFile.reset();
        rawLidarFile.reset();
        rawTcpWaveFile.reset();
        rawTcpWavePeakIndexFile.reset();
        eventLogFile.reset();
        errorLogFile.reset();
    }

    void resetCurrentCounts()
    {
        sensorRows.store(0);
        waveformFrames.store(0);
        waveformFileCount.store(0);
        rawEpsilonRecordCount.store(0);
        rawPtbRecordCount.store(0);
        rawHmpRecordCount.store(0);
        rawLidarRecordCount.store(0);
        rawTcpWaveRecordCount.store(0);
        lastTcpStatusUpdateMs.store(0);
    }

public:
    SensorSnapshotProvider sensorSnapshotProvider;
    StatusCallback statusCallback;
    WarningCallback warningCallback;
    GroundRecordingOptions options;
    RecordingSessionLayout layout;
    GroundRecordingStatus lastStatus;
    std::chrono::steady_clock::time_point steadyClockAnchor;
    std::chrono::system_clock::time_point systemClockAnchor;
    QString sessionStartTimeUtc;
    quint64 sessionStartTimeUs = 0;

    std::unique_ptr<QFile> sensorsFile;
    std::unique_ptr<QFile> rawEpsilonFile;
    std::unique_ptr<QFile> rawPtbFile;
    std::unique_ptr<QFile> rawHmpFile;
    std::unique_ptr<QFile> rawLidarFile;
    std::unique_ptr<QFile> rawTcpWaveFile;
    std::unique_ptr<QFile> rawTcpWavePeakIndexFile;
    std::unique_ptr<QFile> eventLogFile;
    std::unique_ptr<QFile> errorLogFile;

    mutable std::mutex filesMutex;
    std::thread sensorThread;
    std::atomic<bool> workerRunning{false};
    std::atomic<bool> paused{false};
    std::atomic<qint64> sensorRows{0};
    std::atomic<qint64> waveformFrames{0};
    std::atomic<qint64> waveformFileCount{0};
    std::atomic<quint64> rawEpsilonRecordCount{0};
    std::atomic<quint64> rawPtbRecordCount{0};
    std::atomic<quint64> rawHmpRecordCount{0};
    std::atomic<quint64> rawLidarRecordCount{0};
    std::atomic<quint64> rawTcpWaveRecordCount{0};
    std::atomic<qint64> lastTcpStatusUpdateMs{0};

    std::thread tcpThread;
    std::mutex tcpQueueMutex;
    std::condition_variable tcpQueueCv;
    std::deque<TcpRawRecord> tcpQueue;
    bool tcpWorkerRunning = false;
    quint64 tcpQueueBytes = 0;
    quint64 tcpDroppedCount = 0;
    qint64 lastTcpQueueWarningMs = 0;
};

GroundRecordingService::GroundRecordingService()
    : impl_(std::make_unique<Impl>())
{
}

GroundRecordingService::~GroundRecordingService() = default;

void GroundRecordingService::setSensorSnapshotProvider(SensorSnapshotProvider provider)
{
    impl_->sensorSnapshotProvider = std::move(provider);
}

void GroundRecordingService::setStatusCallback(StatusCallback callback)
{
    impl_->statusCallback = std::move(callback);
}

void GroundRecordingService::setWarningCallback(WarningCallback callback)
{
    impl_->warningCallback = std::move(callback);
}

bool GroundRecordingService::start(const GroundRecordingOptions& options,
                                   GroundRecordingStartError *startError,
                                   QString *errorMessage)
{
    return impl_->start(options, startError, errorMessage);
}

bool GroundRecordingService::pause()
{
    return impl_->pause();
}

GroundRecordingStopSummary GroundRecordingService::stop()
{
    return impl_->stop();
}

GroundRecordingStatus GroundRecordingService::status() const
{
    return impl_->status();
}

bool GroundRecordingService::isSessionOpen() const
{
    return impl_->isSessionOpen();
}

bool GroundRecordingService::isActive() const
{
    return impl_->workerRunning.load();
}

bool GroundRecordingService::isPaused() const
{
    return impl_->paused.load();
}

bool GroundRecordingService::recordRawEpsilonFrame(quint64 hostTimestampUs,
                                                   quint8 packetId,
                                                   quint8 serialNumber,
                                                   const void *data,
                                                   size_t size)
{
    return impl_->recordRaw(impl_->rawEpsilonFile.get(),
                            impl_->rawEpsilonRecordCount,
                            kRawSourceEpsilon,
                            packetId,
                            serialNumber,
                            hostTimestampUs,
                            data,
                            size);
}

bool GroundRecordingService::recordRawPtbResponse(quint64 hostTimestampUs,
                                                  const void *data,
                                                  size_t size)
{
    return impl_->recordRaw(impl_->rawPtbFile.get(),
                            impl_->rawPtbRecordCount,
                            kRawSourcePtb,
                            kRawRecordTypeGeneric,
                            0,
                            hostTimestampUs,
                            data,
                            size);
}

bool GroundRecordingService::recordRawHmpResponse(quint64 hostTimestampUs,
                                                  const void *data,
                                                  size_t size)
{
    return impl_->recordRaw(impl_->rawHmpFile.get(),
                            impl_->rawHmpRecordCount,
                            kRawSourceHmp,
                            0x03u,
                            0,
                            hostTimestampUs,
                            data,
                            size);
}

bool GroundRecordingService::recordRawLidarFrame(quint64 hostTimestampUs,
                                                 quint16 protocol,
                                                 const void *data,
                                                 size_t size)
{
    return impl_->recordRaw(impl_->rawLidarFile.get(),
                            impl_->rawLidarRecordCount,
                            kRawSourceLidar,
                            protocol,
                            0,
                            hostTimestampUs,
                            data,
                            size);
}

bool GroundRecordingService::recordTcpWaveFrame(quint64 hostTimestampUs,
                                                const QByteArray& rawSignalPayload,
                                                const QByteArray& harmonicPayload,
                                                TcpFloatEncoding floatEncoding)
{
    if (!impl_->workerRunning.load() ||
        static_cast<quint64>(rawSignalPayload.size()) > std::numeric_limits<quint32>::max() ||
        static_cast<quint64>(harmonicPayload.size()) > std::numeric_limits<quint32>::max())
    {
        return false;
    }

    QByteArray payload;
    payload.resize(static_cast<qsizetype>(sizeof(quint32) * 2) +
                   rawSignalPayload.size() + harmonicPayload.size());
    char *cursor = payload.data();
    const quint32 rawSize = qToLittleEndian(static_cast<quint32>(rawSignalPayload.size()));
    const quint32 harmonicSize = qToLittleEndian(static_cast<quint32>(harmonicPayload.size()));
    std::memcpy(cursor, &rawSize, sizeof(rawSize));
    cursor += sizeof(rawSize);
    std::memcpy(cursor, &harmonicSize, sizeof(harmonicSize));
    cursor += sizeof(harmonicSize);
    if (!rawSignalPayload.isEmpty())
    {
        std::memcpy(cursor, rawSignalPayload.constData(), static_cast<size_t>(rawSignalPayload.size()));
        cursor += rawSignalPayload.size();
    }
    if (!harmonicPayload.isEmpty())
    {
        std::memcpy(cursor, harmonicPayload.constData(), static_cast<size_t>(harmonicPayload.size()));
    }

    TcpRawRecord record;
    record.timestampUs = hostTimestampUs;
    record.flags = kRawTcpWaveCombinedPayloadFlag | tcpFloatEncodingToRawDatFlags(floatEncoding);
    record.payload = std::move(payload);
    return impl_->enqueueTcpRawRecord(std::move(record));
}

void GroundRecordingService::appendEvent(const QString& level, const QString& message)
{
    impl_->appendEvent(level, message);
}

void GroundRecordingService::appendError(const QString& message)
{
    impl_->appendError(message);
}

quint64 GroundRecordingService::steadyToEpochUs(
    const std::chrono::steady_clock::time_point& timePoint) const
{
    return impl_->steadyToEpochUs(timePoint);
}

quint64 GroundRecordingService::currentTimestampUs()
{
    return static_cast<quint64>(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) * 1000ULL;
}

QString GroundRecordingService::defaultRecordingDirectory()
{
    QDir directory(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 6; ++depth)
    {
        if (QFileInfo::exists(directory.filePath(QStringLiteral("CMakeLists.txt"))) &&
            QFileInfo::exists(directory.filePath(QStringLiteral("README.md"))))
        {
            return directory.filePath(QStringLiteral("data"));
        }
        if (!directory.cdUp())
        {
            break;
        }
    }
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data"));
}

}  // namespace VaporView::Ground::Session
