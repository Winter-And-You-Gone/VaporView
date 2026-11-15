#pragma once

#include "FpgaUsbTransport.h"

namespace VaporView::Ground::Devices
{
// Produces and consumes VLP1 wire bytes through the same boundary as USB.
// No OS device is registered and no physical USB/serial endpoint is opened.
enum class FpgaSimulatorStartup { RunningDemo, Idle };
// Rd105Integrated models the new firmware. The two HCD2 profiles retain older
// capability sets; HistoricalExtensions additionally exposes withdrawn AI8
// parameter transactions for explicit compatibility tests. All profiles also
// model two explicitly simulated TEC103 devices on the LC0/LC1 raw UARTs.
enum class FpgaSimulatorFirmware { CurrentHcd2, Hcd2Ai8Scan, HistoricalExtensions, Rd105Integrated };
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
    FpgaSimulatorFirmware firmware = FpgaSimulatorFirmware::Rd105Integrated);
}
