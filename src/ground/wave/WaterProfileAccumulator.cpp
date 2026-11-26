#include "WaterProfileAccumulator.h"

#include <algorithm>
#include <cmath>
#include <limits>

void WaterProfileAccumulator::Moments::add(double value)
{
    const double delta = value - mean;
    mean += delta / ++count;
    m2 += delta * (value - mean);
}

void WaterProfileAccumulator::Moments::merge(const Moments &other)
{
    if (!count) { *this = other; return; }
    const double delta = other.mean - mean;
    const quint64 combined = count + other.count;
    const double fraction = double(other.count) / combined;
    m2 += other.m2 + delta * delta * (count * fraction);
    mean += delta * fraction;
    count = combined;
}

bool WaterProfileAccumulator::append(int channel, const QString &id, qint64 timeMs, double concentration, double heightM)
{
    if (channel < 0 || channel >= int(channels_.size()) || id.isEmpty() || !std::isfinite(concentration) ||
        concentration < 0 || !std::isfinite(heightM) || heightM < 0 || heightM > maximumHeightM) return false;
    auto &data = channels_[channel];
    if (data.seen.contains(id)) return false;
    if (data.fifo.size() == recentIdCapacity) {
        data.seen.remove(data.fifo.front());
        data.fifo.pop_front();
    }
    data.fifo.push_back(id);
    data.seen.insert(id);
    data.bins[int(std::floor(heightM))].add(concentration);
    if (!data.latest || timeMs >= data.latest->timeMs) data.latest = Point{heightM, concentration, timeMs};
    return true;
}

QVector<WaterProfileAccumulator::Layer> WaterProfileAccumulator::layers(int channel, int resolutionM) const
{
    if (channel < 0 || channel >= int(channels_.size()) ||
        std::find(resolutions.begin(), resolutions.end(), resolutionM) == resolutions.end()) return {};
    QVector<Layer> result;
    const auto &bins = channels_[channel].bins;
    for (auto it = bins.cbegin(); it != bins.cend();) {
        const int index = it.key() / resolutionM;
        Moments group;
        do { group.merge(it.value()); ++it; } while (it != bins.cend() && it.key() / resolutionM == index);
        const int low = index * resolutionM;
        const int high = std::min(low + resolutionM, maximumHeightM);
        // Sample standard deviation. A singleton has no estimate.
        const double deviation = group.count < 2 ? std::numeric_limits<double>::quiet_NaN()
            : std::sqrt(std::max(0.0, group.m2) / (group.count - 1));
        result.append({index, low, high, (low + high) * 0.5, group.mean, deviation, group.count});
    }
    return result;
}

std::optional<WaterProfileAccumulator::Point> WaterProfileAccumulator::latest(int channel) const
{
    return channel >= 0 && channel < int(channels_.size()) ? channels_[channel].latest : std::nullopt;
}

qsizetype WaterProfileAccumulator::recentIdCount(int channel) const
{
    return channel >= 0 && channel < int(channels_.size()) ? channels_[channel].seen.size() : 0;
}

void WaterProfileAccumulator::clearChannel(int channel)
{
    if (channel >= 0 && channel < int(channels_.size())) channels_[channel] = {};
}

void WaterProfileAccumulator::clearAll()
{
    for (auto &channel : channels_) channel = {};
}
