#include "geo/TrajectoryReplay.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace VaporView::Geo {
namespace
{

constexpr qint64 kFallbackReplayStepUs = 100000;

} // namespace

void TrajectoryReplay::clear()
{
    samples_.clear();
    current_index_ = -1;
    current_elapsed_us_ = 0;
    playing_ = false;
}

void TrajectoryReplay::setSamples(std::vector<NavSample> samples)
{
    samples_ = std::move(samples);
    playing_ = false;
    current_index_ = samples_.empty() ? -1 : static_cast<int>(samples_.size()) - 1;
    current_elapsed_us_ = samples_.empty() ? 0 : durationUs();
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
    return samples_.empty() ? 0 : sampleTimestampUs(0);
}

qint64 TrajectoryReplay::endTimestampUs() const
{
    if (samples_.empty())
    {
        return 0;
    }
    return hasTimestampTimeline()
        ? sampleTimestampUs(static_cast<int>(samples_.size()) - 1)
        : fallbackDurationUs();
}

qint64 TrajectoryReplay::durationUs() const
{
    if (samples_.size() < 2)
    {
        return 0;
    }
    if (!hasTimestampTimeline())
    {
        return fallbackDurationUs();
    }
    return std::max<qint64>(0, endTimestampUs() - startTimestampUs());
}

qint64 TrajectoryReplay::elapsedUs() const
{
    if (current_index_ < 0 || samples_.empty())
    {
        return 0;
    }
    return std::clamp<qint64>(current_elapsed_us_, 0, durationUs());
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
    current_elapsed_us_ = hasTimestampTimeline()
        ? std::max<qint64>(0, sampleTimestampUs(current_index_) - startTimestampUs())
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

    const qint64 targetElapsed = std::clamp<qint64>(elapsedUs, 0, durationUs());
    current_elapsed_us_ = targetElapsed;
    if (!hasTimestampTimeline())
    {
        const int index = static_cast<int>(
            std::clamp<qint64>(targetElapsed / kFallbackReplayStepUs,
                               0,
                               static_cast<qint64>(samples_.size()) - 1));
        current_index_ = index;
        return true;
    }

    const qint64 targetTimestamp = startTimestampUs() + targetElapsed;
    int selectedIndex = 0;
    for (int index = 0; index < static_cast<int>(samples_.size()); ++index)
    {
        if (sampleTimestampUs(index) <= targetTimestamp)
        {
            selectedIndex = index;
            continue;
        }
        break;
    }
    current_index_ = selectedIndex;
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
    current_elapsed_us_ = hasTimestampTimeline()
        ? std::max<qint64>(0, sampleTimestampUs(current_index_) - startTimestampUs())
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
    const qint64 totalDuration = durationUs();
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
    if (samples_.size() < 2)
    {
        return false;
    }
    const qint64 first = timestampUsForSample(samples_.front());
    const qint64 last = timestampUsForSample(samples_.back());
    return first > 0 && last > first;
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
    const qint64 timestamp = timestampUsForSample(samples_[static_cast<std::size_t>(index)]);
    if (timestamp > 0)
    {
        return timestamp;
    }
    return hasTimestampTimeline()
        ? startTimestampUs() + static_cast<qint64>(index) * kFallbackReplayStepUs
        : static_cast<qint64>(index) * kFallbackReplayStepUs;
}

} // namespace VaporView::Geo
