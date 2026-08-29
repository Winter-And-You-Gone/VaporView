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
    Navigation = 1u,
    Pressure = 2u,
    TemperatureHumidity = 3u,
    Distance = 4u,
    Waveform = 5u,
    LaserTemperatureController = 6u,
    SystemTemperatureController = 7u
};

inline constexpr quint16 kSourceNavigation = static_cast<quint16>(RawSourceId::Navigation);
inline constexpr quint16 kSourcePressure = static_cast<quint16>(RawSourceId::Pressure);
inline constexpr quint16 kSourceTemperatureHumidity = static_cast<quint16>(RawSourceId::TemperatureHumidity);
inline constexpr quint16 kSourceDistance = static_cast<quint16>(RawSourceId::Distance);
inline constexpr quint16 kSourceWaveform = static_cast<quint16>(RawSourceId::Waveform);
inline constexpr quint16 kSourceLaserTemperatureController =
    static_cast<quint16>(RawSourceId::LaserTemperatureController);
inline constexpr quint16 kSourceSystemTemperatureController =
    static_cast<quint16>(RawSourceId::SystemTemperatureController);
inline constexpr quint16 kRecordTypePressureResponse = 1u;
inline constexpr quint16 kRecordTypeTemperatureHumidityModbusResponse = 0x03u;
inline constexpr quint16 kRecordTypeWaveformPayload = 1u;
inline constexpr quint16 kRecordTypeSystemTemperatureMeasuredValues = 1u;
inline constexpr quint16 kRecordTypeSystemTemperatureAlarmStatus = 2u;
inline constexpr quint16 kRecordTypeSystemTemperatureMainStatus = 3u;
inline constexpr quint16 kRecordTypeSystemTemperatureControlStatus = 4u;
inline constexpr quint32 kWaveformCombinedPayloadFlag = 0x00000001u;
inline constexpr quint32 kWaveformPayloadPrefixSize = sizeof(quint32) * 2u;

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

struct WaveformPayloadLayout
{
    quint32 rawSignalSize = 0;
    quint32 harmonicSize = 0;
    quint32 rawSignalOffset = kWaveformPayloadPrefixSize;
    quint32 harmonicOffset = kWaveformPayloadPrefixSize;
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

bool encodeWaveformPayload(QByteArrayView rawSignal,
                          QByteArrayView harmonic,
                          QByteArray *payload,
                          QString *error = nullptr);
bool parseWaveformPayloadLayout(QByteArrayView sizePrefix,
                               quint32 totalPayloadSize,
                               WaveformPayloadLayout *layout,
                               QString *error = nullptr);

bool writeFileHeader(QIODevice& device, quint16 sourceId, QString *error = nullptr);
bool writeRecord(QIODevice& device,
                 const RawRecordHeader& header,
                 QByteArrayView payload,
                 QString *error = nullptr);
RawScanResult scan(QIODevice& device, const RawScanOptions& options = {});

}  // namespace VaporView::SessionRawDat
