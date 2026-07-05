#include "geo/SessionTrackReader.h"

#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <algorithm>
#include <cmath>

namespace VaporView::Geo {
namespace {

QString normalizeHeader(QString name)
{
    return name.trimmed().toLower();
}

QStringList parseCsvLine(const QString& line)
{
    QStringList fields;
    QString current;
    bool inQuotes = false;
    for (int i = 0; i < line.size(); ++i)
    {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char('"'))
        {
            if (inQuotes && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"'))
            {
                current.append(ch);
                ++i;
            }
            else
            {
                inQuotes = !inQuotes;
            }
        }
        else if (ch == QLatin1Char(',') && !inQuotes)
        {
            fields.push_back(current);
            current.clear();
        }
        else
        {
            current.append(ch);
        }
    }
    fields.push_back(current);
    return fields;
}

int findColumn(const QHash<QString, int>& columns, std::initializer_list<const char *> names)
{
    for (const char *name : names)
    {
        const auto it = columns.constFind(QString::fromLatin1(name));
        if (it != columns.cend())
        {
            return it.value();
        }
    }
    return -1;
}

QString fieldAt(const QStringList& fields, int column)
{
    return column >= 0 && column < fields.size() ? fields.at(column).trimmed() : QString();
}

double readDouble(const QStringList& fields, int column)
{
    const QString value = fieldAt(fields, column);
    if (value.isEmpty())
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    bool ok = false;
    const double parsed = value.toDouble(&ok);
    return ok ? parsed : std::numeric_limits<double>::quiet_NaN();
}

qint64 readInt64(const QStringList& fields, int column)
{
    const QString value = fieldAt(fields, column);
    if (value.isEmpty())
    {
        return 0;
    }
    bool ok = false;
    const qint64 parsed = value.toLongLong(&ok);
    return ok ? parsed : 0;
}

int readInt(const QStringList& fields, int column)
{
    const QString value = fieldAt(fields, column);
    if (value.isEmpty())
    {
        return 0;
    }
    bool ok = false;
    const int parsed = value.toInt(&ok);
    return ok ? parsed : 0;
}

FixQuality parseFixQuality(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized.isEmpty())
    {
        return FixQuality::Unknown;
    }
    if (normalized == QStringLiteral("invalid") ||
        normalized == QStringLiteral("none") ||
        normalized == QStringLiteral("no fix") ||
        normalized == QStringLiteral("0"))
    {
        return FixQuality::Invalid;
    }
    if (normalized.contains(QStringLiteral("fixed")) ||
        normalized.contains(QStringLiteral("rtk fixed")) ||
        normalized == QStringLiteral("6") ||
        normalized == QStringLiteral("4"))
    {
        return FixQuality::Fixed;
    }
    if (normalized.contains(QStringLiteral("float")) ||
        normalized.contains(QStringLiteral("rtk float")) ||
        normalized == QStringLiteral("5"))
    {
        return FixQuality::Float;
    }
    if (normalized.contains(QStringLiteral("dgps")) ||
        normalized.contains(QStringLiteral("differential")) ||
        normalized == QStringLiteral("2"))
    {
        return FixQuality::Dgps;
    }
    if (normalized.contains(QStringLiteral("single")) ||
        normalized.contains(QStringLiteral("fix")) ||
        normalized == QStringLiteral("1") ||
        normalized == QStringLiteral("3"))
    {
        return FixQuality::Single;
    }
    return FixQuality::Unknown;
}

HeightReference parseHeightReference(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized.isEmpty())
    {
        return HeightReference::Unknown;
    }
    if (normalized.contains(QStringLiteral("ellipsoid")) ||
        normalized == QStringLiteral("hae") ||
        normalized == QStringLiteral("wgs84"))
    {
        return HeightReference::Ellipsoid;
    }
    if (normalized.contains(QStringLiteral("msl")) ||
        normalized.contains(QStringLiteral("mean sea")) ||
        normalized.contains(QStringLiteral("orthometric")) ||
        normalized.contains(QStringLiteral("geoid")))
    {
        return HeightReference::MeanSeaLevel;
    }
    if (normalized.contains(QStringLiteral("local")) ||
        normalized.contains(QStringLiteral("ned")))
    {
        return HeightReference::Local;
    }
    if (normalized.contains(QStringLiteral("dem")) ||
        normalized.contains(QStringLiteral("terrain")))
    {
        return HeightReference::Dem;
    }
    return HeightReference::Unknown;
}

QString locateDevicesCsv(const QString& sessionDir)
{
    const QDir dir(sessionDir);
    const QString preferred = dir.filePath(QStringLiteral("sensors/devices.csv"));
    if (QFile::exists(preferred))
    {
        return preferred;
    }

    QDirIterator it(sessionDir,
                    QStringList{QStringLiteral("devices.csv")},
                    QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        return it.next();
    }
    return {};
}

} // namespace

SessionTrackReadResult readSessionTrack(const QString& sessionDir)
{
    SessionTrackReadResult result;
    const QString csvPath = locateDevicesCsv(sessionDir);
    if (csvPath.isEmpty())
    {
        result.error = QStringLiteral("devices.csv not found under session directory");
        return result;
    }

    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        result.error = file.errorString();
        result.sourceCsvPath = csvPath;
        return result;
    }

    QTextStream in(&file);
    if (in.atEnd())
    {
        result.error = QStringLiteral("devices.csv is empty");
        result.sourceCsvPath = csvPath;
        return result;
    }

    const QStringList headers = parseCsvLine(in.readLine());
    QHash<QString, int> columns;
    for (int i = 0; i < headers.size(); ++i)
    {
        columns.insert(normalizeHeader(headers.at(i)), i);
    }

    const int recordTsCol = findColumn(columns, {"record_timestamp_us", "host_time_us", "timestamp_us"});
    const int deviceTsCol = findColumn(columns, {"device_timestamp_us", "epsilon_device_timestamp_us"});
    const int latCol = findColumn(columns, {"nav_lat_deg", "lat_deg", "latitude_deg", "epsilon_latitude_deg"});
    const int lonCol = findColumn(columns, {"nav_lon_deg", "lon_deg", "longitude_deg", "epsilon_longitude_deg"});
    const int heightCol = findColumn(columns, {"nav_height_m", "height_m", "altitude_m", "epsilon_height_m"});
    const int ecefXCol = findColumn(columns, {"ecef_x_m", "nav_ecef_x_m", "epsilon_ecef_x_m"});
    const int ecefYCol = findColumn(columns, {"ecef_y_m", "nav_ecef_y_m", "epsilon_ecef_y_m"});
    const int ecefZCol = findColumn(columns, {"ecef_z_m", "nav_ecef_z_m", "epsilon_ecef_z_m"});
    const int nedNCol = findColumn(columns, {"ned_n_m", "nav_ned_n_m", "epsilon_ned_n_m"});
    const int nedECol = findColumn(columns, {"ned_e_m", "nav_ned_e_m", "epsilon_ned_e_m"});
    const int nedDCol = findColumn(columns, {"ned_d_m", "nav_ned_d_m", "epsilon_ned_d_m"});
    const int velNCol = findColumn(columns, {"vel_n_mps", "nav_vel_n_mps", "epsilon_vel_n_mps"});
    const int velECol = findColumn(columns, {"vel_e_mps", "nav_vel_e_mps", "epsilon_vel_e_mps"});
    const int velDCol = findColumn(columns, {"vel_d_mps", "nav_vel_d_mps", "epsilon_vel_d_mps"});
    const int rollCol = findColumn(columns, {"roll_deg", "epsilon_roll_deg"});
    const int pitchCol = findColumn(columns, {"pitch_deg", "epsilon_pitch_deg"});
    const int yawCol = findColumn(columns, {"yaw_deg", "heading_deg", "epsilon_yaw_deg"});
    const int satellitesCol = findColumn(columns, {"satellites", "satellite_count", "num_satellites", "gnss_satellites", "gnss_satellite_count", "epsilon_gnss_satellites"});
    const int hdopCol = findColumn(columns, {"hdop", "epsilon_hdop"});
    const int vdopCol = findColumn(columns, {"vdop", "epsilon_vdop"});
    const int diffAgeCol = findColumn(columns, {"diff_age_s", "epsilon_diff_age_s"});
    const int fixCol = findColumn(columns, {"fix_quality", "gnss_status", "rtk_status", "gnss_fix_text", "epsilon_gnss_fix_text", "gnss_fix_code"});
    const int heightReferenceCol = findColumn(columns, {"height_reference", "height_ref", "altitude_reference", "nav_height_reference"});

    if (latCol < 0 || lonCol < 0 || heightCol < 0)
    {
        result.error = QStringLiteral("devices.csv does not contain latitude/longitude/height columns");
        result.sourceCsvPath = csvPath;
        return result;
    }

    while (!in.atEnd())
    {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty())
        {
            continue;
        }

        const QStringList fields = parseCsvLine(line);
        NavSample sample;
        sample.recordTimestampUs = readInt64(fields, recordTsCol);
        sample.deviceTimestampUs = readInt64(fields, deviceTsCol);
        sample.latDeg = readDouble(fields, latCol);
        sample.lonDeg = readDouble(fields, lonCol);
        sample.heightM = readDouble(fields, heightCol);
        sample.ecefXM = readDouble(fields, ecefXCol);
        sample.ecefYM = readDouble(fields, ecefYCol);
        sample.ecefZM = readDouble(fields, ecefZCol);
        sample.nedNM = readDouble(fields, nedNCol);
        sample.nedEM = readDouble(fields, nedECol);
        sample.nedDM = readDouble(fields, nedDCol);
        sample.velNMps = readDouble(fields, velNCol);
        sample.velEMps = readDouble(fields, velECol);
        sample.velDMps = readDouble(fields, velDCol);
        sample.rollDeg = readDouble(fields, rollCol);
        sample.pitchDeg = readDouble(fields, pitchCol);
        sample.yawDeg = readDouble(fields, yawCol);
        sample.satellites = readInt(fields, satellitesCol);
        sample.hdop = readDouble(fields, hdopCol);
        sample.vdop = readDouble(fields, vdopCol);
        sample.diffAgeS = readDouble(fields, diffAgeCol);
        sample.fixQuality = parseFixQuality(fieldAt(fields, fixCol));
        sample.heightReference = parseHeightReference(fieldAt(fields, heightReferenceCol));

        if (sample.hasLlh())
        {
            result.samples.push_back(sample);
        }
    }

    result.ok = true;
    result.sourceCsvPath = csvPath;
    return result;
}

} // namespace VaporView::Geo
