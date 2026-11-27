#include "RecordedConcentrations.h"
#include "shared/session/CalibrationRuntime.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace VaporView::Ground::Calibration
{
namespace
{
bool validateRows(RecordedConcentrations& result)
{
    using namespace VaporView::Session;
    const auto runtime=result.snapshot["runtime"].toObject();
    result.error=validateRuntimeCalibration(runtime);if(!result.error.isEmpty())return false;
    if(result.snapshot["kind"]!="vaporview_session_calibration" || result.snapshot["schema_version"]!=1 ||
       result.snapshot["file_id"]!=runtime["file_id"] || result.snapshot["file_sha256"]!=runtime["file_sha256"]){result.error=QStringLiteral("Session 定标来源与参数快照不一致");return false;}
    QSet<QString> ids;QString session;
    for(const auto& value:result.rows) {
        const auto row=value.toObject();HarmonicLayout l;FpgaWave::NormalizedFeature f;
        bool parsed=true;
        const auto decimal=[&](const char *key){bool ok=false;auto v=row[QLatin1String(key)].toString().toULongLong(&ok);parsed&=ok;return v;};
        const auto host=decimal("host_time_us"),seq=decimal("waveform_sequence");l.fpgaTick=decimal("fpga_tick");l.segment=decimal("segment_id");
        for(const auto *key:{"fpga_channel","cycle_id","original_points","valid_points","quality_flags","peak_index"}) {
            const auto v=row[QLatin1String(key)];const double n=v.toDouble(-2);
            if(!v.isDouble() || !std::isfinite(n) || std::floor(n)!=n || n<(QLatin1String(key)=="peak_index"?-1:0) || n>4294967295.0)parsed=false;
        }
        if(!parsed){result.error=QStringLiteral("Session 浓度整数字段无效");return false;}
        l.channel=quint32(row["fpga_channel"].toInt());l.cycle=quint32(row["cycle_id"].toDouble());l.originalPoints=quint32(row["original_points"].toDouble());
        f.validPoints=quint32(row["valid_points"].toDouble());f.quality=quint32(row["quality_flags"].toDouble());f.peakIndex=qint64(row["peak_index"].toDouble());
        f.peak=row["peak"].isDouble()?row["peak"].toDouble():std::numeric_limits<double>::quiet_NaN();
        const auto recordedSession=row["session_id"].toString();if(session.isEmpty())session=recordedSession;
        if(!parsed || !host || !l.originalPoints || (l.channel!=0x30 && l.channel!=0x31) || f.validPoints>l.originalPoints || (f.quality&~quint32(31)) ||
           recordedSession.isEmpty() || recordedSession!=session || ids.contains(row["record_id"].toString()) ||
           calculateRuntimeConcentration(runtime,session,host,seq,l,f)!=row) {
            result.error=QStringLiteral("Session 浓度记录损坏、重复或与原参数/质量规则不一致");return false;
        }
        ids.insert(row["record_id"].toString());
    }
    return true;
}
}
RecordedConcentrations readRecordedConcentrations(const QString& filename)
{
    RecordedConcentrations r;QFile file(filename);
    if(!file.open(QIODevice::ReadOnly)){r.error=file.errorString();return r;}
    if(file.size()>256*1024*1024){r.error=QStringLiteral("历史浓度文件过大，请按 Session 归档读取");return r;}
    if(filename.endsWith(".jsonl",Qt::CaseInsensitive) || QFileInfo(filename).fileName()=="calibration_snapshot.json") {
        QFile snapshot(QDir(QFileInfo(filename).absolutePath()).filePath("calibration_snapshot.json"));
        if(!snapshot.open(QIODevice::ReadOnly)){r.error=snapshot.errorString();return r;}
        r.snapshot=QJsonDocument::fromJson(snapshot.readAll()).object();
        if(!filename.endsWith(".jsonl",Qt::CaseInsensitive)){file.close();file.setFileName(QDir(QFileInfo(filename).absolutePath()).filePath("calibration_concentrations.jsonl"));if(!file.open(QIODevice::ReadOnly)){r.error=file.errorString();return r;}}
        if(file.size()>256*1024*1024){r.error=QStringLiteral("历史浓度文件过大，请按 Session 归档读取");return r;}
        while(!file.atEnd()) {QJsonParseError error;const auto line=file.readLine(65537);if(line.trimmed().isEmpty())continue;auto doc=QJsonDocument::fromJson(line,&error);
            if(error.error!=QJsonParseError::NoError || !doc.isObject()){r.error=QStringLiteral("历史 Session 浓度 JSONL 损坏或尾行未写完");return r;}r.rows.append(doc.object());}
    } else {
        auto document=QJsonDocument::fromJson(file.readAll()).object();const auto digest=document.take("sha256").toString();
        if(document["kind"]!="recorded_water_concentrations" || document["schema_version"]!=1 || digest.toLatin1()!=VaporView::Session::runtimeCalibrationDigest(document) || !document["results"].isArray()){
            r.error=QStringLiteral("历史浓度导出包摘要或 schema 无效");return r;}
        r.snapshot=document["snapshot"].toObject();r.rows=document["results"].toArray();
    }
    r.success=validateRows(r);return r;
}
bool attachRecordedConcentrationsToSamples(
    const QString& sessionDir, const std::vector<quint64>& sampleRecordTimestampsUs,
    std::vector<VaporView::Geo::TrajectoryRenderSample>& samples, QString& waterUnit,
    QString& warning, quint64 maxDeltaUs, std::uint32_t calibrationEpoch)
{
    const QString path = QDir(sessionDir).filePath(QStringLiteral("calibration_concentrations.jsonl"));
    if (!QFileInfo::exists(path)) return false;

    const auto recorded = readRecordedConcentrations(path);
    if (!recorded.success) {
        if (!warning.isEmpty()) warning += QLatin1Char(' ');
        warning += QStringLiteral("无法加载已记录的水汽浓度：%1").arg(recorded.error);
        return false;
    }

    struct Reading { quint64 hostTimeUs; double concentration; };
    std::vector<Reading> column, inSitu;
    QString unit;
    for (const auto& value : recorded.rows) {
        const auto row = value.toObject();
        if (!row["valid"].toBool()) continue;
        const auto concentration = row["initial_concentration"];
        const quint64 hostTimeUs = row["host_time_us"].toString().toULongLong();
        const int wavelength = row["wavelength_nm"].toInt();
        if (!hostTimeUs || !concentration.isDouble() || !std::isfinite(concentration.toDouble()) ||
            (wavelength != 816 && wavelength != 1368)) continue;
        auto& channel = wavelength == 816 ? column : inSitu;
        channel.push_back({hostTimeUs, concentration.toDouble()});
        if (unit.isEmpty()) unit = row["unit"].toString();
    }
    if (column.empty() && inSitu.empty()) return false;
    for (auto* channel : {&column, &inSitu}) {
        std::stable_sort(channel->begin(), channel->end(), [](const Reading& a, const Reading& b) {
            return a.hostTimeUs < b.hostTimeUs;
        });
    }
    const auto attach = [maxDeltaUs](const std::vector<Reading>& channel, quint64 time, double& target) {
        const auto after = std::upper_bound(channel.begin(), channel.end(), time,
            [](quint64 t, const Reading& reading) { return t < reading.hostTimeUs; });
        if (after == channel.begin()) return false;
        const auto& latest = *std::prev(after);
        if (time - latest.hostTimeUs > maxDeltaUs) return false;
        target = latest.concentration;
        return true;
    };
    bool matched = false;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        auto& sample = samples[i];
        quint64 time = i < sampleRecordTimestampsUs.size() ? sampleRecordTimestampsUs[i] : 0;
        if (!time && sample.navigation.recordTimestampUs > 0) time = quint64(sample.navigation.recordTimestampUs);
        if (!time) continue;
        const bool hasColumn = attach(column, time, sample.heat.columnWaterConcentration);
        const bool hasInSitu = attach(inSitu, time, sample.heat.inSituWaterConcentration);
        if (hasColumn || hasInSitu) {
            sample.heat.calibrationEpoch = calibrationEpoch;
            matched = true;
        }
    }
    if (matched) waterUnit = unit;
    return matched;
}

bool exportRecordedConcentrations(const QString& filename,const RecordedConcentrations& source,QString *error)
{
    auto verified=source;if(!validateRows(verified)){*error=verified.error;return false;}
    QJsonObject j{{"kind","recorded_water_concentrations"},{"schema_version",1},{"snapshot",source.snapshot},{"results",source.rows}};
    j["sha256"]=QString::fromLatin1(VaporView::Session::runtimeCalibrationDigest(j));const auto bytes=QJsonDocument(j).toJson(QJsonDocument::Compact);
    QSaveFile file(filename);file.setDirectWriteFallback(false);if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()){*error=file.errorString();return false;}return true;
}
}
