#include "shared/session/SessionManifest.h"

#include "shared/session/UnifiedRawDat.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <QSaveFile>
#include <QStringView>

namespace VaporView::Session
{
namespace
{

QString numberText(quint64 value)
{
    return QString::number(value);
}

quint64 unsignedFromJson(const QJsonValue& value, quint64 fallback = 0)
{
    if (value.isString())
    {
        bool ok = false;
        const quint64 parsed = value.toString().toULongLong(&ok);
        return ok ? parsed : fallback;
    }
    if (value.isDouble())
    {
        const double number = value.toDouble();
        return number >= 0.0 ? static_cast<quint64>(number) : fallback;
    }
    return fallback;
}

int intFromJson(const QJsonValue& value, int fallback)
{
    if (value.isString())
    {
        bool ok = false;
        const int parsed = value.toString().toInt(&ok);
        return ok ? parsed : fallback;
    }
    return value.toInt(fallback);
}

QJsonValue optionalString(const QString& value)
{
    return value.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(value);
}

QJsonObject captureToJson(const CaptureMetadata& capture)
{
    QJsonObject object;
    object.insert(QStringLiteral("telemetry_transport"), optionalString(capture.telemetryTransport));
    object.insert(QStringLiteral("telemetry_endpoint"), optionalString(capture.telemetryEndpoint));
    object.insert(QStringLiteral("telemetry_port"), optionalString(capture.telemetryPort));
    object.insert(QStringLiteral("telemetry_baud"), optionalString(capture.telemetryBaud));
    return object;
}

QJsonObject countsToJson(const SessionRecordCounts& counts)
{
    QJsonObject object;
    object.insert(QStringLiteral("sensor_rows"), numberText(counts.sensorRows));
    object.insert(QStringLiteral("temperature_controller_rows"), numberText(counts.temperatureControllerRows));
    object.insert(QStringLiteral("waveform_frames"), numberText(counts.waveformFrames));
    object.insert(QStringLiteral("waveform_feature_rows"), numberText(counts.waveformFeatureRows));
    object.insert(QStringLiteral("event_rows"), numberText(counts.eventRows));
    object.insert(QStringLiteral("error_rows"), numberText(counts.errorRows));
    return object;
}

QJsonObject pathsToJson()
{
    const SessionPackageLayout& layout = standardSessionPackageLayout();
    QJsonObject object;
    object.insert(QStringLiteral("sensor_summary_csv"), layout.sensorSummaryCsvPath);
    object.insert(QStringLiteral("temperature_controller_csv"), layout.temperatureControllerCsvPath);
    object.insert(QStringLiteral("waveform_features_csv"), layout.waveformFeaturesCsvPath);
    object.insert(QStringLiteral("navigation_raw"), layout.navigationRawPath);
    object.insert(QStringLiteral("pressure_raw"), layout.pressureRawPath);
    object.insert(QStringLiteral("temperature_humidity_raw"), layout.temperatureHumidityRawPath);
    object.insert(QStringLiteral("distance_raw"), layout.distanceRawPath);
    object.insert(QStringLiteral("waveform_raw"), layout.waveformRawPath);
    object.insert(QStringLiteral("waveform_peaks_csv"), layout.waveformPeaksCsvPath);
    object.insert(QStringLiteral("event_log"), layout.eventLogPath);
    object.insert(QStringLiteral("error_log"), layout.errorLogPath);
    object.insert(QStringLiteral("device_config"), layout.deviceConfigPath);
    object.insert(QStringLiteral("raw_format_document"), layout.rawFormatDocumentPath);
    return object;
}

quint64 rawRecordCountForKey(const RawFileRecordCounts& counts, const QString& key)
{
    if (key == QLatin1String("navigation")) return counts.epsilon;
    if (key == QLatin1String("pressure")) return counts.ptb;
    if (key == QLatin1String("temperature_humidity")) return counts.hmp;
    if (key == QLatin1String("distance")) return counts.lidar;
    if (key == QLatin1String("waveform")) return counts.tcpWave;
    return 0;
}

void setRawRecordCountForKey(RawFileRecordCounts& counts, const QString& key, quint64 value)
{
    if (key == QLatin1String("navigation") || key == QLatin1String("epsilon")) counts.epsilon = value;
    else if (key == QLatin1String("pressure") || key == QLatin1String("ptb")) counts.ptb = value;
    else if (key == QLatin1String("temperature_humidity") || key == QLatin1String("hmp")) counts.hmp = value;
    else if (key == QLatin1String("distance") || key == QLatin1String("lidar")) counts.lidar = value;
    else if (key == QLatin1String("waveform") || key == QLatin1String("tcp_wave")) counts.tcpWave = value;
}

QJsonObject rawFileObjectCompat(const QJsonObject& rawFiles,
                                const RawFileDefinition& definition)
{
    QJsonObject raw = rawFiles.value(definition.key).toObject();
    if (!raw.isEmpty())
    {
        return raw;
    }
    const SessionPathAliases& aliases = sessionPathAliases(definition.kind);
    for (const QString& legacyKey : aliases.manifestRawFileKeys)
    {
        if (legacyKey == definition.key)
        {
            continue;
        }
        raw = rawFiles.value(legacyKey).toObject();
        if (!raw.isEmpty())
        {
            return raw;
        }
    }
    return {};
}

QJsonObject rawFilesToJson(const RawFileRecordCounts& counts, int rawDatFormatVersion)
{
    QJsonObject object;
    for (const RawFileDefinition& definition : standardRawFileDefinitions())
    {
        QJsonObject raw;
        raw.insert(QStringLiteral("path"), definition.relativePath);
        raw.insert(QStringLiteral("source_id"), static_cast<int>(definition.sourceId));
        raw.insert(QStringLiteral("format_version"), rawDatFormatVersion);
        raw.insert(QStringLiteral("records"), numberText(rawRecordCountForKey(counts, definition.key)));
        object.insert(definition.key, raw);
    }
    return object;
}

RecordingOrigin parseOriginCompat(const QJsonObject& json)
{
    const auto parsed = recordingOriginFromString(json.value(QStringLiteral("recording_origin")).toString());
    if (parsed)
    {
        return *parsed;
    }

    const QString legacyMode = json.value(QStringLiteral("mode")).toString().trimmed().toLower();
    if (legacyMode == QLatin1String("sky"))
    {
        return RecordingOrigin::Sky;
    }
    return RecordingOrigin::Ground;
}

quint64 countFromObjects(const QJsonObject& counts, const QJsonObject& root, const QString& key, quint64 fallback = 0)
{
    if (counts.contains(key))
    {
        return unsignedFromJson(counts.value(key), fallback);
    }
    return unsignedFromJson(root.value(key), fallback);
}

}  // namespace

QString sessionStateToString(SessionState state)
{
    switch (state)
    {
    case SessionState::Recording:
        return QStringLiteral("recording");
    case SessionState::Complete:
        return QStringLiteral("complete");
    case SessionState::Recovered:
        return QStringLiteral("recovered");
    }
    return QStringLiteral("recording");
}

SessionState sessionStateFromString(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QLatin1String("complete"))
    {
        return SessionState::Complete;
    }
    if (normalized == QLatin1String("recovered"))
    {
        return SessionState::Recovered;
    }
    return SessionState::Recording;
}

QJsonObject sessionManifestToJson(const SessionManifest& manifest)
{
    QJsonObject root;
    root.insert(QStringLiteral("session_format"), QStringLiteral("vaporview.session"));
    root.insert(QStringLiteral("session_format_version"), 1);
    root.insert(QStringLiteral("recording_origin"), recordingOriginToString(manifest.recordingOrigin));
    root.insert(QStringLiteral("session_name"), manifest.sessionName);
    root.insert(QStringLiteral("state"), sessionStateToString(manifest.state));
    root.insert(QStringLiteral("start_time_utc"), manifest.startTimeUtc);
    root.insert(QStringLiteral("start_time_us"), numberText(manifest.startTimeUs));
    root.insert(QStringLiteral("end_time_utc"), optionalString(manifest.endTimeUtc));
    root.insert(QStringLiteral("end_time_us"), manifest.endTimeUs == 0
        ? QJsonValue(QJsonValue::Null)
        : QJsonValue(numberText(manifest.endTimeUs)));
    root.insert(QStringLiteral("elapsed_ms"), numberText(manifest.elapsedMs));
    root.insert(QStringLiteral("software_version"), manifest.softwareVersion);
    root.insert(QStringLiteral("timestamp_unit"), manifest.timestampUnit);
    root.insert(QStringLiteral("raw_dat_format_version"), manifest.rawDatFormatVersion);
    root.insert(QStringLiteral("epsilon_schema_version"), manifest.epsilonSchemaVersion);
    root.insert(QStringLiteral("sensor_export_rate_hz"), manifest.sensorExportRateHz);
    root.insert(QStringLiteral("other_devices_export_rate_hz"), manifest.otherDevicesExportRateHz);
    root.insert(QStringLiteral("raw_export_mode"), manifest.rawExportMode);
    root.insert(QStringLiteral("waveform_export_rate_hz"), manifest.waveformExportRateHz);
    root.insert(QStringLiteral("waveform_export_mode"), manifest.waveformExportMode);
    root.insert(QStringLiteral("waveform_value_type"), manifest.waveformValueType);
    root.insert(QStringLiteral("waveform_timestamp_type"), manifest.waveformTimestampType);
    root.insert(QStringLiteral("waveform_points_per_frame"), manifest.waveformPointsPerFrame);
    root.insert(QStringLiteral("waveform_file_count"), numberText(manifest.waveformFileCount));
    root.insert(QStringLiteral("capture"), captureToJson(manifest.capture));
    root.insert(QStringLiteral("counts"), countsToJson(manifest.counts));
    root.insert(QStringLiteral("paths"), pathsToJson());
    root.insert(QStringLiteral("raw_files"), rawFilesToJson(manifest.rawRecords, manifest.rawDatFormatVersion));
    return root;
}

SessionManifestParseResult sessionManifestFromJson(const QJsonObject& json)
{
    SessionManifestParseResult result;
    if (json.isEmpty())
    {
        result.error = QStringLiteral("empty session manifest");
        return result;
    }

    SessionManifest manifest;
    if (json.contains(QStringLiteral("recording_origin")))
    {
        const QString originText = json.value(QStringLiteral("recording_origin")).toString();
        const auto parsedOrigin = recordingOriginFromString(originText);
        if (!parsedOrigin)
        {
            result.error = QStringLiteral("invalid recording_origin: %1").arg(originText);
            return result;
        }
        manifest.recordingOrigin = *parsedOrigin;
    }
    else
    {
        manifest.recordingOrigin = parseOriginCompat(json);
    }
    manifest.sessionName = json.value(QStringLiteral("session_name")).toString();
    manifest.state = sessionStateFromString(json.value(QStringLiteral("state")).toString(
        json.value(QStringLiteral("end_time_utc")).toString().isEmpty()
            ? QStringLiteral("recording")
            : QStringLiteral("complete")));
    manifest.startTimeUtc = json.value(QStringLiteral("start_time_utc")).toString();
    manifest.endTimeUtc = json.value(QStringLiteral("end_time_utc")).toString();
    manifest.startTimeUs = unsignedFromJson(json.value(QStringLiteral("start_time_us")));
    manifest.endTimeUs = unsignedFromJson(json.value(QStringLiteral("end_time_us")));
    manifest.elapsedMs = unsignedFromJson(json.value(QStringLiteral("elapsed_ms")));
    manifest.softwareVersion = json.value(QStringLiteral("software_version")).toString(QStringLiteral("dev"));
    manifest.timestampUnit = json.value(QStringLiteral("timestamp_unit")).toString(QStringLiteral("microseconds"));
    manifest.rawDatFormatVersion = intFromJson(
        json.value(QStringLiteral("raw_dat_format_version")),
        static_cast<int>(SessionRawDat::kCurrentFormatVersion));
    manifest.epsilonSchemaVersion = intFromJson(json.value(QStringLiteral("epsilon_schema_version")), 1);
    manifest.sensorExportRateHz = intFromJson(json.value(QStringLiteral("sensor_export_rate_hz")), 10);
    manifest.otherDevicesExportRateHz = intFromJson(json.value(QStringLiteral("other_devices_export_rate_hz")),
                                                    manifest.sensorExportRateHz);
    manifest.rawExportMode = json.value(QStringLiteral("raw_export_mode")).toString(QStringLiteral("unified_raw_dat"));
    manifest.waveformExportRateHz = intFromJson(json.value(QStringLiteral("waveform_export_rate_hz")), 10);
    manifest.waveformExportMode = json.value(QStringLiteral("waveform_export_mode"))
        .toString(manifest.waveformExportRateHz > 0 ? QStringLiteral("fixed_rate") : QStringLiteral("per_frame"));
    manifest.waveformValueType = json.value(QStringLiteral("waveform_value_type")).toString(QStringLiteral("float32"));
    manifest.waveformTimestampType = json.value(QStringLiteral("waveform_timestamp_type")).toString(QStringLiteral("uint64"));
    manifest.waveformPointsPerFrame = intFromJson(json.value(QStringLiteral("waveform_points_per_frame")), 50000);
    manifest.waveformFileCount = unsignedFromJson(json.value(QStringLiteral("waveform_file_count")));

    const QJsonObject capture = json.value(QStringLiteral("capture")).toObject();
    manifest.capture.telemetryTransport = capture.value(QStringLiteral("telemetry_transport")).toString(
        json.value(QStringLiteral("telemetry_transport")).toString());
    manifest.capture.telemetryEndpoint = capture.value(QStringLiteral("telemetry_endpoint")).toString(
        json.value(QStringLiteral("telemetry_endpoint")).toString());
    manifest.capture.telemetryPort = capture.value(QStringLiteral("telemetry_port")).toString(
        json.value(QStringLiteral("telemetry_port")).toString());
    manifest.capture.telemetryBaud = capture.value(QStringLiteral("telemetry_baud")).toString(
        json.value(QStringLiteral("telemetry_baud")).toString());

    const QJsonObject counts = json.value(QStringLiteral("counts")).toObject();
    manifest.counts.sensorRows = countFromObjects(counts, json, QStringLiteral("sensor_rows"));
    manifest.counts.temperatureControllerRows =
        countFromObjects(counts, json, QStringLiteral("temperature_controller_rows"));
    manifest.counts.waveformFrames = countFromObjects(counts, json, QStringLiteral("waveform_frames"));
    manifest.counts.waveformFeatureRows = counts.contains(QStringLiteral("waveform_feature_rows"))
        ? unsignedFromJson(counts.value(QStringLiteral("waveform_feature_rows")))
        : unsignedFromJson(json.value(QStringLiteral("waveform_features")));
    manifest.counts.eventRows = countFromObjects(counts, json, QStringLiteral("event_rows"));
    manifest.counts.errorRows = countFromObjects(counts, json, QStringLiteral("error_rows"));

    const QJsonObject rawFiles = json.value(QStringLiteral("raw_files")).toObject();
    for (const RawFileDefinition& definition : standardRawFileDefinitions())
    {
        const QJsonObject raw = rawFileObjectCompat(rawFiles, definition);
        const quint64 count = raw.contains(QStringLiteral("records"))
            ? unsignedFromJson(raw.value(QStringLiteral("records")))
            : unsignedFromJson(raw.value(QStringLiteral("record_count")));
        setRawRecordCountForKey(manifest.rawRecords, definition.key, count);
    }

    result.manifest = manifest;
    result.success = true;
    return result;
}

bool writeSessionManifestAtomically(const QString& filename,
                                    const SessionManifest& manifest,
                                    QString *errorMessage)
{
    QSaveFile file(filename);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }

    const QByteArray payload = QJsonDocument(sessionManifestToJson(manifest)).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size())
    {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }

    if (!file.commit())
    {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    return true;
}

}  // namespace VaporView::Session
