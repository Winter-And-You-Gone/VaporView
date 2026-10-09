#include "FpgaDeviceSession.h"

#include <QCoreApplication>

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace VaporView;
using namespace VaporView::Ground::Devices;

namespace
{

void appendU32(QByteArray& bytes, quint32 value)
{
    for (int i = 0; i < 4; ++i)
        bytes.append(char((value >> (8 * i)) & 0xFF));
}

QByteArray toArray(const std::vector<std::uint8_t>& bytes)
{
    return QByteArray(reinterpret_cast<const char *>(bytes.data()),
                      static_cast<int>(bytes.size()));
}

QByteArray makeTlv(quint16 tag, quint8 type, quint32 value)
{
    QByteArray payload;
    payload.append(char(1));
    payload.append(char(0));
    payload.append(char(1));
    payload.append(char(0));
    payload.append(char(tag & 0xFF));
    payload.append(char(tag >> 8));
    payload.append(char(type));
    payload.append(char(4));
    appendU32(payload, value);
    return payload;
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const quint32 sequence = 1;
    QByteArray responsePayload;
    appendU32(responsePayload, 0);
    responsePayload.append("pong", 4);
    const QByteArray response = toArray(FpgaVlp1::buildFrame(
        FpgaVlp1::FrameType::Response, sequence, 1, 0x00FE,
        std::vector<std::uint8_t>(responsePayload.begin(), responsePayload.end())));

    QByteArray capabilitiesPayload;
    appendU32(capabilitiesPayload, 0);
    appendU32(capabilitiesPayload, 0xFFFF);
    appendU32(capabilitiesPayload, 0x100);
    appendU32(capabilitiesPayload, 100000000);
    appendU32(capabilitiesPayload, 8192);
    appendU32(capabilitiesPayload, 4096);
    appendU32(capabilitiesPayload, 1);
    const QByteArray capabilitiesResponse = toArray(FpgaVlp1::buildFrame(
        FpgaVlp1::FrameType::Response, 2, 1, 0x0001,
        std::vector<std::uint8_t>(capabilitiesPayload.begin(), capabilitiesPayload.end())));

    QByteArray registersPayload;
    appendU32(registersPayload, 0);
    appendU32(registersPayload, 0x6600);
    registersPayload.append(char(2));
    registersPayload.append(char(0));
    registersPayload.append(char(0));
    registersPayload.append(char(0));
    appendU32(registersPayload, 0x00460200);
    appendU32(registersPayload, 0x00000001);
    const QByteArray registersResponse = toArray(FpgaVlp1::buildFrame(
        FpgaVlp1::FrameType::Response, 3, 1, 0x0002,
        std::vector<std::uint8_t>(registersPayload.begin(), registersPayload.end())));

    const QByteArray dataPayload = makeTlv(0x0012, 7, 101459000);
    const QByteArray data = toArray(FpgaVlp1::buildFrame(
        FpgaVlp1::FrameType::Data, 2, 0x0040, 0x1100,
        std::vector<std::uint8_t>(dataPayload.begin(), dataPayload.end())));

    auto transport = std::make_unique<FpgaUsbReplayTransport>(
        response + capabilitiesResponse + registersResponse + data);
    FpgaDeviceSession session(std::move(transport));
    assert(session.open(QStringLiteral("replay")));

    bool commandDone = false;
    bool capabilitiesDone = false;
    bool registersDone = false;
    bool ptbUpdated = false;
    QObject::connect(&session, &FpgaDeviceSession::commandCompleted,
                     [&](quint32 seq, quint16 message, bool success, quint32 status,
                         const QByteArray& payload) {
        assert(seq == sequence);
        assert(message == 0x00FE);
        assert(success);
        assert(status == 0);
        assert(payload.size() == 8);
        commandDone = true;
    });
    QObject::connect(&session, &FpgaDeviceSession::ptbDataUpdated,
                     [&](const PtbData& value) {
        assert(value.valid);
        assert(value.pressure_hpa > 1014.5 && value.pressure_hpa < 1014.7);
        ptbUpdated = true;
    });
    QObject::connect(&session, &FpgaDeviceSession::capabilitiesChanged,
                     [&](const FpgaCapabilities& value) {
        assert(value.valid);
        assert(value.protocolVersion == 0x100);
        assert(value.timeClockHz == 100000000);
        capabilitiesDone = true;
    });
    QObject::connect(&session, &FpgaDeviceSession::registerReadCompleted,
                     [&](quint32 seq, quint32 address, const QVector<quint32>& values) {
        assert(seq == 3);
        assert(address == 0x6600);
        assert(values.size() == 2);
        assert(values[0] == 0x00460200);
        registersDone = true;
    });

    assert(session.ping(QByteArrayLiteral("pong")) == sequence);
    assert(session.requestCapabilities() == 2);
    assert(session.readRegisters(0x6600, 2) == 3);
    session.poll();
    assert(commandDone);
    assert(capabilitiesDone);
    assert(registersDone);
    assert(ptbUpdated);
    assert(session.transport()->diagnostic().bytesWritten > 0);
    session.close();

    std::cout << "fpga_device_session_test passed\n";
    return 0;
}
