#ifndef VAPORVIEW_BOUNDED_LRU_CACHE_H
#define VAPORVIEW_BOUNDED_LRU_CACHE_H

#include <QHash>
#include <QList>

template<typename Key, typename Value>
class BoundedLruCache
{
public:
    explicit BoundedLruCache(int capacity)
        : capacity_(qMax(0, capacity))
    {
    }

    bool find(const Key& key, Value *value)
    {
        const auto it = values_.constFind(key);
        if (it == values_.constEnd())
        {
            return false;
        }

        if (value)
        {
            *value = it.value();
        }
        recency_.removeAll(key);
        recency_.append(key);
        return true;
    }

    void insert(const Key& key, const Value& value)
    {
        if (capacity_ == 0)
        {
            return;
        }

        values_.insert(key, value);
        recency_.removeAll(key);
        recency_.append(key);
        while (values_.size() > capacity_)
        {
            values_.remove(recency_.takeFirst());
        }
    }

    void clear()
    {
        values_.clear();
        recency_.clear();
    }

    int size() const
    {
        return values_.size();
    }

    bool contains(const Key& key) const
    {
        return values_.contains(key);
    }

private:
    int capacity_ = 0;
    QHash<Key, Value> values_;
    QList<Key> recency_;
};

#endif
