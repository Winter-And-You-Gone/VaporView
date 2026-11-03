#include "CalibrationProject.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace VaporView::Ground::Calibration
{
QString calibrationUtcNow() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
QString calibrationUuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
namespace
{
// Preserve Qt Compact JSON byte ordering/escaping without allocating another
// complete UTF-8 copy of a large project solely to calculate its digest.
class CanonicalJsonHash
{
public:
    void append(const QJsonValue& value)
    {
        int budget=128;
        if(small(value,budget)) {
            writeBytes(value.toJson(QJsonValue::JsonFormat::Compact));return;
        }
        if (value.isObject()) {
            writeBytes("{");bool first=true;const auto object=value.toObject();
            for(auto it=object.constBegin();it!=object.constEnd();++it) {
                if(!first)writeBytes(",");first=false;
                writeBytes(QJsonValue(it.key()).toJson(QJsonValue::JsonFormat::Compact));writeBytes(":");append(it.value());
            }
            writeBytes("}");
        } else if(value.isArray()) {
            writeBytes("[");bool first=true;
            for(const auto& item:value.toArray()) {if(!first)writeBytes(",");first=false;append(item);}
            writeBytes("]");
        } else writeBytes(value.toJson(QJsonValue::JsonFormat::Compact));
    }
    QByteArray result() {flush();return hash_.result().toHex();}
private:
    QCryptographicHash hash_{QCryptographicHash::Sha256};
    QByteArray buffer_;
    static bool small(const QJsonValue& value,int& remaining)
    {
        if(--remaining<0)return false;
        if(value.isObject()) {
            const auto object=value.toObject();
            for(auto it=object.constBegin();it!=object.constEnd();++it)if(!small(it.value(),remaining))return false;
        } else if(value.isArray()) {
            for(const auto& item:value.toArray())if(!small(item,remaining))return false;
        }
        return true;
    }
    void flush() {hash_.addData(buffer_);buffer_.clear();}
    void writeBytes(QByteArrayView bytes)
    {
        if(buffer_.size()+bytes.size()>65536)flush();
        if(bytes.size()>65536)hash_.addData(bytes);
        else buffer_.append(bytes);
    }
};
bool date(const QString& s) { return QDateTime::fromString(s, Qt::ISODateWithMs).isValid(); }
QJsonObject parametersJson(const Fit& f)
{
    auto j = f.toJson(); j.remove("dataset"); j.remove("dataset_sha256"); return j;
}
bool finite(const QJsonObject& j, const char *key)
{ return j[QLatin1String(key)].isDouble() && std::isfinite(j[QLatin1String(key)].toDouble()); }
}
QByteArray calibrationDigest(const QJsonObject& object)
{
    CanonicalJsonHash hash;hash.append(object);return hash.result();
}
QJsonObject PeakRecord::toJson() const
{ return {{"task_id",taskId},{"stage_id",stageId},{"feature",feature.toJson()}, {"environment",environment.toJson()}, {"device_identity",deviceIdentity}, {"alignment",alignment},{"sensor_readings",sensorReadings()}}; }
QJsonObject PeakRecord::sensorReadings() const
{ return QJsonDocument::fromJson(sensorReadingsJson).object(); }
void PeakRecord::setSensorReadings(const QJsonObject& readings)
{ sensorReadingsJson=QJsonDocument(readings).toJson(QJsonDocument::Compact); }
PeakRecord PeakRecord::fromJson(const QJsonObject& j, QString *error)
{
    error->clear();
    PeakRecord r;
    r.taskId=j["task_id"].toString();r.stageId=j["stage_id"].toString();
    r.feature=HarmonicPeakFeature::fromJson(j["feature"].toObject(),error);
    if (!error->isEmpty()) return r;
    r.environment=Environment::fromJson(j["environment"].toObject(),error);
    r.deviceIdentity=j["device_identity"].toObject(); r.alignment=j["alignment"].toString();
    r.setSensorReadings(j["sensor_readings"].toObject());
    if(!j["sensor_readings"].isObject()){*error=QStringLiteral("原始传感器来源记录无效");return r;}
    if (!j["device_identity"].isObject() || (r.alignment!="unavailable" && r.alignment!="host_receipt_uncertain"))
        *error=QStringLiteral("传感器或设备来源字段无效");
    return r;
}
QJsonObject ExclusionOperation::toJson() const
{ return {{"record_id",recordId},{"excluded",excluded},{"reason",reason},{"utc",utc},{"revision",revision}}; }
ConcentrationStage ConcentrationStage::create(double c, quint64 ms)
{ ConcentrationStage s; s.id=calibrationUuid(); s.concentration=c; s.configuredMs=ms; return s; }
bool ConcentrationStage::isExcluded(const QString& id) const
{
    for (auto i=operations.crbegin();i!=operations.crend();++i) if(i->recordId==id) return i->excluded;
    return false;
}
StageStatistics ConcentrationStage::statistics(int wave) const
{
    StageStatistics s;
    QSet<QString> excludedIds;
    for (const auto& op:operations) { if(op.excluded) excludedIds.insert(op.recordId); else excludedIds.remove(op.recordId); }
    long double mean=0, m2=0;
    for (const auto& row:samples.value(wave)) {
        ++s.raw;
        if (!row.feature.invalidReason().isEmpty()) { ++s.invalid; continue; }
        ++s.valid;
        if (excludedIds.contains(row.feature.id)) { ++s.excluded; continue; }
        const double v=row.feature.peak;
        if (!s.used) s.minimum=s.maximum=v;
        s.minimum=std::min(s.minimum,v); s.maximum=std::max(s.maximum,v);
        ++s.used; const long double delta=v-mean; mean+=delta/s.used; m2+=delta*(v-mean);
    }
    s.mean=double(mean); s.standardDeviation=s.used>1?std::sqrt(double(m2/(s.used-1))):0;
    return s;
}
QJsonObject StageStatistics::toJson() const
{
    return {{"raw",raw},{"valid",valid},{"invalid",invalid},{"excluded",excluded},{"used",used},
        {"mean",used?QJsonValue(mean):QJsonValue(QJsonValue::Null)},
        {"standard_deviation",used?QJsonValue(standardDeviation):QJsonValue(QJsonValue::Null)},
        {"minimum",used?QJsonValue(minimum):QJsonValue(QJsonValue::Null)},
        {"maximum",used?QJsonValue(maximum):QJsonValue(QJsonValue::Null)}};
}
QJsonObject ConcentrationStage::toJson() const
{
    QJsonObject channels, stats;
    for (int w:{816,1368}) {
        QJsonArray rows; for (const auto& r:samples.value(w)) rows.append(r.toJson());
        channels[QString::number(w)]=rows; stats[QString::number(w)]=statistics(w).toJson();
    }
    QJsonArray ops; for (const auto& o:operations) ops.append(o.toJson());
    return {{"id",id},{"concentration",concentration},{"note",note},{"status",status},
        {"configured_ms",QString::number(configuredMs)},{"effective_ms",QString::number(effectiveMs)},
        {"wall_ms",QString::number(wallMs)},{"started_utc",startedUtc},{"completed_utc",completedUtc},
        {"channels",channels},{"statistics",stats},{"exclusions",ops}};
}
QJsonObject StageFit::toJson() const
{
    QJsonArray ids, sd; for (const auto& id:stageIds) ids.append(id); for(double d:deviations) sd.append(d);
    return {{"parameters",parametersJson(parameters)},{"revision",revision},{"warning",warning},{"stage_ids",ids},{"standard_deviations",sd}};
}
CalibrationProject CalibrationProject::create(const QString& n, const QString& u)
{
    CalibrationProject p; p.id=calibrationUuid(); p.name=n; p.unit=u; p.createdUtc=calibrationUtcNow();
    p.softwareVersion=QCoreApplication::applicationVersion(); return p;
}
void CalibrationProject::invalidateFits() { ++revision; fits.clear(); }
bool CalibrationProject::exclude(const QString& sid,int w,const QSet<QString>& ids,bool excluded,const QString& reason,QString *error)
{
    if (excluded && reason.trimmed().isEmpty()) { *error=QStringLiteral("请填写排除原因"); return false; }
    for (auto& stage:stages) if(stage.id==sid) {
        QSet<QString> available; for(const auto& r:stage.samples.value(w)) available.insert(r.feature.id);
        if(ids.isEmpty() || !available.contains(ids)) { *error=QStringLiteral("请选择当前波长的有效记录标识"); return false; }
        invalidateFits();
        for(const auto& id:ids) stage.operations.append({id,reason,calibrationUtcNow(),excluded,revision});
        return true;
    }
    *error=QStringLiteral("阶段不存在"); return false;
}
QMap<int,QString> CalibrationProject::fit()
{
    QMap<int,QString> errors;
    for(int w:{816,1368}) {
        QVector<QPair<double,double>> points; StageFit f;
        for(const auto& s:stages) if(s.status=="complete") {
            const auto stats=s.statistics(w); if(!stats.used) continue;
            points.append({stats.mean,s.concentration}); f.stageIds.append(s.id); f.deviations.append(stats.standardDeviation);
        }
        auto result=fitLinearPoints(points);
        if(!result.success) { fits.remove(w); errors[w]=result.error; continue; }
        f.parameters=result.fit; f.parameters.id=calibrationUuid(); f.parameters.createdUtc=calibrationUtcNow();
        f.revision=revision;
        if(result.fit.concentrationCount<3) f.warning=QStringLiteral("仅两个浓度水平，无法充分评估残差；建议至少三个浓度水平");
        fits[w]=f;
    }
    return errors;
}
Fit CalibrationProject::calculationFit(int w) const
{
    auto f=fits.value(w).parameters;
    f.dataset.id=id; f.dataset.name=name; f.dataset.unit=unit; f.dataset.revision=revision;
    f.dataset.wavelengthNm=w; f.dataset.channel=channelForWavelength(w);
    return f;
}
QString CalibrationProject::validate(bool requireFits) const
{
    if(QUuid(id).isNull() || name.trimmed().isEmpty() || !date(createdUtc) || revision<1 ||
       (unit!="ppmv" && unit!="mol/mol" && unit!="mg/m3")) return QStringLiteral("任务标识、名称、时间、修订或单位无效");
    if(hardwareMapping!=QJsonObject{{"816",0x31},{"1368",0x30}}) return QStringLiteral("固定光路映射不兼容：816 nm=DLIA1/0x31，1368 nm=DLIA0/0x30");
    if(origin!="live_acquisition" && origin!="software_test" && origin!="legacy_import") return QStringLiteral("未知任务来源");
    QSet<QString> stageIds, recordIds;
    for(const auto& s:stages) {
        if(QUuid(s.id).isNull() || stageIds.contains(s.id) || !std::isfinite(s.concentration) || s.concentration<0 ||
           !s.configuredMs || s.effectiveMs>s.configuredMs || s.wallMs<s.effectiveMs ||
           !QStringList{"pending","acquiring","paused","incomplete","complete","abandoned"}.contains(s.status) ||
           (s.status=="complete" && (s.effectiveMs!=s.configuredMs || !date(s.completedUtc))) ||
           (s.status!="pending" && !date(s.startedUtc))) return QStringLiteral("阶段状态、时间或浓度无效");
        stageIds.insert(s.id); QSet<QString> local;
        for(int w:{816,1368}) for(const auto& r:s.samples.value(w)) {
            const auto err=r.feature.sourceError(); if(!err.isEmpty()) return err;
            if(QUuid(r.taskId).isNull() || r.stageId!=s.id || r.feature.channel!=channelForWavelength(w) || recordIds.contains(r.feature.id) || r.deviceIdentity!=deviceIdentity)
                return QStringLiteral("样本通道、设备身份不匹配或周期重复");
            recordIds.insert(r.feature.id); local.insert(r.feature.id);
        }
        int previous=0;
        for(const auto& op:s.operations) {
            if(!local.contains(op.recordId) || op.revision<previous || op.revision>revision || op.revision<1 || !date(op.utc) ||
               (op.excluded && op.reason.trimmed().isEmpty())) return QStringLiteral("人工审核日志无效");
            previous=op.revision;
        }
    }
    for(int w:{816,1368}) {
        if(!fits.contains(w)) { if(requireFits) return QStringLiteral("%1 nm 缺少有效拟合结果").arg(w); else continue; }
        const auto& f=fits[w]; const auto& v=f.parameters;
        if(f.revision!=revision || v.model!="linear" || v.modelVersion!=1 || QUuid(v.id).isNull() || !date(v.createdUtc) ||
           !std::isfinite(v.a) || !std::isfinite(v.b) || !std::isfinite(v.rMin) || !std::isfinite(v.rMax) || v.rMin>=v.rMax ||
           !std::isfinite(v.cMin) || !std::isfinite(v.cMax) || v.cMin<0 || v.cMin>=v.cMax || v.sampleCount<2 || v.concentrationCount<2)
            return QStringLiteral("%1 nm 拟合无效或对应旧修订").arg(w);
    }
    return {};
}
QJsonObject CalibrationProject::toJson() const
{
    QJsonArray ss, legacy; for(const auto& s:stages) ss.append(s.toJson()); for(const auto& f:legacyFits) legacy.append(f.toJson());
    QJsonObject ff; for(auto i=fits.begin();i!=fits.end();++i) ff[QString::number(i.key())]=i->toJson();
    return {{"id",id},{"name",name},{"unit",unit},{"created_utc",createdUtc},{"software_version",softwareVersion},
        {"origin",origin},{"parent_file_id",parentFileId},{"revision",revision},{"device_identity",deviceIdentity},
        {"hardware_mapping",hardwareMapping},{"peak_algorithm","pointwise_h2_over_h1_v1"},{"model","validation_linear_v1"},
        {"compensation","unconfigured"},{"stages",ss},{"fits",ff},{"legacy_fits",legacy}};
}
CalibrationProject CalibrationProject::fromJson(QJsonObject j,QString *error,const CalibrationReadObserver& observer)
{
    error->clear();
    CalibrationProject p;
    p.id=j["id"].toString();p.name=j["name"].toString();p.unit=j["unit"].toString();p.createdUtc=j["created_utc"].toString();
    p.softwareVersion=j["software_version"].toString();p.origin=j["origin"].toString();p.parentFileId=j["parent_file_id"].toString();
    p.revision=j["revision"].toInt();p.deviceIdentity=j["device_identity"].toObject();p.hardwareMapping=j["hardware_mapping"].toObject();
    if(!j["stages"].isArray() || !j["fits"].isObject() || !j["legacy_fits"].isArray() || !j["device_identity"].isObject() ||
       j["peak_algorithm"]!="pointwise_h2_over_h1_v1" || j["model"]!="validation_linear_v1" || j["compensation"]!="unconfigured") {
        *error=QStringLiteral("任务 schema 或算法版本不兼容"); return p;
    }
    auto stages=j.take("stages").toArray();
    // Detach the small header from removed raw-array storage before consuming
    // stages, so a decoded stage can be released as its native records are built.
    j=QJsonDocument::fromJson(QJsonDocument(j).toJson(QJsonDocument::Compact)).object();
    while(!stages.isEmpty()) {
        const auto o=stages.takeAt(0).toObject(); ConcentrationStage s;
        s.id=o["id"].toString();s.note=o["note"].toString();s.status=o["status"].toString();
        s.startedUtc=o["started_utc"].toString();s.completedUtc=o["completed_utc"].toString();s.concentration=o["concentration"].toDouble(-1);
        bool ok=true;
        const auto ms=[&](const char *key) { bool parsed=false;auto n=o[QLatin1String(key)].toString().toULongLong(&parsed);ok&=parsed;return n; };
        s.configuredMs=ms("configured_ms");s.effectiveMs=ms("effective_ms");s.wallMs=ms("wall_ms");
        if(!ok || !finite(o,"concentration") || !o["exclusions"].isArray()) { *error=QStringLiteral("阶段字段类型错误");return p; }
        for(int w:{816,1368}) {
            const auto a=o["channels"].toObject()[QString::number(w)];
            if(!a.isArray() || a.toArray().size()>2000000) { *error=QStringLiteral("样本数组无效或过大");return p; }
            s.samples[w].reserve(a.toArray().size());
            for(const auto& row:a.toArray()) {
                auto r=PeakRecord::fromJson(row.toObject(),error);if(!error->isEmpty()) return p;
                if(r.deviceIdentity!=p.deviceIdentity) { *error=QStringLiteral("样本设备身份不匹配");return p; }
                r.deviceIdentity=p.deviceIdentity;
                s.samples[w].append(r);
            }
        }
        for(const auto& op:o["exclusions"].toArray()) {
            const auto x=op.toObject();if(!x["excluded"].isBool()) { *error=QStringLiteral("排除状态类型错误");return p; }
            s.operations.append({x["record_id"].toString(),x["reason"].toString(),x["utc"].toString(),x["excluded"].toBool(),x["revision"].toInt()});
        }
        if(observer)observer("stage_records");
        for(int w:{816,1368}) if(o["statistics"].toObject()[QString::number(w)]!=s.statistics(w).toJson()) {
            *error=QStringLiteral("阶段统计与原始数据不一致");return p;
        }
        p.stages.append(s);
        if(observer)observer("stage_statistics");
    }
    for(const auto& v:j["legacy_fits"].toArray()) {
        auto f=Fit::fromJson(v.toObject(),error);if(!error->isEmpty()) return p;p.legacyFits.append(f);
    }
    CalibrationProject expected=p;
    if(p.origin=="legacy_import") {
        if(p.legacyFits.size()!=2 || !p.stages.isEmpty()) { *error=QStringLiteral("旧版导入必须保留两路原始 Fit，不得伪造采集阶段");return p; }
        expected=importLegacy(p.legacyFits[0],p.legacyFits[1],error);if(!error->isEmpty()) return p;
    } else expected.fit();
    if(observer)observer("refit");
    const auto fits=j["fits"].toObject();
    for(auto i=fits.begin();i!=fits.end();++i) {
        bool ok=false;const int w=i.key().toInt(&ok);const auto saved=i.value().toObject();const auto param=saved["parameters"].toObject();
        if(!ok || !expected.fits.contains(w)) { *error=QStringLiteral("拟合波长或输入数据无效");return p; }
        auto f=expected.fits[w];
        const auto expectedParams=parametersJson(f.parameters);
        for(auto k=expectedParams.begin();k!=expectedParams.end();++k) {
            if(k.key()=="id" || k.key()=="created_utc") continue;
            if(param[k.key()]!=k.value()) { *error=QStringLiteral("拟合参数与阶段均值不一致：%1").arg(k.key());return p; }
        }
        f.parameters.id=param["id"].toString();f.parameters.createdUtc=param["created_utc"].toString();f.revision=saved["revision"].toInt();
        if(saved["stage_ids"]!=f.toJson()["stage_ids"] || saved["standard_deviations"]!=f.toJson()["standard_deviations"] || saved["warning"]!=f.warning) {
            *error=QStringLiteral("拟合来源或质量提示不一致");return p;
        }
        p.fits[w]=f;
    }
    *error=p.validate(); return p;
}
CalibrationProject CalibrationProject::importLegacy(const Fit& a,const Fit& b,QString *error)
{
    error->clear();
    auto p=create(a.dataset.name, a.dataset.unit);p.origin="legacy_import";
    if(a.dataset.wavelengthNm!=816 || b.dataset.wavelengthNm!=1368 || a.dataset.unit!=b.dataset.unit ||
       !a.dataset.hardwareMappingError().isEmpty() || !b.dataset.hardwareMappingError().isEmpty()) {
        *error=QStringLiteral("旧版两路参数单位或固定通道不兼容，保留原始库供历史读取");return p;
    }
    for(const auto& f:{a,b}) {
        QString e; Fit::fromJson(f.toJson(),&e);if(!e.isEmpty()) { *error=e;return p; }
        p.legacyFits.append(f);StageFit sf;sf.parameters=f;sf.revision=p.revision;
        sf.warning=QStringLiteral("旧版导入：缺失的原始阶段和采集时长未重建");p.fits[f.dataset.wavelengthNm]=sf;
    }
    return p;
}
}
