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

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

class ScopedEnvironment final
{
public:
    ScopedEnvironment(const wchar_t* name, const std::wstring& value)
        : name_(name)
    {
        const DWORD required = GetEnvironmentVariableW(name_.c_str(), nullptr, 0);
        if (required > 0)
        {
            previous_.resize(required);
            const DWORD written = GetEnvironmentVariableW(name_.c_str(),
                                                          previous_.data(),
                                                          required);
            if (written > 0 && written < required)
            {
                previous_.resize(written);
                hadPrevious_ = true;
            }
        }
        SetEnvironmentVariableW(name_.c_str(), value.c_str());
    }

    ~ScopedEnvironment()
    {
        SetEnvironmentVariableW(name_.c_str(), hadPrevious_ ? previous_.c_str() : nullptr);
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

private:
    std::wstring name_;
    std::wstring previous_;
    bool hadPrevious_ = false;
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

std::string wideToUtf8(const std::wstring& text)
{
    const int required = WideCharToMultiByte(CP_UTF8,
                                             0,
                                             text.c_str(),
                                             -1,
                                             nullptr,
                                             0,
                                             nullptr,
                                             nullptr);
    std::string result(required > 0 ? required - 1 : 0, '\0');
    if (required > 1)
    {
        WideCharToMultiByte(CP_UTF8,
                            0,
                            text.c_str(),
                            -1,
                            result.data(),
                            required,
                            nullptr,
                            nullptr);
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

bool currentProcessElevation(bool& elevated, TOKEN_ELEVATION_TYPE& type)
{
    HANDLE rawToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken))
    {
        return false;
    }
    Handle token(rawToken);

    TOKEN_ELEVATION elevation{};
    DWORD required = 0;
    if (!GetTokenInformation(token.value,
                             TokenElevation,
                             &elevation,
                             sizeof(elevation),
                             &required))
    {
        return false;
    }
    if (!GetTokenInformation(token.value,
                             TokenElevationType,
                             &type,
                             sizeof(type),
                             &required))
    {
        return false;
    }
    elevated = elevation.TokenIsElevated != 0;
    return true;
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
                        TRUE,
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

void requireContains(const std::string& result,
                     const std::string& needle,
                     const std::string& message)
{
    require(result.find(needle) != std::string::npos, message);
}

} // namespace

int main(int argc, char** argv)
{
    const std::string mode = argc >= 2 ? argv[1] : "standard";
    const bool elevatedMode = mode == "elevated";
    const bool standardMode = mode == "standard";
    require(elevatedMode || standardMode, "expected standard or elevated mode");

    if (!explorerExistsInCurrentSession())
    {
        std::cout << "SKIP: no explorer.exe token in current session\n";
        return 77;
    }

    bool parentElevated = true;
    TOKEN_ELEVATION_TYPE parentType = TokenElevationTypeFull;
    require(currentProcessElevation(parentElevated, parentType),
            "could not inspect test host token elevation");

    if (elevatedMode && (!parentElevated || parentType != TokenElevationTypeFull))
    {
        std::cout << "SKIPPED: requires an elevated test runner\n";
        return 77;
    }
    if (standardMode && parentElevated)
    {
        std::cout << "SKIP: standard parent branch requires a non-elevated test runner\n";
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
        joinPath(tempPathBuffer,
                 L"VaporView Relaunch Test " + utf8ToWide(mode.c_str()) +
                     L" " + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(targetDir);
    std::filesystem::create_directories(targetDir);

    const std::wstring fakeMaintenanceTool = joinPath(targetDir, L"VaporViewMaintenanceTool.exe");
    const std::wstring badMaintenanceTool = joinPath(targetDir, L"OtherMaintenanceTool.exe");
    const std::wstring fakeVaporView = joinPath(targetDir, L"VaporView.exe");
    const std::wstring probeResult = joinPath(targetDir, L"relaunch_probe_result.txt");
    const std::wstring badApplication = joinPath(targetDir, L"BadName.exe");

    std::ofstream(fakeMaintenanceTool, std::ios::binary).put('\0');
    std::ofstream(badMaintenanceTool, std::ios::binary).put('\0');
    std::filesystem::copy_file(probeSource, fakeVaporView, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(probeSource, badApplication, std::filesystem::copy_options::overwrite_existing);

    SECURITY_ATTRIBUTES inheritableAttributes{};
    inheritableAttributes.nLength = sizeof(inheritableAttributes);
    inheritableAttributes.bInheritHandle = TRUE;
    Handle inheritedProbe(CreateEventW(&inheritableAttributes, TRUE, FALSE, nullptr));
    require(inheritedProbe.value != nullptr, "CreateEventW for inherited-handle probe failed");

    ScopedEnvironment resultPathEnv(L"VAPORVIEW_RELAUNCHER_TEST_RESULT", probeResult);
    ScopedEnvironment parentElevatedEnv(L"VAPORVIEW_RELAUNCHER_TEST_PARENT_ELEVATED",
                                        parentElevated ? L"1" : L"0");
    ScopedEnvironment inheritedHandleEnv(
        L"VAPORVIEW_RELAUNCHER_TEST_INHERITED_HANDLE",
        std::to_wstring(reinterpret_cast<uintptr_t>(inheritedProbe.value)));

    require(runRelauncher(relauncher, badMaintenanceTool, fakeVaporView) != 0,
            "relauncher accepted an unexpected maintenance tool file name");
    require(!waitForFile(probeResult),
            "invalid maintenance tool target unexpectedly started a process");

    require(runRelauncher(relauncher, fakeMaintenanceTool, badApplication) != 0,
            "relauncher accepted an unexpected application file name");
    require(!waitForFile(probeResult),
            "invalid relaunch target unexpectedly started a process");

    require(runRelauncher(relauncher, fakeMaintenanceTool, fakeVaporView) == 0,
            "relauncher failed to start probe child");
    require(waitForFile(probeResult), "probe child result was not written");
    const std::string result = readFile(probeResult);
    requireContains(result, "token_ok=1", "probe could not read token elevation");
    requireContains(result, "elevated=0", "relauncher started an elevated child process");
    require(result.find("type=2") == std::string::npos,
            "relauncher started a child with TokenElevationTypeFull");
    requireContains(result,
                    std::string("parent_elevated=") + (parentElevated ? "1" : "0"),
                    "probe did not receive parent elevation state");
    requireContains(result,
                    std::string("relauncher_elevated=") + (parentElevated ? "1" : "0"),
                    "relauncher elevation state did not match the parent branch");
    requireContains(result,
                    "inherited_handle_accessible=0",
                    "probe child inherited an unnecessary parent handle");

    DWORD currentSession = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &currentSession);
    requireContains(result,
                    "session=" + std::to_string(currentSession),
                    "relauncher started child in the wrong session");
    requireContains(result,
                    "relauncher_session=" + std::to_string(currentSession),
                    "relauncher reported the wrong session");
    requireContains(result,
                    "cwd=" + wideToUtf8(targetDir),
                    "relauncher did not set the application working directory");

    std::filesystem::remove_all(targetDir);
    std::cout << "update_relauncher_" << mode << "_parent_test passed\n";
    return 0;
}
