#include "SkyDeviceManager.h"
#include "TelemetryCodec.h"

namespace VaporView
{

std::function<CommandErrorCode()> SkyDeviceManager::prepareTemperatureOperation(const CommandMessage& command)
{
    // Simulation updates the manager's model on its owner thread at completion.
    if (simulate_data_) return {};
    TemperatureControllerCommand request;
    const auto id = command.command_id;
    const bool common = id == CommandId::SetTemperatureControllerMode ||
        id == CommandId::SetTemperatureDeviceAddress || id == CommandId::SetTemperatureRs485Baud ||
        id == CommandId::SetTemperatureOvertempOutputMode || id == CommandId::RestoreTemperatureFactoryDefaults;
    if (!TelemetryCodec::parseTemperatureControllerCommand(command.payload, request) ||
        (!common && (request.channel < 1 || request.channel > 2)))
        return [] { return CommandErrorCode::InvalidPayload; };
    const auto collector = temperature_controller_;
    if (!collector || temperature_controller_status_.state != DeviceState::Connected)
        return [] { return CommandErrorCode::DeviceNotConnected; };
    // Only the collector crosses the thread boundary. It serializes hardware I/O;
    // status, configuration and QObjects remain owned by the Core event loop.
    return [collector, request, id] {
        bool ok = false;
        switch (id) {
        case CommandId::SetTemperatureTarget: ok = collector->setTargetTemperature(request.channel, request.target_temperature_c); break;
        case CommandId::SetTemperatureOutputEnabled: ok = collector->setOutputEnabled(request.channel, request.output_enabled); break;
        case CommandId::SetTemperatureOutputMode: ok = collector->setOutputMode(request.channel, request.output_mode); break;
        case CommandId::SetTemperatureMaxOutputPercent: ok = collector->setMaxOutputPercent(request.channel, request.max_output_percent); break;
        case CommandId::SetTemperaturePid: ok = collector->setPid(request.channel, request.kp, request.ki, request.kd); break;
        case CommandId::SetTemperatureAutoPid: ok = collector->setAutoPid(request.channel, request.auto_pid_mode); break;
        case CommandId::SetTemperatureOvertempUpper: ok = collector->setOvertempUpper(request.channel, request.overtemp_upper_c); break;
        case CommandId::SetTemperatureOvertempLower: ok = collector->setOvertempLower(request.channel, request.overtemp_lower_c); break;
        case CommandId::SetTemperatureSlope: ok = collector->setTemperatureSlope(request.channel, request.temperature_slope_c_per_s); break;
        case CommandId::SetTemperatureStartupDelay: ok = collector->setStartupDelay(request.channel, request.startup_delay_s); break;
        case CommandId::SetTemperatureControllerMode: ok = collector->setControllerMode(request.controller_mode); break;
        case CommandId::SetTemperatureDeviceAddress: ok = collector->setDeviceAddress(request.device_address); break;
        case CommandId::SetTemperatureRs485Baud: ok = collector->setRs485BaudIndex(request.rs485_baud_index); break;
        case CommandId::SetTemperatureOvertempOutputMode: ok = collector->setOvertempOutputMode(request.overtemp_output_mode); break;
        case CommandId::RestoreTemperatureFactoryDefaults:
            ok = collector->restoreFactoryDefaults();
            if (ok) collector->setSlaveAddress(1);
            break;
        case CommandId::SetTemperatureSensorConfig:
            ok = collector->setSensorConfig(request.channel, request.sensor_model, request.ntc_b, request.ntc_r0,
                request.pt_r0, request.pt_a, request.pt_b, request.pt_c,
                request.polynomial_mantissas, request.polynomial_exponents);
            break;
        default: return CommandErrorCode::UnknownCommand;
        }
        return ok ? CommandErrorCode::Ok : CommandErrorCode::ConfigApplyFailed;
    };
}


} // namespace VaporView
