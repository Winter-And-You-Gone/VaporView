#include "ground/devices/TemperatureCommandState.h"

#include <QSettings>
#include "shared/config/SettingsWriteBarrier.h"

#include <array>
#include <chrono>
#include <cstddef>

namespace VaporView::Ground::Devices
{

int temperatureRs485BaudRateForIndex(quint16 index)
{
    static constexpr std::array<int, 8> kRates = {
        4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800};
    return index < kRates.size() ? kRates.at(index) : 9600;
}

TemperatureSerialSettingsUpdate applyConfirmedTemperatureCommand(
    TemperatureControllerData& state,
    CommandId command,
    const TemperatureControllerCommand& payload)
{
    TemperatureSerialSettingsUpdate settingsUpdate;
    const quint8 channel = payload.channel == 0 ? 1 : payload.channel;

    state.valid = true;
    state.timestamp = std::chrono::steady_clock::now();

    const int channelIndex = static_cast<int>(channel - 1);
    if (channelIndex >= 0 && channelIndex < static_cast<int>(state.channels.size()))
    {
        auto& channelData = state.channels[static_cast<std::size_t>(channelIndex)];
        switch (command)
        {
        case CommandId::SetTemperatureTarget:
            channelData.target_temperature_c = payload.target_temperature_c;
            break;
        case CommandId::SetTemperatureOutputEnabled:
            channelData.output_enabled = payload.output_enabled;
            break;
        case CommandId::SetTemperatureOutputMode:
            channelData.output_mode = static_cast<int>(payload.output_mode);
            break;
        case CommandId::SetTemperatureMaxOutputPercent:
            channelData.max_output_percent = static_cast<int>(payload.max_output_percent);
            break;
        case CommandId::SetTemperaturePid:
            channelData.kp = static_cast<int>(payload.kp);
            channelData.ki = static_cast<int>(payload.ki);
            channelData.kd = static_cast<int>(payload.kd);
            break;
        case CommandId::SetTemperatureAutoPid:
            channelData.auto_pid_mode = static_cast<int>(payload.auto_pid_mode);
            break;
        case CommandId::SetTemperatureOvertempUpper:
            channelData.overtemp_upper_c = payload.overtemp_upper_c;
            break;
        case CommandId::SetTemperatureOvertempLower:
            channelData.overtemp_lower_c = payload.overtemp_lower_c;
            break;
        case CommandId::SetTemperatureSlope:
            channelData.temperature_slope_c_per_s = payload.temperature_slope_c_per_s;
            break;
        case CommandId::SetTemperatureStartupDelay:
            channelData.startup_delay_s = static_cast<int>(payload.startup_delay_s);
            break;
        case CommandId::SetTemperatureSensorConfig:
            channelData.sensor_model = static_cast<int>(payload.sensor_model);
            channelData.ntc_b = static_cast<int>(payload.ntc_b);
            channelData.ntc_r0 = static_cast<int>(payload.ntc_r0);
            channelData.pt_r0 = static_cast<int>(payload.pt_r0);
            channelData.pt_a = payload.pt_a;
            channelData.pt_b = payload.pt_b;
            channelData.pt_c = payload.pt_c;
            channelData.polynomial_mantissas = payload.polynomial_mantissas;
            for (std::size_t i = 0; i < channelData.polynomial_exponents.size(); ++i)
            {
                channelData.polynomial_exponents[i] =
                    static_cast<int>(payload.polynomial_exponents[i]);
            }
            break;
        default:
            break;
        }
    }

    switch (command)
    {
    case CommandId::SetTemperatureControllerMode:
        state.controller_mode = static_cast<int>(payload.controller_mode);
        break;
    case CommandId::SetTemperatureDeviceAddress:
        state.device_address = static_cast<int>(payload.device_address);
        settingsUpdate.slaveAddress = state.device_address;
        break;
    case CommandId::SetTemperatureRs485Baud:
        state.rs485_baud_index = static_cast<int>(payload.rs485_baud_index);
        settingsUpdate.baudRate = temperatureRs485BaudRateForIndex(payload.rs485_baud_index);
        break;
    case CommandId::SetTemperatureOvertempOutputMode:
        state.overtemp_output_mode = static_cast<int>(payload.overtemp_output_mode);
        break;
    case CommandId::RestoreTemperatureFactoryDefaults:
        state.device_address = 1;
        state.rs485_baud_index = 1;
        state.overtemp_output_mode = 1;
        settingsUpdate.slaveAddress = 1;
        settingsUpdate.baudRate = 9600;
        break;
    default:
        break;
    }

    return settingsUpdate;
}

void persistTemperatureSerialSettings(const TemperatureSerialSettingsUpdate& update)
{
    if (!update.slaveAddress && !update.baudRate)
    {
        return;
    }

    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
    if (update.slaveAddress)
    {
        VaporView::setPersistentSetting(settings,
            QStringLiteral("serial/temperature_slave_address"),
            *update.slaveAddress);
    }
    if (update.baudRate)
    {
        VaporView::setPersistentSetting(settings,
            QStringLiteral("serial/temperature_baud"),
            QString::number(*update.baudRate));
    }
}

}  // namespace VaporView::Ground::Devices
