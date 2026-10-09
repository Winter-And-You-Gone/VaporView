#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace VaporView
{
enum class EpsilonSettingsGroup : uint8_t
{
    Installation = 0, Fusion = 1, Communication = 2, Filters = 3,
    Sensors = 4, InitialState = 5, ReferencePoint = 6, ExternalAids = 7
};
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

struct EpsilonDgnssOperation { std::map<std::string, std::string> values; };
struct EpsilonDgnssSnapshot
{
    std::map<std::string, std::string> values;
    std::vector<std::string> unsupported;
    bool saved = false;
    bool readback_verified = false;
    bool restart_required = false;
};
struct EpsilonDgnssDescriptor
{
    std::string name, label_zh, label_en;
    bool writable = false;
    bool secret = false;
};
const std::vector<EpsilonDgnssDescriptor>& epsilonDgnssDescriptors();
const EpsilonDgnssDescriptor* epsilonDgnssDescriptor(const std::string& name);
bool validateEpsilonDgnss(const EpsilonDgnssOperation& operation, std::string& error);
bool parseEpsilonDgnssResponse(const std::string& response, const std::string& name, std::string& value);

const std::vector<EpsilonParameterDescriptor>& epsilonParameterDescriptors(EpsilonSettingsGroup group);
const std::vector<EpsilonSettingsGroup>& epsilonSettingsGroups();
bool isValidEpsilonSettingsGroup(EpsilonSettingsGroup group);
const EpsilonParameterDescriptor* epsilonParameterDescriptor(const std::string& name);
bool validateEpsilonSettings(const EpsilonSettingsOperation& operation, std::string& error);
bool parseEpsilonParameterResponse(const std::string& response, const std::string& name, double& value);
bool epsilonSettingsValuesEqual(double left, double right);

// Exchange receives a complete CRLF command and the vendor command interval.
// Keeping the protocol independent of serial I/O permits failure-path tests.
using EpsilonSettingsExchange = std::function<std::string(const std::string&, int)>;
bool readEpsilonDgnss(const EpsilonSettingsExchange& exchange, EpsilonDgnssSnapshot& snapshot, std::string& error);
bool applyEpsilonDgnss(const EpsilonDgnssOperation& operation, const EpsilonSettingsExchange& exchange,
                      EpsilonDgnssSnapshot& snapshot, std::string& error);
bool readEpsilonSettings(EpsilonSettingsGroup group, const EpsilonSettingsExchange& exchange,
                         EpsilonSettingsSnapshot& snapshot, std::string& error);
bool applyEpsilonSettings(const EpsilonSettingsOperation& operation,
                          const EpsilonSettingsExchange& exchange,
                          EpsilonSettingsSnapshot& snapshot, std::string& error);
} // namespace VaporView
