#include "FpgaWaveformAssembler.h"

#include <algorithm>

namespace VaporView::FpgaWave
{
namespace
{
quint16 u16(const char *data)
{
    return quint16(static_cast<unsigned char>(data[0]))
        | (quint16(static_cast<unsigned char>(data[1])) << 8);
}

quint32 u32(const char *data)
{
    return quint32(static_cast<unsigned char>(data[0]))
        | (quint32(static_cast<unsigned char>(data[1])) << 8)
        | (quint32(static_cast<unsigned char>(data[2])) << 16)
        | (quint32(static_cast<unsigned char>(data[3])) << 24);
}

bool isRawSource(quint16 source)
{
    return source == 0x0020 || source == 0x0021;
}

bool isDliaSource(quint16 source)
{
    return source == 0x0030 || source == 0x0031;
}
}

bool Assembler::parseHeader(const Fragment& fragment, Pending& pending,
                            quint16& fragmentIndex, quint32& firstPoint,
                            quint32& fragmentPoints, quint32& bytesPerPoint)
{
    const QByteArray& payload = fragment.payload;
    if (payload.size() < 32)
        return false;

    const char *data = payload.constData();
    const quint16 schema = u16(data);
    pending.stream.schema = schema;
    if (schema != 2)
        return false;

    if (isRawSource(fragment.source))
    {
        pending.stream.format = u16(data + 12);
        pending.stream.rate = u32(data + 4);
        pending.stream.totalPoints = u32(data + 8);
        fragmentIndex = u16(data + 16);
        pending.fragmentCount = u16(data + 18);
        firstPoint = u32(data + 20);
        fragmentPoints = u32(data + 24);
        bytesPerPoint = 4;
    }
    else if (isDliaSource(fragment.source))
    {
        pending.stream.format = u16(data + 2);
        pending.stream.rate = u32(data + 4);
        pending.stream.totalPoints = u32(data + 8);
        fragmentIndex = u16(data + 12);
        pending.fragmentCount = u16(data + 14);
        firstPoint = u32(data + 16);
        fragmentPoints = u32(data + 20);
        bytesPerPoint = u32(data + 24);
        if (bytesPerPoint == 0)
            return false;
    }
    else
    {
        return false;
    }

    pending.stream.bytesPerPoint = bytesPerPoint;
    if (pending.fragmentCount == 0 || fragmentIndex >= pending.fragmentCount)
        return false;
    const qint64 expectedBytes = 32LL + qint64(fragmentPoints) * bytesPerPoint;
    if (expectedBytes != payload.size())
        return false;
    return true;
}

std::optional<CompletedStream> Assembler::finish(Pending& pending)
{
    if (pending.fragmentCount == 0 || pending.fragments.size() != pending.fragmentCount)
        return std::nullopt;

    CompletedStream result = pending.stream;
    // A full fragment set is only a valid completed scan when the protocol
    // does not mark it partial/overflow and all indices are continuous.
    result.complete = true;
    // VLP1 flags: bit3 is OVERFLOW_SINCE_LAST and bit5 is PARTIAL_DATA.
    result.overflow = (result.flags & (1u << 3)) != 0;
    result.partial = (result.flags & (1u << 5)) != 0;

    quint32 expectedPoint = 0;
    for (quint16 index = 0; index < pending.fragmentCount; ++index)
    {
        const auto first = pending.firstPoints.find(index);
        const auto count = pending.pointCounts.find(index);
        const auto bytes = pending.fragments.find(index);
        if (first == pending.firstPoints.end() || count == pending.pointCounts.end()
            || bytes == pending.fragments.end() || first->second != expectedPoint)
        {
            result.continuityError = true;
        }
        if (bytes != pending.fragments.end())
            result.pointBytes.append(bytes->second);
        if (count != pending.pointCounts.end())
            expectedPoint += count->second;
    }

    if (result.totalPoints != 0 && expectedPoint != result.totalPoints)
        result.continuityError = true;

    result.complete = !result.partial && !result.overflow && !result.continuityError;

    if (result.format == 0 && result.bytesPerPoint == 4
        && result.pointBytes.size() % 4 == 0)
    {
        result.signed32Samples.reserve(result.pointBytes.size() / 4);
        for (int offset = 0; offset < result.pointBytes.size(); offset += 4)
        {
            const quint32 value = u32(result.pointBytes.constData() + offset);
            result.signed32Samples.push_back(static_cast<qint32>(value));
        }
    }
    return result;
}

std::optional<CompletedStream> Assembler::accept(const Fragment& fragment)
{
    Key key{fragment.source, fragment.message, fragment.cycleId, fragment.timestamp};
    Pending& pending = pending_[key];
    if (pending.fragments.empty())
    {
        pending.stream.source = fragment.source;
        pending.stream.message = fragment.message;
        pending.stream.flags = fragment.flags;
        pending.stream.cycleId = fragment.cycleId;
        pending.stream.timestamp = fragment.timestamp;
    }

    quint16 fragmentIndex = 0;
    quint32 firstPoint = 0;
    quint32 fragmentPoints = 0;
    quint32 bytesPerPoint = 0;
    const bool hasExisting = !pending.fragments.empty();
    const quint16 oldSchema = pending.stream.schema;
    const quint16 oldFormat = pending.stream.format;
    const quint32 oldRate = pending.stream.rate;
    const quint32 oldTotalPoints = pending.stream.totalPoints;
    const quint32 oldBytesPerPoint = pending.stream.bytesPerPoint;
    if (!parseHeader(fragment, pending, fragmentIndex, firstPoint, fragmentPoints, bytesPerPoint))
    {
        pending.stream.partial = true;
        pending.stream.continuityError = true;
        return std::nullopt;
    }

    if (hasExisting && (oldSchema != pending.stream.schema || oldFormat != pending.stream.format
                        || oldRate != pending.stream.rate
                        || oldTotalPoints != pending.stream.totalPoints
                        || oldBytesPerPoint != pending.stream.bytesPerPoint))
    {
        pending.stream.continuityError = true;
    }

    if (!pending.fragments.empty() && pending.stream.flags != fragment.flags)
    {
        pending.stream.continuityError = true;
        pending.stream.flags |= fragment.flags;
    }

    const qint64 pointByteCount = qint64(fragmentPoints) * bytesPerPoint;
    const QByteArray pointBytes = fragment.payload.mid(32, static_cast<int>(pointByteCount));
    if (pending.fragments.find(fragmentIndex) != pending.fragments.end())
        pending.stream.continuityError = true;
    pending.fragments[fragmentIndex] = pointBytes;
    pending.firstPoints[fragmentIndex] = firstPoint;
    pending.pointCounts[fragmentIndex] = fragmentPoints;

    if (pending.fragments.size() != pending.fragmentCount)
        return std::nullopt;

    auto result = finish(pending);
    pending_.erase(key);
    return result;
}

void Assembler::clear()
{
    pending_.clear();
}

std::size_t Assembler::pendingStreamCount() const
{
    return pending_.size();
}

}  // namespace VaporView::FpgaWave
