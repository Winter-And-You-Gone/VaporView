#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace VaporView::Map3D {

enum class MapLoadCategory : std::size_t
{
    Earth = 0,
    Imagery = 1,
    Dem = 2,
    Osm = 3,
    Buildings = 4,
    Count = 5,
};

inline constexpr std::size_t kMapLoadCategoryCount =
    static_cast<std::size_t>(MapLoadCategory::Count);

struct MapLoadProgressSnapshot
{
    bool active = false;
    int activeTasks = 0;
    int completedTasks = 0;
    int totalTasks = 0;
    std::array<int, kMapLoadCategoryCount> activeByCategory{};
    std::array<int, kMapLoadCategoryCount> completedByCategory{};
    std::array<int, kMapLoadCategoryCount> totalByCategory{};
    std::uint64_t revision = 0;

    int percent() const
    {
        if (!active || totalTasks <= 0)
        {
            return 0;
        }
        const int raw = (completedTasks * 100) / totalTasks;
        if (raw < 0)
        {
            return 0;
        }
        return raw > 99 ? 99 : raw;
    }
};

class Map3DLoadTracker
{
public:
    static Map3DLoadTracker& instance()
    {
        static Map3DLoadTracker tracker;
        return tracker;
    }

    std::uint64_t beginTask(MapLoadCategory category)
    {
        const std::size_t idx = static_cast<std::size_t>(category);
        if (idx >= kMapLoadCategoryCount)
        {
            return 0;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        auto& slot = slots_[idx];
        ++slot.active;
        ++slot.total;
        ++revision_;
        return slot.epoch;
    }

    void finishTask(MapLoadCategory category, std::uint64_t epoch)
    {
        const std::size_t idx = static_cast<std::size_t>(category);
        if (idx >= kMapLoadCategoryCount)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        auto& slot = slots_[idx];
        if (slot.epoch != epoch || slot.active <= 0)
        {
            return;
        }
        --slot.active;
        ++slot.completed;
        resetCompletedIfAllIdleLocked();
        ++revision_;
    }

    void cancelTask(MapLoadCategory category, std::uint64_t epoch)
    {
        const std::size_t idx = static_cast<std::size_t>(category);
        if (idx >= kMapLoadCategoryCount)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        auto& slot = slots_[idx];
        if (slot.epoch != epoch || slot.active <= 0)
        {
            return;
        }
        --slot.active;
        if (slot.total > slot.completed)
        {
            --slot.total;
        }
        resetCompletedIfAllIdleLocked();
        ++revision_;
    }

    void interruptCategory(MapLoadCategory category)
    {
        const std::size_t idx = static_cast<std::size_t>(category);
        if (idx >= kMapLoadCategoryCount)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        auto& slot = slots_[idx];
        ++slot.epoch;
        if (slot.active != 0 || slot.completed != 0 || slot.total != 0)
        {
            slot.active = 0;
            slot.completed = 0;
            slot.total = 0;
            resetCompletedIfAllIdleLocked();
            ++revision_;
        }
    }

    void interruptAll()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        bool changed = false;
        for (auto& slot : slots_)
        {
            ++slot.epoch;
            if (slot.active != 0 || slot.completed != 0 || slot.total != 0)
            {
                slot.active = 0;
                slot.completed = 0;
                slot.total = 0;
                changed = true;
            }
        }
        if (changed)
        {
            ++revision_;
        }
    }

    MapLoadProgressSnapshot snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        MapLoadProgressSnapshot out;
        out.revision = revision_;
        for (std::size_t i = 0; i < kMapLoadCategoryCount; ++i)
        {
            const auto& slot = slots_[i];
            out.activeByCategory[i] = slot.active;
            out.completedByCategory[i] = slot.completed;
            out.totalByCategory[i] = slot.total;
            out.activeTasks += slot.active;
            out.completedTasks += slot.completed;
            out.totalTasks += slot.total;
        }
        out.active = out.activeTasks > 0;
        return out;
    }

private:
    struct CategorySlot
    {
        int active = 0;
        int completed = 0;
        int total = 0;
        std::uint64_t epoch = 1;
    };

    void resetCompletedIfAllIdleLocked()
    {
        int totalActive = 0;
        for (const auto& slot : slots_)
        {
            totalActive += slot.active;
        }
        if (totalActive == 0)
        {
            for (auto& slot : slots_)
            {
                slot.completed = 0;
                slot.total = 0;
            }
        }
    }

    mutable std::mutex mutex_;
    std::array<CategorySlot, kMapLoadCategoryCount> slots_{};
    std::uint64_t revision_ = 0;
};

class ScopedMapLoadTask
{
public:
    explicit ScopedMapLoadTask(MapLoadCategory category)
        : category_(category)
        , epoch_(Map3DLoadTracker::instance().beginTask(category))
        , open_(true)
    {
    }

    ScopedMapLoadTask(const ScopedMapLoadTask&) = delete;
    ScopedMapLoadTask& operator=(const ScopedMapLoadTask&) = delete;

    ~ScopedMapLoadTask()
    {
        cancel();
    }

    void finish()
    {
        if (open_.exchange(false))
        {
            Map3DLoadTracker::instance().finishTask(category_, epoch_);
        }
    }

    void cancel()
    {
        if (open_.exchange(false))
        {
            Map3DLoadTracker::instance().cancelTask(category_, epoch_);
        }
    }

private:
    MapLoadCategory category_;
    std::uint64_t epoch_;
    std::atomic_bool open_{false};
};

} // namespace VaporView::Map3D
