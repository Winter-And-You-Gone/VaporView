#include "geo/TrajectoryReplay.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace VaporView::Geo {
namespace
{

constexpr auto kFallbackReplayStep = std::chrono::milliseconds(100);

} // namespace

void TrajectoryReplay::clear()
{
    samples_.reset();
    has_timestamp_timeline_ = false;
    start_timestamp_us_ = 0;
    end_timestamp_us_ = 0;
    duration_ = Duration::zero();
    current_index_ = -1;
    current_elapsed_ = Duration::zero();
    playing_ = false;
}

void TrajectoryReplay::setSamples(std::vector<NavSample> samples)
{
    setSamples(std::make_shared<const std::vector<NavSample>>(std::move(samples)));
}

void TrajectoryReplay::setSamples(std::shared_ptr<const std::vector<NavSample>> samples)
{
    samples_ = std::move(samples);
    rebuildTimelineCache();
    playing_ = false;
    current_index_ = hasSamples() ? static_cast<int>(samples_->size()) - 1 : -1;
    current_elapsed_ = hasSamples() ? duration_ : Duration::zero();
}

bool TrajectoryReplay::hasSamples() const
{
    return samples_ && !samples_->empty();
}

int TrajectoryReplay::sampleCount() const
{
    return samples_ ? static_cast<int>(samples_->size()) : 0;
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
    if (!hasSamples())
    {
        return 0;
    }
    return has_timestamp_timeline_ ? end_timestamp_us_ : fallbackDuration().count();
}

TrajectoryReplay::Duration TrajectoryReplay::duration() const
{
    return duration_;
}

TrajectoryReplay::Duration TrajectoryReplay::elapsed() const
{
    if (current_index_ < 0 || !hasSamples())
    {
        return Duration::zero();
    }
    return std::clamp(current_elapsed_, Duration::zero(), duration_);
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
    if (!hasSamples())
    {
        playing_ = false;
        current_index_ = -1;
        return;
    }
    if (current_index_ < 0 || current_index_ >= sampleCount() - 1)
    {
        current_index_ = 0;
        current_elapsed_ = Duration::zero();
    }
    else
    {
        current_elapsed_ = elapsed();
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
    current_index_ = hasSamples() ? 0 : -1;
    current_elapsed_ = Duration::zero();
}

bool TrajectoryReplay::seek(int index)
{
    if (!hasSamples())
    {
        current_index_ = -1;
        playing_ = false;
        return false;
    }
    current_index_ = std::clamp(index, 0, sampleCount() - 1);
    current_elapsed_ = has_timestamp_timeline_
        ? Duration((std::max<qint64>)(0, sampleTimestampUs(current_index_) - start_timestamp_us_))
        : kFallbackReplayStep * current_index_;
    return true;
}

bool TrajectoryReplay::seekElapsed(Duration elapsedValue)
{
    if (!hasSamples())
    {
        current_index_ = -1;
        playing_ = false;
        return false;
    }

    const Duration targetElapsed = std::clamp(elapsedValue, Duration::zero(), duration_);
    current_elapsed_ = targetElapsed;
    if (!has_timestamp_timeline_)
    {
        const int index = static_cast<int>(
            std::clamp<qint64>(targetElapsed / kFallbackReplayStep,
                               0,
                               static_cast<qint64>(sampleCount()) - 1));
        current_index_ = index;
        return true;
    }

    const qint64 targetTimestamp = start_timestamp_us_ + targetElapsed.count();
    const auto upper = std::upper_bound(
        samples().cbegin(),
        samples().cend(),
        targetTimestamp,
        [](qint64 timestampUs, const NavSample& sample) {
            return timestampUs < timestampUsForSample(sample);
        });
    current_index_ = upper == samples().cbegin()
        ? 0
        : static_cast<int>(std::distance(samples().cbegin(), upper) - 1);
    return true;
}

bool TrajectoryReplay::stepForward()
{
    if (!playing_ || !hasSamples())
    {
        playing_ = false;
        return false;
    }

    const int nextIndex = current_index_ < 0 ? 0 : current_index_ + 1;
    if (nextIndex >= sampleCount())
    {
        current_index_ = sampleCount() - 1;
        playing_ = false;
        return false;
    }

    current_index_ = nextIndex;
    current_elapsed_ = has_timestamp_timeline_
        ? Duration((std::max<qint64>)(0, sampleTimestampUs(current_index_) - start_timestamp_us_))
        : kFallbackReplayStep * current_index_;
    if (current_index_ >= sampleCount() - 1)
    {
        playing_ = false;
    }
    return true;
}

bool TrajectoryReplay::stepBy(Duration delta)
{
    if (!playing_ || !hasSamples())
    {
        playing_ = false;
        return false;
    }

    if (current_index_ < 0)
    {
        current_index_ = 0;
    }

    const Duration targetElapsed = elapsed() + (std::max)(Duration::zero(), delta);
    const Duration totalDuration = duration_;
    seekElapsed(targetElapsed);

    if (targetElapsed >= totalDuration || current_index_ >= sampleCount() - 1)
    {
        current_index_ = sampleCount() - 1;
        current_elapsed_ = totalDuration;
        playing_ = false;
    }

    return true;
}

const NavSample* TrajectoryReplay::currentSample() const
{
    if (current_index_ < 0 || current_index_ >= sampleCount())
    {
        return nullptr;
    }
    return &samples()[static_cast<std::size_t>(current_index_)];
}

std::vector<NavSample> TrajectoryReplay::visibleSamples() const
{
    if (current_index_ < 0 || !hasSamples())
    {
        return {};
    }
    const auto end = samples().cbegin() + current_index_ + 1;
    return std::vector<NavSample>(samples().cbegin(), end);
}

const std::vector<NavSample>& TrajectoryReplay::samples() const
{
    static const std::vector<NavSample> empty;
    return samples_ ? *samples_ : empty;
}

std::shared_ptr<const std::vector<NavSample>> TrajectoryReplay::sampleStorage() const
{
    return samples_;
}

std::chrono::milliseconds TrajectoryReplay::interval() const
{
    return intervalForSpeed(speed_);
}

std::chrono::milliseconds TrajectoryReplay::intervalForSpeed(double speed)
{
    const double sanitized = std::isfinite(speed) ? (std::max)(0.1, speed) : 1.0;
    return std::chrono::milliseconds(
        std::clamp(static_cast<int>(std::lround(100.0 / sanitized)), 10, 500));
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
    duration_ = fallbackDuration();
    if (sampleCount() < 2)
    {
        return;
    }
    qint64 previous = timestampUsForSample(samples().front());
    if (previous <= 0)
    {
        return;
    }

    for (std::size_t index = 1; index < samples().size(); ++index)
    {
        const qint64 current = timestampUsForSample(samples()[index]);
        if (current <= previous)
        {
            return;
        }
        previous = current;
    }
    has_timestamp_timeline_ = true;
    start_timestamp_us_ = timestampUsForSample(samples().front());
    end_timestamp_us_ = previous;
    duration_ = Duration((std::max<qint64>)(0, end_timestamp_us_ - start_timestamp_us_));
}

TrajectoryReplay::Duration TrajectoryReplay::fallbackDuration() const
{
    if (sampleCount() < 2)
    {
        return Duration::zero();
    }
    return kFallbackReplayStep * (sampleCount() - 1);
}

qint64 TrajectoryReplay::sampleTimestampUs(int index) const
{
    if (index < 0 || index >= sampleCount())
    {
        return 0;
    }
    if (has_timestamp_timeline_)
    {
        return timestampUsForSample(samples()[static_cast<std::size_t>(index)]);
    }
    return (kFallbackReplayStep * index).count();
}

} // namespace VaporView::Geo
