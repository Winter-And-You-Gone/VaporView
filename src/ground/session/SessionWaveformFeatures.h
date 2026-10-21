#pragma once

#include <QJsonObject>
#include <QStringList>
#include <QVector>
#include <limits>

namespace VaporView::Ground
{
// A row of the existing FPGA feature index, never a peak recomputed from raw data.
struct WaveformFeature
{
    QString id, sessionKey, sessionDirectory, csvFilename;
    int csvLine = 0;
    quint64 hostTimeUs = 0, waveformSequence = 0, fpgaTick = 0, segmentId = 0;
    quint32 channel = 0, cycleId = 0, qualityFlags = 0, originalPoints = 0;
    qint64 peakIndex = -1;
    double peak = std::numeric_limits<double>::quiet_NaN();
    QStringList headers, fields;
    QString sourceError() const;
    QString invalidReason() const;
    QJsonObject toJson() const;
    static WaveformFeature fromJson(const QJsonObject& json, QString *error);
};

struct WaveformFeaturesResult
{
    bool success = false;
    QString error;
    QVector<WaveformFeature> records;
};

class SessionWaveformFeatures final
{
public:
    static WaveformFeaturesResult read(const QString& sessionDirectory);
    static WaveformFeaturesResult readFile(const QString& filename, const QString& sessionDirectory);
};
}
