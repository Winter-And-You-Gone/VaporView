#ifndef VaporView_LOG_SERVICE_H_
#define VaporView_LOG_SERVICE_H_

#include "LogRecord.h"

#include <QObject>
#include <QString>
#include <QVariantMap>

#include <memory>
#include <atomic>

class QProcess;

namespace VaporView
{

class LogWriterThread;

class LogService final : public QObject
{
    Q_OBJECT

public:
    explicit LogService(const QString& applicationName,
                        QObject *parent = nullptr,
                        const QString& logDirectoryOverride = {});
    ~LogService() override;

    LogService(const LogService&) = delete;
    LogService& operator=(const LogService&) = delete;

    static LogService *instance();

    void installQtMessageHandler();
    void publish(LogRecord record);
    LogRecord publish(LogLevel level,
                      const QString& source,
                      const QString& category,
                      const QString& message,
                      const QVariantMap& fields = {},
                      const QString& correlationId = {},
                      const QString& sessionId = {});

    QString logFilePath() const;
    QString logDirectory() const;
    quint64 nextSequence();

signals:
    void recordPublished(const VaporView::LogRecord& record);
    void diagnosticFailure(const QString& message);

private:
    static void qtMessageHandler(QtMsgType type,
                                 const QMessageLogContext& context,
                                 const QString& message);
    static quint64 currentProcessId();
    static quint64 currentThreadId();
    static quint64 currentTimestampUs();
    static QString chooseLogDirectory(const QString& applicationName);

    QString application_name_;
    QString log_directory_;
    std::unique_ptr<LogWriterThread> writer_;
    std::atomic<quint64> sequence_{0};
    bool qt_message_handler_installed_ = false;
    static LogService *instance_;
    static QtMessageHandler previous_message_handler_;
};

void reportUserIssue(LogLevel level,
                     const QString& source,
                     const QString& category,
                     const QString& message,
                     const QVariantMap& details = QVariantMap());

void attachProcessLogging(QProcess *process,
                          const QString& source,
                          const QString& category);
QByteArray processLoggedStandardOutput(const QProcess *process);
QByteArray processLoggedStandardError(const QProcess *process);

}  // namespace VaporView

#endif
