#include "ground/session/RecordingSessionLayout.h"

#include "shared/session/SessionPackageLayout.h"

#include <QDir>
#include <QFileInfo>

namespace VaporView::Ground::Session
{

std::optional<RecordingSessionLayout> createRecordingSessionLayout(
    const QString& recordsPath,
    const QString& sessionName)
{
    QDir recordsDir(recordsPath);
    if (!recordsDir.exists() && !recordsDir.mkpath(QStringLiteral(".")))
    {
        return std::nullopt;
    }

    QString finalSessionName = sessionName;
    QString finalSessionDirectory = recordsDir.filePath(finalSessionName);
    int suffix = 1;
    while (QFileInfo::exists(finalSessionDirectory))
    {
        finalSessionName = QStringLiteral("%1_%2").arg(sessionName).arg(suffix++);
        finalSessionDirectory = recordsDir.filePath(finalSessionName);
    }

    QDir sessionDir(finalSessionDirectory);
    if (!recordsDir.mkpath(finalSessionName))
    {
        return std::nullopt;
    }
    for (const QString& relativeDirectory : VaporView::Session::standardSessionDirectories())
    {
        if (!sessionDir.mkpath(relativeDirectory))
        {
            return std::nullopt;
        }
    }

    const VaporView::Session::SessionPackageLayout& packageLayout =
        VaporView::Session::standardSessionPackageLayout();

    RecordingSessionLayout layout;
    layout.sessionName = finalSessionName;
    layout.sessionDirectory = QDir::fromNativeSeparators(finalSessionDirectory);
    layout.sensorSummaryFilename = VaporView::Session::sessionPackageFilePath(finalSessionDirectory, packageLayout.sensorSummaryCsvPath);
    layout.temperatureControllerFilename = VaporView::Session::sessionPackageFilePath(finalSessionDirectory, packageLayout.temperatureControllerCsvPath);
    layout.waveformFeaturesFilename = VaporView::Session::sessionPackageFilePath(finalSessionDirectory, packageLayout.waveformFeaturesCsvPath);
    layout.navigationRawFilename = VaporView::Session::sessionPackageFilePath(finalSessionDirectory, packageLayout.navigationRawPath);
    layout.pressureRawFilename = VaporView::Session::sessionPackageFilePath(finalSessionDirectory, packageLayout.pressureRawPath);
    layout.temperatureHumidityRawFilename = VaporView::Session::sessionPackageFilePath(finalSessionDirectory, packageLayout.temperatureHumidityRawPath);
    layout.distanceRawFilename = VaporView::Session::sessionPackageFilePath(finalSessionDirectory, packageLayout.distanceRawPath);
    layout.waveformRawFilename = VaporView::Session::sessionPackageFilePath(finalSessionDirectory, packageLayout.waveformRawPath);
    layout.waveformPeaksFilename = VaporView::Session::sessionPackageFilePath(finalSessionDirectory, packageLayout.waveformPeaksCsvPath);
    layout.rawDatDocumentFilename = VaporView::Session::sessionPackageFilePath(finalSessionDirectory, packageLayout.rawFormatDocumentPath);
    layout.sessionMetadataFilename = VaporView::Session::sessionPackageFilePath(finalSessionDirectory, packageLayout.manifestPath);
    layout.eventLogFilename = VaporView::Session::sessionPackageFilePath(finalSessionDirectory, packageLayout.eventLogPath);
    layout.errorLogFilename = VaporView::Session::sessionPackageFilePath(finalSessionDirectory, packageLayout.errorLogPath);
    return layout;
}

}  // namespace VaporView::Ground::Session
