#pragma once

#include "ground/session/SessionWaveformFeatures.h"
#include <QMap>
#include <memory>
#include <optional>

namespace VaporView::Ground::Calibration
{
constexpr int SchemaVersion = 1;
constexpr int LibrarySchemaVersion = 2;
constexpr quint64 EnvironmentWindowUs = 2000000;
// Confirmed optical wiring: UI channel 2 is 816 nm; UI channel 1 is 1368 nm.
constexpr quint32 channelForWavelength(int wavelengthNm)
{
    return wavelengthNm == 816 ? 0x31 : wavelengthNm == 1368 ? 0x30 : 0;
}

struct Environment
{
    double temperatureC = std::numeric_limits<double>::quiet_NaN();
    double pressureHpa = std::numeric_limits<double>::quiet_NaN();
    quint64 snapshotTimeUs = 0, deltaUs = 0;
    QString source; // Snapshot timestamp, not an asserted sensor acquisition time.
    QJsonObject toJson() const;
    static Environment fromJson(const QJsonObject&, QString *error);
};
struct Sample
{
    WaveformFeature feature;
    double concentration = 0;
    bool excluded = false;
    QString note, origin = QStringLiteral("unverified");
    Environment environment;
    QJsonObject toJson() const;
    static Sample fromJson(const QJsonObject&, QString *error);
};
struct Dataset
{
    QString id, name, unit = QStringLiteral("ppmv"), createdUtc, modifiedUtc, taskId;
    int wavelengthNm = 816;
    quint32 channel = channelForWavelength(816);
    int revision = 0;
    bool archived = false;
    QVector<Sample> samples;
    static Dataset create(const QString& name, int wavelength, quint32 channel, const QString& unit);
    QString validate() const;
    // Apply to current imports/fits/activation, not immutable historical decoding.
    QString hardwareMappingError() const;
    QJsonObject toJson() const;
    static Dataset fromJson(const QJsonObject&, QString *error);
};
struct CalibrationTask
{
    QString id; // Empty for an unchanged legacy single-wavelength task.
    QMap<int, Dataset> datasets;
    static CalibrationTask create(const QString& name, const QString& unit);
    QString validate() const;
};
struct Fit
{
    QString id, model = QStringLiteral("linear"), createdUtc, datasetDigest;
    int modelVersion = 1;
    Dataset dataset; // Immutable, complete input snapshot, including excluded rows.
    double a = 0, b = 0, rMin = 0, rMax = 0, cMin = 0, cMax = 0, rmse = 0;
    double rSquared = std::numeric_limits<double>::quiet_NaN();
    int sampleCount = 0, concentrationCount = 0;
    QVector<double> residuals;
    QJsonObject toJson() const;
    static Fit fromJson(const QJsonObject&, QString *error);
};
struct FitResult { bool success = false; QString error; Fit fit; };

// New scientific models implement this interface and add a versioned factory entry.
class FitModel
{
public:
    virtual ~FitModel() = default;
    virtual FitResult fit(const Dataset&) const = 0;
    virtual double evaluate(const Fit&, double peak) const = 0;
    static std::unique_ptr<FitModel> create(const QString& type, int version);
};
struct ImportResult
{
    bool success = false;
    QString error, warning;
    QVector<Sample> samples;
    int duplicates = 0, invalid = 0;
};
ImportResult importSession(const QString& session, const Dataset&, double concentration, bool softwareTest);
struct TaskImportResult
{
    bool success = false;
    QString error;
    QMap<int, ImportResult> channels;
};
TaskImportResult importTaskSession(const QString& session, const CalibrationTask&, double concentration,
                                   bool softwareTest, bool singleConcentrationConfirmed);

// Compensation is deliberately separate from the initial calibration model.
struct CompensationResult { bool applied = false; double value = 0; QString status; };
class CompensationModel
{
public:
    virtual ~CompensationModel() = default;
    virtual CompensationResult apply(double initial, const Environment&) const = 0;
};
struct ConcentrationResult
{
    bool valid = false, compensated = false;
    double initial = std::numeric_limits<double>::quiet_NaN();
    double value = std::numeric_limits<double>::quiet_NaN();
    QString status, calculatedUtc, calibrationId, datasetId, unit, origin;
    int datasetRevision = 0, wavelengthNm = 0;
    QString algorithm = QStringLiteral("water_initial_linear_v1");
    Sample input;
    QJsonObject toJson() const;
};
ConcentrationResult calculate(const Sample&, int wavelength, const Fit *fit);

struct Library
{
    QVector<Dataset> datasets; // Append-only revisions, deletion is an archived revision.
    QVector<Fit> fits;        // Append-only immutable versions.
    QMap<int, QString> active;
    QByteArray diskDigest;   // Optimistic concurrency token; prevents overwriting another editor.
    const Fit *activeFit(int wavelength) const;
};
class Store final
{
public:
    static QString defaultFilename();
    explicit Store(QString filename = defaultFilename());
    bool load(Library&, QString *error) const;
    bool save(Library&, QString *error) const;
    QString filename() const { return filename_; }
private:
    QString filename_;
};
struct TaskSaveResult
{
    bool success = false;
    QString error;
    CalibrationTask task;
    Library library;
    std::optional<Fit> fit;
};
TaskSaveResult saveTask(const CalibrationTask&, const Library&, const Store&, int fitWavelength = 0);
struct ConcentrationSnapshot
{
    bool success = false;
    QString error, warning;
    QVector<ConcentrationResult> results;
    QVector<Fit> calibrations;
};
// Export one immutable, self-contained result snapshot. Each referenced fit is
// stored once, including its original dataset, independently of active versions.
bool exportResults(const QString& filename, const QVector<ConcentrationResult>&, QString *error,
                   const QVector<Fit>& calibrations = {});
ConcentrationSnapshot readResults(const QString& filename, const QVector<Fit>& legacyCalibrations = {});
}
