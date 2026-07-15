#pragma once

#include "TelemetryTypes.h"
#include "data_types.h"

#include <chrono>

namespace VaporView::Ground
{

struct RemoteEpsilonTelemetry
{
    VaporView::EpsilonData data;
    bool available = false;
    bool hasPosition = false;
};

RemoteEpsilonTelemetry decodeRemoteEpsilonTelemetry(
    const VaporView::TelemetryBasic& telemetry,
    std::chrono::steady_clock::time_point timestamp);

}  // namespace VaporView::Ground
