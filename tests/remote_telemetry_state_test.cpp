#include "ground/devices/RemoteTelemetryState.h"

#include <QCoreApplication>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    VaporView::Ground::Devices::RemoteTelemetryState state;

    state.setDeviceState(VaporView::SkyDeviceId::Epsilon, VaporView::DeviceState::Connected);
    state.noteStatus(1000);
    state.noteDeviceData(VaporView::SkyDeviceId::Epsilon, 1100);
    require(state.statusFresh(3999), "status remains fresh inside timeout");
    require(!state.statusFresh(4001), "status expires outside timeout");
    require(state.deviceDataFresh(VaporView::SkyDeviceId::Epsilon, 3099, 2000),
            "device data remains fresh inside timeout");
    require(!state.deviceDataFresh(VaporView::SkyDeviceId::Epsilon, 3101, 2000),
            "device data expires outside timeout");

    state.notePacket(VaporView::MsgType::TelemetryBasic, 1000);
    state.notePacket(VaporView::MsgType::TelemetryBasic, 1500);
    state.notePacket(VaporView::MsgType::TelemetryBasic, 2000);
    require(std::abs(state.packetRate(VaporView::MsgType::TelemetryBasic) - 2.0) < 1e-9,
            "packet rate uses the arrival window");

    state.noteWaveformPacket(2, 1000);
    state.noteWaveformPacket(2, 1250);
    require(std::abs(state.waveformPacketRate(2) - 4.0) < 1e-9,
            "waveform channels maintain independent rates");

    state.markLinkClosed();
    require(state.lastStatusMs() == 0 && state.packetRate(VaporView::MsgType::TelemetryBasic) == 0.0,
            "link close invalidates status and packet rates");
    require(state.deviceState(VaporView::SkyDeviceId::Epsilon) == VaporView::DeviceState::Connected,
            "link close preserves the last reported state for reconnect UI");

    state.reset();
    require(state.deviceState(VaporView::SkyDeviceId::Epsilon) == VaporView::DeviceState::Disconnected &&
                state.lastDeviceDataMs(VaporView::SkyDeviceId::Epsilon) == 0,
            "full reset clears device state and timestamps");

    std::cout << "remote_telemetry_state_test passed\n";
    return 0;
}
