#pragma once

#include "FpgaUsbTransport.h"

namespace VaporView::Ground::Devices
{
// Produces and consumes VLP1 wire bytes through the same boundary as USB.
// No OS device is registered and no physical USB/serial endpoint is opened.
std::unique_ptr<FpgaUsbTransport> makeFpgaUsbSimulator(QObject *parent = nullptr);
}
