#pragma once

#include <QByteArray>
#include <QVector>

#include <cstdint>
#include <map>
#include <optional>

namespace VaporView::FpgaWave
{

struct Fragment
{
    quint16 source = 0;
    quint16 message = 0;
    quint32 flags = 0;
    quint32 cycleId = 0;
    quint64 timestamp = 0;
    QByteArray payload;
};

struct CompletedStream
{
    quint16 source = 0;
    quint16 message = 0;
    quint32 flags = 0;
    quint32 cycleId = 0;
    quint64 timestamp = 0;
    quint16 schema = 0;
    quint16 format = 0;
    quint32 rate = 0;
    quint32 totalPoints = 0;
    quint32 bytesPerPoint = 0;
    bool complete = false;
    bool partial = false;
    bool overflow = false;
    bool continuityError = false;
    QVector<qint32> signed32Samples;
    QByteArray pointBytes;
};

// Aggregates the schema-2 RAW/DLIA fragments documented by Vapor_Radar_App_V2.
// It never interprets non-I32 point formats as measurements; those bytes remain
// available in pointBytes for a later format-specific decoder.
class Assembler
{
public:
    std::optional<CompletedStream> accept(const Fragment& fragment);
    void clear();
    std::size_t pendingStreamCount() const;

private:
    struct Key
    {
        quint16 source = 0;
        quint16 message = 0;
        quint32 cycleId = 0;
        quint64 timestamp = 0;

        bool operator<(const Key& other) const
        {
            if (source != other.source) return source < other.source;
            if (message != other.message) return message < other.message;
            if (cycleId != other.cycleId) return cycleId < other.cycleId;
            return timestamp < other.timestamp;
        }
    };

    struct Pending
    {
        CompletedStream stream;
        quint16 fragmentCount = 0;
        std::map<quint16, QByteArray> fragments;
        std::map<quint16, quint32> firstPoints;
        std::map<quint16, quint32> pointCounts;
    };

    static bool parseHeader(const Fragment& fragment, Pending& pending,
                            quint16& fragmentIndex, quint32& firstPoint,
                            quint32& fragmentPoints, quint32& bytesPerPoint);
    static std::optional<CompletedStream> finish(Pending& pending);
    std::map<Key, Pending> pending_;
};

}  // namespace VaporView::FpgaWave
