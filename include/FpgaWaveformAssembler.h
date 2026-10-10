#pragma once

#include <QByteArray>
#include <QVector>
#include <QMetaType>

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
    quint16 adcBits = 0;
    bool complete = false;
    bool partial = false;
    bool overflow = false;
    bool continuityError = false;
    QVector<qint32> signed32Samples;
    QVector<quint32> unsigned32Samples;
    struct DliaPoint {
        qint32 i1 = 0, q1 = 0, i2 = 0, q2 = 0, h1 = 0, h2 = 0;
        bool hasIq = false, hasHarmonics = false;
    };
    QVector<DliaPoint> dliaPoints;
    // Present fragment spans let offline consumers identify gaps without inventing samples.
    QVector<quint32> fragmentFirstPoints;
    QVector<quint32> fragmentPointCounts;
    QByteArray pointBytes;
};

// RAW schema 1/2 and DLIA schema 2, with bounded pending cycles per source.
// Drain takeExpired() after each accept(); flush() before resetting a connection.
class Assembler
{
public:
    explicit Assembler(std::size_t maxPendingGroupsPerSource = 4);
    std::optional<CompletedStream> accept(const Fragment& fragment);
    QVector<CompletedStream> takeExpired();
    QVector<CompletedStream> flush();
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
        quint64 arrivalOrder = 0;
        quint64 pointByteCount = 0;
    };

    static bool parseHeader(const Fragment& fragment, Pending& pending,
                            quint16& fragmentIndex, quint32& firstPoint,
                            quint32& fragmentPoints, quint32& bytesPerPoint, quint32& headerBytes);
    static CompletedStream finish(const Pending& pending, bool expired = false);
    void expireOldest(quint16 source);
    void rememberCompleted(const Key& key);
    std::map<Key, Pending> pending_;
    std::map<Key, quint64> completed_;
    QVector<CompletedStream> expired_;
    std::size_t maxPendingGroupsPerSource_ = 4;
    quint64 arrivalOrder_ = 0;
};

}  // namespace VaporView::FpgaWave

Q_DECLARE_METATYPE(VaporView::FpgaWave::CompletedStream)
