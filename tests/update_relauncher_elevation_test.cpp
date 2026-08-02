#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{

struct Handle
{
    HANDLE value = nullptr;
    explicit Handle(HANDLE handle = nullptr) : value(handle) {}
    ~Handle()
    {
        if (value && value != INVALID_HANDLE_VALUE)
        {
            CloseHandle(value);
        }
    }
};

std::wstring utf8ToWide(const char* text)
{
    const int required = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    std::wstring result(required > 0 ? required - 1 : 0, L'\0');
    if (required > 1)
    {
        MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), required);
    }
    return result;
}

std::wstring joinPath(const std::wstring& directory, const std::wstring& fileName)
{
    if (directory.empty() || directory.back() == L'\\')
    {
        return directory + fileName;
    }
    return directory + L"\\" + fileName;
}

std::wstring quote(const std::wstring& value)
{
    std::wstring quoted = L"\"";
    for (wchar_t ch : value)
    {
        if (ch == L'"')
        {
            quoted += L"\\\"";
        }
        else
        {
            quoted += ch;
        }
    }
    quoted += L"\"";
    return quoted;
}

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

bool explorerExistsInCurrentSession()
{
    DWORD currentSession = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &currentSession))
    {
        return false;
    }

    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.value == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.value, &entry))
    {
        return false;
    }
    do
    {
        DWORD session = 0;
        if (_wcsicmp(entry.szExeFile, L"explorer.exe") == 0 &&
            ProcessIdToSessionId(entry.th32ProcessID, &session) &&
            session == currentSession)
        {
            return true;
        }
    } while (Process32NextW(snapshot.value, &entry));
    return false;
}

int runRelauncher(const std::wstring& relauncher,
                  const std::wstring& maintenanceTool,
                  const std::wstring& application)
{
    std::wstring commandLine =
        quote(relauncher) + L" " + quote(maintenanceTool) + L" " + quote(application);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(relauncher.c_str(),
                        commandLine.data(),
                        nullptr,
                        nullptr,
                        FALSE,
                        0,
                        nullptr,
                        nullptr,
                        &startup,
                        &process))
    {
        std::wcerr << L"CreateProcessW failed for relauncher: " << GetLastError() << L"\n";
        std::exit(1);
    }
    CloseHandle(process.hThread);
    Handle processHandle(process.hProcess);
    require(WaitForSingleObject(processHandle.value, 60000) == WAIT_OBJECT_0,
            "relauncher timed out");
    DWORD exitCode = 1;
    GetExitCodeProcess(processHandle.value, &exitCode);
    return static_cast<int>(exitCode);
}

std::string readFile(const std::wstring& path)
{
    std::ifstream input(std::filesystem::path(path), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool waitForFile(const std::wstring& path)
{
    for (int i = 0; i < 100; ++i)
    {
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            return true;
        }
        Sleep(100);
    }
    return false;
}

} // namespace

int main()
{
    if (!explorerExistsInCurrentSession())
    {
        std::cout << "SKIP: no explorer.exe token in current session\n";
        return 77;
    }

    const std::wstring binaryDir = utf8ToWide(VAPORVIEW_BINARY_DIR);
    const std::wstring relauncher = joinPath(binaryDir, L"VaporViewUpdateRelauncher.exe");
    const std::wstring probeSource = joinPath(binaryDir, L"relauncher_probe_child.exe");
    require(GetFileAttributesW(relauncher.c_str()) != INVALID_FILE_ATTRIBUTES,
            "VaporViewUpdateRelauncher.exe not found");
    require(GetFileAttributesW(probeSource.c_str()) != INVALID_FILE_ATTRIBUTES,
            "relauncher_probe_child.exe not found");

    wchar_t tempPathBuffer[MAX_PATH] = {};
    require(GetTempPathW(MAX_PATH, tempPathBuffer) > 0, "GetTempPathW failed");
    const std::wstring targetDir =
        joinPath(tempPathBuffer, L"VaporView Relaunch Test " + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(targetDir);
    std::filesystem::create_directories(targetDir);

    const std::wstring fakeMaintenanceTool = joinPath(targetDir, L"VaporViewMaintenanceTool.exe");
    const std::wstring fakeVaporView = joinPath(targetDir, L"VaporView.exe");
    const std::wstring probeResult = joinPath(targetDir, L"relaunch_probe_result.txt");
    const std::wstring badApplication = joinPath(targetDir, L"BadName.exe");

    std::ofstream(fakeMaintenanceTool, std::ios::binary).put('\0');
    std::filesystem::copy_file(probeSource, fakeVaporView, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(probeSource, badApplication, std::filesystem::copy_options::overwrite_existing);

    require(runRelauncher(relauncher, fakeMaintenanceTool, badApplication) != 0,
            "relauncher accepted an unexpected application file name");
    require(!waitForFile(probeResult),
            "invalid relaunch target unexpectedly started a process");

    require(runRelauncher(relauncher, fakeMaintenanceTool, fakeVaporView) == 0,
            "relauncher failed to start probe child");
    require(waitForFile(probeResult), "probe child result was not written");
    const std::string result = readFile(probeResult);
    require(result.find("token_ok=1") != std::string::npos, "probe could not read token elevation");
    require(result.find("elevated=0") != std::string::npos,
            "relauncher started an elevated child process");

    DWORD currentSession = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &currentSession);
    require(result.find("session=" + std::to_string(currentSession)) != std::string::npos,
            "relauncher started child in the wrong session");

    std::filesystem::remove_all(targetDir);
    std::cout << "update_relauncher_elevation_test passed\n";
    return 0;
}
