#include "SessionTimeFormat.h"

#include <QDateTime>
#include <QStringList>
#include <QTimeZone>

namespace
{

QDateTime parseMetadataUtcDateTime(const QString& value)
{
    QDateTime dateTime = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!dateTime.isValid())
    {
        dateTime = QDateTime::fromString(value, Qt::ISODate);
    }
    if (dateTime.isValid() && dateTime.timeSpec() == Qt::LocalTime)
    {
        dateTime.setTimeSpec(Qt::UTC);
    }
    return dateTime;
}

QTimeZone beijingTimeZone()
{
    return QTimeZone("Asia/Shanghai");
}

}  // namespace

namespace VaporView
{

QString formatSessionMetadataTimeBeijing(const QString& utcText)
{
    if (utcText.trimmed().isEmpty())
    {
        return QStringLiteral("---");
    }

    const QDateTime utc = parseMetadataUtcDateTime(utcText);
    if (!utc.isValid())
    {
        return utcText;
    }

    return utc.toTimeZone(beijingTimeZone()).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss 'UTC+8'"));
}

QString formatSessionDurationText(const QString& startUtc, const QString& endUtc, bool english)
{
    const QDateTime start = parseMetadataUtcDateTime(startUtc);
    const QDateTime end = parseMetadataUtcDateTime(endUtc);
    if (!start.isValid() || !end.isValid() || end < start)
    {
        return QStringLiteral("---");
    }

    qint64 seconds = start.secsTo(end);
    const qint64 hours = seconds / 3600;
    seconds %= 3600;
    const qint64 minutes = seconds / 60;
    seconds %= 60;

    QStringList parts;
    if (hours > 0)
    {
        parts << (english ? QString("%1h").arg(hours) : QString("%1小时").arg(hours));
    }
    if (minutes > 0 || hours > 0)
    {
        parts << (english ? QString("%1m").arg(minutes) : QString("%1分").arg(minutes));
    }
    parts << (english ? QString("%1s").arg(seconds) : QString("%1秒").arg(seconds));
    return parts.join(' ');
}

}  // namespace VaporView
