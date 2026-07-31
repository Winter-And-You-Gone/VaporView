#ifndef VaporView_LOG_RECORD_H_
#define VaporView_LOG_RECORD_H_

#include <QJsonObject>
#include <QString>
#include <QVariantMap>

#include <QtGlobal>

namespace VaporView
{

namespace LogRecordLimits
{
constexpr qsizetype kMaxMessageUtf8Bytes = 64 * 1024;
constexpr qsizetype kMaxSingleStringUtf8Bytes = 64 * 1024;
constexpr qsizetype kMaxByteArrayBytes = 64 * 1024;
constexpr qsizetype kMaxFieldsJsonBytes = 192 * 1024;
constexpr qsizetype kMaxSerializedRecordBytes = 256 * 1024;
constexpr int kMaxVariantDepth = 8;
constexpr qsizetype kMaxContainerElements = 256;
constexpr qsizetype kMaxVariantNodes = 4096;
}  // namespace LogRecordLimits

enum class LogLevel : quint8
{
    Debug = 0,
    Info,
    Warning,
    Error,
    Critical,
};

QString logLevelName(LogLevel level);
LogLevel logLevelFromName(const QString& name);

struct LogRecord
{
    int schema_version = 1;
    QString timestamp_utc;
    quint64 timestamp_us = 0;
    LogLevel level = LogLevel::Info;
    QString source;
    QString category;
    quint64 process_id = 0;
    quint64 thread_id = 0;
    quint64 sequence = 0;
    QString correlation_id;
    QString session_id;
    QString message;
    QVariantMap fields;

    QJsonObject toJsonObject() const;
    QByteArray toJsonLine() const;
};

}  // namespace VaporView

Q_DECLARE_METATYPE(VaporView::LogRecord)

#endif
