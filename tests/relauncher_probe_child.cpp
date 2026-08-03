#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{

bool tokenElevation(bool& elevated, TOKEN_ELEVATION_TYPE& type)
{
    HANDLE rawToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken))
    {
        return false;
    }

    TOKEN_ELEVATION elevation{};
    DWORD required = 0;
    const BOOL elevationOk = GetTokenInformation(rawToken,
                                                 TokenElevation,
                                                 &elevation,
                                                 sizeof(elevation),
                                                 &required);
    TOKEN_ELEVATION_TYPE elevationType = TokenElevationTypeDefault;
    const BOOL typeOk = GetTokenInformation(rawToken,
                                            TokenElevationType,
                                            &elevationType,
                                            sizeof(elevationType),
                                            &required);
    CloseHandle(rawToken);
    if (!elevationOk || !typeOk)
    {
        return false;
    }
    elevated = elevation.TokenIsElevated != 0;
    type = elevationType;
    return true;
}

std::string wideToUtf8(const wchar_t* text)
{
    const int required = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string result(required > 0 ? required - 1 : 0, '\0');
    if (required > 1)
    {
        WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), required, nullptr, nullptr);
    }
    return result;
}

std::wstring valueAfter(int argc, wchar_t** argv, const wchar_t* option)
{
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (wcscmp(argv[i], option) == 0)
        {
            return argv[i + 1];
        }
    }
    return {};
}

bool inheritedHandleAccessible(const std::wstring& value)
{
    if (value.empty())
    {
        return false;
    }
    wchar_t* end = nullptr;
    const unsigned long long raw = wcstoull(value.c_str(), &end, 10);
    if (end == value.c_str() || raw == 0)
    {
        return false;
    }
    HANDLE handle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(raw));
    const DWORD waitResult = WaitForSingleObject(handle, 0);
    return waitResult != WAIT_FAILED;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    wchar_t cwd[MAX_PATH] = {};
    GetCurrentDirectoryW(MAX_PATH, cwd);

    bool elevated = true;
    TOKEN_ELEVATION_TYPE elevationType = TokenElevationTypeFull;
    const bool tokenOk = tokenElevation(elevated, elevationType);

    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);

    std::wstring resultPath = valueAfter(argc, argv, L"--result");
    if (resultPath.empty())
    {
        resultPath = L"relaunch_probe_result.txt";
    }

    std::ofstream out(std::filesystem::path(resultPath), std::ios::binary);
    out << "token_ok=" << (tokenOk ? 1 : 0) << "\n";
    out << "elevated=" << (elevated ? 1 : 0) << "\n";
    out << "type=" << static_cast<int>(elevationType) << "\n";
    out << "session=" << sessionId << "\n";
    out << "cwd=" << wideToUtf8(cwd) << "\n";
    out << "parent_elevated=" << wideToUtf8(valueAfter(argc, argv, L"--parent-elevated").c_str()) << "\n";
    out << "relauncher_elevated=" << wideToUtf8(valueAfter(argc, argv, L"--relauncher-elevated").c_str()) << "\n";
    out << "relauncher_session=" << wideToUtf8(valueAfter(argc, argv, L"--relauncher-session").c_str()) << "\n";
    out << "inherited_handle_accessible="
        << (inheritedHandleAccessible(valueAfter(argc, argv, L"--inherited-handle")) ? 1 : 0)
        << "\n";
    return out ? 0 : 1;
}
