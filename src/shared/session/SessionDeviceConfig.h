#pragma once

#include "shared/session/SessionManifest.h"

#include <QJsonObject>
#include <QString>

namespace VaporView::Session
{

struct DeviceConnectionConfig
{
    QString port;
    QString baud;
    QString rateHz;
};

struct SessionDeviceConfig
{
    RecordingOrigin recordingOrigin = RecordingOrigin::Ground;
    QString recordingDirectory;
    QString sessionDirectory;
    int epsilonSchemaVersion = 1;
    int sensorExportRateHz = 0;
    int otherDevicesExportRateHz = 0;
    QString rawExportMode = QStringLiteral("unified_raw_dat");
    int rawDatFormatVersion = 2;
    int waveformExportRateHz = 0;
    QString waveformExportMode = QStringLiteral("per_frame");
    QString waveformValueType = QStringLiteral("float32");
    QString waveformTimestampType = QStringLiteral("uint64");
    int waveformPointsPerFrame = 0;
    CaptureMetadata telemetry;
    QString waveformHost;
    int waveformPort = 0;
    DeviceConnectionConfig epsilon;
    DeviceConnectionConfig ptb;
    DeviceConnectionConfig hmp;
    DeviceConnectionConfig lidar;
    DeviceConnectionConfig temperatureController;
};

QJsonObject sessionDeviceConfigToJson(const SessionDeviceConfig& config);
bool writeSessionDeviceConfigAtomically(const QString& filename,
                                        const SessionDeviceConfig& config,
                                        QString *errorMessage = nullptr);

}  // namespace VaporView::Session
