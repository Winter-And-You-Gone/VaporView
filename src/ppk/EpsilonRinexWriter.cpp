#include "ppk/EpsilonRinexWriter.h"
#include "ppk/ObservationStore.h"
#include "rtklib.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>

namespace VaporView::Ppk
{
RinexSignal epsilonRinexSignal(std::uint8_t constellation, std::uint8_t frequency)
{
    switch (constellation)
    {
    case 1:
        switch (frequency)
        {
        case 0:
            return {SYS_GPS, 0, "1C"};
        case 1:
            return {SYS_GPS, 0, "1X"};
        case 2:
            return {SYS_GPS, 0, "1W"};
        case 4:
            return {SYS_GPS, 0, "2X"};
        case 5:
            return {SYS_GPS, 0, "2W"};
        case 7:
            return {SYS_GPS, 0, "5X"};
        }
        break;
    case 2:
        switch (frequency)
        {
        case 0:
            return {SYS_GLO, 1, "1C"};
        case 2:
            return {SYS_GLO, 1, "1P"};
        case 4:
            return {SYS_GLO, 1, "2C"};
        case 5:
            return {SYS_GLO, 1, "2P"};
        }
        break;
    case 3:
        switch (frequency)
        {
        case 0:
            return {SYS_CMP, 5, "2I"};
        case 4:
            return {SYS_CMP, 5, "7I"};
        case 7:
            return {SYS_CMP, 5, "6I"};
        }
        break;
    case 4:
        switch (frequency)
        {
        case 0:
            return {SYS_GAL, 2, "1X"};
        case 7:
            return {SYS_GAL, 2, "5X"};
        case 8:
            return {SYS_GAL, 2, "7X"};
        }
        break;
    case 6:
        switch (frequency)
        {
        case 0:
            return {SYS_QZS, 3, "1C"};
        case 1:
            return {SYS_QZS, 3, "1X"};
        case 4:
            return {SYS_QZS, 3, "2X"};
        case 7:
            return {SYS_QZS, 3, "5X"};
        }
        break;
    }
    return {};
}

QString epsilonRinexMappingPolicy()
{
    return QStringLiteral("EPSILON2-D4G/v1; manual zero-based GPS indices; D4G constellation policy "
                          "GPS 0=1C,1=1X,2=1W,4=2X,5=2W,7=5X; GLO 0=1C,2=1P,4=2C,5=2P; "
                          "BDS 0=2I,4=7I,7=6I; GAL 0=1X,7=5X,8=7X; QZS 0=1C,1=1X,4=2X,7=5X. "
                          "BDS/GAL use the D4G band ordering policy where the manual lacks per-constellation codes; "
                          "X selects combined components where protocol cannot distinguish them; "
                          "no undocumented tracking bits inferred; raw tracking status retained in observations.bin. "
                          "Observation UTC is converted with RTKLIB leap seconds to RINEX GPST.");
}

RinexWriteResult EpsilonRinexWriter::write(const QString &source, const QString &destination, std::uint8_t receiver,
                                           const std::atomic_bool *cancel)
{
    RinexWriteResult result;
    rnxopt_t options{};
    options.rnxver = 304;
    std::strcpy(options.prog, "VaporView");
    std::strcpy(options.marker, "EPSILON2-D4G");
    std::strcpy(options.markertype, "NON_GEODETIC");
    std::strcpy(options.rec[1], "EPSILON2-D4G");
    std::strcpy(options.comment[0], "Source UTC converted to GPST; antenna phase center");
    std::set<std::string> codes[7];
    bool first = true;
    const auto header =
        ObservationStore::read(source,
                               [&](const RawSatelliteEpoch &e)
                               {
                                   if (cancel && cancel->load())
                                       return false;
                                   if (e.receiver != receiver)
                                       return true;
                                   const gtime_t utc{static_cast<time_t>(e.unixSeconds), e.nanoseconds * 1e-9};
                                   if (first)
                                   {
                                       options.tstart = utc2gpst(utc);
                                       first = false;
                                   }
                                   options.tend = utc2gpst(utc);
                                   for (const auto &o : e.observations)
                                   {
                                       const auto signal = epsilonRinexSignal(o.system, o.frequency);
                                       if (!signal.code)
                                       {
                                           ++result.unmappedObservations;
                                           continue;
                                       }
                                       options.navsys |= signal.system;
                                       for (char type : {'C', 'L', 'D', 'S'})
                                           codes[signal.headerIndex].insert(std::string(1, type) + signal.code);
                                   }
                                   return true;
                               });
    if (!header.success || first || !options.navsys)
    {
        result.error = (cancel && cancel->load()) ? QStringLiteral("CANCELLED")
                       : header.success           ? QStringLiteral("NO_MAPPED_ROVER_OBSERVATIONS")
                                                  : header.error;
        return result;
    }
    for (int sys = 0; sys < 7; ++sys)
        for (const auto &code : codes[sys])
            std::strcpy(options.tobs[sys][options.nobs[sys]++], code.c_str());
    if (!QDir().mkpath(QFileInfo(destination).absolutePath()))
    {
        result.error = QStringLiteral("RINEX_DIRECTORY_FAILED");
        return result;
    }
    const QString temporary = destination + QStringLiteral(".part");
#ifdef _WIN32
    FILE *raw = _wfopen(reinterpret_cast<const wchar_t *>(temporary.utf16()), L"wb");
#else
    FILE *raw = std::fopen(QFile::encodeName(temporary).constData(), "wb");
#endif
    if (!raw)
    {
        result.error = QStringLiteral("RINEX_OPEN_FAILED");
        return result;
    }
    std::unique_ptr<FILE, decltype(&std::fclose)> file(raw, std::fclose);
    auto nav = std::make_unique<nav_t>();
    // RTKLIB's header writer indexes a full GLONASS slot array even when
    // there are no ephemerides. Zero slots mean unknown, never invented FCNs.
    std::vector<geph_t> glonassSlots(MAXPRNGLO);
    nav->geph = glonassSlots.data();
    bool written = outrnxobsh(raw, &options, nav.get()) != 0;
    const auto body = ObservationStore::read(
        source,
        [&](const RawSatelliteEpoch &e)
        {
            if (cancel && cancel->load())
                return false;
            if (e.receiver != receiver)
                return true;
            std::map<int, obsd_t> satellites;
            for (const auto &o : e.observations)
            {
                const auto signal = epsilonRinexSignal(o.system, o.frequency);
                if (!signal.code)
                    continue;
                int prn = o.prn;
                if (signal.system == SYS_QZS && prn < MINPRNQZS)
                    prn += MINPRNQZS - 1;
                const int sat = satno(signal.system, prn);
                if (!sat)
                    continue;
                auto &observation = satellites[sat];
                observation.sat = static_cast<uint8_t>(sat);
                observation.rcv = 1;
                observation.time = utc2gpst({static_cast<time_t>(e.unixSeconds), e.nanoseconds * 1e-9});
                const auto code = obs2code(signal.code);
                int index = code2idx(signal.system, code);
                if (index < 0 || index >= NFREQ)
                    continue;
                if (observation.code[index] && observation.code[index] != code)
                {
                    index = NFREQ;
                    while (index < NFREQ + NEXOBS && observation.code[index])
                        ++index;
                    if (index == NFREQ + NEXOBS)
                    {
                        written = false;
                        return false;
                    }
                }
                observation.code[index] = code;
                observation.P[index] = o.pseudoRangeM;
                observation.L[index] = o.carrierPhaseCycles;
                observation.D[index] = o.dopplerHz;
                observation.SNR[index] =
                    static_cast<uint16_t>(std::clamp(std::lround(o.snrDbHz / SNR_UNIT), 0L, 65535L));
                ++result.observations;
            }
            std::vector<obsd_t> observations;
            for (const auto &item : satellites)
                observations.push_back(item.second);
            if (!observations.empty())
            {
                written = written && outrnxobsb(raw, &options, observations.data(), int(observations.size()), 0);
                ++result.epochs;
            }
            return written;
        });
    written = written && std::fflush(raw) == 0 && !std::ferror(raw);
    file.reset();
    if (!body.success || !written || !result.epochs)
    {
        QFile::remove(temporary);
        result.error = (cancel && cancel->load()) ? QStringLiteral("CANCELLED")
                       : body.error.isEmpty()     ? QStringLiteral("RINEX_WRITE_FAILED")
                                                  : body.error;
        return result;
    }
    // QSaveFile keeps an existing usable RINEX on failure.
    QFile input(temporary);
    QSaveFile output(destination);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
    {
        result.error = QStringLiteral("RINEX_COMMIT_FAILED");
        QFile::remove(temporary);
        return result;
    }
    while (!input.atEnd())
    {
        const auto bytes = input.read(64 * 1024);
        if (output.write(bytes) != bytes.size())
        {
            written = false;
            break;
        }
    }
    input.close();
    QFile::remove(temporary);
    result.success = written && output.commit();
    if (!result.success)
        result.error = QStringLiteral("RINEX_COMMIT_FAILED");
    if (result.success)
    {
        QSaveFile metadata(QFileInfo(destination).dir().filePath(QStringLiteral("rinex_metadata.json")));
        if (metadata.open(QIODevice::WriteOnly))
        {
            metadata.write(
                QJsonDocument(QJsonObject{{"version", "3.04"},
                                          {"time_scale", "GPST"},
                                          {"source_time_scale", "UTC"},
                                          {"receiver", receiver},
                                          {"mapping_policy", epsilonRinexMappingPolicy()},
                                          {"unmapped_observations", QString::number(result.unmappedObservations)},
                                          {"recovered_incomplete_tail", header.recoveredTail}})
                    .toJson());
            metadata.commit();
        }
    }
    return result;
}
} // namespace VaporView::Ppk
