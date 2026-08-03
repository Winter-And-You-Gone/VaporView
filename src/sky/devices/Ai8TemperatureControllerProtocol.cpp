#include "Ai8TemperatureControllerProtocol.h"

#include <algorithm>
#include <cmath>

namespace VaporView::Ai8TemperatureControllerProtocol
{
namespace
{

quint16 clampUnsigned(double value, double scale)
{
    return static_cast<quint16>(std::clamp(std::lround(value * scale), 0L, 65535L));
}

quint16 clampSigned(double value, double scale)
{
    const long raw = std::clamp(std::lround(value * scale), -32768L, 32767L);
    return static_cast<quint16>(static_cast<qint16>(raw));
}

} // namespace

quint16 channelRegister(Register base, int channel)
{
    const int offset = std::clamp(channel, 1, 96) - 1;
    return static_cast<quint16>(static_cast<quint16>(base) + offset);
}

quint16 groupRegister(Register base, int group)
{
    const int offset = std::clamp(group, 1, kParameterGroupCount) - 1;
    return static_cast<quint16>(static_cast<quint16>(base) + offset);
}

quint16 encodeSignedTenths(double value)
{
    return clampSigned(value, 10.0);
}

quint16 encodeUnsignedTenths(double value)
{
    return clampUnsigned(value, 10.0);
}

quint16 encodeSignedHundredths(double value)
{
    return clampSigned(value, 100.0);
}

quint16 encodeManualOutput(double percent)
{
    return clampUnsigned(std::clamp(percent, 0.0, 100.0), 256.0);
}

double decodeSignedTenths(quint16 value)
{
    return static_cast<double>(static_cast<qint16>(value)) / 10.0;
}

double decodeUnsignedTenths(quint16 value)
{
    return static_cast<double>(value) / 10.0;
}

double decodeSignedHundredths(quint16 value)
{
    return static_cast<double>(static_cast<qint16>(value)) / 100.0;
}

double decodeManualOutput(quint16 value)
{
    return static_cast<double>(value) / 256.0;
}

quint8 decodeChannelAlarmStatus(quint16 statusRegister, int channel)
{
    const int normalizedChannel = std::clamp(channel, 1, kChannelCount) - 1;
    return normalizedChannel % 2 == 0
        ? static_cast<quint8>((statusRegister >> 8) & 0x00FFu)
        : static_cast<quint8>(statusRegister & 0x00FFu);
}

ChannelControlState decodeChannelControlState(quint16 statusRegister, int channel)
{
    const int normalizedChannel = std::clamp(channel, 1, kChannelCount) - 1;
    const quint8 statusByte = normalizedChannel % 2 == 0
        ? static_cast<quint8>((statusRegister >> 8) & 0x00FFu)
        : static_cast<quint8>(statusRegister & 0x00FFu);
    if ((statusByte & 0x02u) != 0)
    {
        return ChannelControlState::Stopped;
    }
    return (statusByte & 0x01u) == 0
        ? ChannelControlState::AutoTuning
        : ChannelControlState::ApidOutput;
}

QString channelControlStateName(ChannelControlState state, bool english)
{
    switch (state)
    {
    case ChannelControlState::ApidOutput:
        return english ? QStringLiteral("APID output") : QStringLiteral("APID输出");
    case ChannelControlState::AutoTuning:
        return english ? QStringLiteral("Auto tuning") : QStringLiteral("自整定");
    case ChannelControlState::Stopped:
        return english ? QStringLiteral("Output stopped") : QStringLiteral("停止输出");
    case ChannelControlState::Unknown:
        break;
    }
    return english ? QStringLiteral("Output: --") : QStringLiteral("输出：--");
}

quint16 encodeChannelInput(int group, int correctionEntry)
{
    const int normalizedGroup = std::clamp(group, 0, kParameterGroupCount);
    const int normalizedEntry = std::clamp(correctionEntry, 0, 999);
    return static_cast<quint16>(normalizedEntry * 10 + normalizedGroup);
}

int decodeChannelInputGroup(quint16 value)
{
    return static_cast<int>(value % 10u);
}

int decodeCorrectionEntry(quint16 value)
{
    return static_cast<int>(value / 10u);
}

quint16 encodeBaudRate(int baudRate)
{
    return static_cast<quint16>(std::clamp(baudRate / 100, 0, 65535));
}

int decodeBaudRate(quint16 value)
{
    return static_cast<int>(value) * 100;
}

bool isSupportedBaudRate(int baudRate)
{
    switch (baudRate)
    {
    case 4800:
    case 9600:
    case 19200:
    case 38400:
    case 57600:
    case 115200:
        return true;
    default:
        return false;
    }
}

bool isDocumentedRunState(quint16 value)
{
    switch (value)
    {
    case 0:
    case 15:
    case 9655:
        return true;
    default:
        return false;
    }
}

QString pageName(Page page, bool english)
{
    switch (page)
    {
    case Page::Channel:
        return english ? QStringLiteral("channel parameters") : QStringLiteral("通道参数");
    case Page::InputGroup:
        return english ? QStringLiteral("input parameters") : QStringLiteral("输入参数组");
    case Page::OutputGroup:
        return english ? QStringLiteral("output parameters") : QStringLiteral("输出参数组");
    case Page::Global:
        return english ? QStringLiteral("global parameters") : QStringLiteral("全局参数");
    }
    return {};
}

} // namespace VaporView::Ai8TemperatureControllerProtocol
