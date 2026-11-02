#pragma once

#include "WaterCalibration.h"
#include <QSet>

namespace VaporView::Ground::Calibration
{
struct PeakRecord
{
    HarmonicPeakFeature feature;
    QString taskId, stageId; // Original acquisition provenance, retained in revisions.
    Environment environment;
    QJsonObject deviceIdentity;
    QJsonObject sensorReadings; // Each source retains its own receipt time and FPGA tick.
    QString alignment = QStringLiteral("unavailable");
    QJsonObject toJson() const;
    static PeakRecord fromJson(const QJsonObject&, QString *error);
};
struct ExclusionOperation
{
    QString recordId, reason, utc;
    bool excluded = false;
    int revision = 0;
    QJsonObject toJson() const;
};
struct StageStatistics
{
    int raw = 0, valid = 0, invalid = 0, excluded = 0, used = 0;
    double mean = 0, standardDeviation = 0, minimum = 0, maximum = 0;
    QJsonObject toJson() const;
};
struct ConcentrationStage
{
    QString id, note, status = QStringLiteral("pending"), startedUtc, completedUtc;
    double concentration = 0;
    quint64 configuredMs = 600000, effectiveMs = 0, wallMs = 0;
    QMap<int, QVector<PeakRecord>> samples;
    QVector<ExclusionOperation> operations;
    bool isExcluded(const QString& recordId) const;
    StageStatistics statistics(int wavelength) const;
    QJsonObject toJson() const;
    static ConcentrationStage create(double concentration, quint64 durationMs = 600000);
};
struct StageFit
{
    Fit parameters; // Numerical model bridge only; no Dataset is persisted in native files.
    int revision = 0;
    QString warning;
    QVector<QString> stageIds;
    QVector<double> deviations;
    QJsonObject toJson() const;
};
struct CalibrationProject
{
    QString id, name, unit = QStringLiteral("ppmv"), createdUtc, softwareVersion;
    QString origin = QStringLiteral("live_acquisition"), parentFileId;
    QJsonObject deviceIdentity;
    QJsonObject hardwareMapping{{"816", 0x31}, {"1368", 0x30}};
    int revision = 1;
    QVector<ConcentrationStage> stages;
    QMap<int, StageFit> fits;
    QVector<Fit> legacyFits;
    static CalibrationProject create(const QString& name, const QString& unit);
    QString validate(bool requireFits = false) const;
    bool exclude(const QString& stageId, int wavelength, const QSet<QString>& records,
                 bool excluded, const QString& reason, QString *error);
    void invalidateFits();
    QMap<int, QString> fit();
    Fit calculationFit(int wavelength) const;
    QJsonObject toJson() const;
    static CalibrationProject fromJson(const QJsonObject&, QString *error);
    // Legacy stages are deliberately absent; immutable original fits retain provenance.
    static CalibrationProject importLegacy(const Fit& fit816, const Fit& fit1368, QString *error);
};
QString calibrationUtcNow();
QString calibrationUuid();
QByteArray calibrationDigest(const QJsonObject&);
}
