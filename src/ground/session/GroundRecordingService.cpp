#include "ground/session/GroundRecordingService.h"

#include "ground/session/RecordingSessionLayout.h"
#include "shared/session/SessionManifest.h"
#include "shared/session/SessionPackageInitializer.h"
#include "shared/session/SessionPackageLayout.h"
#include "shared/session/SessionSensorCsv.h"
#include "shared/session/UnifiedRawDat.h"
#include "shared/concurrency/BoundedByteQueue.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
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
    SessionRawDat::TcpWavePayloadLayout layout;
    if (!SessionRawDat::parseTcpWavePayloadLayout(
            QByteArrayView(payload).first(std::min(
                payload.size(),
                static_cast<qsizetype>(SessionRawDat::kTcpWavePayloadPrefixSize))),
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

QJsonObject serialConfigJson(const GroundRecordingSerialConfig& config)
{
    QJsonObject object;
    object[QStringLiteral("port")] = config.port;
    object[QStringLiteral("baud")] = config.baud;
    object[QStringLiteral("rate_hz")] = config.rateHz;
    return object;
}

QString applicationSoftwareVersion()
{
    return QCoreApplication::applicationVersion().isEmpty()
        ? QStringLiteral("dev")
        : QCoreApplication::applicationVersion();
}

QJsonObject groundDeviceConfigJson(const GroundRecordingOptions& options,
                                   const QString& sessionDirectory)
{
    QJsonObject root;
    root[QStringLiteral("recording_directory")] = options.baseDirectory;
    root[QStringLiteral("session_directory")] = sessionDirectory;
    root[QStringLiteral("epsilon_schema_version")] = QStringLiteral("epsilon.v1");
    root[QStringLiteral("sensor_export_rate_hz")] = options.exportRateHz;
    root[QStringLiteral("other_devices_export_rate_hz")] = options.exportRateHz;
    root[QStringLiteral("raw_export_mode")] = QStringLiteral("unified_raw_dat");
    root[QStringLiteral("raw_dat_format_version")] = static_cast<int>(SessionRawDat::kCurrentFormatVersion);
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
    raw[QStringLiteral("directory")] = VaporView::Session::standardSessionPackageLayout().epsilonRawPath.section(QLatin1Char('/'), 0, 0);
    raw[QStringLiteral("format_doc")] = VaporView::Session::standardSessionPackageLayout().rawFormatDocumentPath;
    raw[QStringLiteral("mode")] = QStringLiteral("per_verified_raw_frame_or_response");
    root[QStringLiteral("raw_dat")] = raw;

    QJsonObject sensors;
    sensors[QStringLiteral("epsilon")] = serialConfigJson(options.deviceConfig.epsilon);
    sensors[QStringLiteral("ptb")] = serialConfigJson(options.deviceConfig.ptb);
    sensors[QStringLiteral("hmp")] = serialConfigJson(options.deviceConfig.hmp);
    sensors[QStringLiteral("lidar")] = serialConfigJson(options.deviceConfig.lidar);
    sensors[QStringLiteral("rd105")] = serialConfigJson(options.deviceConfig.temperatureController);
    root[QStringLiteral("sensors")] = sensors;
    return root;
}

RecordingSessionLayout groundLayoutFromPackage(const VaporView::Session::SessionPackageInitResult& init)
{
    const auto& packageLayout = init.layout;
    RecordingSessionLayout layout;
    layout.sessionName = init.sessionName;
    layout.sessionDirectory = init.sessionDirectory;
    layout.sensorsFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.devicesCsvPath);
    layout.temperatureControllerFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.temperatureControllerCsvPath);
    layout.waveformFeaturesFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.waveformFeaturesCsvPath);
    layout.rawEpsilonFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.epsilonRawPath);
    layout.rawPtbFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.ptbRawPath);
    layout.rawHmpFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.hmpRawPath);
    layout.rawLidarFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.lidarRawPath);
    layout.rawTcpWaveFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.tcpWaveRawPath);
    layout.rawTcpWavePeakIndexFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.tcpWavePeaksCsvPath);
    layout.rawDatDocumentFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.rawFormatDocumentPath);
    layout.sessionMetadataFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.manifestPath);
    layout.eventLogFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.eventLogPath);
    layout.errorLogFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.errorLogPath);
    layout.deviceConfigFilename = VaporView::Session::sessionPackageFilePath(init.sessionDirectory, packageLayout.deviceConfigPath);
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
        initOptions.initialDeviceConfig = groundDeviceConfigJson(options, QString());

        const VaporView::Session::SessionPackageInitResult initResult =
            VaporView::Session::initializeSessionPackage(initOptions);
        if (!initResult.success)
        {
            if (startError) *startError = GroundRecordingStartError::CreateSessionLayout;
            if (errorMessage) *errorMessage = initResult.error;
            return false;
        }
        layout = groundLayoutFromPackage(initResult);

        if (!writeDeviceConfigSnapshot())
        {
            warn(GroundRecordingWarning::DeviceConfigSnapshotFailed, 0);
        }

        sensorsFile = std::make_unique<QFile>(layout.sensorsFilename);
        temperatureControllerFile = std::make_unique<QFile>(layout.temperatureControllerFilename);
        waveformFeaturesFile = std::make_unique<QFile>(layout.waveformFeaturesFilename);
        eventLogFile = std::make_unique<QFile>(layout.eventLogFilename);
        errorLogFile = std::make_unique<QFile>(layout.errorLogFilename);
        rawTcpWavePeakIndexFile = std::make_unique<QFile>(layout.rawTcpWavePeakIndexFilename);
        if (!sensorsFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
            !temperatureControllerFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
            !waveformFeaturesFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
            !eventLogFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
            !errorLogFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
            !rawTcpWavePeakIndexFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
            !openRawFile(rawEpsilonFile, layout.rawEpsilonFilename, SessionRawDat::kSourceEpsilon) ||
            !openRawFile(rawPtbFile, layout.rawPtbFilename, SessionRawDat::kSourcePtb) ||
            !openRawFile(rawHmpFile, layout.rawHmpFilename, SessionRawDat::kSourceHmp) ||
            !openRawFile(rawLidarFile, layout.rawLidarFilename, SessionRawDat::kSourceLidar) ||
            !openRawFile(rawTcpWaveFile, layout.rawTcpWaveFilename, SessionRawDat::kSourceTcpWave))
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
            QTextStream out(rawTcpWavePeakIndexFile.get());
            out.setEncoding(QStringConverter::Utf8);
            out << VaporView::Session::tcpWavePeaksCsvHeader();
            out.flush();
        }
        {
            QTextStream out(sensorsFile.get());
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
        eventRows.fetch_add(1);
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
        errorRows.fetch_add(1);
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
                case SessionRawDat::kSourceEpsilon:
                    file = &rawEpsilonFile;
                    recordCount = &rawEpsilonRecordCount;
                    break;
                case SessionRawDat::kSourcePtb:
                    file = &rawPtbFile;
                    recordCount = &rawPtbRecordCount;
                    break;
                case SessionRawDat::kSourceHmp:
                    file = &rawHmpFile;
                    recordCount = &rawHmpRecordCount;
                    break;
                case SessionRawDat::kSourceLidar:
                    file = &rawLidarFile;
                    recordCount = &rawLidarRecordCount;
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

                if (writeRawRecord(rawTcpWaveFile,
                                   rawTcpWaveRecordCount,
                                   SessionRawDat::kSourceTcpWave,
                                   SessionRawDat::kRecordTypeTcpWavePayload,
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
        manifest.rawRecords.epsilon = rawEpsilonRecordCount.load();
        manifest.rawRecords.ptb = rawPtbRecordCount.load();
        manifest.rawRecords.hmp = rawHmpRecordCount.load();
        manifest.rawRecords.lidar = rawLidarRecordCount.load();
        manifest.rawRecords.tcpWave = rawTcpWaveRecordCount.load();
        return VaporView::Session::writeSessionManifestAtomically(layout.sessionMetadataFilename,
                                                                  manifest,
                                                                  errorMessage);
    }

    bool writeDeviceConfigSnapshot() const
    {
        return writeJsonFileAtomically(layout.deviceConfigFilename,
                                       groundDeviceConfigJson(options, layout.sessionDirectory),
                                       nullptr);
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
        sensorsFile.reset();
        temperatureControllerFile.reset();
        waveformFeaturesFile.reset();
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

    std::unique_ptr<QFile> sensorsFile;
    std::unique_ptr<QFile> rawEpsilonFile;
    std::unique_ptr<QFile> rawPtbFile;
    std::unique_ptr<QFile> rawHmpFile;
    std::unique_ptr<QFile> rawLidarFile;
    std::unique_ptr<QFile> rawTcpWaveFile;
    std::unique_ptr<QFile> rawTcpWavePeakIndexFile;
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
    std::atomic<quint64> rawEpsilonRecordCount{0};
    std::atomic<quint64> rawPtbRecordCount{0};
    std::atomic<quint64> rawHmpRecordCount{0};
    std::atomic<quint64> rawLidarRecordCount{0};
    std::atomic<quint64> rawTcpWaveRecordCount{0};
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
    return impl_->recordRaw(impl_->rawEpsilonFile,
                            impl_->rawEpsilonRecordCount,
                            SessionRawDat::kSourceEpsilon,
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
    return impl_->recordRaw(impl_->rawPtbFile,
                            impl_->rawPtbRecordCount,
                            SessionRawDat::kSourcePtb,
                            SessionRawDat::kRecordTypePtbResponse,
                            0,
                            hostTimestampUs,
                            data,
                            size);
}

bool GroundRecordingService::recordRawHmpResponse(quint64 hostTimestampUs,
                                                  const void *data,
                                                  size_t size)
{
    return impl_->recordRaw(impl_->rawHmpFile,
                            impl_->rawHmpRecordCount,
                            SessionRawDat::kSourceHmp,
                            SessionRawDat::kRecordTypeHmpModbusResponse,
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
    return impl_->recordRaw(impl_->rawLidarFile,
                            impl_->rawLidarRecordCount,
                            SessionRawDat::kSourceLidar,
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
    if (!SessionRawDat::encodeTcpWavePayload(rawSignalPayload, harmonicPayload, &payload))
    {
        return false;
    }

    TcpRawRecord record;
    record.timestampUs = hostTimestampUs;
    record.flags = SessionRawDat::kTcpWaveCombinedPayloadFlag | tcpFloatEncodingToRawDatFlags(floatEncoding);
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
