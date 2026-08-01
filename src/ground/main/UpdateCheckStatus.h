#pragma once

namespace VaporView
{

// Qt Installer Framework returns this status after an essential component,
// such as the maintenance tool itself, was updated successfully.
inline constexpr int kIfwEssentialComponentsUpdatedExitCode = 6;

constexpr bool ifwUpdateCommandSucceeded(int exitCode) noexcept
{
    return exitCode == 0 || exitCode == kIfwEssentialComponentsUpdatedExitCode;
}

} // namespace VaporView
