#include "ppk/PpkProcessor.h"
#include "ppk/AttitudeStore.h"
#include "ppk/EpsilonRinexWriter.h"
#include "ppk/ObservationStore.h"
#include "LogService.h"
#include "rtklib.h"
#undef lock
#undef unlock

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QRegularExpression>
#include <QLockFile>
#include <QTemporaryDir>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cmath>
#include <mutex>
#include <memory>

namespace
{
std::timed_mutex postposMutex;
thread_local const std::atomic_bool *processingCancel = nullptr;
thread_local VaporView::Ppk::PpkProcessor::Progress processingProgress;
thread_local double processingStart = 0, processingEnd = 0;
thread_local QString processingMessage;
thread_local int processingPercent = -1;
} // namespace
extern "C" int showmsg(const char *format, ...)
{
    char message[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (*message)
        processingMessage = QString::fromUtf8(message);
    return processingCancel && processingCancel->load() ? 1 : 0;
}
extern "C" void settspan(gtime_t start, gtime_t end)
{
    processingStart = double(start.time) + start.sec;
    processingEnd = double(end.time) + end.sec;
}
extern "C" void settime(gtime_t time)
{
    if (processingProgress && processingEnd > processingStart)
    {
        const int percent = std::clamp(
            int(100 * (double(time.time) + time.sec - processingStart) / (processingEnd - processingStart)), 0, 100);
        if (percent != processingPercent)
        {
            processingPercent = percent;
            processingProgress(percent, processingMessage);
        }
    }
}

namespace VaporView::Ppk
{
namespace
{
QString path(const QString &session, const QString &relative)
{
    return QDir(session).filePath(relative);
}
QJsonObject readJson(const QString &filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
bool writeJson(const QString &filename, const QJsonObject &json, QString *error = nullptr)
{
    QDir().mkpath(QFileInfo(filename).absolutePath());
    QSaveFile file(filename);
    const auto bytes = QJsonDocument(json).toJson();
    const bool ok = file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
    if (!ok && error)
        *error = file.errorString();
    return ok;
}
void publishPpkLog(const QString &session, const QString &event, const QString &reason, QVariantMap fields = {})
{
    fields.insert(QStringLiteral("event"), event);
    const auto code = reason.section(':', 0, 0);
    const bool stableCode = QRegularExpression(QStringLiteral("^[A-Z][A-Z0-9_]*$")).match(code).hasMatch();
    fields.insert(QStringLiteral("reason_code"), stableCode ? code : QStringLiteral("PPK_PROCESSING_ERROR"));
    if (reason != code || !stableCode)
        fields.insert(QStringLiteral("system_error"), reason);
    fields.insert(QStringLiteral("session"), session);
    LogService::withCurrentInstance(
        [&](LogService &log)
        {
            log.publish(reason == QStringLiteral("OK") ? LogLevel::Info : LogLevel::Error, QStringLiteral("ppk"),
                        QStringLiteral("session.ppk"), QStringLiteral("Session PPK 处理状态已更新。"), fields, {},
                        session);
        });
}
bool archive(const QString &session, const QString &source, const QString &relative, QString *error)
{
    QFile input(source);
    QSaveFile output(path(session, relative));
    if (QFileInfo(source).absoluteFilePath() == QFileInfo(output.fileName()).absoluteFilePath())
        return true;
    QDir().mkpath(QFileInfo(output.fileName()).absolutePath());
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
    {
        if (error)
            *error = QStringLiteral("PPK_INPUT_OPEN_FAILED");
        return false;
    }
    while (!input.atEnd())
    {
        const auto bytes = input.read(65536);
        if (bytes.isEmpty() && input.error() != QFile::NoError)
        {
            if (error)
                *error = input.errorString();
            return false;
        }
        if (output.write(bytes) != bytes.size())
        {
            if (error)
                *error = output.errorString();
            return false;
        }
    }
    const bool ok = output.commit();
    if (!ok && error)
        *error = output.errorString();
    return ok;
}
bool inspectRinex(const QString &filename, bool &observations, bool &navigation, QString *error)
{
    observations = navigation = false;
    QTemporaryDir temporary;
    const QString local = temporary.filePath(QStringLiteral("input.rnx"));
    if (!temporary.isValid() || !QFile::copy(filename, local))
    {
        if (error)
            *error = QStringLiteral("RINEX_INPUT_OPEN_FAILED");
        return false;
    }
    obs_t obs{};
    auto nav = std::make_unique<nav_t>();
    sta_t station{};
    const auto name = QFile::encodeName(QDir::toNativeSeparators(local));
    {
        std::lock_guard<std::timed_mutex> guard(postposMutex);
        readrnxt(name.constData(), 1, {}, {}, 0, "", &obs, nav.get(), &station);
    }
    observations = obs.n > 0;
    navigation = nav->n > 0 || nav->ng > 0 || nav->ns > 0;
    freeobs(&obs);
    freenav(nav.get(), 0xFF);
    if (!observations && !navigation && error)
        *error = QStringLiteral("INVALID_OR_EMPTY_RINEX");
    return observations || navigation;
}
QString safeRelative(const QString &value)
{
    const auto clean = QDir::cleanPath(value);
    return value.isEmpty() || QDir::isAbsolutePath(value) || clean.startsWith(QStringLiteral("..")) ? QString() : clean;
}
bool loadSolution(const QString &filename, PpkTrajectory &trajectory, QString &error)
{
    solbuf_t solutions{};
    initsolbuf(&solutions, 0, 0);
    QByteArray name = QFile::encodeName(QDir::toNativeSeparators(filename));
    char *files[] = {name.data()};
    if (!readsol(files, 1, &solutions) || !solutions.n)
    {
        freesolbuf(&solutions);
        error = QStringLiteral("EMPTY_RTKLIB_SOLUTION");
        return false;
    }
    for (int i = 0; i < solutions.n; ++i)
    {
        const sol_t *solution = getsol(&solutions, i);
        if (!solution || solution->stat < 1 || solution->stat > 7 || solution->type != 0)
            continue;
        PpkSample sample;
        const auto utc = gpst2utc(solution->time);
        sample.utcNs = std::uint64_t(utc.time) * 1000000000ULL + std::uint64_t(std::llround(utc.sec * 1e9));
        for (int axis = 0; axis < 3; ++axis)
            sample.ecef[axis] = solution->rr[axis];
        if (!std::isfinite(norm(sample.ecef.data(), 3)) || norm(sample.ecef.data(), 3) < 6e6)
            continue;
        double llh[3];
        ecef2pos(sample.ecef.data(), llh);
        sample.latitude = llh[0] * R2D;
        sample.longitude = llh[1] * R2D;
        sample.height = llh[2];
        sample.quality = solution->stat;
        sample.satelliteCount = solution->ns;
        double covariance[9] = {solution->qr[0], solution->qr[3], solution->qr[5], solution->qr[3], solution->qr[1],
                                solution->qr[4], solution->qr[5], solution->qr[4], solution->qr[2]},
               enu[9];
        covenu(llh, covariance, enu);
        sample.sdE = std::sqrt((std::max)(enu[0], 0.0));
        sample.sdN = std::sqrt((std::max)(enu[4], 0.0));
        sample.sdU = std::sqrt((std::max)(enu[8], 0.0));
        sample.age = solution->age;
        sample.ratio = solution->ratio;
        if (!trajectory.empty() && sample.utcNs <= trajectory.back().utcNs)
            continue;
        trajectory.push_back(sample);
    }
    freesolbuf(&solutions);
    if (trajectory.empty())
    {
        error = QStringLiteral("NO_VALID_RTKLIB_SOLUTION");
        return false;
    }
    return true;
}
} // namespace

PpkConfig PpkProcessor::loadConfig(const QString &session)
{
    const auto object = readJson(path(session, QStringLiteral("ppk/ppk_config.json")));
    PpkConfig config;
    config.roverObs = safeRelative(object.value("rover_obs").toString());
    config.baseObs = safeRelative(object.value("base_obs").toString());
    for (const auto &value : object.value("navigation_files").toArray())
    {
        const auto relative = safeRelative(value.toString());
        if (!relative.isEmpty())
            config.navigationFiles.push_back(relative);
    }
    config.receiver = object.value("receiver").toInt(1);
    config.frequencies = object.value("frequencies").toInt(3);
    config.elevationMaskDeg = object.value("elevation_mask_deg").toDouble(15);
    config.constellations = object.value("constellations").toInt(config.constellations);
    const auto arm = object.value("imu_to_main_antenna_body_m").toArray();
    for (int i = 0; i < 3 && i < arm.size(); ++i)
        config.imuToAntennaBodyM[i] = arm[i].toDouble();
    return config;
}
bool PpkProcessor::saveConfig(const QString &session, const PpkConfig &config, QString *error)
{
    QJsonArray nav, arm;
    for (const auto &value : config.navigationFiles)
        nav.append(value);
    for (double value : config.imuToAntennaBodyM)
        arm.append(value);
    auto object = readJson(path(session, QStringLiteral("ppk/ppk_config.json")));
    object.insert("version", 1);
    object.insert("rover_obs", config.roverObs);
    object.insert("base_obs", config.baseObs);
    object.insert("navigation_files", nav);
    object.insert("receiver", config.receiver);
    object.insert("positioning_mode", "Kinematic");
    object.insert("frequencies", config.frequencies);
    object.insert("elevation_mask_deg", config.elevationMaskDeg);
    object.insert("constellations", config.constellations);
    object.insert("imu_to_main_antenna_body_m", arm);
    object.insert("rtklib_version", QStringLiteral(VER_RTKLIB PATCH_LEVEL));
    object.insert("signal_mapping_policy", epsilonRinexMappingPolicy());
    return writeJson(path(session, QStringLiteral("ppk/ppk_config.json")), object, error);
}
PpkStatus PpkProcessor::status(const QString &session)
{
    PpkStatus result;
    const auto config = loadConfig(session);
    result.roverAvailable = QFileInfo(ObservationStore::filename(session)).size() > 12 ||
                            (!config.roverObs.isEmpty() && QFileInfo::exists(path(session, config.roverObs)));
    result.baseAvailable = !config.baseObs.isEmpty() && QFileInfo::exists(path(session, config.baseObs));
    result.navigationAvailable = !config.navigationFiles.isEmpty();
    for (const auto &nav : config.navigationFiles)
        result.navigationAvailable &= QFileInfo::exists(path(session, nav));
    result.quality = readJson(path(session, QStringLiteral("ppk/ppk_quality.json")));
    result.state = result.quality.value("state").toString();
    result.error = result.quality.value("error").toString();
    result.completed = result.state == QStringLiteral("Completed") &&
                       QFileInfo::exists(path(session, QStringLiteral("ppk/trajectory.csv")));
    if (result.state.isEmpty())
        result.state = result.ready() ? QStringLiteral("Ready") : QStringLiteral("MissingInputs");
    return result;
}
bool PpkProcessor::importBase(const QString &session, const QString &file, QString *error)
{
    bool observations = false, navigation = false;
    if (!inspectRinex(file, observations, navigation, error) || !observations)
    {
        if (error && error->isEmpty())
            *error = QStringLiteral("BASE_OBSERVATIONS_REQUIRED");
        return false;
    }
    const QString relative = QStringLiteral("ppk/base/base.obs");
    if (!archive(session, file, relative, error))
        return false;
    auto config = loadConfig(session);
    config.baseObs = relative;
    if (navigation && !config.navigationFiles.contains(relative))
        config.navigationFiles.append(relative);
    if (!saveConfig(session, config, error))
        return false;
    // CORS and EPSILON exports commonly package OBS and NAV side by side.
    const QFileInfo info(file);
    QStringList candidates{info.completeBaseName() + QStringLiteral(".nav")};
    const auto suffix = info.suffix();
    if (suffix.size() == 3 && suffix.endsWith('o', Qt::CaseInsensitive))
        for (QChar system : {QChar('n'), QChar('g'), QChar('p')})
            candidates.append(info.completeBaseName() + '.' + suffix.left(2) + system);
    for (const auto &candidate : candidates)
    {
        const QString sibling = info.dir().filePath(candidate);
        if (QFileInfo::exists(sibling) && !importNavigation(session, sibling, error))
            return false;
    }
    return true;
}
bool PpkProcessor::importNavigation(const QString &session, const QString &file, QString *error)
{
    bool observations = false, navigation = false;
    if (!inspectRinex(file, observations, navigation, error) || !navigation)
    {
        if (error && error->isEmpty())
            *error = QStringLiteral("NAVIGATION_EPHEMERIS_REQUIRED");
        return false;
    }
    const QString relative = QStringLiteral("ppk/nav/") + QFileInfo(file).fileName();
    if (!archive(session, file, relative, error))
        return false;
    auto config = loadConfig(session);
    if (!config.navigationFiles.contains(relative))
        config.navigationFiles.append(relative);
    return saveConfig(session, config, error);
}
bool PpkProcessor::clearResult(const QString &session, QString *error)
{
    QLockFile lock(path(session, QStringLiteral("ppk/processing.lock")));
    lock.setStaleLockTime(0);
    if (!lock.tryLock())
    {
        if (error)
            *error = QStringLiteral("SESSION_PPK_BUSY");
        return false;
    }
    for (const QString &relative : {QStringLiteral("ppk/solution.pos"), QStringLiteral("ppk/trajectory.csv"),
                                    QStringLiteral("ppk/ppk_quality.json")})
        if (QFileInfo::exists(path(session, relative)) && !QFile::remove(path(session, relative)))
        {
            if (error)
                *error = QStringLiteral("PPK_RESULT_REMOVE_FAILED");
            return false;
        }
    auto object = readJson(path(session, QStringLiteral("ppk/ppk_config.json")));
    object.insert("navigation_source", "Original");
    return writeJson(path(session, QStringLiteral("ppk/ppk_config.json")), object, error);
}

PpkProcessResult PpkProcessor::process(const QString &session, const PpkConfig &requested,
                                       const std::atomic_bool *cancel, const Progress &progress)
{
    PpkProcessResult result;
    PpkConfig config = requested;
    QDir().mkpath(path(session, QStringLiteral("ppk")));
    QLockFile sessionLock(path(session, QStringLiteral("ppk/processing.lock")));
    sessionLock.setStaleLockTime(0);
    if (!sessionLock.tryLock())
    {
        result.error = QStringLiteral("SESSION_PPK_BUSY");
        publishPpkLog(session, QStringLiteral("ppk_processing_failed"), result.error);
        return result;
    }
    auto fail = [&](const QString &reason)
    {
        result.error = reason;
        result.cancelled = (cancel && cancel->load()) || reason == QStringLiteral("CANCELLED");
        writeJson(path(session, QStringLiteral("ppk/ppk_quality.json")),
                  {{"state", "Failed"}, {"error", reason}, {"cancelled", result.cancelled}});
        publishPpkLog(session, QStringLiteral("ppk_processing_failed"), reason);
        return result;
    };
    if (!std::isfinite(config.elevationMaskDeg) || config.elevationMaskDeg < 0 || config.elevationMaskDeg >= 90 ||
        config.receiver < 0 || config.receiver > 255 || config.frequencies < 1 || config.frequencies > NFREQ ||
        !config.constellations)
        return fail(QStringLiteral("INVALID_PPK_CONFIG"));
    for (double arm : config.imuToAntennaBodyM)
        if (!std::isfinite(arm))
            return fail(QStringLiteral("INVALID_LEVER_ARM"));
    if (cancel && cancel->load())
        return fail(QStringLiteral("CANCELLED"));
    if (config.baseObs.isEmpty() || config.navigationFiles.isEmpty())
        return fail(QStringLiteral("MISSING_BASE_OR_NAVIGATION"));
    if (!writeJson(path(session, QStringLiteral("ppk/ppk_quality.json")), {{"state", "Processing"}}, &result.error))
        return fail(result.error);
    publishPpkLog(session, QStringLiteral("ppk_processing_started"), QStringLiteral("OK"));
    if (config.roverObs.isEmpty() || QFileInfo(ObservationStore::filename(session)).size() > 12)
    {
        config.roverObs = QStringLiteral("ppk/rover/rover.obs");
        const auto rinex =
            EpsilonRinexWriter::write(ObservationStore::filename(session), path(session, config.roverObs),
                                      static_cast<uint8_t>(config.receiver), cancel);
        if (!rinex.success)
            return fail(rinex.error);
        publishPpkLog(session, QStringLiteral("ppk_rinex_generated"), QStringLiteral("OK"),
                      {{"epochs", qulonglong(rinex.epochs)}});
    }
    if (!saveConfig(session, config, &result.error))
        return fail(result.error);
    std::vector<AttitudeSample> attitudes;
    if (!AttitudeStore::read(session, attitudes, &result.error))
        return fail(QStringLiteral("MISSING_GNSS_UTC_ATTITUDE"));
    PpkTimeAlignment alignment(std::move(attitudes));
    // Qt handles Unicode Session paths; RTKLIB receives private bounded paths.
    QTemporaryDir work(QDir::tempPath() + QStringLiteral("/vv-ppk-XXXXXX"));
    if (!work.isValid())
        return fail(QStringLiteral("PPK_WORK_DIRECTORY_FAILED"));
    QStringList inputs{config.roverObs, config.baseObs};
    inputs.append(config.navigationFiles);
    std::vector<QByteArray> names;
    names.reserve(inputs.size());
    for (int i = 0; i < inputs.size(); ++i)
    {
        const auto relative = safeRelative(inputs[i]);
        if (relative.isEmpty())
            return fail(QStringLiteral("INVALID_INPUT_PATH"));
        const auto local = work.filePath(QStringLiteral("input%1.rnx").arg(i));
        if (!QFile::copy(path(session, relative), local))
            return fail(QStringLiteral("PPK_INPUT_COPY_FAILED"));
        const auto name = QFile::encodeName(QDir::toNativeSeparators(local));
        if (name.size() >= MAXSTRPATH || name.contains('\0'))
            return fail(QStringLiteral("RTKLIB_PATH_TOO_LONG"));
        names.push_back(name);
    }
    std::vector<char *> files;
    for (auto &name : names)
        files.push_back(name.data());
    const QString solutionFile = work.filePath(QStringLiteral("solution.pos"));
    auto solutionName = QFile::encodeName(solutionFile);
    prcopt_t options = prcopt_default;
    options.mode = PMODE_KINEMA;
    options.soltype = 0;
    options.nf = config.frequencies;
    options.navsys = config.constellations;
    options.elmin = config.elevationMaskDeg * D2R;
    options.refpos = POSOPT_RINEX;
    options.ionoopt = IONOOPT_BRDC;
    options.tropopt = TROPOPT_SAAS;
    solopt_t solution = solopt_default;
    solution.posf = SOLF_XYZ;
    solution.times = TIMES_GPST;
    solution.timef = 1;
    solution.timeu = 9;
    solution.outhead = 1;
    solution.outopt = 1;
    solution.sstat = 0;
    solution.trace = 0;
    filopt_t auxiliary{};
    int code;
    {
        std::unique_lock<std::timed_mutex> guard(postposMutex, std::defer_lock);
        while (!guard.try_lock_for(std::chrono::milliseconds(100)))
            if (cancel && cancel->load())
                return fail(QStringLiteral("CANCELLED"));
        if (cancel && cancel->load())
            return fail(QStringLiteral("CANCELLED"));
        processingCancel = cancel;
        processingProgress = progress;
        processingMessage.clear();
        processingStart = processingEnd = 0;
        processingPercent = -1;
        code = postpos({}, {}, 0, 0, &options, &solution, &auxiliary, files.data(), int(files.size()),
                       solutionName.data(), "", "");
        processingCancel = nullptr;
        processingProgress = {};
    }
    if (cancel && cancel->load())
        return fail(QStringLiteral("CANCELLED"));
    if (code < 0)
        return fail(QStringLiteral("RTKLIB_POSTPOS_FAILED"));
    if (!loadSolution(solutionFile, result.trajectory, result.error))
        return fail(result.error + QStringLiteral(": ") + processingMessage);
    for (auto &sample : result.trajectory)
        if (!alignment.correctToImu(sample, config.imuToAntennaBodyM, &result.error))
            return fail(result.error);
    int fixed = 0, floating = 0;
    double varianceN = 0, varianceE = 0, varianceU = 0;
    for (const auto &sample : result.trajectory)
    {
        fixed += sample.quality == 1;
        floating += sample.quality == 2;
        varianceN += sample.sdN * sample.sdN;
        varianceE += sample.sdE * sample.sdE;
        varianceU += sample.sdU * sample.sdU;
    }
    const double count = double(result.trajectory.size());
    result.quality = {{"state", "Completed"},
                      {"sample_count", int(result.trajectory.size())},
                      {"fix_count", fixed},
                      {"float_count", floating},
                      {"fix_percent", 100 * fixed / count},
                      {"float_percent", 100 * floating / count},
                      {"start_utc_ns", QString::number(result.trajectory.front().utcNs)},
                      {"end_utc_ns", QString::number(result.trajectory.back().utcNs)},
                      {"rms_sd_n", std::sqrt(varianceN / count)},
                      {"rms_sd_e", std::sqrt(varianceE / count)},
                      {"rms_sd_u", std::sqrt(varianceU / count)},
                      {"reference_point", "IMU"},
                      {"processed_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                      {"solution_file", "ppk/solution.pos"},
                      {"trajectory_file", "ppk/trajectory.csv"}};
    if (!archive(session, solutionFile, QStringLiteral("ppk/solution.pos"), &result.error) ||
        !writePpkTrajectory(path(session, QStringLiteral("ppk/trajectory.csv")), result.trajectory, &result.error) ||
        !writeJson(path(session, QStringLiteral("ppk/ppk_quality.json")), result.quality, &result.error))
        return fail(result.error);
    result.success = true;
    publishPpkLog(session, QStringLiteral("ppk_processing_completed"), QStringLiteral("OK"),
                  {{"sample_count", int(result.trajectory.size())}, {"fix_count", fixed}, {"float_count", floating}});
    return result;
}
} // namespace VaporView::Ppk
