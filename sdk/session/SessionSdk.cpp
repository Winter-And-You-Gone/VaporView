#include <vaporview/SessionSdk.h>

#include "TcpWaveEncoding.h"
#include "ground/calibration/RecordedConcentrations.h"
#include "ground/session/SessionCsv.h"
#include "ground/session/SessionWaveformRepository.h"
#include "shared/io/SafeWritePath.h"
#include "shared/session/FpgaSessionArchive.h"
#include "shared/session/HarmonicWaveform.h"
#include "shared/session/SessionPathResolver.h"
#include "shared/session/SessionSensorCsv.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <utility>

namespace VaporView::SessionSdk
{
namespace
{
using Session::SessionFileKind;
namespace Raw = SessionRawDat;
namespace Csv = Ground::SessionCsv;
namespace Calibration = Ground::Calibration;

struct Source
{
    Category category;
    SessionFileKind kind;
    const char* name;
    quint16 rawSource = 0;
};

constexpr Source sources[] = {
    {Category::Sensors, SessionFileKind::SensorSummaryCsv, "sensors"},
    {Category::NavigationRaw, SessionFileKind::NavigationRaw, "navigation_raw", Raw::kSourceNavigation},
    {Category::PressureRaw, SessionFileKind::PressureRaw, "pressure_raw", Raw::kSourcePressure},
    {Category::TemperatureHumidityRaw, SessionFileKind::TemperatureHumidityRaw, "temperature_humidity_raw", Raw::kSourceTemperatureHumidity},
    {Category::DistanceRaw, SessionFileKind::DistanceRaw, "distance_raw", Raw::kSourceDistance},
    {Category::Waveforms, SessionFileKind::WaveformRaw, "waveforms", Raw::kSourceWaveform},
    {Category::CycleFeatures, SessionFileKind::WaveformFeaturesCsv, "cycle_features"},
    {Category::TcpPeaks, SessionFileKind::WaveformPeaksCsv, "tcp_peaks"}
};

QString decimal(quint64 value) { return QString::number(value); }
Result corrupt(const QString& detail) { return {Status::Corrupt, detail}; }
Result ioError(const QFileDevice& file) { return {Status::IoError, file.errorString()}; }
Result missing() { return {Status::Missing, QStringLiteral("Session category is absent")}; }

bool contains(Range range, quint64 timestamp)
{
    return timestamp >= range.firstUs && timestamp <= range.lastUs;
}

bool contained(const QString& root, const QString& path)
{
    return VaporView::isContainedWritePath(root, path);
}

QString csvLine(const QStringList& fields)
{
    QStringList escaped;
    escaped.reserve(fields.size());
    for (const auto& field : fields) escaped.append(SessionSensorCsv::escape(field));
    return escaped.join(',') + '\n';
}

QJsonObject csvFields(const QStringList& headers, const QStringList& fields)
{
    QJsonObject row;
    for (qsizetype i = 0; i < headers.size(); ++i) row.insert(headers[i], fields[i]);
    return row;
}

QJsonObject recordJson(const Record& record)
{
    QJsonObject json{{"ordinal", decimal(record.ordinal)}, {"timestamp_us", decimal(record.timestampUs)},
                     {"fields", record.fields}};
    if (record.sequence) json["sequence"] = decimal(*record.sequence);
    return json;
}

QByteArray jsonLine(const QJsonObject& json)
{
    return QJsonDocument(json).toJson(QJsonDocument::Compact) + '\n';
}

bool write(QSaveFile& file, const QByteArray& bytes)
{
    return file.write(bytes) == bytes.size();
}

QByteArray readAt(QFile& file, quint64 offset, quint32 count)
{
    return file.seek(qint64(offset)) ? file.read(count) : QByteArray();
}

Result inspectRaw(const Raw::RawScanResult& scan)
{
    if (scan.success()) return {Status::Ok, scan.warning};
    return {scan.status == Raw::RawReadStatus::IoError ? Status::IoError : Status::Corrupt, scan.error};
}

QJsonObject rawFields(const Raw::RawRecordHeader& h)
{
    return {{"source_id", h.sourceId}, {"record_type", h.recordType},
            {"flags", double(h.flags)}, {"payload_size", double(h.payloadSize)}};
}

struct LegacyChoice
{
    Ground::SessionWaveformCatalog catalog;
    Ground::SessionWaveformCatalog second;
    bool paired = false;
};

Result visitCsv(const QString& path, Category category, Range range, const Reader::Visitor& visitor)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return ioError(file);
    QTextStream stream(&file);
    QString error;
    const auto headers = Csv::parseCsvLine(stream.readLine(), &error);
    const int timeIndex = Csv::findHeaderIndex(headers, category == Category::Sensors
        ? QStringList{"record_timestamp_us", "epsilon_host_timestamp_us", "rtk_timestamp_us"}
        : QStringList{"host_time_us", "epsilon_time_us"});
    if (!error.isEmpty() || timeIndex < 0 || QSet<QString>(headers.begin(), headers.end()).size() != headers.size())
        return corrupt(QStringLiteral("Invalid CSV header: %1").arg(path));
    const int sequenceIndex = Csv::findHeaderIndex(headers, {"waveform_sequence"});
    quint64 ordinal = 0, selected = 0, lineNumber = 1;
    while (!stream.atEnd()) {
        const auto line = stream.readLine();
        ++lineNumber;
        if (line.trimmed().isEmpty()) continue;
        const auto fields = Csv::parseCsvLine(line, &error);
        bool timeOk = false, sequenceOk = false;
        const quint64 timestamp = Csv::csvValueAt(fields, timeIndex).toULongLong(&timeOk);
        const auto sequenceText = Csv::csvValueAt(fields, sequenceIndex);
        const quint64 sequence = sequenceText.toULongLong(&sequenceOk);
        if (!error.isEmpty() || fields.size() != headers.size() || !timeOk ||
            (!sequenceText.isEmpty() && !sequenceOk))
            return corrupt(QStringLiteral("Invalid CSV row %1: %2").arg(lineNumber).arg(path));
        const auto index = ordinal++;
        if (!contains(range, timestamp)) continue;
        if (!visitor({index, timestamp, sequenceOk ? std::optional<quint64>(sequence) : std::nullopt,
                      csvFields(headers, fields)}))
            return {Status::Cancelled, QStringLiteral("Visitor stopped"), selected};
        ++selected;
    }
    if (file.error() != QFileDevice::NoError) return ioError(file);
    return {Status::Ok, {}, selected};
}

}  // namespace

struct Reader::State
{
    QString directory;
    QJsonObject metadata;
    QMap<Category, QString> paths;
    QMap<Category, quint16> rawSources;
    Raw::RawScanResult waveIndex;
    Result waveformStatus = missing();
    QList<LegacyChoice> legacy;
    quint64 waveCount = 0;

    Result waveform(quint64 ordinal, Record& record, Waveform* data) const;
};

QString version() { return QStringLiteral(VAPORVIEW_SESSION_SDK_VERSION); }

QString categoryName(Category category)
{
    if (category == Category::Concentrations) return QStringLiteral("concentrations");
    for (const auto& source : sources)
        if (source.category == category) return QString::fromLatin1(source.name);
    return {};
}

Reader::Reader() = default;
Reader::~Reader() = default;

Result Reader::open(const QString& sessionDirectory)
{
    state_.reset();
    auto state = std::make_unique<State>();
    const QFileInfo input(sessionDirectory);
    if (!input.isDir() || sessionDirectory.trimmed().isEmpty())
        return {Status::InvalidArgument, QStringLiteral("Supply a Session directory")};
    state->directory = QDir::fromNativeSeparators(input.canonicalFilePath());
    const QDir root(state->directory);
    const auto manifestPath = root.filePath(QStringLiteral("session.json"));
    if (!contained(state->directory, manifestPath)) return corrupt(QStringLiteral("Unsafe Session manifest path"));
    const auto loaded = Ground::SessionLoader::loadMetadata(state->directory);
    if (!loaded.success || QDir(loaded.metadata.sessionDirectory) != root)
        return corrupt(loaded.success ? QStringLiteral("Select the Session itself") : loaded.error);
    const auto context = Session::loadSessionPathContext(state->directory);
    const auto& md = loaded.metadata;
    state->metadata = {{"sdk_version", version()}, {"session_name", md.sessionName},
        {"recording_origin", Session::recordingOriginToString(md.recordingOrigin)},
        {"software_version", context.manifest["software_version"]},
        {"start_time_utc", md.startTimeUtc}, {"end_time_utc", md.endTimeUtc},
        {"manifest_present", context.manifestPresent}, {"state", context.manifest["state"]},
        {"session_format_version", context.manifest["session_format_version"]},
        {"sensor_rows_declared", decimal(md.sensorRows)}, {"waveform_frames_declared", decimal(md.waveformFrames)},
        {"sensor_rate_hz", md.sensorExportRateHz}, {"waveform_rate_hz", md.waveformExportRateHz},
        {"waveform_mode", md.waveformExportMode}};
    for (const auto& source : sources) {
        const auto path = Session::resolveSessionPath(context, source.kind);
        if (!contained(state->directory, path.absolutePath))
            return corrupt(QStringLiteral("Unsafe Session data path: %1").arg(path.relativePath));
        if (path.exists) state->paths.insert(source.category, path.absolutePath);
        if (source.rawSource) state->rawSources.insert(source.category, source.rawSource);
    }
    const auto snapshot = root.filePath(QStringLiteral("calibration_snapshot.json"));
    const auto concentrations = root.filePath(QStringLiteral("calibration_concentrations.jsonl"));
    if (!contained(state->directory, snapshot) || !contained(state->directory, concentrations))
        return corrupt(QStringLiteral("Unsafe concentration path"));
    if (QFileInfo::exists(concentrations) || QFileInfo::exists(snapshot))
        state->paths.insert(Category::Concentrations, snapshot);

    bool tryLegacy = !state->paths.contains(Category::Waveforms);
    if (!tryLegacy) {
        QFile file(state->paths.value(Category::Waveforms));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Unbuffered)) state->waveformStatus = ioError(file);
        else {
            Raw::RawScanOptions options;
            options.expectedSourceId = Raw::kSourceWaveform;
            state->waveIndex = Raw::scan(file, options);
            state->waveformStatus = inspectRaw(state->waveIndex);
            state->waveCount = quint64(state->waveIndex.records.size());
            tryLegacy = state->waveIndex.status == Raw::RawReadStatus::NotUnifiedFormat ||
                (state->waveformStatus && state->waveCount == 0);
        }
    }

    if (tryLegacy) {
        const auto archive = Session::FpgaSessionArchive::rawPath(state->directory);
        if (!contained(state->directory, archive) || !contained(state->directory, md.waveformDirectory) ||
            !contained(state->directory, md.waveformIndexFilename))
            return corrupt(QStringLiteral("Unsafe legacy waveform path"));
        const auto legacyFiles = QDir(md.waveformDirectory).entryList({QStringLiteral("*.dat")}, QDir::Files);
        if (QFileInfo::exists(archive) || QFileInfo::exists(md.waveformIndexFilename) || !legacyFiles.isEmpty()) {
            auto legacyMetadata = md;
            legacyMetadata.waveformRawFilename.clear();
            auto legacy = Ground::SessionWaveformRepository::loadCatalog(legacyMetadata, {}, true);
            if (!legacy.success) state->waveformStatus = corrupt(legacy.error);
            else if (!legacy.catalog.isEmpty()) {
                if (legacy.catalog.fpgaChoices.isEmpty()) {
                    state->waveCount = legacy.catalog.frameCount;
                    state->legacy.append({std::move(legacy.catalog), {}, false});
                } else {
                    // Historical VLP1 archives have one timeline per channel. Pair components by index.
                    for (const auto& choice : legacy.catalog.fpgaChoices) {
                        if (choice.component != 0) continue;
                        LegacyChoice channel;
                        channel.catalog = legacy.catalog;
                        const auto index = &choice - legacy.catalog.fpgaChoices.constData();
                        Ground::SessionWaveformRepository::selectFpgaWaveform(channel.catalog, int(index));
                        for (qsizetype i = 0; i < legacy.catalog.fpgaChoices.size(); ++i) {
                            const auto& second = legacy.catalog.fpgaChoices[i];
                            if (second.source != choice.source || second.component != 1) continue;
                            channel.second = legacy.catalog;
                            Ground::SessionWaveformRepository::selectFpgaWaveform(channel.second, int(i));
                            channel.paired = true;
                            break;
                        }
                        state->waveCount += channel.catalog.frameCount;
                        state->legacy.append(std::move(channel));
                    }
                }
                state->waveformStatus = {Status::Ok, legacy.warning};
            } else if (state->waveformStatus.status == Status::Missing) {
                state->waveformStatus = {Status::Ok, legacy.warning};
            }
            state->paths.insert(Category::Waveforms, archive);
        }
    }
    if (!context.manifestPresent && state->paths.isEmpty())
        return corrupt(QStringLiteral("No recognizable Session files"));
    state_ = std::move(state);
    return {Status::Ok, loaded.warning};
}

QJsonObject Reader::metadata() const { return state_ ? state_->metadata : QJsonObject(); }
QList<Category> Reader::categories() const { return state_ ? state_->paths.keys() : QList<Category>(); }

Result Reader::State::waveform(quint64 ordinal, Record& record, Waveform* data) const
{
    if (!waveformStatus) return waveformStatus;
    if (ordinal >= waveCount) return {Status::InvalidArgument, QStringLiteral("Waveform ordinal is out of range")};
    record.ordinal = ordinal;
    if (!legacy.isEmpty()) {
        for (const auto& choice : legacy) {
            const auto& catalog = choice.catalog;
            if (ordinal >= catalog.frameCount) {
                ordinal -= catalog.frameCount;
                continue;
            }
            if (!catalog.fpgaTimestampsUs.isEmpty()) {
                const auto i = qsizetype(ordinal);
                record.timestampUs = catalog.fpgaTimestampsUs[i];
                record.fields = {{"encoding", "legacy_vlp1_float32"},
                    {"channel_id", catalog.fpgaSource},
                    {"point_count", double(catalog.rawTcpFrames[i].harmonicPayloadSize / sizeof(float))}};
            } else if (!catalog.indexedFrames.isEmpty()) {
                const auto& index = catalog.indexedFrames[qsizetype(ordinal)];
                record.timestampUs = index.timestampUs;
                record.fields = {{"encoding", "legacy_indexed_float32"}, {"point_count", double(index.pointCount)}};
            } else {
                const auto name = catalog.sourceFilename(ordinal);
                if (!contained(directory, name)) return corrupt(QStringLiteral("Unsafe legacy waveform segment"));
                const auto segment = std::find_if(catalog.legacySegments.cbegin(), catalog.legacySegments.cend(),
                    [&](const auto& s) { return name == s.filename; });
                const auto pointCount = quint64(catalog.pointsPerFrame);
                if (segment == catalog.legacySegments.cend() || pointCount * sizeof(float) > Raw::kMaxPayloadSize)
                    return corrupt(QStringLiteral("Invalid legacy waveform segment"));
                const auto frameBytes = sizeof(quint64) + pointCount * sizeof(float);
                QFile file(name);
                if (!file.open(QIODevice::ReadOnly)) return ioError(file);
                const auto time = readAt(file, (ordinal - segment->startFrame) * frameBytes, sizeof(quint64));
                if (time.size() != sizeof(quint64)) return corrupt(QStringLiteral("Truncated legacy waveform timestamp"));
                record.timestampUs = qFromLittleEndian<quint64>(time.constData());
                record.fields = {{"encoding", "legacy_segment_float32"}, {"point_count", double(pointCount)}};
            }
            if (!data) return {};
            auto frame = Ground::SessionWaveformRepository::readFrame(catalog, ordinal);
            if (!frame.success) return corrupt(frame.error);
            if (choice.paired) {
                const auto h1 = Ground::SessionWaveformRepository::readFrame(choice.second, ordinal);
                if (!h1.success || h1.timestampUs != frame.timestampUs || h1.samples.size() != frame.samples.size())
                    return corrupt(QStringLiteral("Legacy FPGA harmonic components do not match"));
                data->h1 = QVector<double>(h1.samples.cbegin(), h1.samples.cend());
                data->h2 = QVector<double>(frame.samples.cbegin(), frame.samples.cend());
            } else if (catalog.fpgaSource) data->raw = std::move(frame.samples);
            else data->harmonic = std::move(frame.samples);
            return {};
        }
    }
    const auto& index = waveIndex.records[qsizetype(ordinal)];
    const auto& h = index.header;
    record.timestampUs = h.hostTimestampUs;
    record.sequence = h.sequence;
    record.fields = rawFields(h);
    QFile file(paths.value(Category::Waveforms));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Unbuffered)) return ioError(file);
    if (h.recordType == Raw::kRecordTypeHarmonicPair) {
        Session::HarmonicLayout layout;
        const auto prefix = readAt(file, index.payloadOffset, Session::kHarmonicHeaderSize);
        if (!Session::parseHarmonicLayout(prefix, h.payloadSize, layout))
            return corrupt(QStringLiteral("Invalid FPGA waveform layout at ordinal %1").arg(ordinal));
        record.fields["encoding"] = layout.sampleType == Session::HarmonicSampleType::Int32 ? "int32" : "float64";
        record.fields["channel_id"] = layout.channel;
        record.fields["cycle_id"] = double(layout.cycle);
        record.fields["sample_rate"] = double(layout.rate);
        record.fields["point_count"] = double(layout.originalPoints);
        record.fields["stored_points"] = double(layout.storedPoints);
        record.fields["quality_flags"] = double(layout.quality);
        record.fields["fpga_tick"] = decimal(layout.fpgaTick);
        record.fields["segment_id"] = decimal(layout.segment);
        record.fields["input_format"] = layout.inputFormat;
        if (data) {
            const auto payload = readAt(file, index.payloadOffset, h.payloadSize);
            if (payload.size() != h.payloadSize || !Session::harmonicSamplePairs(payload, data->h1, data->h2))
                return corrupt(QStringLiteral("Truncated FPGA waveform at ordinal %1").arg(ordinal));
        }
        return {};
    }
    Raw::WaveformPayloadLayout payloadLayout;
    const bool combined = (h.flags & Raw::kWaveformCombinedPayloadFlag) != 0;
    if (combined) {
        const auto prefix = readAt(file, index.payloadOffset, Raw::kWaveformPayloadPrefixSize);
        if (!Raw::parseWaveformPayloadLayout(prefix, h.payloadSize, &payloadLayout))
            return corrupt(QStringLiteral("Invalid TCP waveform layout at ordinal %1").arg(ordinal));
    } else {
        payloadLayout.harmonicSize = h.payloadSize;
        payloadLayout.harmonicOffset = 0;
    }
    if (payloadLayout.rawSignalSize % sizeof(float) || payloadLayout.harmonicSize % sizeof(float))
        return corrupt(QStringLiteral("Invalid TCP sample count at ordinal %1").arg(ordinal));
    auto encoding = waveIndex.fileHeader.version == 1 ? TcpFloatEncoding::Unknown : tcpFloatEncodingFromRawDatFlags(h.flags);
    record.fields["encoding"] = tcpFloatEncodingLabel(true, encoding);
    record.fields["raw_points"] = double(payloadLayout.rawSignalSize / sizeof(float));
    record.fields["point_count"] = double(payloadLayout.harmonicSize / sizeof(float));
    if (data) {
        const auto harmonic = readAt(file, index.payloadOffset + payloadLayout.harmonicOffset, payloadLayout.harmonicSize);
        const auto raw = readAt(file, index.payloadOffset + payloadLayout.rawSignalOffset, payloadLayout.rawSignalSize);
        if (harmonic.size() != payloadLayout.harmonicSize || raw.size() != payloadLayout.rawSignalSize)
            return corrupt(QStringLiteral("Truncated TCP waveform at ordinal %1").arg(ordinal));
        if (encoding == TcpFloatEncoding::Unknown) encoding = autoDetectTcpFloatEncoding(harmonic.isEmpty() ? raw : harmonic);
        record.fields["encoding"] = tcpFloatEncodingLabel(true, encoding);
        data->harmonic = decodeTcpFloatPayload(harmonic, encoding);
        data->raw = decodeTcpFloatPayload(raw, encoding);
    }
    return {};
}

Result Reader::readWaveform(quint64 ordinal, Waveform& frame) const
{
    frame = {};
    if (!state_) return {Status::InvalidArgument, QStringLiteral("Open a Session first")};
    Waveform next;
    auto result = state_->waveform(ordinal, next.record, &next);
    if (result) {
        frame = std::move(next);
        result.records = 1;
        result.message = state_->waveformStatus.message;
    }
    return result;
}

Result Reader::visit(Category category, Range range, const Visitor& visitor) const
{
    if (!state_ || !visitor || range.firstUs > range.lastUs || categoryName(category).isEmpty())
        return {Status::InvalidArgument, QStringLiteral("Open a Session and supply a valid category, time range and visitor")};
    if (!state_->paths.contains(category)) return missing();
    const auto path = state_->paths.value(category);
    quint64 selected = 0;
    const auto emitRecord = [&](const Record& record) {
        if (!contains(range, record.timestampUs)) return true;
        if (!visitor(record)) return false;
        ++selected;
        return true;
    };
    const auto stopped = [&] { return Result{Status::Cancelled, QStringLiteral("Visitor stopped"), selected}; };
    if (category == Category::Waveforms) {
        if (!state_->waveformStatus) return state_->waveformStatus;
        for (quint64 i = 0; i < state_->waveCount; ++i) {
            Record record;
            const auto result = state_->waveform(i, record, nullptr);
            if (!result) return result;
            if (!emitRecord(record)) return stopped();
        }
        return {Status::Ok, state_->waveformStatus.message, selected};
    }
    if (category == Category::Concentrations) {
        const auto recorded = Calibration::readRecordedConcentrations(path);
        if (!recorded.success) return corrupt(recorded.error);
        for (qsizetype i = 0; i < recorded.rows.size(); ++i) {
            const auto row = recorded.rows[i].toObject();
            if (!emitRecord({quint64(i), row["host_time_us"].toString().toULongLong(),
                             row["waveform_sequence"].toString().toULongLong(), row})) return stopped();
        }
        return {Status::Ok, {}, selected};
    }
    if (const auto source = state_->rawSources.value(category); source) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Unbuffered)) return ioError(file);
        Raw::RawScanOptions options;
        options.expectedSourceId = source;
        const auto scan = Raw::scan(file, options);
        if (const auto result = inspectRaw(scan); !result) return result;
        for (qsizetype i = 0; i < scan.records.size(); ++i) {
            const auto& index = scan.records[i];
            const auto& h = index.header;
            if (!contains(range, h.hostTimestampUs)) continue;
            const auto payload = readAt(file, index.payloadOffset, h.payloadSize);
            if (payload.size() != h.payloadSize) return corrupt(QStringLiteral("Truncated raw record: %1").arg(path));
            auto fields = rawFields(h);
            fields["payload_base64"] = QString::fromLatin1(payload.toBase64());
            if (!emitRecord({quint64(i), h.hostTimestampUs, h.sequence, fields})) return stopped();
        }
        return {Status::Ok, scan.warning, selected};
    }
    return visitCsv(path, category, range, visitor);
}

Result Reader::exportTo(Category category, Range range, const QString& directory) const
{
    if (!state_ || categoryName(category).isEmpty() || range.firstUs > range.lastUs)
        return {Status::InvalidArgument, QStringLiteral("Open a Session and select a valid category and time range")};
    if (!state_->paths.contains(category)) return missing();
    const QFileInfo destination(directory);
    const auto target = QDir::fromNativeSeparators(destination.canonicalFilePath());
    if (!destination.isDir() || directory.trimmed().isEmpty() || target.isEmpty() ||
        destination.isSymLink() || destination.isJunction() || QDir(target) == QDir(state_->directory) ||
        contained(state_->directory, target))
        return {Status::InvalidArgument, QStringLiteral("Choose an existing export directory outside the source Session")};
    const bool csv = category == Category::Sensors || category == Category::CycleFeatures || category == Category::TcpPeaks;
    const auto filename = QDir(target).filePath(categoryName(category) +
        (csv ? ".csv" : category == Category::Concentrations ? ".json" : ".jsonl"));
    if (!contained(target, filename) || QFileInfo::exists(filename))
        return {Status::InvalidArgument, QStringLiteral("Export destination already exists or is unsafe: %1").arg(filename)};
    if (category == Category::Concentrations) {
        auto recorded = Calibration::readRecordedConcentrations(state_->paths.value(category));
        if (!recorded.success) return corrupt(recorded.error);
        QJsonArray selected;
        for (const auto& value : recorded.rows)
            if (contains(range, value.toObject()["host_time_us"].toString().toULongLong())) selected.append(value);
        recorded.rows = std::move(selected);
        QString error;
        if (!Calibration::exportRecordedConcentrations(filename, recorded, &error)) return {Status::IoError, error};
        return {Status::Ok, {}, quint64(recorded.rows.size())};
    }
    QSaveFile file(filename);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return ioError(file);
    QStringList headers;
    if (csv) {
        QFile source(state_->paths.value(category));
        if (!source.open(QIODevice::ReadOnly | QIODevice::Text)) return ioError(source);
        QTextStream stream(&source);
        headers = Csv::parseCsvLine(stream.readLine());
        if (!write(file, csvLine(headers).toUtf8())) return ioError(file);
    }
    Result issue;
    const auto result = visit(category, range, [&](const Record& row) {
        if (csv) {
            QStringList values;
            for (const auto& key : headers) values.append(row.fields.value(key).toString());
            if (write(file, csvLine(values).toUtf8())) return true;
        } else if (category == Category::Waveforms) {
            Waveform frame;
            issue = readWaveform(row.ordinal, frame);
            if (!issue) return false;
            if (!write(file, jsonLine(recordJson(frame.record)))) {
                issue = ioError(file);
                return false;
            }
            const auto writeSamples = [&](const QString& component, const auto& values) {
                constexpr qsizetype blockSize = 1024;
                for (qsizetype offset = 0; offset < values.size(); offset += blockSize) {
                    QJsonArray samples;
                    for (qsizetype i = offset; i < std::min(values.size(), offset + blockSize); ++i) {
                        const double value = values[i];
                        samples.append(std::isfinite(value) ? QJsonValue(value) : QJsonValue(QJsonValue::Null));
                    }
                    if (!write(file, jsonLine({{"ordinal", decimal(row.ordinal)}, {"component", component},
                            {"first_point", double(offset)}, {"samples", samples}}))) return false;
                }
                return true;
            };
            if (writeSamples("h1", frame.h1) && writeSamples("h2", frame.h2) &&
                writeSamples("raw", frame.raw) && writeSamples("harmonic", frame.harmonic)) return true;
        } else if (write(file, jsonLine(recordJson(row)))) return true;
        issue = ioError(file);
        return false;
    });
    if (!issue) return issue;
    if (!result) return result;
    if (!file.commit()) return ioError(file);
    return result;
}

}  // namespace VaporView::SessionSdk
