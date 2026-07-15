#include "ground/session/SessionIndex.h"

#include <algorithm>
#include <iterator>

namespace VaporView::Ground::Session
{

int closestTimestampIndex(const QVector<quint64>& timestampsUs, quint64 timestampUs)
{
    if (timestampsUs.isEmpty())
    {
        return -1;
    }

    const auto lower = std::lower_bound(timestampsUs.cbegin(), timestampsUs.cend(), timestampUs);
    if (lower == timestampsUs.cbegin())
    {
        return 0;
    }
    if (lower == timestampsUs.cend())
    {
        return timestampsUs.size() - 1;
    }

    const int upperIndex = static_cast<int>(std::distance(timestampsUs.cbegin(), lower));
    const int lowerIndex = upperIndex - 1;
    const quint64 lowerDelta = timestampUs - timestampsUs.at(lowerIndex);
    const quint64 upperDelta = timestampsUs.at(upperIndex) - timestampUs;
    return lowerDelta <= upperDelta ? lowerIndex : upperIndex;
}

double measuredRateHz(const QVector<quint64>& timestampsUs)
{
    quint64 firstUs = 0;
    quint64 lastUs = 0;
    int validCount = 0;
    for (quint64 timestampUs : timestampsUs)
    {
        if (timestampUs == 0)
        {
            continue;
        }
        if (validCount == 0)
        {
            firstUs = timestampUs;
        }
        lastUs = timestampUs;
        ++validCount;
    }

    if (validCount < 2 || lastUs <= firstUs)
    {
        return 0.0;
    }

    const double durationSeconds = static_cast<double>(lastUs - firstUs) / 1000000.0;
    return durationSeconds > 0.0
        ? static_cast<double>(validCount - 1) / durationSeconds
        : 0.0;
}

}  // namespace VaporView::Ground::Session
