#include "RtkStreamService.h"

#include <QStringList>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "rtklib.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>
#include <string>

extern "C" int strsvrpeek(strsvr_t *svr, uint8_t *buff, int nmax);

namespace
{
constexpr int kRtcmPeekChunkSize = 4096;
constexpr int kRtcmFirstBytesLimit = 32;

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

QString bytesToHex(const std::vector<uint8_t> &bytes)
{
    QStringList parts;
    parts.reserve(static_cast<int>(bytes.size()));
    for (uint8_t byte : bytes)
    {
        parts.append(QString::number(byte, 16).rightJustified(2, QLatin1Char('0')).toUpper());
    }
    return parts.join(QLatin1Char(' '));
}

QString bytesToAsciiPreview(const std::vector<uint8_t> &bytes)
{
    QString preview;
    preview.reserve(static_cast<int>(bytes.size()));
    for (uint8_t byte : bytes)
    {
        if (byte >= 0x20 && byte <= 0x7E)
        {
            preview.append(QChar(static_cast<char>(byte)));
        }
        else if (byte == '\r' || byte == '\n' || byte == '\t')
        {
            preview.append(QLatin1Char(' '));
        }
        else
        {
            preview.append(QLatin1Char('.'));
        }
    }
    return preview.simplified();
}

QString formatMessageTypes(const std::map<int, int> &messageTypeCounts)
{
    QStringList parts;
    int emitted = 0;
    for (const auto &entry : messageTypeCounts)
    {
        parts.append(QStringLiteral("%1x%2").arg(entry.first).arg(entry.second));
        if (++emitted >= 8)
        {
            break;
        }
    }
    return parts.join(QStringLiteral(", "));
}
}

struct RtkStreamService::Impl
{
    strsvr_t server{};
    bool initialized = false;
    bool running = false;
    std::vector<uint8_t> rtcmParseBuffer;
    std::vector<uint8_t> firstInputBytes;
    std::map<int, int> rtcmMessageTypeCounts;
    std::uint64_t rtcmDiagnosticBytes = 0;
    int rtcm3FrameCount = 0;
    int rtcm3CrcOkCount = 0;
    int rtcm3CrcFailCount = 0;
    int nonRtcmByteCount = 0;

    void resetDiagnostics()
    {
        rtcmParseBuffer.clear();
        firstInputBytes.clear();
        rtcmMessageTypeCounts.clear();
        rtcmDiagnosticBytes = 0;
        rtcm3FrameCount = 0;
        rtcm3CrcOkCount = 0;
        rtcm3CrcFailCount = 0;
        nonRtcmByteCount = 0;
    }

    void appendDiagnosticBytes(const uint8_t *data, int size)
    {
        if (!data || size <= 0)
        {
            return;
        }

        rtcmDiagnosticBytes += static_cast<std::uint64_t>(size);
        for (int i = 0; i < size && static_cast<int>(firstInputBytes.size()) < kRtcmFirstBytesLimit; ++i)
        {
            firstInputBytes.push_back(data[i]);
        }

        rtcmParseBuffer.insert(rtcmParseBuffer.end(), data, data + size);
        parseRtcm3Frames();
    }

    void parseRtcm3Frames()
    {
        while (!rtcmParseBuffer.empty())
        {
            const auto sync = std::find(rtcmParseBuffer.begin(), rtcmParseBuffer.end(), static_cast<uint8_t>(0xD3));
            if (sync == rtcmParseBuffer.end())
            {
                nonRtcmByteCount += static_cast<int>(rtcmParseBuffer.size());
                rtcmParseBuffer.clear();
                return;
            }

            if (sync != rtcmParseBuffer.begin())
            {
                nonRtcmByteCount += static_cast<int>(std::distance(rtcmParseBuffer.begin(), sync));
                rtcmParseBuffer.erase(rtcmParseBuffer.begin(), sync);
            }

            if (rtcmParseBuffer.size() < 3)
            {
                return;
            }

            if ((rtcmParseBuffer[1] & 0xFCU) != 0U)
            {
                ++nonRtcmByteCount;
                rtcmParseBuffer.erase(rtcmParseBuffer.begin());
                continue;
            }

            const int payloadLength = ((rtcmParseBuffer[1] & 0x03) << 8) | rtcmParseBuffer[2];
            const int frameLength = 3 + payloadLength + 3;
            if (static_cast<int>(rtcmParseBuffer.size()) < frameLength)
            {
                return;
            }

            ++rtcm3FrameCount;
            const uint32_t expectedCrc =
                (static_cast<uint32_t>(rtcmParseBuffer[3 + payloadLength]) << 16) |
                (static_cast<uint32_t>(rtcmParseBuffer[4 + payloadLength]) << 8) |
                static_cast<uint32_t>(rtcmParseBuffer[5 + payloadLength]);
            const uint32_t actualCrc = rtk_crc24q(rtcmParseBuffer.data(), 3 + payloadLength);
            if (actualCrc == expectedCrc)
            {
                ++rtcm3CrcOkCount;
            }
            else
            {
                ++rtcm3CrcFailCount;
            }

            if (payloadLength >= 2)
            {
                const int messageType =
                    (static_cast<int>(rtcmParseBuffer[3]) << 4) |
                    (static_cast<int>(rtcmParseBuffer[4]) >> 4);
                ++rtcmMessageTypeCounts[messageType];
            }

            rtcmParseBuffer.erase(rtcmParseBuffer.begin(), rtcmParseBuffer.begin() + frameLength);
        }

        if (rtcmParseBuffer.size() > 2048)
        {
            const int discardCount = static_cast<int>(rtcmParseBuffer.size() - 2048);
            nonRtcmByteCount += discardCount;
            rtcmParseBuffer.erase(rtcmParseBuffer.begin(), rtcmParseBuffer.end() - 2048);
        }
    }

    void collectPeekDiagnostics()
    {
        if (!running || !server.state)
        {
            return;
        }

        std::array<uint8_t, kRtcmPeekChunkSize> buffer = {};
        while (true)
        {
            const int readBytes = strsvrpeek(&server, buffer.data(), static_cast<int>(buffer.size()));
            if (readBytes <= 0)
            {
                break;
            }
            appendDiagnosticBytes(buffer.data(), readBytes);
            if (readBytes < static_cast<int>(buffer.size()))
            {
                break;
            }
        }
    }
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
    impl_->resetDiagnostics();

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
    impl_->collectPeekDiagnostics();

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
    result.rtcmDiagnosticBytes = impl_->rtcmDiagnosticBytes;
    result.rtcm3FrameCount = impl_->rtcm3FrameCount;
    result.rtcm3CrcOkCount = impl_->rtcm3CrcOkCount;
    result.rtcm3CrcFailCount = impl_->rtcm3CrcFailCount;
    result.nonRtcmByteCount = impl_->nonRtcmByteCount;
    result.rtcm3PendingBytes = static_cast<int>(impl_->rtcmParseBuffer.size());
    result.streamStateMask = mask;
    result.firstInputHex = bytesToHex(impl_->firstInputBytes);
    result.firstInputAscii = bytesToAsciiPreview(impl_->firstInputBytes);
    result.rtcmMessageTypes = formatMessageTypes(impl_->rtcmMessageTypeCounts);
    result.message = trimRtklibMessage(message.data());
    return result;
}
