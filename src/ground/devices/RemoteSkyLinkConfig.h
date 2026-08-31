#pragma once

#include "TelemetryLink.h"

#include <QString>

namespace VaporView::Ground::Devices
{

// Persistent ground-to-Sky telemetry endpoint settings. This remains independent
// of QWidget state so the connection path can consume it directly.
struct RemoteSkyLinkConfig
{
    VaporView::TelemetryTransportType transport = VaporView::TelemetryTransportType::Tcp;
    QString tcpHost = QStringLiteral("192.168.1.2");
    int tcpPort = 39100;
    QString serialPort;
    int serialBaudRate = 921600;
};

}  // namespace VaporView::Ground::Devices
