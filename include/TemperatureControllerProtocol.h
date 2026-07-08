#ifndef VaporView_TEMPERATURE_CONTROLLER_PROTOCOL_H_
#define VaporView_TEMPERATURE_CONTROLLER_PROTOCOL_H_

#include <QByteArray>
#include <QString>
#include <QVector>

#include <cstdint>

namespace VaporView
{
namespace TemperatureControllerProtocol
{

enum class Register : quint16
{
    FactoryReset = 0x0000,
    DeviceAddress = 0x0002,
    Rs485Baud = 0x0009,
    OvertempOutputMode = 0x000B,
    TargetTemperature = 0x1000,
    MeasuredTemperature = 0x1002,
    Resistor = 0x1004,
    OutputEnabled = 0x1100,
    OutputMode = 0x1101,
    OutputPolarity = 0x1102,
    OutputDuty = 0x1103,
    AutoPid = 0x1107,
    TemperatureSlope = 0x1108,
    MaxOutputPercent = 0x110E,
    PowerMode = 0x1110,
    OutputCurrent = 0x1111,
    MaxOutputCurrent = 0x1112,
    Kp = 0x1200,
    Ki = 0x1202,
    Kd = 0x1204,
    ControllerMode = 0x0004,
    InternalTemperature = 0x0003,
    ErrorCode = 0x0007,
    FirmwareVersion = 0x000C,
    DeviceModel = 0x0001,
};

enum class FrameStatus
{
    Ok,
    TooShort,
    CrcMismatch,
    SlaveMismatch,
    FunctionMismatch,
    AddressMismatch,
    RegisterCountMismatch,
    ByteCountMismatch,
    ExceptionResponse,
    InvalidArgument,
};

struct WriteResponse
{
    FrameStatus status = FrameStatus::InvalidArgument;
    quint8 exception_code = 0;
};

struct ReadResponse
{
    FrameStatus status = FrameStatus::InvalidArgument;
    quint8 exception_code = 0;
    QVector<quint16> registers;
};

quint16 modbusCrc16(const quint8 *data, qsizetype size);
quint16 modbusCrc16(const QByteArray& data);
QByteArray withCrc(QByteArray frame);
QByteArray buildReadRegistersRequest(quint8 slaveAddress, quint16 startAddress, quint16 registerCount);
QByteArray buildWriteRegistersRequest(quint8 slaveAddress, quint16 startAddress, const QVector<quint16>& registers);
QByteArray buildReadRegisterRequest(quint8 slaveAddress, quint8 channel, Register reg, quint16 registerCount);
QByteArray buildWriteRegisterRequest(quint8 slaveAddress, quint8 channel, Register reg, const QVector<quint16>& registers);
WriteResponse parseWriteRegistersResponse(const QByteArray& frame, quint8 slaveAddress, quint16 startAddress, quint16 registerCount);
ReadResponse parseReadRegistersResponse(const QByteArray& frame, quint8 slaveAddress, quint16 registerCount);
quint16 channelAddress(quint8 channel, Register reg);
QVector<quint16> encodeInt16(qint16 value);
QVector<quint16> encodeUInt16(quint16 value);
QVector<quint16> encodeInt32(qint32 value);
QVector<quint16> encodeUInt32(quint32 value);
QVector<quint16> encodeInt64(qint64 value);
QVector<quint16> encodeUInt64(quint64 value);
qint16 decodeInt16(const QVector<quint16>& registers, int offset = 0);
quint16 decodeUInt16(const QVector<quint16>& registers, int offset = 0);
qint32 decodeInt32(const QVector<quint16>& registers, int offset = 0);
quint32 decodeUInt32(const QVector<quint16>& registers, int offset = 0);
qint64 decodeInt64(const QVector<quint16>& registers, int offset = 0);
quint64 decodeUInt64(const QVector<quint16>& registers, int offset = 0);
qint32 temperatureCelsiusToRaw(double celsius);
double rawToTemperatureCelsius(qint32 raw);
QString frameStatusText(FrameStatus status);

}  // namespace TemperatureControllerProtocol
}  // namespace VaporView

#endif
