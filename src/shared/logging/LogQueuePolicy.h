#ifndef VaporView_LOG_QUEUE_POLICY_H_
#define VaporView_LOG_QUEUE_POLICY_H_

#include "LogRecord.h"

#include <QString>
#include <QVector>

namespace VaporView::LoggingInternal
{

enum class QueueOverflowAction
{
    RemoveLowPriorityAndEnqueue,
    DropIncoming,
    EmergencyWrite,
    EnqueueCriticalBeyondLimit,
};

struct QueueOverflowDecision
{
    QueueOverflowAction action = QueueOverflowAction::DropIncoming;
    qsizetype low_priority_index = -1;
    quint64 dropped_increment = 0;
};

QueueOverflowDecision decideQueueOverflow(const QVector<LogLevel>& queuedLevels,
                                          LogLevel incomingLevel);

LogRecord makeDropNotice(quint64 dropped,
                         quint64 sequence,
                         quint64 timestampUs,
                         const QString& timestampUtc,
                         quint64 processId,
                         quint64 threadId);

}  // namespace VaporView::LoggingInternal

#endif
