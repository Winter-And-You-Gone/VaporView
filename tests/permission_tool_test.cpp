#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <aclapi.h>
#include <tlhelp32.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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
};

struct LocalMemory
{
    void* value = nullptr;
    explicit LocalMemory(void* memory = nullptr) : value(memory) {}
    ~LocalMemory()
    {
        if (value)
        {
            LocalFree(value);
        }
    }
};

struct SidBuffer
{
    std::vector<BYTE> bytes;
    PSID sid() { return bytes.empty() ? nullptr : bytes.data(); }
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

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
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

int runPermissionTool(const std::wstring& tool,
                      const std::wstring& action,
                      const std::wstring& targetDir)
{
    std::wstring commandLine =
        quote(tool) + L" " + action + L" --target-dir " + quote(targetDir);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(tool.c_str(),
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
        std::wcerr << L"CreateProcessW failed for permission tool: " << GetLastError() << L"\n";
        std::exit(1);
    }
    CloseHandle(process.hThread);
    Handle processHandle(process.hProcess);
    require(WaitForSingleObject(processHandle.value, 60000) == WAIT_OBJECT_0,
            "permission tool timed out");
    DWORD exitCode = 1;
    GetExitCodeProcess(processHandle.value, &exitCode);
    return static_cast<int>(exitCode);
}

SidBuffer copySid(PSID sid)
{
    SidBuffer result;
    const DWORD length = GetLengthSid(sid);
    result.bytes.resize(length);
    require(CopySid(length, result.sid(), sid) != FALSE, "CopySid failed");
    return result;
}

SidBuffer currentUserSid()
{
    HANDLE rawToken = nullptr;
    require(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken) != FALSE,
            "OpenProcessToken failed");
    Handle token(rawToken);
    DWORD required = 0;
    GetTokenInformation(token.value, TokenUser, nullptr, 0, &required);
    std::vector<BYTE> buffer(required);
    require(GetTokenInformation(token.value, TokenUser, buffer.data(), required, &required) != FALSE,
            "GetTokenInformation(TokenUser) failed");
    const auto* tokenUser = reinterpret_cast<const TOKEN_USER*>(buffer.data());
    return copySid(tokenUser->User.Sid);
}

bool maskGrantsFullControl(ACCESS_MASK mask)
{
    if ((mask & GENERIC_ALL) == GENERIC_ALL)
    {
        return true;
    }
    GENERIC_MAPPING mapping{};
    mapping.GenericRead = FILE_GENERIC_READ;
    mapping.GenericWrite = FILE_GENERIC_WRITE;
    mapping.GenericExecute = FILE_GENERIC_EXECUTE;
    mapping.GenericAll = FILE_ALL_ACCESS;
    MapGenericMask(&mask, &mapping);
    const ACCESS_MASK required = FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE |
                                 DELETE | READ_CONTROL | WRITE_DAC | WRITE_OWNER;
    return (mask & required) == required;
}

PACL daclForPath(const std::wstring& path, LocalMemory& descriptor)
{
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR rawDescriptor = nullptr;
    const DWORD result = GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),
                                               SE_FILE_OBJECT,
                                               DACL_SECURITY_INFORMATION,
                                               nullptr,
                                               nullptr,
                                               &dacl,
                                               nullptr,
                                               &rawDescriptor);
    descriptor.value = rawDescriptor;
    require(result == ERROR_SUCCESS, "GetNamedSecurityInfoW failed");
    require(dacl != nullptr, "unexpected null DACL");
    return dacl;
}

int countExplicitTargetFullControlAces(const std::wstring& path, PSID targetSid)
{
    LocalMemory descriptor;
    PACL dacl = daclForPath(path, descriptor);
    ACL_SIZE_INFORMATION info{};
    require(GetAclInformation(dacl, &info, sizeof(info), AclSizeInformation) != FALSE,
            "GetAclInformation failed");

    int matches = 0;
    for (DWORD i = 0; i < info.AceCount; ++i)
    {
        void* ace = nullptr;
        require(GetAce(dacl, i, &ace) != FALSE, "GetAce failed");
        const auto* header = reinterpret_cast<const ACE_HEADER*>(ace);
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE ||
            (header->AceFlags & INHERITED_ACE) != 0)
        {
            continue;
        }
        const auto* allowed = reinterpret_cast<const ACCESS_ALLOWED_ACE*>(ace);
        PSID aceSid = const_cast<SID*>(reinterpret_cast<const SID*>(&allowed->SidStart));
        if (EqualSid(aceSid, targetSid) &&
            (header->AceFlags & OBJECT_INHERIT_ACE) != 0 &&
            (header->AceFlags & CONTAINER_INHERIT_ACE) != 0 &&
            maskGrantsFullControl(allowed->Mask))
        {
            ++matches;
        }
    }
    return matches;
}

SidBuffer wellKnownSid(WELL_KNOWN_SID_TYPE type)
{
    SidBuffer sid;
    sid.bytes.resize(SECURITY_MAX_SID_SIZE);
    DWORD size = static_cast<DWORD>(sid.bytes.size());
    require(CreateWellKnownSid(type, nullptr, sid.sid(), &size) != FALSE,
            "CreateWellKnownSid failed");
    sid.bytes.resize(size);
    return sid;
}

bool hasFullControlAce(const std::wstring& path, PSID sid, bool includeInherited = true)
{
    LocalMemory descriptor;
    PACL dacl = daclForPath(path, descriptor);
    ACL_SIZE_INFORMATION info{};
    require(GetAclInformation(dacl, &info, sizeof(info), AclSizeInformation) != FALSE,
            "GetAclInformation failed");
    for (DWORD i = 0; i < info.AceCount; ++i)
    {
        void* ace = nullptr;
        require(GetAce(dacl, i, &ace) != FALSE, "GetAce failed");
        const auto* header = reinterpret_cast<const ACE_HEADER*>(ace);
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE ||
            (!includeInherited && (header->AceFlags & INHERITED_ACE) != 0))
        {
            continue;
        }
        const auto* allowed = reinterpret_cast<const ACCESS_ALLOWED_ACE*>(ace);
        PSID aceSid = const_cast<SID*>(reinterpret_cast<const SID*>(&allowed->SidStart));
        if (EqualSid(aceSid, sid) && maskGrantsFullControl(allowed->Mask))
        {
            return true;
        }
    }
    return false;
}

void writeFile(const std::wstring& path)
{
    Handle file(CreateFileW(path.c_str(),
                            GENERIC_WRITE,
                            0,
                            nullptr,
                            CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL,
                            nullptr));
    require(file.value && file.value != INVALID_HANDLE_VALUE, "CreateFileW failed");
    const char payload[] = "readonly probe\n";
    DWORD written = 0;
    require(WriteFile(file.value, payload, sizeof(payload) - 1, &written, nullptr) != FALSE,
            "WriteFile failed");
}

std::wstring windowsDirectory()
{
    std::wstring value(MAX_PATH, L'\0');
    const UINT written = GetWindowsDirectoryW(value.data(), static_cast<UINT>(value.size()));
    require(written > 0 && written < value.size(), "GetWindowsDirectoryW failed");
    value.resize(written);
    return value;
}

std::wstring environmentPath(const wchar_t* variable)
{
    const DWORD required = GetEnvironmentVariableW(variable, nullptr, 0);
    if (required == 0)
    {
        return {};
    }
    std::wstring value(required, L'\0');
    const DWORD written = GetEnvironmentVariableW(variable, value.data(), required);
    value.resize(written);
    return value;
}

void verifyDangerousPathsAreRejected(const std::wstring& tool)
{
    std::vector<std::wstring> dangerous = {
        L"",
        L"C:",
        L"C:\\",
        windowsDirectory(),
        windowsDirectory() + L"\\..\\Windows",
    };
    const std::wstring programFiles = environmentPath(L"ProgramFiles");
    const std::wstring userProfile = environmentPath(L"USERPROFILE");
    if (!programFiles.empty())
    {
        dangerous.push_back(programFiles);
    }
    if (!userProfile.empty())
    {
        dangerous.push_back(userProfile);
    }

    const DWORD drives = GetLogicalDrives();
    for (wchar_t drive = L'D'; drive <= L'Z'; ++drive)
    {
        if ((drives & (1 << (drive - L'A'))) != 0)
        {
            std::wstring root;
            root += drive;
            root += L":\\";
            dangerous.push_back(root);
            break;
        }
    }

    for (const std::wstring& path : dangerous)
    {
        require(runPermissionTool(tool, L"verify", path) != 0,
                "dangerous path was accepted");
    }
}

void verifyReparsePointIsNotFollowed(const std::wstring& tool,
                                     const std::wstring& targetDir,
                                     const std::wstring& parentDir)
{
    const std::wstring outsideDir = joinPath(parentDir, L"outside");
    const std::wstring outsideFile = joinPath(outsideDir, L"outside-readonly.txt");
    const std::wstring linkPath = joinPath(targetDir, L"external-link");
    std::filesystem::create_directories(outsideDir);
    writeFile(outsideFile);
    require(SetFileAttributesW(outsideFile.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE,
            "SetFileAttributesW readonly failed");

    if (!CreateSymbolicLinkW(linkPath.c_str(),
                             outsideDir.c_str(),
                             SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
    {
        std::cout << "SKIP: directory symlink creation failed; reparse traversal check skipped\n";
        return;
    }

    require(runPermissionTool(tool, L"apply", targetDir) == 0,
            "permission tool apply failed with reparse point");
    const DWORD outsideAttributes = GetFileAttributesW(outsideFile.c_str());
    require((outsideAttributes & FILE_ATTRIBUTE_READONLY) != 0,
            "permission tool followed reparse point and modified outside file attributes");
    RemoveDirectoryW(linkPath.c_str());
}

} // namespace

int main()
{
    const std::wstring binaryDir = utf8ToWide(VAPORVIEW_BINARY_DIR);
    const std::wstring tool = joinPath(binaryDir, L"VaporViewPermissionTool.exe");
    require(GetFileAttributesW(tool.c_str()) != INVALID_FILE_ATTRIBUTES,
            "VaporViewPermissionTool.exe not found");

    wchar_t tempPathBuffer[MAX_PATH] = {};
    require(GetTempPathW(MAX_PATH, tempPathBuffer) > 0, "GetTempPathW failed");
    const std::wstring parentDir =
        joinPath(tempPathBuffer, L"VaporView Permission Tool Test " + std::to_wstring(GetCurrentProcessId()));
    const std::wstring targetDir = joinPath(parentDir, L"VaporView 权限 测试");
    std::filesystem::remove_all(parentDir);
    std::filesystem::create_directories(joinPath(targetDir, L"child directory"));

    const std::wstring readonlyFile = joinPath(targetDir, L"child directory\\readonly file.txt");
    writeFile(readonlyFile);
    require(SetFileAttributesW(readonlyFile.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE,
            "failed to set readonly test attribute");

    verifyReparsePointIsNotFollowed(tool, targetDir, parentDir);

    require(runPermissionTool(tool, L"apply", targetDir) == 0, "permission tool apply failed");
    require(runPermissionTool(tool, L"apply", targetDir) == 0,
            "permission tool second apply failed");
    require(runPermissionTool(tool, L"verify", targetDir) == 0, "permission tool verify failed");

    SidBuffer targetSid = currentUserSid();
    require(countExplicitTargetFullControlAces(targetDir, targetSid.sid()) == 1,
            "apply is not idempotent for target user ACEs");

    const DWORD readonlyAttributes = GetFileAttributesW(readonlyFile.c_str());
    require((readonlyAttributes & FILE_ATTRIBUTE_READONLY) == 0,
            "readonly attribute was not cleared from regular file");

    const std::wstring inheritedFile = joinPath(targetDir, L"inherited.txt");
    writeFile(inheritedFile);
    require(hasFullControlAce(inheritedFile, targetSid.sid()),
            "new file did not inherit target user Full Control");

    const std::wstring inheritedDir = joinPath(targetDir, L"inherited-dir");
    require(CreateDirectoryW(inheritedDir.c_str(), nullptr) != FALSE,
            "CreateDirectoryW inherited-dir failed");
    require(hasFullControlAce(inheritedDir, targetSid.sid()),
            "new directory did not inherit target user Full Control");

    for (const WELL_KNOWN_SID_TYPE type :
         {WinWorldSid, WinBuiltinUsersSid, WinAuthenticatedUserSid})
    {
        SidBuffer broadGroup = wellKnownSid(type);
        require(!hasFullControlAce(targetDir, broadGroup.sid(), false),
                "broad user group has Full Control");
    }
    for (const WELL_KNOWN_SID_TYPE type : {WinLocalSystemSid, WinBuiltinAdministratorsSid})
    {
        SidBuffer privileged = wellKnownSid(type);
        require(hasFullControlAce(targetDir, privileged.sid()),
                "SYSTEM or Administrators Full Control was not preserved");
    }

    verifyDangerousPathsAreRejected(tool);
    std::filesystem::remove_all(parentDir);
    std::cout << "permission_tool_test passed\n";
    return 0;
}
