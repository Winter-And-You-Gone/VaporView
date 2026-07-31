#include "LogService.h"
#include "logging/LogQueuePolicy.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
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
#include <deque>
#include <functional>
#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace VaporView
{

namespace
{
constexpr qsizetype kQueueLimit = 8192;
constexpr qint64 kMaxFileBytes = 10LL * 1024LL * 1024LL;
constexpr qint64 kMaxTotalFileBytes = 100LL * 1024LL * 1024LL;
constexpr int kMaxRetainedFiles = 10;
constexpr int kMaxRotatedFiles = kMaxRetainedFiles - 1;
constexpr qint64 kDropReportIntervalMs = 5000;
constexpr qint64 kFlushIntervalMs = 250;
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

bool isHighPriority(LogLevel level)
{
    return level >= LogLevel::Warning;
}

void fallbackWrite(const QByteArray& line)
{
#ifdef Q_OS_WIN
    const QString text = QString::fromUtf8(line);
    OutputDebugStringW(reinterpret_cast<LPCWSTR>(text.utf16()));
    OutputDebugStringW(L"\n");
#else
    std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
#endif
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
    return static_cast<quint64>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
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
        QVariantMap fields{
            {QStringLiteral("stream"), standardError ? QStringLiteral("stderr")
                                                       : QStringLiteral("stdout")},
            {QStringLiteral("raw_bytes"), line.size()}};
        if (partial)
        {
            fields.insert(QStringLiteral("partial"), true);
        }
        logService.publish(standardError ? LogLevel::Warning : LogLevel::Debug,
                           source,
                           category,
                           QString::fromLocal8Bit(line),
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

    struct CompletionState
    {
        QMutex mutex;
        QWaitCondition condition;
        bool completed = false;
        bool success = false;
    };

    struct QueuedRecord
    {
        LogRecord record;
        std::shared_ptr<CompletionState> completion;
    };

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

    bool enqueue(LogRecord record, std::shared_ptr<CompletionState> completion = {})
    {
        QMutexLocker locker(&mutex_);
        if (stopping_)
        {
            complete(completion, false);
            return false;
        }

        if (queue_.size() >= kQueueLimit)
        {
            QVector<LogLevel> queuedLevels;
            queuedLevels.reserve(static_cast<qsizetype>(queue_.size()));
            for (const QueuedRecord& item : queue_)
            {
                queuedLevels.push_back(item.record.level);
            }
            const LoggingInternal::QueueOverflowDecision decision =
                LoggingInternal::decideQueueOverflow(queuedLevels, record.level);
            if (decision.action == LoggingInternal::QueueOverflowAction::RemoveLowPriorityAndEnqueue)
            {
                queue_.erase(queue_.begin() + decision.low_priority_index);
                dropped_count_ += decision.dropped_increment;
            }
            else if (decision.action == LoggingInternal::QueueOverflowAction::DropIncoming)
            {
                dropped_count_ += decision.dropped_increment;
                complete(completion, false);
                return false;
            }
            else if (decision.action == LoggingInternal::QueueOverflowAction::EmergencyWrite)
            {
                locker.unlock();
                const bool success = emergencyWrite(record);
                complete(completion, success);
                return success;
            }
            // Critical records are allowed to grow the queue temporarily so
            // they cannot overtake or silently replace accepted records.
        }

        queue_.push_back({std::move(record), std::move(completion)});
        wake_.wakeOne();
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
            wake_.wakeAll();
        }
        if (QThread::currentThread() == this)
        {
            return;
        }
        wait();
    }

    bool emergencyWrite(const LogRecord& record)
    {
        QMutexLocker locker(&file_mutex_);
        return writeRecord(record, true);
    }

protected:
    void run() override
    {
        while (true)
        {
            QueuedRecord item;
            quint64 dropped = 0;
            bool idleFlush = false;
            {
                QMutexLocker locker(&mutex_);
                while (queue_.empty() && !stopping_)
                {
                    if (pending_flush_.load(std::memory_order_acquire))
                    {
                        if (!wake_.wait(&mutex_, static_cast<unsigned long>(kFlushIntervalMs)))
                        {
                            idleFlush = true;
                            break;
                        }
                    }
                    else
                    {
                        wake_.wait(&mutex_);
                    }
                }
                if (queue_.empty() && stopping_)
                {
                    break;
                }
                if (!idleFlush)
                {
                    item = std::move(queue_.front());
                    queue_.pop_front();
                }
                const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                if (dropped_count_ > 0 &&
                    (last_drop_report_ms_ == 0 || nowMs - last_drop_report_ms_ >= kDropReportIntervalMs))
                {
                    dropped = std::exchange(dropped_count_, 0ULL);
                    last_drop_report_ms_ = nowMs;
                }
            }

            QMutexLocker fileLocker(&file_mutex_);
            if (dropped > 0)
            {
                writeDropNotice(dropped);
            }
            if (idleFlush)
            {
                flushPending();
            }
            else
            {
                const bool success = writeRecord(item.record, item.completion != nullptr);
                complete(item.completion, success);
            }
        }

        while (true)
        {
            QueuedRecord item;
            quint64 dropped = 0;
            bool hasItem = false;
            {
                QMutexLocker locker(&mutex_);
                if (queue_.empty())
                {
                    dropped = std::exchange(dropped_count_, 0ULL);
                }
                else
                {
                    item = std::move(queue_.front());
                    queue_.pop_front();
                    hasItem = true;
                }
            }
            if (!hasItem)
            {
                QMutexLocker fileLocker(&file_mutex_);
                if (dropped > 0)
                {
                    writeDropNotice(dropped);
                }
                flushPending();
                closeFile();
                break;
            }
            QMutexLocker fileLocker(&file_mutex_);
            if (dropped > 0)
            {
                writeDropNotice(dropped);
            }
            complete(item.completion, writeRecord(item.record, item.completion != nullptr));
        }
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

    void writeDropNotice(quint64 dropped)
    {
        const LogRecord notice = LoggingInternal::makeDropNotice(
            dropped,
            sequence_callback_ ? sequence_callback_() : 0,
            currentTimestampUsNow(),
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
            static_cast<quint64>(QCoreApplication::applicationPid()),
            static_cast<quint64>(reinterpret_cast<quintptr>(QThread::currentThreadId())));
        writeRecord(notice, true);
    }

    bool openFile()
    {
        const QString date = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
        const QString desiredPath = QDir(directory_).filePath(
            QStringLiteral("%1-%2.jsonl").arg(application_name_, date));
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
            notifyFailure(QStringLiteral("Cannot create application log directory: %1").arg(directory_));
            return false;
        }
        current_path_ = desiredPath;
        file_.setFileName(current_path_);
        if (!file_.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        {
            current_path_.clear();
            notifyFailure(QStringLiteral("Cannot open application log file in: %1").arg(directory_));
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

    bool writeRecordOnce(const LogRecord& record, bool forceFlush)
    {
        const QByteArray line = record.toJsonLine();
        if (!openFile())
        {
            return false;
        }
        rotateIfNeeded(line.size());
        if (!file_.isOpen() || file_.write(line) != line.size())
        {
            return false;
        }
        ++pending_records_;
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const bool due = forceFlush || isHighPriority(record.level) ||
            last_flush_ms_ == 0 || nowMs - last_flush_ms_ >= kFlushIntervalMs ||
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
        const QByteArray line = record.toJsonLine();
        if (writeRecordOnce(record, forceFlush))
        {
            return true;
        }

        notifyFailure(QStringLiteral("Cannot write application log file: %1").arg(current_path_));
        if (switchToFallbackDirectory() && writeRecordOnce(record, forceFlush))
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
            notifyFailure(QStringLiteral("Cannot flush application log file: %1").arg(current_path_));
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
                notifyFailure(QStringLiteral("Cannot flush application log file: %1").arg(current_path_));
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
        return QDir(directory_).filePath(QStringLiteral("%1-%2.jsonl").arg(application_name_, date));
    }

private:

    QString directory_;
    QString application_name_;
    QString fallback_directory_;
    QString current_path_;
    QFile file_;
    FailureCallback failure_callback_;
    SequenceCallback sequence_callback_;
    bool failure_reported_ = false;
    bool cleanup_complete_ = false;
    qint64 last_flush_ms_ = 0;
    qsizetype pending_records_ = 0;
    QMutex mutex_;
    mutable QMutex file_mutex_;
    QWaitCondition wake_;
    std::deque<QueuedRecord> queue_;
    quint64 dropped_count_ = 0;
    qint64 last_drop_report_ms_ = 0;
    bool stopping_ = false;
    std::atomic_bool pending_flush_{false};
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
    QByteArray line = QJsonDocument(toJsonObject()).toJson(QJsonDocument::Compact);
    line.append('\n');
    return line;
}

LogService::LogService(const QString& applicationName,
                       QObject *parent,
                       const QString& logDirectoryOverride)
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
        defaultFallbackLogDirectory(),
        [this](const QString& message) { emit diagnosticFailure(message); },
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
            QStringLiteral("Application logging initialized."),
            {{QStringLiteral("log_directory"), log_directory_}});
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
    {
        QMutexLocker locker(&instance_mutex);
        while (active_instance_accesses > 0)
        {
            instance_condition.wait(&instance_mutex);
        }
    }

    publish(LogLevel::Info, QStringLiteral("App"), QStringLiteral("lifecycle"),
            QStringLiteral("Application logging stopped."));
    if (writer_)
    {
        writer_->stop();
    }
    if (owns_global_instance_)
    {
        QMutexLocker locker(&instance_mutex);
        instance_shutting_down = false;
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
    if (record.sequence == 0)
    {
        record.sequence = nextSequence();
    }
    emit recordPublished(record);
    if (writer_)
    {
        if (record.level == LogLevel::Critical)
        {
            auto completion = std::make_shared<LogWriterThread::CompletionState>();
            const bool queued = writer_->enqueue(record, completion);
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
            writer_->enqueue(std::move(record));
        }
    }
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
                             logService.publish(exitCode == 0 ? LogLevel::Info : LogLevel::Error,
                                                source,
                                                category,
                                                QStringLiteral("Child process finished."),
                                                {{QStringLiteral("exit_code"), exitCode},
                                                 {QStringLiteral("exit_status"), static_cast<int>(exitStatus)}});
                         });
                     });
    QObject::connect(process, &QProcess::errorOccurred, process,
                     [source, category](QProcess::ProcessError error) {
                         LogService::withCurrentInstance([&](LogService& logService) {
                             logService.publish(LogLevel::Error,
                                                source,
                                                category,
                                                QStringLiteral("Child process error."),
                                                {{QStringLiteral("process_error"), static_cast<int>(error)}});
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
    record.sequence = nextSequence();
    publish(record);
    return record;
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
