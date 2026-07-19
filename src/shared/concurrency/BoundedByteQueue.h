#ifndef VAPORVIEW_BOUNDED_BYTE_QUEUE_H
#define VAPORVIEW_BOUNDED_BYTE_QUEUE_H

#include <QtGlobal>

#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <utility>

namespace VaporView
{

template<typename Record>
class BoundedByteQueue
{
public:
    enum class PushStatus
    {
        Enqueued,
        Closed,
        Full,
    };

    struct PushResult
    {
        PushStatus status = PushStatus::Closed;
        quint64 queuedBytes = 0;
        quint64 droppedRecords = 0;
    };

    explicit BoundedByteQueue(
        quint64 capacityBytes,
        quint64 capacityRecords = std::numeric_limits<quint64>::max())
        : capacityBytes_(capacityBytes)
        , capacityRecords_(capacityRecords)
    {
    }

    void reset(bool open)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        records_.clear();
        queuedBytes_ = 0;
        droppedRecords_ = 0;
        open_ = open;
    }

    PushResult push(Record record, quint64 bytes)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!open_)
        {
            return {PushStatus::Closed, queuedBytes_, droppedRecords_};
        }
        if (bytes > capacityBytes_ ||
            queuedBytes_ > capacityBytes_ - bytes ||
            static_cast<quint64>(records_.size()) >= capacityRecords_)
        {
            ++droppedRecords_;
            return {PushStatus::Full, queuedBytes_, droppedRecords_};
        }

        queuedBytes_ += bytes;
        records_.push_back(Entry{std::move(record), bytes});
        const PushResult result{PushStatus::Enqueued, queuedBytes_, droppedRecords_};
        condition_.notify_one();
        return result;
    }

    bool waitPop(Record *record)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this]() { return !records_.empty() || !open_; });
        if (records_.empty())
        {
            return false;
        }

        Entry entry = std::move(records_.front());
        records_.pop_front();
        queuedBytes_ -= entry.bytes;
        if (record)
        {
            *record = std::move(entry.record);
        }
        return true;
    }

    bool tryPop(Record *record)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (records_.empty())
        {
            return false;
        }

        Entry entry = std::move(records_.front());
        records_.pop_front();
        queuedBytes_ -= entry.bytes;
        if (record)
        {
            *record = std::move(entry.record);
        }
        return true;
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return records_.empty();
    }

    void close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            open_ = false;
        }
        condition_.notify_all();
    }

    quint64 droppedRecords() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return droppedRecords_;
    }

private:
    struct Entry
    {
        Record record;
        quint64 bytes = 0;
    };

    quint64 capacityBytes_ = 0;
    quint64 capacityRecords_ = 0;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<Entry> records_;
    quint64 queuedBytes_ = 0;
    quint64 droppedRecords_ = 0;
    bool open_ = false;
};

}  // namespace VaporView

#endif
