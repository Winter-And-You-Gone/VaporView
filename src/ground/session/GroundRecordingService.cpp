#include "ground/session/GroundRecordingService.h"

#include "ground/session/RecordingSessionLayout.h"
#include "shared/session/SessionDeviceConfig.h"
#include "shared/session/SessionManifest.h"
#include "shared/session/SessionPackageInitializer.h"
#include "shared/session/SessionPackageLayout.h"
#include "shared/session/SessionSensorCsv.h"
#include "shared/session/UnifiedRawDat.h"
#include "shared/concurrency/BoundedByteQueue.h"
#include "shared/config/SettingsWriteBarrier.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

namespace VaporView::Ground::Session
{
namespace
{

constexpr qint64 kTcpRecordingStatusRefreshMs = 500;
constexpr quint64 kTcpRawRecordQueueWarningBytes = 32ULL * 1024ULL * 1024ULL;
constexpr quint64 kTcpRawRecordQueueMaxBytes = 256ULL * 1024ULL * 1024ULL;
constexpr qint64 kTcpRawRecordQueueWarningIntervalMs = 5000;
constexpr quint64 kDeviceRawRecordQueueWarningBytes = 2ULL * 1024ULL * 1024ULL;
constexpr quint64 kDeviceRawRecordQueueMaxBytes = 8ULL * 1024ULL * 1024ULL;
constexpr qint64 kDeviceRawRecordQueueWarningIntervalMs = 5000;
struct TcpRawRecord
{
    quint64 timestampUs = 0;
    quint32 flags = 0;
    QByteArray payload;
};

struct DeviceRawRecord
{
    quint64 timestampUs = 0;
    quint16 sourceId = 0;
    quint16 recordType = 0;
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
    SessionRawDat::WaveformPayloadLayout layout;
    if (!SessionRawDat::parseWaveformPayloadLayout(
            QByteArrayView(payload).first(std::min(
                payload.size(),
                static_cast<qsizetype>(SessionRawDat::kWaveformPayloadPrefixSize))),
            static_cast<quint32>(payload.size()),
            &layout) ||
        layout.harmonicSize == 0 ||
        layout.harmonicSize % static_cast<quint32>(sizeof(float)) != 0)
    {
        return {};
    }

    return summarizeTcpWavePeakSamples(
        payload.constData() + layout.harmonicOffset,
        static_cast<qsizetype>(layout.harmonicSize),
        tcpFloatEncodingFromRawDatFlags(flags));
}

QString peakValueCsvText(float value)
{
    return std::isfinite(value)
        ? QString::number(static_cast<double>(value), 'g', 9)
        : QString();
}

VaporView::Session::DeviceConnectionConfig sessionDeviceConnectionConfig(
    const GroundRecordingSerialConfig& config)
{
    VaporView::Session::DeviceConnectionConfig result;
    result.port = config.port;
    result.baud = config.baud;
    result.rateHz = config.rateHz;
    return result;
}

QString applicationSoftwareVersion()
{
    return QCoreApplication::applicationVersion().isEmpty()
        ? QStringLiteral("dev")
        : QCoreApplication::applicationVersion();
}

RecordingSessionLayout groundLayoutFromPackage(const VaporView::Session::SessionPackageInitResult& init)
{
    const auto& packageLayout = init.layout;
    RecordingSessionLayout layout;
    layout.sessionName = init.sessionName;
    layout.sessionDirectory = init.sessionDirectory;
    layout.sensorSummaryFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.sensorSummaryCsvPath);
    layout.temperatureControllerFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.temperatureControllerCsvPath);
    layout.waveformFeaturesFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.waveformFeaturesCsvPath);
    layout.navigationRawFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.navigationRawPath);
    layout.pressureRawFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.pressureRawPath);
    layout.temperatureHumidityRawFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.temperatureHumidityRawPath);
    layout.distanceRawFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.distanceRawPath);
    layout.waveformRawFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.waveformRawPath);
    layout.waveformPeaksFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.waveformPeaksCsvPath);
    layout.rawDatDocumentFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.rawFormatDocumentPath);
    layout.sessionMetadataFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.manifestPath);
    layout.eventLogFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.eventLogPath);
    layout.errorLogFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.errorLogPath);
    return layout;
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

        sessionStartTimeUtc = timestampUtc();
        sessionStartTimeUs = GroundRecordingService::currentTimestampUs();

        VaporView::Session::SessionPackageInitOptions initOptions;
        initOptions.origin = VaporView::Session::RecordingOrigin::Ground;
        initOptions.sessionName = sessionName;
        initOptions.outputDirectory = options.baseDirectory;
        initOptions.softwareVersion = applicationSoftwareVersion();
        initOptions.startTimeUtc = sessionStartTimeUtc;
        initOptions.startTimeUs = sessionStartTimeUs;
        initOptions.sensorExportRateHz = options.exportRateHz;
        initOptions.otherDevicesExportRateHz = options.exportRateHz;
        initOptions.waveformExportRateHz = 0;
        initOptions.waveformPointsPerFrame = 50000;
        initOptions.capture.telemetryTransport = QStringLiteral("tcp_wave");
        initOptions.capture.telemetryEndpoint = options.deviceConfig.waveformHost;
        initOptions.capture.telemetryPort = QString::number(options.deviceConfig.waveformPort);
        initOptions.deviceConfig.waveformHost = options.deviceConfig.waveformHost;
        initOptions.deviceConfig.waveformPort = options.deviceConfig.waveformPort;
        initOptions.deviceConfig.epsilon = sessionDeviceConnectionConfig(options.deviceConfig.epsilon);
        initOptions.deviceConfig.ptb = sessionDeviceConnectionConfig(options.deviceConfig.ptb);
        initOptions.deviceConfig.hmp = sessionDeviceConnectionConfig(options.deviceConfig.hmp);
        initOptions.deviceConfig.lidar = sessionDeviceConnectionConfig(options.deviceConfig.lidar);
        initOptions.deviceConfig.temperatureController =
            sessionDeviceConnectionConfig(options.deviceConfig.temperatureController);

        const VaporView::Session::SessionPackageInitResult initResult =
            VaporView::Session::initializeSessionPackage(initOptions);
        if (!initResult.success)
        {
            if (startError) *startError = GroundRecordingStartError::CreateSessionLayout;
            if (errorMessage) *errorMessage = initResult.error;
            return false;
        }
        layout = groundLayoutFromPackage(initResult);

        sensorSummaryFile = std::make_unique<QFile>(layout.sensorSummaryFilename);
        temperatureControllerFile = std::make_unique<QFile>(layout.temperatureControllerFilename);
        waveformFeaturesFile = std::make_unique<QFile>(layout.waveformFeaturesFilename);
        eventLogFile = std::make_unique<QFile>(layout.eventLogFilename);
        errorLogFile = std::make_unique<QFile>(layout.errorLogFilename);
        waveformPeaksFile = std::make_unique<QFile>(layout.waveformPeaksFilename);
        if (!sensorSummaryFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
            !temperatureControllerFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
            !waveformFeaturesFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
            !eventLogFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
            !errorLogFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
            !waveformPeaksFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
            !openRawFile(navigationRawFile, layout.navigationRawFilename, SessionRawDat::kSourceNavigation) ||
            !openRawFile(pressureRawFile, layout.pressureRawFilename, SessionRawDat::kSourcePressure) ||
            !openRawFile(temperatureHumidityRawFile, layout.temperatureHumidityRawFilename, SessionRawDat::kSourceTemperatureHumidity) ||
            !openRawFile(distanceRawFile, layout.distanceRawFilename, SessionRawDat::kSourceDistance) ||
            !openRawFile(waveformRawFile, layout.waveformRawFilename, SessionRawDat::kSourceWaveform))
        {
            if (startError) *startError = GroundRecordingStartError::OpenSessionFiles;
            if (errorMessage) *errorMessage = QStringLiteral("failed to open session files");
            closeFiles();
            resetFiles();
            layout = {};
            return false;
        }

        resetCurrentCounts();
        {
            QTextStream out(eventLogFile.get());
            out.setEncoding(QStringConverter::Utf8);
            out << VaporView::Session::eventLogCsvHeader();
            out.flush();
        }
        {
            QTextStream out(waveformPeaksFile.get());
            out.setEncoding(QStringConverter::Utf8);
            out << VaporView::Session::waveformPeaksCsvHeader();
            out.flush();
        }
        {
            QTextStream out(sensorSummaryFile.get());
            out.setEncoding(QStringConverter::Utf8);
            out << SessionSensorCsv::header();
            out.flush();
        }
        {
            QTextStream out(temperatureControllerFile.get());
            out.setEncoding(QStringConverter::Utf8);
            out << VaporView::Session::temperatureControllerCsvHeader();
            out.flush();
        }
        {
            QTextStream out(waveformFeaturesFile.get());
            out.setEncoding(QStringConverter::Utf8);
            out << VaporView::Session::waveformFeaturesCsvHeader();
            out.flush();
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
        result.rawNavigationRecords = rawNavigationRecordCount.load();
        result.rawPressureRecords = rawPressureRecordCount.load();
        result.rawTemperatureHumidityRecords = rawTemperatureHumidityRecordCount.load();
        result.rawDistanceRecords = rawDistanceRecordCount.load();
        result.rawWaveformRecords = rawWaveformRecordCount.load();
        return result;
    }

    bool isSessionOpen() const
    {
        std::lock_guard<std::mutex> lock(filesMutex);
        return sensorSummaryFile && sensorSummaryFile->isOpen();
    }

    bool recordRaw(std::unique_ptr<QFile>& file,
                   std::atomic<quint64>& recordCount,
                   quint16 sourceId,
                   quint16 recordType,
                   quint32 flags,
                   quint64 timestampUs,
                   const void *data,
                   size_t size)
    {
        Q_UNUSED(file);
        Q_UNUSED(recordCount);
        if (!workerRunning.load() ||
            size > static_cast<size_t>(std::numeric_limits<int>::max()) ||
            size > static_cast<size_t>(std::numeric_limits<quint32>::max()) ||
            (size > 0 && !data))
        {
            return false;
        }

        DeviceRawRecord record;
        record.timestampUs = timestampUs;
        record.sourceId = sourceId;
        record.recordType = recordType;
        record.flags = flags;
        if (size > 0)
        {
            record.payload = QByteArray(reinterpret_cast<const char *>(data), static_cast<int>(size));
        }

        const auto result = deviceRawQueue.push(std::move(record), static_cast<quint64>(size));
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (result.status == VaporView::BoundedByteQueue<DeviceRawRecord>::PushStatus::Full)
        {
            qint64 lastWarningMs = lastDeviceQueueWarningMs.load();
            if ((lastWarningMs <= 0 ||
                 nowMs - lastWarningMs >= kDeviceRawRecordQueueWarningIntervalMs) &&
                lastDeviceQueueWarningMs.compare_exchange_strong(lastWarningMs, nowMs))
            {
                warn(GroundRecordingWarning::DeviceRawQueueFull,
                     result.queuedBytes / (1024ULL * 1024ULL));
            }
            return false;
        }
        if (result.status != VaporView::BoundedByteQueue<DeviceRawRecord>::PushStatus::Enqueued)
        {
            return false;
        }
        qint64 lastWarningMs = lastDeviceQueueWarningMs.load();
        if (result.queuedBytes >= kDeviceRawRecordQueueWarningBytes &&
            (lastWarningMs <= 0 ||
             nowMs - lastWarningMs >= kDeviceRawRecordQueueWarningIntervalMs) &&
            lastDeviceQueueWarningMs.compare_exchange_strong(lastWarningMs, nowMs))
        {
            warn(GroundRecordingWarning::DeviceRawQueueBacklog,
                 result.queuedBytes / (1024ULL * 1024ULL));
        }
        return true;
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

    bool appendEvent(const QString& level, const QString& message)
    {
        std::lock_guard<std::mutex> lock(filesMutex);
        if (!eventLogFile || !eventLogFile->isOpen())
        {
            return false;
        }
        QTextStream out(eventLogFile.get());
        out.setEncoding(QStringConverter::Utf8);
        out << SessionSensorCsv::escape(timestampUtc()) << ','
            << GroundRecordingService::currentTimestampUs() << ','
            << SessionSensorCsv::escape(level) << ','
            << SessionSensorCsv::escape(message) << '\n';
        out.flush();
        const bool ok = out.status() == QTextStream::Ok && eventLogFile->flush();
        if (ok)
        {
            eventRows.fetch_add(1);
        }
        return ok;
    }

    bool appendError(const QString& message)
    {
        std::lock_guard<std::mutex> lock(filesMutex);
        if (!errorLogFile || !errorLogFile->isOpen())
        {
            return false;
        }
        QTextStream out(errorLogFile.get());
        out.setEncoding(QStringConverter::Utf8);
        out << '[' << timestampUtc() << "] " << message << '\n';
        out.flush();
        const bool ok = out.status() == QTextStream::Ok && errorLogFile->flush();
        if (ok)
        {
            errorRows.fetch_add(1);
        }
        return ok;
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

        QString error;
        if (!SessionRawDat::writeFileHeader(*file, sourceId, &error))
        {
            file->close();
            file.reset();
            return false;
        }
        file->flush();
        return true;
    }

    bool writeRawRecord(std::unique_ptr<QFile>& file,
                        std::atomic<quint64>& recordCount,
                        quint16 sourceId,
                        quint16 recordType,
                        quint32 flags,
                        quint64 timestampUs,
                        const void *data,
                        size_t size)
    {
        if ((size > 0 && !data) || size > SessionRawDat::kMaxPayloadSize)
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(filesMutex);
        QFile *rawFile = file.get();
        if (!rawFile || !rawFile->isOpen())
        {
            return false;
        }

        const quint64 sequence = recordCount.load(std::memory_order_relaxed);
        SessionRawDat::RawRecordHeader header;
        header.hostTimestampUs = timestampUs;
        header.sourceId = sourceId;
        header.recordType = recordType;
        header.flags = flags;
        header.sequence = sequence;
        const QByteArrayView payload = size > 0
            ? QByteArrayView(static_cast<const char *>(data), static_cast<qsizetype>(size))
            : QByteArrayView();
        if (!SessionRawDat::writeRecord(*rawFile, header, payload))
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
        startDeviceRawWorker();
        QFile *file = sensorSummaryFile.get();
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
        stopDeviceRawWorker();
        stopTcpWorker();
    }

    void startDeviceRawWorker()
    {
        stopDeviceRawWorker();
        lastDeviceQueueWarningMs.store(0);
        deviceRawQueue.reset(true);
        deviceRawThread = std::thread([this]() {
            DeviceRawRecord record;
            while (deviceRawQueue.waitPop(&record))
            {
                std::unique_ptr<QFile> *file = nullptr;
                std::atomic<quint64> *recordCount = nullptr;
                switch (record.sourceId)
                {
                case SessionRawDat::kSourceNavigation:
                    file = &navigationRawFile;
                    recordCount = &rawNavigationRecordCount;
                    break;
                case SessionRawDat::kSourcePressure:
                    file = &pressureRawFile;
                    recordCount = &rawPressureRecordCount;
                    break;
                case SessionRawDat::kSourceTemperatureHumidity:
                    file = &temperatureHumidityRawFile;
                    recordCount = &rawTemperatureHumidityRecordCount;
                    break;
                case SessionRawDat::kSourceDistance:
                    file = &distanceRawFile;
                    recordCount = &rawDistanceRecordCount;
                    break;
                default:
                    break;
                }

                if (file && recordCount)
                {
                    writeRawRecord(*file,
                                   *recordCount,
                                   record.sourceId,
                                   record.recordType,
                                   record.flags,
                                   record.timestampUs,
                                   record.payload.constData(),
                                   static_cast<size_t>(record.payload.size()));
                }
            }
        });
    }

    void stopDeviceRawWorker()
    {
        deviceRawQueue.close();
        if (deviceRawThread.joinable())
        {
            deviceRawThread.join();
        }
        const quint64 dropped = deviceRawQueue.droppedRecords();
        deviceRawQueue.reset(false);
        lastDeviceQueueWarningMs.store(0);
        if (dropped > 0)
        {
            warn(GroundRecordingWarning::DeviceRawFramesDropped, dropped);
        }
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

                if (writeRawRecord(waveformRawFile,
                                   rawWaveformRecordCount,
                                   SessionRawDat::kSourceWaveform,
                                   SessionRawDat::kRecordTypeWaveformPayload,
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
        if (!waveformPeaksFile || !waveformPeaksFile->isOpen())
        {
            return;
        }
            QTextStream out(waveformPeaksFile.get());
        out.setEncoding(QStringConverter::Utf8);
        out << record.timestampUs << ','
            << peakValueCsvText(summary.value) << ','
            << summary.index << ','
            << summary.pointCount << ",0,0\n";
        out.flush();
    }

    bool writeSessionMetadata(const QString& endTimeUtc = QString(), QString *errorMessage = nullptr) const
    {
        if (layout.sessionMetadataFilename.isEmpty() || layout.sessionDirectory.isEmpty())
        {
            if (errorMessage) *errorMessage = QStringLiteral("session metadata path is empty");
            return false;
        }

        const quint64 endUs = endTimeUtc.isEmpty() ? 0 : GroundRecordingService::currentTimestampUs();
        const quint64 elapsedMs = endUs > 0 && sessionStartTimeUs > 0 && endUs >= sessionStartTimeUs
            ? (endUs - sessionStartTimeUs) / 1000ULL
            : 0;

        VaporView::Session::SessionManifest manifest;
        manifest.recordingOrigin = VaporView::Session::RecordingOrigin::Ground;
        manifest.sessionName = layout.sessionName;
        manifest.state = endTimeUtc.isEmpty()
            ? VaporView::Session::SessionState::Recording
            : VaporView::Session::SessionState::Complete;
        manifest.startTimeUtc = sessionStartTimeUtc;
        manifest.endTimeUtc = endTimeUtc;
        manifest.startTimeUs = sessionStartTimeUs;
        manifest.endTimeUs = endUs;
        manifest.elapsedMs = elapsedMs;
        manifest.softwareVersion = applicationSoftwareVersion();
        manifest.rawDatFormatVersion = static_cast<int>(SessionRawDat::kCurrentFormatVersion);
        manifest.sensorExportRateHz = options.exportRateHz;
        manifest.otherDevicesExportRateHz = options.exportRateHz;
        manifest.waveformExportRateHz = 0;
        manifest.waveformPointsPerFrame = 50000;
        manifest.waveformFileCount = waveformFileCount.load();
        manifest.capture.telemetryTransport = QStringLiteral("tcp_wave");
        manifest.capture.telemetryEndpoint = options.deviceConfig.waveformHost;
        manifest.capture.telemetryPort = QString::number(options.deviceConfig.waveformPort);
        manifest.counts.sensorRows = static_cast<quint64>(std::max<qint64>(0, sensorRows.load()));
        manifest.counts.temperatureControllerRows = 0;
        manifest.counts.waveformFrames = static_cast<quint64>(std::max<qint64>(0, waveformFrames.load()));
        manifest.counts.waveformFeatureRows = 0;
        manifest.counts.eventRows = eventRows.load();
        manifest.counts.errorRows = errorRows.load();
        manifest.rawRecords.navigation = rawNavigationRecordCount.load();
        manifest.rawRecords.pressure = rawPressureRecordCount.load();
        manifest.rawRecords.temperatureHumidity = rawTemperatureHumidityRecordCount.load();
        manifest.rawRecords.distance = rawDistanceRecordCount.load();
        manifest.rawRecords.waveform = rawWaveformRecordCount.load();
        return VaporView::Session::writeSessionManifestAtomically(layout.sessionMetadataFilename,
                                                                  manifest,
                                                                  errorMessage);
    }

    void closeFiles()
    {
        std::lock_guard<std::mutex> lock(filesMutex);
        for (QFile *file : {navigationRawFile.get(),
                            pressureRawFile.get(),
                            temperatureHumidityRawFile.get(),
                            distanceRawFile.get(),
                            waveformRawFile.get(),
                            waveformPeaksFile.get(),
                            sensorSummaryFile.get(),
                            temperatureControllerFile.get(),
                            waveformFeaturesFile.get(),
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
        std::lock_guard<std::mutex> lock(filesMutex);
        sensorSummaryFile.reset();
        temperatureControllerFile.reset();
        waveformFeaturesFile.reset();
        navigationRawFile.reset();
        pressureRawFile.reset();
        temperatureHumidityRawFile.reset();
        distanceRawFile.reset();
        waveformRawFile.reset();
        waveformPeaksFile.reset();
        eventLogFile.reset();
        errorLogFile.reset();
    }

    void resetCurrentCounts()
    {
        sensorRows.store(0);
        waveformFrames.store(0);
        waveformFileCount.store(0);
        rawNavigationRecordCount.store(0);
        rawPressureRecordCount.store(0);
        rawTemperatureHumidityRecordCount.store(0);
        rawDistanceRecordCount.store(0);
        rawWaveformRecordCount.store(0);
        eventRows.store(0);
        errorRows.store(0);
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

    std::unique_ptr<QFile> sensorSummaryFile;
    std::unique_ptr<QFile> navigationRawFile;
    std::unique_ptr<QFile> pressureRawFile;
    std::unique_ptr<QFile> temperatureHumidityRawFile;
    std::unique_ptr<QFile> distanceRawFile;
    std::unique_ptr<QFile> waveformRawFile;
    std::unique_ptr<QFile> waveformPeaksFile;
    std::unique_ptr<QFile> temperatureControllerFile;
    std::unique_ptr<QFile> waveformFeaturesFile;
    std::unique_ptr<QFile> eventLogFile;
    std::unique_ptr<QFile> errorLogFile;

    mutable std::mutex filesMutex;
    std::thread sensorThread;
    std::atomic<bool> workerRunning{false};
    std::atomic<bool> paused{false};
    std::atomic<qint64> sensorRows{0};
    std::atomic<qint64> waveformFrames{0};
    std::atomic<qint64> waveformFileCount{0};
    std::atomic<quint64> rawNavigationRecordCount{0};
    std::atomic<quint64> rawPressureRecordCount{0};
    std::atomic<quint64> rawTemperatureHumidityRecordCount{0};
    std::atomic<quint64> rawDistanceRecordCount{0};
    std::atomic<quint64> rawWaveformRecordCount{0};
    std::atomic<quint64> eventRows{0};
    std::atomic<quint64> errorRows{0};
    std::atomic<qint64> lastTcpStatusUpdateMs{0};

    std::thread deviceRawThread;
    VaporView::BoundedByteQueue<DeviceRawRecord> deviceRawQueue{kDeviceRawRecordQueueMaxBytes};
    std::atomic<qint64> lastDeviceQueueWarningMs{0};

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
    if (VaporView::settingsWritesSuspended())
    {
        if (startError) *startError = GroundRecordingStartError::CreateSessionLayout;
        if (errorMessage) *errorMessage = QStringLiteral("UI test mode blocks recording service file creation.");
        return false;
    }
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
    return impl_->recordRaw(impl_->navigationRawFile,
                            impl_->rawNavigationRecordCount,
                            SessionRawDat::kSourceNavigation,
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
    return impl_->recordRaw(impl_->pressureRawFile,
                            impl_->rawPressureRecordCount,
                            SessionRawDat::kSourcePressure,
                            SessionRawDat::kRecordTypePressureResponse,
                            0,
                            hostTimestampUs,
                            data,
                            size);
}

bool GroundRecordingService::recordRawHmpResponse(quint64 hostTimestampUs,
                                                  const void *data,
                                                  size_t size)
{
    return impl_->recordRaw(impl_->temperatureHumidityRawFile,
                            impl_->rawTemperatureHumidityRecordCount,
                            SessionRawDat::kSourceTemperatureHumidity,
                            SessionRawDat::kRecordTypeTemperatureHumidityModbusResponse,
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
    return impl_->recordRaw(impl_->distanceRawFile,
                            impl_->rawDistanceRecordCount,
                            SessionRawDat::kSourceDistance,
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
    if (!SessionRawDat::encodeWaveformPayload(rawSignalPayload, harmonicPayload, &payload))
    {
        return false;
    }

    TcpRawRecord record;
    record.timestampUs = hostTimestampUs;
    record.flags = SessionRawDat::kWaveformCombinedPayloadFlag | tcpFloatEncodingToRawDatFlags(floatEncoding);
    record.payload = std::move(payload);
    return impl_->enqueueTcpRawRecord(std::move(record));
}

bool GroundRecordingService::appendEvent(const QString& level, const QString& message)
{
    return impl_->appendEvent(level, message);
}

bool GroundRecordingService::appendError(const QString& message)
{
    return impl_->appendError(message);
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
