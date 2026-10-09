#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace VaporView
{
enum class EpsilonMaintenanceAction : uint8_t { Level = 0, Accelerometer = 1, Gyroscope = 2, Magnetic2D = 3, Magnetic3D = 4 };
enum class EpsilonMaintenanceStatus : uint8_t { Acknowledged = 0, Completed = 1, Failed = 2, Unsupported = 3, SentUnverified = 4, Running = 5, Cancelled = 6 };

struct EpsilonMaintenanceResult
{
    EpsilonMaintenanceAction action = EpsilonMaintenanceAction::Level;
    EpsilonMaintenanceStatus status = EpsilonMaintenanceStatus::Failed;
    int progress_percent = 0;
    std::string error;
    bool saved = false;
    bool restart_required = false;
    bool progress_known = false;
    bool fit_error_known = false;
    double fit_error = 0;
    std::string algorithm;
    bool succeeded() const { return status == EpsilonMaintenanceStatus::Completed; }
};

inline bool validEpsilonMaintenanceAction(EpsilonMaintenanceAction action)
{
    return action == EpsilonMaintenanceAction::Level ||
           action == EpsilonMaintenanceAction::Accelerometer ||
           action == EpsilonMaintenanceAction::Gyroscope ||
           action == EpsilonMaintenanceAction::Magnetic2D || action == EpsilonMaintenanceAction::Magnetic3D;
}
// The exchange reports a completed write before waiting/reading, preserving
// the fact that a tare may be active even if its response read throws.
using EpsilonMaintenanceWriteTrace = std::function<void()>;
using EpsilonMaintenanceExchange = std::function<std::string(
    const std::string&, int, const EpsilonMaintenanceWriteTrace&)>;
using EpsilonMaintenanceProgress = std::function<void(const EpsilonMaintenanceResult&)>;
bool runEpsilonMaintenance(EpsilonMaintenanceAction action,
                           const EpsilonMaintenanceExchange& exchange,
                           EpsilonMaintenanceResult& result, std::string& error,
                           EpsilonMaintenanceProgress progress = {}, std::function<bool()> shouldCancel = {});
} // namespace VaporView
