#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

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

} // namespace

int wmain()
{
    wchar_t cwd[MAX_PATH] = {};
    GetCurrentDirectoryW(MAX_PATH, cwd);

    bool elevated = true;
    TOKEN_ELEVATION_TYPE elevationType = TokenElevationTypeFull;
    const bool tokenOk = tokenElevation(elevated, elevationType);

    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);

    std::ofstream out("relaunch_probe_result.txt", std::ios::binary);
    out << "token_ok=" << (tokenOk ? 1 : 0) << "\n";
    out << "elevated=" << (elevated ? 1 : 0) << "\n";
    out << "type=" << static_cast<int>(elevationType) << "\n";
    out << "session=" << sessionId << "\n";
    out << "cwd=" << wideToUtf8(cwd) << "\n";
    return out ? 0 : 1;
}
