#include "geo/TrajectoryReplay.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace VaporView::Geo {

void TrajectoryReplay::clear()
{
    samples_.clear();
    current_index_ = -1;
    playing_ = false;
}

void TrajectoryReplay::setSamples(std::vector<NavSample> samples)
{
    samples_ = std::move(samples);
    playing_ = false;
    current_index_ = samples_.empty() ? -1 : static_cast<int>(samples_.size()) - 1;
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
    if (current_index_ >= static_cast<int>(samples_.size()) - 1)
    {
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

} // namespace VaporView::Geo
