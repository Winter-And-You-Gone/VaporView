#pragma once

#include "FpgaUsbTransport.h"

namespace VaporView::Ground::Devices
{
// Produces and consumes VLP1 wire bytes through the same boundary as USB.
// No OS device is registered and no physical USB/serial endpoint is opened.
enum class FpgaSimulatorStartup { RunningDemo, Idle };
// HistoricalExtensions is an explicit test fixture for the withdrawn AI8/RD105
// extensions. The default models the current HCD2 production firmware.
enum class FpgaSimulatorFirmware { CurrentHcd2, HistoricalExtensions };
class FpgaUsbSimulator : public FpgaUsbTransport
{
public:
    using FpgaUsbTransport::FpgaUsbTransport;
    virtual void setScenario(bool stalled, bool partialFailure) = 0;
    virtual void injectFault(const QString& fault) = 0;
};
// A new board starts at firmware reset. Opening/closing a host connection does
// not power-cycle it. RunningDemo is an explicit fixture for protocol tests.
std::unique_ptr<FpgaUsbSimulator> makeFpgaUsbSimulator(QObject *parent = nullptr,
    FpgaSimulatorStartup startup = FpgaSimulatorStartup::Idle,
    FpgaSimulatorFirmware firmware = FpgaSimulatorFirmware::CurrentHcd2);
}
