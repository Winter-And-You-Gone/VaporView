#include "WaterCalibration.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

namespace VaporView::Ground::Calibration
{
namespace
{
QByteArray checksum(const QJsonObject& object)
{
    return QCryptographicHash::hash(QJsonDocument(object).toJson(QJsonDocument::Compact),
                                    QCryptographicHash::Sha256).toHex();
}
bool addFit(const QJsonObject& json, QMap<QString, Fit>& fits, QString& error)
{
    const auto fit = Fit::fromJson(json, &error);
    if (!error.isEmpty()) return false;
    if (fits.contains(fit.id)) { error = QStringLiteral("浓度快照含重复的定标版本 ID"); return false; }
    fits.insert(fit.id, fit);
    return true;
}
bool decodeResult(const QJsonObject& json, const QMap<QString, Fit>& fits,
                  bool legacy, ConcentrationResult& result, QString& error)
{
    const auto input = json["input"].toObject();
    Sample sample;
    sample.feature = WaveformFeature::fromJson(input["feature"].toObject(), &error);
    if (!error.isEmpty()) return false;
    sample.environment = Environment::fromJson(input["environment"].toObject(), &error);
    if (!error.isEmpty()) return false;
    if (!input["origin"].isString() || !input["note"].isString()) {
        error = QStringLiteral("浓度快照的输入来源或备注字段无效"); return false;
    }
    sample.origin = input["origin"].toString(); sample.note = input["note"].toString();
    const int wavelength = json["wavelength_nm"].toInt();
    const QString id = json["calibration_id"].toString();
    if ((wavelength != 816 && wavelength != 1368) ||
        !QDateTime::fromString(json["calculated_utc"].toString(), Qt::ISODateWithMs).isValid()) {
        error = QStringLiteral("浓度快照波长或计算时间无效"); return false;
    }
    if (!id.isEmpty() && !fits.contains(id)) {
        error = QStringLiteral("浓度快照缺少原定标版本 %1；旧版文件需同时加载对应定标库").arg(id); return false;
    }
    const auto it = fits.constFind(id);
    // Reuse the actual calculation and quality policy, rather than maintaining
    // a second predictor. Restore the recorded time, never replace it with now.
    result = calculate(sample, wavelength, it == fits.cend() ? nullptr : &it.value());
    result.calculatedUtc = json["calculated_utc"].toString();
    const auto expected = result.toJson();
    for (auto field = expected.begin(); field != expected.end(); ++field) {
        if (legacy && field.key() == "compensation_status" && !json.contains(field.key())) continue;
        if (json[field.key()] != field.value()) {
            error = QStringLiteral("浓度快照与原始输入及参数不一致：%1").arg(field.key()); return false;
        }
    }
    return true;
}
}

bool exportResults(const QString& filename, const QVector<ConcentrationResult>& rows, QString *error,
                   const QVector<Fit>& calibrations)
{
    error->clear();
    QSet<QString> needed;
    for (const auto& row : rows) if (!row.calibrationId.isEmpty()) needed.insert(row.calibrationId);
    QMap<QString, Fit> fits;
    for (const auto& fit : calibrations)
        if (needed.contains(fit.id) && !addFit(fit.toJson(), fits, *error)) return false;
    QJsonArray results, parameters;
    for (const auto& row : rows) {
        ConcentrationResult verified;
        if (!decodeResult(row.toJson(), fits, false, verified, *error)) return false;
        results.append(row.toJson());
    }
    for (const auto& fit : fits) parameters.append(fit.toJson());
    QJsonObject root{{"schema_version", 2}, {"kind", "water_initial_concentrations"},
                     {"calibrations", parameters}, {"results", results}};
    root["sha256"] = QString::fromLatin1(checksum(root));
    const auto bytes = QJsonDocument(root).toJson();
    QSaveFile file(filename);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        *error = file.errorString(); return false;
    }
    return true;
}

ConcentrationSnapshot readResults(const QString& filename, const QVector<Fit>& legacyCalibrations)
{
    ConcentrationSnapshot result;
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) { result.error = file.errorString(); return result; }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    auto root = document.object();
    const int version = root["schema_version"].toInt();
    if (parse.error != QJsonParseError::NoError || !document.isObject() ||
        root["kind"] != "water_initial_concentrations" || (version != 1 && version != 2) || !root["results"].isArray()) {
        result.error = QStringLiteral("浓度快照损坏、字段缺失或版本不支持"); return result;
    }
    QMap<QString, Fit> fits;
    if (version == 2) {
        const auto stored = root.take("sha256").toString().toLatin1();
        if (stored != checksum(root) || !root["calibrations"].isArray()) {
            result.error = QStringLiteral("浓度快照摘要不匹配或缺少定标参数"); return result;
        }
        for (const auto& json : root["calibrations"].toArray())
            if (!addFit(json.toObject(), fits, result.error)) return result;
    } else {
        for (const auto& fit : legacyCalibrations)
            if (!addFit(fit.toJson(), fits, result.error)) return result;
        result.warning = QStringLiteral("读取旧版 v1 快照：参数由当前库的原版本提供，建议重新导出为独立 v2 快照");
    }
    QVector<ConcentrationResult> rows;
    for (const auto& json : root["results"].toArray()) {
        ConcentrationResult row;
        if (!decodeResult(json.toObject(), fits, version == 1, row, result.error)) return result;
        rows.append(std::move(row));
    }
    result.results = std::move(rows);
    for (const auto& fit : fits) result.calibrations.append(fit);
    result.success = true;
    return result;
}
}
