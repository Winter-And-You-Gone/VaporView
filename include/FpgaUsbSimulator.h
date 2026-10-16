#pragma once

#include "FpgaUsbTransport.h"

namespace VaporView::Ground::Devices
{
// Produces and consumes VLP1 wire bytes through the same boundary as USB.
// No OS device is registered and no physical USB/serial endpoint is opened.
enum class FpgaSimulatorStartup { RunningDemo, Idle };
// Idle models a fresh board for a device-row connection; existing demo callers
// retain the running scene unless they explicitly select Idle.
std::unique_ptr<FpgaUsbTransport> makeFpgaUsbSimulator(QObject *parent = nullptr,
    FpgaSimulatorStartup startup = FpgaSimulatorStartup::RunningDemo);
}
