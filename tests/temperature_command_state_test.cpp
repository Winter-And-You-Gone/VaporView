#include "ground/devices/TemperatureCommandState.h"

#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main()
{
    using namespace VaporView;
    using namespace VaporView::Ground::Devices;

    TemperatureControllerData state;
    TemperatureControllerCommand target;
    target.channel = 0;
    target.target_temperature_c = 42.5;
    const auto targetUpdate = applyConfirmedTemperatureCommand(
        state,
        CommandId::SetTemperatureTarget,
        target);
    require(state.valid, "confirmed command marks state valid");
    require(state.channels[0].target_temperature_c == 42.5, "channel zero maps to channel one");
    require(!targetUpdate.slaveAddress && !targetUpdate.baudRate, "target has no serial setting update");

    TemperatureControllerCommand sensor;
    sensor.channel = 2;
    sensor.sensor_model = 3;
    sensor.ntc_b = 410000;
    sensor.polynomial_mantissas[3] = 123456;
    sensor.polynomial_exponents[3] = -7;
    applyConfirmedTemperatureCommand(state, CommandId::SetTemperatureSensorConfig, sensor);
    require(state.channels[1].sensor_model == 3, "sensor model applied to selected channel");
    require(state.channels[1].ntc_b == 410000, "sensor coefficient applied");
    require(state.channels[1].polynomial_mantissas[3] == 123456, "polynomial mantissa applied");
    require(state.channels[1].polynomial_exponents[3] == -7, "polynomial exponent applied");

    TemperatureControllerCommand baud;
    baud.rs485_baud_index = 5;
    const auto baudUpdate = applyConfirmedTemperatureCommand(
        state,
        CommandId::SetTemperatureRs485Baud,
        baud);
    require(state.rs485_baud_index == 5, "baud index applied");
    require(baudUpdate.baudRate && *baudUpdate.baudRate == 115200, "baud index converted for persistence");

    TemperatureControllerCommand defaults;
    const auto defaultsUpdate = applyConfirmedTemperatureCommand(
        state,
        CommandId::RestoreTemperatureFactoryDefaults,
        defaults);
    require(state.device_address == 1, "factory address restored");
    require(state.rs485_baud_index == 1, "factory baud index restored");
    require(state.overtemp_output_mode == 1, "factory overtemperature mode restored");
    require(defaultsUpdate.slaveAddress && *defaultsUpdate.slaveAddress == 1, "factory address persistence update");
    require(defaultsUpdate.baudRate && *defaultsUpdate.baudRate == 9600, "factory baud persistence update");
    require(temperatureRs485BaudRateForIndex(99) == 9600, "invalid baud index uses compatible fallback");

    std::cout << "temperature_command_state_test passed\n";
    return 0;
}
