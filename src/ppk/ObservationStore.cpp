#include "ppk/ObservationStore.h"
#include "LogService.h"

#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtEndian>
#include <cmath>

namespace VaporView::Ppk
{
namespace
{
constexpr quint32 kMaxRecordSize = 128 * 1024;
const QByteArray kHeader("VVPPKOBS\x01\0\0\0", 12);
void prepare(QDataStream &stream)
{
    stream.setVersion(QDataStream::Qt_6_0);
    stream.setByteOrder(QDataStream::LittleEndian);
}
} // namespace

QByteArray encodeEpoch(const RawSatelliteEpoch &e)
{
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    prepare(out);
    out << quint32(e.unixSeconds) << quint32(e.nanoseconds) << qint32(e.receiverClockOffsetUs) << quint8(e.receiver)
        << quint64(e.hostTimestampUs) << quint32(e.observations.size());
    for (const auto &o : e.observations)
        out << quint8(o.system) << quint8(o.prn) << quint8(o.elevationDeg) << quint16(o.azimuthDeg)
            << quint8(o.frequency) << quint8(o.trackingStatus) << o.carrierPhaseCycles << o.pseudoRangeM
            << double(o.dopplerHz) << double(o.snrDbHz);
    return bytes;
}

bool decodeEpoch(const QByteArray &bytes, RawSatelliteEpoch &e)
{
    QDataStream in(bytes);
    prepare(in);
    RawSatelliteEpoch next;
    quint32 seconds, nanos, count;
    qint32 offset;
    quint8 receiver;
    quint64 host;
    in >> seconds >> nanos >> offset >> receiver >> host >> count;
    if (in.status() != QDataStream::Ok || nanos >= 1000000000 || !seconds || count > 3000)
        return false;
    next.unixSeconds = seconds;
    next.nanoseconds = nanos;
    next.receiverClockOffsetUs = offset;
    next.receiver = receiver;
    next.hostTimestampUs = host;
    next.observations.reserve(count);
    for (quint32 i = 0; i < count; ++i)
    {
        RawSatelliteObservation o;
        quint8 system, prn, elevation, frequency, tracking;
        quint16 azimuth;
        double doppler, snr;
        in >> system >> prn >> elevation >> azimuth >> frequency >> tracking >> o.carrierPhaseCycles >>
            o.pseudoRangeM >> doppler >> snr;
        o.system = system;
        o.prn = prn;
        o.elevationDeg = elevation;
        o.azimuthDeg = azimuth;
        o.frequency = frequency;
        o.trackingStatus = tracking;
        o.dopplerHz = float(doppler);
        o.snrDbHz = float(snr);
        next.observations.push_back(o);
    }
    if (in.status() != QDataStream::Ok || !in.atEnd())
        return false;
    for (const auto &o : next.observations)
        if (!std::isfinite(o.carrierPhaseCycles) || !std::isfinite(o.pseudoRangeM) || !std::isfinite(o.dopplerHz) ||
            !std::isfinite(o.snrDbHz))
            return false;
    e = std::move(next);
    return true;
}

QString ObservationStore::filename(const QString &session)
{
    return QDir(session).filePath(QStringLiteral("ppk/rover/observations.bin"));
}

bool ObservationStore::append(const QString &session, const RawSatelliteEpoch &e, QString *error)
{
    auto fail = [&](const QString &message)
    {
        if (error)
            *error = message;
        LogService::withCurrentInstance(
            [&](LogService &log)
            {
                log.publish(LogLevel::Error, QStringLiteral("ppk"), QStringLiteral("session.ppk"),
                            QStringLiteral("PPK Rover 原始观测写入失败。"),
                            {{"event", "ppk_rover_observation_failed"},
                             {"reason_code", "OBSERVATION_WRITE_FAILED"},
                             {"error", message},
                             {"session", session}},
                            {}, session);
            });
        return false;
    };
    const QString path = filename(session);
    if (file_.isOpen() && file_.fileName() != path)
        close();
    if (!file_.isOpen())
    {
        if (!QDir().mkpath(QFileInfo(path).absolutePath()))
            return fail(QStringLiteral("PPK_DIRECTORY_FAILED"));
        file_.setFileName(path);
        if (!file_.open(QIODevice::WriteOnly | QIODevice::Append))
            return fail(file_.errorString());
        count_ = 0;
        firstUtcNs_ = 0;
        lastUtcNs_ = 0;
        if (file_.size() == 0 && file_.write(kHeader) != kHeader.size())
            return fail(file_.errorString());
        LogService::withCurrentInstance(
            [&](LogService &log)
            {
                log.publish(LogLevel::Info, QStringLiteral("ppk"), QStringLiteral("session.ppk"),
                            QStringLiteral("开始记录 PPK Rover 原始观测。"),
                            {{"event", "ppk_rover_observation_started"}, {"reason_code", "OK"}, {"session", session}},
                            {}, session);
            });
    }
    const auto bytes = encodeEpoch(e);
    QDataStream out(&file_);
    prepare(out);
    out << quint32(bytes.size()) << quint16(qChecksum(bytes));
    if (file_.write(bytes) != bytes.size() || out.status() != QDataStream::Ok || !file_.flush())
        return fail(file_.errorString());
    ++count_;
    if (!firstUtcNs_)
        firstUtcNs_ = e.utcNanoseconds();
    lastUtcNs_ = e.utcNanoseconds();
    return true;
}

void ObservationStore::close()
{
    if (!file_.isOpen())
        return;
    QSaveFile metadata(QFileInfo(file_).dir().filePath(QStringLiteral("metadata.json")));
    if (metadata.open(QIODevice::WriteOnly))
    {
        const QJsonObject object{{"format", "VVPPKOBS"},
                                 {"version", 1},
                                 {"epoch_count", QString::number(count_)},
                                 {"first_utc_ns", QString::number(firstUtcNs_)},
                                 {"last_utc_ns", QString::number(lastUtcNs_)},
                                 {"time_scale", "UTC"},
                                 {"reference_point", "MAIN_ANTENNA"},
                                 {"tracking_status_policy", "Preserved verbatim; no undocumented LLI bits inferred"}};
        metadata.write(QJsonDocument(object).toJson());
        metadata.commit();
    }
    file_.flush();
    file_.close();
    const QString session = QFileInfo(file_).dir().absolutePath() + QStringLiteral("/../..");
    LogService::withCurrentInstance(
        [&](LogService &log)
        {
            log.publish(LogLevel::Info, QStringLiteral("ppk"), QStringLiteral("session.ppk"),
                        QStringLiteral("完成 PPK Rover 原始观测记录。"),
                        {{"event", "ppk_rover_observation_completed"},
                         {"reason_code", "OK"},
                         {"session", QDir::cleanPath(session)},
                         {"epoch_count", qulonglong(count_)},
                         {"first_utc_ns", QString::number(firstUtcNs_)},
                         {"last_utc_ns", QString::number(lastUtcNs_)}},
                        {}, QDir::cleanPath(session));
        });
}

ObservationReadResult ObservationStore::read(const QString &path,
                                             const std::function<bool(const RawSatelliteEpoch &)> &consume)
{
    ObservationReadResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        result.error = file.errorString();
        return result;
    }
    if (file.read(12) != kHeader)
    {
        result.error = QStringLiteral("INVALID_OBSERVATION_HEADER");
        return result;
    }
    QDataStream in(&file);
    prepare(in);
    while (!file.atEnd())
    {
        if (file.bytesAvailable() < 6)
        {
            result.recoveredTail = true;
            break;
        }
        quint32 size;
        quint16 checksum;
        in >> size >> checksum;
        if (size > kMaxRecordSize || size < 25)
        {
            result.error = QStringLiteral("INVALID_OBSERVATION_LENGTH");
            return result;
        }
        if (file.bytesAvailable() < size)
        {
            result.recoveredTail = true;
            break;
        }
        const auto bytes = file.read(size);
        RawSatelliteEpoch e;
        if (qChecksum(bytes) != checksum || !decodeEpoch(bytes, e))
        {
            result.error = QStringLiteral("OBSERVATION_CHECKSUM_FAILED");
            return result;
        }
        if (!consume(e))
        {
            result.error = QStringLiteral("OBSERVATION_CONSUMER_STOPPED");
            return result;
        }
        ++result.epochs;
    }
    result.success = true;
    return result;
}
} // namespace VaporView::Ppk
