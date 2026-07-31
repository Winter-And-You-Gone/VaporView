#include "logging/LogQueuePolicy.h"

namespace VaporView::LoggingInternal
{
namespace
{
bool isHighPriority(LogLevel level)
{
    return level >= LogLevel::Warning;
}
}  // namespace

QueueOverflowDecision decideQueueOverflow(const QVector<LogLevel>& queuedLevels,
                                          LogLevel incomingLevel)
{
    for (qsizetype index = 0; index < queuedLevels.size(); ++index)
    {
        if (!isHighPriority(queuedLevels.at(index)))
        {
            return {QueueOverflowAction::RemoveLowPriorityAndEnqueue, index, 1};
        }
    }
    if (!isHighPriority(incomingLevel))
    {
        return {QueueOverflowAction::DropIncoming, -1, 1};
    }
    if (incomingLevel == LogLevel::Critical)
    {
        return {QueueOverflowAction::EnqueueCriticalBeyondLimit, -1, 0};
    }
    return {QueueOverflowAction::EmergencyWrite, -1, 0};
}

LogRecord makeDropNotice(quint64 dropped,
                         quint64 sequence,
                         quint64 timestampUs,
                         const QString& timestampUtc,
                         quint64 processId,
                         quint64 threadId)
{
    LogRecord notice;
    notice.timestamp_utc = timestampUtc;
    notice.timestamp_us = timestampUs;
    notice.level = LogLevel::Warning;
    notice.source = QStringLiteral("LogService");
    notice.category = QStringLiteral("queue");
    notice.process_id = processId;
    notice.thread_id = threadId;
    notice.sequence = sequence;
    notice.message = QStringLiteral("Dropped %1 log records because the writer queue was full.").arg(dropped);
    notice.fields.insert(QStringLiteral("dropped_count"), static_cast<qulonglong>(dropped));
    return notice;
}

}  // namespace VaporView::LoggingInternal
