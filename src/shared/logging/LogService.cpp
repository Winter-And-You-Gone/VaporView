#include "LogService.h"

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

}  // namespace

class LogWriterThread final : public QThread
{
public:
    using FailureCallback = std::function<void(const QString&)>;

    LogWriterThread(QString directory, QString applicationName, FailureCallback failureCallback)
        : directory_(std::move(directory)), application_name_(std::move(applicationName)),
          failure_callback_(std::move(failureCallback))
    {
    }

    ~LogWriterThread() override
    {
        stop();
    }

    void enqueue(LogRecord record)
    {
        QMutexLocker locker(&mutex_);
        if (stopping_)
        {
            return;
        }

        if (queue_.size() >= kQueueLimit)
        {
            auto lowPriority = std::find_if(queue_.begin(), queue_.end(), [](const LogRecord& item) {
                return !isHighPriority(item.level);
            });
            if (lowPriority != queue_.end())
            {
                queue_.erase(lowPriority);
                ++dropped_count_;
            }
            else if (!isHighPriority(record.level))
            {
                ++dropped_count_;
                return;
            }
            else
            {
                ++dropped_count_;
                fallbackWrite(record.toJsonLine());
                return;
            }
        }

        queue_.push_back(std::move(record));
        wake_.wakeOne();
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
            wake_.wakeOne();
        }
        wait();
    }

protected:
    void run() override
    {
        while (true)
        {
            LogRecord record;
            quint64 dropped = 0;
            {
                QMutexLocker locker(&mutex_);
                while (queue_.empty() && !stopping_)
                {
                    wake_.wait(&mutex_);
                }
                if (queue_.empty() && stopping_)
                {
                    break;
                }
                record = std::move(queue_.front());
                queue_.pop_front();
                dropped = std::exchange(dropped_count_, 0ULL);
            }

            if (dropped > 0)
            {
                LogRecord notice;
                notice.timestamp_utc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
                notice.timestamp_us = static_cast<quint64>(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) * 1000ULL;
                notice.level = LogLevel::Warning;
                notice.source = QStringLiteral("LogService");
                notice.category = QStringLiteral("queue");
                notice.message = QStringLiteral("Dropped %1 low-priority log records because the writer queue was full.").arg(dropped);
                notice.fields.insert(QStringLiteral("dropped_count"), static_cast<qulonglong>(dropped));
                writeRecord(notice);
            }
            writeRecord(record);
        }

        QMutexLocker locker(&mutex_);
        while (!queue_.empty())
        {
            writeRecord(queue_.front());
            queue_.pop_front();
        }
        closeFile();
    }

private:
    void openFile()
    {
        if (file_.isOpen())
        {
            return;
        }
        if (!cleanup_complete_)
        {
            cleanupOldFiles();
            cleanup_complete_ = true;
        }
        if (!QDir().mkpath(directory_))
        {
            notifyFailure(QStringLiteral("Cannot create application log directory: %1").arg(directory_));
            return;
        }
        const QString date = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
        current_path_ = QDir(directory_).filePath(
            QStringLiteral("%1-%2.jsonl").arg(application_name_, date));
        file_.setFileName(current_path_);
        if (!file_.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        {
            current_path_.clear();
            notifyFailure(QStringLiteral("Cannot open application log file in: %1").arg(directory_));
        }
    }

    void cleanupOldFiles()
    {
        QDir directory(directory_);
        if (!directory.exists())
        {
            return;
        }
        const QStringList patterns{
            QStringLiteral("%1-*.jsonl").arg(application_name_),
            QStringLiteral("%1-*.jsonl.*").arg(application_name_)};
        const QFileInfoList files = directory.entryInfoList(patterns,
                                                              QDir::Files | QDir::Readable,
                                                              QDir::Time);
        qint64 retainedBytes = 0;
        int retainedFiles = 0;
        for (const QFileInfo& fileInfo : files)
        {
            const bool keep = retainedFiles < kMaxRetainedFiles &&
                (retainedFiles == 0 || retainedBytes + fileInfo.size() <= kMaxTotalFileBytes);
            if (keep)
            {
                ++retainedFiles;
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
        openFile();
    }

    void writeRecord(const LogRecord& record)
    {
        const QByteArray line = record.toJsonLine();
        openFile();
        if (!file_.isOpen())
        {
            fallbackWrite(line);
            return;
        }
        rotateIfNeeded(line.size());
        if (file_.write(line) != line.size() || !file_.flush())
        {
            notifyFailure(QStringLiteral("Cannot flush application log file: %1").arg(current_path_));
            fallbackWrite(line);
        }
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
            file_.flush();
            file_.close();
        }
    }

    QString directory_;
    QString application_name_;
    QString current_path_;
    QFile file_;
    FailureCallback failure_callback_;
    bool failure_reported_ = false;
    bool cleanup_complete_ = false;
    QMutex mutex_;
    QWaitCondition wake_;
    std::deque<LogRecord> queue_;
    quint64 dropped_count_ = 0;
    bool stopping_ = false;
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
        [this](const QString& message) { emit diagnosticFailure(message); });
    writer_->start();
    if (!instance_)
    {
        instance_ = this;
    }
    publish(LogLevel::Info, QStringLiteral("App"), QStringLiteral("lifecycle"),
            QStringLiteral("Application logging initialized."),
            {{QStringLiteral("log_directory"), log_directory_}});
}

LogService::~LogService()
{
    publish(LogLevel::Info, QStringLiteral("App"), QStringLiteral("lifecycle"),
            QStringLiteral("Application logging stopped."));
    if (writer_)
    {
        writer_->stop();
    }
    if (instance_ == this && previous_message_handler_)
    {
        qInstallMessageHandler(previous_message_handler_);
        previous_message_handler_ = nullptr;
    }
    if (instance_ == this)
    {
        instance_ = nullptr;
    }
}

LogService *LogService::instance()
{
    return instance_;
}

void LogService::installQtMessageHandler()
{
    if (previous_message_handler_ == nullptr)
    {
        previous_message_handler_ = qInstallMessageHandler(&LogService::qtMessageHandler);
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
        writer_->enqueue(std::move(record));
    }
}

void reportUserIssue(LogLevel level,
                     const QString& source,
                     const QString& category,
                     const QString& message,
                     const QVariantMap& details)
{
    if (LogService *logService = LogService::instance())
    {
        QVariantMap fields = details;
        fields.insert(QStringLiteral("ui_visible"), true);
        logService->publish(level, source, category, message, fields);
    }
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
    const QString date = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    return QDir(log_directory_).filePath(QStringLiteral("%1-%2.jsonl").arg(application_name_, date));
}

QString LogService::logDirectory() const
{
    return log_directory_;
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

    if (instance_)
    {
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
        instance_->publish(level,
                           QStringLiteral("Qt"),
                           context.category ? QString::fromUtf8(context.category) : QStringLiteral("default"),
                           message,
                           fields);
    }

    if (previous_message_handler_ && previous_message_handler_ != &LogService::qtMessageHandler)
    {
        previous_message_handler_(type, context, message);
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
    return static_cast<quint64>(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) * 1000ULL;
}

QString LogService::chooseLogDirectory(const QString& applicationName)
{
    const QString primary = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("logs"));
    const QString fallback = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("logs"));
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
