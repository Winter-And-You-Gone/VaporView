#pragma once

#include "TelemetryTypes.h"
#include "data_types.h"

#include <optional>

namespace VaporView::Ground::Devices
{

struct TemperatureSerialSettingsUpdate
{
    std::optional<int> slaveAddress;
    std::optional<int> baudRate;
};

int temperatureRs485BaudRateForIndex(quint16 index);

TemperatureSerialSettingsUpdate applyConfirmedTemperatureCommand(
    TemperatureControllerData& state,
    CommandId command,
    const TemperatureControllerCommand& payload);

void persistTemperatureSerialSettings(const TemperatureSerialSettingsUpdate& update);

}  // namespace VaporView::Ground::Devices
