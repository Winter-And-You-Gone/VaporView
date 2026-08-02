#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace
{

struct Handle
{
    HANDLE value = nullptr;

    Handle() = default;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle()
    {
        if (value && value != INVALID_HANDLE_VALUE)
        {
            CloseHandle(value);
        }
    }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    Handle(Handle&& other) noexcept : value(other.value)
    {
        other.value = nullptr;
    }

    Handle& operator=(Handle&& other) noexcept
    {
        if (this != &other)
        {
            if (value && value != INVALID_HANDLE_VALUE)
            {
                CloseHandle(value);
            }
            value = other.value;
            other.value = nullptr;
        }
        return *this;
    }
};

std::wstring errorText(const wchar_t* context, DWORD error = GetLastError())
{
    return std::wstring(context) + L" failed with Win32 error " + std::to_wstring(error);
}

void debugLog(const std::wstring& message)
{
    OutputDebugStringW((L"VaporViewUpdateRelauncher: " + message + L"\n").c_str());
}

void showManualStartMessage()
{
    const wchar_t* message =
        L"VaporView \u5df2\u66f4\u65b0\u5b8c\u6210\uff0c"
        L"\u4f46\u65e0\u6cd5\u786e\u8ba4\u53ef\u7528\u7684"
        L"\u975e\u7ba1\u7406\u5458\u4ee4\u724c\u6765\u5b89\u5168"
        L"\u542f\u52a8\u4e3b\u7a0b\u5e8f\u3002"
        L"\n\n\u8bf7\u7a0d\u540e\u4ece\u684c\u9762\u6216"
        L"\u5f00\u59cb\u83dc\u5355\u5feb\u6377\u65b9\u5f0f"
        L"\u624b\u52a8\u542f\u52a8 VaporView\u3002";
    MessageBoxW(nullptr,
                message,
                L"VaporView",
                MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
}

bool iequals(const std::wstring& left, const std::wstring& right)
{
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}

std::wstring normalizePath(const wchar_t* path)
{
    if (!path || path[0] == L'\0')
    {
        return {};
    }

    const DWORD required = GetFullPathNameW(path, 0, nullptr, nullptr);
    if (required == 0)
    {
        return {};
    }

    std::wstring result(required, L'\0');
    const DWORD written = GetFullPathNameW(path, required, result.data(), nullptr);
    if (written == 0 || written >= required)
    {
        return {};
    }
    result.resize(written);
    std::replace(result.begin(), result.end(), L'/', L'\\');
    while (result.size() > 3 && (result.back() == L'\\' || result.back() == L'/'))
    {
        result.pop_back();
    }
    return result;
}

std::wstring fileNameOf(const std::wstring& path)
{
    const std::wstring::size_type slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring parentOf(const std::wstring& path)
{
    const std::wstring::size_type slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
    {
        return {};
    }
    if (slash == 2 && path.size() >= 3 && path[1] == L':')
    {
        return path.substr(0, 3);
    }
    return path.substr(0, slash);
}

bool regularFileExistsWithoutReparsePoint(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

std::wstring processPath(DWORD processId)
{
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId));
    if (!process.value)
    {
        return {};
    }

    std::wstring path(32768, L'\0');
    DWORD size = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process.value, 0, path.data(), &size))
    {
        return {};
    }
    path.resize(size);
    return path;
}

std::vector<DWORD> matchingProcesses(const std::wstring& executablePath)
{
    std::vector<DWORD> matches;
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.value == INVALID_HANDLE_VALUE)
    {
        return matches;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot.value, &entry))
    {
        do
        {
            const std::wstring candidate = normalizePath(processPath(entry.th32ProcessID).c_str());
            if (!candidate.empty() && iequals(candidate, executablePath))
            {
                matches.push_back(entry.th32ProcessID);
            }
        } while (Process32NextW(snapshot.value, &entry));
    }
    return matches;
}

BOOL CALLBACK closeProcessWindow(HWND window, LPARAM parameter)
{
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == static_cast<DWORD>(parameter) && IsWindow(window))
    {
        PostMessageW(window, WM_CLOSE, 0, 0);
    }
    return TRUE;
}

void requestMaintenanceToolExit(const std::wstring& maintenanceToolPath)
{
    Sleep(100);
    for (int attempt = 0; attempt < 120; ++attempt)
    {
        const std::vector<DWORD> processes = matchingProcesses(maintenanceToolPath);
        if (processes.empty())
        {
            return;
        }
        for (const DWORD processId : processes)
        {
            EnumWindows(closeProcessWindow, static_cast<LPARAM>(processId));
        }
        Sleep(500);
    }
}

bool sameSession(DWORD processId, DWORD sessionId)
{
    DWORD candidateSession = 0;
    return ProcessIdToSessionId(processId, &candidateSession) &&
           candidateSession == sessionId;
}

Handle duplicatePrimaryTokenFromProcess(DWORD processId)
{
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId));
    if (!process.value)
    {
        return {};
    }

    HANDLE rawToken = nullptr;
    if (!OpenProcessToken(process.value,
                          TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY |
                              TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
                          &rawToken))
    {
        return {};
    }
    Handle token(rawToken);

    HANDLE rawPrimaryToken = nullptr;
    if (!DuplicateTokenEx(token.value,
                          TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY |
                              TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
                          nullptr,
                          SecurityImpersonation,
                          TokenPrimary,
                          &rawPrimaryToken))
    {
        return {};
    }
    return Handle(rawPrimaryToken);
}

Handle shellPrimaryTokenForCurrentSession()
{
    DWORD currentSession = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &currentSession))
    {
        debugLog(errorText(L"ProcessIdToSessionId"));
        return {};
    }

    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.value == INVALID_HANDLE_VALUE)
    {
        debugLog(errorText(L"CreateToolhelp32Snapshot"));
        return {};
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.value, &entry))
    {
        debugLog(errorText(L"Process32FirstW"));
        return {};
    }

    do
    {
        if (!iequals(entry.szExeFile, L"explorer.exe") ||
            !sameSession(entry.th32ProcessID, currentSession))
        {
            continue;
        }

        Handle token = duplicatePrimaryTokenFromProcess(entry.th32ProcessID);
        if (token.value)
        {
            return token;
        }
    } while (Process32NextW(snapshot.value, &entry));

    debugLog(L"could not find an Explorer token in the current interactive session");
    return {};
}

bool validateRelaunchTarget(const std::wstring& maintenanceToolPath,
                            const std::wstring& applicationPath,
                            std::wstring& workingDirectory)
{
    if (!iequals(fileNameOf(maintenanceToolPath), L"VaporViewMaintenanceTool.exe"))
    {
        debugLog(L"maintenance tool path is not VaporViewMaintenanceTool.exe");
        return false;
    }
    if (!regularFileExistsWithoutReparsePoint(maintenanceToolPath))
    {
        debugLog(L"maintenance tool path does not exist or is a reparse point");
        return false;
    }
    if (!iequals(fileNameOf(applicationPath), L"VaporView.exe"))
    {
        debugLog(L"application path is not VaporView.exe");
        return false;
    }
    if (!regularFileExistsWithoutReparsePoint(applicationPath))
    {
        debugLog(L"application path does not exist or is a reparse point");
        return false;
    }

    const std::wstring maintenanceDirectory = parentOf(maintenanceToolPath);
    const std::wstring applicationDirectory = parentOf(applicationPath);
    if (maintenanceDirectory.empty() || applicationDirectory.empty() ||
        !iequals(maintenanceDirectory, applicationDirectory))
    {
        debugLog(L"maintenance tool and application are not in the same install root");
        return false;
    }

    workingDirectory = applicationDirectory;
    return true;
}

bool currentProcessIsElevated()
{
    HANDLE rawToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken))
    {
        debugLog(errorText(L"OpenProcessToken"));
        return true;
    }
    Handle token(rawToken);

    TOKEN_ELEVATION elevation{};
    DWORD required = 0;
    if (!GetTokenInformation(token.value, TokenElevation, &elevation, sizeof(elevation), &required))
    {
        debugLog(errorText(L"GetTokenInformation(TokenElevation)"));
        return true;
    }
    return elevation.TokenIsElevated != 0;
}

bool launchWithCurrentToken(const std::wstring& applicationPath,
                            const std::wstring& workingDirectory)
{
    std::wstring commandLine = L"\"" + applicationPath + L"\"";
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    const BOOL started = CreateProcessW(applicationPath.c_str(),
                                        commandLine.data(),
                                        nullptr,
                                        nullptr,
                                        FALSE,
                                        0,
                                        nullptr,
                                        workingDirectory.c_str(),
                                        &startupInfo,
                                        &processInfo);
    if (!started)
    {
        debugLog(errorText(L"CreateProcessW(non-elevated current token)"));
        return false;
    }
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
}

bool launchApplicationUnelevated(const std::wstring& applicationPath,
                                 const std::wstring& workingDirectory)
{
    if (!currentProcessIsElevated())
    {
        return launchWithCurrentToken(applicationPath, workingDirectory);
    }

    Handle shellToken = shellPrimaryTokenForCurrentSession();
    if (!shellToken.value)
    {
        return false;
    }

    std::wstring commandLine = L"\"" + applicationPath + L"\"";
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    const BOOL started = CreateProcessWithTokenW(shellToken.value,
                                                 LOGON_WITH_PROFILE,
                                                 applicationPath.c_str(),
                                                 commandLine.data(),
                                                 0,
                                                 nullptr,
                                                 workingDirectory.c_str(),
                                                 &startupInfo,
                                                 &processInfo);
    if (!started)
    {
        debugLog(errorText(L"CreateProcessWithTokenW"));
        return false;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (!arguments || argumentCount != 3)
    {
        if (arguments)
        {
            LocalFree(arguments);
        }
        debugLog(L"usage: VaporViewUpdateRelauncher.exe <VaporViewMaintenanceTool.exe> <VaporView.exe>");
        return 2;
    }

    const std::wstring maintenanceToolPath = normalizePath(arguments[1]);
    const std::wstring applicationPath = normalizePath(arguments[2]);
    LocalFree(arguments);

    std::wstring workingDirectory;
    if (maintenanceToolPath.empty() || applicationPath.empty() ||
        !validateRelaunchTarget(maintenanceToolPath, applicationPath, workingDirectory))
    {
        return 4;
    }

    requestMaintenanceToolExit(maintenanceToolPath);
    if (launchApplicationUnelevated(applicationPath, workingDirectory))
    {
        return 0;
    }
    showManualStartMessage();
    return 3;
}
