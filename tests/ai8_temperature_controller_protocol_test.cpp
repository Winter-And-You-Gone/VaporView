#include "Ai8TemperatureControllerProtocol.h"
#include "TemperatureControllerProtocol.h"

#include <QByteArray>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main()
{
    namespace Ai8 = VaporView::Ai8TemperatureControllerProtocol;
    namespace Modbus = VaporView::TemperatureControllerProtocol;

    require(Ai8::channelRegister(Ai8::Register::MeasuredValueBase, 1) == 0x0600 &&
                Ai8::channelRegister(Ai8::Register::MeasuredValueBase, 8) == 0x0607,
            "AI-8288 PV register range mismatch");
    require(Ai8::channelRegister(Ai8::Register::ChannelOutputBase, 8) == 0x0247 &&
                Ai8::channelRegister(Ai8::Register::ProgramNumberBase, 8) == 0x02A7 &&
                Ai8::channelRegister(Ai8::Register::HighAlarmBase, 8) == 0x03C7 &&
                Ai8::channelRegister(Ai8::Register::DisplayedSetpointBase, 8) == 0x0487,
            "AI-8288 extended channel register range mismatch");
    require(Ai8::groupRegister(Ai8::Register::InputTypeBase, 4) == 0x0803 &&
                Ai8::groupRegister(Ai8::Register::DeviationHighAlarmBase, 4) == 0x0813 &&
                Ai8::groupRegister(Ai8::Register::ControlActionBase, 4) == 0x082F &&
                Ai8::groupRegister(Ai8::Register::SetpointHighLimitBase, 4) == 0x083F,
            "AI-8288 parameter group register range mismatch");
    require(static_cast<quint16>(Ai8::Register::ControlStatusBase) == 0x06C0 &&
                Ai8::kControlStatusRegisterCount == 4 &&
                static_cast<quint16>(Ai8::Register::AlarmStatusBase) == 0x0680 &&
                Ai8::kAlarmStatusRegisterCount == 4,
            "AI-8288 control status register range mismatch");
    require(static_cast<quint16>(Ai8::Register::Address) == 0x0840 &&
                static_cast<quint16>(Ai8::Register::SerialNumberLow) == 0x0855 &&
                static_cast<quint16>(Ai8::Register::OutputStartChannel) == 0x0856 &&
                static_cast<quint16>(Ai8::Register::P1tiOpsn) == 0x085F,
            "AI-8288 global and extension register range mismatch");
    require(Ai8::decodeChannelAlarmStatus(0x0102, 1) == 0x01 &&
                Ai8::decodeChannelAlarmStatus(0x0102, 2) == 0x02,
            "AI-8288 alarm status byte decoding mismatch");
    require(Ai8::decodeChannelControlState(0x0000, 1) == Ai8::ChannelControlState::AutoTuning &&
                Ai8::decodeChannelControlState(0x0100, 1) == Ai8::ChannelControlState::ApidOutput &&
                Ai8::decodeChannelControlState(0x0201, 1) == Ai8::ChannelControlState::Stopped &&
                Ai8::decodeChannelControlState(0x0201, 2) == Ai8::ChannelControlState::ApidOutput,
            "AI-8288 channel control status decoding mismatch");

    require(Ai8::encodeSignedTenths(-12.3) == static_cast<quint16>(static_cast<qint16>(-123)) &&
                std::fabs(Ai8::decodeSignedTenths(Ai8::encodeSignedTenths(-12.3)) + 12.3) < 0.001,
            "signed tenths conversion mismatch");
    require(std::fabs(Ai8::decodeSignedHundredths(Ai8::encodeSignedHundredths(3.25)) - 3.25) < 0.001,
            "signed hundredths conversion mismatch");
    require(Ai8::encodeManualOutput(100.0) == 25600 &&
                std::fabs(Ai8::decodeManualOutput(12800) - 50.0) < 0.001,
            "manual output conversion mismatch");
    require(Ai8::encodeChannelInput(2, 11) == 112 &&
                Ai8::decodeChannelInputGroup(112) == 2 &&
                Ai8::decodeCorrectionEntry(112) == 11,
            "channel input/correction encoding mismatch");
    require(Ai8::encodeBaudRate(19200) == 192 && Ai8::decodeBaudRate(192) == 19200,
            "bAud 0.1K encoding mismatch");
    require(Ai8::isSupportedBaudRate(19200) &&
                !Ai8::isSupportedBaudRate(123457),
            "AI-8288 internal bAud remains a documented fixed-value enum");
    require(Ai8::isDocumentedRunState(0) &&
                Ai8::isDocumentedRunState(15) &&
                Ai8::isDocumentedRunState(9655) &&
                !Ai8::isDocumentedRunState(1) &&
                !Ai8::isDocumentedRunState(65535),
            "Srun documented-value validation mismatch");

    const QByteArray request = Modbus::buildReadRegistersRequest(
        1,
        Ai8::channelRegister(Ai8::Register::MeasuredValueBase, 1),
        Ai8::kChannelCount);
    require(request.size() == 8 && static_cast<quint8>(request.at(0)) == 1 &&
                static_cast<quint8>(request.at(1)) == 3 &&
                static_cast<quint8>(request.at(2)) == 0x06 &&
                static_cast<quint8>(request.at(3)) == 0x00 &&
                static_cast<quint8>(request.at(5)) == Ai8::kChannelCount,
            "eight-channel PV polling request mismatch");

    std::cout << "ai8 temperature controller protocol tests passed\n";
    return 0;
}
