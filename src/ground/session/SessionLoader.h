#pragma once

#include "ground/SessionData.h"
#include "shared/session/RecordingOrigin.h"

#include <QString>
#include <QtGlobal>

#include <functional>

namespace VaporView::Ground
{

struct SessionMetadata
{
    QString sessionDirectory;
    QString metadataFilename;
    VaporView::Session::RecordingOrigin recordingOrigin = VaporView::Session::RecordingOrigin::Ground;
    QString sessionName;
    QString startTimeUtc;
    QString endTimeUtc;
    QString sensorSummaryCsvFilename;
    QString waveformDirectory;
    QString waveformIndexFilename;
    QString waveformPeaksCsvFilename;
    QString waveformRawFilename;
    quint64 sensorRows = 0;
    quint64 waveformFrames = 0;
    int waveformPointsPerFrame = 50000;
    int sensorExportRateHz = 10;
    int waveformExportRateHz = 10;
    QString waveformExportMode;
};

struct SessionMetadataLoadResult
{
    bool success = false;
    QString error;
    QString warning;
    SessionMetadata metadata;
};

struct SessionSensorLoadResult
{
    bool success = false;
    bool fileAvailable = false;
    QString warning;
    SessionSensorData data;
};

class SessionLoader final
{
public:
    static QString resolveSessionDirectory(const QString& path);
    static SessionMetadataLoadResult loadMetadata(const QString& sessionDirectory);
    static SessionSensorLoadResult loadSensors(
        const SessionMetadata& metadata,
        const std::function<void(quint64 rowsRead, quint64 expectedRows)>& progress = {});
};

}  // namespace VaporView::Ground
