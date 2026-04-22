#include "RtkStreamService.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "rtklib.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace
{
QString sanitizeField(const QString &value)
{
    QString sanitized = value.trimmed();
    sanitized.remove(QLatin1Char('\r'));
    sanitized.remove(QLatin1Char('\n'));
    return sanitized;
}

QString buildNtripPath(const RtkStreamConfig &config)
{
    const QString server = sanitizeField(config.server);
    const QString port = sanitizeField(config.port);
    const QString mountpoint = sanitizeField(config.mountpoint);
    const QString username = sanitizeField(config.username);
    const QString password = config.password;

    if (!username.isEmpty())
    {
        // RTKLIB STR_NTRIPCLI expects a raw NTRIP path, not a URL with scheme.
        return QString("%1:%2@%3:%4/%5")
            .arg(username, password, server, port, mountpoint);
    }

    return QString("%1:%2/%3")
        .arg(server, port, mountpoint);
}

QString buildSerialPath(const RtkStreamConfig &config)
{
    // RTKLIB STR_SERIAL expects a raw "port:baud:data:parity:stop:flow" path.
    return QString("%1:%2:8:n:1:off")
        .arg(sanitizeField(config.outputPort))
        .arg(config.baudrate);
}

QString buildOutputPath(const RtkStreamConfig &config)
{
    if (!sanitizeField(config.outputPathOverride).isEmpty())
    {
        return sanitizeField(config.outputPathOverride);
    }
    return buildSerialPath(config);
}

QString trimRtklibMessage(const char *message)
{
    if (!message)
    {
        return {};
    }

    QString text = QString::fromLocal8Bit(message).trimmed();
    if (text.endsWith(QLatin1Char(',')))
    {
        text.chop(1);
        text = text.trimmed();
    }
    return text;
}

bool hasValidNmeaPosition(const RtkStreamConfig &config)
{
    return config.sendNmeaGga &&
        std::isfinite(config.nmeaLatitudeDeg) &&
        std::isfinite(config.nmeaLongitudeDeg) &&
        std::isfinite(config.nmeaHeightM) &&
        std::abs(config.nmeaLatitudeDeg) <= 90.0 &&
        std::abs(config.nmeaLongitudeDeg) <= 180.0;
}
}

struct RtkStreamService::Impl
{
    strsvr_t server{};
    bool initialized = false;
    bool running = false;
};

RtkStreamService::RtkStreamService()
    : impl_(std::make_unique<Impl>())
{
    strsvrinit(&impl_->server, 1);
    impl_->initialized = true;
}

RtkStreamService::~RtkStreamService()
{
    stop();
}

bool RtkStreamService::start(const RtkStreamConfig &config, QString *errorMessage)
{
    const QString outputPath = buildOutputPath(config);
    if (sanitizeField(config.server).isEmpty() ||
        sanitizeField(config.port).isEmpty() ||
        sanitizeField(config.mountpoint).isEmpty() ||
        outputPath.isEmpty())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Missing required RTK stream fields.");
        }
        return false;
    }

    stop();

    const QString ntripPath = buildNtripPath(config);
    const int outputStreamType = config.outputMode == RtkStreamConfig::OutputMode::TcpClient
        ? STR_TCPCLI
        : STR_SERIAL;

    std::string inputPathStorage = ntripPath.toUtf8().toStdString();
    std::string outputPathStorage = outputPath.toUtf8().toStdString();
    std::array<char *, 2> paths = {
        inputPathStorage.data(),
        outputPathStorage.data(),
    };

    char emptyLog0[] = "";
    char emptyLog1[] = "";
    std::array<char *, 2> logs = {emptyLog0, emptyLog1};
    std::array<strconv_t *, 1> nullConverters = {nullptr};
    std::array<char *, 2> nullCommands = {nullptr, nullptr};
    std::array<char *, 2> nullPeriodicCommands = {nullptr, nullptr};
    std::array<int, 2> streamTypes = {STR_NTRIPCLI, outputStreamType};
    std::array<double, 3> nmeaPos = {};
    const bool sendNmeaGga = hasValidNmeaPosition(config);
    if (sendNmeaGga)
    {
        const double llh[] = {
            config.nmeaLatitudeDeg * D2R,
            config.nmeaLongitudeDeg * D2R,
            config.nmeaHeightM,
        };
        pos2ecef(llh, nmeaPos.data());
    }

    std::array<int, 8> options = {
        (std::max)(1000, config.timeoutMs),
        (std::max)(100, config.reconnectMs),
        2000,
        32768,
        10,
        sendNmeaGga ? (std::max)(1000, config.nmeaGgaCycleMs) : 0,
        30,
        sendNmeaGga ? 0 : (std::max)(0, config.relayBack),
    };

    if (!strsvrstart(
            &impl_->server,
            options.data(),
            streamTypes.data(),
            paths.data(),
            logs.data(),
            nullConverters.data(),
            nullCommands.data(),
            nullPeriodicCommands.data(),
            sendNmeaGga ? nmeaPos.data() : nullptr))
    {
        impl_->running = false;
        if (errorMessage)
        {
            const QString message = stats().message;
            *errorMessage = message.isEmpty() ? QStringLiteral("Failed to start embedded RTK stream service.") : message;
        }
        return false;
    }

    impl_->running = true;
    return true;
}

void RtkStreamService::stop()
{
    if (!impl_->initialized || !impl_->server.state)
    {
        impl_->running = false;
        return;
    }

    std::array<char *, 2> nullCommands = {nullptr, nullptr};
    strsvrstop(&impl_->server, nullCommands.data());
    impl_->running = false;
}

bool RtkStreamService::isRunning() const
{
    return impl_->running && impl_->server.state != 0;
}

RtkStreamStats RtkStreamService::stats() const
{
    RtkStreamStats result;
    result.running = isRunning();

    std::array<int, 2> stat = {};
    std::array<int, 2> logStat = {};
    std::array<int, 2> bytes = {};
    std::array<int, 2> bps = {};
    std::array<char, MAXSTRMSG * 4> message = {};

    strsvrstat(&impl_->server, stat.data(), logStat.data(), bytes.data(), bps.data(), message.data());

    static constexpr std::array<char, 5> kStateChars = {'E', '-', 'W', 'C', 'C'};
    QString mask;
    mask.reserve(5);
    for (int i = 0; i < 5; ++i)
    {
        const int code = i < static_cast<int>(stat.size()) ? stat[static_cast<std::size_t>(i)] : 0;
        result.streamStates[static_cast<std::size_t>(i)] = code;
        const int index = std::clamp(code + 1, 0, static_cast<int>(kStateChars.size() - 1));
        mask.append(QChar(kStateChars[static_cast<std::size_t>(index)]));
    }

    result.inputBytes = bytes[0];
    result.outputBytes = bytes[1];
    result.inputBps = bps[0];
    result.outputBps = bps[1];
    result.streamStateMask = mask;
    result.message = trimRtklibMessage(message.data());
    return result;
}
