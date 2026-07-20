#pragma once

#include "shared/session/SessionManifest.h"

#include <QJsonObject>
#include <QString>

namespace VaporView::Session
{

struct SessionPackageInitOptions
{
    RecordingOrigin origin = RecordingOrigin::Ground;
    QString sessionName;
    QString outputDirectory;
    QString softwareVersion = QStringLiteral("dev");
    QString startTimeUtc;
    quint64 startTimeUs = 0;
    int sensorExportRateHz = 0;
    int otherDevicesExportRateHz = 0;
    int waveformExportRateHz = 0;
    int waveformPointsPerFrame = 50000;
    CaptureMetadata capture;
    QJsonObject initialDeviceConfig;
};

struct SessionPackageInitResult
{
    bool success = false;
    QString error;
    QString sessionName;
    QString sessionDirectory;
    SessionPackageLayout layout;
    SessionManifest manifest;
};

SessionPackageInitResult initializeSessionPackage(const SessionPackageInitOptions& options);

}  // namespace VaporView::Session
