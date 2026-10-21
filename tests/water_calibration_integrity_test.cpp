#include "WaterCalibrationFixture.h"
#include "ground/session/SessionCsv.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cmath>

using namespace VaporView::Ground;
using namespace VaporView::Ground::Calibration;
using namespace WaterCalibrationTest;

namespace
{
int failures = 0;
void expect(bool condition, const char *message)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
void write(const QString& filename, const QByteArray& bytes)
{
    QFile file(filename); check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "write software-test fixture");
    check(file.write(bytes) == bytes.size(), "complete fixture write");
}
QByteArray csv(const QStringList& headers, const QStringList& fields)
{
    return (headers.join(',') + '\n' + fields.join(',') + '\n').toUtf8();
}
Sample atPeak(Sample sample, double peak)
{
    sample.feature.peak = peak;
    for (const auto *key : {"peak", "max_value"})
        sample.feature.fields[sample.feature.headers.indexOf(QLatin1String(key))] = QString::number(peak, 'g', 17);
    return sample;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temp; check(temp.isValid(), "isolated software-test scope");
    const auto first = recordSession(temp.filePath("first"), 1);
    const auto second = recordSession(temp.filePath("second"), 2);
    const auto third = recordSession(temp.filePath("third"), 3);
    const auto repeated = recordSession(temp.filePath("independent_repeat"), 1);
    auto data = Dataset::create("SOFTWARE TEST ONLY", 816, 0x30, "ppmv");
    data.samples = importSession(first, data, 2, true).samples;
    const auto independent = importSession(repeated, data, 2, true);
    expect(independent.success && independent.samples.size() == 1, "independent measurement with identical peak is retained");
    expect(importSession(first, data, 2, true).duplicates == 1, "same original period is deduplicated");
    data.samples += importSession(second, data, 4, true).samples;
    data.samples += importSession(third, data, 5, true).samples;
    auto model = FitModel::create("linear", 1);
    const auto fitted = model->fit(data); check(fitted.success, "fit independent hand-verifiable dataset");
    const auto& fit = fitted.fit;
    expect(std::abs(fit.a - 1.5) < 1e-12 && std::abs(fit.b - 2.0/3) < 1e-12 &&
        std::abs(fit.rSquared - 27.0/28) < 1e-12 && std::abs(fit.rmse - std::sqrt(1.0/18)) < 1e-12,
        "independent OLS coefficients, R squared and RMSE");
    expect(fit.residuals.size() == 3 && std::abs(fit.residuals[1] - 1.0/3) < 1e-12, "residual direction and input order");
    const auto original = data.samples.first();
    expect(calculate(atPeak(original, fit.rMin), 816, &fit).valid && calculate(atPeak(original, fit.rMax), 816, &fit).valid,
        "both exact range boundaries are inclusive");
    expect(!calculate(atPeak(original, std::nextafter(fit.rMin, -INFINITY)), 816, &fit).valid &&
        !calculate(atPeak(original, std::nextafter(fit.rMax, INFINITY)), 816, &fit).valid, "one-ULP extrapolation is rejected");
    auto untraceable = original; untraceable.feature.id.clear(); untraceable.feature.sessionDirectory.clear();
    expect(!calculate(untraceable, 816, &fit).valid, "no valid concentration without original record provenance");
    auto inconsistent = original; inconsistent.feature.peak = 1.5;
    expect(!calculate(inconsistent, 816, &fit).valid, "typed peak must agree with original feature fields");
    auto unknown = original; unknown.origin = "measured_without_verification";
    expect(!calculate(unknown, 816, &fit).valid, "unknown provenance cannot produce a valid result");
    auto untracedEnvironment = original; untracedEnvironment.environment.temperatureC = 20;
    expect(!calculate(untracedEnvironment, 816, &fit).valid, "available environmental input requires its snapshot source and timestamp");
    auto noVersion = fit; noVersion.id.clear();
    expect(!calculate(original, 816, &noVersion).valid, "valid result requires a stable calibration version identity");

    const auto filename = temp.filePath("features.csv");
    auto headers = original.feature.headers, fields = original.feature.fields;
    const int peakX = headers.indexOf("peak_x");
    auto missingHeaders = headers, missingFields = fields; missingHeaders.removeAt(peakX); missingFields.removeAt(peakX);
    write(filename, csv(missingHeaders, missingFields));
    expect(!SessionWaveformFeatures::readFile(filename, first).success, "missing peak position column is rejected");
    auto badPosition = fields; badPosition[peakX] = "2";
    write(filename, csv(headers, badPosition));
    expect(!SessionWaveformFeatures::readFile(filename, first).success, "inconsistent original peak positions are rejected");
    auto quoted = fields; quoted.last() = '"' + quoted.last();
    write(filename, csv(headers, quoted));
    expect(!SessionWaveformFeatures::readFile(filename, first).success, "unterminated CSV quote is rejected");
    auto truncated = fields; truncated = truncated.mid(0, 8);
    write(filename, csv(headers, truncated));
    expect(!SessionWaveformFeatures::readFile(filename, first).success, "truncated completed row is not silently skipped as non-FPGA");
    write(filename, QByteArray::fromHex("efbbbf") + csv(headers, fields));
    expect(SessionWaveformFeatures::readFile(filename, first).success, "UTF-8 BOM does not hide host timestamp header");
    auto validQuoted = fields; validQuoted[headers.indexOf("source")] = "\"fpga\"";
    write(filename, csv(headers, validQuoted));
    expect(SessionWaveformFeatures::readFile(filename, first).success, "valid quoted CSV fields remain compatible");
    QString csvError;
    const auto escaped = SessionCsv::parseCsvLine(QStringLiteral("\"a,b\",\"a\"\"b\","), &csvError);
    expect(csvError.isEmpty() && escaped == QStringList({"a,b", "a\"b", ""}), "quoted commas, escaped quotes and trailing empty field");
    SessionCsv::parseCsvLine(QStringLiteral("\"2\"0,1"), &csvError);
    expect(!csvError.isEmpty(), "characters after a closing quote cannot change a numeric value");

    // A completed file with a corrupt trailing row must not partially import.
    const auto originalCsv = read(first + "/sensors/waveform_features.csv");
    write(first + "/sensors/waveform_features.csv", originalCsv + "1,2\n");
    const auto failedImport = importSession(first, data, 2, true);
    expect(!failedImport.success && failedImport.samples.isEmpty() && data.samples.size() == 3, "failed import is atomic even after valid preceding rows");
    write(first + "/sensors/waveform_features.csv", originalCsv);

    QString error;
    auto json = data.toJson(); auto samples = json["samples"].toArray(); auto sampleJson = samples[0].toObject();
    sampleJson.remove("excluded"); samples[0] = sampleJson; json["samples"] = samples;
    Dataset::fromJson(json, &error);
    expect(!error.isEmpty(), "missing exclusion state cannot silently restore a sample");
    error.clear(); json = data.toJson(); json["archived"] = "false";
    Dataset::fromJson(json, &error);
    expect(!error.isEmpty(), "invalid archived field type is rejected");
    error.clear(); auto environment = original.environment.toJson(); environment["temperature_c"] = "broken";
    Environment::fromJson(environment, &error);
    expect(!error.isEmpty(), "malformed environmental input is not silently replaced with missing data");
    auto invalidLibraryData = data; invalidLibraryData.revision = 1; invalidLibraryData.samples[0].feature.peak = 1.5;
    Library invalidLibrary; invalidLibrary.datasets.append(invalidLibraryData);
    expect(!Store(temp.filePath("invalid-library.json")).save(invalidLibrary, &error), "reject non-roundtrippable typed dataset before saving");

    data.revision = 1; Library library; library.datasets.append(data);
    auto v1 = model->fit(data); library.fits.append(v1.fit); library.active[816] = v1.fit.id;
    Store store(temp.filePath("library.json")); check(store.save(library, &error), qPrintable(error));
    const auto oldFitJson = library.fits.first().toJson();
    data.revision = 2; data.samples[0].concentration = 3; data.samples[1].excluded = true;
    auto v2 = model->fit(data); check(v2.success, "edit and exclusion leave two usable concentration points");
    library.datasets.append(data); library.fits.append(v2.fit); library.active[816] = v2.fit.id;
    check(store.save(library, &error), qPrintable(error));
    Library reopened; check(store.load(reopened, &error), qPrintable(error));
    expect(reopened.fits.first().toJson() == oldFitJson && reopened.activeFit(816)->id == v2.fit.id,
        "edited input and switched version never alter historical parameters");
    expect(v2.fit.sampleCount == 2 && v2.fit.concentrationCount == 2, "excluded samples do not contribute to fit statistics");
    reopened.active[816].clear(); check(store.save(reopened, &error) && store.load(reopened, &error), "persist disable");
    expect(!calculate(original, 816, reopened.activeFit(816)).valid, "disabled version does not produce a valid result after reopening");
    const auto bytes = read(store.filename()); write(store.filename(), "{broken");
    const auto previousCount = reopened.fits.size();
    expect(!store.load(reopened, &error) && reopened.fits.size() == previousCount, "failed load preserves in-memory history");
    expect(!store.save(reopened, &error) && read(store.filename()) == "{broken", "failed load cannot overwrite a damaged existing library");
    write(store.filename(), bytes);

    // Match recorder lifecycle boundaries without inventing a no-Session stream.
    VaporView::Ground::Session::GroundRecordingService recorder;
    VaporView::Ground::Session::GroundRecordingOptions options; options.baseDirectory = temp.filePath("lifecycle");
    options.fpgaConfiguration = {{"backend", "simulator"}};
    check(recorder.start(options), "start lifecycle Session");
    const auto lifecyclePath = recorder.status().sessionDirectory;
    VaporView::FpgaWave::CompletedStream wave; wave.source = 0x30; wave.totalPoints = 1; wave.complete = true;
    wave.format = 0; wave.cycleId = 7;
    VaporView::FpgaWave::CompletedStream::DliaPoint point; point.hasHarmonics = true; point.h1 = point.h2 = 1;
    wave.dliaPoints.append(point);
    const quint64 timestamp = 1700000000000000ULL;
    check(recorder.recordFpgaHarmonics(timestamp, wave) && recorder.pause(), "first cycle is drained on pause");
    auto lifecycleData = Dataset::create("LIFECYCLE SOFTWARE TEST", 816, 0x30, "ppmv");
    auto batch = importSession(lifecyclePath, lifecycleData, 2, true);
    expect(batch.success && batch.samples.size() == 1, "paused Session exposes exactly its completed cycles");
    lifecycleData.samples = batch.samples;
    expect(!recorder.recordFpgaHarmonics(timestamp, wave), "pause rejects new cycle admission");
    check(recorder.start(options), "resume same Session");
    expect(recorder.status().sessionDirectory == lifecyclePath, "resume keeps original source Session");
    check(recorder.recordFpgaHarmonics(timestamp, wave) && recorder.pause(), "same cycle id and timestamp after resume");
    batch = importSession(lifecyclePath, lifecycleData, 2, true);
    expect(batch.success && batch.samples.size() == 1 && batch.duplicates == 1 &&
        batch.samples.first().feature.segmentId != lifecycleData.samples.first().feature.segmentId,
        "record sequence separates resumed periods even with equal timestamp, cycle and peak");
    lifecycleData.samples += batch.samples;
    check(!recorder.stop().writeFailed, "seal lifecycle Session");
    const auto sealedBytes = read(lifecyclePath + "/sensors/waveform_features.csv");
    expect(!recorder.recordFpgaHarmonics(timestamp, wave) && read(lifecyclePath + "/sensors/waveform_features.csv") == sealedBytes,
        "late cycle cannot write to a stopped Session");
    check(recorder.start(options), "start another Session");
    const auto nextPath = recorder.status().sessionDirectory;
    check(recorder.recordFpgaHarmonics(timestamp, wave) && !recorder.stop().writeFailed, "record second source Session");
    batch = importSession(nextPath, lifecycleData, 2, true);
    expect(nextPath != lifecyclePath && batch.success && batch.samples.size() == 1 &&
        batch.samples.first().feature.sessionKey != lifecycleData.samples.first().feature.sessionKey,
        "new Session retains independent measurement identity without polluting the old source");
    std::cout << "water_calibration_integrity_test: " << failures << " failures (SOFTWARE TEST DATA ONLY)\n";
    return failures ? 1 : 0;
}
