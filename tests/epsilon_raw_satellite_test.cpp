#include "EpsilonRawSatellite.h"
#include "ppk/ObservationStore.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace
{
void require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
template <typename T> void put(QByteArray &bytes, int offset, T value)
{
    std::memcpy(bytes.data() + offset, &value, sizeof(T));
}
QByteArray payload(int system = 1, int frequencyCount = 1)
{
    QByteArray p(22 + 26 * frequencyCount, '\0');
    put<quint32>(p, 0, 1700000000);
    put<quint32>(p, 4, 123456789);
    put<qint32>(p, 8, -125);
    p[12] = 1;
    p[13] = 0;
    p[14] = 1;
    p[15] = 1;
    p[16] = char(system);
    p[17] = 7;
    p[18] = 45;
    put<quint16>(p, 19, 270);
    p[21] = char(frequencyCount);
    for (int i = 0; i < frequencyCount; ++i)
    {
        const int offset = 22 + 26 * i;
        p[offset] = char(i ? 4 : 0);
        p[offset + 1] = char(0xA5);
        put<double>(p, offset + 2, 123456789.125 + i);
        put<double>(p, offset + 10, 23456789.75 + i);
        put<float>(p, offset + 18, -1234.5f);
        put<float>(p, offset + 22, 47.25f);
    }
    return p;
}
QByteArray frame(const QByteArray &p)
{
    QByteArray f(8 + p.size(), '\0');
    f[0] = char(0xFC);
    f[1] = char(0x77);
    f[2] = char(p.size());
    f[3] = 17;
    quint8 crc8 = 0;
    for (int i = 0; i < 4; ++i)
    {
        crc8 ^= quint8(f[i]);
        for (int j = 0; j < 8; ++j)
            crc8 = (crc8 & 1) ? (crc8 >> 1) ^ 0x8C : crc8 >> 1;
    }
    quint16 crc16 = 0;
    for (char byte : p)
    {
        crc16 ^= quint16(quint8(byte)) << 8;
        for (int j = 0; j < 8; ++j)
            crc16 = (crc16 & 0x8000) ? (crc16 << 1) ^ 0x1021 : crc16 << 1;
    }
    f[4] = char(crc8);
    f[5] = char(crc16 >> 8);
    f[6] = char(crc16);
    std::memcpy(f.data() + 7, p.data(), p.size());
    f[f.size() - 1] = char(0xFD);
    return f;
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace VaporView::Ppk;
    for (int system : {1, 2, 3, 4, 6})
    {
        for (int count : {1, 2, 3})
        {
            // Use distinct frequency identifiers for the third observation.
            auto p = payload(system, count);
            if (count == 3)
                p[74] = 7;
            const auto f = frame(p);
            RawSatellitePacket packet;
            std::string reason;
            require(decodeRawSatelliteFrame(reinterpret_cast<const quint8 *>(f.constData()), f.size(), packet, reason),
                    "valid frame");
            require(packet.epoch.unixSeconds == 1700000000 && packet.epoch.nanoseconds == 123456789 &&
                        packet.epoch.receiverClockOffsetUs == -125 && packet.epoch.receiver == 1,
                    "UTC and receiver fields");
            require(packet.epoch.observations.size() == count, "multi frequency count");
            const auto &o = packet.epoch.observations.back();
            require(o.system == system && o.prn == 7 && o.elevationDeg == 45 && o.azimuthDeg == 270 &&
                        o.trackingStatus == 0xA5,
                    "satellite fields");
            require(o.carrierPhaseCycles == 123456789.125 + count - 1 && o.pseudoRangeM == 23456789.75 + count - 1 &&
                        o.dopplerHz == -1234.5f && o.snrDbHz == 47.25f,
                    "exact binary observation values");
            auto bad = f;
            bad[bad.size() - 2] ^= 1;
            require(
                !decodeRawSatelliteFrame(reinterpret_cast<const quint8 *>(bad.constData()), bad.size(), packet, reason),
                "bad CRC rejected");
        }
    }
    auto p = payload();
    RawSatellitePacket packet;
    std::string reason;
    require(decodeRawSatellitePayload(reinterpret_cast<const quint8 *>(p.constData()), p.size(), packet, reason),
            "decode packet");
    std::vector<std::string> events;
    RawSatelliteAssembler assembler([&](const std::string &event, const std::string &, quint8, quint64, int, int)
                                    { events.push_back(event); });
    require(bool(assembler.accept(packet)), "single packet epoch");
    require(!assembler.accept(packet), "completed duplicate ignored");
    ++packet.epoch.unixSeconds;
    packet.totalPackets = 3;
    packet.packetNumber = 2;
    require(!assembler.accept(packet), "out of order first");
    require(!assembler.accept(packet), "duplicate ignored");
    packet.packetNumber = 0;
    packet.epoch.observations[0].prn = 8;
    require(!assembler.accept(packet), "missing packet detected");
    packet.packetNumber = 1;
    packet.epoch.observations[0].prn = 9;
    auto e = assembler.accept(packet);
    require(e && e->observations.size() == 3, "complete out of order epoch");
    ++packet.epoch.unixSeconds;
    packet.epoch.receiver = 2;
    packet.totalPackets = 2;
    packet.packetNumber = 1;
    require(!assembler.accept(packet), "receiver 2 separate");
    packet.packetNumber = 2;
    packet.epoch.observations[0].prn = 10;
    require(bool(assembler.accept(packet)), "one based packets");
    ++packet.epoch.unixSeconds;
    packet.packetNumber = 1;
    require(!assembler.accept(packet), "conflict epoch starts incomplete");
    packet.packetNumber = 2;
    ++packet.epoch.receiverClockOffsetUs;
    require(!assembler.accept(packet), "conflicting epoch header rejected");
    --packet.epoch.receiverClockOffsetUs;
    require(!assembler.accept(packet), "conflicted epoch stays rejected after a retry");
    ++packet.epoch.unixSeconds;
    packet.packetNumber = 1;
    require(!assembler.accept(packet), "incomplete epoch");
    ++packet.epoch.unixSeconds;
    require(!assembler.accept(packet), "new epoch discards incomplete");
    --packet.epoch.unixSeconds;
    require(!assembler.accept(packet), "time regression ignored");
    assembler.flush();
    require(std::find(events.begin(), events.end(), "epsilon_raw_satellite_epoch_incomplete") != events.end(),
            "incomplete event");
    require(std::find(events.begin(), events.end(), "epsilon_raw_satellite_time_regression") != events.end(),
            "regression event");

    QTemporaryDir session;
    require(session.isValid(), "temporary session");
    ObservationStore store;
    require(store.append(session.path(), *e), "stream observation");
    store.close();
    auto read = ObservationStore::read(
        ObservationStore::filename(session.path()),
        [&](const RawSatelliteEpoch &loaded)
        {
            require(loaded.utcNanoseconds() == e->utcNanoseconds() && loaded.observations.size() == 3,
                    "reopen exact epoch");
            require(loaded.observations[0].carrierPhaseCycles == e->observations[0].carrierPhaseCycles,
                    "reopen exact phase");
            return true;
        });
    require(read.success && read.epochs == 1, "read stored session");
    QFile file(ObservationStore::filename(session.path()));
    require(file.open(QIODevice::Append), "append crash tail");
    file.write("\x25\0", 2);
    file.close();
    read = ObservationStore::read(file.fileName(), [](const RawSatelliteEpoch &) { return true; });
    require(read.success && read.recoveredTail && read.epochs == 1, "crash recovery keeps complete epochs");
    return 0;
}
