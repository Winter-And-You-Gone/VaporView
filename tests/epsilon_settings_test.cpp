#include "EpsilonSettings.h"

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
        for (const auto group : {EpsilonSettingsGroup::Installation, EpsilonSettingsGroup::Fusion})
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
