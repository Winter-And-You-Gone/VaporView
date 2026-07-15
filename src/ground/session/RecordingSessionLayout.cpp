#include "ground/session/RecordingSessionLayout.h"

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
    if (!recordsDir.mkpath(finalSessionName) ||
        !sessionDir.mkpath(QStringLiteral("sensors")) ||
        !sessionDir.mkpath(QStringLiteral("raw")) ||
        !sessionDir.mkpath(QStringLiteral("logs")) ||
        !sessionDir.mkpath(QStringLiteral("config")))
    {
        return std::nullopt;
    }

    RecordingSessionLayout layout;
    layout.sessionName = finalSessionName;
    layout.sessionDirectory = QDir::fromNativeSeparators(finalSessionDirectory);
    layout.sensorsFilename = QDir::fromNativeSeparators(sessionDir.filePath(QStringLiteral("sensors/devices.csv")));
    layout.rawEpsilonFilename = QDir::fromNativeSeparators(sessionDir.filePath(QStringLiteral("raw/epsilon.dat")));
    layout.rawPtbFilename = QDir::fromNativeSeparators(sessionDir.filePath(QStringLiteral("raw/ptb.dat")));
    layout.rawHmpFilename = QDir::fromNativeSeparators(sessionDir.filePath(QStringLiteral("raw/hmp.dat")));
    layout.rawLidarFilename = QDir::fromNativeSeparators(sessionDir.filePath(QStringLiteral("raw/lidar.dat")));
    layout.rawTcpWaveFilename = QDir::fromNativeSeparators(sessionDir.filePath(QStringLiteral("raw/tcp_wave.dat")));
    layout.rawTcpWavePeakIndexFilename =
        QDir::fromNativeSeparators(sessionDir.filePath(QStringLiteral("raw/tcp_wave_peaks.csv")));
    layout.rawDatDocumentFilename = QDir::fromNativeSeparators(sessionDir.filePath(QStringLiteral("raw_dat_format.md")));
    layout.sessionMetadataFilename = QDir::fromNativeSeparators(sessionDir.filePath(QStringLiteral("session.json")));
    layout.eventLogFilename = QDir::fromNativeSeparators(sessionDir.filePath(QStringLiteral("logs/event_log.csv")));
    layout.errorLogFilename = QDir::fromNativeSeparators(sessionDir.filePath(QStringLiteral("logs/error_log.txt")));
    layout.deviceConfigFilename =
        QDir::fromNativeSeparators(sessionDir.filePath(QStringLiteral("config/device_config.json")));
    return layout;
}

}  // namespace VaporView::Ground::Session
