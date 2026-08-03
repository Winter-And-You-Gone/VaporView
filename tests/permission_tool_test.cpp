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

void require(bool condition, const std::string& message);

void writeTextFile(const std::wstring& path, const std::string& text)
{
    std::ofstream output(std::filesystem::path(path), std::ios::binary);
    output << text;
    require(output.good(), "failed to write text file");
}

void createFakeInstallRoot(const std::wstring& targetDir, const std::wstring& toolSource)
{
    std::filesystem::create_directories(targetDir);
    std::filesystem::create_directories(joinPath(targetDir, L"resources"));
    std::filesystem::create_directories(joinPath(targetDir, L"data"));
    writeTextFile(joinPath(targetDir, L".vaporview-install-root"),
                  "VAPORVIEW_INSTALL_ROOT_V1\nproduct=VaporView\n");
    writeTextFile(joinPath(targetDir, L"VaporView.exe"), "fake vaporview executable\n");
    std::filesystem::copy_file(toolSource,
                               joinPath(targetDir, L"VaporViewPermissionTool.exe"),
                               std::filesystem::copy_options::overwrite_existing);
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

bool aceMatchesSid(void* ace, PSID sid)
{
    const auto* header = reinterpret_cast<const ACE_HEADER*>(ace);
    if (header->AceType != ACCESS_ALLOWED_ACE_TYPE &&
        header->AceType != ACCESS_DENIED_ACE_TYPE)
    {
        return false;
    }
    const auto* allowed = reinterpret_cast<const ACCESS_ALLOWED_ACE*>(ace);
    PSID aceSid = const_cast<SID*>(reinterpret_cast<const SID*>(&allowed->SidStart));
    return EqualSid(aceSid, sid) != FALSE;
}

bool removeExplicitDenyAcesForSid(const std::wstring& path, PSID sid)
{
    LocalMemory descriptor;
    PACL oldDacl = daclForPath(path, descriptor);
    ACL_SIZE_INFORMATION info{};
    require(GetAclInformation(oldDacl, &info, sizeof(info), AclSizeInformation) != FALSE,
            "GetAclInformation failed while removing deny ACEs");

    DWORD aclBytes = sizeof(ACL);
    std::vector<void*> keptAces;
    for (DWORD i = 0; i < info.AceCount; ++i)
    {
        void* ace = nullptr;
        require(GetAce(oldDacl, i, &ace) != FALSE, "GetAce failed while removing deny ACEs");
        const auto* header = reinterpret_cast<const ACE_HEADER*>(ace);
        if (header->AceType == ACCESS_DENIED_ACE_TYPE &&
            (header->AceFlags & INHERITED_ACE) == 0 &&
            aceMatchesSid(ace, sid))
        {
            continue;
        }
        keptAces.push_back(ace);
        aclBytes += header->AceSize;
    }

    std::vector<BYTE> aclBuffer(aclBytes);
    PACL newDacl = reinterpret_cast<PACL>(aclBuffer.data());
    require(InitializeAcl(newDacl, aclBytes, ACL_REVISION) != FALSE,
            "InitializeAcl failed while removing deny ACEs");
    for (void* ace : keptAces)
    {
        const auto* header = reinterpret_cast<const ACE_HEADER*>(ace);
        require(AddAce(newDacl, ACL_REVISION, MAXDWORD, ace, header->AceSize) != FALSE,
                "AddAce failed while removing deny ACEs");
    }

    const DWORD setResult = SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),
                                                  SE_FILE_OBJECT,
                                                  DACL_SECURITY_INFORMATION,
                                                  nullptr,
                                                  nullptr,
                                                  newDacl,
                                                  nullptr);
    return setResult == ERROR_SUCCESS;
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

bool addAce(const std::wstring& path,
            PSID sid,
            ACCESS_MODE mode,
            ACCESS_MASK mask,
            DWORD inheritance)
{
    PACL oldDacl = nullptr;
    PSECURITY_DESCRIPTOR rawDescriptor = nullptr;
    const DWORD infoResult = GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),
                                                   SE_FILE_OBJECT,
                                                   DACL_SECURITY_INFORMATION,
                                                   nullptr,
                                                   nullptr,
                                                   &oldDacl,
                                                   nullptr,
                                                   &rawDescriptor);
    LocalMemory descriptor(rawDescriptor);
    require(infoResult == ERROR_SUCCESS, "GetNamedSecurityInfoW for addAce failed");

    EXPLICIT_ACCESSW access{};
    access.grfAccessPermissions = mask;
    access.grfAccessMode = mode;
    access.grfInheritance = inheritance;
    BuildTrusteeWithSidW(&access.Trustee, sid);

    PACL newDacl = nullptr;
    const DWORD aclResult = SetEntriesInAclW(1, &access, oldDacl, &newDacl);
    LocalMemory aclMemory(newDacl);
    require(aclResult == ERROR_SUCCESS, "SetEntriesInAclW for addAce failed");

    const DWORD setResult = SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),
                                                  SE_FILE_OBJECT,
                                                  DACL_SECURITY_INFORMATION,
                                                  nullptr,
                                                  nullptr,
                                                  newDacl,
                                                  nullptr);
    return setResult == ERROR_SUCCESS;
}

bool replaceTargetAllowAce(const std::wstring& path, PSID sid, ACCESS_MASK mask)
{
    return addAce(path,
                  sid,
                  SET_ACCESS,
                  mask,
                  OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE);
}

bool addTargetDenyAce(const std::wstring& path, PSID sid, ACCESS_MASK mask)
{
    return addAce(path,
                  sid,
                  DENY_ACCESS,
                  mask,
                  OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE);
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
    SetFileAttributesW(outsideFile.c_str(), FILE_ATTRIBUTE_NORMAL);
}

void verifyInstallRootMarkerGuards(const std::wstring& validTool,
                                   const std::wstring& parentDir,
                                   const std::wstring& toolSource)
{
    const std::wstring unrelatedDir = joinPath(parentDir, L"UnrelatedApp");
    std::filesystem::create_directories(unrelatedDir);
    require(runPermissionTool(validTool, L"verify", unrelatedDir) != 0,
            "ordinary directory without marker was accepted");

    const std::wstring fakeNamedRoot = joinPath(parentDir, L"FakeVaporViewName");
    std::filesystem::create_directories(fakeNamedRoot);
    require(runPermissionTool(validTool, L"verify", fakeNamedRoot) != 0,
            "directory named like VaporView without marker was accepted");

    const std::wstring badMarkerRoot = joinPath(parentDir, L"Bad Marker Root");
    createFakeInstallRoot(badMarkerRoot, toolSource);
    writeTextFile(joinPath(badMarkerRoot, L".vaporview-install-root"), "bad marker\n");
    require(runPermissionTool(joinPath(badMarkerRoot, L"VaporViewPermissionTool.exe"),
                              L"verify",
                              badMarkerRoot) != 0,
            "invalid marker content was accepted");

    const std::wstring largeMarkerRoot = joinPath(parentDir, L"Large Marker Root");
    createFakeInstallRoot(largeMarkerRoot, toolSource);
    writeTextFile(joinPath(largeMarkerRoot, L".vaporview-install-root"),
                  std::string(2048, 'x'));
    require(runPermissionTool(joinPath(largeMarkerRoot, L"VaporViewPermissionTool.exe"),
                              L"verify",
                              largeMarkerRoot) != 0,
            "oversized marker was accepted");

    const std::wstring markerOnlyRoot = joinPath(parentDir, L"Marker Only Root");
    std::filesystem::create_directories(markerOnlyRoot);
    writeTextFile(joinPath(markerOnlyRoot, L".vaporview-install-root"),
                  "VAPORVIEW_INSTALL_ROOT_V1\nproduct=VaporView\n");
    require(runPermissionTool(validTool, L"verify", markerOnlyRoot) != 0,
            "marker-only directory was accepted");

    const std::wstring wrongToolDir = joinPath(parentDir, L"CopiedToolElsewhere");
    std::filesystem::create_directories(wrongToolDir);
    const std::wstring wrongTool = joinPath(wrongToolDir, L"VaporViewPermissionTool.exe");
    std::filesystem::copy_file(toolSource, wrongTool, std::filesystem::copy_options::overwrite_existing);
    require(runPermissionTool(wrongTool, L"verify", parentDir) != 0,
            "permission tool copied outside target root was accepted");
}

void verifyAclFailureCases(const std::wstring& targetDir,
                           const std::wstring& tool,
                           PSID targetSid)
{
    require(addTargetDenyAce(targetDir, targetSid, FILE_WRITE_DATA),
            "failed to add deny write ACE");
    require(runPermissionTool(tool, L"verify", targetDir) != 0,
            "verify passed despite a higher-priority deny write ACE");
    require(removeExplicitDenyAcesForSid(targetDir, targetSid),
            "failed to remove deny write ACE");
    require(runPermissionTool(tool, L"apply", targetDir) == 0,
            "apply failed after deny write ACE");

    require(addTargetDenyAce(targetDir, targetSid, DELETE | FILE_DELETE_CHILD),
            "failed to add deny delete ACE");
    require(runPermissionTool(tool, L"verify", targetDir) != 0,
            "verify passed despite missing delete rights");
    require(removeExplicitDenyAcesForSid(targetDir, targetSid),
            "failed to remove deny delete ACE");
    require(runPermissionTool(tool, L"apply", targetDir) == 0,
            "apply failed after deny delete ACE");

    ACCESS_MASK missingWriteOwner = FILE_GENERIC_READ | FILE_GENERIC_WRITE |
                                    FILE_GENERIC_EXECUTE | DELETE | READ_CONTROL |
                                    WRITE_DAC;
    require(replaceTargetAllowAce(targetDir, targetSid, missingWriteOwner),
            "failed to replace allow ACE without WRITE_OWNER");
    require(runPermissionTool(tool, L"verify", targetDir) != 0,
            "verify passed despite missing WRITE_OWNER");
    require(runPermissionTool(tool, L"apply", targetDir) == 0,
            "apply failed after missing WRITE_OWNER ACE");

    ACCESS_MASK missingWriteDac = FILE_GENERIC_READ | FILE_GENERIC_WRITE |
                                  FILE_GENERIC_EXECUTE | DELETE | READ_CONTROL |
                                  WRITE_OWNER;
    require(replaceTargetAllowAce(targetDir, targetSid, missingWriteDac),
            "failed to replace allow ACE without WRITE_DAC");
    require(runPermissionTool(tool, L"verify", targetDir) != 0,
            "verify passed despite missing WRITE_DAC");
    require(runPermissionTool(tool, L"apply", targetDir) == 0,
            "apply failed after missing WRITE_DAC ACE");
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
    const std::wstring targetDir = joinPath(parentDir, L"VaporView \u6743\u9650 \u6D4B\u8BD5");
    std::filesystem::remove_all(parentDir);
    createFakeInstallRoot(targetDir, tool);
    std::filesystem::create_directories(joinPath(targetDir, L"child directory"));
    const std::wstring toolInRoot = joinPath(targetDir, L"VaporViewPermissionTool.exe");

    const std::wstring readonlyFile = joinPath(targetDir, L"child directory\\readonly file.txt");
    writeFile(readonlyFile);
    require(SetFileAttributesW(readonlyFile.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE,
            "failed to set readonly test attribute");

    verifyReparsePointIsNotFollowed(toolInRoot, targetDir, parentDir);
    verifyInstallRootMarkerGuards(toolInRoot, parentDir, tool);

    require(runPermissionTool(toolInRoot, L"apply", targetDir) == 0, "permission tool apply failed");
    require(runPermissionTool(toolInRoot, L"apply", targetDir) == 0,
            "permission tool second apply failed");
    require(runPermissionTool(toolInRoot, L"verify", targetDir) == 0, "permission tool verify failed");

    SidBuffer targetSid = currentUserSid();
    require(countExplicitTargetFullControlAces(targetDir, targetSid.sid()) == 1,
            "apply is not idempotent for target user ACEs");

    verifyAclFailureCases(targetDir, toolInRoot, targetSid.sid());
    require(runPermissionTool(toolInRoot, L"verify", targetDir) == 0,
            "permission tool verify failed after ACL failure cases were repaired");

    const std::wstring tempToolDir = joinPath(targetDir, L"tmpMaintenanceToolApp");
    std::filesystem::create_directories(tempToolDir);
    const std::wstring tempTool = joinPath(tempToolDir, L"VaporViewPermissionTool.exe");
    std::filesystem::copy_file(tool, tempTool, std::filesystem::copy_options::overwrite_existing);
    require(runPermissionTool(tempTool, L"verify", targetDir) == 0,
            "legal tmpMaintenanceToolApp permission tool was rejected");

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
                "broad user group has explicit Full Control");
    }
    for (const WELL_KNOWN_SID_TYPE type : {WinLocalSystemSid, WinBuiltinAdministratorsSid})
    {
        SidBuffer privileged = wellKnownSid(type);
        require(hasFullControlAce(targetDir, privileged.sid()),
                "SYSTEM or Administrators Full Control was not preserved");
    }

    verifyDangerousPathsAreRejected(toolInRoot);
    GetFileAttributesW(readonlyFile.c_str());
    std::filesystem::remove_all(parentDir);
    std::cout << "permission_tool_test passed\n";
    return 0;
}
