#pragma once

#include <QHash>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>
#include <array>
#include <deque>
#include <optional>

class WaterProfileAccumulator
{
public:
    static constexpr int maximumHeightM = 20000;
    static constexpr int recentIdCapacity = 4096;
    static constexpr int unbinnedResolution = 0;
    static constexpr std::array<int, 6> resolutions{unbinnedResolution, 1, 5, 10, 20, 50};
    static constexpr int maxUnbinnedPoints = 20000;
    static constexpr double initialUnbinnedHeightStepM = 0.05;
    static constexpr double initialUnbinnedConcentrationStep = 0.05;
    struct RawPoint {
        double heightM = 0, concentration = 0;
        qint64 timeMs = 0;
        quint64 sampleCount = 0;
    };
    struct Layer {
        int binIndex, minHeightM, maxHeightM;
        double centerHeightM, meanConcentration, stdDev;
        quint64 sampleCount;
    };
    struct Point { double heightM, concentration; qint64 timeMs; };

    bool append(int channel, const QString &id, qint64 timeMs, double concentration, double heightM);
    QVector<RawPoint> rawPoints(int channel) const;
    quint64 rawSampleCount(int channel) const;
    QVector<Layer> layers(int channel, int resolutionM = 5) const;
    std::optional<Point> latest(int channel) const;
    qsizetype recentIdCount(int channel) const;
    void clearChannel(int channel);
    void clearAll();

private:
    struct Moments {
        quint64 count = 0;
        double mean = 0, m2 = 0;
        void add(double value);
        void merge(const Moments &other);
    };
    struct Channel {
        QVector<RawPoint> rawPoints;
        QHash<quint64, int> rawCellIndex;
        double rawHeightStepM = initialUnbinnedHeightStepM;
        double rawConcStep = initialUnbinnedConcentrationStep;
        quint64 rawSampleCount = 0;
        void mergeRawPoint(const RawPoint &point);
        QMap<int, Moments> bins;
        QSet<QString> seen;
        std::deque<QString> fifo;
        std::optional<Point> latest;
    };
    std::array<Channel, 2> channels_;
};
