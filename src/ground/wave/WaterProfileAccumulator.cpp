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

namespace {
quint64 cellKey(double height, double concentration, double heightStep, double concentrationStep)
{
    const auto index = [](double value, double step) {
        return quint32(std::min(std::floor(value / step), double(std::numeric_limits<quint32>::max())));
    };
    return (quint64(index(height, heightStep)) << 32) | index(concentration, concentrationStep);
}
}

void WaterProfileAccumulator::Channel::mergeRawPoint(const RawPoint &point)
{
    const auto key = cellKey(point.heightM, point.concentration, rawHeightStepM, rawConcStep);
    const auto it = rawCellIndex.constFind(key);
    if (it == rawCellIndex.cend()) {
        rawCellIndex.insert(key, int(rawPoints.size()));
        rawPoints.append(point);
        return;
    }
    auto &stored = rawPoints[it.value()];
    const auto count = stored.sampleCount + point.sampleCount;
    if (point.timeMs >= stored.timeMs) stored = point;
    stored.sampleCount = count;
}

QVector<WaterProfileAccumulator::RawPoint> WaterProfileAccumulator::rawPoints(int channel) const
{
    return channel >= 0 && channel < int(channels_.size()) ? channels_[channel].rawPoints : QVector<RawPoint>{};
}

quint64 WaterProfileAccumulator::rawSampleCount(int channel) const
{
    return channel >= 0 && channel < int(channels_.size()) ? channels_[channel].rawSampleCount : 0;
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
    ++data.rawSampleCount;
    data.mergeRawPoint({heightM, concentration, timeMs, 1});
    while (data.rawPoints.size() > maxUnbinnedPoints) {
        data.rawHeightStepM *= 2;
        data.rawConcStep *= 2;
        QVector<RawPoint> previous;
        previous.swap(data.rawPoints);
        data.rawCellIndex.clear();
        data.rawPoints.reserve(previous.size());
        for (const auto &point : previous) data.mergeRawPoint(point);
    }
    ++revision_;
    return true;
}

QVector<WaterProfileAccumulator::Layer> WaterProfileAccumulator::layers(int channel, int resolutionM) const
{
    if (channel < 0 || channel >= int(channels_.size()) || resolutionM == unbinnedResolution ||
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

quint64 WaterProfileAccumulator::revision() const
{
    return revision_;
}

void WaterProfileAccumulator::clearChannel(int channel)
{
    if (rawSampleCount(channel) == 0) return;
    channels_[channel] = {};
    ++revision_;
}

void WaterProfileAccumulator::clearAll()
{
    if (!channels_[0].rawSampleCount && !channels_[1].rawSampleCount) return;
    channels_ = {};
    ++revision_;
}
