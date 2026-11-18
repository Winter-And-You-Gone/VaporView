#include <vaporview/SessionSdk.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>

#include <cstdio>

using namespace VaporView::SessionSdk;

namespace
{
constexpr quint64 timeUs = 1770000000000123;
constexpr quint64 sequence = 9007199254741001;
constexpr quint32 points = 300000;

#define VERIFY(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "Line %d: %s\n", __LINE__, #condition); return false; \
} } while (false)

template<class T>
void append(QByteArray& bytes, T value)
{
    const auto offset = bytes.size();
    bytes.resize(offset + sizeof(T));
    qToLittleEndian(value, bytes.data() + offset);
}

QByteArray fileHeader(quint16 source)
{
    QByteArray bytes = "VVRAWDAT";
    append<quint32>(bytes, 2);
    append<quint32>(bytes, 20);
    append(bytes, source);
    append<quint16>(bytes, 0);
    return bytes;
}

void addRecord(QByteArray& bytes, quint16 source, quint16 type, quint64 seq, const QByteArray& payload)
{
    append<quint32>(bytes, 0x44525756);
    append<quint32>(bytes, 36);
    append(bytes, timeUs);
    append<quint32>(bytes, quint32(payload.size()));
    append(bytes, source);
    append(bytes, type);
    append<quint32>(bytes, 0);
    append(bytes, seq);
    bytes += payload;
}

QByteArray pairedPayload(quint16 channel, quint32 count, bool incomplete)
{
    QByteArray bytes;
    bytes.reserve(48 + count * 8);
    for (quint16 val : {1, 48, int(channel), 1}) append(bytes, val);
    append<quint32>(bytes, 77);
    append<quint32>(bytes, 100000);
    append(bytes, count);
    append<quint32>(bytes, incomplete ? 0 : count);
    append<quint64>(bytes, 100200300400);
    append<quint64>(bytes, 3);
    append<quint32>(bytes, incomplete ? 4 : 0);
    append<quint32>(bytes, 0);
    if (!incomplete) for (quint32 i = 0; i < count; ++i) {
        append<qint32>(bytes, 16777217 + i);
        append<qint32>(bytes, -100 - qint32(i));
    }
    return bytes;
}

bool write(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.flush();
}

QByteArray jsonBytes(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QString sha256(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool verifyConcentrations(const QDir& source, const QDir& output)
{
    const QString fileId = "{61c87c86-948c-47ce-9465-5873791248f9}";
    const QString fileSha(64, 'a');
    QJsonObject p{{"wavelength", 1368}, {"channel", 48}, {"model", "linear"}, {"model_version", 1},
        {"parameter_id", "{c697851b-1736-4ddc-bd4c-3479b4983dd7}"}, {"a", 4}, {"b", 1},
        {"r_min", 0}, {"r_max", 1}, {"c_min", 1}, {"c_max", 5},
        {"r_squared", 1}, {"rmse", 0}, {"sample_count", 2}, {"concentration_count", 2}, {"unit", "ppmv"}};
    auto other = p;
    other["wavelength"] = 816;
    other["channel"] = 49;
    other["parameter_id"] = "{33220d3c-ab58-4eeb-b8c1-a46712e0f840}";
    QJsonObject runtime{{"kind", "water_runtime_parameters"}, {"schema_version", 1},
        {"file_id", fileId}, {"file_sha256", fileSha}, {"peak_algorithm", "pointwise_h2_over_h1_v1"},
        {"model", "validation_linear_v1"}, {"compensation", "unconfigured"},
        {"hardware_mapping", QJsonObject{{"816", 49}, {"1368", 48}}},
        {"task_id", "{0d2c0ff1-22ca-49b0-90de-e99524f17bc9}"}, {"task_name", "fixture"},
        {"created_utc", "2026-02-02T12:00:00.000Z"}, {"device_identity", QJsonObject()},
        {"channels", QJsonObject{{"816", other}, {"1368", p}}}, {"origin", "sky"}};
    runtime["sha256"] = sha256(jsonBytes(runtime));
    const QJsonObject snapshot{{"kind", "vaporview_session_calibration"}, {"schema_version", 1},
        {"file_id", fileId}, {"file_sha256", fileSha}, {"runtime", runtime}};
    QJsonObject row{{"session_id", "fixture"}, {"record_id", sha256("fixture|48|9007199254741001")},
        {"wavelength_nm", 1368}, {"fpga_channel", 48}, {"host_time_us", "1770000000000123"},
        {"fpga_tick", "100200300400"}, {"cycle_id", 77}, {"segment_id", "3"},
        {"waveform_sequence", "9007199254741001"}, {"peak", 0.25}, {"initial_concentration", 2},
        {"valid", true}, {"status", QStringLiteral("有效初始浓度；未进行温压补偿")}, {"quality_flags", 1},
        {"original_points", int(points)}, {"valid_points", int(points - 1)}, {"peak_index", 7}, {"compensated", false},
        {"compensation_status", QStringLiteral("未进行温压补偿")}, {"environment_status", "unavailable"},
        {"vcal_file_id", fileId}, {"vcal_sha256", fileSha}, {"model_parameters", p}, {"unit", "ppmv"},
        {"model_version", 1}, {"origin", "sky"}};
    row["sha256"] = sha256(jsonBytes(row));
    VERIFY(write(source.filePath("calibration_snapshot.json"), jsonBytes(snapshot)));
    VERIFY(write(source.filePath("calibration_concentrations.jsonl"), jsonBytes(row) + '\n'));

    VERIFY(write(source.filePath("session.json"),
        R"({"session_format_version":1,"session_name":"fixture","recording_origin":"sky","state":"complete","software_version":"test-version"})"));
    Reader reader;
    VERIFY(reader.open(source.absolutePath()));
    VERIFY(reader.metadata()["session_format_version"] == 1);
    VERIFY(reader.metadata()["session_name"] == "fixture");
    VERIFY(reader.metadata()["software_version"] == "test-version");
    const auto visit = reader.visit(Category::Concentrations, {timeUs, timeUs}, [&](const Record& record) {
        return record.sequence == sequence && record.timestampUs == timeUs && record.fields == row &&
            record.fields["initial_concentration"] == 2 && record.fields["unit"] == "ppmv" &&
            record.fields["quality_flags"] == 1 && !record.fields["compensated"].toBool();
    });
    VERIFY(visit && visit.records == 1);
    const auto result = reader.exportTo(Category::Concentrations, {timeUs, timeUs}, output.absolutePath());
    VERIFY(result && result.records == 1);
    QFile file(output.filePath("concentrations.json"));
    VERIFY(file.open(QIODevice::ReadOnly));
    auto exported = QJsonDocument::fromJson(file.readAll()).object();
    const auto digest = exported.take("sha256").toString();
    VERIFY(!digest.isEmpty() && digest == sha256(jsonBytes(exported)));
    VERIFY(exported["snapshot"].toObject() == snapshot && exported["results"].toArray()[0].toObject() == row);
    return true;
}

bool verify()
{
    QTemporaryDir temporary;
    VERIFY(temporary.isValid());
    QDir root(temporary.path());
    VERIFY(root.mkpath("source/raw") && root.mkpath("source/sensors") && root.mkdir("out"));
    QDir source(root.filePath("source"));
    QDir output(root.filePath("out"));
    const QByteArray sensors = "record_timestamp_us,nav_lat_deg,hc2a_temperature_c,hc2a_humidity_rh,ptb_pressure_hpa,sdfm300_distance_m\n"
        "1770000000000123,23.2,27.01,30.04,1001.23,1.3\n"
        "1770000000000123,23.3,27.02,,1001.25,1.4\n"
        "1770000000000124,23.4,27.03,30.06,1001.26,1.5\n";
    // Historical alias, no manifest, no concentrations or optional temperature stream.
    VERIFY(write(source.filePath("sensors/devices.csv"), sensors));
    QByteArray waves = fileHeader(5);
    addRecord(waves, 5, 2, sequence, pairedPayload(48, points, false));
    addRecord(waves, 5, 2, sequence + 1, pairedPayload(49, 8, true));
    VERIFY(write(source.filePath("raw/waveform.dat"), waves));
    QByteArray pressure = fileHeader(2);
    addRecord(pressure, 2, 1, 19, "1001.23");
    VERIFY(write(source.filePath("raw/ptb.dat"), pressure));
    const QByteArray features =
        "host_time_us,epsilon_time_us,original_point_count,search_start_index,search_end_index,channel_id,peak,mean,rms,peak_index,peak_x,min_value,max_value,quality_flags,source,component,algorithm_version,cycle_id,segment_id,fpga_tick,waveform_sequence\n"
        "1770000000000123,0,300000,0,0,48,0.25,0.2,0.3,7,7,0.1,0.25,1,fpga,H2/H1,1,77,3,100200300400,9007199254741001\n";
    VERIFY(write(source.filePath("sensors/waveform_features.csv"), features));

    Reader reader;
    VERIFY(reader.open(source.absolutePath()));
    VERIFY(reader.metadata()["manifest_present"].toBool() == false);
    VERIFY(reader.categories().contains(Category::Waveforms) && !reader.categories().contains(Category::Concentrations));
    VERIFY(reader.visit(Category::Concentrations, {}, [](const Record&) { return true; }).status == Status::Missing);
    const auto sensorsResult = reader.visit(Category::Sensors, {timeUs, timeUs}, [](const Record& r) {
        return r.timestampUs == timeUs && !r.sequence &&
            r.fields["ptb_pressure_hpa"] == (r.ordinal == 0 ? "1001.23" : "1001.25");
    });
    VERIFY(sensorsResult && sensorsResult.records == 2);
    const auto pressureResult = reader.visit(Category::PressureRaw, {}, [](const Record& r) {
        return r.timestampUs == timeUs && r.sequence == 19 && r.fields["source_id"] == 2 &&
            QByteArray::fromBase64(r.fields["payload_base64"].toString().toLatin1()) == "1001.23";
    });
    VERIFY(pressureResult && pressureResult.records == 1);
    const auto waveResult = reader.visit(Category::Waveforms, {timeUs, timeUs}, [](const Record& r) {
        return r.timestampUs == timeUs && r.sequence == sequence + r.ordinal &&
            r.fields["channel_id"] == double(48 + r.ordinal);
    });
    VERIFY(waveResult && waveResult.records == 2);
    Waveform frame;
    VERIFY(reader.readWaveform(0, frame));
    VERIFY(frame.h1.size() == points && frame.h2.size() == points);
    VERIFY(frame.h1.first() == 16777217 && frame.h2.last() == -300099);
    VERIFY(reader.readWaveform(1, frame));
    VERIFY(frame.h1.isEmpty() && frame.h2.isEmpty() && frame.record.fields["quality_flags"] == 4);
    VERIFY(reader.readWaveform(2, frame).status == Status::InvalidArgument);

    const auto featureResult = reader.visit(Category::CycleFeatures, {timeUs, timeUs}, [](const Record& r) {
        return r.timestampUs == timeUs && r.sequence == sequence && r.fields["peak"] == "0.25" &&
            r.fields["quality_flags"] == "1" && r.fields["component"] == "H2/H1";
    });
    VERIFY(featureResult && featureResult.records == 1);
    VERIFY(reader.exportTo(Category::CycleFeatures, {}, output.absolutePath()));
    QFile featureExport(output.filePath("cycle_features.csv"));
    VERIFY(featureExport.open(QIODevice::ReadOnly) && featureExport.readAll() == features);
    VERIFY(reader.exportTo(Category::Sensors, {timeUs, timeUs}, output.absolutePath()));
    QFile sensorExport(output.filePath("sensors.csv"));
    VERIFY(sensorExport.open(QIODevice::ReadOnly) && sensorExport.readAll() == sensors.left(sensors.lastIndexOf("1770000000000124")));
    VERIFY(reader.exportTo(Category::Sensors, {}, source.absolutePath()).status == Status::InvalidArgument);
    VERIFY(reader.exportTo(Category::Sensors, {}, output.absolutePath()).status == Status::InvalidArgument);
    auto exportWaves = reader.exportTo(Category::Waveforms, {}, output.absolutePath());
    VERIFY(exportWaves && exportWaves.records == 2);
    QFile waveExport(output.filePath("waveforms.jsonl"));
    VERIFY(waveExport.open(QIODevice::ReadOnly));
    auto first = QJsonDocument::fromJson(waveExport.readLine()).object();
    VERIFY(first["sequence"] == "9007199254741001" && first["timestamp_us"] == "1770000000000123");
    auto block = QJsonDocument::fromJson(waveExport.readLine()).object();
    VERIFY(block["component"] == "h1" && block["samples"].toArray()[0] == 16777217);
    const auto old = source.filePath("raw/waveform.dat");
    VERIFY(write(old, waves.chopped(1)));
    VERIFY(reader.open(source.absolutePath()));
    const auto recovered = reader.visit(Category::Waveforms, {}, [](const Record&) { return true; });
    VERIFY(recovered && recovered.records == 1 && !recovered.message.isEmpty());
    // A structurally valid record carrying a bad waveform layout reports corruption.
    QByteArray malformed = fileHeader(5);
    addRecord(malformed, 5, 2, sequence, QByteArray(48, '\0'));
    VERIFY(write(old, malformed));
    VERIFY(reader.open(source.absolutePath()));
    VERIFY(reader.readWaveform(0, frame).status == Status::Corrupt);
    return verifyConcentrations(source, output);
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    return verify() ? 0 : 1;
}
