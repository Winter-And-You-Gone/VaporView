#include "ppk/SessionNavigationSource.h"
#include "ppk/PpkProcessor.h"
#include "LogService.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QFileInfo>
#include <QGlobalStatic>

namespace VaporView::Ppk
{
Q_GLOBAL_STATIC(SessionNavigationEvents, navigationEvents)
SessionNavigationEvents *SessionNavigationEvents::instance()
{
    return navigationEvents;
}
namespace
{
QString configPath(const QString &session)
{
    return QDir(session).filePath(QStringLiteral("ppk/ppk_config.json"));
}
QJsonObject readConfig(const QString &session)
{
    QFile file(configPath(session));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
} // namespace
NavigationSource sessionNavigationSource(const QString &session)
{
    return readConfig(session).value("navigation_source").toString() == QStringLiteral("PPK")
               ? NavigationSource::Ppk
               : NavigationSource::Original;
}
bool setSessionNavigationSource(const QString &session, NavigationSource source, QString *error)
{
    if (source == NavigationSource::Ppk)
    {
        // Selection runs on the GUI thread. Full validation belongs to the
        // background Session loaders, which also reject corrupt trajectories.
        if (!PpkProcessor::status(session).completed)
        {
            if (error && error->isEmpty())
                *error = QStringLiteral("PPK_TRACK_UNAVAILABLE");
            return false;
        }
    }
    auto config = readConfig(session);
    config.insert("navigation_source", source == NavigationSource::Ppk ? "PPK" : "Original");
    QDir().mkpath(QDir(session).filePath("ppk"));
    QSaveFile file(configPath(session));
    const auto bytes = QJsonDocument(config).toJson();
    const bool ok = file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
    if (!ok && error)
        *error = file.errorString();
    if (ok)
        LogService::withCurrentInstance(
            [&](LogService &log)
            {
                log.publish(LogLevel::Info, QStringLiteral("ppk"), QStringLiteral("session.ppk"),
                            QStringLiteral("Session 导航轨迹来源已切换。"),
                            {{QStringLiteral("event"), QStringLiteral("ppk_track_activated")},
                             {QStringLiteral("reason_code"), QStringLiteral("OK")},
                             {QStringLiteral("navigation_source"),
                              source == NavigationSource::Ppk ? QStringLiteral("PPK") : QStringLiteral("Original")}},
                            {}, session);
            });
    if (ok)
        SessionNavigationEvents::instance()->notify(QFileInfo(session).absoluteFilePath());
    return ok;
}
SessionNavigationResolver::SessionNavigationResolver(const QString &session) : source_(sessionNavigationSource(session))
{
    if (usesPpk() && (!PpkProcessor::status(session).completed ||
                      !readPpkTrajectory(QDir(session).filePath("ppk/trajectory.csv"), trajectory_, &error_)))
    {
        trajectory_.clear();
        if (error_.isEmpty())
            error_ = QStringLiteral("PPK_TRACK_UNAVAILABLE");
        LogService::withCurrentInstance(
            [&](LogService &log)
            {
                log.publish(LogLevel::Warning, QStringLiteral("ppk"), QStringLiteral("session.ppk"),
                            QStringLiteral("所选 PPK 轨迹不可用。"),
                            {{"event", "ppk_track_unavailable"},
                             {"reason_code", "PPK_TRACK_UNAVAILABLE"},
                             {"system_error", error_},
                             {"session", session}},
                            {}, session);
            });
    }
}
std::optional<PpkSample> SessionNavigationResolver::atTimestamp(std::uint64_t time) const
{
    return usesPpk() ? interpolateTrajectory(trajectory_, time) : std::nullopt;
}
void SessionNavigationResolver::prepareCsv(const QStringList &headers)
{
    columns_.clear();
    for (int i = 0; i < headers.size(); ++i)
        columns_.insert(headers[i].trimmed().toLower(), i);
}
void SessionNavigationResolver::applyCsvRow(QStringList &row) const
{
    if (!usesPpk())
        return;
    auto find = [&](std::initializer_list<const char *> names)
    {
        for (const char *name : names)
            if (columns_.contains(QString::fromLatin1(name)))
                return columns_.value(QString::fromLatin1(name));
        return -1;
    };
    const int timeIndex =
        find({"record_timestamp_us", "host_time_us", "timestamp_us", "epsilon_host_timestamp_us", "rtk_timestamp_us"});
    const auto sample = atTimestamp(timeIndex >= 0 ? row.value(timeIndex).toULongLong() : 0);
    auto set = [&](std::initializer_list<const char *> names, const QString &value)
    {
        const int index = find(names);
        if (index >= 0 && index < row.size())
            row[index] = value;
    };
    auto number = [](double value) { return QString::number(value, 'g', 17); };
    set({"nav_lat_deg", "lat_deg", "latitude_deg", "epsilon_latitude_deg", "rtk_lat", "rtk_lat_deg"},
        sample ? number(sample->latitude) : QString());
    set({"nav_lon_deg", "lon_deg", "longitude_deg", "epsilon_longitude_deg", "rtk_lon", "rtk_lon_deg"},
        sample ? number(sample->longitude) : QString());
    set({"nav_height_m", "rtk_height", "rtk_alt", "rtk_alt_m", "height_m", "altitude_m", "epsilon_height_m"},
        sample ? number(sample->height) : QString());
    set({"ecef_x_m", "nav_ecef_x_m", "epsilon_ecef_x_m"}, sample ? number(sample->ecef[0]) : QString());
    set({"ecef_y_m", "nav_ecef_y_m", "epsilon_ecef_y_m"}, sample ? number(sample->ecef[1]) : QString());
    set({"ecef_z_m", "nav_ecef_z_m", "epsilon_ecef_z_m"}, sample ? number(sample->ecef[2]) : QString());
    // Original local NED coordinates used a different trajectory origin.
    set({"ned_n_m", "nav_ned_n_m", "epsilon_ned_n_m"}, QString());
    set({"ned_e_m", "nav_ned_e_m", "epsilon_ned_e_m"}, QString());
    set({"ned_d_m", "nav_ned_d_m", "epsilon_ned_d_m"}, QString());
    set({"epsilon_valid", "rtk_valid"}, sample ? QStringLiteral("true") : QStringLiteral("false"));
    set({"gnss_fix", "rtk_fix", "fix_quality", "gnss_status", "rtk_status", "epsilon_gnss_fix_text"},
        sample ? (sample->quality == 1   ? QStringLiteral("RTK_FIXED")
                  : sample->quality == 2 ? QStringLiteral("RTK_FLOAT")
                                         : QStringLiteral("SINGLE"))
               : QStringLiteral("INVALID"));
    set({"gnss_satellites", "satellites", "satellite_count", "epsilon_gnss_satellites", "rtk_sat"},
        sample ? QString::number(sample->satelliteCount) : QString());
    // Subsequent readers use the sensor's Session time after relocation.
    if (timeIndex >= 0)
        set({"epsilon_host_timestamp_us", "rtk_timestamp_us"}, row.value(timeIndex));
    set({"height_reference"}, QStringLiteral("WGS84_ELLIPSOID"));
}
} // namespace VaporView::Ppk
