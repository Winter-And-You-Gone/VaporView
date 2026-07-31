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
    require(Ai8::groupRegister(Ai8::Register::InputTypeBase, 4) == 0x0803 &&
                Ai8::groupRegister(Ai8::Register::ControlActionBase, 4) == 0x082F,
            "AI-8288 parameter group register range mismatch");

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
