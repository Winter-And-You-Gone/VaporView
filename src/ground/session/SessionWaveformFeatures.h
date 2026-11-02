#pragma once

#include <QJsonObject>
#include <QStringList>
#include <QVector>
#include <limits>

namespace VaporView::Ground
{
// A row of the existing FPGA feature index, never a peak recomputed from raw data.
struct HarmonicPeakFeature
{
    QString id, sessionKey, sessionDirectory, csvFilename;
    int csvLine = 0;
    quint64 hostTimeUs = 0, waveformSequence = 0, fpgaTick = 0, segmentId = 0;
    quint32 channel = 0, cycleId = 0, qualityFlags = 0, originalPoints = 0;
    qint64 peakIndex = -1;
    double peak = std::numeric_limits<double>::quiet_NaN();
    QStringList headers, fields;
    QString streamIdentity; // Native live-cycle provenance, independent of Session/CSV.
    quint32 validPoints = 0;
    QString sourceError() const;
    QString invalidReason() const;
    QJsonObject toJson() const;
    static HarmonicPeakFeature fromJson(const QJsonObject& json, QString *error);
};

struct HarmonicPeakFeaturesResult
{
    bool success = false;
    QString error;
    QVector<HarmonicPeakFeature> records;
};

class SessionWaveformFeatures final
{
public:
    static HarmonicPeakFeaturesResult read(const QString& sessionDirectory);
    static HarmonicPeakFeaturesResult readFile(const QString& filename, const QString& sessionDirectory);
};
}
