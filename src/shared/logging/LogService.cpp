#include "LogService.h"
#include "logging/BoundedLogRecord.h"
#include "logging/LogQueuePolicy.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMetaType>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QWaitCondition>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace VaporView
{

namespace
{
constexpr qint64 kMaxFileBytes = 10LL * 1024LL * 1024LL;
constexpr qint64 kMaxTotalFileBytes = 100LL * 1024LL * 1024LL;
constexpr int kMaxRetainedFiles = 10;
constexpr int kMaxRotatedFiles = kMaxRetainedFiles - 1;
constexpr qint64 kDropReportPeriodMilliseconds = 5000;
constexpr qint64 kFlushPeriodMilliseconds = 250;
constexpr qsizetype kFlushRecordLimit = 256;
constexpr qsizetype kMaxProcessCaptureBytes = 1 * 1024 * 1024;
constexpr qsizetype kMaxProcessLineBytes = 64 * 1024;
constexpr char kProcessStdoutProperty[] = "_vaporview_logged_stdout";
constexpr char kProcessStderrProperty[] = "_vaporview_logged_stderr";
constexpr char kProcessStdoutLineBufferProperty[] = "_vaporview_logged_stdout_line_buffer";
constexpr char kProcessStderrLineBufferProperty[] = "_vaporview_logged_stderr_line_buffer";

QMutex instance_mutex;
QWaitCondition instance_condition;
int active_instance_accesses = 0;
bool instance_shutting_down = false;
// Lifecycle state is released before publish, file IO, signal emission, or any
// external callback. LogWriterThread likewise never nests its queue mutex with
// its file mutex: queue state is selected first, then IO runs after unlock.

bool isHighPriority(LogLevel level)
{
    return level >= LogLevel::Warning;
}

void fallbackWrite(const QByteArray& line)
{
#ifdef Q_OS_WIN
    const QString text = QString::fromUtf8(line);
    OutputDebugStringW(reinterpret_cast<LPCWSTR>(text.utf16()));
    if (!line.endsWith('\n'))
    {
        OutputDebugStringW(L"\n");
    }
#endif
    std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stderr);
    if (!line.endsWith('\n'))
    {
        std::fputc('\n', stderr);
    }
    std::fflush(stderr);
}

QString rotatedPath(const QString& currentPath, int index)
{
    return QStringLiteral("%1.%2").arg(currentPath).arg(index);
}

QString defaultFallbackLogDirectory()
{
#ifdef Q_OS_WIN
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    return localAppData.isEmpty()
        ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
              .filePath(QStringLiteral("logs"))
        : QDir(localAppData).filePath(QStringLiteral("VaporView/logs"));
#else
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("logs"));
#endif
}

quint64 currentTimestampUsNow()
{
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch());
    return static_cast<quint64>(elapsed / std::chrono::microseconds(1));
}

void appendBounded(QByteArray& target, const QByteArray& bytes, qsizetype limit)
{
    target.append(bytes);
    if (target.size() > limit)
    {
        target.remove(0, target.size() - limit);
    }
}

void publishProcessLine(QProcess *process,
                        const QString& source,
                        const QString& category,
                        const QByteArray& bytes,
                        bool standardError,
                        bool partial)
{
    QByteArray line = bytes;
    if (line.endsWith('\r'))
    {
        line.chop(1);
    }
    if (line.isEmpty())
    {
        return;
    }
    LogService::withCurrentInstance([&](LogService& logService) {
        QVariantMap fields = {
            {QStringLiteral("event"), QStringLiteral("child_process_output")},
            {QStringLiteral("stream"), standardError ? QStringLiteral("stderr")
                                                       : QStringLiteral("stdout")},
            {QStringLiteral("process_output"), QString::fromLocal8Bit(line)},
            {QStringLiteral("raw_bytes"), line.size()}};
        if (partial)
        {
            fields.insert(QStringLiteral("partial"), true);
        }
        logService.publish(standardError ? LogLevel::Warning : LogLevel::Debug,
                           source,
                           category,
                           standardError
                               ? QStringLiteral("已收到子进程错误输出。")
                               : QStringLiteral("已收到子进程标准输出。"),
                           fields);
    });
    Q_UNUSED(process);
}

void consumeProcessOutput(QProcess *process,
                          const QString& source,
                          const QString& category,
                          const QByteArray& bytes,
                          bool standardError)
{
    if (!process || bytes.isEmpty())
    {
        return;
    }

    const char *captureProperty = standardError ? kProcessStderrProperty : kProcessStdoutProperty;
    QByteArray captured = process->property(captureProperty).toByteArray();
    appendBounded(captured, bytes, kMaxProcessCaptureBytes);
    process->setProperty(captureProperty, captured);

    const char *lineProperty = standardError
        ? kProcessStderrLineBufferProperty
        : kProcessStdoutLineBufferProperty;
    QByteArray pending = process->property(lineProperty).toByteArray();
    pending.append(bytes);
    while (true)
    {
        const qsizetype newline = pending.indexOf('\n');
        if (newline < 0)
        {
            break;
        }
        publishProcessLine(process,
                           source,
                           category,
                           pending.left(newline),
                           standardError,
                           false);
        pending.remove(0, newline + 1);
    }
    while (pending.size() > kMaxProcessLineBytes)
    {
        publishProcessLine(process,
                           source,
                           category,
                           pending.left(kMaxProcessLineBytes),
                           standardError,
                           true);
        pending.remove(0, kMaxProcessLineBytes);
    }
    process->setProperty(lineProperty, pending);
}

void flushProcessOutput(QProcess *process,
                        const QString& source,
                        const QString& category,
                        bool standardError)
{
    if (!process)
    {
        return;
    }
    const char *lineProperty = standardError
        ? kProcessStderrLineBufferProperty
        : kProcessStdoutLineBufferProperty;
    const QByteArray pending = process->property(lineProperty).toByteArray();
    publishProcessLine(process, source, category, pending, standardError, true);
    process->setProperty(lineProperty, QByteArray());
}

}  // namespace

class LogWriterThread final : public QThread
{
public:
    using FailureCallback = std::function<void(const QString&)>;
    using SequenceCallback = std::function<quint64()>;
    using CompletionState = LoggingInternal::LogCompletionState;
    using QueuedRecord = LoggingInternal::QueuedLogRecord;

    LogWriterThread(QString directory,
                    QString applicationName,
                    QString fallbackDirectory,
                    FailureCallback failureCallback,
                    SequenceCallback sequenceCallback)
        : directory_(std::move(directory)), application_name_(std::move(applicationName)),
          fallback_directory_(std::move(fallbackDirectory)),
          failure_callback_(std::move(failureCallback)),
          sequence_callback_(std::move(sequenceCallback))
    {
    }

    ~LogWriterThread() override
    {
        stop();
    }

    bool enqueue(LogRecord& record, std::shared_ptr<CompletionState> completion = {})
    {
        QMutexLocker locker(&mutex_);
        if (stopping_)
        {
            complete(completion, false);
            return false;
        }

        if (record.sequence == 0)
        {
            record.sequence = sequence_callback_ ? sequence_callback_() : 0;
        }

        LoggingInternal::QueueEnqueueResult result = queue_.enqueue({record, completion});
        dropped_count_ += result.dropped_increment;
        const std::shared_ptr<CompletionState> evictedCompletion =
            result.evicted ? result.evicted->completion : nullptr;

        if (result.action == LoggingInternal::QueueEnqueueAction::DroppedIncoming)
        {
            wake_.wakeOne();
            locker.unlock();
            complete(evictedCompletion, false);
            complete(completion, false);
            return false;
        }
        if (result.action == LoggingInternal::QueueEnqueueAction::EmergencyWrite)
        {
            std::optional<LogRecord> overloadNotice = std::nullopt;
            if (result.critical_overload_started)
            {
                overloadNotice = LoggingInternal::makeCriticalOverloadNotice(
                    sequence_callback_ ? sequence_callback_() : 0,
                    currentTimestampUsNow(),
                    QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
                    static_cast<quint64>(QCoreApplication::applicationPid()),
                    static_cast<quint64>(reinterpret_cast<quintptr>(QThread::currentThreadId())),
                    result.pending_critical_limit);
            }
            locker.unlock();
            complete(evictedCompletion, false);
            const bool success = emergencyWrite(record, overloadNotice);
            complete(completion, success);
            return success;
        }

        wake_.wakeOne();
        locker.unlock();
        complete(evictedCompletion, false);
        return true;
    }

    void stop()
    {
        {
            QMutexLocker locker(&mutex_);
            if (!isRunning())
            {
                return;
            }
            stopping_ = true;
#ifdef VAPORVIEW_LOGGING_TEST_HOOKS
            test_block_requested_ = false;
            test_block_condition_.wakeAll();
#endif
            wake_.wakeAll();
        }
        if (QThread::currentThread() == this)
        {
            return;
        }
        wait();
    }

    bool emergencyWrite(const LogRecord& record,
                        const std::optional<LogRecord>& overloadNotice = std::nullopt)
    {
        const QByteArray recordLine = LoggingInternal::serializePreparedLogRecord(record);
        fallbackWrite(recordLine);
        QByteArray noticeLine;
        if (overloadNotice)
        {
            noticeLine = LoggingInternal::serializePreparedLogRecord(*overloadNotice);
            fallbackWrite(noticeLine);
        }

        QMutexLocker locker(&file_mutex_);
        const bool recordWritten = writeEmergencyLine(recordLine);
        const bool noticeWritten = !overloadNotice || writeEmergencyLine(noticeLine);
        return recordWritten && noticeWritten;
    }

#ifdef VAPORVIEW_LOGGING_TEST_HOOKS
    void setBlockedForTest(bool blocked)
    {
        QMutexLocker locker(&mutex_);
        test_block_requested_ = blocked;
        if (!blocked)
        {
            test_block_condition_.wakeAll();
        }
        wake_.wakeAll();
    }

    bool waitUntilBlockedForTest(std::chrono::milliseconds timeout)
    {
        const QDeadlineTimer deadline(timeout);
        QMutexLocker locker(&mutex_);
        while (!test_writer_blocked_ && !deadline.hasExpired())
        {
            test_block_condition_.wait(&mutex_, deadline);
        }
        return test_writer_blocked_;
    }

    bool setMaxPendingCriticalForTest(qsizetype limit)
    {
        QMutexLocker locker(&mutex_);
        return queue_.setMaxPendingCriticalForTest(limit);
    }

    QVariantMap stateForTest() const
    {
        QMutexLocker locker(&mutex_);
        const LoggingInternal::LogQueueStats stats = queue_.stats();
        return {{QStringLiteral("size"), static_cast<qlonglong>(stats.size)},
                {QStringLiteral("pending_critical"), static_cast<qlonglong>(stats.pending_critical)},
                {QStringLiteral("max_observed_size"),
                 static_cast<qlonglong>(stats.max_observed_size)},
                {QStringLiteral("priority_index_lookups"),
                 static_cast<qulonglong>(stats.priority_index_lookups)},
                {QStringLiteral("critical_overload_active"), stats.critical_overload_active}};
    }

    QString emergencyFilePathForTest() const
    {
        QMutexLocker locker(&file_mutex_);
        return emergency_path_;
    }
#endif

protected:
    void run() override
    {
        while (true)
        {
            std::optional<QueuedRecord> item = std::nullopt;
            bool idleFlush = false;
            {
                QMutexLocker locker(&mutex_);
                while (queue_.empty() && !stopping_)
                {
#ifdef VAPORVIEW_LOGGING_TEST_HOOKS
                    while (test_block_requested_ && !stopping_)
                    {
                        test_writer_blocked_ = true;
                        test_block_condition_.wakeAll();
                        test_block_condition_.wait(&mutex_);
                    }
                    test_writer_blocked_ = false;
                    test_block_condition_.wakeAll();
                    if (stopping_)
                    {
                        break;
                    }
                    if (!queue_.empty())
                    {
                        break;
                    }
#endif
                    qint64 waitMs = -1;
                    if (pending_flush_.load(std::memory_order_acquire))
                    {
                        waitMs = kFlushPeriodMilliseconds;
                    }
                    if (dropped_count_ > 0)
                    {
                        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                        const qint64 dropWaitMs = last_drop_report_ms_ == 0
                            ? 0
                            : (std::max)(0LL,
                                         kDropReportPeriodMilliseconds -
                                             (nowMs - last_drop_report_ms_));
                        waitMs = waitMs < 0 ? dropWaitMs : (std::min)(waitMs, dropWaitMs);
                    }
                    if (waitMs == 0)
                    {
                        break;
                    }
                    const bool signaled = waitMs < 0
                        ? wake_.wait(&mutex_)
                        : wake_.wait(&mutex_, static_cast<unsigned long>(waitMs));
                    if (!signaled && pending_flush_.load(std::memory_order_acquire))
                    {
                        idleFlush = true;
                        break;
                    }
                }

#ifdef VAPORVIEW_LOGGING_TEST_HOOKS
                while (test_block_requested_ && !stopping_)
                {
                    test_writer_blocked_ = true;
                    test_block_condition_.wakeAll();
                    test_block_condition_.wait(&mutex_);
                }
                test_writer_blocked_ = false;
                test_block_condition_.wakeAll();
#endif

                if (queue_.empty())
                {
                    queueDropNoticeIfDueLocked(stopping_);
                }
                if (!queue_.empty())
                {
                    item = queue_.takeNext();
                    // The pop creates room for a control record. Appending the
                    // notice at the tail preserves FIFO/sequence ordering.
                    queueDropNoticeIfDueLocked(false);
                }
                else if (stopping_)
                {
                    break;
                }
            }

            if (idleFlush)
            {
                QMutexLocker fileLocker(&file_mutex_);
                flushPending();
            }
            if (item)
            {
                bool success = false;
                {
                    QMutexLocker fileLocker(&file_mutex_);
                    success = writeRecord(item->record, item->completion != nullptr);
                }
                {
                    QMutexLocker locker(&mutex_);
                    queue_.markProcessed(item->record.level);
                    wake_.wakeAll();
                }
                complete(item->completion, success);
            }
        }

        QMutexLocker fileLocker(&file_mutex_);
        flushPending();
        closeFile();
    }

private:
    static void complete(const std::shared_ptr<CompletionState>& completion, bool success)
    {
        if (!completion)
        {
            return;
        }
        QMutexLocker locker(&completion->mutex);
        completion->success = success;
        completion->completed = true;
        completion->condition.wakeAll();
    }

    void queueDropNoticeIfDueLocked(bool force)
    {
        if (dropped_count_ == 0 ||
            queue_.size() >= LoggingInternal::kLogQueueCapacity)
        {
            return;
        }
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (!force && last_drop_report_ms_ != 0 &&
            nowMs - last_drop_report_ms_ < kDropReportPeriodMilliseconds)
        {
            return;
        }

        const quint64 dropped = std::exchange(dropped_count_, 0ULL);
        last_drop_report_ms_ = nowMs;
        LogRecord notice = LoggingInternal::makeDropNotice(
            dropped,
            sequence_callback_ ? sequence_callback_() : 0,
            currentTimestampUsNow(),
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
            static_cast<quint64>(QCoreApplication::applicationPid()),
            static_cast<quint64>(reinterpret_cast<quintptr>(QThread::currentThreadId())));
        const LoggingInternal::QueueEnqueueResult result =
            queue_.enqueue({std::move(notice), {}});
        if (result.action != LoggingInternal::QueueEnqueueAction::Enqueued)
        {
            dropped_count_ += dropped;
        }
    }

    bool rotateEmergencyIfNeeded(const QString& path, qint64 additionalBytes)
    {
        const QFileInfo active(path);
        if (!active.exists() || active.size() + additionalBytes <= kMaxFileBytes)
        {
            return true;
        }
        for (int index = kMaxRotatedFiles - 1; index >= 1; --index)
        {
            const QString source = rotatedPath(path, index);
            const QString target = rotatedPath(path, index + 1);
            if (!QFile::exists(source))
            {
                continue;
            }
            if (QFile::exists(target) && !QFile::remove(target))
            {
                return false;
            }
            if (!QFile::rename(source, target))
            {
                return false;
            }
        }
        const QString firstRotation = rotatedPath(path, 1);
        if (QFile::exists(firstRotation) && !QFile::remove(firstRotation))
        {
            return false;
        }
        return !QFile::exists(path) || QFile::rename(path, firstRotation);
    }

    bool writeEmergencyLine(const QByteArray& line)
    {
        QStringList candidateDirectories = {directory_};
        if (!fallback_directory_.isEmpty() &&
            QDir::cleanPath(fallback_directory_) != QDir::cleanPath(directory_))
        {
            candidateDirectories.push_back(fallback_directory_);
        }

        for (const QString& candidate : candidateDirectories)
        {
            if (!QDir().mkpath(candidate))
            {
                continue;
            }
            const QString path = QDir(candidate).filePath(
                QStringLiteral("%1-emergency-%2.jsonl")
                    .arg(application_name_,
                         QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"))));
            if (!rotateEmergencyIfNeeded(path, line.size()))
            {
                continue;
            }
            QFile emergencyFile(path);
            if (!emergencyFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
            {
                continue;
            }
            const qint64 originalSize = emergencyFile.size();
            const qint64 written = emergencyFile.write(line);
            const bool completeWrite = written == line.size();
            const bool flushed = completeWrite && emergencyFile.flush();
            if (!flushed && originalSize >= 0)
            {
                emergencyFile.resize(originalSize);
            }
            emergencyFile.close();
            if (flushed)
            {
                emergency_path_ = path;
                return true;
            }
        }
        return false;
    }

    bool openFile()
    {
        const QString date = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
        const QString desiredPath = QDir(directory_).filePath(
            QStringLiteral("%1-%2.jsonl").arg(application_name_).arg(date));
        if (file_.isOpen() && current_path_ == desiredPath)
        {
            return true;
        }
        if (file_.isOpen())
        {
            closeFile();
            cleanup_complete_ = false;
        }
        if (!cleanup_complete_)
        {
            cleanupOldFiles(desiredPath);
            cleanup_complete_ = true;
        }
        if (!QDir().mkpath(directory_))
        {
            notifyFailure(QStringLiteral("无法创建应用日志目录。路径：%1").arg(directory_));
            return false;
        }
        current_path_ = desiredPath;
        file_.setFileName(current_path_);
        if (!file_.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        {
            current_path_.clear();
            notifyFailure(QStringLiteral("无法打开应用日志文件。目录：%1").arg(directory_));
            return false;
        }
        last_flush_ms_ = 0;
        pending_records_ = 0;
        return true;
    }

    void cleanupOldFiles(const QString& activePath)
    {
        QDir directory(directory_);
        if (!directory.exists())
        {
            return;
        }
        const QStringList patterns{
            QStringLiteral("%1-*.jsonl").arg(application_name_),
            QStringLiteral("%1-*.jsonl.*").arg(application_name_)};
        QFileInfoList files = directory.entryInfoList(patterns,
                                                       QDir::Files | QDir::Readable,
                                                       QDir::NoSort);
        const QString activeAbsolutePath = QFileInfo(activePath).absoluteFilePath();
        const QFileInfo activeInfo(activeAbsolutePath);
        const qint64 activeBytes = activeInfo.exists() ? activeInfo.size() : 0;
        std::sort(files.begin(), files.end(), [](const QFileInfo& left, const QFileInfo& right) {
            const qint64 leftTime = left.lastModified().toMSecsSinceEpoch();
            const qint64 rightTime = right.lastModified().toMSecsSinceEpoch();
            if (leftTime != rightTime)
            {
                return leftTime > rightTime;
            }
            return left.absoluteFilePath() > right.absoluteFilePath();
        });
        qint64 retainedBytes = activeBytes;
        const int maxOtherFiles = (std::max)(0, kMaxRetainedFiles - 1);
        const qint64 reservedActiveBytes = (std::max)(activeBytes, kMaxFileBytes);
        const qint64 remainingBytes = reservedActiveBytes < kMaxTotalFileBytes
            ? kMaxTotalFileBytes - reservedActiveBytes
            : 0;
        int retainedOtherFiles = 0;
        qint64 retainedOtherBytes = 0;
        for (const QFileInfo& fileInfo : files)
        {
            if (fileInfo.absoluteFilePath() == activeAbsolutePath)
            {
                continue;
            }
            const bool keep = retainedOtherFiles < maxOtherFiles &&
                retainedOtherBytes + fileInfo.size() <= remainingBytes &&
                retainedBytes + fileInfo.size() <= kMaxTotalFileBytes;
            if (keep)
            {
                ++retainedOtherFiles;
                retainedOtherBytes += fileInfo.size();
                retainedBytes += fileInfo.size();
                continue;
            }
            QFile::remove(fileInfo.absoluteFilePath());
        }
    }

    void rotateIfNeeded(qint64 additionalBytes)
    {
        if (!file_.isOpen() || file_.size() + additionalBytes <= kMaxFileBytes)
        {
            return;
        }
        closeFile();
        for (int index = kMaxRotatedFiles - 1; index >= 1; --index)
        {
            const QString source = rotatedPath(current_path_, index);
            const QString target = rotatedPath(current_path_, index + 1);
            if (QFile::exists(source))
            {
                QFile::remove(target);
                QFile::rename(source, target);
            }
        }
        if (QFile::exists(current_path_))
        {
            QFile::remove(rotatedPath(current_path_, 1));
            QFile::rename(current_path_, rotatedPath(current_path_, 1));
        }
        cleanup_complete_ = false;
        openFile();
    }

    bool switchToFallbackDirectory()
    {
        if (fallback_directory_.isEmpty() ||
            QDir::cleanPath(directory_) == QDir::cleanPath(fallback_directory_))
        {
            return false;
        }
        closeFile();
        directory_ = fallback_directory_;
        current_path_.clear();
        cleanup_complete_ = false;
        return true;
    }

    bool writeRecordOnce(const LogRecord& record,
                         const QByteArray& line,
                         bool forceFlush)
    {
        if (!openFile())
        {
            return false;
        }
        rotateIfNeeded(line.size());
        if (!file_.isOpen())
        {
            return false;
        }
        const qint64 originalSize = file_.size();
        if (file_.write(line) != line.size())
        {
            if (originalSize >= 0)
            {
                file_.resize(originalSize);
                file_.seek(originalSize);
            }
            return false;
        }
        ++pending_records_;
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const bool due = forceFlush || isHighPriority(record.level) ||
            last_flush_ms_ == 0 || nowMs - last_flush_ms_ >= kFlushPeriodMilliseconds ||
            pending_records_ >= kFlushRecordLimit;
        if (!due)
        {
            pending_flush_.store(true, std::memory_order_release);
            return true;
        }
        if (!file_.flush())
        {
            return false;
        }
        last_flush_ms_ = nowMs;
        pending_records_ = 0;
        pending_flush_.store(false, std::memory_order_release);
        return true;
    }

    bool writeRecord(const LogRecord& record, bool forceFlush)
    {
        const QByteArray line = LoggingInternal::serializePreparedLogRecord(record);
        if (writeRecordOnce(record, line, forceFlush))
        {
            return true;
        }

        notifyFailure(QStringLiteral("无法写入应用日志文件。路径：%1").arg(current_path_));
        if (switchToFallbackDirectory() && writeRecordOnce(record, line, forceFlush))
        {
            return true;
        }
        fallbackWrite(line);
        return false;
    }

    void flushPending()
    {
        if (!file_.isOpen() || !pending_flush_.load(std::memory_order_acquire))
        {
            return;
        }
        if (!file_.flush())
        {
            notifyFailure(QStringLiteral("无法刷新应用日志文件。路径：%1").arg(current_path_));
            return;
        }
        last_flush_ms_ = QDateTime::currentMSecsSinceEpoch();
        pending_records_ = 0;
        pending_flush_.store(false, std::memory_order_release);
    }

    void notifyFailure(const QString& message)
    {
        if (failure_reported_ || !failure_callback_)
        {
            return;
        }
        failure_reported_ = true;
        failure_callback_(message);
    }

    void closeFile()
    {
        if (file_.isOpen())
        {
            if (!file_.flush())
            {
                notifyFailure(QStringLiteral("无法刷新应用日志文件。路径：%1").arg(current_path_));
            }
            file_.close();
        }
        last_flush_ms_ = 0;
        pending_records_ = 0;
        pending_flush_.store(false, std::memory_order_release);
    }

public:
    QString activeDirectory() const
    {
        QMutexLocker locker(&file_mutex_);
        return directory_;
    }

    QString activeFilePath() const
    {
        QMutexLocker locker(&file_mutex_);
        if (!current_path_.isEmpty())
        {
            return current_path_;
        }
        const QString date = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
        return QDir(directory_).filePath(
            QStringLiteral("%1-%2.jsonl").arg(application_name_).arg(date));
    }

private:
    // mutex_ protects queue_, dropped/stop state, Critical accounting, and test
    // wake state. file_mutex_ protects QFile, active paths, rotation, cleanup,
    // and emergency output. The two locks are intentionally never nested.
    QString directory_;
    QString application_name_;
    QString fallback_directory_;
    QString current_path_;
    QString emergency_path_;
    QFile file_;
    FailureCallback failure_callback_;
    SequenceCallback sequence_callback_;
    bool failure_reported_ = false;
    bool cleanup_complete_ = false;
    qint64 last_flush_ms_ = 0;
    qsizetype pending_records_ = 0;
    mutable QMutex mutex_;
    mutable QMutex file_mutex_;
    QWaitCondition wake_;
    LoggingInternal::LogRecordQueue queue_;
    quint64 dropped_count_ = 0;
    qint64 last_drop_report_ms_ = 0;
    bool stopping_ = false;
    std::atomic_bool pending_flush_ = false;
#ifdef VAPORVIEW_LOGGING_TEST_HOOKS
    QWaitCondition test_block_condition_;
    bool test_block_requested_ = false;
    bool test_writer_blocked_ = false;
#endif
};

LogService *LogService::instance_ = nullptr;
QtMessageHandler LogService::previous_message_handler_ = nullptr;

QString logLevelName(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Debug: return QStringLiteral("Debug");
    case LogLevel::Info: return QStringLiteral("Info");
    case LogLevel::Warning: return QStringLiteral("Warning");
    case LogLevel::Error: return QStringLiteral("Error");
    case LogLevel::Critical: return QStringLiteral("Critical");
    }
    return QStringLiteral("Info");
}

LogLevel logLevelFromName(const QString& name)
{
    const QString normalized = name.trimmed().toLower();
    if (normalized == QStringLiteral("debug")) return LogLevel::Debug;
    if (normalized == QStringLiteral("warning") || normalized == QStringLiteral("warn")) return LogLevel::Warning;
    if (normalized == QStringLiteral("error")) return LogLevel::Error;
    if (normalized == QStringLiteral("critical")) return LogLevel::Critical;
    return LogLevel::Info;
}

QJsonObject LogRecord::toJsonObject() const
{
    QJsonObject object;
    object.insert(QStringLiteral("schema_version"), schema_version);
    object.insert(QStringLiteral("timestamp_utc"), timestamp_utc);
    object.insert(QStringLiteral("timestamp_us"), static_cast<qint64>(timestamp_us));
    object.insert(QStringLiteral("level"), logLevelName(level));
    object.insert(QStringLiteral("source"), source);
    object.insert(QStringLiteral("category"), category);
    object.insert(QStringLiteral("process_id"), static_cast<qint64>(process_id));
    object.insert(QStringLiteral("thread_id"), static_cast<qint64>(thread_id));
    object.insert(QStringLiteral("sequence"), static_cast<qint64>(sequence));
    object.insert(QStringLiteral("correlation_id"), correlation_id);
    object.insert(QStringLiteral("session_id"), session_id);
    object.insert(QStringLiteral("message"), message);
    object.insert(QStringLiteral("fields"), QJsonObject::fromVariantMap(fields));
    return object;
}

QByteArray LogRecord::toJsonLine() const
{
    return LoggingInternal::serializeBoundedLogRecord(*this);
}

LogService::LogService(const QString& applicationName,
                       QObject *parent,
                       const QString& logDirectoryOverride,
                       const QString& fallbackDirectoryOverride)
    : QObject(parent),
      application_name_(applicationName),
      log_directory_(logDirectoryOverride.trimmed().isEmpty()
                         ? chooseLogDirectory(applicationName)
                         : logDirectoryOverride)
{
    qRegisterMetaType<LogRecord>("VaporView::LogRecord");
    writer_ = std::make_unique<LogWriterThread>(
        log_directory_,
        application_name_,
        fallbackDirectoryOverride.trimmed().isEmpty()
            ? defaultFallbackLogDirectory()
            : fallbackDirectoryOverride,
        [this](const QString& message) {
            QMetaObject::invokeMethod(this,
                                      [this, message]() { emit writerFailureReported(message); },
                                      Qt::QueuedConnection);
        },
        [this]() { return nextSequence(); });
    writer_->start();
    {
        QMutexLocker locker(&instance_mutex);
        if (!instance_ && !instance_shutting_down)
        {
            instance_ = this;
            owns_global_instance_ = true;
            instance_shutting_down = false;
        }
    }
    publish(LogLevel::Info, QStringLiteral("App"), QStringLiteral("lifecycle"),
            QStringLiteral("应用日志系统已启动。"),
            {{QStringLiteral("event"), QStringLiteral("logging_started")},
             {QStringLiteral("log_directory"), log_directory_}});
}

LogService::~LogService()
{
    QtMessageHandler previousHandler = nullptr;
    bool restoreHandler = false;
    {
        QMutexLocker locker(&instance_mutex);
        if (owns_global_instance_ && instance_ == this)
        {
            instance_shutting_down = true;
            instance_ = nullptr;
            instance_condition.wakeAll();
            previousHandler = previous_message_handler_;
            previous_message_handler_ = nullptr;
            restoreHandler = qt_message_handler_installed_;
            qt_message_handler_installed_ = false;
        }
    }
    if (restoreHandler)
    {
        qInstallMessageHandler(previousHandler);
    }
    if (owns_global_instance_)
    {
        QMutexLocker locker(&instance_mutex);
        while (active_instance_accesses > 0)
        {
            instance_condition.wait(&instance_mutex);
        }
    }

    publish(LogLevel::Info, QStringLiteral("App"), QStringLiteral("lifecycle"),
            QStringLiteral("应用日志系统已停止。"),
            {{QStringLiteral("event"), QStringLiteral("logging_stopped")}});
    if (writer_)
    {
        writer_->stop();
    }
    if (owns_global_instance_)
    {
        QMutexLocker locker(&instance_mutex);
        instance_shutting_down = false;
        instance_condition.wakeAll();
    }
}

LogService *LogService::instance()
{
    QMutexLocker locker(&instance_mutex);
    return instance_;
}

bool LogService::withCurrentInstance(const std::function<void(LogService&)>& callback)
{
    if (!callback)
    {
        return false;
    }
    LogService *service = nullptr;
    {
        QMutexLocker locker(&instance_mutex);
        if (instance_shutting_down || !instance_)
        {
            return false;
        }
        service = instance_;
        ++active_instance_accesses;
    }

    const auto releaseAccess = []() {
        QMutexLocker locker(&instance_mutex);
        --active_instance_accesses;
        if (active_instance_accesses == 0)
        {
            instance_condition.wakeAll();
        }
    };
    try
    {
        callback(*service);
    }
    catch (...)
    {
        releaseAccess();
        throw;
    }
    releaseAccess();
    return true;
}

void LogService::writeLogFallback(const LogRecord& record)
{
    fallbackWrite(LoggingInternal::serializeBoundedLogRecord(record));
}

void LogService::installQtMessageHandler()
{
    if (!qt_message_handler_installed_)
    {
        QtMessageHandler previous = qInstallMessageHandler(&LogService::qtMessageHandler);
        QMutexLocker locker(&instance_mutex);
        previous_message_handler_ = previous;
        qt_message_handler_installed_ = true;
    }
}

void LogService::publish(LogRecord record)
{
    submit(std::move(record));
}

LogRecord LogService::submit(LogRecord record)
{
    const bool localProcessRecord = record.process_id == 0;
    if (record.timestamp_us == 0)
    {
        record.timestamp_us = currentTimestampUs();
    }
    if (record.timestamp_utc.isEmpty())
    {
        record.timestamp_utc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    }
    if (record.process_id == 0)
    {
        record.process_id = currentProcessId();
    }
    if (record.thread_id == 0)
    {
        record.thread_id = currentThreadId();
    }
    if (localProcessRecord && record.sequence > 0)
    {
        quint64 observed = sequence_.load(std::memory_order_relaxed);
        while (observed < record.sequence &&
               !sequence_.compare_exchange_weak(observed,
                                                record.sequence,
                                                std::memory_order_relaxed))
        {
        }
    }
    record = LoggingInternal::boundLogRecord(std::move(record));
    if (writer_)
    {
        if (record.level == LogLevel::Critical)
        {
            auto completion = std::make_shared<LogWriterThread::CompletionState>();
            const bool queued = writer_->enqueue(record, completion);
            emit recordPublished(record);
            if (queued && QThread::currentThread() != writer_.get())
            {
                QMutexLocker locker(&completion->mutex);
                while (!completion->completed)
                {
                    completion->condition.wait(&completion->mutex);
                }
            }
        }
        else
        {
            writer_->enqueue(record);
            emit recordPublished(record);
        }
    }
    else
    {
        if (record.sequence == 0)
        {
            record.sequence = nextSequence();
        }
        emit recordPublished(record);
    }
    return record;
}

void reportUserIssue(LogLevel level,
                     const QString& source,
                     const QString& category,
                     const QString& message,
                     const QVariantMap& details)
{
    LogService::withCurrentInstance([&](LogService& logService) {
        QVariantMap fields = details;
        fields.insert(QStringLiteral("ui_visible"), true);
        if (!fields.contains(QStringLiteral("ui_visibility")))
        {
            fields.insert(QStringLiteral("ui_visibility"), QStringLiteral("attention"));
        }
        if (!fields.contains(QStringLiteral("event")))
        {
            fields.insert(QStringLiteral("event"), QStringLiteral("user_issue_reported"));
        }
        if (level >= LogLevel::Error &&
            !fields.contains(QStringLiteral("error_code")) &&
            !fields.contains(QStringLiteral("reason_code")))
        {
            fields.insert(QStringLiteral("error_code"),
                          QStringLiteral("UNCLASSIFIED_USER_ISSUE"));
        }
        logService.publish(level, source, category, message, fields);
    });
}

void attachProcessLogging(QProcess *process,
                          const QString& source,
                          const QString& category)
{
    if (!process)
    {
        return;
    }
    QObject::connect(process, &QProcess::readyReadStandardOutput, process,
                     [process, source, category]() {
                         consumeProcessOutput(process,
                                               source,
                                               category,
                                               process->readAllStandardOutput(),
                                               false);
                     });
    QObject::connect(process, &QProcess::readyReadStandardError, process,
                     [process, source, category]() {
                         consumeProcessOutput(process,
                                               source,
                                               category,
                                               process->readAllStandardError(),
                                               true);
                     });
    QObject::connect(process,
                     qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                     process,
                     [process, source, category](int exitCode, QProcess::ExitStatus exitStatus) {
                         consumeProcessOutput(process,
                                               source,
                                               category,
                                               process->readAllStandardOutput(),
                                               false);
                         consumeProcessOutput(process,
                                               source,
                                               category,
                                               process->readAllStandardError(),
                                               true);
                         flushProcessOutput(process, source, category, false);
                         flushProcessOutput(process, source, category, true);
                         LogService::withCurrentInstance([&](LogService& logService) {
                             QVariantMap fields{
                                 {QStringLiteral("event"), QStringLiteral("child_process_finished")},
                                 {QStringLiteral("exit_code"), exitCode},
                                 {QStringLiteral("exit_status"), static_cast<int>(exitStatus)},
                             };
                             if (exitCode != 0 || exitStatus != QProcess::NormalExit)
                             {
                                 fields.insert(QStringLiteral("error_code"),
                                               QStringLiteral("CHILD_PROCESS_ABNORMAL_EXIT"));
                             }
                             logService.publish(exitCode == 0 ? LogLevel::Info : LogLevel::Error,
                                                source,
                                                category,
                                                QStringLiteral("子进程已结束。"),
                                                fields);
                         });
                     });
    QObject::connect(process, &QProcess::errorOccurred, process,
                     [source, category](QProcess::ProcessError error) {
                         LogService::withCurrentInstance([&](LogService& logService) {
                             logService.publish(LogLevel::Error,
                                                source,
                                                category,
                                                QStringLiteral("子进程发生错误。"),
                                                {{QStringLiteral("event"), QStringLiteral("child_process_error")},
                                                 {QStringLiteral("error_code"), QStringLiteral("CHILD_PROCESS_ERROR")},
                                                 {QStringLiteral("process_error"), static_cast<int>(error)}});
                         });
                     });
}

QByteArray processLoggedStandardOutput(const QProcess *process)
{
    return process ? process->property(kProcessStdoutProperty).toByteArray() : QByteArray();
}

QByteArray processLoggedStandardError(const QProcess *process)
{
    return process ? process->property(kProcessStderrProperty).toByteArray() : QByteArray();
}

LogRecord LogService::publish(LogLevel level,
                              const QString& source,
                              const QString& category,
                              const QString& message,
                              const QVariantMap& fields,
                              const QString& correlationId,
                              const QString& sessionId)
{
    LogRecord record;
    record.level = level;
    record.source = source;
    record.category = category;
    record.message = message;
    record.fields = fields;
    record.correlation_id = correlationId;
    record.session_id = sessionId;
    record.timestamp_us = currentTimestampUs();
    record.timestamp_utc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    record.process_id = currentProcessId();
    record.thread_id = currentThreadId();
    return submit(std::move(record));
}

QString LogService::logFilePath() const
{
    return writer_ ? writer_->activeFilePath() :
        QDir(log_directory_).filePath(QStringLiteral("%1-%2.jsonl")
                                          .arg(application_name_,
                                               QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"))));
}

QString LogService::logDirectory() const
{
    return writer_ ? writer_->activeDirectory() : log_directory_;
}

#ifdef VAPORVIEW_LOGGING_TEST_HOOKS
void LogService::setWriterBlockedForTest(bool blocked)
{
    if (writer_)
    {
        writer_->setBlockedForTest(blocked);
    }
}

bool LogService::waitForWriterBlockedForTest(std::chrono::milliseconds timeout)
{
    return writer_ && writer_->waitUntilBlockedForTest(timeout);
}

bool LogService::setMaxPendingCriticalForTest(qsizetype limit)
{
    return writer_ && writer_->setMaxPendingCriticalForTest(limit);
}

QVariantMap LogService::writerStateForTest() const
{
    return writer_ ? writer_->stateForTest() : QVariantMap{};
}

QString LogService::emergencyLogFilePathForTest() const
{
    return writer_ ? writer_->emergencyFilePathForTest() : QString{};
}

bool LogService::waitUntilGlobalAccessRejectedForTest(std::chrono::milliseconds timeout)
{
    const QDeadlineTimer deadline(timeout);
    QMutexLocker locker(&instance_mutex);
    while (!instance_shutting_down && instance_ && !deadline.hasExpired())
    {
        instance_condition.wait(&instance_mutex, deadline);
    }
    return instance_shutting_down || !instance_;
}
#endif

quint64 LogService::nextSequence()
{
    return sequence_.fetch_add(1, std::memory_order_relaxed) + 1;
}

void LogService::qtMessageHandler(QtMsgType type,
                                  const QMessageLogContext& context,
                                  const QString& message)
{
    static thread_local bool handling = false;
    if (handling)
    {
        fallbackWrite(message.toUtf8());
        return;
    }
    handling = true;

    LogLevel level = LogLevel::Info;
    switch (type)
    {
    case QtDebugMsg: level = LogLevel::Debug; break;
    case QtInfoMsg: level = LogLevel::Info; break;
    case QtWarningMsg: level = LogLevel::Warning; break;
    case QtCriticalMsg: level = LogLevel::Critical; break;
    case QtFatalMsg: level = LogLevel::Critical; break;
    }
    QVariantMap fields;
    if (context.file) fields.insert(QStringLiteral("file"), QString::fromUtf8(context.file));
    if (context.function) fields.insert(QStringLiteral("function"), QString::fromUtf8(context.function));
    if (context.line > 0) fields.insert(QStringLiteral("line"), context.line);
    fields.insert(QStringLiteral("event"), QStringLiteral("qt_message"));
    if (level >= LogLevel::Error)
    {
        fields.insert(QStringLiteral("error_code"),
                      type == QtFatalMsg
                          ? QStringLiteral("QT_FATAL_MESSAGE")
                          : QStringLiteral("QT_CRITICAL_MESSAGE"));
    }
    LogService::withCurrentInstance([&](LogService& logService) {
        logService.publish(level,
                           QStringLiteral("Qt"),
                           context.category ? QString::fromUtf8(context.category) : QStringLiteral("default"),
                           message,
                           fields);
    });

    QtMessageHandler previous = nullptr;
    {
        QMutexLocker locker(&instance_mutex);
        previous = previous_message_handler_;
    }
    if (previous && previous != &LogService::qtMessageHandler)
    {
        previous(type, context, message);
    }
    else if (type == QtFatalMsg)
    {
        fallbackWrite(message.toUtf8());
    }
    handling = false;

    if (type == QtFatalMsg)
    {
        std::abort();
    }
}

quint64 LogService::currentProcessId()
{
#ifdef Q_OS_WIN
    return static_cast<quint64>(GetCurrentProcessId());
#else
    return static_cast<quint64>(QCoreApplication::applicationPid());
#endif
}

quint64 LogService::currentThreadId()
{
    return static_cast<quint64>(reinterpret_cast<quintptr>(QThread::currentThreadId()));
}

quint64 LogService::currentTimestampUs()
{
    return currentTimestampUsNow();
}

QString LogService::chooseLogDirectory(const QString& applicationName)
{
    const QString primary = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("logs"));
    const QString fallback = defaultFallbackLogDirectory();
    for (const QString& candidate : {primary, fallback})
    {
        if (candidate.isEmpty() || !QDir().mkpath(candidate))
        {
            continue;
        }
        const QString probePath = QDir(candidate).filePath(
            QStringLiteral(".%1-log-write-test").arg(applicationName));
        QFile probe(probePath);
        if (probe.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            probe.close();
            QFile::remove(probePath);
            return candidate;
        }
    }
    return primary;
}

}  // namespace VaporView
