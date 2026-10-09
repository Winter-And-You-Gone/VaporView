#include "EpsilonSettings.h"
#include "EpsilonMaintenance.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

using namespace VaporView;
namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

struct Device
{
    std::map<std::string, double> values;
    std::vector<std::string> commands;
    std::string missing;
    std::string write_failure;
    bool save_failure = false;
    bool exit_failure = false;
    bool entry_failure = false;
    bool readback_failure = false;
    bool throw_query = false;
    int saves = 0;

    Device()
    {
        for (const auto group : epsilonSettingsGroups())
            for (const auto& descriptor : epsilonParameterDescriptors(group))
                values[descriptor.name] = 0;
    }
    std::string exchange(const std::string& command, int interval)
    {
        require(interval == 1500, "all settings commands preserve vendor 1500 ms interval");
        require(command.size() >= 2 && command.substr(command.size() - 2) == "\r\n", "CRLF terminates every command");
        commands.push_back(command);
        if (command == "#fconfig\r\n")
            return entry_failure ? "" : "*#OK\r\n";
        if (command == "#fdeconfig\r\n")
            return exit_failure ? "" : "*#OK\r\n";
        if (command == "#fsave\r\n")
        {
            ++saves;
            return save_failure ? "*#ERROR\r\n" : "*#OK\r\n";
        }
        std::istringstream input(command);
        std::string prefix, action, name;
        input >> prefix >> action >> name;
        require(prefix == "#fparam" && epsilonParameterDescriptor(name), "only whitelist parameter commands");
        if (action == "get")
        {
            if (throw_query)
                throw std::runtime_error("disconnected");
            if (name == missing || (saves && readback_failure))
                return command + "*#OK\r\n"; // Echo/ack are not values.
            return command + name + "=" + std::to_string(values.at(name)) + "\r\n";
        }
        require(action == "set", "settings use documented get/set grammar");
        if (name == write_failure)
            return "*#ERROR\r\n";
        double value = 0;
        input >> value;
        values[name] = value;
        return "*#OK\r\n";
    }
    EpsilonSettingsExchange channel()
    {
        return [this](const std::string& command, int interval) { return exchange(command, interval); };
    }
    int writes() const
    {
        int result = 0;
        for (const auto& command : commands)
            if (command.find("#fparam set ") == 0)
                ++result;
        return result;
    }
    void requireExited() const
    {
        require(!commands.empty() && commands.back() == "#fdeconfig\r\n", "every configuration attempt exits mode");
    }
};
}

int main()
{
    {
        std::string value, error;
        require(parseEpsilonDgnssResponse("NTRIP_PASSWORD：failed-error-password\r\n", "NTRIP_PASSWORD", value) && value == "failed-error-password",
                "DGNSS values may contain ordinary error words and fullwidth delimiter");
        require(!parseEpsilonDgnssResponse("NTRIP_ACCOUNT=a\r\nNTRIP_ACCOUNT=b\r\n", "NTRIP_ACCOUNT", value), "DGNSS rejects conflicting duplicate values");
        require(!validateEpsilonDgnss({{{"NTRIP_PASSWORD", "bad\r\n#freboot"}}}, error), "DGNSS rejects injection");
        require(!validateEpsilonDgnss({{{"RTCM_TYPE", "2"}}}, error), "conflicting RTCM type mapping is read-only");
        std::map<std::string,std::string> values{{"NTRIP_ACCOUNT","old"}};
        int saves = 0;
        std::vector<std::string> commands;
        const EpsilonSettingsExchange exchange = [&](const std::string& command,int) {
            commands.push_back(command);
            if (command == "#fsave\r\n") { ++saves; return std::string("*#OK\r\n"); }
            if (command.find("#fdgnss get ") == 0) return "NTRIP_ACCOUNT=" + values["NTRIP_ACCOUNT"] + "\r\n";
            if (command.find("#fdgnss set ") == 0) values["NTRIP_ACCOUNT"] = "new";
            return std::string("*#OK\r\n");
        };
        EpsilonDgnssSnapshot snapshot;
        require(applyEpsilonDgnss({{{"NTRIP_ACCOUNT","new"}}}, exchange, snapshot, error) && saves == 1 &&
                snapshot.saved && snapshot.readback_verified && commands.back() == "#fdeconfig\r\n", "real DGNSS helper writes saves verifies and exits");
    }
    for (const std::string failureLine : {"", "ERROR write rejected\r\n", "FAILED write rejected\r\n", "UNSUPPORTED field\r\n", "*#ERROR\r\n"})
    {
        const std::string requested = "error-failed-unsupported.example";
        std::string value = "old", error;
        int saves = 0;
        std::vector<std::string> commands;
        const EpsilonSettingsExchange exchange = [&](const std::string& command, int) {
            commands.push_back(command);
            if (command.find("#fdgnss get ") == 0) return "NTRIP_PASSWORD=" + value + "\r\n";
            if (command.find("#fdgnss set ") == 0)
            {
                value = requested;
                return command + failureLine + "*#OK\r\n";
            }
            if (command == "#fsave\r\n") ++saves;
            return std::string("*#OK\r\n");
        };
        EpsilonDgnssSnapshot snapshot;
        const bool success = applyEpsilonDgnss({{{"NTRIP_PASSWORD", requested}}}, exchange, snapshot, error);
        require(success == failureLine.empty(), "DGNSS write ACK ignores error words in echoed values but rejects independent error lines");
        require(commands.back() == "#fdeconfig\r\n" && snapshot.restart_required,
                "DGNSS echoed write preserves write risk and exits configuration");
        require(failureLine.empty() ? snapshot.saved && snapshot.readback_verified && saves == 1
                                    : !snapshot.saved && !snapshot.readback_verified && saves == 0 && !error.empty(),
                "DGNSS rejected ACK cannot claim save or readback success");
    }
    {
        EpsilonMaintenanceResult result;
        std::string error;
        int exits = 0, aidWrites = 0, saves = 0;
        const EpsilonMaintenanceExchange exchange = [&](const std::string& command, int, const EpsilonMaintenanceWriteTrace& written) {
            if (written) written();
            if (command.find("#fparam get ") == 0)
            { const auto key = command.substr(12, command.size()-14); return key + "=1\r\n"; }
            if (command.find("#fparam set ") == 0) ++aidWrites;
            if (command == "#fsave\r\n") ++saves;
            if (command == "#fdeconfig\r\n" && ++exits == 1) throw std::runtime_error("exit read disconnected");
            return std::string("*#OK\r\n");
        };
        require(!runEpsilonMaintenance(EpsilonMaintenanceAction::Magnetic2D, exchange, result, error, {}, [] { return true; }) &&
                aidWrites == 6 && saves == 1 && exits == 2,
                "magnetic first exit exception still restores and saves every changed AID");
        require(result.status == EpsilonMaintenanceStatus::Cancelled && result.restart_required && !result.saved &&
                error.find("exit request") != std::string::npos && result.error == error,
                "magnetic exit exception remains an unverified failure after successful AID restoration");
    }
    for (int scenario = 0; scenario < 5; ++scenario)
    {
        int poll = 0;
        std::string error;
        EpsilonMaintenanceResult result;
        std::vector<std::string> commands;
        const EpsilonMaintenanceExchange exchange = [&](const std::string& command, int, const EpsilonMaintenanceWriteTrace& written) {
            commands.push_back(command);
            if (written) written();
            if (command.find("#fparam get ") == 0)
            { const auto key = command.substr(12, command.size()-14); return key + "=1\r\n"; }
            if (command == "#fmagcal2d\r\n") return std::string("*#OK\r\nNow: 1");
            if (command.empty())
            {
                ++poll;
                if (scenario == 0 || scenario == 4) return std::string("00 percent\r\n");
                return std::string("\r\n*#OK\r\n");
            }
            if (scenario == 4 && poll && command.find("#fparam set ") == 0) return std::string("*#ERROR\r\n");
            return std::string("*#OK\r\n");
        };
        const bool success = runEpsilonMaintenance(EpsilonMaintenanceAction::Magnetic2D, exchange, result, error, {},
            [&]() { return scenario == 2 && poll > 0; });
        require(success == (scenario == 0), "magnetic completion requires real progress and successful AID restoration");
        if (scenario == 2) require(result.status == EpsilonMaintenanceStatus::Cancelled, "magnetic cancellation is explicitly unverified");
        if (scenario == 4) require(result.saved && result.status == EpsilonMaintenanceStatus::Failed, "auto-save survives AID restore failure");
        if (scenario == 1 || scenario == 3) require(poll == 300, "ACK-only magnetic reports never complete before timeout");
    }
    {
        EpsilonMaintenanceResult result;
        std::string error;
        int polls = 0, aidWrites = 0;
        const EpsilonMaintenanceExchange exchange = [&](const std::string& command, int, const EpsilonMaintenanceWriteTrace& written) {
            if (command == "#fmagcal2d\r\n") return std::string(); // Short write: no trace.
            if (written) written();
            if (command.empty()) ++polls;
            if (command.find("#fparam set ") == 0) ++aidWrites;
            if (command.find("#fparam get ") == 0)
            { const auto key = command.substr(12, command.size()-14); return key + "=1\r\n"; }
            return std::string("*#OK\r\n");
        };
        require(!runEpsilonMaintenance(EpsilonMaintenanceAction::Magnetic2D, exchange, result, error) &&
                result.status == EpsilonMaintenanceStatus::Failed && result.restart_required && polls == 0 && aidWrites == 6,
                "short magnetic command write fails without polling and restores AID despite earlier successful AID writes");
    }
    for (int scenario = 0; scenario < 3; ++scenario)
    {
        int poll = 0;
        EpsilonMaintenanceResult result;
        std::string error;
        const EpsilonMaintenanceExchange exchange = [&](const std::string& command, int, const EpsilonMaintenanceWriteTrace& written) {
            if (written) written();
            if (command.find("#fparam get ") == 0)
            { const auto key = command.substr(12, command.size()-14); return key + "=0\r\n"; }
            if (command == "#fmagcal3d\r\n") return std::string("*#OK\r\n");
            if (command.empty())
            {
                ++poll;
                if (scenario == 0) return std::string("The fitting error of the current calculation: 2.5\r\nCalibration Algorithm: High\r\n");
                if (poll == 1) return std::string("The fitting error of the current calculation: 1\r\nCalibration Algorithm: Low\r\n");
                if (scenario == 1) return std::string("This is a magnetometer 3D calibration.\r\nCalibration Algorithm: High\r\n");
                return std::string("The fitting error of the current calculation: NaN\r\nCalibration Algorithm: High\r\n");
            }
            return std::string("*#OK\r\n");
        };
        require(runEpsilonMaintenance(EpsilonMaintenanceAction::Magnetic3D, exchange, result, error) == (scenario == 0),
                "3D magnetic calibration requires same report finite fitting error below three and High algorithm");
    }
    for (int scenario = 0; scenario < 3; ++scenario)
    {
        EpsilonMaintenanceResult result;
        std::string error;
        int aidWrites = 0;
        const EpsilonMaintenanceExchange exchange = [&](const std::string& command, int, const EpsilonMaintenanceWriteTrace& written) {
            if (written) written();
            if (command.find("#fparam set ") == 0) ++aidWrites;
            if (command == "#fparam get AID_MAG_2D_MAGNETIC\r\n") return std::string("AID_MAG_2D_MAGNETIC=1\r\n");
            if (command == "#fparam get AID_MAG_3D_MAGNETIC\r\n") return scenario == 0 ? std::string("*#ERROR\r\n") : std::string("AID_MAG_3D_MAGNETIC=0\r\n");
            if (command == "#fparam get AID_MAG_V_MAGNETIC\r\n") return std::string("AID_MAG_V_MAGNETIC=1\r\n");
            if (command == "#fdeconfig\r\n" && scenario == 2) return std::string("*#ERROR\r\n");
            return std::string("*#OK\r\n");
        };
        require(!runEpsilonMaintenance(EpsilonMaintenanceAction::Magnetic2D, exchange, result, error, {}, [] { return true; }),
                "magnetic alias conflict or cancellation does not claim completion");
        if (scenario == 0) require(result.restart_required && aidWrites > 0, "cancel after disabling supported MAG_V alias preserves reboot risk and restores AID");
        else require(aidWrites == 0, "conflicting aliases issue no AID writes");
        if (scenario == 2) require(error.find("exit request") != std::string::npos, "failed magnetic exit is retained even without AID changes");
    }
    for (int scenario = 0; scenario < 7; ++scenario)
    {
        std::vector<std::string> commands;
        EpsilonMaintenanceResult result;
        std::string error;
        const EpsilonMaintenanceExchange exchange = [&](const std::string& command, int interval,
                                                        const EpsilonMaintenanceWriteTrace& written) {
            require(interval == 1500, "maintenance preserves official command interval");
            commands.push_back(command);
            if (command == "#fimucal_acce\r\n")
            {
                if (written) written();
                if (scenario == 5) throw std::runtime_error("read after successful write failed");
                return scenario == 1 ? std::string() : std::string("*#OK\r\n");
            }
            if ((scenario == 2 && command == "#fconfig\r\n") ||
                (scenario == 3 && command == "#fsave\r\n") ||
                (scenario == 4 && command == "#fdeconfig\r\n"))
                return std::string("*#ERROR\r\n*#OK\r\n");
            if (scenario == 6 && command == "#fdeconfig\r\n")
                throw std::runtime_error("exit disconnected");
            return std::string("*#OK\r\n");
        };
        const bool success = runEpsilonMaintenance(EpsilonMaintenanceAction::Accelerometer, exchange, result, error);
        require(success == (scenario <= 1), "real maintenance helper distinguishes acknowledgement, silence and failures");
        require(commands.back() == "#fdeconfig\r\n", "maintenance always attempts configuration exit");
        if (scenario == 0)
            require(result.status == EpsilonMaintenanceStatus::Acknowledged && result.saved && result.restart_required,
                    "acknowledged static tare is saved and needs restart, not completed");
        if (scenario == 1)
            require(result.status == EpsilonMaintenanceStatus::SentUnverified && result.saved && result.restart_required,
                    "silent action response stays sent-unverified despite save acknowledgement");
        if (scenario == 2)
            require(commands.size() == 2 && !result.restart_required, "mixed error and OK entry does not send tare");
        if (scenario == 3)
            require(!result.saved && result.restart_required, "mixed save response is not persistence acknowledgement");
        if (scenario == 5)
            require(result.restart_required && !result.saved, "read exception preserves completed action write fact");
    }
    for (const auto group : epsilonSettingsGroups())
    {
        Device device;
        EpsilonSettingsSnapshot groupSnapshot;
        std::string groupError;
        require(readEpsilonSettings(group, device.channel(), groupSnapshot, groupError), "all seven groups can read independently");
        require(groupSnapshot.values.size() == epsilonParameterDescriptors(group).size(), "each group snapshot includes its own descriptors");
    }
    for (const double currentProtocol : {0.0, 1.0, 2.0, 999.0})
    {
        Device device;
        device.values["COMM_BAUD2"] = 5;
        device.values["COMM_STREAM_TYP2"] = currentProtocol;
        EpsilonSettingsSnapshot communicationSnapshot;
        std::string communicationError;
        const bool applied = applyEpsilonSettings({EpsilonSettingsGroup::Communication, {{"COMM_BAUD2", 6}}},
                                                  device.channel(), communicationSnapshot, communicationError);
        require(applied == (currentProtocol == 0 || currentProtocol == 2), "Main and unknown protocol prevent baud writes");
        require(device.writes() == (applied ? 1 : 0), "protected communication request issues no write");
        device.requireExited();
    }
    {
        Device device;
        device.missing = "COMM_STREAM_TYP3";
        EpsilonSettingsSnapshot communicationSnapshot;
        std::string communicationError;
        require(!applyEpsilonSettings({EpsilonSettingsGroup::Communication, {{"COMM_BAUD3", 6}}}, device.channel(),
                                      communicationSnapshot, communicationError) && device.writes() == 0,
                "unreadable control protocol prevents baud writes");
        device.requireExited();
    }
    require(epsilonSettingsGroups().size() == 8, "all documented settings groups are exposed");
    require(epsilonParameterDescriptor("COMM_BAUD2") != nullptr &&
            epsilonParameterDescriptor("FILT_LPF_CUTOFF_FREQ_ACC_XY") != nullptr &&
            epsilonParameterDescriptor("IMU_RANGE_ACC") != nullptr &&
            epsilonParameterDescriptor("USER_DEFINE_L_IMU_POINT_X") != nullptr,
            "second-stage parameter descriptors are discoverable");
    {
        std::string descriptorError;
        require(validateEpsilonSettings({EpsilonSettingsGroup::Sensors, {{"IMU_RANGE_ACC", 0}}}, descriptorError),
                "documented non-monotonic sensor enum value is accepted");
        require(!validateEpsilonSettings({EpsilonSettingsGroup::Filters, {{"FILT_LPF_CUTOFF_FREQ_ACC_XY", -1}}}, descriptorError),
                "filter cutoff cannot be negative");
        require(validateEpsilonSettings({EpsilonSettingsGroup::Communication, {{"COMM_STREAM_TYP2", 2}}}, descriptorError),
                "secondary stream can use documented NAV protocol");
        require(!validateEpsilonSettings({EpsilonSettingsGroup::Communication, {{"COMM_STREAM_TYP2", 1}}}, descriptorError),
                "cannot create a second Main stream");
        require(!validateEpsilonSettings({EpsilonSettingsGroup::ExternalAids, {{"ODOM_SCAL1", 0}}}, descriptorError),
                "odometer scale must be positive");
        require(validateEpsilonSettings({EpsilonSettingsGroup::ExternalAids, {{"ODOM_TYPE", 3}, {"ODOM_SCAL1", 1.02}}}, descriptorError),
                "user odometer settings accept documented type and positive scale");
        require(!validateEpsilonSettings({EpsilonSettingsGroup::Sensors, {{"GPIO_1_FUNCTION", 0}}}, descriptorError),
                "internal GNSS PPS GPIO1 cannot be reconfigured");
        require(validateEpsilonSettings({EpsilonSettingsGroup::Sensors, {{"GPIO_2_FUNCTION", 2}}}, descriptorError),
                "external GPIO2 PPS input can be configured");
        for (int code = 10; code <= 24; ++code)
            require(validateEpsilonSettings({EpsilonSettingsGroup::Communication, {{"COMM_STREAM_TYP2", static_cast<double>(code)}}}, descriptorError),
                    "EPSILON common external input protocol enum is accepted");
        require(!validateEpsilonSettings({EpsilonSettingsGroup::Communication, {{"COMM_STREAM_TYP2", 25}}}, descriptorError),
                "unverified DroneCAN protocol remains unavailable");
        require(validateEpsilonSettings({EpsilonSettingsGroup::Communication, {{"MSG_OUT_NMEA", 4}}}, descriptorError),
                "NMEA five Hz uses documented enum four");
        require(!validateEpsilonSettings({EpsilonSettingsGroup::Communication, {{"MSG_OUT_NMEA", 3}}}, descriptorError),
                "NMEA frequency does not accept invented sequential enum");
    }
    for (const double currentProtocol : {0.0, 1.0, 2.0})
    {
        Device device;
        device.values["COMM_STREAM_TYP3"] = currentProtocol;
        EpsilonSettingsSnapshot streamSnapshot;
        std::string streamError;
        const bool applied = applyEpsilonSettings({EpsilonSettingsGroup::Communication, {{"COMM_STREAM_TYP3", 3}}},
                                                  device.channel(), streamSnapshot, streamError);
        require(applied == (currentProtocol != 1), "secondary protocol applies while Main remains protected");
        require(device.writes() == (applied ? 1 : 0), "Main protocol protection prevents writes");
    }
    const std::string roll = "BODY_TO_VEHICLE_ALGN_ROLL";
    double value = 123;
    require(parseEpsilonParameterResponse("#fparam get BODY_TO_VEHICLE_ALGN_ROLL\r\nBODY_TO_VEHICLE_ALGN_ROLL=-1.25e+1\r\n*#OK\r\n", roll, value) && value == -12.5,
            "parse named finite value despite echo and trailing ack");
    for (const auto& reply : {"*#OK\r\n", "#fparam get BODY_TO_VEHICLE_ALGN_ROLL\r\n", "OTHER=0\r\n",
                             "BODY_TO_VEHICLE_ALGN_ROLL=nan\r\n", "BODY_TO_VEHICLE_ALGN_ROLL=1junk\r\n",
                             "BODY_TO_VEHICLE_ALGN_ROLL=1\r\nBODY_TO_VEHICLE_ALGN_ROLL=2\r\n",
                             "BODY_TO_VEHICLE_ALGN_ROLL=1\r\n*#ERROR\r\n"})
    {
        value = 123;
        require(!parseEpsilonParameterResponse(reply, roll, value) && value == 123,
                "unrecognized/ambiguous/error replies do not synthesize zero");
    }
    require(!parseEpsilonParameterResponse("UNKNOWN=0\r\n", "UNKNOWN", value), "unknown parameters are not parsed");

    std::string error;
    EpsilonSettingsSnapshot snapshot;
    EpsilonSettingsOperation operation{EpsilonSettingsGroup::Installation, {{roll, 10}}};
    for (const auto invalid : {EpsilonSettingsOperation{EpsilonSettingsGroup::Installation, {{"UNKNOWN", 1}}},
                               EpsilonSettingsOperation{EpsilonSettingsGroup::Installation, {{roll, 361}}},
                               EpsilonSettingsOperation{EpsilonSettingsGroup::Installation, {{roll, std::numeric_limits<double>::infinity()}}},
                               EpsilonSettingsOperation{EpsilonSettingsGroup::Installation, {{"GNSS_L_ANTS_BASE_LINE", 1}}},
                               EpsilonSettingsOperation{EpsilonSettingsGroup::Fusion, {{"DYNAMICS_MODEL", 2}}},
                               EpsilonSettingsOperation{EpsilonSettingsGroup::Fusion, {{"AID_MAG_3D_MAGNETIC", .5}}},
                               EpsilonSettingsOperation{EpsilonSettingsGroup::Fusion, {{roll, 0}}}})
    {
        Device device;
        require(!applyEpsilonSettings(invalid, device.channel(), snapshot, error) && !error.empty(), "reject unsafe settings");
        require(device.commands.empty(), "invalid values never enter configuration or write");
    }
    {
        Device device;
        device.missing = "GNSS_L_ANTS_BASE_LINE";
        require(readEpsilonSettings(EpsilonSettingsGroup::Installation, device.channel(), snapshot, error), "partial capabilities can be read");
        require(snapshot.unsupported.size() == 1 && !snapshot.values.count(device.missing), "unsupported value is absent, not zero");
        require(snapshot.values.size() == epsilonParameterDescriptors(EpsilonSettingsGroup::Installation).size() - 1, "only selected group read");
        device.requireExited();
    }
    {
        Device device;
        require(applyEpsilonSettings(operation, device.channel(), snapshot, error), "apply changed setting");
        require(device.writes() == 1 && device.saves == 1, "only changed item written and saved once");
        require(snapshot.saved && snapshot.readback_verified && snapshot.restart_required && snapshot.values.at(roll) == 10,
                "save ack, current readback and restart are separate states");
        device.requireExited();
    }
    {
        Device device;
        device.values[roll] = 10;
        require(applyEpsilonSettings(operation, device.channel(), snapshot, error), "unchanged setting verifies successfully");
        require(device.writes() == 0 && device.saves == 0 && !snapshot.saved && snapshot.readback_verified && !snapshot.restart_required,
                "unchanged operation is not falsely marked newly saved/restart-required");
        device.requireExited();
    }
    {
        Device device;
        device.missing = roll;
        require(!applyEpsilonSettings(operation, device.channel(), snapshot, error), "unread parameter is not written");
        require(device.writes() == 0 && device.saves == 0 && snapshot.unsupported.size() == 1, "preflight capability failure prevents all writes");
        device.requireExited();
    }
    {
        Device device;
        const EpsilonSettingsOperation pair{EpsilonSettingsGroup::Installation, {{"GNSS_L_IMU_ANT1_X", 1}, {"GNSS_L_IMU_ANT1_Y", 2}}};
        device.write_failure = "GNSS_L_IMU_ANT1_Y";
        require(!applyEpsilonSettings(pair, device.channel(), snapshot, error), "partial write failure is reported");
        require(device.values.at("GNSS_L_IMU_ANT1_X") == 1 && device.saves == 0 && !snapshot.saved && !snapshot.readback_verified,
                "partial write is not falsely atomic or saved");
        require(!snapshot.values.count("GNSS_L_IMU_ANT1_X") && !snapshot.values.count("GNSS_L_IMU_ANT1_Y") && error.find("partial") != std::string::npos,
                "partial unverified values are absent and warning is explicit");
        device.requireExited();
    }
    for (int failure = 0; failure < 5; ++failure)
    {
        Device device;
        device.save_failure = failure == 0;
        device.readback_failure = failure == 1;
        device.exit_failure = failure == 2;
        device.entry_failure = failure == 3;
        device.throw_query = failure == 4;
        require(!applyEpsilonSettings(operation, device.channel(), snapshot, error) && !error.empty(), "save/readback/exit/entry/disconnect failure is not success");
        if (failure == 0)
            require(!snapshot.saved && !snapshot.readback_verified, "save failure flags");
        if (failure == 1)
            require(snapshot.saved && !snapshot.readback_verified && !snapshot.values.count(roll), "saved but unreadable state is explicit");
        if (failure >= 3)
            require(device.writes() == 0 && device.saves == 0, "entry/query exception happens before writes");
        device.requireExited();
    }
    {
        Device device;
        device.missing = "AID_GYO_TRUN_ON_TARE_ENABLED";
        const EpsilonSettingsOperation tare{EpsilonSettingsGroup::Fusion, {{device.missing, 1}}};
        require(!applyEpsilonSettings(tare, device.channel(), snapshot, error), "unsupported spelling fails");
        require(device.writes() == 0, "no blind TURN alias fallback write");
        for (const auto& command : device.commands)
            require(command.find("AID_GYO_TURN_ON_TARE_ENABLED") == std::string::npos, "does not substitute firmware alias");
    }
    std::cout << "EPSILON settings protocol tests passed\n";
    return 0;
}
