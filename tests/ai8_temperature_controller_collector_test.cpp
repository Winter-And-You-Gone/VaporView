#include "Ai8TemperatureControllerCollector.h"

#include <QString>
#include <QVector>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <utility>
#include <vector>

namespace
{

namespace Protocol = VaporView::Ai8TemperatureControllerProtocol;

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

struct FakeRegisterBackend
{
    std::map<quint16, quint16> registers;
    std::vector<quint16> writes;
    int writeReadBackCount = 0;

    bool read(quint16 address, quint16 count, std::vector<quint16>& values)
    {
        values.clear();
        values.reserve(count);
        for (quint16 offset = 0; offset < count; ++offset)
        {
            values.push_back(registers[static_cast<quint16>(address + offset)]);
        }
        return true;
    }

    bool writeAndConfirm(quint16 address, quint16 value, QString *)
    {
        writes.push_back(address);
        registers[address] = value;
        ++writeReadBackCount;
        return registers[address] == value;
    }

    void resetWrites()
    {
        writes.clear();
        writeReadBackCount = 0;
    }
};

void seedGlobalRegisters(FakeRegisterBackend& backend, quint16 runState)
{
    backend.registers.clear();
    for (quint16 address = static_cast<quint16>(Protocol::Register::Address);
         address <= static_cast<quint16>(Protocol::Register::P1tiOpsn);
         ++address)
    {
        backend.registers[address] = 0;
    }
    backend.registers[static_cast<quint16>(Protocol::Register::Address)] = 1;
    backend.registers[static_cast<quint16>(Protocol::Register::BaudRate)] = 192;
    backend.registers[static_cast<quint16>(Protocol::Register::ControlChannelCount)] = 8;
    backend.registers[static_cast<quint16>(Protocol::Register::RunState)] = runState;
    backend.registers[static_cast<quint16>(Protocol::Register::ControlCycle)] = 4;
    backend.registers[static_cast<quint16>(Protocol::Register::SampleMode)] = 0;
    backend.registers[static_cast<quint16>(Protocol::Register::DecimalPoint)] = 1;
    backend.registers[static_cast<quint16>(Protocol::Register::ParameterLock)] = 0;
}

Protocol::PageData globalRequest()
{
    Protocol::PageData data;
    data.page = Protocol::Page::Global;
    data.global.address = 1;
    data.global.baudRate = 19200;
    data.global.controlChannelCount = 8;
    data.global.controlCycleS = 0.4;
    data.global.runStateRaw = 0;
    data.global.runStateIsDocumented = true;
    data.global.runStateWriteRequested = false;
    data.global.runStateWriteValue = 0;
    data.global.parameterLock = 0;
    data.global.sampleMode = 0;
    data.global.decimalPoint = 1;
    return data;
}

bool containsAddress(const std::vector<quint16>& addresses, quint16 address)
{
    for (quint16 candidate : addresses)
    {
        if (candidate == address)
        {
            return true;
        }
    }
    return false;
}

} // namespace

int main()
{
    VaporView::Ai8TemperatureControllerCollector collector;
    FakeRegisterBackend backend;
    collector.setRegisterBackendForTest(
        [&backend](quint16 address, quint16 count, std::vector<quint16>& values) {
            return backend.read(address, count, values);
        },
        [&backend](quint16 address, quint16 value, QString *errorMessage) {
            return backend.writeAndConfirm(address, value, errorMessage);
        });

    const quint16 runStateRegister = static_cast<quint16>(Protocol::Register::RunState);
    const quint16 controlCycleRegister = static_cast<quint16>(Protocol::Register::ControlCycle);
    QString message;

    auto channelAddress = [](Protocol::Register reg, int channel) {
        return Protocol::channelRegister(reg, channel);
    };
    auto groupAddress = [](Protocol::Register reg, int group) {
        return Protocol::groupRegister(reg, group);
    };

    backend.registers.clear();
    backend.registers[channelAddress(Protocol::Register::SetpointBase, 1)] =
        Protocol::encodeSignedTenths(30.0);
    backend.registers[channelAddress(Protocol::Register::MeasuredValueBase, 1)] =
        Protocol::encodeSignedTenths(29.8);
    backend.registers[channelAddress(Protocol::Register::ProportionalBandBase, 1)] =
        Protocol::encodeUnsignedTenths(20.0);
    backend.registers[channelAddress(Protocol::Register::IntegralTimeBase, 1)] =
        Protocol::encodeUnsignedTenths(100.0);
    backend.registers[channelAddress(Protocol::Register::DerivativeTimeBase, 1)] =
        Protocol::encodeSignedHundredths(50.0);
    backend.registers[channelAddress(Protocol::Register::ChannelInputBase, 1)] =
        Protocol::encodeChannelInput(2, 11);
    backend.registers[channelAddress(Protocol::Register::MeasurementOffsetBase, 1)] =
        Protocol::encodeSignedTenths(-1.2);
    backend.registers[channelAddress(Protocol::Register::ChannelOutputBase, 1)] = 2;
    backend.registers[channelAddress(Protocol::Register::ProgramNumberBase, 1)] = 1234;
    backend.registers[channelAddress(Protocol::Register::WorkModeBase, 1)] = 0;
    backend.registers[channelAddress(Protocol::Register::ManualOutputBase, 1)] =
        Protocol::encodeManualOutput(4.5);
    backend.registers[channelAddress(Protocol::Register::HighAlarmBase, 1)] =
        Protocol::encodeSignedTenths(320.0);
    backend.registers[channelAddress(Protocol::Register::LowAlarmBase, 1)] =
        Protocol::encodeSignedTenths(-20.0);
    backend.registers[channelAddress(Protocol::Register::DisplayedSetpointBase, 1)] =
        Protocol::encodeSignedTenths(30.0);
    backend.registers[static_cast<quint16>(Protocol::Register::AlarmStatusBase)] = 0x0102;
    Protocol::Selection selection;
    selection.channel = 1;
    Protocol::PageData page;
    require(collector.readPage(Protocol::Page::Channel, selection, page, &message),
            "extended AI-8288 channel page can be read");
    require(page.channel.channelInputGroup == 2 &&
                page.channel.correctionEntry == 11 &&
                page.channel.channelOutputGroupRaw == 2 &&
                page.channel.programNumber == 1234 &&
                std::fabs(page.channel.highAlarmC - 320.0) < 0.001 &&
                std::fabs(page.channel.lowAlarmC + 20.0) < 0.001 &&
                page.channel.alarmStatusValid &&
                page.channel.alarmStatusRaw == 0x01,
            "extended AI-8288 channel fields are decoded");

    backend.resetWrites();
    Protocol::PageData channelRequest;
    channelRequest.page = Protocol::Page::Channel;
    channelRequest.selection.channel = 1;
    channelRequest.channel.setpointC = 30.0;
    channelRequest.channel.proportionalBand = 20.0;
    channelRequest.channel.integralTimeS = 100.0;
    channelRequest.channel.derivativeTimeS = 50.0;
    channelRequest.channel.channelOutputGroupRaw = 3;
    channelRequest.channel.programNumber = 4321;
    channelRequest.channel.workMode = 0;
    channelRequest.channel.highAlarmC = 310.0;
    channelRequest.channel.lowAlarmC = -10.0;
    require(collector.writePage(channelRequest, &message),
            "extended AI-8288 channel page can be written");
    require(containsAddress(backend.writes, channelAddress(Protocol::Register::ChannelOutputBase, 1)) &&
                containsAddress(backend.writes, channelAddress(Protocol::Register::ProgramNumberBase, 1)) &&
                containsAddress(backend.writes, channelAddress(Protocol::Register::HighAlarmBase, 1)) &&
                containsAddress(backend.writes, channelAddress(Protocol::Register::LowAlarmBase, 1)) &&
                !containsAddress(backend.writes, channelAddress(Protocol::Register::ManualOutputBase, 1)),
            "extended channel write touches the documented writable registers");

    backend.registers.clear();
    selection = {};
    selection.outputGroup = 2;
    backend.registers[groupAddress(Protocol::Register::ControlActionBase, 2)] = 0x00F0;
    backend.registers[groupAddress(Protocol::Register::DeviationHighAlarmBase, 2)] =
        Protocol::encodeSignedTenths(30.0);
    backend.registers[groupAddress(Protocol::Register::DeviationLowAlarmBase, 2)] =
        Protocol::encodeSignedTenths(-30.0);
    backend.registers[groupAddress(Protocol::Register::HysteresisBase, 2)] =
        Protocol::encodeSignedTenths(2.0);
    backend.registers[groupAddress(Protocol::Register::OutputLowBase, 2)] = 5;
    backend.registers[groupAddress(Protocol::Register::OutputHighBase, 2)] = 95;
    backend.registers[groupAddress(Protocol::Register::OutputHighThresholdBase, 2)] =
        Protocol::encodeSignedTenths(300.0);
    backend.registers[groupAddress(Protocol::Register::RiseSlopeBase, 2)] =
        Protocol::encodeUnsignedTenths(1.0);
    backend.registers[groupAddress(Protocol::Register::FallSlopeBase, 2)] =
        Protocol::encodeUnsignedTenths(2.0);
    backend.registers[groupAddress(Protocol::Register::SetpointLowLimitBase, 2)] =
        Protocol::encodeSignedTenths(-50.0);
    backend.registers[groupAddress(Protocol::Register::SetpointHighLimitBase, 2)] =
        Protocol::encodeSignedTenths(300.0);
    backend.registers[groupAddress(Protocol::Register::AlarmResetBase, 2)] = 31;
    require(collector.readPage(Protocol::Page::OutputGroup, selection, page, &message),
            "extended AI-8288 output page can be read");
    require(page.output.controlAction == 0 &&
                std::fabs(page.output.deviationHighAlarm - 30.0) < 0.001 &&
                std::fabs(page.output.deviationLowAlarm + 30.0) < 0.001 &&
                std::fabs(page.output.outputHighThreshold - 300.0) < 0.001 &&
                std::fabs(page.output.setpointLowLimit + 50.0) < 0.001 &&
                std::fabs(page.output.setpointHighLimit - 300.0) < 0.001,
            "extended AI-8288 output fields are decoded");
    backend.resetWrites();
    Protocol::PageData outputRequest = page;
    outputRequest.output.controlAction = 1;
    outputRequest.output.deviationHighAlarm = 35.0;
    outputRequest.output.deviationLowAlarm = -35.0;
    outputRequest.output.outputHighThreshold = 280.0;
    outputRequest.output.setpointLowLimit = -40.0;
    outputRequest.output.setpointHighLimit = 280.0;
    require(collector.writePage(outputRequest, &message),
            "extended AI-8288 output page can be written");
    require(backend.registers[groupAddress(Protocol::Register::ControlActionBase, 2)] == 0x00F1 &&
                containsAddress(backend.writes, groupAddress(Protocol::Register::DeviationHighAlarmBase, 2)) &&
                containsAddress(backend.writes, groupAddress(Protocol::Register::DeviationLowAlarmBase, 2)) &&
                containsAddress(backend.writes, groupAddress(Protocol::Register::OutputHighThresholdBase, 2)) &&
                containsAddress(backend.writes, groupAddress(Protocol::Register::SetpointLowLimitBase, 2)) &&
                containsAddress(backend.writes, groupAddress(Protocol::Register::SetpointHighLimitBase, 2)),
            "extended output write preserves Act bits and writes new fields");

    seedGlobalRegisters(backend, 1);
    backend.registers[static_cast<quint16>(Protocol::Register::LocalInputChannelCount)] = 8;
    backend.registers[static_cast<quint16>(Protocol::Register::ExpansionInputChannelCount)] = 8;
    backend.registers[static_cast<quint16>(Protocol::Register::CommonAlarmOutput)] = 0x0018;
    backend.registers[static_cast<quint16>(Protocol::Register::IndependentAlarmChannelCount)] = 4;
    backend.registers[static_cast<quint16>(Protocol::Register::IndependentAlarmMask)] = 0x000F;
    backend.registers[static_cast<quint16>(Protocol::Register::AlarmFunctionA)] = 0x0001;
    backend.registers[static_cast<quint16>(Protocol::Register::AlarmFunctionB)] = 0x0002;
    backend.registers[static_cast<quint16>(Protocol::Register::ParityFlags)] = 0;
    backend.registers[static_cast<quint16>(Protocol::Register::AlarmPolarity)] = 0x0003;
    backend.registers[static_cast<quint16>(Protocol::Register::ExtraHysteresis)] =
        Protocol::encodeUnsignedTenths(2.0);
    backend.registers[static_cast<quint16>(Protocol::Register::MainStatus)] = 0x0200;
    backend.registers[static_cast<quint16>(Protocol::Register::ModelFeature)] = 0x218A;
    backend.registers[static_cast<quint16>(Protocol::Register::SerialNumberHigh)] = 0x2000;
    backend.registers[static_cast<quint16>(Protocol::Register::SerialNumberLow)] = 0x1919;
    backend.registers[static_cast<quint16>(Protocol::Register::OutputStartChannel)] = 1;
    backend.registers[static_cast<quint16>(Protocol::Register::HighResolutionFilter)] = 20;
    backend.registers[static_cast<quint16>(Protocol::Register::Aif1)] = 150;
    backend.registers[static_cast<quint16>(Protocol::Register::Aif2)] = 15;
    backend.registers[static_cast<quint16>(Protocol::Register::P1faAif3)] = 9999;
    backend.registers[static_cast<quint16>(Protocol::Register::Difa)] = 2;
    backend.registers[static_cast<quint16>(Protocol::Register::Spsr)] = 100;
    backend.registers[static_cast<quint16>(Protocol::Register::AtFunction)] = 55;
    backend.registers[static_cast<quint16>(Protocol::Register::AiflP1pr)] = 0;
    backend.registers[static_cast<quint16>(Protocol::Register::P1tiOpsn)] = 0;
    Protocol::PageData globalReadBack;
    require(collector.readPage(Protocol::Page::Global, {}, globalReadBack, &message),
            "AI-8288 global diagnostic block can be read");
    require(globalReadBack.global.localInputChannelCount == 8 &&
                globalReadBack.global.expansionInputChannelCount == 8 &&
                globalReadBack.global.commonAlarmOutput == 0x0018 &&
                globalReadBack.global.independentAlarmChannelCount == 4 &&
                globalReadBack.global.independentAlarmMask == 0x000F &&
                globalReadBack.global.alarmFunctionA == 0x0001 &&
                globalReadBack.global.alarmFunctionB == 0x0002 &&
                globalReadBack.global.alarmPolarity == 0x0003 &&
                std::fabs(globalReadBack.global.extraHysteresis - 2.0) < 0.001 &&
                globalReadBack.global.mainStatusRaw == 0x0200 &&
                globalReadBack.global.modelFeature == 0x218A &&
                globalReadBack.global.serialNumber == 0x20001919u &&
                globalReadBack.global.outputStartChannel == 1 &&
                globalReadBack.global.highResolutionFilter == 20 &&
                globalReadBack.global.aif1 == 150 &&
                globalReadBack.global.aif2 == 15 &&
                globalReadBack.global.p1faAif3 == 9999 &&
                globalReadBack.global.difa == 2 &&
                globalReadBack.global.spsr == 100 &&
                globalReadBack.global.atFunction == 55,
            "AI-8288 global diagnostic fields are decoded");
    Protocol::PageData request = globalRequest();
    request.global.runStateRaw = 1;
    request.global.runStateIsDocumented = false;
    request.global.controlCycleS = 0.5;
    require(collector.writePage(request, &message),
            "changing CtI succeeds when the device reports an unknown Srun value");
    require(containsAddress(backend.writes, controlCycleRegister) &&
                !containsAddress(backend.writes, runStateRegister),
            "changing CtI does not write the Srun register");
    require(backend.registers[runStateRegister] == 1,
            "unknown Srun raw value remains unchanged after another field is saved");
    require(!message.contains(QStringLiteral("Srun")),
            "save result does not claim that Srun was modified");

    for (quint16 runState : {static_cast<quint16>(0),
                             static_cast<quint16>(15),
                             static_cast<quint16>(9655)})
    {
        seedGlobalRegisters(backend, runState);
        backend.resetWrites();
        request = globalRequest();
        request.global.runStateRaw = runState;
        request.global.runStateIsDocumented = true;
        request.global.controlCycleS = 0.5;
        require(collector.writePage(request, &message),
                "changing CtI succeeds for a documented unchanged Srun value");
        require(!containsAddress(backend.writes, runStateRegister),
                "an unchanged documented Srun value is not written");
    }

    for (const auto& change : std::vector<std::pair<quint16, quint16>>{{1, 0}, {0, 15}, {9655, 0}})
    {
        seedGlobalRegisters(backend, change.first);
        backend.resetWrites();
        request = globalRequest();
        request.global.runStateRaw = change.first;
        request.global.runStateIsDocumented = Protocol::isDocumentedRunState(change.first);
        request.global.runStateWriteRequested = true;
        request.global.runStateWriteValue = change.second;
        require(collector.writePage(request, &message),
                "an explicitly selected documented Srun value is written successfully");
        require(backend.writes.size() == 1 && backend.writes.front() == runStateRegister &&
                    backend.registers[runStateRegister] == change.second &&
                    backend.writeReadBackCount == 1,
                "explicit Srun change writes the correct register and performs confirmation");

        Protocol::PageData confirmed;
        require(collector.readPage(Protocol::Page::Global, {}, confirmed, &message),
                "global page can be read back after an explicit Srun change");
        require(confirmed.global.runStateRaw == change.second &&
                    confirmed.global.runStateIsDocumented &&
                    !confirmed.global.runStateWriteRequested,
                "read-back clears the Srun write intent");
    }

    for (quint16 invalidValue : {static_cast<quint16>(1),
                                 static_cast<quint16>(2),
                                 static_cast<quint16>(65535)})
    {
        seedGlobalRegisters(backend, 0);
        backend.resetWrites();
        request = globalRequest();
        request.global.controlCycleS = 0.5;
        request.global.runStateWriteRequested = true;
        request.global.runStateWriteValue = invalidValue;
        require(!collector.writePage(request, &message),
                "an undocumented Srun write request is rejected");
        require(backend.writes.empty() && backend.registers[controlCycleRegister] == 4,
                "an invalid Srun request is rejected before any register write");
        require(message.contains(QString::number(static_cast<uint>(invalidValue))) &&
                    message.contains(QStringLiteral("Srun")),
                "invalid Srun error identifies the unsupported value");
    }

    std::cout << "ai8 temperature controller collector tests passed\n";
    return 0;
}
