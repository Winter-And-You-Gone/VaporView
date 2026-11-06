#ifndef VaporView_LOG_SERVICE_H_
#define VaporView_LOG_SERVICE_H_

#include "LogRecord.h"

#include <QObject>
#include <QString>
#include <QVariantMap>

#include <memory>
#include <atomic>
#include <chrono>
#include <functional>

class QProcess;

namespace VaporView
{

class LogWriterThread;

// Compiler builtins are evaluated at the call site, even in C++17 default arguments.
#if defined(__clang__) || defined(__GNUC__) || (defined(_MSC_VER) && _MSC_VER >= 1926)
#define VAPORVIEW_LOG_FILE __builtin_FILE()
#define VAPORVIEW_LOG_FUNCTION __builtin_FUNCTION()
#define VAPORVIEW_LOG_LINE __builtin_LINE()
#else
#define VAPORVIEW_LOG_FILE ""
#define VAPORVIEW_LOG_FUNCTION ""
#define VAPORVIEW_LOG_LINE 0
#endif

class LogService final : public QObject
{
    Q_OBJECT

public:
    explicit LogService(const QString& applicationName,
                        QObject *parent = nullptr,
                        const QString& logDirectoryOverride = {},
                        const QString& fallbackDirectoryOverride = {});
    ~LogService() override;

    LogService(const LogService&) = delete;
    LogService& operator=(const LogService&) = delete;

    // Legacy raw access only. The returned pointer is not protected across
    // statements and must never be retained. New code uses withCurrentInstance().
    [[deprecated("Use LogService::withCurrentInstance() to avoid lifetime races")]]
    static LogService *instance();
    // Safe access for callbacks that may race with application shutdown.
    static bool withCurrentInstance(const std::function<void(LogService&)>& callback);

    // Write one bounded structured record directly to stderr/debugger when no
    // LogService instance is available. This path must not call Qt logging.
    static void writeLogFallback(const LogRecord& record);
    // Capture at an outer business wrapper before fallback or IPC forwarding.
    static void addCallLocation(QVariantMap& fields, const char *file, const char *function, int line)
    {
        if (file && *file && !fields.contains(QStringLiteral("file")))
            fields.insert(QStringLiteral("file"), QString::fromUtf8(file));
        if (function && *function && !fields.contains(QStringLiteral("function")))
            fields.insert(QStringLiteral("function"), QString::fromUtf8(function));
        if (line > 0 && !fields.contains(QStringLiteral("line")))
            fields.insert(QStringLiteral("line"), line);
    }
    static void addCallLocation(LogRecord& record, const char *file, const char *function, int line)
    {
        if (!record.source_location.isEmpty())
            return;
        if (file && *file) record.source_location.file = QString::fromUtf8(file);
        if (function && *function) record.source_location.function = QString::fromUtf8(function);
        if (line > 0) record.source_location.line = line;
        addCallLocation(record.fields, file, function, line);
    }

    void installQtMessageHandler();
    void publish(LogRecord record,
                 const char *file = VAPORVIEW_LOG_FILE,
                 const char *function = VAPORVIEW_LOG_FUNCTION,
                 int line = VAPORVIEW_LOG_LINE);
    LogRecord publish(LogLevel level,
                      const QString& source,
                      const QString& category,
                      const QString& message,
                      const QVariantMap& fields = {},
                      const QString& correlationId = {},
                      const QString& sessionId = {},
                      const char *file = VAPORVIEW_LOG_FILE,
                      const char *function = VAPORVIEW_LOG_FUNCTION,
                      int line = VAPORVIEW_LOG_LINE);

    QString logFilePath() const;
    QString logDirectory() const;
    quint64 nextSequence();

#ifdef VAPORVIEW_LOGGING_TEST_HOOKS
    void setWriterBlockedForTest(bool blocked);
    bool waitForWriterBlockedForTest(std::chrono::milliseconds timeout);
    bool setMaxPendingCriticalForTest(qsizetype limit);
    QVariantMap writerStateForTest() const;
    QString emergencyLogFilePathForTest() const;
    static bool waitUntilGlobalAccessRejectedForTest(std::chrono::milliseconds timeout);
#endif

signals:
    void recordPublished(const VaporView::LogRecord& record);
    void writerFailureReported(const QString& message);

private:
    static void qtMessageHandler(QtMsgType type,
                                 const QMessageLogContext& context,
                                 const QString& message);
    static quint64 currentProcessId();
    static quint64 currentThreadId();
    static quint64 currentTimestampUs();
    static QString chooseLogDirectory(const QString& applicationName);
    LogRecord submit(LogRecord record);

    QString application_name_;
    QString log_directory_;
    std::unique_ptr<LogWriterThread> writer_;
    std::atomic<quint64> sequence_{0};
    bool qt_message_handler_installed_ = false;
    bool owns_global_instance_ = false;
    static LogService *instance_;
    static QtMessageHandler previous_message_handler_;
};

void reportUserIssue(LogLevel level,
                     const QString& source,
                     const QString& category,
                     const QString& message,
                     const QVariantMap& details = QVariantMap(),
                     const char *file = VAPORVIEW_LOG_FILE,
                     const char *function = VAPORVIEW_LOG_FUNCTION,
                     int line = VAPORVIEW_LOG_LINE);

void attachProcessLogging(QProcess *process,
                          const QString& source,
                          const QString& category);
QByteArray processLoggedStandardOutput(const QProcess *process);
QByteArray processLoggedStandardError(const QProcess *process);

}  // namespace VaporView

#endif
