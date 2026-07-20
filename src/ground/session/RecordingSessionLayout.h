#pragma once

#include <QString>

#include <optional>

namespace VaporView::Ground::Session
{

struct RecordingSessionLayout
{
    QString sessionName;
    QString sessionDirectory;
    QString sensorsFilename;
    QString temperatureControllerFilename;
    QString waveformFeaturesFilename;
    QString rawEpsilonFilename;
    QString rawPtbFilename;
    QString rawHmpFilename;
    QString rawLidarFilename;
    QString rawTcpWaveFilename;
    QString rawTcpWavePeakIndexFilename;
    QString rawDatDocumentFilename;
    QString sessionMetadataFilename;
    QString eventLogFilename;
    QString errorLogFilename;
};

std::optional<RecordingSessionLayout> createRecordingSessionLayout(
    const QString& recordsPath,
    const QString& sessionName);

}  // namespace VaporView::Ground::Session
