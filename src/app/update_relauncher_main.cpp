#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
std::wstring absolutePath(const wchar_t* path)
{
    const DWORD required = GetFullPathNameW(path, 0, nullptr, nullptr);
    if (required == 0)
    {
        std::wstring result(path);
        std::replace(result.begin(), result.end(), L'/', L'\\');
        return result;
    }

    std::wstring result(required, L'\0');
    const DWORD written = GetFullPathNameW(path, required, result.data(), nullptr);
    if (written == 0 || written >= required)
    {
        std::wstring fallback(path);
        std::replace(fallback.begin(), fallback.end(), L'/', L'\\');
        return fallback;
    }
    result.resize(written);
    std::replace(result.begin(), result.end(), L'/', L'\\');
    return result;
}

bool pathsEqual(const std::wstring& left, const std::wstring& right)
{
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}

std::wstring processPath(DWORD processId)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process)
    {
        return {};
    }

    std::wstring path(32768, L'\0');
    DWORD size = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &size))
    {
        CloseHandle(process);
        return {};
    }
    CloseHandle(process);
    path.resize(size);
    return path;
}

std::vector<DWORD> matchingProcesses(const std::wstring& executablePath)
{
    std::vector<DWORD> matches;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return matches;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            const std::wstring candidate = processPath(entry.th32ProcessID);
            if (!candidate.empty() && pathsEqual(candidate, executablePath))
            {
                matches.push_back(entry.th32ProcessID);
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
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
    // Give the IFW restart handler a brief chance to finish dispatching the click,
    // then close it before its wizard can visibly return to the welcome page.
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

bool launchApplication(const std::wstring& applicationPath)
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
                                        nullptr,
                                        &startupInfo,
                                        &processInfo);
    if (!started)
    {
        return false;
    }
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
}
}

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
        return 2;
    }

    const std::wstring maintenanceToolPath = absolutePath(arguments[1]);
    const std::wstring applicationPath = absolutePath(arguments[2]);
    LocalFree(arguments);

    requestMaintenanceToolExit(maintenanceToolPath);
    return launchApplication(applicationPath) ? 0 : 3;
}
