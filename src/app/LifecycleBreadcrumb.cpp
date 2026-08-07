#include "app/LifecycleBreadcrumb.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#endif

namespace VaporView
{
namespace
{

constexpr char kBreadcrumbFileName[] = "lifecycle-breadcrumb.jsonl";
std::atomic<unsigned long long> gSequence{0};

std::string jsonEscape(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const unsigned char ch : value)
    {
        switch (ch)
        {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (ch < 0x20)
            {
                constexpr char kHex[] = "0123456789abcdef";
                escaped += "\\u00";
                escaped += kHex[(ch >> 4) & 0x0F];
                escaped += kHex[ch & 0x0F];
            }
            else
            {
                escaped += static_cast<char>(ch);
            }
            break;
        }
    }
    return escaped;
}

std::string timestampUtc()
{
    const auto now = std::chrono::system_clock::now();
    const auto sinceEpoch = now.time_since_epoch();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        sinceEpoch) % 1000;
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);

    std::tm utc = {};
#ifdef _WIN32
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif

    std::ostringstream stream;
    stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S")
           << '.' << std::setw(3) << std::setfill('0') << milliseconds.count()
           << 'Z';
    return stream.str();
}

unsigned long long currentProcessId() noexcept
{
#ifdef _WIN32
    return static_cast<unsigned long long>(GetCurrentProcessId());
#else
    return static_cast<unsigned long long>(getpid());
#endif
}

unsigned long long currentThreadId() noexcept
{
#ifdef _WIN32
    return static_cast<unsigned long long>(GetCurrentThreadId());
#else
    return static_cast<unsigned long long>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

std::string makeBreadcrumbLine(std::string_view event,
                               std::optional<int> exitCode,
                               std::string_view reasonCode)
{
    const unsigned long long sequence =
        gSequence.fetch_add(1, std::memory_order_relaxed) + 1ULL;

    std::ostringstream stream;
    stream << "{\"timestamp_utc\":\"" << jsonEscape(timestampUtc()) << '"'
           << ",\"process_id\":" << currentProcessId()
           << ",\"thread_id\":" << currentThreadId()
           << ",\"sequence\":" << sequence
           << ",\"event\":\"" << jsonEscape(event) << '"';
    if (exitCode)
    {
        stream << ",\"exit_code\":" << *exitCode;
    }
    if (!reasonCode.empty())
    {
        stream << ",\"reason_code\":\"" << jsonEscape(reasonCode) << '"';
    }
    stream << '}';
    return stream.str();
}

std::filesystem::path breadcrumbFilePath(const std::filesystem::path& directory)
{
    return directory / kBreadcrumbFileName;
}

bool writeBreadcrumbLineToDirectory(const std::filesystem::path& directory,
                                    const std::string& line) noexcept
{
    try
    {
        std::error_code error;
        if (!std::filesystem::exists(directory, error))
        {
            std::filesystem::create_directories(directory, error);
        }
        if (error || !std::filesystem::is_directory(directory, error))
        {
            return false;
        }

        std::ofstream file(breadcrumbFilePath(directory),
                           std::ios::out | std::ios::app | std::ios::binary);
        if (!file.is_open())
        {
            return false;
        }
        file << line << '\n';
        file.flush();
        const bool ok = file.good();
        file.close();
        return ok;
    }
    catch (...)
    {
        return false;
    }
}

std::filesystem::path executableDirectory() noexcept
{
    try
    {
#ifdef _WIN32
        std::wstring buffer(260, L'\0');
        while (true)
        {
            const DWORD length = GetModuleFileNameW(
                nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0)
            {
                break;
            }
            if (length < buffer.size() - 1)
            {
                buffer.resize(length);
                return std::filesystem::path(buffer).parent_path();
            }
            buffer.resize(buffer.size() * 2);
        }
#else
        std::error_code error;
        const std::filesystem::path exePath =
            std::filesystem::read_symlink("/proc/self/exe", error);
        if (!error && !exePath.empty())
        {
            return exePath.parent_path();
        }
#endif
    }
    catch (...)
    {
    }
    return {};
}

#ifdef _WIN32
std::filesystem::path environmentVariablePath(const wchar_t *name) noexcept
{
    try
    {
        const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
        if (required <= 1)
        {
            return {};
        }
        std::wstring value(required, L'\0');
        const DWORD length = GetEnvironmentVariableW(name, value.data(), required);
        if (length == 0 || length >= required)
        {
            return {};
        }
        value.resize(length);
        return std::filesystem::path(value);
    }
    catch (...)
    {
        return {};
    }
}
#endif
std::vector<std::filesystem::path> defaultBreadcrumbDirectories()
{
    std::vector<std::filesystem::path> directories;
    const std::filesystem::path appDir = executableDirectory();
    if (!appDir.empty())
    {
        directories.push_back(appDir / "logs");
    }

#ifdef _WIN32
    const std::filesystem::path localAppData = environmentVariablePath(L"LOCALAPPDATA");
    if (!localAppData.empty())
    {
        directories.emplace_back(localAppData / "VaporView" / "logs");
    }
#else
    if (const char *home = std::getenv("HOME"))
    {
        if (*home != '\0')
        {
            directories.emplace_back(std::filesystem::path(home) / ".local" /
                                     "share" / "VaporView" / "logs");
        }
    }
#endif

    std::error_code error;
    const std::filesystem::path current = std::filesystem::current_path(error);
    if (!error && !current.empty())
    {
        directories.push_back(current / "logs");
    }
    return directories;
}

bool writeLifecycleBreadcrumbImpl(const std::filesystem::path& directory,
                                  std::string_view event,
                                  std::optional<int> exitCode,
                                  std::string_view reasonCode) noexcept
{
    try
    {
        return writeBreadcrumbLineToDirectory(
            directory, makeBreadcrumbLine(event, exitCode, reasonCode));
    }
    catch (...)
    {
        return false;
    }
}

}  // namespace

void writeLifecycleBreadcrumb(std::string_view event,
                              std::optional<int> exitCode,
                              std::string_view reasonCode) noexcept
{
    try
    {
        const std::string line = makeBreadcrumbLine(event, exitCode, reasonCode);
        for (const std::filesystem::path& directory : defaultBreadcrumbDirectories())
        {
            if (writeBreadcrumbLineToDirectory(directory, line))
            {
                return;
            }
        }
    }
    catch (...)
    {
    }
}

#ifdef VAPORVIEW_LIFECYCLE_BREADCRUMB_TESTING
namespace LifecycleBreadcrumbTest
{

std::filesystem::path lifecycleBreadcrumbFilePath(const std::filesystem::path& directory)
{
    return breadcrumbFilePath(directory);
}

bool writeLifecycleBreadcrumbToDirectory(const std::filesystem::path& directory,
                                         std::string_view event,
                                         std::optional<int> exitCode,
                                         std::string_view reasonCode) noexcept
{
    return writeLifecycleBreadcrumbImpl(directory, event, exitCode, reasonCode);
}

}  // namespace LifecycleBreadcrumbTest
#endif

}  // namespace VaporView
