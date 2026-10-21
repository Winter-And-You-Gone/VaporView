#include "SessionWaveformFeatures.h"
#include "SessionCsv.h"
#include "shared/session/HarmonicWaveform.h"
#include "shared/session/SessionPathResolver.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>
#include <cmath>

namespace VaporView::Ground
{
namespace
{
QString recordId(const QString& sessionKey, quint32 channel, quint64 sequence)
{
    return QString::fromLatin1(QCryptographicHash::hash(
        (sessionKey + '|' + QString::number(channel) + '|' + QString::number(sequence)).toUtf8(),
        QCryptographicHash::Sha256).toHex());
}
bool decode(WaveformFeature& r, QString *error)
{
    if (QSet<QString>(r.headers.begin(), r.headers.end()).size() != r.headers.size()) {
        *error = QStringLiteral("特征 CSV 存在重复字段名"); return false;
    }
    const auto field = [&](const char *key) { return SessionCsv::csvValueAt(r.fields, r.headers.indexOf(QLatin1String(key))); };
    for (const auto *key : {"host_time_us", "epsilon_time_us", "channel_id", "peak", "mean", "rms", "min_value", "max_value",
                            "quality_flags", "original_point_count", "peak_index", "peak_x", "source", "component",
                            "algorithm_version", "cycle_id", "segment_id", "fpga_tick", "waveform_sequence",
                            "search_start_index", "search_end_index"}) {
        if (!r.headers.contains(QLatin1String(key))) { *error = QStringLiteral("特征 CSV 缺少字段：%1").arg(QLatin1String(key)); return false; }
    }
    bool ok = true;
    const auto number = [&](const char *key, quint64 max) {
        bool parsed = false;
        const auto v = field(key).toULongLong(&parsed);
        ok = ok && parsed && v <= max;
        return v;
    };
    r.hostTimeUs = number("host_time_us", UINT64_MAX);
    r.waveformSequence = number("waveform_sequence", UINT64_MAX);
    r.fpgaTick = number("fpga_tick", UINT64_MAX);
    r.segmentId = number("segment_id", UINT64_MAX);
    r.channel = quint32(number("channel_id", UINT32_MAX));
    r.cycleId = quint32(number("cycle_id", UINT32_MAX));
    r.qualityFlags = quint32(number("quality_flags", UINT32_MAX));
    r.originalPoints = quint32(number("original_point_count", UINT32_MAX));
    bool indexOk = false;
    r.peakIndex = field("peak_index").toLongLong(&indexOk);
    bool peakOk = false;
    r.peak = field("peak").isEmpty() ? std::numeric_limits<double>::quiet_NaN() : field("peak").toDouble(&peakOk);
    if (!ok || !indexOk || (!field("peak").isEmpty() && (!peakOk || !std::isfinite(r.peak)))) {
        *error = QStringLiteral("特征记录包含无效数值（第 %1 行）").arg(r.csvLine); return false;
    }
    bool positionOk = false, maximumOk = false;
    const auto position = field("peak_x").toLongLong(&positionOk);
    const auto maximum = field("max_value").toDouble(&maximumOk);
    if ((r.peakIndex >= 0 && (!positionOk || position != r.peakIndex)) ||
        (r.peakIndex < 0 && !field("peak_x").isEmpty()) ||
        (peakOk && (!maximumOk || !std::isfinite(maximum) || maximum != r.peak))) {
        *error = QStringLiteral("特征峰值或位置字段不一致（第 %1 行）").arg(r.csvLine); return false;
    }
    if (field("source") != "fpga" || field("component") != "H2/H1" || field("algorithm_version") != "1" ||
        field("search_start_index") != "0" || field("search_end_index") != "0" ||
        (r.channel != 0x30 && r.channel != 0x31)) {
        *error = QStringLiteral("只支持 FPGA H2/H1 算法 v1 的全扫描特征（第 %1 行）").arg(r.csvLine); return false;
    }
    r.id = recordId(r.sessionKey, r.channel, r.waveformSequence);
    return true;
}
}

QString WaveformFeature::sourceError() const
{
    QString error;
    const auto decoded = fromJson(toJson(), &error);
    if (!error.isEmpty()) return error;
    if (decoded.hostTimeUs != hostTimeUs || decoded.waveformSequence != waveformSequence || decoded.fpgaTick != fpgaTick ||
        decoded.segmentId != segmentId || decoded.channel != channel || decoded.cycleId != cycleId ||
        decoded.qualityFlags != qualityFlags || decoded.originalPoints != originalPoints || decoded.peakIndex != peakIndex ||
        !(decoded.peak == peak || (std::isnan(decoded.peak) && std::isnan(peak))))
        return QStringLiteral("峰值输入与原始来源特征字段不一致");
    return {};
}

QString WaveformFeature::invalidReason() const
{
    using namespace VaporView::Session;
    // InvalidReference marks skipped samples, not an invalid cycle. Match the
    // existing full-cycle peak algorithm: a remaining valid maximum is usable.
    if (qualityFlags & ~quint32(InvalidReference)) return QStringLiteral("周期质量不合格（flags=%1）").arg(qualityFlags);
    if (!hostTimeUs || !originalPoints || peakIndex < 0 || quint64(peakIndex) >= originalPoints || !std::isfinite(peak))
        return QStringLiteral("没有有效的完整周期峰值");
    if (channel != 0x30 && channel != 0x31) return QStringLiteral("不支持的 FPGA 通道");
    return {};
}

QJsonObject WaveformFeature::toJson() const
{
    return {{"id", id}, {"session_key", sessionKey}, {"session", sessionDirectory}, {"csv", csvFilename}, {"line", csvLine},
            {"headers", QJsonArray::fromStringList(headers)}, {"fields", QJsonArray::fromStringList(fields)}};
}
WaveformFeature WaveformFeature::fromJson(const QJsonObject& j, QString *error)
{
    WaveformFeature r;
    r.sessionKey = j["session_key"].toString();
    r.sessionDirectory = j["session"].toString(); r.csvFilename = j["csv"].toString(); r.csvLine = j["line"].toInt();
    if (!j["headers"].isArray() || !j["fields"].isArray()) { *error = QStringLiteral("特征原始字段必须为字符串数组"); return r; }
    for (const auto& v : j["headers"].toArray()) {
        if (!v.isString()) { *error = QStringLiteral("特征字段名必须为字符串"); return r; }
        r.headers.append(v.toString());
    }
    for (const auto& v : j["fields"].toArray()) {
        if (!v.isString()) { *error = QStringLiteral("特征原始字段必须为字符串"); return r; }
        r.fields.append(v.toString());
    }
    if (r.sessionKey.isEmpty() || r.sessionDirectory.isEmpty() || r.csvFilename.isEmpty() || r.csvLine < 2 || r.fields.size() != r.headers.size())
        *error = QStringLiteral("特征来源信息不完整");
    else if (decode(r, error) && r.id != j["id"].toString()) *error = QStringLiteral("特征记录标识不匹配");
    return r;
}

WaveformFeaturesResult SessionWaveformFeatures::read(const QString& directory)
{
    const auto context = VaporView::Session::loadSessionPathContext(directory);
    if (!context.manifestError.isEmpty()) return {false, context.manifestError, {}};
    return readFile(VaporView::Session::resolveSessionPath(context,
        VaporView::Session::SessionFileKind::WaveformFeaturesCsv).absolutePath, directory);
}

WaveformFeaturesResult SessionWaveformFeatures::readFile(const QString& filename, const QString& directory)
{
    WaveformFeaturesResult result;
    QFile file(filename);
    if (!QFileInfo(directory).isDir() || !file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("来源 Session 或特征文件不存在/无法读取：%1").arg(filename); return result;
    }
    const auto header = file.readLine();
    const auto context = VaporView::Session::loadSessionPathContext(directory);
    if (!context.manifestError.isEmpty()) { result.error = context.manifestError; return result; }
    if (!header.endsWith('\n') && context.manifest["state"].toString() == "recording") {
        // A newly opened recorder may not have flushed even its header yet.
        result.success = true; return result;
    }
    const auto headers = SessionCsv::parseCsvLine(QString::fromUtf8(header).trimmed(), &result.error);
    if (!result.error.isEmpty()) return result;
    // Validate the header even when the file contains no data yet.
    WaveformFeature probe; probe.headers = headers; QString headerError;
    decode(probe, &headerError);
    if (headerError.startsWith(QStringLiteral("特征 CSV"))) { result.error = headerError; return result; }
    const auto manifest = context.manifest;
    // The standard manifest has no UUID. Its recording identity survives moving
    // or copying a Session, unlike a directory-only key. Legacy files fall back
    // to their canonical path; no missing identity is invented.
    QString sessionKey;
    const QString name = manifest["session_name"].toString();
    const QString start = manifest["start_time_utc"].toString();
    if (!name.isEmpty() && !start.isEmpty())
        sessionKey = name + '|' + start + '|' + manifest["recording_origin"].toString();
    else {
        sessionKey = QFileInfo(directory).canonicalFilePath();
#ifdef Q_OS_WIN
        sessionKey = sessionKey.toLower();
#endif
    }
    int line = 1;
    QSet<QString> ids;
    while (!file.atEnd()) {
        const auto bytes = file.readLine(); ++line;
        // A recorder may be in the middle of writing its last row. Retry later.
        if (!bytes.endsWith('\n') && context.manifest["state"].toString() == "recording") break;
        if (bytes.trimmed().isEmpty()) continue;
        WaveformFeature r;
        r.sessionKey = sessionKey;
        r.sessionDirectory = QDir::fromNativeSeparators(QFileInfo(directory).absoluteFilePath());
        r.csvFilename = QFileInfo(filename).absoluteFilePath(); r.csvLine = line; r.headers = headers;
        r.fields = SessionCsv::parseCsvLine(QString::fromUtf8(bytes).trimmed(), &result.error);
        if (!result.error.isEmpty() || r.fields.size() != headers.size()) {
            result.error = QStringLiteral("特征 CSV 第 %1 行无效：%2").arg(line).arg(
                result.error.isEmpty() ? QStringLiteral("列数错误") : result.error);
            return result;
        }
        const int source = headers.indexOf("source"), component = headers.indexOf("component");
        if (SessionCsv::csvValueAt(r.fields, source) != "fpga" || SessionCsv::csvValueAt(r.fields, component) != "H2/H1") continue;
        if (!decode(r, &result.error)) {
            if (result.error.isEmpty()) result.error = QStringLiteral("特征列数错误（第 %1 行）").arg(line);
            return result;
        }
        if (ids.contains(r.id)) { result.error = QStringLiteral("特征文件存在重复记录：%1").arg(r.id); return result; }
        ids.insert(r.id); result.records.append(r);
    }
    result.success = true;
    return result;
}
}
