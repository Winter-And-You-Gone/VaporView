#include "FpgaTelemetry.h"
#include "FpgaControlConfig.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringList>
#include <QtEndian>
#include <cmath>
#include <limits>

namespace VaporView::FpgaRemote
{
namespace
{
bool integer(const QJsonValue& v, double min = 0, double max = 4294967295.0)
{
    return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() >= min &&
        v.toDouble() <= max && std::floor(v.toDouble()) == v.toDouble();
}
bool decimal(const QJsonValue& v, quint64& out)
{
    if (!v.isString() || !QRegularExpression(QStringLiteral("^(0|[1-9][0-9]*)$")).match(v.toString()).hasMatch()) return false;
    bool ok = false;
    out = v.toString().toULongLong(&ok);
    return ok;
}
bool document(const QByteArray& payload, QJsonObject& out)
{
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return false;
    out = doc.object();
    return integer(out.value("version"), 1, 1);
}
QByteArray bytes(QJsonObject object)
{
    object.insert("version", 1);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
bool sensorSource(int source)
{
    return source == 0x40 || source == 0x42 || source == 0x43 ||
        source == 0x44 || source == 0x45 || source == 0x46;
}
FpgaSensor::SensorKind kind(int source)
{
    switch (source) {
    case 0x40: return FpgaSensor::SensorKind::Ptb210;
    case 0x42: return FpgaSensor::SensorKind::Epsilon;
    case 0x43: return FpgaSensor::SensorKind::Bmp390;
    case 0x44: return FpgaSensor::SensorKind::Sht45;
    case 0x45: return FpgaSensor::SensorKind::Tfa1500;
    case 0x46: return FpgaSensor::SensorKind::Ai8;
    default: return FpgaSensor::SensorKind::Unknown;
    }
}
// The persisted configuration has an exact shape. Validate before fromJson,
// whose convenience defaults/coercions are inappropriate for remote writes.
bool configurationShape(const QJsonValue& value, const QJsonValue& model, const QString& key = {})
{
    if (model.isObject()) {
        if (!value.isObject()) return false;
        const auto o = value.toObject(), m = model.toObject();
        if (o.size() != m.size()) return false;
        for (auto it = m.begin(); it != m.end(); ++it)
            if (!o.contains(it.key()) || !configurationShape(o.value(it.key()), it.value(), it.key())) return false;
        return true;
    }
    if (model.isArray()) {
        if (!value.isArray() || value.toArray().size() != model.toArray().size()) return false;
        const auto a = value.toArray(), m = model.toArray();
        for (int i = 0; i < a.size(); ++i) if (!configurationShape(a[i], m[i])) return false;
        return true;
    }
    if (model.isBool()) return value.isBool();
    if (model.isString()) return value.isString() && QStringList{"auto", "cypress", "winusb"}.contains(value.toString());
    if (key == "voltsPerCode") return value.isDouble() && std::isfinite(value.toDouble()) && value.toDouble() > 0;
    if (key == "polarity") return integer(value, -1, 1) && value.toInt() != 0;
    if (key == "scanAmplitude" || key == "sineAmplitude" || key == "bias" || key == "offset") return integer(value, -2147483648.0, 2147483647.0);
    if (!integer(value)) return false;
    if (key == "pressureSource") return value.toInt() == 0x40 || value.toInt() == 0x43;
    if (key == "rawWindowSeconds") return integer(value, 1, 60);
    if (key == "phaseMode") return integer(value, 0, 2);
    if (key == "mode") return integer(value, 0, 3);
    if (key == "slaveAddress") return integer(value, 1, 247);
    if (key == "channel") return integer(value, 1, 8);
    if (key == "minCode" || key == "maxCode" || key == "clearCode") return integer(value, 0, 0xfffff);
    if (key == "adcRateHz" || key == "updateRateHz" || key == "outputRateHz" || key == "spiClockHz" || key == "timeoutMs" || key == "pollIntervalMs") return integer(value, 1);
    return true;
}
bool configurationValid(const QJsonObject& object)
{
    if (!configurationShape(object, FpgaControlConfig{}.toJson())) return false;
    const auto c = FpgaControlConfig::fromJson(object);
    for (int i = 0; i < 2; ++i) if (c.dac[i].minCode > c.dac[i].maxCode || c.dlia[i].outputRateHz > c.adcRateHz) return false;
    return true;
}
}

bool validateControl(const QJsonObject& control, QString *error)
{
    const auto fail = [error] { if (error) *error = QStringLiteral("Invalid FPGA control payload"); return false; };
    if (!control.value("op").isString()) return fail();
    const auto op = control.value("op").toString();
    QStringList keys{"op"};
    if (control.contains("version")) { keys.append("version"); if (!integer(control.value("version"), 1, 1)) return fail(); }
    if (op == "connect") {
        keys << "locator" << "backend";
        if (!control.value("locator").isString() || !control.value("backend").isString() ||
            !QStringList{"auto", "cypress", "winusb"}.contains(control.value("backend").toString())) return fail();
    } else if (op == "configure") {
        keys << "configuration";
        if (!control.value("configuration").isObject() || !configurationValid(control.value("configuration").toObject())) return fail();
    } else if (op == "acquisition" || op == "wms" || op == "dac" || op == "sensor" || op == "raw") {
        keys << "enabled";
        if (!control.value("enabled").isBool()) return fail();
        if (op == "wms" || op == "dac") {
            keys << "channel";
            if (!integer(control.value("channel"), 0, 1)) return fail();
        }
        if (op == "sensor") {
            keys << "source";
            if (!integer(control.value("source"), 0, 65535) || !sensorSource(control.value("source").toInt())) return fail();
        }
    } else if (op == "temperature") {
        keys << "celsius";
        const auto v = control.value("celsius");
        if (!v.isDouble() || !std::isfinite(v.toDouble()) || v.toDouble() < -999 || v.toDouble() > 2147.4 ||
            std::abs(v.toDouble() * 10 - std::round(v.toDouble() * 10)) > 1e-6) return fail();
    } else if (op != "disconnect" && op != "refresh") return fail();
    for (auto it = control.begin(); it != control.end(); ++it) if (!keys.contains(it.key())) return fail();
    return true;
}
QByteArray encodeControl(const QJsonObject& control) { return validateControl(control) ? bytes(control) : QByteArray{}; }
bool parseControl(const QByteArray& payload, QJsonObject& control, QString *error)
{
    QJsonObject result;
    if (!document(payload, result) || !validateControl(result, error)) return false;
    control = result; return true;
}
QByteArray encodeStatus(const QJsonObject& status) { return bytes(status); }
bool parseStatus(const QByteArray& payload, QJsonObject& status)
{
    QJsonObject result; quint64 archived;
    if (!document(payload, result) || !result.value("connected").isBool() || !result.value("ready").isBool() ||
        !result.value("busy").isBool() || !result.value("detail").isString() || !result.value("configuration").isObject() ||
        !configurationValid(result.value("configuration").toObject()) || !result.value("registers").isObject() ||
        !decimal(result.value("archived_records"), archived)) return false;
    if (result.contains("recording_state") && !integer(result.value("recording_state"),0,2)) return false;
    if (result.contains("session_directory") && !result.value("session_directory").isString()) return false;
    const auto registers = result.value("registers").toObject();
    for (auto it = registers.begin(); it != registers.end(); ++it) {
        quint64 address;
        if (!decimal(it.key(), address) || address > 0xffffffffULL || !integer(it.value())) return false;
    }
    status = result; return true;
}

QByteArray encodeSensor(const FpgaSensor::Reading& r, quint64 hostTimestampUs)
{
    QJsonObject values;
#define PUT(field) if (r.field) values.insert(#field, double(*r.field));
    PUT(pressurePa) PUT(temperatureC) PUT(humidityPct) PUT(heaterMode) PUT(distanceMm) PUT(pressureMilliPa)
    PUT(ai8PvRaw) PUT(ai8SpRaw) PUT(ai8SvRaw) PUT(ai8PvMicroC) PUT(ai8SpMicroC) PUT(ai8OpRaw) PUT(ai8Alarm)
    PUT(ai8Control) PUT(ai8Host) PUT(ai8SetResult) PUT(ai8DeviceStatus) PUT(ai8DeviceError) PUT(ai8SampleCounter)
    PUT(bmpPressureRaw) PUT(bmpTemperatureRaw) PUT(epsilonMessageId) PUT(epsilonSequence)
#undef PUT
    if (r.epsilonDeviceTimestampUs) values.insert("epsilonDeviceTimestampUs", QString::number(*r.epsilonDeviceTimestampUs));
    // EPSILON data is a validated FDILink body, not a raw USB frame.
    if (!r.epsilonData.isEmpty()) values.insert("epsilonData", QString::fromLatin1(r.epsilonData.toBase64()));
    const auto& v = r.validity;
    return bytes({{"source",r.source},{"message",r.message},{"schema",r.schema},{"flags",double(r.flags)},
        {"tick",QString::number(r.timestamp)},{"host_timestamp_us",QString::number(hostTimestampUs)},
        {"valid",QJsonObject{{"structure",v.structure},{"crc",v.crc},{"online",v.deviceOnline},
             {"measurement",v.measurement},{"continuity_loss",v.continuityLoss},{"flags",double(v.rawFlags)}}},
        {"values",values},{"error",r.ai8DeviceError ? double(*r.ai8DeviceError) : 0.0}});
}
bool parseSensor(const QByteArray& payload, FpgaSensor::Reading& reading, quint64 *hostTimestampUs)
{
    QJsonObject o; quint64 timestamp, host;
    if (!document(payload,o) || !integer(o.value("source"),0,65535) || !sensorSource(o.value("source").toInt()) ||
        !integer(o.value("message"),0,65535) || !integer(o.value("schema"),0,65535) || !integer(o.value("flags")) ||
        !decimal(o.value("tick"),timestamp) || !decimal(o.value("host_timestamp_us"),host) ||
        !o.value("valid").isObject() || !o.value("values").isObject() || !integer(o.value("error"))) return false;
    FpgaSensor::Reading r;
    r.source=quint16(o.value("source").toInt()); r.kind=kind(r.source); r.message=quint16(o.value("message").toInt());
    r.schema=quint16(o.value("schema").toInt()); r.flags=quint32(o.value("flags").toDouble()); r.timestamp=timestamp;
    const auto v=o.value("valid").toObject();
    for (const auto *key : {"structure","crc","online","measurement","continuity_loss"}) if (!v.value(QLatin1String(key)).isBool()) return false;
    if (!integer(v.value("flags"))) return false;
    r.validity={v.value("structure").toBool(),v.value("crc").toBool(),v.value("online").toBool(),
        v.value("measurement").toBool(),v.value("continuity_loss").toBool(),quint32(v.value("flags").toDouble())};
    const auto values=o.value("values").toObject();
#define GET(field, minimum, maximum) if (values.contains(#field)) { const auto x=values.value(#field); if (!integer(x,minimum,maximum)) return false; r.field=decltype(r.field)::value_type(x.toDouble()); }
    for (const auto *key : {"pressurePa","temperatureC","humidityPct"}) {
        if (values.contains(QLatin1String(key)) && (!values.value(QLatin1String(key)).isDouble() || !std::isfinite(values.value(QLatin1String(key)).toDouble()))) return false;
    }
    if(values.contains("pressurePa")) r.pressurePa=values.value("pressurePa").toDouble();
    if(values.contains("temperatureC")) r.temperatureC=values.value("temperatureC").toDouble();
    if(values.contains("humidityPct")) r.humidityPct=values.value("humidityPct").toDouble();
    GET(pressureMilliPa,-2147483648.0,2147483647.0) GET(ai8PvRaw,-2147483648.0,2147483647.0)
    GET(ai8SpRaw,-2147483648.0,2147483647.0) GET(ai8SvRaw,-2147483648.0,2147483647.0)
    GET(ai8PvMicroC,-2147483648.0,2147483647.0) GET(ai8SpMicroC,-2147483648.0,2147483647.0)
    GET(heaterMode,0,4294967295.0) GET(distanceMm,0,4294967295.0) GET(ai8OpRaw,0,4294967295.0)
    GET(ai8Alarm,0,4294967295.0) GET(ai8Control,0,4294967295.0) GET(ai8Host,0,4294967295.0)
    GET(ai8SetResult,0,4294967295.0) GET(ai8DeviceStatus,0,4294967295.0) GET(ai8DeviceError,0,4294967295.0)
    GET(ai8SampleCounter,0,4294967295.0) GET(bmpPressureRaw,0,16777215) GET(bmpTemperatureRaw,0,16777215)
    GET(epsilonMessageId,0,255) GET(epsilonSequence,0,255)
#undef GET
    if(values.contains("epsilonDeviceTimestampUs")) {
        bool ok=false; const auto x=values.value("epsilonDeviceTimestampUs");
        if (!x.isString()) return false;
        r.epsilonDeviceTimestampUs=x.toString().toLongLong(&ok); if(!ok) return false;
    }
    if(values.contains("epsilonData")) {
        if (!values.value("epsilonData").isString()) return false;
        const auto encoded=values.value("epsilonData").toString().toLatin1();
        r.epsilonData=QByteArray::fromBase64(encoded);
        if(r.epsilonData.toBase64()!=encoded) return false;
    }
    reading=r; if(hostTimestampUs) *hostTimestampUs=host; return true;
}

QByteArray encodeWaveformPreview(const FpgaWave::CompletedStream& s)
{
    const qsizetype count = !s.dliaPoints.isEmpty() ? s.dliaPoints.size() :
        !s.unsigned32Samples.isEmpty() ? s.unsigned32Samples.size() : s.signed32Samples.size();
    const quint32 stride=quint32(qMax<qsizetype>(1,(count+63)/64));
    const quint64 previewStride=quint64(stride)*(s.preview ? s.previewStride : 1u);
    if(previewStride==0 || previewStride>std::numeric_limits<quint32>::max()) return {};
    QJsonArray points;
    for(qsizetype i=0;i<count;i+=stride) {
        if(!s.dliaPoints.isEmpty()) {const auto& p=s.dliaPoints[i]; points.append(QJsonObject{{"i1",p.i1},{"q1",p.q1},{"i2",p.i2},{"q2",p.q2},{"h1",p.h1},{"h2",p.h2},{"iq",p.hasIq},{"harmonics",p.hasHarmonics}});}
        else points.append(!s.unsigned32Samples.isEmpty() ? double(s.unsigned32Samples[i]) : double(s.signed32Samples[i]));
    }
    return bytes({{"source",s.source},{"message",s.message},{"flags",double(s.flags)},{"cycle",double(s.cycleId)},
        {"tick",QString::number(s.timestamp)},{"schema",s.schema},{"format",s.format},{"rate",double(s.rate)},
        {"original_points",double(s.totalPoints)},{"stride",double(previewStride)},{"bytes_per_point",double(s.bytesPerPoint)},
        {"adc_bits",s.adcBits},{"sample_type",!s.dliaPoints.isEmpty()?"dlia":!s.unsigned32Samples.isEmpty()?"uint32":"int32"},
        {"quality",QJsonObject{{"complete",s.complete},{"partial",s.partial},{"overflow",s.overflow},{"continuity_error",s.continuityError}}},{"points",points}});
}
bool parseWaveformPreview(const QByteArray& payload, FpgaWave::CompletedStream& stream)
{
    QJsonObject o; quint64 tick;
    if(!document(payload,o) || !decimal(o.value("tick"),tick) || !o.value("points").isArray() || !o.value("quality").isObject()) return false;
    for(const auto *k:{"source","message","schema","format","adc_bits"}) if(!integer(o.value(QLatin1String(k)),0,65535)) return false;
    for(const auto *k:{"flags","cycle","rate","original_points","bytes_per_point"}) if(!integer(o.value(QLatin1String(k)))) return false;
    if(!integer(o.value("stride"),1)) return false;
    const auto points=o.value("points").toArray();
    const auto q=o.value("quality").toObject();
    if(points.size()>64 || double(points.size())>o.value("original_points").toDouble()) return false;
    for(const auto *k:{"complete","partial","overflow","continuity_error"}) if(!q.value(QLatin1String(k)).isBool()) return false;
    FpgaWave::CompletedStream s;
    s.source=quint16(o.value("source").toInt()); s.message=quint16(o.value("message").toInt()); s.schema=quint16(o.value("schema").toInt());
    s.format=quint16(o.value("format").toInt()); s.adcBits=quint16(o.value("adc_bits").toInt()); s.timestamp=tick;
    s.flags=quint32(o.value("flags").toDouble()); s.cycleId=quint32(o.value("cycle").toDouble()); s.rate=quint32(o.value("rate").toDouble());
    s.totalPoints=quint32(o.value("original_points").toDouble()); s.bytesPerPoint=quint32(o.value("bytes_per_point").toDouble());
    s.preview=true; s.previewStride=quint32(o.value("stride").toDouble());
    s.complete=q.value("complete").toBool();s.partial=q.value("partial").toBool();s.overflow=q.value("overflow").toBool();s.continuityError=q.value("continuity_error").toBool();
    const auto type=o.value("sample_type").toString();
    if(type!="int32" && type!="uint32" && type!="dlia") return false;
    const bool raw=s.source==0x20 || s.source==0x21;
    const bool dlia=s.source==0x30 || s.source==0x31;
    if(s.rate==0 || s.totalPoints==0 || s.totalPoints>(1u<<20) ||
        (raw && (s.message!=0x1000 || (s.schema!=1 && s.schema!=2) || s.format>1 || s.bytesPerPoint!=4 ||
            s.adcBits==0 || s.adcBits>32 || type!=(s.format==0?"int32":"uint32"))) ||
        (dlia && (s.message!=0x1001 || s.schema!=2 || s.format>2 || type!="dlia" ||
            s.bytesPerPoint!=(s.format==0?8u:s.format==1?16u:24u))) || (!raw && !dlia)) return false;
    const auto appendWord=[&s](quint32 value) {
        char word[4]; qToLittleEndian(value, word); s.pointBytes.append(word,4);
    };
    for(const auto& value:points) {
        if(type=="dlia") {
            if(!value.isObject()) return false;
            const auto p=value.toObject();
            for(const auto *k:{"i1","q1","i2","q2","h1","h2"}) if(!integer(p.value(QLatin1String(k)),-2147483648.0,2147483647.0)) return false;
            if(!p.value("iq").isBool() || !p.value("harmonics").isBool()) return false;
            s.dliaPoints.append({p.value("i1").toInt(),p.value("q1").toInt(),p.value("i2").toInt(),p.value("q2").toInt(),p.value("h1").toInt(),p.value("h2").toInt(),p.value("iq").toBool(),p.value("harmonics").toBool()});
            if(s.format!=0) {appendWord(quint32(p.value("i1").toInt()));appendWord(quint32(p.value("q1").toInt()));appendWord(quint32(p.value("i2").toInt()));appendWord(quint32(p.value("q2").toInt()));}
            if(s.format!=1) {appendWord(quint32(p.value("h1").toInt()));appendWord(quint32(p.value("h2").toInt()));}
        } else if(type=="uint32") {if(!integer(value)) return false;s.unsigned32Samples.append(quint32(value.toDouble()));appendWord(quint32(value.toDouble()));}
        else {if(!integer(value,-2147483648.0,2147483647.0)) return false;s.signed32Samples.append(qint32(value.toDouble()));appendWord(quint32(qint32(value.toDouble())));}
    }
    stream=s;return true;
}
}
