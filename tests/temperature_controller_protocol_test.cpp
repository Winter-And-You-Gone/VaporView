#include "TemperatureControllerProtocol.h"
#include "data_collector.h"

#include <QByteArray>

#include <cmath>
#include <iostream>

namespace
{

QByteArray bytes(std::initializer_list<unsigned int> values)
{
    QByteArray result;
    result.reserve(static_cast<int>(values.size()));
    for (unsigned int value : values)
    {
        result.append(static_cast<char>(value & 0xFFu));
    }
    return result;
}

bool require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << message << "\n";
        return false;
    }
    return true;
}

bool checkDocumentReadTargetCommand()
{
    const QByteArray expected = bytes({0x01, 0x03, 0x10, 0x00, 0x00, 0x02, 0xC0, 0xCB});
    const QByteArray actual = VaporView::TemperatureControllerProtocol::buildReadRegisterRequest(
        1,
        1,
        VaporView::TemperatureControllerProtocol::Register::TargetTemperature,
        2);
    return require(actual == expected, "document read target temperature command mismatch");
}

bool checkDocumentWriteTargetCommand()
{
    const QByteArray expected = bytes({0x01, 0x10, 0x10, 0x00, 0x00, 0x02, 0x04, 0x00, 0x26, 0x25, 0xA0, 0xC5, 0x4C});
    const qint32 raw = VaporView::TemperatureControllerProtocol::temperatureCelsiusToRaw(25.0);
    const QByteArray actual = VaporView::TemperatureControllerProtocol::buildWriteRegisterRequest(
        1,
        1,
        VaporView::TemperatureControllerProtocol::Register::TargetTemperature,
        VaporView::TemperatureControllerProtocol::encodeInt32(raw));
    return require(actual == expected, "document write target temperature command mismatch");
}

bool checkDocumentWriteResponse()
{
    const QByteArray response = bytes({0x01, 0x10, 0x10, 0x00, 0x00, 0x02, 0x45, 0x08});
    const auto parsed = VaporView::TemperatureControllerProtocol::parseWriteRegistersResponse(response, 1, 0x1000, 2);
    return require(parsed.status == VaporView::TemperatureControllerProtocol::FrameStatus::Ok,
                   "document write target temperature response should parse");
}

bool checkTemperatureScaling()
{
    bool ok = true;
    ok &= require(VaporView::TemperatureControllerProtocol::temperatureCelsiusToRaw(25.0) == 2500000,
                  "25C scaling mismatch");
    ok &= require(VaporView::TemperatureControllerProtocol::temperatureCelsiusToRaw(-40.0) == -4000000,
                  "-40C scaling mismatch");
    ok &= require(std::fabs(VaporView::TemperatureControllerProtocol::rawToTemperatureCelsius(2259187) - 22.59187) < 0.000001,
                  "raw temperature decoding mismatch");
    ok &= require(VaporView::TemperatureControllerProtocol::temperatureCelsiusToRaw(-500.0) == -40000000,
                  "low temperature clamp mismatch");
    ok &= require(VaporView::TemperatureControllerProtocol::temperatureCelsiusToRaw(2000.0) == 100000000,
                  "high temperature clamp mismatch");
    return ok;
}

bool checkChannelAddressOffset()
{
    bool ok = true;
    ok &= require(VaporView::TemperatureControllerProtocol::channelAddress(1, VaporView::TemperatureControllerProtocol::Register::TargetTemperature) == 0x1000,
                  "channel 1 target address mismatch");
    ok &= require(VaporView::TemperatureControllerProtocol::channelAddress(2, VaporView::TemperatureControllerProtocol::Register::TargetTemperature) == 0x2000,
                  "channel 2 target address mismatch");
    ok &= require(VaporView::TemperatureControllerProtocol::channelAddress(2, VaporView::TemperatureControllerProtocol::Register::MeasuredTemperature) == 0x2002,
                  "channel 2 measured temperature address mismatch");
    ok &= require(VaporView::TemperatureControllerProtocol::channelAddress(1, VaporView::TemperatureControllerProtocol::Register::AutoPid) == 0x1107,
                  "channel 1 auto PID address mismatch");
    ok &= require(VaporView::TemperatureControllerProtocol::channelAddress(2, VaporView::TemperatureControllerProtocol::Register::AutoPid) == 0x2107,
                  "channel 2 auto PID address mismatch");
    ok &= require(VaporView::TemperatureControllerProtocol::channelAddress(1, VaporView::TemperatureControllerProtocol::Register::SensorModel) == 0x1300,
                  "channel 1 sensor model address mismatch");
    ok &= require(VaporView::TemperatureControllerProtocol::channelAddress(2, VaporView::TemperatureControllerProtocol::Register::SensorModel) == 0x2300,
                  "channel 2 sensor model address mismatch");
    ok &= require(VaporView::TemperatureControllerProtocol::channelAddress(1, VaporView::TemperatureControllerProtocol::Register::NtcB) == 0x1301,
                  "channel 1 NTC B address mismatch");
    ok &= require(VaporView::TemperatureControllerProtocol::channelAddress(2, VaporView::TemperatureControllerProtocol::Register::NtcB) == 0x2301,
                  "channel 2 NTC B address mismatch");
    ok &= require(VaporView::TemperatureControllerProtocol::channelAddress(2, VaporView::TemperatureControllerProtocol::Register::PolynomialA0Mantissa) == 0x2315,
                  "channel 2 A0 mantissa address mismatch");
    ok &= require(VaporView::TemperatureControllerProtocol::channelAddress(
                      2,
                      static_cast<VaporView::TemperatureControllerProtocol::Register>(
                          static_cast<quint16>(VaporView::TemperatureControllerProtocol::Register::PolynomialA0Exponent) + 7 * 5)) == 0x233C,
                  "channel 2 A7 exponent address mismatch");
    ok &= require(static_cast<quint16>(VaporView::TemperatureControllerProtocol::Register::ControllerMode) == 0x0004,
                  "controller mode address mismatch");
    ok &= require(static_cast<quint16>(VaporView::TemperatureControllerProtocol::Register::FactoryReset) == 0x0000,
                  "factory reset address mismatch");
    ok &= require(static_cast<quint16>(VaporView::TemperatureControllerProtocol::Register::DeviceAddress) == 0x0002,
                  "device address register mismatch");
    ok &= require(static_cast<quint16>(VaporView::TemperatureControllerProtocol::Register::Rs485Baud) == 0x0009,
                  "RS485 baud register mismatch");
    ok &= require(static_cast<quint16>(VaporView::TemperatureControllerProtocol::Register::OvertempOutputMode) == 0x000B,
                  "over-temperature output mode register mismatch");
    ok &= require(static_cast<quint16>(VaporView::TemperatureControllerProtocol::Register::DeviceModel) == 0x0001,
                  "device model register mismatch");
    ok &= require(static_cast<quint16>(VaporView::TemperatureControllerProtocol::Register::FirmwareVersion) == 0x000C,
                  "firmware version register mismatch");
    ok &= require(VaporView::TemperatureControllerProtocol::deviceModelName(1) == QStringLiteral("103"),
                  "device model 1 name mismatch");
    ok &= require(VaporView::TemperatureControllerProtocol::deviceModelName(31) == QStringLiteral("203"),
                  "device model 31 name mismatch");
    ok &= require(VaporView::TemperatureControllerProtocol::deviceModelName(99) == QStringLiteral("99"),
                  "unknown device model name mismatch");
    return ok;
}

bool checkReadResponseDecoding()
{
    const QByteArray response = VaporView::TemperatureControllerProtocol::withCrc(
        bytes({0x01, 0x03, 0x04, 0x00, 0x26, 0x25, 0xA0}));
    const auto parsed = VaporView::TemperatureControllerProtocol::parseReadRegistersResponse(response, 1, 2);
    bool ok = require(parsed.status == VaporView::TemperatureControllerProtocol::FrameStatus::Ok,
                      "read response should parse");
    ok &= require(VaporView::TemperatureControllerProtocol::decodeInt32(parsed.registers) == 2500000,
                  "read response int32 decode mismatch");
    return ok;
}

bool checkRejectsBadFrames()
{
    bool ok = true;
    QByteArray response = bytes({0x01, 0x10, 0x10, 0x00, 0x00, 0x02, 0x45, 0x08});
    response[7] = static_cast<char>(0x09);
    ok &= require(VaporView::TemperatureControllerProtocol::parseWriteRegistersResponse(response, 1, 0x1000, 2).status ==
                      VaporView::TemperatureControllerProtocol::FrameStatus::CrcMismatch,
                  "bad CRC should be rejected");

    const QByteArray wrongSlave = bytes({0x02, 0x10, 0x10, 0x00, 0x00, 0x02, 0x45, 0x3B});
    ok &= require(VaporView::TemperatureControllerProtocol::parseWriteRegistersResponse(wrongSlave, 1, 0x1000, 2).status ==
                      VaporView::TemperatureControllerProtocol::FrameStatus::SlaveMismatch,
                  "wrong slave should be rejected");

    const QByteArray wrongAddress = bytes({0x01, 0x10, 0x10, 0x02, 0x00, 0x02, 0xE4, 0xC8});
    ok &= require(VaporView::TemperatureControllerProtocol::parseWriteRegistersResponse(wrongAddress, 1, 0x1000, 2).status ==
                      VaporView::TemperatureControllerProtocol::FrameStatus::AddressMismatch,
                  "wrong address should be rejected");

    const QByteArray exception = VaporView::TemperatureControllerProtocol::withCrc(bytes({0x01, 0x90, 0x02}));
    const auto parsedException = VaporView::TemperatureControllerProtocol::parseWriteRegistersResponse(exception, 1, 0x1000, 2);
    ok &= require(parsedException.status == VaporView::TemperatureControllerProtocol::FrameStatus::ExceptionResponse,
                  "exception response should be detected");
    ok &= require(parsedException.exception_code == 0x02, "exception code mismatch");
    return ok;
}

bool checkEnvironmentSerialExamples()
{
    bool ok = true;
    VaporView::EnvironmentSerialValues bmp;
    ok &= require(VaporView::parseEnvironmentSerialLine(
                      "T:25.32 deg C,P:100653.25 Pa", bmp),
                  "BMP390 example line should parse");
    ok &= require(bmp.has_temperature && std::fabs(bmp.temperature_c - 25.32) < 0.001,
                  "BMP390 temperature mismatch");
    ok &= require(bmp.has_pressure && std::fabs(bmp.pressure_hpa - 1006.5325) < 0.0001,
                  "BMP390 pressure conversion mismatch");

    VaporView::EnvironmentSerialValues shtTemperature;
    VaporView::EnvironmentSerialValues shtHumidity;
    ok &= require(VaporView::parseEnvironmentSerialLine(
                      "Temperature: 23.75 degrees C", shtTemperature),
                  "SHT45 temperature line should parse");
    ok &= require(VaporView::parseEnvironmentSerialLine(
                      "Humidity: 48.20 % rH", shtHumidity),
                  "SHT45 humidity line should parse");
    ok &= require(shtTemperature.has_temperature &&
                      std::fabs(shtTemperature.temperature_c - 23.75) < 0.001,
                  "SHT45 temperature mismatch");
    ok &= require(shtHumidity.has_humidity &&
                      std::fabs(shtHumidity.humidity_rh - 48.20) < 0.001,
                  "SHT45 humidity mismatch");
    return ok;
}

}  // namespace

int main()
{
    int failures = 0;
    failures += checkDocumentReadTargetCommand() ? 0 : 1;
    failures += checkDocumentWriteTargetCommand() ? 0 : 1;
    failures += checkDocumentWriteResponse() ? 0 : 1;
    failures += checkTemperatureScaling() ? 0 : 1;
    failures += checkChannelAddressOffset() ? 0 : 1;
    failures += checkReadResponseDecoding() ? 0 : 1;
    failures += checkRejectsBadFrames() ? 0 : 1;
    failures += checkEnvironmentSerialExamples() ? 0 : 1;
    return failures == 0 ? 0 : 1;
}
