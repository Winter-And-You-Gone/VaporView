#include "logging/LogQueuePolicy.h"

#include <algorithm>
#include <utility>

namespace VaporView::LoggingInternal
{

LogRecordQueue::LogRecordQueue(qsizetype capacity, qsizetype maxPendingCritical)
    : capacity_(std::max<qsizetype>(1, capacity)),
      max_pending_critical_(std::max<qsizetype>(1, maxPendingCritical))
{
}

QueueEnqueueResult LogRecordQueue::enqueue(QueuedLogRecord record)
{
    QueueEnqueueResult result;
    const LogLevel incomingLevel = record.record.level;

    if (incomingLevel == LogLevel::Critical &&
        pending_critical_ >= max_pending_critical_)
    {
        result.action = QueueEnqueueAction::EmergencyWrite;
        result.critical_overload_started = !critical_overload_active_;
        result.pending_critical_limit = max_pending_critical_;
        critical_overload_active_ = true;
        return result;
    }

    if (static_cast<qsizetype>(queue_.size()) >= capacity_)
    {
        ++priority_index_lookups_;
        result.evicted = evictDebug();

        if (!result.evicted && incomingLevel != LogLevel::Debug)
        {
            ++priority_index_lookups_;
            result.evicted = evictInfo();
        }

        if (result.evicted)
        {
            result.dropped_increment = 1;
        }
        else if (incomingLevel == LogLevel::Debug || incomingLevel == LogLevel::Info)
        {
            result.action = QueueEnqueueAction::DroppedIncoming;
            result.dropped_increment = 1;
            return result;
        }
        else if (incomingLevel != LogLevel::Critical)
        {
            result.action = QueueEnqueueAction::EmergencyWrite;
            return result;
        }
        // Critical may exceed the normal capacity, but only up to the separate
        // pending-Critical limit checked above.
    }

    append(std::move(record));
    result.action = QueueEnqueueAction::Enqueued;
    return result;
}

std::optional<QueuedLogRecord> LogRecordQueue::takeNext()
{
    if (queue_.empty())
    {
        return std::nullopt;
    }

    Iterator first = queue_.begin();
    if (first->record.level == LogLevel::Debug)
    {
        debug_index_.pop_front();
    }
    else if (first->record.level == LogLevel::Info)
    {
        info_index_.pop_front();
    }

    QueuedLogRecord record = std::move(*first);
    queue_.erase(first);
    return record;
}

void LogRecordQueue::markProcessed(LogLevel level)
{
    if (level != LogLevel::Critical)
    {
        return;
    }
    if (pending_critical_ > 0)
    {
        --pending_critical_;
    }
    if (pending_critical_ < max_pending_critical_)
    {
        critical_overload_active_ = false;
    }
}

bool LogRecordQueue::empty() const
{
    return queue_.empty();
}

qsizetype LogRecordQueue::size() const
{
    return static_cast<qsizetype>(queue_.size());
}

LogQueueStats LogRecordQueue::stats() const
{
    return {size(),
            pending_critical_,
            max_observed_size_,
            priority_index_lookups_,
            critical_overload_active_};
}

#ifdef VAPORVIEW_LOGGING_TEST_HOOKS
bool LogRecordQueue::setMaxPendingCriticalForTest(qsizetype limit)
{
    if (!queue_.empty() || pending_critical_ != 0 || limit <= 0)
    {
        return false;
    }
    max_pending_critical_ = limit;
    critical_overload_active_ = false;
    return true;
}
#endif

void LogRecordQueue::append(QueuedLogRecord record)
{
    queue_.push_back(std::move(record));
    Iterator inserted = std::prev(queue_.end());
    if (inserted->record.level == LogLevel::Debug)
    {
        debug_index_.push_back(inserted);
    }
    else if (inserted->record.level == LogLevel::Info)
    {
        info_index_.push_back(inserted);
    }
    else if (inserted->record.level == LogLevel::Critical)
    {
        ++pending_critical_;
    }
    max_observed_size_ = (std::max)(max_observed_size_, size());
}

std::optional<QueuedLogRecord> LogRecordQueue::evictDebug()
{
    if (debug_index_.empty())
    {
        return std::nullopt;
    }
    Iterator victim = debug_index_.front();
    debug_index_.pop_front();
    QueuedLogRecord record = std::move(*victim);
    queue_.erase(victim);
    return record;
}

std::optional<QueuedLogRecord> LogRecordQueue::evictInfo()
{
    if (info_index_.empty())
    {
        return std::nullopt;
    }
    Iterator victim = info_index_.front();
    info_index_.pop_front();
    QueuedLogRecord record = std::move(*victim);
    queue_.erase(victim);
    return record;
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

LogRecord makeCriticalOverloadNotice(quint64 sequence,
                                     quint64 timestampUs,
                                     const QString& timestampUtc,
                                     quint64 processId,
                                     quint64 threadId,
                                     qsizetype pendingCriticalLimit)
{
    LogRecord notice;
    notice.timestamp_utc = timestampUtc;
    notice.timestamp_us = timestampUs;
    notice.level = LogLevel::Critical;
    notice.source = QStringLiteral("LogService");
    notice.category = QStringLiteral("queue.critical_overload");
    notice.process_id = processId;
    notice.thread_id = threadId;
    notice.sequence = sequence;
    notice.message = QStringLiteral("Critical log queue limit reached; using emergency output.");
    notice.fields.insert(QStringLiteral("pending_critical_limit"), pendingCriticalLimit);
    return notice;
}

}  // namespace VaporView::LoggingInternal
