#include "ppk/PpkNavigation.h"
#include "rtklib.h"
#include <QFile>
#include <QSaveFile>
#include <QTextStream>
#include <algorithm>
#include <cmath>

namespace VaporView::Ppk
{
namespace
{
constexpr std::uint64_t kAttitudeMaxGapNs = 2000000000ULL;
bool normalize(std::array<double, 4> &q)
{
    double norm = 0;
    for (double value : q)
        norm += value * value;
    if (!std::isfinite(norm) || norm < 1e-12)
        return false;
    norm = std::sqrt(norm);
    for (double &value : q)
        value /= norm;
    return true;
}
void resolveLlh(PpkSample &s)
{
    double pos[3];
    ecef2pos(s.ecef.data(), pos);
    s.latitude = pos[0] * R2D;
    s.longitude = pos[1] * R2D;
    s.height = pos[2];
}
} // namespace

std::array<double, 4> quaternionFromEuler(double roll, double pitch, double yaw)
{
    const double cr = std::cos(roll / 2), sr = std::sin(roll / 2), cp = std::cos(pitch / 2), sp = std::sin(pitch / 2),
                 cy = std::cos(yaw / 2), sy = std::sin(yaw / 2);
    return {cr * cp * cy + sr * sp * sy, sr * cp * cy - cr * sp * sy, cr * sp * cy + sr * cp * sy,
            cr * cp * sy - sr * sp * cy};
}

std::array<double, 4> slerp(std::array<double, 4> a, std::array<double, 4> b, double t)
{
    normalize(a);
    normalize(b);
    double dot = 0;
    for (int i = 0; i < 4; ++i)
        dot += a[i] * b[i];
    if (dot < 0)
    {
        for (double &value : b)
            value = -value;
        dot = -dot;
    }
    dot = std::clamp(dot, -1.0, 1.0);
    double first = 1 - t, second = t;
    if (dot < 0.9995)
    {
        const double angle = std::acos(dot), sine = std::sin(angle);
        first = std::sin((1 - t) * angle) / sine;
        second = std::sin(t * angle) / sine;
    }
    for (int i = 0; i < 4; ++i)
        a[i] = first * a[i] + second * b[i];
    normalize(a);
    return a;
}

PpkTimeAlignment::PpkTimeAlignment(std::vector<AttitudeSample> attitudes) : attitudes_(std::move(attitudes))
{
    // Reject unusable samples and time reversals instead of sorting across a clock reset.
    std::vector<AttitudeSample> valid;
    for (auto a : attitudes_)
        if (a.utcNs && a.hostUs && normalize(a.bodyToNed) &&
            (valid.empty() || (a.utcNs > valid.back().utcNs && a.hostUs > valid.back().hostUs)))
            valid.push_back(a);
    attitudes_ = std::move(valid);
}

std::optional<AttitudeSample> PpkTimeAlignment::atUtc(std::uint64_t utcNs) const
{
    const auto after = std::lower_bound(attitudes_.begin(), attitudes_.end(), utcNs,
                                        [](const AttitudeSample &a, std::uint64_t time) { return a.utcNs < time; });
    if (after != attitudes_.end() && after->utcNs == utcNs)
        return *after;
    if (after == attitudes_.begin() || after == attitudes_.end())
        return {};
    const auto &before = *(after - 1);
    if (after->utcNs - before.utcNs > kAttitudeMaxGapNs)
        return {};
    const double t = double(utcNs - before.utcNs) / double(after->utcNs - before.utcNs);
    return AttitudeSample{utcNs, before.hostUs + std::uint64_t(std::llround(t * double(after->hostUs - before.hostUs))),
                          slerp(before.bodyToNed, after->bodyToNed, t)};
}

std::optional<std::uint64_t> PpkTimeAlignment::utcForSession(std::uint64_t hostUs) const
{
    const auto after = std::lower_bound(attitudes_.begin(), attitudes_.end(), hostUs,
                                        [](const AttitudeSample &a, std::uint64_t time) { return a.hostUs < time; });
    if (after != attitudes_.end() && after->hostUs == hostUs)
        return after->utcNs;
    if (after == attitudes_.begin() || after == attitudes_.end())
        return {};
    const auto &before = *(after - 1);
    if (after->utcNs - before.utcNs > kAttitudeMaxGapNs)
        return {};
    const double t = double(hostUs - before.hostUs) / double(after->hostUs - before.hostUs);
    return before.utcNs + std::uint64_t(std::llround(t * double(after->utcNs - before.utcNs)));
}

bool PpkTimeAlignment::correctToImu(PpkSample &sample, const std::array<double, 3> &arm, QString *error) const
{
    const auto attitude = atUtc(sample.utcNs);
    if (!attitude)
    {
        if (error)
            *error = QStringLiteral("ATTITUDE_UTC_ALIGNMENT_UNAVAILABLE: %1 (%2..%3)")
                         .arg(qulonglong(sample.utcNs))
                         .arg(attitudes_.empty() ? 0ULL : qulonglong(attitudes_.front().utcNs))
                         .arg(attitudes_.empty() ? 0ULL : qulonglong(attitudes_.back().utcNs));
        return false;
    }
    sample.sessionUs = attitude->hostUs;
    const auto &q = attitude->bodyToNed;
    const double w = q[0], x = q[1], y = q[2], z = q[3];
    const double north =
        (1 - 2 * (y * y + z * z)) * arm[0] + 2 * (x * y - w * z) * arm[1] + 2 * (x * z + w * y) * arm[2];
    const double east =
        2 * (x * y + w * z) * arm[0] + (1 - 2 * (x * x + z * z)) * arm[1] + 2 * (y * z - w * x) * arm[2];
    const double down =
        2 * (x * z - w * y) * arm[0] + 2 * (y * z + w * x) * arm[1] + (1 - 2 * (x * x + y * y)) * arm[2];
    double position[3], delta[3], enu[3] = {east, north, -down};
    ecef2pos(sample.ecef.data(), position);
    enu2ecef(position, enu, delta);
    for (int i = 0; i < 3; ++i)
        sample.ecef[i] -= delta[i];
    resolveLlh(sample);
    return true;
}

bool writePpkTrajectory(const QString &path, const PpkTrajectory &trajectory, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    QTextStream out(&file);
    out << "gnss_utc_ns,session_timestamp_us,latitude,longitude,ellipsoidal_height,ecef_x,ecef_y,ecef_z,quality,"
           "satellite_count,sd_n,sd_e,sd_u,age,ratio,source,reference_point\n";
    for (const auto &s : trajectory)
    {
        out << quint64(s.utcNs) << ',' << quint64(s.sessionUs) << ',' << QString::number(s.latitude, 'f', 10) << ','
            << QString::number(s.longitude, 'f', 10) << ',' << QString::number(s.height, 'f', 6) << ',';
        for (double value : s.ecef)
            out << QString::number(value, 'f', 6) << ',';
        out << s.quality << ',' << s.satelliteCount << ',' << QString::number(s.sdN, 'g', 12) << ','
            << QString::number(s.sdE, 'g', 12) << ',' << QString::number(s.sdU, 'g', 12) << ','
            << QString::number(s.age, 'g', 12) << ',' << QString::number(s.ratio, 'g', 12) << ",PPK,IMU\n";
    }
    out.flush();
    const bool ok = out.status() == QTextStream::Ok && file.commit();
    if (!ok && error)
        *error = file.errorString();
    return ok;
}

bool readPpkTrajectory(const QString &path, PpkTrajectory &trajectory, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    QTextStream in(&file);
    const auto headers = in.readLine().split(',');
    if (headers.size() != 17 || headers[0] != QStringLiteral("gnss_utc_ns"))
    {
        if (error)
            *error = QStringLiteral("INVALID_PPK_TRAJECTORY_HEADER");
        return false;
    }
    PpkTrajectory next;
    while (!in.atEnd())
    {
        const auto fields = in.readLine().split(',');
        if (fields.size() != 17 || fields[15] != QStringLiteral("PPK") || fields[16] != QStringLiteral("IMU"))
        {
            if (error)
                *error = QStringLiteral("INVALID_PPK_TRAJECTORY_ROW");
            return false;
        }
        PpkSample s;
        bool ok = true, parsed;
        s.utcNs = fields[0].toULongLong(&parsed);
        ok &= parsed;
        s.sessionUs = fields[1].toULongLong(&parsed);
        ok &= parsed;
        double *values[] = {&s.latitude, &s.longitude, &s.height, &s.ecef[0], &s.ecef[1], &s.ecef[2]};
        for (int i = 0; i < 6; ++i)
        {
            *values[i] = fields[2 + i].toDouble(&parsed);
            ok &= parsed && std::isfinite(*values[i]);
        }
        s.quality = fields[8].toInt(&parsed);
        ok &= parsed;
        s.satelliteCount = fields[9].toInt(&parsed);
        ok &= parsed;
        double *quality[] = {&s.sdN, &s.sdE, &s.sdU, &s.age, &s.ratio};
        for (int i = 0; i < 5; ++i)
        {
            *quality[i] = fields[10 + i].toDouble(&parsed);
            ok &= parsed && std::isfinite(*quality[i]);
        }
        if (!ok || !s.utcNs || !s.sessionUs || s.quality < 1 || s.quality > 7 || std::abs(s.latitude) > 90 ||
            std::abs(s.longitude) > 180 ||
            (!next.empty() && (s.utcNs <= next.back().utcNs || s.sessionUs <= next.back().sessionUs)))
        {
            if (error)
                *error = QStringLiteral("INVALID_PPK_TRAJECTORY_VALUE");
            return false;
        }
        next.push_back(s);
    }
    if (next.empty())
    {
        if (error)
            *error = QStringLiteral("EMPTY_PPK_TRAJECTORY");
        return false;
    }
    trajectory = std::move(next);
    return true;
}

std::optional<PpkSample> interpolateTrajectory(const PpkTrajectory &track, std::uint64_t sessionUs)
{
    const auto after = std::lower_bound(track.begin(), track.end(), sessionUs,
                                        [](const PpkSample &s, std::uint64_t time) { return s.sessionUs < time; });
    if (after != track.end() && after->sessionUs == sessionUs)
        return *after;
    if (after == track.begin() || after == track.end())
        return {};
    const auto &before = *(after - 1);
    const double t = double(sessionUs - before.sessionUs) / double(after->sessionUs - before.sessionUs);
    PpkSample s = before;
    s.sessionUs = sessionUs;
    s.utcNs = before.utcNs + std::uint64_t(std::llround(t * double(after->utcNs - before.utcNs)));
    for (int i = 0; i < 3; ++i)
        s.ecef[i] = (1 - t) * before.ecef[i] + t * after->ecef[i];
    // Never promote an interpolated position beyond the worse endpoint quality.
    s.quality = std::max(before.quality, after->quality);
    resolveLlh(s);
    return s;
}
} // namespace VaporView::Ppk
