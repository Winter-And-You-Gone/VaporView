#include "TemperatureControllerProtocol.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace VaporView
{
namespace TemperatureControllerProtocol
{
namespace
{
constexpr quint8 kReadHoldingRegisters = 0x03;
constexpr quint8 kWriteMultipleRegisters = 0x10;
constexpr quint16 kChannelAddressOffset = 0x1000;
constexpr qint32 kMinTemperatureRaw = -40000000;
constexpr qint32 kMaxTemperatureRaw = 100000000;
constexpr double kTemperatureScale = 100000.0;

quint8 byteAt(const QByteArray& data, qsizetype index)
{
    return static_cast<quint8>(data.at(index));
}

void appendU16BE(QByteArray& data, quint16 value)
{
    data.append(static_cast<char>((value >> 8) & 0xFFu));
    data.append(static_cast<char>(value & 0xFFu));
}

void appendU16LE(QByteArray& data, quint16 value)
{
    data.append(static_cast<char>(value & 0xFFu));
    data.append(static_cast<char>((value >> 8) & 0xFFu));
}

quint16 readU16BE(const QByteArray& data, qsizetype offset)
{
    return static_cast<quint16>((static_cast<quint16>(byteAt(data, offset)) << 8) |
                               static_cast<quint16>(byteAt(data, offset + 1)));
}

quint16 readU16LE(const QByteArray& data, qsizetype offset)
{
    return static_cast<quint16>(static_cast<quint16>(byteAt(data, offset)) |
                               (static_cast<quint16>(byteAt(data, offset + 1)) << 8));
}

bool crcMatches(const QByteArray& frame)
{
    if (frame.size() < 4)
    {
        return false;
    }
    const quint16 expected = modbusCrc16(reinterpret_cast<const quint8*>(frame.constData()), frame.size() - 2);
    return expected == readU16LE(frame, frame.size() - 2);
}

FrameStatus checkException(const QByteArray& frame, quint8 slaveAddress, quint8 functionCode, quint8& exceptionCode)
{
    if (frame.size() != 5)
    {
        return FrameStatus::TooShort;
    }
    if (byteAt(frame, 0) != slaveAddress)
    {
        return FrameStatus::SlaveMismatch;
    }
    if (!crcMatches(frame))
    {
        return FrameStatus::CrcMismatch;
    }
    if (byteAt(frame, 1) != static_cast<quint8>(functionCode | 0x80u))
    {
        return FrameStatus::FunctionMismatch;
    }
    exceptionCode = byteAt(frame, 2);
    return FrameStatus::ExceptionResponse;
}

}  // namespace

quint16 modbusCrc16(const quint8 *data, qsizetype size)
{
    quint16 crc = 0xFFFF;
    for (qsizetype i = 0; i < size; ++i)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
        {
            if ((crc & 0x0001u) != 0)
            {
                crc = static_cast<quint16>((crc >> 1) ^ 0xA001u);
            }
            else
            {
                crc = static_cast<quint16>(crc >> 1);
            }
        }
    }
    return crc;
}

quint16 modbusCrc16(const QByteArray& data)
{
    return modbusCrc16(reinterpret_cast<const quint8*>(data.constData()), data.size());
}

QByteArray withCrc(QByteArray frame)
{
    appendU16LE(frame, modbusCrc16(frame));
    return frame;
}

QByteArray buildReadRegistersRequest(quint8 slaveAddress, quint16 startAddress, quint16 registerCount)
{
    QByteArray frame;
    frame.reserve(8);
    frame.append(static_cast<char>(slaveAddress));
    frame.append(static_cast<char>(kReadHoldingRegisters));
    appendU16BE(frame, startAddress);
    appendU16BE(frame, registerCount);
    return withCrc(frame);
}

QByteArray buildWriteRegistersRequest(quint8 slaveAddress, quint16 startAddress, const QVector<quint16>& registers)
{
    QByteArray frame;
    frame.reserve(9 + registers.size() * 2);
    frame.append(static_cast<char>(slaveAddress));
    frame.append(static_cast<char>(kWriteMultipleRegisters));
    appendU16BE(frame, startAddress);
    appendU16BE(frame, static_cast<quint16>(registers.size())) ;
    frame.append(static_cast<char>(registers.size() * 2));
    for (quint16 value : registers)
    {
        appendU16BE(frame, value);
    }
    return withCrc(frame);
}

QByteArray buildReadRegisterRequest(quint8 slaveAddress, quint8 channel, Register reg, quint16 registerCount)
{
    return buildReadRegistersRequest(slaveAddress, channelAddress(channel, reg), registerCount);
}

QByteArray buildWriteRegisterRequest(quint8 slaveAddress, quint8 channel, Register reg, const QVector<quint16>& registers)
{
    return buildWriteRegistersRequest(slaveAddress, channelAddress(channel, reg), registers);
}

WriteResponse parseWriteRegistersResponse(const QByteArray& frame, quint8 slaveAddress, quint16 startAddress, quint16 registerCount)
{
    WriteResponse response;
    if (frame.size() >= 2 && byteAt(frame, 1) == static_cast<quint8>(kWriteMultipleRegisters | 0x80u))
    {
        response.status = checkException(frame, slaveAddress, kWriteMultipleRegisters, response.exception_code);
        return response;
    }
    if (frame.size() < 8)
    {
        response.status = FrameStatus::TooShort;
        return response;
    }
    if (!crcMatches(frame))
    {
        response.status = FrameStatus::CrcMismatch;
        return response;
    }
    if (byteAt(frame, 0) != slaveAddress)
    {
        response.status = FrameStatus::SlaveMismatch;
        return response;
    }
    if (byteAt(frame, 1) != kWriteMultipleRegisters)
    {
        response.status = FrameStatus::FunctionMismatch;
        return response;
    }
    if (readU16BE(frame, 2) != startAddress)
    {
        response.status = FrameStatus::AddressMismatch;
        return response;
    }
    if (readU16BE(frame, 4) != registerCount)
    {
        response.status = FrameStatus::RegisterCountMismatch;
        return response;
    }
    response.status = FrameStatus::Ok;
    return response;
}

ReadResponse parseReadRegistersResponse(const QByteArray& frame, quint8 slaveAddress, quint16 registerCount)
{
    ReadResponse response;
    if (frame.size() >= 2 && byteAt(frame, 1) == static_cast<quint8>(kReadHoldingRegisters | 0x80u))
    {
        response.status = checkException(frame, slaveAddress, kReadHoldingRegisters, response.exception_code);
        return response;
    }
    if (frame.size() < 5)
    {
        response.status = FrameStatus::TooShort;
        return response;
    }
    if (!crcMatches(frame))
    {
        response.status = FrameStatus::CrcMismatch;
        return response;
    }
    if (byteAt(frame, 0) != slaveAddress)
    {
        response.status = FrameStatus::SlaveMismatch;
        return response;
    }
    if (byteAt(frame, 1) != kReadHoldingRegisters)
    {
        response.status = FrameStatus::FunctionMismatch;
        return response;
    }
    const quint8 byteCount = byteAt(frame, 2);
    if (byteCount != registerCount * 2)
    {
        response.status = FrameStatus::ByteCountMismatch;
        return response;
    }
    if (frame.size() != 3 + byteCount + 2)
    {
        response.status = FrameStatus::ByteCountMismatch;
        return response;
    }
    response.registers.reserve(registerCount);
    for (quint16 i = 0; i < registerCount; ++i)
    {
        response.registers.append(readU16BE(frame, 3 + i * 2));
    }
    response.status = FrameStatus::Ok;
    return response;
}

quint16 channelAddress(quint8 channel, Register reg)
{
    if (channel <= 1)
    {
        return static_cast<quint16>(reg);
    }
    return static_cast<quint16>(static_cast<quint16>(reg) + (channel - 1) * kChannelAddressOffset);
}

QVector<quint16> encodeInt16(qint16 value)
{
    return {static_cast<quint16>(value)};
}

QVector<quint16> encodeUInt16(quint16 value)
{
    return {value};
}

QVector<quint16> encodeInt32(qint32 value)
{
    return encodeUInt32(static_cast<quint32>(value));
}

QVector<quint16> encodeUInt32(quint32 value)
{
    return {
        static_cast<quint16>((value >> 16) & 0xFFFFu),
        static_cast<quint16>(value & 0xFFFFu),
    };
}

QVector<quint16> encodeInt64(qint64 value)
{
    return encodeUInt64(static_cast<quint64>(value));
}

QVector<quint16> encodeUInt64(quint64 value)
{
    return {
        static_cast<quint16>((value >> 48) & 0xFFFFu),
        static_cast<quint16>((value >> 32) & 0xFFFFu),
        static_cast<quint16>((value >> 16) & 0xFFFFu),
        static_cast<quint16>(value & 0xFFFFu),
    };
}

qint16 decodeInt16(const QVector<quint16>& registers, int offset)
{
    return static_cast<qint16>(decodeUInt16(registers, offset));
}

quint16 decodeUInt16(const QVector<quint16>& registers, int offset)
{
    if (offset < 0 || offset >= registers.size())
    {
        return 0;
    }
    return registers.at(offset);
}

qint32 decodeInt32(const QVector<quint16>& registers, int offset)
{
    return static_cast<qint32>(decodeUInt32(registers, offset));
}

quint32 decodeUInt32(const QVector<quint16>& registers, int offset)
{
    if (offset < 0 || offset + 1 >= registers.size())
    {
        return 0;
    }
    return (static_cast<quint32>(registers.at(offset)) << 16) |
           static_cast<quint32>(registers.at(offset + 1));
}

qint64 decodeInt64(const QVector<quint16>& registers, int offset)
{
    return static_cast<qint64>(decodeUInt64(registers, offset));
}

quint64 decodeUInt64(const QVector<quint16>& registers, int offset)
{
    if (offset < 0 || offset + 3 >= registers.size())
    {
        return 0;
    }
    return (static_cast<quint64>(registers.at(offset)) << 48) |
           (static_cast<quint64>(registers.at(offset + 1)) << 32) |
           (static_cast<quint64>(registers.at(offset + 2)) << 16) |
           static_cast<quint64>(registers.at(offset + 3));
}

qint32 temperatureCelsiusToRaw(double celsius)
{
    if (!std::isfinite(celsius))
    {
        return 0;
    }
    const double scaled = std::round(celsius * kTemperatureScale);
    const double clamped = std::clamp(scaled,
                                      static_cast<double>(kMinTemperatureRaw),
                                      static_cast<double>(kMaxTemperatureRaw));
    return static_cast<qint32>(clamped);
}

double rawToTemperatureCelsius(qint32 raw)
{
    return static_cast<double>(raw) / kTemperatureScale;
}

QString frameStatusText(FrameStatus status)
{
    switch (status)
    {
    case FrameStatus::Ok:
        return QStringLiteral("OK");
    case FrameStatus::TooShort:
        return QStringLiteral("frame too short");
    case FrameStatus::CrcMismatch:
        return QStringLiteral("CRC mismatch");
    case FrameStatus::SlaveMismatch:
        return QStringLiteral("slave address mismatch");
    case FrameStatus::FunctionMismatch:
        return QStringLiteral("function code mismatch");
    case FrameStatus::AddressMismatch:
        return QStringLiteral("register address mismatch");
    case FrameStatus::RegisterCountMismatch:
        return QStringLiteral("register count mismatch");
    case FrameStatus::ByteCountMismatch:
        return QStringLiteral("byte count mismatch");
    case FrameStatus::ExceptionResponse:
        return QStringLiteral("Modbus exception response");
    case FrameStatus::InvalidArgument:
    default:
        return QStringLiteral("invalid argument");
    }
}

}  // namespace TemperatureControllerProtocol
}  // namespace VaporView
