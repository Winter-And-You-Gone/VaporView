#pragma once

#include "FpgaUsbTransport.h"

namespace VaporView::Ground::Devices
{
// Produces and consumes VLP1 wire bytes through the same boundary as USB.
// No OS device is registered and no physical USB/serial endpoint is opened.
enum class FpgaSimulatorStartup { RunningDemo, Idle };
// The default models the current HCD2 production firmware. Hcd2Ai8Scan models
// the scan-only extension contract; HistoricalExtensions also exposes withdrawn
// AI8 parameter transactions and RD105 for explicit compatibility tests.
enum class FpgaSimulatorFirmware { CurrentHcd2, Hcd2Ai8Scan, HistoricalExtensions };
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
