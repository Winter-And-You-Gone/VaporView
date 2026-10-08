#include "EpsilonSettings.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iomanip>
#include <locale>
#include <regex>
#include <sstream>

namespace VaporView
{
namespace
{
constexpr int CommandIntervalMs = 1500; // FDI_config.c vendor interval.

EpsilonParameterDescriptor real(const char* name, const char* zh, const char* en,
                                const char* unit, double minimum, double maximum)
{
    return {name, EpsilonSettingsGroup::Installation, zh, en, unit,
            EpsilonParameterKind::Real, true, true, minimum, maximum, {}, true};
}

EpsilonParameterDescriptor aid(const char* name, const char* zh, const char* en)
{
    return {name, EpsilonSettingsGroup::Fusion, zh, en, "",
            EpsilonParameterKind::Boolean, true, true, 0, 1,
            {{0, "关闭", "Disabled"}, {1, "开启", "Enabled"}}, true};
}

const std::vector<EpsilonParameterDescriptor> Installation = {
    // The vendor offset.ui permits +/-360 degrees. Antenna configuration UI
    // permits +/-1000 metres for both vectors; these are editing limits, not
    // statements about supported hardware installation dimensions.
    real("BODY_TO_VEHICLE_ALGN_ROLL", "安装横滚角", "Installation roll", "deg", -360, 360),
    real("BODY_TO_VEHICLE_ALGN_PITCH", "安装俯仰角", "Installation pitch", "deg", -360, 360),
    real("BODY_TO_VEHICLE_ALGN_YAW", "安装偏航角", "Installation yaw", "deg", -360, 360),
    real("GNSS_L_IMU_ANT1_X", "主天线杆臂 X", "Main antenna lever arm X", "m", -1000, 1000),
    real("GNSS_L_IMU_ANT1_Y", "主天线杆臂 Y", "Main antenna lever arm Y", "m", -1000, 1000),
    real("GNSS_L_IMU_ANT1_Z", "主天线杆臂 Z", "Main antenna lever arm Z", "m", -1000, 1000),
    real("GNSS_L_ANT2_ANT1_X", "副天线向量 X", "Secondary antenna vector X", "m", -1000, 1000),
    real("GNSS_L_ANT2_ANT1_Y", "副天线向量 Y", "Secondary antenna vector Y", "m", -1000, 1000),
    real("GNSS_L_ANT2_ANT1_Z", "副天线向量 Z", "Secondary antenna vector Z", "m", -1000, 1000),
    real("GNSS_ANTS_HEADING_BIAS", "双天线航向偏角", "Dual antenna heading bias", "deg", 0, 360),
    // Manual p218 specifies metres, but no numeric editing range.
    {"GNSS_L_ANTS_BASE_LINE", EpsilonSettingsGroup::Installation, "双天线基线（只读）",
     "Dual antenna baseline (read only)", "m", EpsilonParameterKind::Real, false,
     false, 0, 0, {}, true}
};

const std::vector<EpsilonParameterDescriptor> Fusion = {
    aid("AID_GNSS_POS_UPDATE", "GNSS 位置融合", "GNSS position aid"),
    aid("AID_GNSS_VEL_UPDATE", "GNSS 速度融合", "GNSS velocity aid"),
    aid("AID_GNSS_DUAL_ANT_HEADING_UPDATE", "双天线航向融合", "Dual antenna heading aid"),
    aid("AID_GNSS_TRACK_HEADING_UPDATE", "GNSS 航迹角融合", "GNSS track heading aid"),
    aid("AID_MAG_2D_MAGNETIC", "磁力计 2D 融合", "2D magnetic aid"),
    aid("AID_MAG_3D_MAGNETIC", "磁力计 3D 融合", "3D magnetic aid"),
    aid("AID_INIT_YAW_USE_MAG", "磁力计初始化航向", "Initial yaw from magnetometer"),
    aid("AID_ZERO_RATE_UPDATE", "静止零角速度辅助", "Zero rate aid"),
    aid("AID_ZERO_VEL_UPDATE", "静止零速度辅助", "Zero velocity aid"),
    aid("AID_ZERO_POS_UPDATE", "静止零位置辅助", "Zero position aid"),
    // Both spellings occur in authoritative sources. Query separately; never
    // substitute one key for the other when writing a device.
    aid("AID_GYO_TRUN_ON_TARE_ENABLED", "开机陀螺零偏估计（TRUN）", "Startup gyro tare (TRUN)"),
    aid("AID_GYO_TURN_ON_TARE_ENABLED", "开机陀螺零偏估计（TURN）", "Startup gyro tare (TURN)"),
    // Info.ini gives the enum numbers; manual p55 says other models remain in
    // development. Only general and automotive are exposed for editing.
    {"DYNAMICS_MODEL", EpsilonSettingsGroup::Fusion, "载体动力学模型", "Dynamics model", "",
     EpsilonParameterKind::Enumeration, true, true, 0, 1,
     {{0, "通用模型", "General"}, {1, "汽车模型（无侧滑）", "Automotive (no sideslip)"}}, true}
};

bool validGroup(EpsilonSettingsGroup group)
{
    return group == EpsilonSettingsGroup::Installation || group == EpsilonSettingsGroup::Fusion;
}

bool accepted(const std::string& response)
{
    static const std::regex ok(R"((^|[\r\n])\s*\*#OK\s*([\r\n]|$))");
    static const std::regex failure("(error|failed|unsupported)", std::regex::icase);
    return std::regex_search(response, ok) && !std::regex_search(response, failure);
}

std::string formatValue(double value)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(12) << value;
    return stream.str();
}

// Every attempted entry is paired with an exit, including an ambiguous entry
// response and exceptions. A failed exit is surfaced rather than hidden.
class ConfigurationScope
{
public:
    ConfigurationScope(const EpsilonSettingsExchange& exchange, std::string& error)
        : exchange_(exchange), error_(error) {}
    ~ConfigurationScope() { close(); }
    bool enter()
    {
        attempted_ = true;
        return accepted(exchange_("#fconfig\r\n", CommandIntervalMs));
    }
    bool close()
    {
        if (!attempted_)
            return true;
        attempted_ = false;
        try
        {
            if (accepted(exchange_("#fdeconfig\r\n", CommandIntervalMs)))
                return true;
        }
        catch (...) {}
        if (!error_.empty())
            error_ += "; ";
        error_ += "EPSILON configuration mode exit was not acknowledged; reconnect device";
        return false;
    }
private:
    const EpsilonSettingsExchange& exchange_;
    std::string& error_;
    bool attempted_ = false;
};

bool query(const EpsilonSettingsExchange& exchange, const std::string& name, double& value)
{
    return parseEpsilonParameterResponse(exchange("#fparam get " + name + "\r\n", CommandIntervalMs), name, value);
}
} // namespace

const std::vector<EpsilonParameterDescriptor>& epsilonParameterDescriptors(EpsilonSettingsGroup group)
{
    static const std::vector<EpsilonParameterDescriptor> empty;
    return group == EpsilonSettingsGroup::Installation ? Installation
        : group == EpsilonSettingsGroup::Fusion ? Fusion : empty;
}

const EpsilonParameterDescriptor* epsilonParameterDescriptor(const std::string& name)
{
    for (const auto* descriptors : {&Installation, &Fusion})
        for (const auto& descriptor : *descriptors)
            if (descriptor.name == name)
                return &descriptor;
    return nullptr;
}

bool validateEpsilonSettings(const EpsilonSettingsOperation& operation, std::string& error)
{
    error.clear();
    if (!validGroup(operation.group) || operation.values.empty())
    {
        error = "Invalid or empty EPSILON settings operation";
        return false;
    }
    for (const auto& entry : operation.values)
    {
        const auto* descriptor = epsilonParameterDescriptor(entry.first);
        if (!descriptor || descriptor->group != operation.group || !descriptor->writable)
        {
            error = "EPSILON parameter is not editable in this group: " + entry.first;
            return false;
        }
        const double value = entry.second;
        if (!std::isfinite(value) || (descriptor->has_range &&
            (value < descriptor->minimum || value > descriptor->maximum)))
        {
            error = "EPSILON parameter value is outside the documented editing range: " + entry.first;
            return false;
        }
        if (descriptor->kind != EpsilonParameterKind::Real &&
            std::none_of(descriptor->options.begin(), descriptor->options.end(),
                         [value](const EpsilonParameterOption& option) { return option.value == value; }))
        {
            error = "EPSILON parameter enum is not supported: " + entry.first;
            return false;
        }
    }
    return true;
}

bool parseEpsilonParameterResponse(const std::string& response, const std::string& name, double& value)
{
    if (!epsilonParameterDescriptor(name))
        return false;
    static const std::regex failure("(error|failed|unsupported)", std::regex::icase);
    if (std::regex_search(response, failure))
        return false;
    const std::regex pattern("^\\s*" + name + R"(\s*=\s*([+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?)\s*$)");
    std::istringstream lines(response);
    std::string line;
    bool found = false;
    double parsed = 0;
    while (std::getline(lines, line))
    {
        std::smatch match;
        if (!std::regex_match(line, match, pattern))
            continue;
        std::istringstream number(match[1].str());
        number.imbue(std::locale::classic());
        double candidate = 0;
        if (!(number >> candidate) || !std::isfinite(candidate))
            return false;
        if (found && candidate != parsed)
            return false; // Ambiguous values are not a current snapshot.
        found = true;
        parsed = candidate;
    }
    if (found)
        value = parsed;
    return found;
}

bool epsilonSettingsValuesEqual(double left, double right)
{
    // Parameters are stored as floats in the vendor tool. Allow float rounding,
    // while preserving meaningful angular/metre changes near zero.
    return std::isfinite(left) && std::isfinite(right) &&
        std::abs(left - right) <= 1e-6 * std::max({1.0, std::abs(left), std::abs(right)});
}

bool readEpsilonSettings(EpsilonSettingsGroup group, const EpsilonSettingsExchange& exchange,
                         EpsilonSettingsSnapshot& snapshot, std::string& error)
{
    snapshot = {};
    snapshot.group = group;
    error.clear();
    if (!validGroup(group) || !exchange)
    {
        error = "Invalid EPSILON settings group or exchange";
        return false;
    }
    ConfigurationScope scope(exchange, error);
    try
    {
        if (!scope.enter())
            error = "EPSILON configuration mode entry was not acknowledged";
        else
        {
            for (const auto& descriptor : epsilonParameterDescriptors(group))
            {
                double value = 0;
                if (query(exchange, descriptor.name, value))
                    snapshot.values.emplace(descriptor.name, value);
                else
                    snapshot.unsupported.push_back(descriptor.name);
            }
            if (snapshot.values.empty())
                error = "No EPSILON settings could be read; unsupported keys are not zero values";
        }
    }
    catch (const std::exception& exception) { error = std::string("EPSILON settings read failed: ") + exception.what(); }
    catch (...) { error = "EPSILON settings read failed"; }
    const bool exited = scope.close();
    return exited && error.empty();
}

bool applyEpsilonSettings(const EpsilonSettingsOperation& operation,
                          const EpsilonSettingsExchange& exchange,
                          EpsilonSettingsSnapshot& snapshot, std::string& error)
{
    snapshot = {};
    snapshot.group = operation.group;
    if (!validateEpsilonSettings(operation, error))
        return false;
    if (!exchange)
    {
        error = "Invalid EPSILON settings exchange";
        return false;
    }
    ConfigurationScope scope(exchange, error);
    try
    {
        if (!scope.enter())
            error = "EPSILON configuration mode entry was not acknowledged";
        else
        {
            // Read every requested key before issuing any write. This rejects
            // firmware aliases, unreadable parameters and unsupported devices.
            for (const auto& entry : operation.values)
            {
                double current = 0;
                if (query(exchange, entry.first, current))
                    snapshot.values.emplace(entry.first, current);
                else
                    snapshot.unsupported.push_back(entry.first);
            }
            if (!snapshot.unsupported.empty())
                error = "EPSILON parameter could not be read; no settings were written: " + snapshot.unsupported.front();
            bool changed = false;
            if (error.empty())
            {
                for (const auto& entry : operation.values)
                {
                    if (epsilonSettingsValuesEqual(snapshot.values.at(entry.first), entry.second))
                        continue;
                    snapshot.values.erase(entry.first); // Acknowledgement is not a readback.
                    snapshot.restart_required = snapshot.restart_required ||
                        epsilonParameterDescriptor(entry.first)->restart_required;
                    if (!accepted(exchange("#fparam set " + entry.first + " " + formatValue(entry.second) + "\r\n", CommandIntervalMs)))
                    {
                        // The failed key may also have changed despite a lost
                        // response; exclude it rather than report an old value.
                        error = "EPSILON write was not acknowledged: " + entry.first +
                            "; partial changes may be active and have not been saved";
                        break;
                    }
                    changed = true;
                }
            }
            if (error.empty() && changed)
            {
                snapshot.saved = accepted(exchange("#fsave\r\n", CommandIntervalMs));
                if (!snapshot.saved)
                    error = "EPSILON save was not acknowledged; changes may be active but persistence is unverified";
            }
            if (error.empty())
            {
                bool verified = true;
                for (const auto& entry : operation.values)
                {
                    double actual = 0;
                    if (!query(exchange, entry.first, actual))
                    {
                        snapshot.values.erase(entry.first);
                        snapshot.unsupported.push_back(entry.first);
                        verified = false;
                    }
                    else
                    {
                        snapshot.values[entry.first] = actual;
                        verified = verified && epsilonSettingsValuesEqual(actual, entry.second);
                    }
                }
                snapshot.readback_verified = verified;
                if (!verified)
                    error = "EPSILON settings readback did not match every requested value";
            }
        }
    }
    catch (const std::exception& exception) { error = std::string("EPSILON settings apply failed; partial changes may be active: ") + exception.what(); }
    catch (...) { error = "EPSILON settings apply failed; partial changes may be active"; }
    const bool exited = scope.close();
    return exited && error.empty();
}
} // namespace VaporView
