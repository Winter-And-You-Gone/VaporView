#include "geo/TrajectoryReplay.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace VaporView::Geo {
namespace
{

constexpr qint64 kFallbackReplayStepUs = 100000;

} // namespace

void TrajectoryReplay::clear()
{
    samples_.clear();
    has_timestamp_timeline_ = false;
    start_timestamp_us_ = 0;
    end_timestamp_us_ = 0;
    duration_us_ = 0;
    current_index_ = -1;
    current_elapsed_us_ = 0;
    playing_ = false;
}

void TrajectoryReplay::setSamples(std::vector<NavSample> samples)
{
    samples_ = std::move(samples);
    rebuildTimelineCache();
    playing_ = false;
    current_index_ = samples_.empty() ? -1 : static_cast<int>(samples_.size()) - 1;
    current_elapsed_us_ = samples_.empty() ? 0 : duration_us_;
}

bool TrajectoryReplay::hasSamples() const
{
    return !samples_.empty();
}

int TrajectoryReplay::sampleCount() const
{
    return static_cast<int>(samples_.size());
}

int TrajectoryReplay::currentIndex() const
{
    return current_index_;
}

bool TrajectoryReplay::isPlaying() const
{
    return playing_;
}

qint64 TrajectoryReplay::startTimestampUs() const
{
    return has_timestamp_timeline_ ? start_timestamp_us_ : 0;
}

qint64 TrajectoryReplay::endTimestampUs() const
{
    if (samples_.empty())
    {
        return 0;
    }
    return has_timestamp_timeline_ ? end_timestamp_us_ : fallbackDurationUs();
}

qint64 TrajectoryReplay::durationUs() const
{
    return duration_us_;
}

qint64 TrajectoryReplay::elapsedUs() const
{
    if (current_index_ < 0 || samples_.empty())
    {
        return 0;
    }
    return std::clamp<qint64>(current_elapsed_us_, 0, duration_us_);
}

double TrajectoryReplay::speed() const
{
    return speed_;
}

void TrajectoryReplay::setSpeed(double speed)
{
    speed_ = std::isfinite(speed) && speed > 0.0 ? speed : 1.0;
}

void TrajectoryReplay::play()
{
    if (samples_.empty())
    {
        playing_ = false;
        current_index_ = -1;
        return;
    }
    if (current_index_ < 0 || current_index_ >= static_cast<int>(samples_.size()) - 1)
    {
        current_index_ = 0;
        current_elapsed_us_ = 0;
    }
    else
    {
        current_elapsed_us_ = elapsedUs();
    }
    playing_ = true;
}

void TrajectoryReplay::pause()
{
    playing_ = false;
}

void TrajectoryReplay::stop()
{
    playing_ = false;
    current_index_ = samples_.empty() ? -1 : 0;
    current_elapsed_us_ = 0;
}

bool TrajectoryReplay::seek(int index)
{
    if (samples_.empty())
    {
        current_index_ = -1;
        playing_ = false;
        return false;
    }
    current_index_ = std::clamp(index, 0, static_cast<int>(samples_.size()) - 1);
    current_elapsed_us_ = has_timestamp_timeline_
        ? (std::max<qint64>)(0, sampleTimestampUs(current_index_) - start_timestamp_us_)
        : static_cast<qint64>(current_index_) * kFallbackReplayStepUs;
    return true;
}

bool TrajectoryReplay::seekElapsedUs(qint64 elapsedUs)
{
    if (samples_.empty())
    {
        current_index_ = -1;
        playing_ = false;
        return false;
    }

    const qint64 targetElapsed = std::clamp<qint64>(elapsedUs, 0, duration_us_);
    current_elapsed_us_ = targetElapsed;
    if (!has_timestamp_timeline_)
    {
        const int index = static_cast<int>(
            std::clamp<qint64>(targetElapsed / kFallbackReplayStepUs,
                               0,
                               static_cast<qint64>(samples_.size()) - 1));
        current_index_ = index;
        return true;
    }

    const qint64 targetTimestamp = start_timestamp_us_ + targetElapsed;
    const auto upper = std::upper_bound(
        samples_.cbegin(),
        samples_.cend(),
        targetTimestamp,
        [](qint64 timestampUs, const NavSample& sample) {
            return timestampUs < timestampUsForSample(sample);
        });
    current_index_ = upper == samples_.cbegin()
        ? 0
        : static_cast<int>(std::distance(samples_.cbegin(), upper) - 1);
    return true;
}

bool TrajectoryReplay::stepForward()
{
    if (!playing_ || samples_.empty())
    {
        playing_ = false;
        return false;
    }

    const int nextIndex = current_index_ < 0 ? 0 : current_index_ + 1;
    if (nextIndex >= static_cast<int>(samples_.size()))
    {
        current_index_ = static_cast<int>(samples_.size()) - 1;
        playing_ = false;
        return false;
    }

    current_index_ = nextIndex;
    current_elapsed_us_ = has_timestamp_timeline_
        ? (std::max<qint64>)(0, sampleTimestampUs(current_index_) - start_timestamp_us_)
        : static_cast<qint64>(current_index_) * kFallbackReplayStepUs;
    if (current_index_ >= static_cast<int>(samples_.size()) - 1)
    {
        playing_ = false;
    }
    return true;
}

bool TrajectoryReplay::stepByElapsedUs(qint64 deltaUs)
{
    if (!playing_ || samples_.empty())
    {
        playing_ = false;
        return false;
    }

    if (current_index_ < 0)
    {
        current_index_ = 0;
    }

    const qint64 targetElapsed = elapsedUs() + std::max<qint64>(0, deltaUs);
    const qint64 totalDuration = duration_us_;
    seekElapsedUs(targetElapsed);

    if (targetElapsed >= totalDuration || current_index_ >= static_cast<int>(samples_.size()) - 1)
    {
        current_index_ = static_cast<int>(samples_.size()) - 1;
        current_elapsed_us_ = totalDuration;
        playing_ = false;
    }

    return true;
}

const NavSample* TrajectoryReplay::currentSample() const
{
    if (current_index_ < 0 || current_index_ >= static_cast<int>(samples_.size()))
    {
        return nullptr;
    }
    return &samples_[static_cast<std::size_t>(current_index_)];
}

std::vector<NavSample> TrajectoryReplay::visibleSamples() const
{
    if (current_index_ < 0 || samples_.empty())
    {
        return {};
    }
    const auto end = samples_.cbegin() + current_index_ + 1;
    return std::vector<NavSample>(samples_.cbegin(), end);
}

const std::vector<NavSample>& TrajectoryReplay::samples() const
{
    return samples_;
}

int TrajectoryReplay::intervalMs() const
{
    return intervalMsForSpeed(speed_);
}

int TrajectoryReplay::intervalMsForSpeed(double speed)
{
    const double sanitized = std::isfinite(speed) ? std::max(0.1, speed) : 1.0;
    return std::clamp(static_cast<int>(std::lround(100.0 / sanitized)), 10, 500);
}

double TrajectoryReplay::speedFromText(const QString& text, double fallback)
{
    QString normalized = text;
    normalized.remove(QLatin1Char('x'));
    bool ok = false;
    const double speed = normalized.toDouble(&ok);
    if (ok && std::isfinite(speed) && speed > 0.0)
    {
        return speed;
    }
    return fallback > 0.0 ? fallback : 1.0;
}

qint64 TrajectoryReplay::timestampUsForSample(const NavSample& sample)
{
    if (sample.recordTimestampUs > 0)
    {
        return sample.recordTimestampUs;
    }
    return sample.deviceTimestampUs > 0 ? sample.deviceTimestampUs : 0;
}

bool TrajectoryReplay::hasTimestampTimeline() const
{
    return has_timestamp_timeline_;
}

void TrajectoryReplay::rebuildTimelineCache()
{
    has_timestamp_timeline_ = false;
    start_timestamp_us_ = 0;
    end_timestamp_us_ = 0;
    duration_us_ = fallbackDurationUs();
    if (samples_.size() < 2)
    {
        return;
    }
    qint64 previous = timestampUsForSample(samples_.front());
    if (previous <= 0)
    {
        return;
    }

    for (std::size_t index = 1; index < samples_.size(); ++index)
    {
        const qint64 current = timestampUsForSample(samples_[index]);
        if (current <= previous)
        {
            return;
        }
        previous = current;
    }
    has_timestamp_timeline_ = true;
    start_timestamp_us_ = timestampUsForSample(samples_.front());
    end_timestamp_us_ = previous;
    duration_us_ = (std::max<qint64>)(0, end_timestamp_us_ - start_timestamp_us_);
}

qint64 TrajectoryReplay::fallbackDurationUs() const
{
    if (samples_.size() < 2)
    {
        return 0;
    }
    return static_cast<qint64>(samples_.size() - 1) * kFallbackReplayStepUs;
}

qint64 TrajectoryReplay::sampleTimestampUs(int index) const
{
    if (index < 0 || index >= static_cast<int>(samples_.size()))
    {
        return 0;
    }
    if (has_timestamp_timeline_)
    {
        return timestampUsForSample(samples_[static_cast<std::size_t>(index)]);
    }
    return static_cast<qint64>(index) * kFallbackReplayStepUs;
}

} // namespace VaporView::Geo
