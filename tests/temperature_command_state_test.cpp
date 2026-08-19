#include "ground/devices/TemperatureCommandState.h"
#include "ground/main/GroundMainWindowSupport.h"

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
    namespace MainSupport = VaporView::Ground::MainSupport;

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

    TemperatureControllerCommand logCommand;
    logCommand.channel = 2;
    logCommand.target_temperature_c = 35.5;
    QVariantMap disconnectedFields =
        MainSupport::temperatureCommandLogFields(CommandId::SetTemperatureTarget,
                                                 logCommand,
                                                 logCommand.channel);
    disconnectedFields.insert(QStringLiteral("reason_code"), QStringLiteral("DEVICE_NOT_CONNECTED"));
    disconnectedFields.insert(
        QStringLiteral("ui_dedupe_key"),
        MainSupport::temperatureCommandDedupeKey(
            QStringLiteral("temperature_command_rejected_not_connected"),
            CommandId::SetTemperatureTarget,
            logCommand.channel));
    const LogRecord disconnectedRecord = MainSupport::makeTemperatureCommandLogRecord(
        LogLevel::Warning,
        QStringLiteral("temperature_command_rejected_not_connected"),
        QStringLiteral("本地激光温控未连接，无法下发温控命令。"),
        disconnectedFields);
    require(disconnectedRecord.level == LogLevel::Warning,
            "rd105DisconnectedCommandIsWarning");
    require(disconnectedRecord.fields.value(QStringLiteral("ui_visibility")).toString() ==
                QStringLiteral("attention"),
            "rd105DisconnectedCommandIsAttention");
    require(disconnectedRecord.fields.value(QStringLiteral("event")).toString() ==
                QStringLiteral("temperature_command_rejected_not_connected") &&
                disconnectedRecord.fields.value(QStringLiteral("reason_code")).toString() ==
                    QStringLiteral("DEVICE_NOT_CONNECTED"),
            "rd105DisconnectedCommandHasStructuredEvent");
    require(disconnectedRecord.fields.value(QStringLiteral("command")).toString() ==
                commandIdName(CommandId::SetTemperatureTarget) &&
                disconnectedRecord.fields.value(QStringLiteral("channel")).toInt() == 2 &&
                disconnectedRecord.fields.value(QStringLiteral("target")).toDouble() == 35.5,
            "rd105DisconnectedCommandCarriesCommandContext");

    QVariantMap failedFields =
        MainSupport::temperatureCommandLogFields(CommandId::SetTemperatureTarget,
                                                 logCommand,
                                                 logCommand.channel);
    failedFields.insert(QStringLiteral("error_code"), QStringLiteral("COMMAND_VERIFY_FAILED"));
    const LogRecord failedRecord = MainSupport::makeTemperatureCommandLogRecord(
        LogLevel::Error,
        QStringLiteral("temperature_command_failed"),
        QStringLiteral("激光温控命令执行失败：写入或读回确认失败。"),
        failedFields);
    require(failedRecord.level == LogLevel::Error &&
                failedRecord.fields.value(QStringLiteral("error_code")).toString() ==
                    QStringLiteral("COMMAND_VERIFY_FAILED"),
            "rd105CommandVerifyFailureIsError");

    QVariantMap successFields =
        MainSupport::temperatureCommandLogFields(CommandId::SetTemperatureTarget,
                                                 logCommand,
                                                 logCommand.channel);
    const LogRecord successRecord = MainSupport::makeTemperatureCommandLogRecord(
        LogLevel::Info,
        QStringLiteral("temperature_command_completed"),
        QStringLiteral("激光温控命令执行成功。"),
        successFields);
    require(successRecord.level == LogLevel::Info &&
                successRecord.fields.value(QStringLiteral("ui_visibility")).toString() ==
                    QStringLiteral("details"),
            "rd105SuccessfulCommandIsInfo");
    require(MainSupport::temperatureCommandDedupeKey(
                QStringLiteral("temperature_command_rejected_not_connected"),
                CommandId::SetTemperatureTarget,
                2) ==
                MainSupport::temperatureCommandDedupeKey(
                    QStringLiteral("temperature_command_rejected_not_connected"),
                    CommandId::SetTemperatureTarget,
                    2),
            "rd105RepeatedDisconnectWarningCanDeduplicate");
    require(MainSupport::commandErrorCodeIdentifier(CommandErrorCode::DeviceNotConnected) ==
                QStringLiteral("DEVICE_NOT_CONNECTED"),
            "command error identifiers are stable machine fields");

    std::cout << "temperature_command_state_test passed\n";
    return 0;
}
