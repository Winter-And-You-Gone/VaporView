#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QVector>

#include <array>
#include <functional>

class QIODevice;

namespace VaporView::SessionRawDat
{

inline constexpr std::array<char, 8> kFileMagic = {'V', 'V', 'R', 'A', 'W', 'D', 'A', 'T'};
inline constexpr quint32 kRecordMarker = 0x44525756u;
inline constexpr quint32 kCurrentFormatVersion = 2u;
inline constexpr std::array<quint32, 2> kSupportedFormatVersions = {1u, 2u};
inline constexpr quint32 kFileHeaderSize = 20u;
inline constexpr quint32 kRecordHeaderSize = 36u;
inline constexpr quint32 kMaxFileHeaderSize = 4096u;
inline constexpr quint32 kMaxRecordHeaderSize = 4096u;
inline constexpr quint32 kMaxPayloadSize = 256u * 1024u * 1024u;

enum class RawSourceId : quint16
{
    Epsilon = 1u,
    Ptb = 2u,
    Hmp = 3u,
    Lidar = 4u,
    TcpWave = 5u
};

inline constexpr quint16 kSourceEpsilon = static_cast<quint16>(RawSourceId::Epsilon);
inline constexpr quint16 kSourcePtb = static_cast<quint16>(RawSourceId::Ptb);
inline constexpr quint16 kSourceHmp = static_cast<quint16>(RawSourceId::Hmp);
inline constexpr quint16 kSourceLidar = static_cast<quint16>(RawSourceId::Lidar);
inline constexpr quint16 kSourceTcpWave = static_cast<quint16>(RawSourceId::TcpWave);
inline constexpr quint16 kRecordTypePtbResponse = 1u;
inline constexpr quint16 kRecordTypeHmpModbusResponse = 0x03u;
inline constexpr quint16 kRecordTypeTcpWavePayload = 1u;
inline constexpr quint32 kTcpWaveCombinedPayloadFlag = 0x00000001u;
inline constexpr quint32 kTcpWavePayloadPrefixSize = sizeof(quint32) * 2u;

struct RawFileHeader
{
    quint32 version = 0;
    quint32 headerSize = 0;
    quint16 sourceId = 0;
    quint16 reserved = 0;
};

struct RawRecordHeader
{
    quint64 hostTimestampUs = 0;
    quint32 payloadSize = 0;
    quint16 sourceId = 0;
    quint16 recordType = 0;
    quint32 flags = 0;
    quint64 sequence = 0;
};

struct RawRecordIndex
{
    RawRecordHeader header;
    quint64 recordOffset = 0;
    quint64 payloadOffset = 0;
};

struct TcpWavePayloadLayout
{
    quint32 rawSignalSize = 0;
    quint32 harmonicSize = 0;
    quint32 rawSignalOffset = kTcpWavePayloadPrefixSize;
    quint32 harmonicOffset = kTcpWavePayloadPrefixSize;
};

enum class RawReadStatus
{
    Ok,
    RecoveredTruncatedTail,
    NotUnifiedFormat,
    UnsupportedVersion,
    InvalidHeader,
    CorruptRecord,
    IoError,
    Cancelled
};

struct RawScanOptions
{
    quint16 expectedSourceId = 0;
    std::function<bool()> isCancelled;
    std::function<void(quint64 completedBytes, quint64 totalBytes)> progress;
};

struct RawScanResult
{
    RawReadStatus status = RawReadStatus::IoError;
    RawFileHeader fileHeader;
    QVector<RawRecordIndex> records;
    QString error;
    QString warning;
    quint64 lastValidOffset = 0;

    bool success() const;
    bool recovered() const;
};

bool isSupportedFormatVersion(quint32 version);
QString supportedFormatVersionsText();
bool isKnownSourceId(quint16 sourceId);
bool isValidRecordType(quint16 sourceId, quint16 recordType);

bool encodeTcpWavePayload(QByteArrayView rawSignal,
                          QByteArrayView harmonic,
                          QByteArray *payload,
                          QString *error = nullptr);
bool parseTcpWavePayloadLayout(QByteArrayView sizePrefix,
                               quint32 totalPayloadSize,
                               TcpWavePayloadLayout *layout,
                               QString *error = nullptr);

bool writeFileHeader(QIODevice& device, quint16 sourceId, QString *error = nullptr);
bool writeRecord(QIODevice& device,
                 const RawRecordHeader& header,
                 QByteArrayView payload,
                 QString *error = nullptr);
RawScanResult scan(QIODevice& device, const RawScanOptions& options = {});

}  // namespace VaporView::SessionRawDat
