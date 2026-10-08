#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace VaporView
{
enum class EpsilonSettingsGroup : uint8_t { Installation = 0, Fusion = 1 };
enum class EpsilonParameterKind : uint8_t { Real, Boolean, Enumeration };

struct EpsilonParameterOption
{
    double value;
    std::string label_zh;
    std::string label_en;
};

struct EpsilonParameterDescriptor
{
    std::string name;
    EpsilonSettingsGroup group;
    std::string label_zh;
    std::string label_en;
    std::string unit;
    EpsilonParameterKind kind = EpsilonParameterKind::Real;
    bool writable = false;
    bool has_range = false;
    double minimum = 0;
    double maximum = 0;
    std::vector<EpsilonParameterOption> options;
    bool restart_required = true;
};

struct EpsilonSettingsOperation
{
    EpsilonSettingsGroup group = EpsilonSettingsGroup::Installation;
    std::map<std::string, double> values;
};

struct EpsilonSettingsSnapshot
{
    EpsilonSettingsGroup group = EpsilonSettingsGroup::Installation;
    std::map<std::string, double> values;
    std::vector<std::string> unsupported;
    bool saved = false;
    bool readback_verified = false;
    bool restart_required = false;
};

const std::vector<EpsilonParameterDescriptor>& epsilonParameterDescriptors(EpsilonSettingsGroup group);
const EpsilonParameterDescriptor* epsilonParameterDescriptor(const std::string& name);
bool validateEpsilonSettings(const EpsilonSettingsOperation& operation, std::string& error);
bool parseEpsilonParameterResponse(const std::string& response, const std::string& name, double& value);
bool epsilonSettingsValuesEqual(double left, double right);

// Exchange receives a complete CRLF command and the vendor command interval.
// Keeping the protocol independent of serial I/O permits failure-path tests.
using EpsilonSettingsExchange = std::function<std::string(const std::string&, int)>;
bool readEpsilonSettings(EpsilonSettingsGroup group, const EpsilonSettingsExchange& exchange,
                         EpsilonSettingsSnapshot& snapshot, std::string& error);
bool applyEpsilonSettings(const EpsilonSettingsOperation& operation,
                          const EpsilonSettingsExchange& exchange,
                          EpsilonSettingsSnapshot& snapshot, std::string& error);
} // namespace VaporView
