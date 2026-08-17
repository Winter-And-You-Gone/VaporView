#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace VaporView
{

void writeLifecycleBreadcrumb(std::string_view event,
                              std::optional<int> exitCode = std::nullopt,
                              std::string_view reasonCode = {}) noexcept;

#ifdef VAPORVIEW_LIFECYCLE_BREADCRUMB_TESTING
namespace LifecycleBreadcrumbTest
{
std::filesystem::path lifecycleBreadcrumbFilePath(const std::filesystem::path& directory);

bool writeLifecycleBreadcrumbToDirectory(const std::filesystem::path& directory,
                                         std::string_view event,
                                         std::optional<int> exitCode = std::nullopt,
                                         std::string_view reasonCode = {}) noexcept;
}  // namespace LifecycleBreadcrumbTest
#endif

}  // namespace VaporView
