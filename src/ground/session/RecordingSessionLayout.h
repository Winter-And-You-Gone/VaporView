#pragma once

#include <QString>

#include <optional>

namespace VaporView::Ground::Session
{

struct RecordingSessionLayout
{
    QString sessionName;
    QString sessionDirectory;
    QString sensorSummaryFilename;
    QString laserTemperatureControllerFilename;
    QString systemTemperatureControllerFilename;
    QString waveformFeaturesFilename;
    QString navigationRawFilename;
    QString pressureRawFilename;
    QString temperatureHumidityRawFilename;
    QString distanceRawFilename;
    QString waveformRawFilename;
    QString laserTemperatureControllerRawFilename;
    QString systemTemperatureControllerRawFilename;
    QString waveformPeaksFilename;
    QString rawDatDocumentFilename;
    QString sessionMetadataFilename;
    QString eventLogFilename;
    QString errorLogFilename;
};

std::optional<RecordingSessionLayout> createRecordingSessionLayout(
    const QString& recordsPath,
    const QString& sessionName);

}  // namespace VaporView::Ground::Session
