#include "ground/session/SessionExportService.h"
#include "shared/config/SettingsWriteBarrier.h"

#include <QDateTime>
#include <QFile>
#include <QStringConverter>
#include <QTextStream>
#include <QTimeZone>

#include <algorithm>

namespace VaporView::Ground
{
namespace
{

QString formatTimestampUs(quint64 timestampUs)
{
    if (timestampUs == 0)
    {
        return QStringLiteral("--");
    }
    return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(timestampUs / 1000), QTimeZone::UTC)
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz 'UTC'"));
}

QString csvCell(QString value)
{
    value.replace('"', QStringLiteral("\"\""));
    if (value.contains(',') || value.contains('"') || value.contains('\n') || value.contains('\r'))
    {
        return QStringLiteral("\"%1\"").arg(value);
    }
    return value;
}

bool timestampInRange(quint64 timestampUs, quint64 startTimestampUs, quint64 endTimestampUs)
{
    return timestampUs >= startTimestampUs && timestampUs <= endTimestampUs;
}

}  // namespace

QString SessionExportService::trajectoryCsvHeader()
{
    return QStringLiteral(
        "index,csv_row,timestamp_utc,timestamp_us,latitude,longitude,height_m,"
        "cumulative_distance_m,segment_distance_m,speed_mps,gnss_fix,peak_value,"
        "waveform_frame,waveform_timestamp_us,waveform_delta_ms");
}

SessionExportResult SessionExportService::exportTrajectoryCsv(
    const QString& filename,
    const QVector<SessionTrackPoint>& points,
    quint64 startTimestampUs,
    quint64 endTimestampUs)
{
    SessionExportResult result;
    if (VaporView::settingsWritesSuspended())
    {
        result.error = QStringLiteral("UI test mode blocks business file export.");
        return result;
    }
    if (filename.trimmed().isEmpty())
    {
        result.error = QStringLiteral("Export filename is empty.");
        return result;
    }
    if (points.isEmpty())
    {
        result.error = QStringLiteral("No trajectory points to export.");
        return result;
    }
    if (startTimestampUs > endTimestampUs)
    {
        result.error = QStringLiteral("The export time range is invalid.");
        return result;
    }

    const qsizetype matchingRows = std::count_if(
        points.cbegin(), points.cend(),
        [startTimestampUs, endTimestampUs](const SessionTrackPoint& point) {
            return timestampInRange(point.timestamp_us, startTimestampUs, endTimestampUs);
        });
    if (matchingRows == 0)
    {
        result.error = QStringLiteral("No trajectory points are inside the export time range.");
        return result;
    }

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        result.error = file.errorString();
        return result;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << trajectoryCsvHeader() << '\n';
    for (int index = 0; index < points.size(); ++index)
    {
        const SessionTrackPoint& point = points.at(index);
        if (!timestampInRange(point.timestamp_us, startTimestampUs, endTimestampUs))
        {
            continue;
        }

        stream << index + 1 << ','
               << (point.csv_row >= 0 ? point.csv_row + 1 : 0) << ','
               << csvCell(formatTimestampUs(point.timestamp_us)) << ','
               << point.timestamp_us << ','
               << QString::number(point.latitude, 'f', 8) << ','
               << QString::number(point.longitude, 'f', 8) << ','
               << (point.has_height ? QString::number(point.height_m, 'f', 3) : QString()) << ','
               << QString::number(point.cumulative_distance_m, 'f', 3) << ','
               << QString::number(point.segment_distance_m, 'f', 3) << ','
               << (point.has_speed ? QString::number(point.speed_mps, 'f', 4) : QString()) << ','
               << csvCell(point.gnss_fix) << ','
               << (point.has_peak_value ? QString::number(point.peak_value, 'f', 6) : QString()) << ','
               << (point.waveform_frame_index >= 0 ? QString::number(point.waveform_frame_index + 1) : QString()) << ','
               << (point.has_waveform_match ? QString::number(point.waveform_timestamp_us) : QString()) << ','
               << (point.has_waveform_match
                       ? QString::number(static_cast<double>(point.waveform_delta_us) / 1000.0, 'f', 3)
                       : QString())
               << '\n';
        ++result.rowsWritten;
    }

    stream.flush();
    if (stream.status() != QTextStream::Ok || file.error() != QFileDevice::NoError)
    {
        result.error = file.errorString();
        if (result.error.isEmpty())
        {
            result.error = QStringLiteral("Failed to write trajectory CSV.");
        }
        return result;
    }
    result.success = true;
    return result;
}

}  // namespace VaporView::Ground
