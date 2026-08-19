#include "shared/session/UnifiedRawDat.h"

#include <QByteArray>
#include <QIODevice>
#include <QStringList>
#include <QtEndian>

#include <algorithm>
#include <limits>
#include <utility>

namespace VaporView::SessionRawDat
{
namespace
{

template <typename T>
void appendLittleEndian(QByteArray& bytes, T value)
{
    const T encoded = qToLittleEndian(value);
    bytes.append(reinterpret_cast<const char *>(&encoded), sizeof(encoded));
}

template <typename T>
T readLittleEndian(const char *data)
{
    return qFromLittleEndian<T>(reinterpret_cast<const uchar *>(data));
}

void setError(QString *error, const QString& message)
{
    if (error)
    {
        *error = message;
    }
}

bool writeBytes(QIODevice& device, QByteArrayView bytes, QString *error)
{
    if (device.write(bytes.data(), bytes.size()) == bytes.size())
    {
        return true;
    }
    setError(error, QStringLiteral("Failed to write raw DAT data: %1").arg(device.errorString()));
    return false;
}

QString truncatedTailWarning(const QString& part, quint64 recordCount, quint64 lastValidOffset)
{
    return QStringLiteral("Truncated final raw DAT record %1 ignored after %2 complete records; last valid offset %3")
        .arg(part)
        .arg(recordCount)
        .arg(lastValidOffset);
}

RawScanResult failedResult(RawReadStatus status,
                           const QString& error,
                           const RawFileHeader& fileHeader = {},
                           quint64 lastValidOffset = 0)
{
    RawScanResult result;
    result.status = status;
    result.fileHeader = fileHeader;
    result.error = error;
    result.lastValidOffset = lastValidOffset;
    return result;
}

void appendWarning(QString& warning, const QString& message)
{
    if (message.isEmpty())
    {
        return;
    }
    if (!warning.isEmpty())
    {
        warning += QStringLiteral("; ");
    }
    warning += message;
}

}  // namespace

bool RawScanResult::success() const
{
    return status == RawReadStatus::Ok || status == RawReadStatus::RecoveredTruncatedTail;
}

bool RawScanResult::recovered() const
{
    return status == RawReadStatus::RecoveredTruncatedTail;
}

bool isSupportedFormatVersion(quint32 version)
{
    switch (version)
    {
    case 1u:
    case 2u:
        return true;
    default:
        return false;
    }
}

QString supportedFormatVersionsText()
{
    QStringList versions;
    versions.reserve(static_cast<qsizetype>(kSupportedFormatVersions.size()));
    for (quint32 version : kSupportedFormatVersions)
    {
        versions.push_back(QString::number(version));
    }
    return versions.join(QStringLiteral(", "));
}

bool isKnownSourceId(quint16 sourceId)
{
    return sourceId >= kSourceNavigation && sourceId <= kSourceSystemTemperatureController;
}

bool isValidRecordType(quint16 sourceId, quint16 recordType)
{
    switch (static_cast<RawSourceId>(sourceId))
    {
    case RawSourceId::Navigation:
        return recordType > 0u && recordType <= std::numeric_limits<quint8>::max();
    case RawSourceId::Pressure:
        return recordType == kRecordTypePressureResponse;
    case RawSourceId::TemperatureHumidity:
        return recordType == kRecordTypeTemperatureHumidityModbusResponse;
    case RawSourceId::Distance:
        return recordType >= 1u && recordType <= 5u;
    case RawSourceId::Waveform:
        return recordType == kRecordTypeWaveformPayload;
    case RawSourceId::LaserTemperatureController:
        return recordType > 0u;
    case RawSourceId::SystemTemperatureController:
        return recordType == kRecordTypeSystemTemperatureMeasuredValues ||
               recordType == kRecordTypeSystemTemperatureAlarmStatus ||
               recordType == kRecordTypeSystemTemperatureMainStatus ||
               recordType == kRecordTypeSystemTemperatureControlStatus;
    }
    return false;
}

bool encodeWaveformPayload(QByteArrayView rawSignal,
                          QByteArrayView harmonic,
                          QByteArray *payload,
                          QString *error)
{
    if (!payload)
    {
        setError(error, QStringLiteral("Raw DAT TCP wave payload output is null"));
        return false;
    }
    if (rawSignal.size() < 0 || harmonic.size() < 0 ||
        static_cast<quint64>(rawSignal.size()) > std::numeric_limits<quint32>::max() ||
        static_cast<quint64>(harmonic.size()) > std::numeric_limits<quint32>::max())
    {
        setError(error, QStringLiteral("Raw DAT TCP wave sub-payload exceeds uint32 size"));
        return false;
    }
    const quint64 totalSize = kWaveformPayloadPrefixSize +
        static_cast<quint64>(rawSignal.size()) + static_cast<quint64>(harmonic.size());
    if (totalSize > kMaxPayloadSize)
    {
        setError(error,
                 QStringLiteral("Raw DAT TCP wave payload size %1 exceeds limit %2")
                     .arg(totalSize)
                     .arg(kMaxPayloadSize));
        return false;
    }

    QByteArray bytes;
    bytes.reserve(static_cast<qsizetype>(totalSize));
    appendLittleEndian(bytes, static_cast<quint32>(rawSignal.size()));
    appendLittleEndian(bytes, static_cast<quint32>(harmonic.size()));
    bytes.append(rawSignal.data(), rawSignal.size());
    bytes.append(harmonic.data(), harmonic.size());
    *payload = std::move(bytes);
    return true;
}

bool parseWaveformPayloadLayout(QByteArrayView sizePrefix,
                               quint32 totalPayloadSize,
                               WaveformPayloadLayout *layout,
                               QString *error)
{
    if (!layout)
    {
        setError(error, QStringLiteral("Raw DAT TCP wave payload layout output is null"));
        return false;
    }
    if (sizePrefix.size() < static_cast<qsizetype>(kWaveformPayloadPrefixSize))
    {
        setError(error,
                 QStringLiteral("Raw DAT TCP wave payload prefix is truncated: %1 bytes available, %2 required")
                     .arg(sizePrefix.size())
                     .arg(kWaveformPayloadPrefixSize));
        return false;
    }

    WaveformPayloadLayout decoded;
    decoded.rawSignalSize = readLittleEndian<quint32>(sizePrefix.data());
    decoded.harmonicSize = readLittleEndian<quint32>(sizePrefix.data() + sizeof(quint32));
    const quint64 requiredSize = kWaveformPayloadPrefixSize +
        static_cast<quint64>(decoded.rawSignalSize) + decoded.harmonicSize;
    if (requiredSize != totalPayloadSize)
    {
        setError(error,
                 QStringLiteral("Raw DAT TCP wave sub-payload sizes require %1 bytes, record declares %2")
                     .arg(requiredSize)
                     .arg(totalPayloadSize));
        return false;
    }
    decoded.harmonicOffset = decoded.rawSignalOffset + decoded.rawSignalSize;
    *layout = decoded;
    return true;
}

bool writeFileHeader(QIODevice& device, quint16 sourceId, QString *error)
{
    if (!device.isWritable())
    {
        setError(error, QStringLiteral("Raw DAT device is not writable"));
        return false;
    }
    if (!isKnownSourceId(sourceId))
    {
        setError(error, QStringLiteral("Invalid raw DAT source ID %1").arg(sourceId));
        return false;
    }

    QByteArray bytes;
    bytes.reserve(static_cast<qsizetype>(kFileHeaderSize));
    bytes.append(kFileMagic.data(), static_cast<qsizetype>(kFileMagic.size()));
    appendLittleEndian(bytes, kCurrentFormatVersion);
    appendLittleEndian(bytes, kFileHeaderSize);
    appendLittleEndian(bytes, sourceId);
    appendLittleEndian<quint16>(bytes, 0u);
    return writeBytes(device, bytes, error);
}

bool writeRecord(QIODevice& device,
                 const RawRecordHeader& header,
                 QByteArrayView payload,
                 QString *error)
{
    if (!device.isWritable())
    {
        setError(error, QStringLiteral("Raw DAT device is not writable"));
        return false;
    }
    if (!isKnownSourceId(header.sourceId) || !isValidRecordType(header.sourceId, header.recordType))
    {
        setError(error,
                 QStringLiteral("Invalid raw DAT source/type %1/%2")
                     .arg(header.sourceId)
                     .arg(header.recordType));
        return false;
    }
    if (payload.size() < 0 || static_cast<quint64>(payload.size()) > kMaxPayloadSize)
    {
        setError(error,
                 QStringLiteral("Raw DAT payload size %1 exceeds limit %2")
                     .arg(payload.size())
                     .arg(kMaxPayloadSize));
        return false;
    }
    if (header.sourceId == kSourceWaveform &&
        (header.flags & kWaveformCombinedPayloadFlag) != 0)
    {
        WaveformPayloadLayout layout;
        const qsizetype prefixSize = std::min(
            payload.size(),
            static_cast<qsizetype>(kWaveformPayloadPrefixSize));
        if (!parseWaveformPayloadLayout(payload.first(prefixSize),
                                       static_cast<quint32>(payload.size()),
                                       &layout,
                                       error))
        {
            return false;
        }
    }

    QByteArray bytes;
    bytes.reserve(static_cast<qsizetype>(kRecordHeaderSize));
    appendLittleEndian(bytes, kRecordMarker);
    appendLittleEndian(bytes, kRecordHeaderSize);
    appendLittleEndian(bytes, header.hostTimestampUs);
    appendLittleEndian(bytes, static_cast<quint32>(payload.size()));
    appendLittleEndian(bytes, header.sourceId);
    appendLittleEndian(bytes, header.recordType);
    appendLittleEndian(bytes, header.flags);
    appendLittleEndian(bytes, header.sequence);
    return writeBytes(device, bytes, error) && writeBytes(device, payload, error);
}

RawScanResult scan(QIODevice& device, const RawScanOptions& options)
{
    if (!device.isReadable() || device.isSequential())
    {
        return failedResult(RawReadStatus::IoError,
                            QStringLiteral("Raw DAT device must be readable and seekable"));
    }
    const qint64 fileSize = device.size();
    if (fileSize < static_cast<qint64>(kFileMagic.size()))
    {
        return failedResult(RawReadStatus::InvalidHeader,
                            QStringLiteral("Truncated raw DAT file header: %1 bytes available, %2 required")
                                .arg(std::max<qint64>(fileSize, 0))
                                .arg(kFileHeaderSize));
    }
    if (!device.seek(0))
    {
        return failedResult(RawReadStatus::IoError,
                            QStringLiteral("Failed to seek raw DAT file header: %1").arg(device.errorString()));
    }

    const QByteArray magic = device.read(static_cast<qint64>(kFileMagic.size()));
    if (magic.size() != static_cast<qsizetype>(kFileMagic.size()))
    {
        return failedResult(RawReadStatus::IoError,
                            QStringLiteral("Failed to read raw DAT magic: %1").arg(device.errorString()));
    }
    if (!std::equal(kFileMagic.cbegin(), kFileMagic.cend(), magic.cbegin()))
    {
        return failedResult(RawReadStatus::NotUnifiedFormat,
                            QStringLiteral("File does not contain unified raw DAT magic"));
    }
    if (fileSize < static_cast<qint64>(kFileHeaderSize))
    {
        return failedResult(RawReadStatus::InvalidHeader,
                            QStringLiteral("Truncated raw DAT file header: %1 bytes available, %2 required")
                                .arg(fileSize)
                                .arg(kFileHeaderSize));
    }
    if (!device.seek(0))
    {
        return failedResult(RawReadStatus::IoError,
                            QStringLiteral("Failed to seek raw DAT file header: %1").arg(device.errorString()));
    }

    const QByteArray headerBytes = device.read(kFileHeaderSize);
    if (headerBytes.size() != static_cast<qsizetype>(kFileHeaderSize))
    {
        return failedResult(RawReadStatus::IoError,
                            QStringLiteral("Failed to read raw DAT file header: %1").arg(device.errorString()));
    }
    RawFileHeader fileHeader;
    fileHeader.version = readLittleEndian<quint32>(headerBytes.constData() + 8);
    fileHeader.headerSize = readLittleEndian<quint32>(headerBytes.constData() + 12);
    fileHeader.sourceId = readLittleEndian<quint16>(headerBytes.constData() + 16);
    fileHeader.reserved = readLittleEndian<quint16>(headerBytes.constData() + 18);

    if (!isSupportedFormatVersion(fileHeader.version))
    {
        return failedResult(
            RawReadStatus::UnsupportedVersion,
            QStringLiteral("Unsupported raw DAT format version %1; supported versions: %2")
                .arg(fileHeader.version)
                .arg(supportedFormatVersionsText()),
            fileHeader);
    }
    if (fileHeader.headerSize < kFileHeaderSize || fileHeader.headerSize > kMaxFileHeaderSize ||
        static_cast<quint64>(fileHeader.headerSize) > static_cast<quint64>(fileSize))
    {
        return failedResult(
            RawReadStatus::InvalidHeader,
            QStringLiteral("Invalid raw DAT file header size %1; expected %2..%3 within file size %4")
                .arg(fileHeader.headerSize)
                .arg(kFileHeaderSize)
                .arg(kMaxFileHeaderSize)
                .arg(fileSize),
            fileHeader);
    }
    if (!isKnownSourceId(fileHeader.sourceId))
    {
        return failedResult(RawReadStatus::InvalidHeader,
                            QStringLiteral("Invalid raw DAT file source ID %1").arg(fileHeader.sourceId),
                            fileHeader);
    }
    if (options.expectedSourceId != 0 && fileHeader.sourceId != options.expectedSourceId)
    {
        return failedResult(
            RawReadStatus::InvalidHeader,
            QStringLiteral("Raw DAT file source ID %1 does not match expected source ID %2")
                .arg(fileHeader.sourceId)
                .arg(options.expectedSourceId),
            fileHeader);
    }
    if (fileHeader.reserved != 0)
    {
        return failedResult(RawReadStatus::InvalidHeader,
                            QStringLiteral("Invalid raw DAT reserved field %1").arg(fileHeader.reserved),
                            fileHeader);
    }
    if (!device.seek(fileHeader.headerSize))
    {
        return failedResult(RawReadStatus::IoError,
                            QStringLiteral("Failed to seek past raw DAT file header: %1")
                                .arg(device.errorString()),
                            fileHeader);
    }

    RawScanResult result;
    result.status = RawReadStatus::Ok;
    result.fileHeader = fileHeader;
    result.lastValidOffset = fileHeader.headerSize;
    quint64 expectedSequence = 0;

    while (device.pos() < fileSize)
    {
        if (options.isCancelled && options.isCancelled())
        {
            result.status = RawReadStatus::Cancelled;
            result.error = QStringLiteral("Raw DAT scan canceled");
            return result;
        }

        const qint64 recordOffset = device.pos();
        const qint64 remaining = fileSize - recordOffset;
        if (remaining < static_cast<qint64>(kRecordHeaderSize))
        {
            result.status = RawReadStatus::RecoveredTruncatedTail;
            appendWarning(result.warning,
                          truncatedTailWarning(QStringLiteral("header"),
                                               static_cast<quint64>(result.records.size()),
                                               result.lastValidOffset));
            return result;
        }

        const QByteArray recordBytes = device.read(kRecordHeaderSize);
        if (recordBytes.size() != static_cast<qsizetype>(kRecordHeaderSize))
        {
            result.status = RawReadStatus::IoError;
            result.error = QStringLiteral("Failed to read raw DAT record header at offset %1: %2")
                               .arg(recordOffset)
                               .arg(device.errorString());
            return result;
        }

        const quint32 marker = readLittleEndian<quint32>(recordBytes.constData());
        const quint32 recordHeaderSize = readLittleEndian<quint32>(recordBytes.constData() + 4);
        RawRecordHeader header;
        header.hostTimestampUs = readLittleEndian<quint64>(recordBytes.constData() + 8);
        header.payloadSize = readLittleEndian<quint32>(recordBytes.constData() + 16);
        header.sourceId = readLittleEndian<quint16>(recordBytes.constData() + 20);
        header.recordType = readLittleEndian<quint16>(recordBytes.constData() + 22);
        header.flags = readLittleEndian<quint32>(recordBytes.constData() + 24);
        header.sequence = readLittleEndian<quint64>(recordBytes.constData() + 28);

        if (marker != kRecordMarker)
        {
            result.status = RawReadStatus::CorruptRecord;
            result.error = QStringLiteral("Invalid raw DAT record marker 0x%1 at offset %2")
                               .arg(marker, 8, 16, QLatin1Char('0'))
                               .arg(recordOffset);
            return result;
        }
        if (recordHeaderSize < kRecordHeaderSize || recordHeaderSize > kMaxRecordHeaderSize)
        {
            result.status = RawReadStatus::CorruptRecord;
            result.error = QStringLiteral("Invalid raw DAT record header size %1 at offset %2; expected %3..%4")
                               .arg(recordHeaderSize)
                               .arg(recordOffset)
                               .arg(kRecordHeaderSize)
                               .arg(kMaxRecordHeaderSize);
            return result;
        }
        if (header.payloadSize > kMaxPayloadSize)
        {
            result.status = RawReadStatus::CorruptRecord;
            result.error = QStringLiteral("Raw DAT payload size %1 at offset %2 exceeds limit %3")
                               .arg(header.payloadSize)
                               .arg(recordOffset)
                               .arg(kMaxPayloadSize);
            return result;
        }
        if (header.sourceId != fileHeader.sourceId)
        {
            result.status = RawReadStatus::CorruptRecord;
            result.error = QStringLiteral("Raw DAT record source ID %1 at offset %2 does not match file source ID %3")
                               .arg(header.sourceId)
                               .arg(recordOffset)
                               .arg(fileHeader.sourceId);
            return result;
        }
        if (!isValidRecordType(header.sourceId, header.recordType))
        {
            result.status = RawReadStatus::CorruptRecord;
            result.error = QStringLiteral("Invalid raw DAT record type %1 for source ID %2 at offset %3")
                               .arg(header.recordType)
                               .arg(header.sourceId)
                               .arg(recordOffset);
            return result;
        }
        if (static_cast<quint64>(recordHeaderSize) > static_cast<quint64>(remaining))
        {
            result.status = RawReadStatus::RecoveredTruncatedTail;
            appendWarning(result.warning,
                          truncatedTailWarning(QStringLiteral("header"),
                                               static_cast<quint64>(result.records.size()),
                                               result.lastValidOffset));
            return result;
        }

        const quint64 payloadOffset = static_cast<quint64>(recordOffset) + recordHeaderSize;
        const quint64 availablePayload = static_cast<quint64>(fileSize) - payloadOffset;
        if (header.payloadSize > availablePayload)
        {
            result.status = RawReadStatus::RecoveredTruncatedTail;
            appendWarning(result.warning,
                          truncatedTailWarning(QStringLiteral("payload"),
                                               static_cast<quint64>(result.records.size()),
                                               result.lastValidOffset));
            return result;
        }
        const quint64 nextRecord = payloadOffset + header.payloadSize;
        if (header.sourceId == kSourceWaveform &&
            (header.flags & kWaveformCombinedPayloadFlag) != 0)
        {
            if (!device.seek(static_cast<qint64>(payloadOffset)))
            {
                result.status = RawReadStatus::IoError;
                result.error = QStringLiteral("Failed to seek raw DAT TCP wave payload at offset %1: %2")
                                   .arg(payloadOffset)
                                   .arg(device.errorString());
                return result;
            }
            const QByteArray prefix = device.read(std::min(header.payloadSize, kWaveformPayloadPrefixSize));
            WaveformPayloadLayout layout;
            QString layoutError;
            if (!parseWaveformPayloadLayout(prefix, header.payloadSize, &layout, &layoutError))
            {
                result.status = RawReadStatus::CorruptRecord;
                result.error = QStringLiteral("Invalid raw DAT TCP wave payload at offset %1: %2")
                                   .arg(recordOffset)
                                   .arg(layoutError);
                return result;
            }
        }
        if (nextRecord > static_cast<quint64>(std::numeric_limits<qint64>::max()) ||
            !device.seek(static_cast<qint64>(nextRecord)))
        {
            result.status = RawReadStatus::IoError;
            result.error = QStringLiteral("Failed to seek raw DAT record at offset %1: %2")
                               .arg(recordOffset)
                               .arg(device.errorString());
            return result;
        }

        RawRecordIndex index;
        index.header = header;
        index.recordOffset = static_cast<quint64>(recordOffset);
        index.payloadOffset = payloadOffset;
        result.records.push_back(index);
        result.lastValidOffset = nextRecord;
        if (header.sequence != expectedSequence)
        {
            appendWarning(result.warning,
                          QStringLiteral("Raw DAT sequence %1 at offset %2; expected %3")
                              .arg(header.sequence)
                              .arg(recordOffset)
                              .arg(expectedSequence));
        }
        expectedSequence = header.sequence + 1;

        if (options.progress && result.records.size() % 512 == 0)
        {
            options.progress(nextRecord, static_cast<quint64>(fileSize));
        }
    }

    if (options.progress)
    {
        options.progress(result.lastValidOffset, static_cast<quint64>(fileSize));
    }
    return result;
}

}  // namespace VaporView::SessionRawDat
