#include "shared/session/SessionDeviceConfig.h"

#include "shared/session/SessionPackageLayout.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <QSaveFile>

namespace VaporView::Session
{
namespace
{

QJsonValue optionalString(const QString& value)
{
    return value.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(value);
}

QJsonObject connectionToJson(const DeviceConnectionConfig& config)
{
    QJsonObject object;
    object.insert(QStringLiteral("port"), optionalString(config.port));
    object.insert(QStringLiteral("baud"), optionalString(config.baud));
    object.insert(QStringLiteral("rate_hz"), optionalString(config.rateHz));
    return object;
}

QJsonObject telemetryToJson(const CaptureMetadata& telemetry)
{
    QJsonObject object;
    object.insert(QStringLiteral("transport"), optionalString(telemetry.telemetryTransport));
    object.insert(QStringLiteral("endpoint"), optionalString(telemetry.telemetryEndpoint));
    object.insert(QStringLiteral("port"), optionalString(telemetry.telemetryPort));
    object.insert(QStringLiteral("baud"), optionalString(telemetry.telemetryBaud));
    return object;
}

}  // namespace

QJsonObject sessionDeviceConfigToJson(const SessionDeviceConfig& config)
{
    const SessionPackageLayout& layout = standardSessionPackageLayout();

    QJsonObject waveform;
    waveform.insert(QStringLiteral("host"), optionalString(config.waveformHost));
    waveform.insert(QStringLiteral("port"), config.waveformPort > 0
        ? QJsonValue(config.waveformPort)
        : QJsonValue(QJsonValue::Null));
    waveform.insert(QStringLiteral("frame_rate_hz"), config.waveformExportRateHz);
    waveform.insert(QStringLiteral("frame_rate_mode"), config.waveformExportMode);
    waveform.insert(QStringLiteral("points_per_frame"), config.waveformPointsPerFrame);
    waveform.insert(QStringLiteral("value_type"), config.waveformValueType);
    waveform.insert(QStringLiteral("timestamp_type"), config.waveformTimestampType);

    QJsonObject rawDat;
    rawDat.insert(QStringLiteral("directory"), layout.epsilonRawPath.section(QLatin1Char('/'), 0, 0));
    rawDat.insert(QStringLiteral("format_doc"), layout.rawFormatDocumentPath);
    rawDat.insert(QStringLiteral("mode"), QStringLiteral("per_verified_raw_frame_or_response"));

    QJsonObject sensors;
    sensors.insert(QStringLiteral("epsilon"), connectionToJson(config.epsilon));
    sensors.insert(QStringLiteral("ptb"), connectionToJson(config.ptb));
    sensors.insert(QStringLiteral("hmp"), connectionToJson(config.hmp));
    sensors.insert(QStringLiteral("lidar"), connectionToJson(config.lidar));
    sensors.insert(QStringLiteral("rd105"), connectionToJson(config.temperatureController));

    QJsonObject root;
    root.insert(QStringLiteral("device_config_format"), QStringLiteral("vaporview.device_config"));
    root.insert(QStringLiteral("device_config_format_version"), 1);
    root.insert(QStringLiteral("recording_origin"), recordingOriginToString(config.recordingOrigin));
    root.insert(QStringLiteral("recording_directory"), optionalString(config.recordingDirectory));
    root.insert(QStringLiteral("session_directory"), optionalString(config.sessionDirectory));
    root.insert(QStringLiteral("epsilon_schema_version"), config.epsilonSchemaVersion);
    root.insert(QStringLiteral("sensor_export_rate_hz"), config.sensorExportRateHz);
    root.insert(QStringLiteral("other_devices_export_rate_hz"), config.otherDevicesExportRateHz);
    root.insert(QStringLiteral("raw_export_mode"), config.rawExportMode);
    root.insert(QStringLiteral("raw_dat_format_version"), config.rawDatFormatVersion);
    root.insert(QStringLiteral("waveform_export_rate_hz"), config.waveformExportRateHz);
    root.insert(QStringLiteral("waveform_export_mode"), config.waveformExportMode);
    root.insert(QStringLiteral("telemetry"), telemetryToJson(config.telemetry));
    root.insert(QStringLiteral("waveform"), waveform);
    root.insert(QStringLiteral("raw_dat"), rawDat);
    root.insert(QStringLiteral("sensors"), sensors);
    return root;
}

bool writeSessionDeviceConfigAtomically(const QString& filename,
                                        const SessionDeviceConfig& config,
                                        QString *errorMessage)
{
    QSaveFile file(filename);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }

    const QByteArray payload = QJsonDocument(sessionDeviceConfigToJson(config)).toJson(QJsonDocument::Indented);
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
