#include "ppk/PpkProcessor.h"
#include "ppk/EpsilonRinexWriter.h"
#include "ppk/ObservationStore.h"
#include "ppk/AttitudeStore.h"
#include "ppk/SessionNavigationSource.h"
#include "data_collector.h"
#include "SkySessionRecorder.h"
#include "ground/session/GroundRecordingService.h"
#include "ground/session/SessionLoader.h"
#include "ground/session/SessionTrajectoryRenderLoader.h"
#include "ground/session/SessionExportService.h"
#include "geo/SessionTrackReader.h"
#include <QDateTime>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <memory>
#include "rtklib.h"
#undef lock
#undef unlock

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
} // namespace
int runTest(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace VaporView::Ppk;
    // Independent axes and wrap tests use an equator/prime-meridian antenna.
    constexpr double pi = 3.14159265358979323846;
    std::vector<AttitudeSample> attitude{{1000000000, 9000000, quaternionFromEuler(0, 0, 0)},
                                         {2000000000, 10000000, quaternionFromEuler(0, 0, pi / 2)}};
    PpkTimeAlignment alignment(attitude);
    auto sample = []()
    {
        PpkSample s;
        s.utcNs = 1000000000;
        s.ecef = {6378137, 0, 0};
        return s;
    };
    auto s = sample();
    require(alignment.correctToImu(s, {0, 0, 0}), "zero lever arm");
    require(s.ecef[0] == 6378137 && s.sessionUs == 9000000, "zero arm and independent clock");
    s = sample();
    require(alignment.correctToImu(s, {2, 0, 0}), "horizontal lever arm");
    require(std::abs(s.ecef[2] + 2) < 1e-8, "north offset subtracts from antenna");
    s = sample();
    require(alignment.correctToImu(s, {0, 0, 3}), "vertical lever arm");
    require(std::abs(s.ecef[0] - 6378140) < 1e-8, "body down offset");
    s = sample();
    s.utcNs = 2000000000;
    require(alignment.correctToImu(s, {2, 0, 0}), "yaw90 correction");
    require(std::abs(s.ecef[1] + 2) < 1e-8, "yaw90 points east");
    const auto mid = alignment.atUtc(1500000000);
    require(mid && mid->hostUs == 9500000, "UTC/session interpolation");
    require(alignment.utcForSession(9500000) == 1500000000, "inverse UTC/session alignment");
    PpkTimeAlignment wrap({{1000000000, 9000000, quaternionFromEuler(0, 0, 179 * pi / 180)},
                           {2000000000, 10000000, quaternionFromEuler(0, 0, -179 * pi / 180)}});
    const auto wrapped = wrap.atUtc(1500000000);
    require(wrapped && std::abs(wrapped->bodyToNed[0]) < 0.00001, "yaw wrap shortest slerp");
    require(!alignment.atUtc(3000000000), "no extrapolated attitude");

    QTemporaryDir multi;
    ObservationStore multiStore;
    RawSatelliteEpoch multiEpoch;
    multiEpoch.unixSeconds = 1700000000;
    multiEpoch.nanoseconds = 123456789;
    multiEpoch.receiver = 1;
    for (uint8_t system : {uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(4), uint8_t(6)})
        for (uint8_t frequency : {uint8_t(0), uint8_t(4), uint8_t(7), uint8_t(8)})
        {
            if (!epsilonRinexSignal(system, frequency).code)
                continue;
            RawSatelliteObservation o;
            o.system = system;
            o.prn = 1;
            o.frequency = frequency;
            o.pseudoRangeM = 23000000.125;
            o.carrierPhaseCycles = 12345678.625;
            o.dopplerHz = -1234.5f;
            o.snrDbHz = 45.25f;
            multiEpoch.observations.push_back(o);
        }
    require(multiStore.append(multi.path(), multiEpoch), "multi constellation observation fixture");
    multiStore.close();
    const QString multiObs = multi.filePath("rover.obs");
    require(EpsilonRinexWriter::write(ObservationStore::filename(multi.path()), multiObs).success,
            "multi constellation RINEX writer");
    obs_t multiReload{};
    auto multiNav = std::make_unique<nav_t>();
    sta_t multiStation{};
    const auto multiName = QFile::encodeName(QDir::toNativeSeparators(multiObs));
    require(readrnxt(multiName.constData(), 1, {}, {}, 0, "", &multiReload, multiNav.get(), &multiStation) > 0 &&
                multiReload.n == 5,
            "RTKLIB reads GPS GLO BDS GAL QZSS");
    int readSignals = 0;
    for (int i = 0; i < multiReload.n; ++i)
        for (int f = 0; f < NFREQ + NEXOBS; ++f)
        {
            if (!multiReload.data[i].code[f])
                continue;
            ++readSignals;
            require(std::abs(multiReload.data[i].P[f] - 23000000.125) < 0.001 &&
                        std::abs(multiReload.data[i].L[f] - 12345678.625) < 0.001 &&
                        std::abs(multiReload.data[i].D[f] + 1234.5) < 0.001,
                    "RINEX C/L/D retain units and precision");
        }
    require(readSignals == int(multiEpoch.observations.size()), "all mapped frequencies survive RINEX round trip");
    freeobs(&multiReload);
    freenav(multiNav.get(), 0xFF);

    QTemporaryDir root;
    require(root.isValid(), "temporary session");
    VaporView::SkySessionRecorder recorder;
    require(recorder.start(root.path() + "/sky", "fixture", 921600), "start formal Sky recorder");
    const QString session = recorder.sessionDirectory();
    VaporView::Ground::Session::GroundRecordingService groundRecorder;
    VaporView::Ground::Session::GroundRecordingOptions recordingOptions;
    recordingOptions.baseDirectory = root.path() + "/ground";
    require(groundRecorder.start(recordingOptions), "start formal Local recorder");
    const QString groundSession = groundRecorder.status().sessionDirectory;
    VaporView::EpsilonCollector collector;
    RawSatelliteAssembler assembler;
    quint64 packetCount = 0;
    collector.setRawFrameCallback(
        [&](uint64_t host, uint8_t id, uint8_t serial, const uint8_t *data, size_t size)
        {
            const QByteArray frame(reinterpret_cast<const char *>(data), size);
            recorder.recordRawEpsilonFrame(host, id, serial, frame);
            require(groundRecorder.recordRawEpsilonFrame(host, id, serial, data, size), "local raw frame queue");
            ++packetCount;
        });
    collector.setRawSatelliteEpochCallback(
        [&](const RawSatelliteEpoch &epoch)
        {
            recorder.recordEpsilonObservationEpoch(encodeEpoch(epoch));
            require(groundRecorder.recordEpsilonObservationEpoch(epoch), "local observation queue");
        });
    const QString fixture = QStringLiteral(VAPORVIEW_SOURCE_DIR "/tests/fixtures/ppk/");
    obs_t observations{};
    auto nav = std::make_unique<nav_t>();
    sta_t station{};
    const auto input = QFile::encodeName(QDir::toNativeSeparators(fixture + QStringLiteral("rover.obs")));
    require(readrnxt(input.constData(), 1, {}, {}, 0, "", &observations, nav.get(), &station) > 0,
            "read real rover fixture");
    quint64 count = 0, firstUtcNs = 0, lastUtcNs = 0;
    const quint64 hostStart = recorder.recordingStartTimeUs() + 1000000;
    for (int begin = 0; begin < observations.n;)
    {
        const auto time = observations.data[begin].time;
        const auto utc = gpst2utc(time);
        RawSatelliteEpoch epoch;
        epoch.unixSeconds = quint32(utc.time);
        epoch.nanoseconds = quint32(std::llround(utc.sec * 1e9));
        epoch.receiver = 1;
        int end = begin;
        while (end < observations.n && std::abs(timediff(observations.data[end].time, time)) < 0.0001)
        {
            const auto &obs = observations.data[end++];
            int prn = 0;
            const int sys = satsys(obs.sat, &prn);
            if (sys != SYS_GPS)
                continue;
            for (int frequency = 0; frequency < NFREQ + NEXOBS; ++frequency)
            {
                if (!obs.P[frequency] || !obs.L[frequency])
                    continue;
                const char *code = code2obs(obs.code[frequency]);
                const int index = code[0] == '1' ? 0 : code[0] == '2' ? 5 : -1;
                if (index < 0)
                    continue;
                RawSatelliteObservation o;
                o.system = 1;
                o.prn = uint8_t(prn);
                o.frequency = uint8_t(index);
                o.carrierPhaseCycles = obs.L[frequency];
                o.pseudoRangeM = obs.P[frequency];
                o.dopplerHz = obs.D[frequency];
                o.snrDbHz = obs.SNR[frequency] * SNR_UNIT;
                epoch.observations.push_back(o);
            }
        }
        if (!firstUtcNs)
            firstUtcNs = epoch.utcNanoseconds();
        lastUtcNs = epoch.utcNanoseconds();
        epoch.hostTimestampUs = hostStart + (epoch.utcNanoseconds() - firstUtcNs) / 1000;
        const int packets = int((epoch.observations.size() + 6) / 7);
        for (int number = packets - 1; number >= 0; --number)
        {
            RawSatellitePacket packet;
            packet.epoch = epoch;
            packet.epoch.observations.assign(epoch.observations.begin() + number * 7,
                                             epoch.observations.begin() +
                                                 (std::min)(size_t((number + 1) * 7), epoch.observations.size()));
            packet.packetNumber = uint8_t(number);
            packet.totalPackets = uint8_t(packets);
            const auto frame = encodeRawSatelliteFrame(packet, uint8_t(packetCount));
            collector.consumeRawSatelliteFrame(frame.data(), frame.size(), epoch.hostTimestampUs, assembler);
        }
        VaporView::EpsilonData original;
        original.valid = true;
        original.latitude_deg = 30.25;
        original.longitude_deg = 120.15;
        original.height_m = 42.5;
        original.gnss_fix_text = "RTK_FIXED";
        original.utc_unix_s = epoch.unixSeconds;
        original.utc_microseconds = epoch.nanoseconds / 1000;
        VaporView::PtbData pressure;
        pressure.valid = true;
        pressure.pressure_hpa = 1012.3;
        VaporView::HmpData humidity;
        humidity.valid = true;
        humidity.temperature = 23.4;
        humidity.humidity = 45.6;
        recorder.recordDeviceSnapshot(epoch.hostTimestampUs, epoch.hostTimestampUs, original, true, pressure, true,
                                      humidity, true, {}, false);
        begin = end;
        ++count;
    }
    // Simulated 5 Hz device attitude covers clock-corrected RTKLIB solution times.
    // RTKLIB can offset sol.time from the raw observation by the receiver clock bias.
    for (quint64 utcNs = firstUtcNs - 1000000000ULL; utcNs <= lastUtcNs + 1000000000ULL; utcNs += 200000000ULL)
    {
        QByteArray frame(110, '\0');
        frame[0] = char(0xFC);
        frame[1] = 0x50;
        frame[2] = 102;
        frame[109] = char(0xFD);
        put<quint32>(frame, 13, quint32(utcNs / 1000000000ULL));
        put<quint32>(frame, 17, quint32(utcNs % 1000000000ULL / 1000));
        const quint64 host = quint64(qint64(hostStart) + (qint64(utcNs) - qint64(firstUtcNs)) / 1000);
        recorder.recordRawEpsilonFrame(host, 0x50, 0, frame);
        require(groundRecorder.recordRawEpsilonFrame(host, 0x50, 0, frame.constData(), frame.size()),
                "local attitude queue");
    }
    require(recorder.stop(), "stop formal Sky Session");
    groundRecorder.stop();
    freeobs(&observations);
    freenav(nav.get(), 0xFF);
    require(recorder.rawNavigationRecordCount() >= packetCount && packetCount > count,
            "raw 0x77 multi-packet frames retained");
    const auto localReopen = ObservationStore::read(ObservationStore::filename(groundSession),
                                                    [](const RawSatelliteEpoch &) { return true; });
    require(localReopen.success && localReopen.epochs == count, "Local observations survive Session close");
    QFile rawFile(QDir(session).filePath("raw/navigation.dat"));
    require(rawFile.open(QIODevice::ReadOnly) && rawFile.size() > packetCount * 48, "formal raw navigation.dat exists");
    require(count > 0, "real observation epochs");
    const auto rinex =
        EpsilonRinexWriter::write(ObservationStore::filename(session), QDir(session).filePath("ppk/rover/rover.obs"));
    require(rinex.success && rinex.epochs == count, "generate RINEX from real observations");
    obs_t reloaded{};
    auto ignored = std::make_unique<nav_t>();
    sta_t reloadedStation{};
    const auto converted = QFile::encodeName(QDir::toNativeSeparators(QDir(session).filePath("ppk/rover/rover.obs")));
    require(readrnxt(converted.constData(), 1, {}, {}, 0, "", &reloaded, ignored.get(), &reloadedStation) > 0 &&
                reloaded.n > 0,
            "RTKLIB reads generated RINEX");
    freeobs(&reloaded);
    freenav(ignored.get(), 0xFF);
    require(PpkProcessor::importBase(session, fixture + "base.obs"), "archive base");
    require(PpkProcessor::importNavigation(session, fixture + "navigation.nav"), "archive NAV");
    auto config = PpkProcessor::loadConfig(session);
    config.frequencies = 2;
    config.constellations = SYS_GPS;
    config.elevationMaskDeg = 10;
    config.imuToAntennaBodyM = {0.1, 0.2, 0.3};
    require(PpkProcessor::status(session).ready(), "session ready for PPK");
    const auto processed = PpkProcessor::process(session, config);
    if (!processed.success)
        std::cerr << processed.error.toStdString() << '\n';
    require(processed.success && !processed.trajectory.empty(), "real RTKLIB Kinematic PPK");
    quint64 last = 0;
    for (const auto &point : processed.trajectory)
    {
        require(point.utcNs > last && std::isfinite(point.latitude) && std::isfinite(point.longitude) &&
                    point.quality >= 1 && point.quality <= 7,
                "solution monotonic finite and legal quality");
        last = point.utcNs;
    }
    PpkTrajectory reopened;
    require(readPpkTrajectory(QDir(session).filePath("ppk/trajectory.csv"), reopened),
            "reopen PPK corrected trajectory");
    require(reopened.size() == processed.trajectory.size() && PpkProcessor::status(session).completed,
            "persisted completed solution");
    {
        QLockFile lock(QDir(session).filePath("ppk/processing.lock"));
        require(lock.tryLock(), "claim Session processing lock");
        require(PpkProcessor::process(session, config).error == "SESSION_PPK_BUSY",
                "duplicate processing cannot overwrite in-flight Session output");
        require(!PpkProcessor::clearResult(session) && PpkProcessor::status(session).completed,
                "clear cannot remove a busy Session result");
    }
    using VaporView::Ground::SessionLoader;
    const auto metadata = SessionLoader::loadMetadata(session);
    require(metadata.success, "reopen formal Session");
    const auto original = SessionLoader::loadSensors(metadata.metadata);
    require(original.success && !original.data.track_points.isEmpty(), "Original track retained");
    require(setSessionNavigationSource(session, NavigationSource::Ppk), "activate PPK navigation source");
    const auto corrected = SessionLoader::loadSensors(metadata.metadata);
    require(corrected.success && !corrected.data.track_points.isEmpty(), "PPK sensor positions loaded");
    const auto &point = corrected.data.track_points.front();
    require(point.navigation_source == "PPK" && std::abs(point.latitude - 30.25) > 0.1,
            "sensor spatial position switches to PPK");
    const auto rendered = VaporView::Ground::Session::SessionTrajectoryRenderLoader::loadSessionDirectory(session);
    require(rendered.success && !rendered.samples.empty(), "3D and heat render loader accepts PPK");
    require(rendered.samples.front().navigation.navigationSource == "PPK" &&
                std::abs(rendered.samples.front().navigation.latDeg - point.latitude) < 1e-8,
            "3D track uses same corrected coordinates");
    require(std::abs(rendered.samples.front().heat.temperatureC.value_or(0) - 23.4) < 1e-6 &&
                std::abs(rendered.samples.front().heat.humidityRh.value_or(0) - 45.6) < 1e-6,
            "heat measurement remains unchanged");
    const QString exported = root.filePath("export.csv");
    require(VaporView::Ground::SessionExportService::exportTrajectoryCsv(exported, corrected.data.track_points).success,
            "export corrected navigation");
    QFile exportedFile(exported);
    require(exportedFile.open(QIODevice::ReadOnly) && exportedFile.readAll().contains(",PPK,IMU"),
            "export identifies corrected reference");
    require(setSessionNavigationSource(session, NavigationSource::Original), "switch back to Original");
    const auto restored = SessionLoader::loadSensors(metadata.metadata);
    require(restored.data.track_points.front().latitude == original.data.track_points.front().latitude,
            "Original coordinates preserved");
    QTemporaryDir oldSession;
    SessionNavigationResolver legacy(oldSession.path());
    require(!legacy.usesPpk() && legacy.available(), "old Session has no required PPK files");
    std::cout << "PPK samples=" << reopened.size() << " FIX=" << processed.quality.value("fix_count").toInt()
              << " FLOAT=" << processed.quality.value("float_count").toInt() << '\n';
    std::atomic_bool cancel{true};
    require(PpkProcessor::process(session, config, &cancel).cancelled, "processing cancellation");
    require(PpkProcessor::clearResult(session) && !PpkProcessor::status(session).completed,
            "clear result preserves inputs");
    return 0;
}

int main(int argc, char **argv)
{
    try
    {
        return runTest(argc, argv);
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
