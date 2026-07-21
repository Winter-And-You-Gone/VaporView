#pragma once

#include "shared/session/RecordingOrigin.h"
#include "shared/session/SessionPackageLayout.h"

#include <QJsonObject>
#include <QString>
#include <QtGlobal>

namespace VaporView::Session
{

enum class SessionState
{
    Recording,
    Complete,
    Recovered
};

QString sessionStateToString(SessionState state);
SessionState sessionStateFromString(const QString& value);

struct CaptureMetadata
{
    QString telemetryTransport;
    QString telemetryEndpoint;
    QString telemetryPort;
    QString telemetryBaud;
};

struct SessionRecordCounts
{
    quint64 sensorRows = 0;
    quint64 temperatureControllerRows = 0;
    quint64 waveformFrames = 0;
    quint64 waveformFeatureRows = 0;
    quint64 eventRows = 0;
    quint64 errorRows = 0;
};

struct RawFileRecordCounts
{
    quint64 navigation = 0;
    quint64 pressure = 0;
    quint64 temperatureHumidity = 0;
    quint64 distance = 0;
    quint64 waveform = 0;
};

struct SessionManifest
{
    RecordingOrigin recordingOrigin = RecordingOrigin::Ground;
    QString sessionName;
    SessionState state = SessionState::Recording;
    QString startTimeUtc;
    QString endTimeUtc;
    quint64 startTimeUs = 0;
    quint64 endTimeUs = 0;
    quint64 elapsedMs = 0;
    QString softwareVersion = QStringLiteral("dev");
    QString timestampUnit = QStringLiteral("microseconds");
    int rawDatFormatVersion = 2;
    int epsilonSchemaVersion = 1;
    int sensorExportRateHz = 0;
    int otherDevicesExportRateHz = 0;
    int waveformExportRateHz = 0;
    int waveformPointsPerFrame = 50000;
    QString rawExportMode = QStringLiteral("unified_raw_dat");
    QString waveformExportMode = QStringLiteral("per_frame");
    QString waveformValueType = QStringLiteral("float32");
    QString waveformTimestampType = QStringLiteral("uint64");
    quint64 waveformFileCount = 0;
    CaptureMetadata capture;
    SessionRecordCounts counts;
    RawFileRecordCounts rawRecords;
};

struct SessionManifestParseResult
{
    bool success = false;
    QString error;
    SessionManifest manifest;
};

QJsonObject sessionManifestToJson(const SessionManifest& manifest);
SessionManifestParseResult sessionManifestFromJson(const QJsonObject& json);
bool writeSessionManifestAtomically(const QString& filename,
                                    const SessionManifest& manifest,
                                    QString *errorMessage = nullptr);

}  // namespace VaporView::Session
