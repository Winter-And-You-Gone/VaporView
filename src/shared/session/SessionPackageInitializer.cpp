#include "shared/session/SessionPackageInitializer.h"

#include "shared/session/SessionDeviceConfig.h"
#include "shared/session/SessionSensorCsv.h"
#include "shared/session/UnifiedRawDat.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringConverter>
#include <QTextStream>

namespace VaporView::Session
{
namespace
{

bool writeTextFile(const QString& filename, const QString& text, QString *error)
{
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        if (error) *error = file.errorString();
        return false;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << text;
    stream.flush();
    if (file.error() != QFileDevice::NoError)
    {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

bool writeRawFileHeader(const QString& filename, quint16 sourceId, QString *error)
{
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (error) *error = file.errorString();
        return false;
    }
    return SessionRawDat::writeFileHeader(file, sourceId, error);
}

SessionManifest makeInitialManifest(const SessionPackageInitOptions& options,
                                    const QString& finalSessionName)
{
    SessionManifest manifest;
    manifest.recordingOrigin = options.origin;
    manifest.sessionName = finalSessionName;
    manifest.state = SessionState::Recording;
    manifest.startTimeUtc = options.startTimeUtc;
    manifest.startTimeUs = options.startTimeUs;
    manifest.softwareVersion = options.softwareVersion.isEmpty() ? QStringLiteral("dev") : options.softwareVersion;
    manifest.rawDatFormatVersion = static_cast<int>(SessionRawDat::kCurrentFormatVersion);
    manifest.sensorExportRateHz = options.sensorExportRateHz;
    manifest.otherDevicesExportRateHz = options.otherDevicesExportRateHz;
    manifest.waveformExportRateHz = options.waveformExportRateHz;
    manifest.waveformPointsPerFrame = options.waveformPointsPerFrame;
    manifest.capture = options.capture;
    return manifest;
}

}  // namespace

SessionPackageInitResult initializeSessionPackage(const SessionPackageInitOptions& options)
{
    SessionPackageInitResult result;
    result.layout = standardSessionPackageLayout();

    if (options.outputDirectory.trimmed().isEmpty() || options.sessionName.trimmed().isEmpty())
    {
        result.error = QStringLiteral("session output directory or name is empty");
        return result;
    }

    QDir recordsDir(options.outputDirectory);
    if (!recordsDir.exists() && !recordsDir.mkpath(QStringLiteral(".")))
    {
        result.error = QStringLiteral("cannot create recording directory: %1").arg(options.outputDirectory);
        return result;
    }

    QString finalSessionName = options.sessionName;
    QString finalSessionDirectory = recordsDir.filePath(finalSessionName);
    int suffix = 1;
    while (QFileInfo::exists(finalSessionDirectory))
    {
        finalSessionName = QStringLiteral("%1_%2").arg(options.sessionName).arg(suffix++);
        finalSessionDirectory = recordsDir.filePath(finalSessionName);
    }

    if (!recordsDir.mkpath(finalSessionName))
    {
        result.error = QStringLiteral("cannot create session directory: %1").arg(finalSessionName);
        return result;
    }

    const QString normalizedSessionDirectory = QDir::fromNativeSeparators(finalSessionDirectory);
    const auto fail = [&result, &normalizedSessionDirectory](const QString& message) {
        QDir(normalizedSessionDirectory).removeRecursively();
        result.error = message;
        return result;
    };

    QDir sessionDir(normalizedSessionDirectory);
    for (const QString& relativeDirectory : standardSessionDirectories())
    {
        if (!sessionDir.mkpath(relativeDirectory))
        {
            return fail(QStringLiteral("cannot create session subdirectory: %1").arg(relativeDirectory));
        }
    }

    result.manifest = makeInitialManifest(options, finalSessionName);
    SessionDeviceConfig deviceConfig = options.deviceConfig;
    deviceConfig.recordingOrigin = options.origin;
    deviceConfig.recordingDirectory = QDir::fromNativeSeparators(options.outputDirectory);
    deviceConfig.sessionDirectory = normalizedSessionDirectory;
    deviceConfig.epsilonSchemaVersion = result.manifest.epsilonSchemaVersion;
    deviceConfig.sensorExportRateHz = result.manifest.sensorExportRateHz;
    deviceConfig.otherDevicesExportRateHz = result.manifest.otherDevicesExportRateHz;
    deviceConfig.rawExportMode = result.manifest.rawExportMode;
    deviceConfig.rawDatFormatVersion = result.manifest.rawDatFormatVersion;
    deviceConfig.waveformExportRateHz = result.manifest.waveformExportRateHz;
    deviceConfig.waveformExportMode = result.manifest.waveformExportMode;
    deviceConfig.waveformValueType = result.manifest.waveformValueType;
    deviceConfig.waveformTimestampType = result.manifest.waveformTimestampType;
    deviceConfig.waveformPointsPerFrame = result.manifest.waveformPointsPerFrame;
    deviceConfig.telemetry = result.manifest.capture;

    QString error;
    if (!writeTextFile(sessionPackageFilePath(normalizedSessionDirectory, result.layout.devicesCsvPath),
                       SessionSensorCsv::header(),
                       &error))
    {
        return fail(QStringLiteral("cannot create devices.csv: %1").arg(error));
    }
    if (!writeTextFile(sessionPackageFilePath(normalizedSessionDirectory, result.layout.temperatureControllerCsvPath),
                       temperatureControllerCsvHeader(),
                       &error))
    {
        return fail(QStringLiteral("cannot create rd105_temperature_controller.csv: %1").arg(error));
    }
    if (!writeTextFile(sessionPackageFilePath(normalizedSessionDirectory, result.layout.waveformFeaturesCsvPath),
                       waveformFeaturesCsvHeader(),
                       &error))
    {
        return fail(QStringLiteral("cannot create waveform_features.csv: %1").arg(error));
    }
    if (!writeTextFile(sessionPackageFilePath(normalizedSessionDirectory, result.layout.tcpWavePeaksCsvPath),
                       tcpWavePeaksCsvHeader(),
                       &error))
    {
        return fail(QStringLiteral("cannot create tcp_wave_peaks.csv: %1").arg(error));
    }
    if (!writeTextFile(sessionPackageFilePath(normalizedSessionDirectory, result.layout.eventLogPath),
                       eventLogCsvHeader(),
                       &error))
    {
        return fail(QStringLiteral("cannot create event_log.csv: %1").arg(error));
    }
    if (!writeTextFile(sessionPackageFilePath(normalizedSessionDirectory, result.layout.errorLogPath),
                       QString(),
                       &error))
    {
        return fail(QStringLiteral("cannot create error_log.txt: %1").arg(error));
    }
    if (!writeSessionDeviceConfigAtomically(
            sessionPackageFilePath(normalizedSessionDirectory, result.layout.deviceConfigPath),
            deviceConfig,
            &error))
    {
        return fail(QStringLiteral("cannot create device_config.json: %1").arg(error));
    }
    if (!writeTextFile(sessionPackageFilePath(normalizedSessionDirectory, result.layout.rawFormatDocumentPath),
                       rawDatFormatDocumentText(),
                       &error))
    {
        return fail(QStringLiteral("cannot create raw_dat_format.md: %1").arg(error));
    }

    for (const RawFileDefinition& definition : standardRawFileDefinitions())
    {
        if (!writeRawFileHeader(sessionPackageFilePath(normalizedSessionDirectory, definition.relativePath),
                                definition.sourceId,
                                &error))
        {
            return fail(QStringLiteral("cannot create RAW DAT file %1: %2").arg(definition.relativePath, error));
        }
    }

    if (!writeSessionManifestAtomically(sessionPackageFilePath(normalizedSessionDirectory, result.layout.manifestPath),
                                        result.manifest,
                                        &error))
    {
        return fail(QStringLiteral("cannot create session.json: %1").arg(error));
    }

    result.success = true;
    result.sessionName = finalSessionName;
    result.sessionDirectory = normalizedSessionDirectory;
    return result;
}

}  // namespace VaporView::Session
