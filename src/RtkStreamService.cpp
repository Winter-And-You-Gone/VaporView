#include "RtkStreamService.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "rtklib.h"

#include <algorithm>
#include <array>
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
        return QString("ntrip://%1:%2@%3:%4/%5")
            .arg(username, password, server, port, mountpoint);
    }

    return QString("ntrip://%1:%2/%3")
        .arg(server, port, mountpoint);
}

QString buildSerialPath(const RtkStreamConfig &config)
{
    // RTKLIB STR_SERIAL expects a raw "port:baud:data:parity:stop:flow" path.
    return QString("%1:%2:8:n:1:off")
        .arg(sanitizeField(config.outputPort))
        .arg(config.baudrate);
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
    if (sanitizeField(config.server).isEmpty() ||
        sanitizeField(config.port).isEmpty() ||
        sanitizeField(config.mountpoint).isEmpty() ||
        sanitizeField(config.outputPort).isEmpty())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Missing required RTK stream fields.");
        }
        return false;
    }

    stop();

    const QString ntripPath = buildNtripPath(config);
    const QString serialPath = buildSerialPath(config);

    std::string inputPathStorage = ntripPath.toUtf8().toStdString();
    std::string outputPathStorage = serialPath.toUtf8().toStdString();
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
    std::array<int, 2> streamTypes = {STR_NTRIPCLI, STR_SERIAL};
    std::array<int, 8> options = {
        (std::max)(1000, config.timeoutMs),
        (std::max)(100, config.reconnectMs),
        2000,
        32768,
        10,
        0,
        30,
        1,
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
            nullptr))
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

bool RtkStreamService::injectInputSentence(const QString &sentence, QString *errorMessage)
{
    if (!isRunning())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("RTK stream service is not running.");
        }
        return false;
    }

    QString trimmed = sentence.trimmed();
    if (trimmed.isEmpty())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Input sentence is empty.");
        }
        return false;
    }

    QByteArray payload = trimmed.toLatin1();
    if (!payload.endsWith("\r\n"))
    {
        payload += "\r\n";
    }

    stream_t *inputStream = &impl_->server.stream[0];
    strlock(inputStream);
    const int written = strwrite(inputStream, reinterpret_cast<uint8_t *>(payload.data()), payload.size());
    strunlock(inputStream);

    if (written != payload.size())
    {
        if (errorMessage)
        {
            const QString message = stats().message;
            *errorMessage = message.isEmpty()
                ? QStringLiteral("Failed to inject NMEA sentence into RTK input stream.")
                : message;
        }
        return false;
    }

    return true;
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

    result.inputBytes = bytes[0];
    result.outputBytes = bytes[1];
    result.inputBps = bps[0];
    result.outputBps = bps[1];
    result.message = trimRtklibMessage(message.data());
    return result;
}
