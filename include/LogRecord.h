#ifndef VaporView_LOG_RECORD_H_
#define VaporView_LOG_RECORD_H_

#include <QJsonObject>
#include <QString>
#include <QVariantMap>

#include <QtGlobal>

namespace VaporView
{

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
