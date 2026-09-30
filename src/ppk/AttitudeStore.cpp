#include "ppk/AttitudeStore.h"
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QtEndian>
#include <cstring>
#include <cmath>

namespace VaporView::Ppk
{
namespace
{
QString filename(const QString &session)
{
    return QDir(session).filePath(QStringLiteral("ppk/rover/attitudes.bin"));
}
float floatLE(const char *p)
{
    quint32 bits = qFromLittleEndian<quint32>(p);
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}
const QByteArray header("VVPPKATT\x01\0\0\0", 12);
} // namespace
bool AttitudeStore::appendSystemState(const QString &session, quint64 hostUs, const QByteArray &frame)
{
    if (frame.size() < 86 || quint8(frame[1]) != 0x50)
        return true;
    const char *payload = frame.constData() + 7;
    const quint32 seconds = qFromLittleEndian<quint32>(payload + 6), micros = qFromLittleEndian<quint32>(payload + 10);
    const double roll = floatLE(payload + 66), pitch = floatLE(payload + 70), yaw = floatLE(payload + 74);
    if (!seconds || micros >= 1000000 || !std::isfinite(roll) || !std::isfinite(pitch) || !std::isfinite(yaw))
        return true;
    const QString path = filename(session);
    if (file_.isOpen() && file_.fileName() != path)
        close();
    if (!file_.isOpen())
    {
        if (!QDir().mkpath(QFileInfo(path).absolutePath()))
            return false;
        file_.setFileName(path);
        if (!file_.open(QIODevice::Append))
            return false;
        if (file_.size() == 0 && file_.write(header) != header.size())
            return false;
    }
    QDataStream out(&file_);
    out.setByteOrder(QDataStream::LittleEndian);
    out.setVersion(QDataStream::Qt_6_0);
    out << quint64(seconds) * 1000000000ULL + quint64(micros) * 1000ULL << hostUs;
    for (double q : quaternionFromEuler(roll, pitch, yaw))
        out << q;
    return out.status() == QDataStream::Ok;
}
void AttitudeStore::close()
{
    if (file_.isOpen())
    {
        file_.flush();
        file_.close();
    }
}
bool AttitudeStore::read(const QString &session, std::vector<AttitudeSample> &samples, QString *error)
{
    QFile file(filename(session));
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (file.read(12) != header)
    {
        if (error)
            *error = QStringLiteral("INVALID_ATTITUDE_HEADER");
        return false;
    }
    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);
    in.setVersion(QDataStream::Qt_6_0);
    samples.clear();
    while (file.bytesAvailable() >= 48)
    {
        AttitudeSample sample;
        quint64 utc, host;
        in >> utc >> host;
        sample.utcNs = utc;
        sample.hostUs = host;
        for (double &q : sample.bodyToNed)
            in >> q;
        if (in.status() != QDataStream::Ok)
        {
            if (error)
                *error = QStringLiteral("ATTITUDE_READ_FAILED");
            return false;
        }
        samples.push_back(sample);
    }
    return !samples.empty();
}
} // namespace VaporView::Ppk
