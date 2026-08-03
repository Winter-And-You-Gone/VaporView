#include "Ai8TemperatureControllerCollector.h"

#include <QString>
#include <QVector>

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
         address <= static_cast<quint16>(Protocol::Register::SerialNumberLow);
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

    seedGlobalRegisters(backend, 1);
    Protocol::PageData request = globalRequest();
    request.global.runStateRaw = 1;
    request.global.runStateIsDocumented = false;
    request.global.controlCycleS = 0.5;
    QString message;
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
