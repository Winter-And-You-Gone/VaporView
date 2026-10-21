#include "WaterCalibration.h"
#include "ground/session/SessionIndex.h"
#include "ground/session/SessionLoader.h"
#include "shared/config/ApplicationConfig.h"
#include "shared/session/SessionPackageLayout.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace VaporView::Ground::Calibration
{
namespace
{
QString now() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool dateValid(const QString& s) { return QDateTime::fromString(s, Qt::ISODateWithMs).isValid(); }
QByteArray digest(const QByteArray& bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex(); }
QByteArray canonical(const QJsonObject& j) { return QJsonDocument(j).toJson(QJsonDocument::Compact); }
QJsonValue optional(double v) { return std::isfinite(v) ? QJsonValue(v) : QJsonValue(QJsonValue::Null); }
double optional(const QJsonValue& v) { return v.isDouble() ? v.toDouble() : std::numeric_limits<double>::quiet_NaN(); }
bool unitValid(const QString& unit) { return unit == "ppmv" || unit == "mol/mol" || unit == "mg/m3"; }
QString sampleError(const Sample& s)
{
    if (!std::isfinite(s.concentration) || s.concentration < 0) return QStringLiteral("标气浓度必须为非负有限数");
    if (s.origin != "software_test" && s.origin != "unverified") return QStringLiteral("未知数据来源类型");
    if (s.feature.id.isEmpty() || s.feature.sessionDirectory.isEmpty()) return QStringLiteral("峰值来源记录不完整");
    return {};
}

class LinearModel final : public FitModel
{
public:
    FitResult fit(const Dataset& data) const override
    {
        FitResult result;
        result.error = data.validate();
        if (!result.error.isEmpty()) return result;
        QVector<const Sample*> rows;
        QSet<double> concentrations;
        for (const auto& s : data.samples) {
            if (s.excluded || !s.feature.invalidReason().isEmpty()) continue;
            rows.append(&s); concentrations.insert(s.concentration);
        }
        if (rows.size() < 2) { result.error = QStringLiteral("至少需要两个有效样本"); return result; }
        if (concentrations.size() < 2) { result.error = QStringLiteral("至少需要两个不同的有效标气浓度"); return result; }
        long double meanR = 0, meanC = 0;
        for (const auto *s : rows) { meanR += s->feature.peak; meanC += s->concentration; }
        meanR /= rows.size(); meanC /= rows.size();
        long double rr = 0, rc = 0, cc = 0;
        for (const auto *s : rows) {
            const long double r = s->feature.peak - meanR, c = s->concentration - meanC;
            rr += r*r; rc += r*c; cc += c*c;
        }
        auto& f = result.fit;
        f.rMin = f.rMax = rows.first()->feature.peak;
        f.cMin = f.cMax = rows.first()->concentration;
        for (const auto *s : rows) {
            f.rMin = std::min(f.rMin, s->feature.peak); f.rMax = std::max(f.rMax, s->feature.peak);
            f.cMin = std::min(f.cMin, s->concentration); f.cMax = std::max(f.cMax, s->concentration);
        }
        if (!(rr > 0) || f.rMin == f.rMax || !std::isfinite(rr) || !std::isfinite(rc) || !std::isfinite(cc)) {
            result.error = QStringLiteral("峰值无变化或拟合矩阵不可解"); return result;
        }
        f.a = double(rc / rr); f.b = double(meanC - (rc / rr) * meanR);
        long double residualSquares = 0;
        for (const auto *s : rows) {
            const double residual = s->concentration - evaluate(f, s->feature.peak);
            f.residuals.append(residual); residualSquares += static_cast<long double>(residual)*residual;
        }
        f.rmse = std::sqrt(double(residualSquares / rows.size()));
        f.rSquared = cc > 0 ? double(1 - residualSquares / cc) : std::numeric_limits<double>::quiet_NaN();
        if (!std::isfinite(f.a) || !std::isfinite(f.b) || !std::isfinite(f.rmse) || !std::isfinite(f.rSquared)) {
            result.error = QStringLiteral("拟合产生非有限参数，已拒绝保存"); return result;
        }
        f.id = uuid(); f.dataset = data; f.createdUtc = now();
        f.datasetDigest = QString::fromLatin1(digest(canonical(data.toJson())));
        f.sampleCount = int(rows.size()); f.concentrationCount = int(concentrations.size());
        result.success = true;
        return result;
    }
    double evaluate(const Fit& f, double r) const override { return std::fma(f.a, r, f.b); }
};
class UnconfiguredCompensation final : public CompensationModel
{
public:
    CompensationResult apply(double initial, const Environment&) const override
    { return {false, initial, QStringLiteral("有效初始浓度；未进行温压补偿")}; }
};
bool containsSimulation(const QJsonValue& v)
{
    if (v.isObject()) {
        const auto j = v.toObject();
        for (auto it = j.begin(); it != j.end(); ++it) {
            if ((it.key() == "backend" && it.value().toString() == "simulator") ||
                (it.key() == "mode" && it.value().toString() == "simulation") || containsSimulation(it.value())) return true;
        }
    } else if (v.isArray()) { for (const auto& item : v.toArray()) if (containsSimulation(item)) return true; }
    return false;
}
QJsonObject libraryJson(const Library& library)
{
    QJsonArray datasets, fits;
    for (const auto& d : library.datasets) datasets.append(d.toJson());
    for (const auto& f : library.fits) fits.append(f.toJson());
    QJsonObject active;
    for (auto it = library.active.begin(); it != library.active.end(); ++it) active[QString::number(it.key())] = it.value();
    return {{"schema_version", SchemaVersion}, {"datasets", datasets}, {"fits", fits}, {"active", active}};
}
QString validateLibrary(const Library& library)
{
    QSet<QString> revisions, fits;
    QMap<QString, int> latest;
    QMap<QString, Dataset> firstRevision;
    for (const auto& d : library.datasets) {
        const auto error = d.validate(); if (!error.isEmpty()) return error;
        const QString key = d.id + ':' + QString::number(d.revision);
        if (d.revision <= 0 || revisions.contains(key) || d.revision != latest.value(d.id) + 1)
            return QStringLiteral("数据集修订号重复或不连续");
        revisions.insert(key); latest[d.id] = d.revision;
        if (firstRevision.contains(d.id)) {
            const auto& first = firstRevision[d.id];
            if (first.wavelengthNm != d.wavelengthNm || first.channel != d.channel || first.unit != d.unit || first.createdUtc != d.createdUtc)
                return QStringLiteral("数据集的波长、硬件通道、单位和创建时间不可在修订中改变");
        } else firstRevision[d.id] = d;
    }
    for (const auto& f : library.fits) {
        if (fits.contains(f.id)) return QStringLiteral("定标参数 ID 重复");
        fits.insert(f.id);
        bool found = false;
        for (const auto& d : library.datasets)
            if (d.id == f.dataset.id && d.revision == f.dataset.revision && d.toJson() == f.dataset.toJson()) { found = true; break; }
        if (!found) return QStringLiteral("定标参数缺少对应的数据集快照");
    }
    for (auto it = library.active.begin(); it != library.active.end(); ++it) {
        if (it.key() != 816 && it.key() != 1368) return QStringLiteral("未知波长通道");
        if (!it.value().isEmpty() && !library.activeFit(it.key())) return QStringLiteral("启用的定标版本不存在或波长不匹配");
    }
    const auto *a = library.activeFit(816), *b = library.activeFit(1368);
    if (a && b && a->dataset.channel == b->dataset.channel)
        return QStringLiteral("两个波长不能同时启用同一 DLIA 通道，请核对物理映射");
    return {};
}
}

QJsonObject Environment::toJson() const
{
    return {{"temperature_c", optional(temperatureC)}, {"pressure_hpa", optional(pressureHpa)},
            {"snapshot_time_us", QString::number(snapshotTimeUs)}, {"delta_us", QString::number(deltaUs)},
            {"source", source}, {"timestamp_kind", "sensor_summary_snapshot"}};
}
Environment Environment::fromJson(const QJsonObject& j, QString *error)
{
    Environment e;
    e.temperatureC = optional(j["temperature_c"]); e.pressureHpa = optional(j["pressure_hpa"]);
    bool t = false, d = false;
    e.snapshotTimeUs = j["snapshot_time_us"].toString().toULongLong(&t);
    e.deltaUs = j["delta_us"].toString().toULongLong(&d); e.source = j["source"].toString();
    if (!t || !d || j["timestamp_kind"] != "sensor_summary_snapshot" || e.deltaUs > EnvironmentWindowUs ||
        ((std::isfinite(e.temperatureC) || std::isfinite(e.pressureHpa)) && (e.source.isEmpty() || !e.snapshotTimeUs)))
        *error = QStringLiteral("温压来源或快照时间无效");
    return e;
}
QJsonObject Sample::toJson() const
{
    return {{"feature", feature.toJson()}, {"concentration", concentration}, {"excluded", excluded},
            {"note", note}, {"origin", origin}, {"environment", environment.toJson()}};
}
Sample Sample::fromJson(const QJsonObject& j, QString *error)
{
    Sample s;
    s.feature = WaveformFeature::fromJson(j["feature"].toObject(), error);
    s.concentration = optional(j["concentration"]); s.excluded = j["excluded"].toBool();
    s.note = j["note"].toString(); s.origin = j["origin"].toString();
    s.environment = Environment::fromJson(j["environment"].toObject(), error);
    if (error->isEmpty()) *error = sampleError(s);
    return s;
}
Dataset Dataset::create(const QString& name, int wavelength, quint32 channel, const QString& unit)
{
    Dataset d; d.id = uuid(); d.name = name; d.wavelengthNm = wavelength; d.channel = channel; d.unit = unit;
    d.createdUtc = d.modifiedUtc = now(); return d;
}
QString Dataset::validate() const
{
    if (QUuid(id).isNull() || name.trimmed().isEmpty() || !dateValid(createdUtc) || !dateValid(modifiedUtc) || revision < 0)
        return QStringLiteral("数据集标识、名称、时间或版本无效");
    if ((wavelengthNm != 816 && wavelengthNm != 1368) || (channel != 0x30 && channel != 0x31))
        return QStringLiteral("波长或 DLIA 通道无效");
    if (!unitValid(unit)) return QStringLiteral("浓度单位不支持（ppmv、mol/mol 或 mg/m3；不自动换算）");
    QSet<QString> ids;
    for (const auto& s : samples) {
        const auto error = sampleError(s); if (!error.isEmpty()) return error;
        if (s.feature.channel != channel) return QStringLiteral("数据集混入其他 DLIA 通道");
        if (ids.contains(s.feature.id)) return QStringLiteral("数据集包含重复的来源记录");
        ids.insert(s.feature.id);
    }
    return {};
}
QJsonObject Dataset::toJson() const
{
    QJsonArray rows; for (const auto& s : samples) rows.append(s.toJson());
    return {{"id", id}, {"name", name}, {"unit", unit}, {"wavelength_nm", wavelengthNm}, {"fpga_channel", int(channel)},
            {"revision", revision}, {"created_utc", createdUtc}, {"modified_utc", modifiedUtc},
            {"archived", archived}, {"samples", rows}, {"parameter_version", SchemaVersion}};
}
Dataset Dataset::fromJson(const QJsonObject& j, QString *error)
{
    Dataset d;
    d.id = j["id"].toString(); d.name = j["name"].toString(); d.unit = j["unit"].toString();
    d.wavelengthNm = j["wavelength_nm"].toInt(); d.channel = quint32(j["fpga_channel"].toInt());
    d.revision = j["revision"].toInt(-1); d.createdUtc = j["created_utc"].toString(); d.modifiedUtc = j["modified_utc"].toString();
    d.archived = j["archived"].toBool();
    if (j["parameter_version"].toInt() != SchemaVersion || !j["samples"].isArray()) *error = QStringLiteral("不支持的数据集版本或缺少样本字段");
    for (const auto& row : j["samples"].toArray()) { d.samples.append(Sample::fromJson(row.toObject(), error)); if (!error->isEmpty()) break; }
    if (error->isEmpty()) *error = d.validate();
    return d;
}
QJsonObject Fit::toJson() const
{
    QJsonArray residualArray; for (auto r : residuals) residualArray.append(r);
    return {{"id", id}, {"model", model}, {"model_version", modelVersion}, {"created_utc", createdUtc},
            {"dataset_sha256", datasetDigest}, {"dataset", dataset.toJson()}, {"a", a}, {"b", b},
            {"r_min", rMin}, {"r_max", rMax}, {"c_min", cMin}, {"c_max", cMax}, {"rmse", rmse},
            {"r_squared", optional(rSquared)}, {"sample_count", sampleCount}, {"concentration_count", concentrationCount},
            {"residuals", residualArray}};
}
Fit Fit::fromJson(const QJsonObject& j, QString *error)
{
    Fit f;
    f.id = j["id"].toString(); f.model = j["model"].toString(); f.modelVersion = j["model_version"].toInt(); f.createdUtc = j["created_utc"].toString();
    f.dataset = Dataset::fromJson(j["dataset"].toObject(), error); f.datasetDigest = j["dataset_sha256"].toString();
    auto model = FitModel::create(f.model, f.modelVersion);
    if (!model || QUuid(f.id).isNull() || !dateValid(f.createdUtc)) *error = QStringLiteral("不支持的模型类型/版本或无效参数标识");
    if (!error->isEmpty()) return f;
    // Refit the saved snapshot: never trust incomplete or tampered coefficients.
    const auto expected = model->fit(f.dataset);
    if (!expected.success) { *error = expected.error; return f; }
    const auto expectedJson = expected.fit.toJson();
    for (const auto *key : {"dataset_sha256", "a", "b", "r_min", "r_max", "c_min", "c_max", "rmse", "r_squared", "sample_count", "concentration_count", "residuals"})
        if (j[QLatin1String(key)] != expectedJson[QLatin1String(key)]) { *error = QStringLiteral("定标参数与输入快照不一致：%1").arg(QLatin1String(key)); return f; }
    const auto id = f.id, created = f.createdUtc;
    f = expected.fit; f.id = id; f.createdUtc = created; return f;
}
std::unique_ptr<FitModel> FitModel::create(const QString& type, int version)
{
    if (type == "linear" && version == 1) return std::make_unique<LinearModel>();
    return {};
}

ImportResult importSession(const QString& directory, const Dataset& data, double concentration, bool softwareTest)
{
    ImportResult result;
    result.error = data.validate();
    if (!std::isfinite(concentration) || concentration < 0) result.error = QStringLiteral("标气浓度无效");
    if (!result.error.isEmpty()) return result;
    const auto metadata = SessionLoader::loadMetadata(directory);
    if (!metadata.success) { result.error = metadata.error; return result; }
    const auto features = SessionWaveformFeatures::read(metadata.metadata.sessionDirectory);
    if (!features.success) { result.error = features.error; return result; }
    const auto sensors = SessionLoader::loadSensors(metadata.metadata);
    result.warning = sensors.warning;
    const bool sensorTimesOrdered = std::is_sorted(sensors.data.timestamps_us.cbegin(), sensors.data.timestamps_us.cend());
    if (!sensorTimesOrdered) result.warning += QStringLiteral("；传感器快照时间不单调，未进行温压关联");
    QFile config(QDir(metadata.metadata.sessionDirectory).filePath(VaporView::Session::standardSessionPackageLayout().deviceConfigPath));
    if (config.open(QIODevice::ReadOnly)) softwareTest = softwareTest || containsSimulation(QJsonDocument::fromJson(config.readAll()).object());
    QSet<QString> existing;
    for (const auto& s : data.samples) existing.insert(s.feature.id);
    for (const auto& feature : features.records) {
        if (feature.channel != data.channel) continue;
        if (existing.contains(feature.id)) { ++result.duplicates; continue; }
        Sample s; s.feature = feature; s.concentration = concentration; s.origin = softwareTest ? "software_test" : "unverified";
        if (!feature.invalidReason().isEmpty()) ++result.invalid;
        if (sensors.success && sensorTimesOrdered) {
            const auto& sensor = sensors.data;
            const int i = VaporView::Ground::Session::closestTimestampIndex(sensor.timestamps_us, feature.hostTimeUs);
            if (i >= 0 && i < sensor.timestamps_us.size()) {
                const auto time = sensor.timestamps_us[i];
                const auto delta = time > feature.hostTimeUs ? time - feature.hostTimeUs : feature.hostTimeUs - time;
                if (time && delta <= EnvironmentWindowUs) {
                    auto& e = s.environment; e.snapshotTimeUs = time; e.deltaUs = delta;
                    e.source = metadata.metadata.sensorSummaryCsvFilename + QStringLiteral(":%1 (record snapshot)").arg(i + 2);
                    if (i < sensor.temperature_values.size()) e.temperatureC = sensor.temperature_values[i];
                    if (i < sensor.pressure_values.size()) e.pressureHpa = sensor.pressure_values[i];
                }
            }
        }
        result.samples.append(s);
    }
    result.success = true;
    if (result.samples.isEmpty() && !result.duplicates) result.warning += QStringLiteral("；未找到所选 DLIA 通道的 FPGA H2/H1 特征");
    return result;
}

ConcentrationResult calculate(const Sample& sample, int wavelength, const Fit *fit)
{
    ConcentrationResult r; r.input = sample; r.wavelengthNm = wavelength; r.calculatedUtc = now(); r.origin = sample.origin;
    const auto fail = [&](const QString& reason) { r.status = reason; return r; };
    if (!fit) return fail(QStringLiteral("未启用定标参数"));
    r.calibrationId = fit->id; r.datasetId = fit->dataset.id; r.datasetRevision = fit->dataset.revision; r.unit = fit->dataset.unit;
    for (const auto& s : fit->dataset.samples) if (s.origin == "software_test") r.origin = "software_test";
    if (wavelength != fit->dataset.wavelengthNm || sample.feature.channel != fit->dataset.channel)
        return fail(QStringLiteral("波长或 DLIA 通道与定标参数不匹配"));
    const auto invalid = sample.feature.invalidReason(); if (!invalid.isEmpty()) return fail(invalid);
    if (!std::isfinite(fit->a) || !std::isfinite(fit->b) || !std::isfinite(fit->rMin) || !std::isfinite(fit->rMax) ||
        fit->rMin >= fit->rMax || !unitValid(fit->dataset.unit)) return fail(QStringLiteral("定标参数无效"));
    if (sample.feature.peak < fit->rMin || sample.feature.peak > fit->rMax) return fail(QStringLiteral("峰值超出定标范围；未外推"));
    auto model = FitModel::create(fit->model, fit->modelVersion);
    if (!model) return fail(QStringLiteral("不支持的定标模型版本"));
    r.initial = model->evaluate(*fit, sample.feature.peak);
    if (!std::isfinite(r.initial) || r.initial < 0) return fail(QStringLiteral("初始浓度非有限或小于零"));
    const auto compensation = UnconfiguredCompensation().apply(r.initial, sample.environment);
    r.value = compensation.value; r.compensated = compensation.applied; r.status = compensation.status; r.valid = true;
    return r;
}
QJsonObject ConcentrationResult::toJson() const
{
    const QJsonObject rawInput{{"feature",input.feature.toJson()}, {"environment",input.environment.toJson()},
                              {"origin",input.origin}, {"note",input.note}};
    return {{"valid", valid}, {"status", status}, {"initial_concentration", optional(initial)}, {"concentration", optional(value)},
            {"host_time_us",QString::number(input.feature.hostTimeUs)}, {"peak",optional(input.feature.peak)},
            {"record_id",input.feature.id}, {"source_session",input.feature.sessionDirectory},
            {"fpga_channel",int(input.feature.channel)}, {"quality_flags",double(input.feature.qualityFlags)},
            {"unit", unit}, {"compensated", compensated}, {"compensation_model", "unconfigured"}, {"wavelength_nm", wavelengthNm},
            {"calibration_id", calibrationId}, {"dataset_id", datasetId}, {"dataset_revision", datasetRevision},
            {"algorithm", algorithm}, {"calculated_utc", calculatedUtc}, {"origin", origin}, {"input", rawInput}};
}
const Fit *Library::activeFit(int wavelength) const
{
    for (const auto& f : fits) if (f.id == active.value(wavelength) && f.dataset.wavelengthNm == wavelength) return &f;
    return nullptr;
}
QString Store::defaultFilename()
{
    return QDir(QFileInfo(VaporView::applicationConfigFilePath()).absolutePath()).filePath("water_calibration/library.json");
}
Store::Store(QString filename) : filename_(std::move(filename)) {}
bool Store::load(Library& target, QString *error) const
{
    error->clear();
    QFile file(filename_);
    if (!file.exists()) { target = {}; return true; }
    if (!file.open(QIODevice::ReadOnly)) { *error = file.errorString(); return false; }
    const auto bytes = file.readAll(); QJsonParseError parse;
    const auto root = QJsonDocument::fromJson(bytes, &parse).object(); const auto payload = root["payload"].toObject();
    if (parse.error != QJsonParseError::NoError || payload["schema_version"].toInt() != SchemaVersion ||
        root["sha256"].toString().toLatin1() != digest(canonical(payload)) || !payload["datasets"].isArray() || !payload["fits"].isArray() || !payload["active"].isObject()) {
        *error = QStringLiteral("定标库损坏、字段缺失或版本不支持；原文件未修改"); return false;
    }
    Library library;
    for (const auto& j : payload["datasets"].toArray()) { library.datasets.append(Dataset::fromJson(j.toObject(), error)); if (!error->isEmpty()) return false; }
    for (const auto& j : payload["fits"].toArray()) { library.fits.append(Fit::fromJson(j.toObject(), error)); if (!error->isEmpty()) return false; }
    const auto active = payload["active"].toObject();
    for (auto it = active.begin(); it != active.end(); ++it) {
        if ((it.key() != "816" && it.key() != "1368") || !it.value().isString()) { *error = QStringLiteral("启用版本映射字段无效"); return false; }
        library.active[it.key().toInt()] = it.value().toString();
    }
    *error = validateLibrary(library); if (!error->isEmpty()) return false;
    library.diskDigest = digest(bytes); target = std::move(library); return true;
}
bool Store::save(Library& library, QString *error) const
{
    *error = validateLibrary(library); if (!error->isEmpty()) return false;
    if (!QDir().mkpath(QFileInfo(filename_).absolutePath())) { *error = QStringLiteral("无法创建定标库目录"); return false; }
    QLockFile lock(filename_ + ".lock");
    if (!lock.tryLock(0)) { *error = QStringLiteral("定标库正被其他操作使用，请稍后重新加载"); return false; }
    Library previous;
    if (!load(previous, error)) return false;
    if (previous.diskDigest != library.diskDigest) { *error = QStringLiteral("定标库已被其他窗口更新，请重新加载后操作"); return false; }
    if (previous.datasets.size() > library.datasets.size() || previous.fits.size() > library.fits.size()) {
        *error = QStringLiteral("禁止覆盖或删除定标历史版本"); return false;
    }
    for (int i = 0; i < previous.datasets.size(); ++i) if (previous.datasets[i].toJson() != library.datasets[i].toJson()) {
        *error = QStringLiteral("历史数据集不可覆盖；请追加新修订"); return false;
    }
    for (int i = 0; i < previous.fits.size(); ++i) if (previous.fits[i].toJson() != library.fits[i].toJson()) {
        *error = QStringLiteral("历史定标参数不可覆盖"); return false;
    }
    // Validate newly supplied fits as rigorously as loaded fits.
    for (const auto& f : library.fits) { Fit::fromJson(f.toJson(), error); if (!error->isEmpty()) return false; }
    const auto payload = libraryJson(library);
    const auto bytes = QJsonDocument(QJsonObject{{"payload", payload}, {"sha256", QString::fromLatin1(digest(canonical(payload)))}}).toJson();
    QSaveFile file(filename_);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { *error = file.errorString(); return false; }
    library.diskDigest = digest(bytes); return true;
}
bool exportResults(const QString& filename, const QVector<ConcentrationResult>& rows, QString *error)
{
    // JSON preserves exact uint64 identifiers as strings, nulls, nested provenance,
    // and the original CSV fields without changing any Session format.
    QJsonArray results; for (const auto& row : rows) results.append(row.toJson());
    const auto bytes = QJsonDocument(QJsonObject{{"schema_version", 1}, {"kind", "water_initial_concentrations"}, {"results", results}}).toJson();
    QSaveFile file(filename);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { *error = file.errorString(); return false; }
    return true;
}
}
