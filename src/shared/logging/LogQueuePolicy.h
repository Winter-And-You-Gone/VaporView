#ifndef VaporView_LOG_QUEUE_POLICY_H_
#define VaporView_LOG_QUEUE_POLICY_H_

#include "LogRecord.h"

#include <QMutex>
#include <QString>
#include <QWaitCondition>

#include <deque>
#include <list>
#include <memory>
#include <optional>

namespace VaporView::LoggingInternal
{

// The normal queue remains fixed at 8192 records. Critical records may exceed
// that base capacity only while their total pending count stays below the
// independent hard limit.
inline constexpr qsizetype kLogQueueCapacity = 8192;
inline constexpr qsizetype kMaxPendingCriticalRecords = 64;

struct LogCompletionState
{
    QMutex mutex;
    QWaitCondition condition;
    bool completed = false;
    bool success = false;
};

struct QueuedLogRecord
{
    LogRecord record;
    std::shared_ptr<LogCompletionState> completion;
};

enum class QueueEnqueueAction
{
    Enqueued,
    DroppedIncoming,
    EmergencyWrite,
};

struct QueueEnqueueResult
{
    QueueEnqueueAction action = QueueEnqueueAction::DroppedIncoming;
    std::optional<QueuedLogRecord> evicted = std::nullopt;
    quint64 dropped_increment = 0;
    bool critical_overload_started = false;
    qsizetype pending_critical_limit = kMaxPendingCriticalRecords;
};

struct LogQueueStats
{
    qsizetype size = 0;
    qsizetype pending_critical = 0;
    qsizetype max_observed_size = 0;
    quint64 priority_index_lookups = 0;
    bool critical_overload_active = false;
};

// This component has no internal lock. LogWriterThread protects every call with
// its queue mutex. A std::list keeps FIFO iterators stable, while the Debug and
// Info indexes make overload eviction O(1) without scanning or copying records.
class LogRecordQueue final
{
public:
    explicit LogRecordQueue(qsizetype capacity = kLogQueueCapacity,
                            qsizetype maxPendingCritical = kMaxPendingCriticalRecords);

    QueueEnqueueResult enqueue(QueuedLogRecord record);
    std::optional<QueuedLogRecord> takeNext();
    void markProcessed(LogLevel level);

    bool empty() const;
    qsizetype size() const;
    LogQueueStats stats() const;

#ifdef VAPORVIEW_LOGGING_TEST_HOOKS
    bool setMaxPendingCriticalForTest(qsizetype limit);
#endif

private:
    using Queue = std::list<QueuedLogRecord>;
    using Iterator = Queue::iterator;

    void append(QueuedLogRecord record);
    std::optional<QueuedLogRecord> evictDebug();
    std::optional<QueuedLogRecord> evictInfo();

    qsizetype capacity_;
    qsizetype max_pending_critical_;
    qsizetype pending_critical_ = 0;
    qsizetype max_observed_size_ = 0;
    quint64 priority_index_lookups_ = 0;
    bool critical_overload_active_ = false;
    Queue queue_;
    std::deque<Iterator> debug_index_;
    std::deque<Iterator> info_index_;
};

LogRecord makeDropNotice(quint64 dropped,
                         quint64 sequence,
                         quint64 timestampUs,
                         const QString& timestampUtc,
                         quint64 processId,
                         quint64 threadId);

LogRecord makeCriticalOverloadNotice(quint64 sequence,
                                     quint64 timestampUs,
                                     const QString& timestampUtc,
                                     quint64 processId,
                                     quint64 threadId,
                                     qsizetype pendingCriticalLimit);

}  // namespace VaporView::LoggingInternal

#endif
