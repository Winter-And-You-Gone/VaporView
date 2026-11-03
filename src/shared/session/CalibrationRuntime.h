#pragma once
#include "shared/session/HarmonicWaveform.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>
#include <cmath>

namespace VaporView::Session
{
inline double evaluateWaterLinear(double a,double b,double peak) {return std::fma(a,peak,b);}
inline QByteArray runtimeCalibrationDigest(const QJsonObject& j)
{return QCryptographicHash::hash(QJsonDocument(j).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex();}
inline QString validateRuntimeCalibration(const QJsonObject& document)
{
    auto j=document;const auto sha=j.take("sha256").toString();
    if(j["schema_version"]!=1 || j["kind"]!="water_runtime_parameters" || sha.toLatin1()!=runtimeCalibrationDigest(j) ||
       QUuid(j["file_id"].toString()).isNull() || !QRegularExpression("^[a-f0-9]{64}$").match(j["file_sha256"].toString()).hasMatch() ||
       j["peak_algorithm"]!="pointwise_h2_over_h1_v1" || j["model"]!="validation_linear_v1" || j["compensation"]!="unconfigured")
        return QStringLiteral("定标运行参数 schema、模型版本或摘要无效");
    if(j["hardware_mapping"].toObject()!=QJsonObject{{"816",0x31},{"1368",0x30}} ||
       QUuid(j["task_id"].toString()).isNull() || j["task_name"].toString().trimmed().isEmpty() ||
       !QDateTime::fromString(j["created_utc"].toString(),Qt::ISODateWithMs).isValid() || !j["device_identity"].isObject())
        return QStringLiteral("定标任务元数据或硬件映射无效");
    QString unit;
    for(int w:{816,1368}) {
        const auto p=j["channels"].toObject()[QString::number(w)].toObject();
        if(p["wavelength"]!=w || p["channel"]!=(w==816?0x31:0x30) || p["model"]!="linear" || p["model_version"]!=1 ||
           QUuid(p["parameter_id"].toString()).isNull())return QStringLiteral("双波长参数缺失或固定光路不兼容");
        for(const auto *key:{"a","b","r_min","r_max","c_min","c_max","r_squared","rmse","sample_count","concentration_count"})
            if(!p[QLatin1String(key)].isDouble() || !std::isfinite(p[QLatin1String(key)].toDouble()))return QStringLiteral("定标参数包含无效数值");
        for(const auto *key:{"sample_count","concentration_count"}) {
            const auto n=p[QLatin1String(key)].toDouble();
            if(n<2 || n>INT_MAX || std::floor(n)!=n)return QStringLiteral("拟合阶段/浓度水平数量无效");
        }
        if(p["rmse"].toDouble()<0 || p["concentration_count"].toDouble()>p["sample_count"].toDouble())return QStringLiteral("拟合统计无效");
        if(p["r_min"].toDouble()>=p["r_max"].toDouble() || p["c_min"].toDouble()<0 || p["c_min"].toDouble()>=p["c_max"].toDouble())
            return QStringLiteral("定标适用范围无效");
        const auto u=p["unit"].toString();if(u!="ppmv" && u!="mol/mol" && u!="mg/m3")return QStringLiteral("浓度单位不兼容");
        if(!unit.isEmpty() && unit!=u)return QStringLiteral("两路浓度单位不一致");unit=u;
    }
    return {};
}
inline QString validateSessionRuntimeSnapshot(const QJsonObject& snapshot)
{
    const auto runtime=snapshot["runtime"].toObject();const auto error=validateRuntimeCalibration(runtime);
    if(!error.isEmpty())return error;
    if(snapshot["kind"]!="vaporview_session_calibration" || snapshot["schema_version"]!=1 ||
        snapshot["file_id"]!=runtime["file_id"] || snapshot["file_sha256"]!=runtime["file_sha256"])
        return QStringLiteral("Session 定标快照与运行参数来源不一致");
    return {};
}
// Shared numeric predictor used by WaterCalibration::calculate and the headless
// Sky recorder. Model selection, identity and source checks precede this call.
inline bool calculateWaterInitial(double a,double b,double rMin,double rMax,double peak,
                                 double& initial,QString& status)
{
    if(!std::isfinite(peak)) {status=QStringLiteral("没有有效的完整周期峰值");return false;}
    if(peak<rMin || peak>rMax) {status=QStringLiteral("峰值超出定标范围；未外推");return false;}
    initial=evaluateWaterLinear(a,b,peak);
    if(!std::isfinite(initial) || initial<0) {status=QStringLiteral("初始浓度非有限或小于零");return false;}
    status=QStringLiteral("有效初始浓度；未进行温压补偿");return true;
}
inline QJsonObject calculateRuntimeConcentration(const QJsonObject& runtime,const QString& session,
    quint64 host,quint64 sequence,const HarmonicLayout& l,const FpgaWave::NormalizedFeature& f)
{
    const int w=l.channel==0x31?816:1368;const auto p=runtime["channels"].toObject()[QString::number(w)].toObject();
    double initial=std::numeric_limits<double>::quiet_NaN();QString status;
    bool valid=(f.quality&~quint32(InvalidReference))==0 && f.validPoints>0 && f.peakIndex>=0 && quint64(f.peakIndex)<l.originalPoints;
    if(valid)valid=calculateWaterInitial(p["a"].toDouble(),p["b"].toDouble(),p["r_min"].toDouble(),p["r_max"].toDouble(),f.peak,initial,status);
    else status=QStringLiteral("周期质量不合格（flags=%1）").arg(f.quality);
    const auto id=QString::fromLatin1(QCryptographicHash::hash((session+'|'+QString::number(l.channel)+'|'+QString::number(sequence)).toUtf8(),QCryptographicHash::Sha256).toHex());
    QJsonObject result{{"session_id",session},{"record_id",id},{"wavelength_nm",w},{"fpga_channel",l.channel},
        {"host_time_us",QString::number(host)},{"fpga_tick",QString::number(l.fpgaTick)},
        {"cycle_id",double(l.cycle)},{"segment_id",QString::number(l.segment)},{"waveform_sequence",QString::number(sequence)},
        {"peak",std::isfinite(f.peak)?QJsonValue(f.peak):QJsonValue(QJsonValue::Null)},
        {"initial_concentration",valid?QJsonValue(initial):QJsonValue(QJsonValue::Null)},
        {"valid",valid},{"status",status},{"quality_flags",double(f.quality)},
        {"original_points",double(l.originalPoints)},{"valid_points",double(f.validPoints)},{"peak_index",double(f.peakIndex)},{"compensated",false},
        {"compensation_status",QStringLiteral("未进行温压补偿")},{"environment_status","unavailable"},
        {"vcal_file_id",runtime["file_id"]},{"vcal_sha256",runtime["file_sha256"]},
        {"model_parameters",p},{"unit",p["unit"]},{"model_version",1},{"origin",runtime["origin"]}};
    result["sha256"]=QString::fromLatin1(runtimeCalibrationDigest(result));return result;
}
}
