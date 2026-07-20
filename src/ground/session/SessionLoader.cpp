#include "ground/session/SessionLoader.h"
#include "ground/session/SessionCsv.h"
#include "shared/session/SessionManifest.h"
#include "shared/session/SessionPackageLayout.h"
#include "shared/session/SessionPathResolver.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QStringConverter>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{

double haversineDistanceMeters(double lat1Deg, double lon1Deg, double lat2Deg, double lon2Deg)
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kEarthRadiusM = 6371000.0;
    const auto radians = [kPi](double degrees) { return degrees * kPi / 180.0; };
    const double lat1 = radians(lat1Deg);
    const double lat2 = radians(lat2Deg);
    const double deltaLat = radians(lat2Deg - lat1Deg);
    const double deltaLon = radians(lon2Deg - lon1Deg);
    const double sinLat = std::sin(deltaLat * 0.5);
    const double sinLon = std::sin(deltaLon * 0.5);
    const double value = sinLat * sinLat + std::cos(lat1) * std::cos(lat2) * sinLon * sinLon;
    const double angle = 2.0 * std::atan2(
        std::sqrt(value),
        std::sqrt(std::max(0.0, 1.0 - value)));
    return kEarthRadiusM * angle;
}

}  // namespace

namespace VaporView::Ground
{

QString SessionLoader::resolveSessionDirectory(const QString& path)
{
    if (path.trimmed().isEmpty())
    {
        return {};
    }

    const QFileInfo info(path);
    if (info.isDir())
    {
        return QDir::fromNativeSeparators(info.absoluteFilePath());
    }
    if (info.isFile() && info.fileName().compare(QStringLiteral("session.json"), Qt::CaseInsensitive) == 0)
    {
        return QDir::fromNativeSeparators(info.absolutePath());
    }
    return {};
}

SessionMetadataLoadResult SessionLoader::loadMetadata(const QString& sessionDirectory)
{
    SessionMetadataLoadResult result;
    const QString normalizedDirectory = resolveSessionDirectory(sessionDirectory);
    if (normalizedDirectory.isEmpty())
    {
        result.error = QStringLiteral("Not a session directory: %1").arg(sessionDirectory);
        return result;
    }

    const VaporView::Session::SessionPathContext pathContext =
        VaporView::Session::loadSessionPathContext(normalizedDirectory);
    const QString metadataPath = QDir(normalizedDirectory).filePath(QStringLiteral("session.json"));
    if (!pathContext.manifestError.isEmpty())
    {
        result.error = pathContext.manifestError;
        return result;
    }

    VaporView::Session::SessionManifest manifest;
    if (pathContext.manifestPresent)
    {
        const VaporView::Session::SessionManifestParseResult manifestResult =
            VaporView::Session::sessionManifestFromJson(pathContext.manifest);
        if (!manifestResult.success)
        {
            result.error = QStringLiteral("Invalid session manifest %1: %2")
                               .arg(metadataPath, manifestResult.error);
            return result;
        }
        manifest = manifestResult.manifest;
    }
    else
    {
        result.warning = QStringLiteral("session.json is missing; using compatibility path defaults.");
    }

    SessionMetadata& metadata = result.metadata;
    metadata.sessionDirectory = normalizedDirectory;
    metadata.metadataFilename = metadataPath;
    metadata.recordingOrigin = manifest.recordingOrigin;
    metadata.sessionName = manifest.sessionName.isEmpty()
        ? QFileInfo(normalizedDirectory).fileName()
        : manifest.sessionName;
    metadata.startTimeUtc = manifest.startTimeUtc;
    metadata.endTimeUtc = manifest.endTimeUtc;
    metadata.sensorRows = manifest.counts.sensorRows;
    metadata.waveformFrames = manifest.counts.waveformFrames;
    metadata.waveformPointsPerFrame = manifest.waveformPointsPerFrame;
    metadata.sensorExportRateHz = manifest.sensorExportRateHz;
    metadata.waveformExportRateHz = manifest.waveformExportRateHz;
    metadata.waveformExportMode = manifest.waveformExportMode;

    const QJsonObject& paths = pathContext.manifest;
    const QJsonObject pathObject = paths.value(QStringLiteral("paths")).toObject();
    const auto appendWarning = [&result](const QString& warning) {
        if (warning.trimmed().isEmpty()) return;
        if (!result.warning.isEmpty()) result.warning += QLatin1Char(' ');
        result.warning += warning;
    };
    const auto sensorSummary = VaporView::Session::resolveSessionPath(
        pathContext, VaporView::Session::SessionFileKind::SensorSummaryCsv);
    const auto waveformPeaks = VaporView::Session::resolveSessionPath(
        pathContext, VaporView::Session::SessionFileKind::WaveformPeaksCsv);
    const auto waveformRaw = VaporView::Session::resolveSessionPath(
        pathContext, VaporView::Session::SessionFileKind::WaveformRaw);
    appendWarning(sensorSummary.warning);
    appendWarning(waveformPeaks.warning);
    appendWarning(waveformRaw.warning);

    const QString waveformRelativePath = pathObject.value(QStringLiteral("waveform_directory"))
                                             .toString(QStringLiteral("waveform"));
    const QString waveformIndexRelativePath = pathObject.value(QStringLiteral("waveform_index"))
                                                  .toString(QStringLiteral("waveform_index.csv"));

    const QDir sessionDir(normalizedDirectory);
    metadata.sensorSummaryCsvFilename = sensorSummary.absolutePath;
    metadata.waveformDirectory = sessionDir.filePath(waveformRelativePath);
    metadata.waveformIndexFilename = sessionDir.filePath(waveformIndexRelativePath);
    metadata.waveformPeaksCsvFilename = waveformPeaks.absolutePath;
    metadata.waveformRawFilename = waveformRaw.absolutePath;
    result.success = true;
    return result;
}

SessionSensorLoadResult SessionLoader::loadSensors(
    const SessionMetadata& metadata,
    const std::function<void(quint64, quint64)>& progress)
{
    using namespace SessionCsv;

    SessionSensorLoadResult result;
    result.success = true;
    QFile file(metadata.sensorSummaryCsvFilename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        result.warning = QStringLiteral("Failed to open sensors CSV: %1")
                             .arg(metadata.sensorSummaryCsvFilename);
        return result;
    }

    result.fileAvailable = true;
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    if (stream.atEnd())
    {
        result.warning = QStringLiteral("Sensors CSV is empty: %1")
                             .arg(metadata.sensorSummaryCsvFilename);
        return result;
    }

    SessionSensorData& data = result.data;
    data.headers = parseCsvLine(stream.readLine());
    const int recordTimestampIndex = findHeaderIndex(data.headers, {QStringLiteral("record_timestamp_us")});
    const int epsilonHostTimestampIndex = findHeaderIndex(data.headers, {QStringLiteral("epsilon_host_timestamp_us")});
    const int navLatIndex = findHeaderIndex(data.headers, {QStringLiteral("nav_lat_deg"), QStringLiteral("rtk_lat")});
    const int navLonIndex = findHeaderIndex(data.headers, {QStringLiteral("nav_lon_deg"), QStringLiteral("rtk_lon")});
    const int navHeightIndex = findHeaderIndex(data.headers, {
        QStringLiteral("nav_height_m"),
        QStringLiteral("rtk_height"),
        QStringLiteral("height_m"),
        QStringLiteral("altitude_m")
    });
    const int trackTimestampIndex = findHeaderIndex(data.headers, {
        QStringLiteral("epsilon_host_timestamp_us"),
        QStringLiteral("record_timestamp_us"),
        QStringLiteral("rtk_timestamp_us")
    });
    const int epsilonValidIndex = findHeaderIndex(data.headers, {QStringLiteral("epsilon_valid"), QStringLiteral("rtk_valid")});
    const int gnssFixIndex = findHeaderIndex(data.headers, {QStringLiteral("gnss_fix"), QStringLiteral("rtk_fix")});
    const bool hasTrackColumns = navLatIndex >= 0 && navLonIndex >= 0;
    const int thValidIndex = findHeaderIndex(data.headers, {QStringLiteral("th_valid")});
    const int baroValidIndex = findHeaderIndex(data.headers, {QStringLiteral("baro_valid")});
    const bool hasLegacyThermometerColumns =
        findHeaderIndex(data.headers, {QStringLiteral("th_timestamp_us"), QStringLiteral("th_valid")}) >= 0;
    const bool hasLegacyBarometerColumns =
        findHeaderIndex(data.headers, {QStringLiteral("baro_timestamp_us"), QStringLiteral("baro_valid")}) >= 0;
    const int hmpTemperatureIndex = findHeaderIndex(
        data.headers,
        hasLegacyThermometerColumns
            ? QStringList{QStringLiteral("hmp_temperature_c"), QStringLiteral("temp_c")}
            : QStringList{QStringLiteral("hmp_temperature_c")});
    const int hmpHumidityIndex = findHeaderIndex(
        data.headers,
        {QStringLiteral("hmp_humidity_rh"), QStringLiteral("humidity_rh")});
    const int ptbPressureIndex = findHeaderIndex(
        data.headers,
        hasLegacyBarometerColumns
            ? QStringList{QStringLiteral("ptb_pressure_hpa"), QStringLiteral("baro_hpa"), QStringLiteral("baro_pa")}
            : QStringList{QStringLiteral("ptb_pressure_hpa")});

    data.rows.reserve(static_cast<int>(
        std::min<quint64>(metadata.sensorRows > 0 ? metadata.sensorRows : 256ULL, 50000ULL)));
    while (!stream.atEnd())
    {
        const QString line = stream.readLine();
        if (line.isEmpty())
        {
            continue;
        }

        QStringList fields = parseCsvLine(line);
        while (fields.size() < data.headers.size())
        {
            fields.push_back(QString());
        }
        data.rows.push_back(fields);

        bool timestampOk = false;
        quint64 timestampUs = 0;
        if (recordTimestampIndex >= 0)
        {
            timestampUs = csvValueAt(fields, recordTimestampIndex).toULongLong(&timestampOk);
        }
        else if (epsilonHostTimestampIndex >= 0)
        {
            timestampUs = csvValueAt(fields, epsilonHostTimestampIndex).toULongLong(&timestampOk);
        }
        else if (trackTimestampIndex >= 0)
        {
            timestampUs = csvValueAt(fields, trackTimestampIndex).toULongLong(&timestampOk);
        }
        else
        {
            timestampUs = csvValueAt(fields, 0).toULongLong(&timestampOk);
        }
        data.timestamps_us.push_back(timestampOk ? timestampUs : 0);

        const bool thValid = thValidIndex < 0 || parseBooleanCsvField(csvValueAt(fields, thValidIndex), true);
        const bool baroValid = baroValidIndex < 0 || parseBooleanCsvField(csvValueAt(fields, baroValidIndex), true);
        double temperatureValue = std::numeric_limits<double>::quiet_NaN();
        if (hmpTemperatureIndex >= 0 && thValid)
        {
            temperatureValue = parseOptionalDouble(csvValueAt(fields, hmpTemperatureIndex));
        }
        data.temperature_values.push_back(temperatureValue);

        const double humidityValue = hmpHumidityIndex >= 0 && thValid
            ? parseOptionalDouble(csvValueAt(fields, hmpHumidityIndex))
            : std::numeric_limits<double>::quiet_NaN();
        data.humidity_values.push_back(humidityValue);

        double pressureValue = std::numeric_limits<double>::quiet_NaN();
        if (ptbPressureIndex >= 0 && baroValid)
        {
            pressureValue = parseOptionalDouble(csvValueAt(fields, ptbPressureIndex));
            const QString pressureHeader = data.headers.value(ptbPressureIndex).trimmed().toLower();
            if (std::isfinite(pressureValue) && pressureHeader.endsWith(QStringLiteral("_pa")))
            {
                pressureValue /= 100.0;
            }
        }
        data.pressure_values.push_back(pressureValue);

        if (hasTrackColumns)
        {
            ++data.track_stats.scanned_rows;
            const bool navValid = epsilonValidIndex < 0 || parseBooleanCsvField(csvValueAt(fields, epsilonValidIndex), true);
            const double latitude = parseOptionalDouble(csvValueAt(fields, navLatIndex));
            const double longitude = parseOptionalDouble(csvValueAt(fields, navLonIndex));
            const QString gnssFix = csvValueAt(fields, gnssFixIndex).trimmed().toUpper();
            const bool missingOrInvalidNav = !navValid || !std::isfinite(latitude) || !std::isfinite(longitude);
            const bool zeroCoordinate = !missingOrInvalidNav && std::abs(latitude) < 1e-8 && std::abs(longitude) < 1e-8;
            const bool outOfRange = !missingOrInvalidNav && !zeroCoordinate &&
                (latitude < -90.0 || latitude > 90.0 || longitude < -180.0 || longitude > 180.0);
            const bool badFix = gnssFix == QStringLiteral("NONE") ||
                gnssFix == QStringLiteral("NO_FIX") ||
                gnssFix == QStringLiteral("INVALID") ||
                gnssFix == QStringLiteral("NO_GPS");

            if (missingOrInvalidNav)
            {
                ++data.track_stats.rejected_invalid_nav;
            }
            else if (zeroCoordinate)
            {
                ++data.track_stats.rejected_zero_coordinate;
            }
            else if (outOfRange)
            {
                ++data.track_stats.rejected_out_of_range;
            }
            else if (badFix)
            {
                ++data.track_stats.rejected_bad_fix;
            }
            else
            {
                bool trackTimestampOk = false;
                const quint64 trackTimestampUs = trackTimestampIndex >= 0
                    ? csvValueAt(fields, trackTimestampIndex).toULongLong(&trackTimestampOk)
                    : data.timestamps_us.last();
                SessionTrackPoint point;
                point.latitude = latitude;
                point.longitude = longitude;
                point.csv_row = data.rows.size() - 1;
                point.gnss_fix = gnssFix;
                if (std::isfinite(temperatureValue))
                {
                    point.temperature_c = temperatureValue;
                    point.has_temperature = true;
                }
                if (std::isfinite(humidityValue))
                {
                    point.humidity_rh = humidityValue;
                    point.has_humidity = true;
                }
                if (std::isfinite(pressureValue))
                {
                    point.pressure_hpa = pressureValue;
                    point.has_pressure = true;
                }
                const double height = parseOptionalDouble(csvValueAt(fields, navHeightIndex));
                if (std::isfinite(height))
                {
                    point.height_m = height;
                    point.has_height = true;
                }
                point.timestamp_us = trackTimestampOk ? trackTimestampUs : data.timestamps_us.last();

                if (!data.track_points.isEmpty())
                {
                    const SessionTrackPoint& previous = data.track_points.last();
                    const double jumpMeters = haversineDistanceMeters(
                        previous.latitude,
                        previous.longitude,
                        point.latitude,
                        point.longitude);
                    if (jumpMeters > data.track_stats.jump_threshold_m)
                    {
                        ++data.track_stats.rejected_jump;
                    }
                    else
                    {
                        point.segment_distance_m = jumpMeters;
                        point.cumulative_distance_m = previous.cumulative_distance_m + jumpMeters;
                        if (previous.timestamp_us > 0 && point.timestamp_us > previous.timestamp_us)
                        {
                            const double elapsedSeconds =
                                static_cast<double>(point.timestamp_us - previous.timestamp_us) / 1000000.0;
                            if (elapsedSeconds > 1e-6)
                            {
                                point.speed_mps = jumpMeters / elapsedSeconds;
                                point.has_speed = true;
                            }
                        }
                        data.track_points.push_back(point);
                        ++data.track_stats.accepted_points;
                    }
                }
                else
                {
                    data.track_points.push_back(point);
                    ++data.track_stats.accepted_points;
                }
            }
        }

        if (progress && data.rows.size() % 5000 == 0)
        {
            progress(static_cast<quint64>(data.rows.size()), metadata.sensorRows);
        }
    }

    return result;
}

}  // namespace VaporView::Ground
