#pragma once

#include "FpgaUsbTransport.h"

namespace VaporView::Ground::Devices
{
inline constexpr quint16 DefaultFpgaVirtualTcpPort = 39181;
inline constexpr auto FpgaVirtualTcpBackend = "virtual-tcp";

// The port is the only configurable part of the endpoint. The locator, when
// supplied, must match tcp://127.0.0.1:<port> exactly.
std::unique_ptr<FpgaUsbTransport> makeFpgaVirtualTcpTransport(
    quint16 port = DefaultFpgaVirtualTcpPort);
}
