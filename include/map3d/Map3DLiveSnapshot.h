#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>

#include <array>
#include <cstdint>
#include <functional>
#include <limits>

namespace VaporView::Map3D {

struct Map3DLiveTelemetrySnapshot {
    bool sourceConnected = false;
    qint64 snapshotHostTimeMs = 0;

    bool hasPosition = false;
    double latDeg = std::numeric_limits<double>::quiet_NaN();
    double lonDeg = std::numeric_limits<double>::quiet_NaN();
    double heightM = std::numeric_limits<double>::quiet_NaN();
    QString fixQualityText;
    qint64 navAgeMs = -1;
    bool navStale = false;

    bool hasTemperature = false;
    double temperatureC = std::numeric_limits<double>::quiet_NaN();
    qint64 temperatureAgeMs = -1;
    bool temperatureStale = false;
    QString temperatureSource;

    bool hasHumidity = false;
    double humidityRh = std::numeric_limits<double>::quiet_NaN();
    qint64 humidityAgeMs = -1;
    bool humidityStale = false;
    QString humiditySource;

    bool hasPressure = false;
    double pressureHpa = std::numeric_limits<double>::quiet_NaN();
    qint64 pressureAgeMs = -1;
    bool pressureStale = false;
    QString pressureSource;

    bool hasLidar = false;
    double lidarDistanceM = std::numeric_limits<double>::quiet_NaN();
    qint64 lidarAgeMs = -1;
    bool lidarStale = false;

    bool calibrationSelected = false;
    bool calibrationValid = false;
    QString calibrationName;
    QString calibrationSha256;
    QString waterUnit;
    std::uint32_t calibrationEpoch = 0;
    QString calibrationWarning;

    bool hasInSituWater = false;
    double inSituWater = std::numeric_limits<double>::quiet_NaN();
    qint64 inSituAgeMs = -1;
    bool inSituStale = false;
    qint64 inSituHostTimeMs = 0;
    QString inSituStatus;

    bool hasColumnWater = false;
    double columnWater = std::numeric_limits<double>::quiet_NaN();
    qint64 columnAgeMs = -1;
    bool columnStale = false;
    qint64 columnHostTimeMs = 0;
    QString columnStatus;

};

struct Map3DWaterProfileSnapshot {
    struct Point {
        double heightM = 0.0;
        double concentration = 0.0;
        qint64 timeMs = 0;
        quint64 sampleCount = 0;
    };

    struct Layer {
        int binIndex = 0;
        int minHeightM = 0;
        int maxHeightM = 0;
        double centerHeightM = 0.0;
        double meanConcentration = 0.0;
        double stdDev = 0.0;
        quint64 sampleCount = 0;
    };

    struct Channel {
        quint64 revision = 0;
        int resolutionM = 5;
        bool hasLatest = false;
        double latestHeightM = std::numeric_limits<double>::quiet_NaN();
        double latestConcentration = std::numeric_limits<double>::quiet_NaN();
        qint64 latestTimeMs = 0;
        quint64 totalSamples = 0;
        QVector<Layer> layers;
        QVector<Point> rawPoints;
    };

    bool calibrationSelected = false;
    QString unit;
    QString calibrationName;
    std::array<Channel, 2> channels; // 0 = 1368 nm in-situ, 1 = 816 nm remote column
};

using Map3DProfileRevisionProvider = std::function<quint64()>;
using Map3DWaterProfileProvider =
    std::function<Map3DWaterProfileSnapshot(int inSituResolutionM, int columnResolutionM)>;

} // namespace VaporView::Map3D
