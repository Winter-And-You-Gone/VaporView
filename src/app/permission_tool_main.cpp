#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cwctype>
#include <iostream>
#include <string>
#include <vector>

namespace
{

constexpr ACCESS_MASK kRequiredFullControl =
    FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE |
    DELETE | READ_CONTROL | WRITE_DAC | WRITE_OWNER;

GENERIC_MAPPING fileGenericMapping()
{
    GENERIC_MAPPING mapping{};
    mapping.GenericRead = FILE_GENERIC_READ;
    mapping.GenericWrite = FILE_GENERIC_WRITE;
    mapping.GenericExecute = FILE_GENERIC_EXECUTE;
    mapping.GenericAll = FILE_ALL_ACCESS;
    return mapping;
}

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

struct LocalMemory
{
    void* value = nullptr;

    LocalMemory() = default;
    explicit LocalMemory(void* memory) : value(memory) {}
    ~LocalMemory()
    {
        if (value)
        {
            LocalFree(value);
        }
    }

    LocalMemory(const LocalMemory&) = delete;
    LocalMemory& operator=(const LocalMemory&) = delete;
};

struct SidBuffer
{
    std::vector<BYTE> bytes;

    PSID sid() { return bytes.empty() ? nullptr : bytes.data(); }
    PSID sid() const { return bytes.empty() ? nullptr : const_cast<BYTE*>(bytes.data()); }
};

std::wstring lastErrorMessage(DWORD error)
{
    wchar_t* buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD size = FormatMessageW(flags,
                                      nullptr,
                                      error,
                                      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                      reinterpret_cast<LPWSTR>(&buffer),
                                      0,
                                      nullptr);
    LocalMemory memory(buffer);
    if (size == 0 || !buffer)
    {
        return L"Win32 error " + std::to_wstring(error);
    }
    std::wstring text(buffer, size);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' ||
                            text.back() == L' ' || text.back() == L'\t'))
    {
        text.pop_back();
    }
    return text + L" (" + std::to_wstring(error) + L")";
}

void printError(const std::wstring& message)
{
    std::wcerr << L"ERROR: " << message << L"\n";
}

bool iequals(const std::wstring& left, const std::wstring& right)
{
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool startsWithPath(const std::wstring& path, const std::wstring& parent)
{
    if (path.size() <= parent.size() || !iequals(path.substr(0, parent.size()), parent))
    {
        return false;
    }
    const wchar_t next = path[parent.size()];
    return next == L'\\' || next == L'/';
}

bool isDriveAbsolute(const std::wstring& path)
{
    return path.size() >= 3 &&
           std::iswalpha(path[0]) &&
           path[1] == L':' &&
           (path[2] == L'\\' || path[2] == L'/');
}

std::wstring normalizePath(const std::wstring& input)
{
    if (input.empty())
    {
        return {};
    }

    const DWORD required = GetFullPathNameW(input.c_str(), 0, nullptr, nullptr);
    if (required == 0)
    {
        return {};
    }

    std::wstring result(required, L'\0');
    const DWORD written = GetFullPathNameW(input.c_str(), required, result.data(), nullptr);
    if (written == 0 || written >= required)
    {
        return {};
    }
    result.resize(written);
    std::replace(result.begin(), result.end(), L'/', L'\\');
    while (result.size() > 3 && result.back() == L'\\')
    {
        result.pop_back();
    }
    return result;
}

std::wstring directoryNameOf(const std::wstring& path)
{
    const std::wstring::size_type slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
    {
        return path;
    }
    return path.substr(slash + 1);
}

std::wstring environmentPath(const wchar_t* name)
{
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0)
    {
        return {};
    }
    std::wstring value(required, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), required);
    if (written == 0 || written >= required)
    {
        return {};
    }
    value.resize(written);
    return normalizePath(value);
}

std::wstring windowsDirectory()
{
    std::wstring value(MAX_PATH, L'\0');
    const UINT written = GetWindowsDirectoryW(value.data(), static_cast<UINT>(value.size()));
    if (written == 0 || written >= value.size())
    {
        return {};
    }
    value.resize(written);
    return normalizePath(value);
}

std::wstring systemDirectory()
{
    std::wstring value(MAX_PATH, L'\0');
    const UINT written = GetSystemDirectoryW(value.data(), static_cast<UINT>(value.size()));
    if (written == 0 || written >= value.size())
    {
        return {};
    }
    value.resize(written);
    return normalizePath(value);
}

bool validateTargetDirectory(const std::wstring& rawPath,
                             std::wstring& normalizedPath,
                             std::wstring& error)
{
    if (rawPath.empty())
    {
        error = L"target directory is empty";
        return false;
    }
    if (!isDriveAbsolute(rawPath))
    {
        error = L"target directory must be an absolute local drive path";
        return false;
    }

    normalizedPath = normalizePath(rawPath);
    if (normalizedPath.empty())
    {
        error = L"target directory could not be normalized";
        return false;
    }
    if (normalizedPath.size() == 3 && normalizedPath[1] == L':' && normalizedPath[2] == L'\\')
    {
        error = L"refusing to modify a drive root";
        return false;
    }
    if (directoryNameOf(normalizedPath).empty() || directoryNameOf(normalizedPath) == L"." ||
        directoryNameOf(normalizedPath) == L"..")
    {
        error = L"target directory leaf is not a normal directory name";
        return false;
    }

    const std::wstring windowsRoot = windowsDirectory();
    const std::wstring systemRoot = systemDirectory();
    if (!windowsRoot.empty() &&
        (iequals(normalizedPath, windowsRoot) || startsWithPath(normalizedPath, windowsRoot)))
    {
        error = L"refusing to modify the Windows directory tree";
        return false;
    }
    if (!systemRoot.empty() &&
        (iequals(normalizedPath, systemRoot) || startsWithPath(normalizedPath, systemRoot)))
    {
        error = L"refusing to modify a Windows system directory";
        return false;
    }

    for (const wchar_t* variable : {L"ProgramFiles", L"ProgramFiles(x86)", L"ProgramW6432", L"USERPROFILE"})
    {
        const std::wstring protectedRoot = environmentPath(variable);
        if (!protectedRoot.empty() && iequals(normalizedPath, protectedRoot))
        {
            error = L"refusing to modify protected root directory " + protectedRoot;
            return false;
        }
    }

    const DWORD attributes = GetFileAttributesW(normalizedPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        error = L"target directory does not exist: " + lastErrorMessage(GetLastError());
        return false;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        error = L"target path is not a directory";
        return false;
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    {
        error = L"target directory itself is a reparse point";
        return false;
    }
    return true;
}

SidBuffer copySid(PSID sid)
{
    SidBuffer result;
    if (!sid || !IsValidSid(sid))
    {
        return result;
    }
    const DWORD length = GetLengthSid(sid);
    result.bytes.resize(length);
    if (!CopySid(length, result.sid(), sid))
    {
        result.bytes.clear();
    }
    return result;
}

SidBuffer tokenUserSid(HANDLE token)
{
    DWORD required = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &required);
    if (required == 0)
    {
        return {};
    }

    std::vector<BYTE> buffer(required);
    if (!GetTokenInformation(token, TokenUser, buffer.data(), required, &required))
    {
        return {};
    }
    const auto* tokenUser = reinterpret_cast<const TOKEN_USER*>(buffer.data());
    return copySid(tokenUser->User.Sid);
}

bool sameSession(DWORD processId, DWORD sessionId)
{
    DWORD candidateSession = 0;
    return ProcessIdToSessionId(processId, &candidateSession) &&
           candidateSession == sessionId;
}

SidBuffer interactiveShellUserSid()
{
    DWORD currentSession = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &currentSession))
    {
        printError(L"ProcessIdToSessionId failed: " + lastErrorMessage(GetLastError()));
        return {};
    }

    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.value == INVALID_HANDLE_VALUE)
    {
        printError(L"CreateToolhelp32Snapshot failed: " + lastErrorMessage(GetLastError()));
        return {};
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.value, &entry))
    {
        printError(L"Process32FirstW failed: " + lastErrorMessage(GetLastError()));
        return {};
    }

    do
    {
        if (!iequals(entry.szExeFile, L"explorer.exe") ||
            !sameSession(entry.th32ProcessID, currentSession))
        {
            continue;
        }

        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID));
        if (!process.value)
        {
            continue;
        }

        HANDLE rawToken = nullptr;
        if (!OpenProcessToken(process.value, TOKEN_QUERY, &rawToken))
        {
            continue;
        }
        Handle token(rawToken);
        SidBuffer sid = tokenUserSid(token.value);
        if (sid.sid())
        {
            return sid;
        }
    } while (Process32NextW(snapshot.value, &entry));

    printError(L"could not determine the interactive shell user SID from explorer.exe");
    return {};
}

std::wstring sidToString(PSID sid)
{
    LPWSTR raw = nullptr;
    if (!ConvertSidToStringSidW(sid, &raw))
    {
        return {};
    }
    LocalMemory memory(raw);
    return raw;
}

bool maskGrantsFullControl(ACCESS_MASK mask)
{
    if ((mask & GENERIC_ALL) == GENERIC_ALL)
    {
        return true;
    }
    GENERIC_MAPPING mapping = fileGenericMapping();
    ACCESS_MASK mapped = mask;
    MapGenericMask(&mapped, &mapping);
    return (mapped & kRequiredFullControl) == kRequiredFullControl;
}

bool getDacl(const std::wstring& path, PACL& dacl, LocalMemory& securityDescriptor, std::wstring& error)
{
    PSECURITY_DESCRIPTOR rawDescriptor = nullptr;
    const DWORD result = GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),
                                               SE_FILE_OBJECT,
                                               DACL_SECURITY_INFORMATION,
                                               nullptr,
                                               nullptr,
                                               &dacl,
                                               nullptr,
                                               &rawDescriptor);
    securityDescriptor.value = rawDescriptor;
    if (result != ERROR_SUCCESS)
    {
        error = L"GetNamedSecurityInfoW failed for " + path + L": " + lastErrorMessage(result);
        return false;
    }
    if (!dacl)
    {
        error = L"target has a null DACL: " + path;
        return false;
    }
    return true;
}

bool setFullControlAce(const std::wstring& path, PSID targetSid, bool directory)
{
    PACL currentDacl = nullptr;
    LocalMemory securityDescriptor;
    std::wstring error;
    if (!getDacl(path, currentDacl, securityDescriptor, error))
    {
        printError(error);
        return false;
    }

    EXPLICIT_ACCESSW access{};
    access.grfAccessPermissions = FILE_ALL_ACCESS;
    access.grfAccessMode = SET_ACCESS;
    access.grfInheritance = directory ? (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE) : NO_INHERITANCE;
    BuildTrusteeWithSidW(&access.Trustee, targetSid);

    PACL newDacl = nullptr;
    const DWORD mergeResult = SetEntriesInAclW(1, &access, currentDacl, &newDacl);
    LocalMemory mergedDacl(newDacl);
    if (mergeResult != ERROR_SUCCESS)
    {
        printError(L"SetEntriesInAclW failed for " + path + L": " + lastErrorMessage(mergeResult));
        return false;
    }

    const DWORD setResult = SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),
                                                  SE_FILE_OBJECT,
                                                  DACL_SECURITY_INFORMATION,
                                                  nullptr,
                                                  nullptr,
                                                  newDacl,
                                                  nullptr);
    if (setResult != ERROR_SUCCESS)
    {
        printError(L"SetNamedSecurityInfoW failed for " + path + L": " + lastErrorMessage(setResult));
        return false;
    }
    return true;
}

bool clearReadonlyFileAttribute(const std::wstring& path, DWORD attributes)
{
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        (attributes & FILE_ATTRIBUTE_READONLY) == 0)
    {
        return true;
    }
    const DWORD updated = attributes & ~FILE_ATTRIBUTE_READONLY;
    if (!SetFileAttributesW(path.c_str(), updated))
    {
        printError(L"SetFileAttributesW failed for " + path + L": " +
                   lastErrorMessage(GetLastError()));
        return false;
    }
    return true;
}

std::wstring joinPath(const std::wstring& directory, const std::wstring& name)
{
    if (directory.empty() || directory.back() == L'\\')
    {
        return directory + name;
    }
    return directory + L"\\" + name;
}

bool applyTree(const std::wstring& path, PSID targetSid)
{
    bool success = true;
    const DWORD rootAttributes = GetFileAttributesW(path.c_str());
    if (rootAttributes == INVALID_FILE_ATTRIBUTES)
    {
        printError(L"GetFileAttributesW failed for " + path + L": " +
                   lastErrorMessage(GetLastError()));
        return false;
    }

    const bool isDirectory = (rootAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (!setFullControlAce(path, targetSid, isDirectory))
    {
        success = false;
    }
    if (!clearReadonlyFileAttribute(path, rootAttributes))
    {
        success = false;
    }
    if (!isDirectory || (rootAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    {
        return success;
    }

    WIN32_FIND_DATAW data{};
    Handle findHandle(FindFirstFileW(joinPath(path, L"*").c_str(), &data));
    if (findHandle.value == INVALID_HANDLE_VALUE)
    {
        printError(L"FindFirstFileW failed for " + path + L": " + lastErrorMessage(GetLastError()));
        return false;
    }

    do
    {
        const std::wstring name(data.cFileName);
        if (name == L"." || name == L"..")
        {
            continue;
        }
        const std::wstring child = joinPath(path, name);
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        {
            std::wcout << L"Skipping reparse point: " << child << L"\n";
            continue;
        }
        if (!applyTree(child, targetSid))
        {
            success = false;
        }
    } while (FindNextFileW(findHandle.value, &data));

    const DWORD findError = GetLastError();
    if (findError != ERROR_NO_MORE_FILES)
    {
        printError(L"FindNextFileW failed for " + path + L": " + lastErrorMessage(findError));
        success = false;
    }
    return success;
}

SidBuffer wellKnownSid(WELL_KNOWN_SID_TYPE type)
{
    SidBuffer sid;
    sid.bytes.resize(SECURITY_MAX_SID_SIZE);
    DWORD size = static_cast<DWORD>(sid.bytes.size());
    if (!CreateWellKnownSid(type, nullptr, sid.sid(), &size))
    {
        sid.bytes.clear();
    }
    sid.bytes.resize(size);
    return sid;
}

bool aceSidEquals(void* ace, PSID sid);
ACCESS_MASK aceMask(void* ace);

bool daclHasFullControlAce(const std::wstring& path, PSID sid, bool includeInherited, bool& hasFullControl, std::wstring& error)
{
    PACL dacl = nullptr;
    LocalMemory securityDescriptor;
    if (!getDacl(path, dacl, securityDescriptor, error))
    {
        return false;
    }

    ACL_SIZE_INFORMATION info{};
    if (!GetAclInformation(dacl, &info, sizeof(info), AclSizeInformation))
    {
        error = L"GetAclInformation failed for " + path + L": " + lastErrorMessage(GetLastError());
        return false;
    }

    hasFullControl = false;
    for (DWORD index = 0; index < info.AceCount; ++index)
    {
        void* ace = nullptr;
        if (!GetAce(dacl, index, &ace))
        {
            continue;
        }
        const auto* header = reinterpret_cast<const ACE_HEADER*>(ace);
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE ||
            (!includeInherited && (header->AceFlags & INHERITED_ACE) != 0) ||
            !aceSidEquals(ace, sid))
        {
            continue;
        }
        if (maskGrantsFullControl(aceMask(ace)))
        {
            hasFullControl = true;
            return true;
        }
    }
    return true;
}

bool verifyEffectiveFullControl(const std::wstring& path, PSID sid, const std::wstring& label)
{
    bool hasFullControl = false;
    std::wstring error;
    if (!daclHasFullControlAce(path, sid, true, hasFullControl, error))
    {
        printError(error);
        return false;
    }
    if (!hasFullControl)
    {
        printError(label + L" does not have effective Full Control on " + path);
        return false;
    }
    return true;
}

bool aceSidEquals(void* ace, PSID sid)
{
    const auto* header = reinterpret_cast<const ACE_HEADER*>(ace);
    if (header->AceType != ACCESS_ALLOWED_ACE_TYPE)
    {
        return false;
    }
    const auto* allowed = reinterpret_cast<const ACCESS_ALLOWED_ACE*>(ace);
    return EqualSid(const_cast<SID*>(reinterpret_cast<const SID*>(&allowed->SidStart)), sid) != FALSE;
}

ACCESS_MASK aceMask(void* ace)
{
    const auto* allowed = reinterpret_cast<const ACCESS_ALLOWED_ACE*>(ace);
    return allowed->Mask;
}

bool verifyExplicitInheritedAce(const std::wstring& path, PSID targetSid)
{
    PACL dacl = nullptr;
    LocalMemory securityDescriptor;
    std::wstring error;
    if (!getDacl(path, dacl, securityDescriptor, error))
    {
        printError(error);
        return false;
    }

    ACL_SIZE_INFORMATION info{};
    if (!GetAclInformation(dacl, &info, sizeof(info), AclSizeInformation))
    {
        printError(L"GetAclInformation failed for " + path + L": " + lastErrorMessage(GetLastError()));
        return false;
    }

    int matches = 0;
    for (DWORD index = 0; index < info.AceCount; ++index)
    {
        void* ace = nullptr;
        if (!GetAce(dacl, index, &ace))
        {
            continue;
        }
        const auto* header = reinterpret_cast<const ACE_HEADER*>(ace);
        if ((header->AceFlags & INHERITED_ACE) != 0 || !aceSidEquals(ace, targetSid))
        {
            continue;
        }
        if ((header->AceFlags & OBJECT_INHERIT_ACE) != 0 &&
            (header->AceFlags & CONTAINER_INHERIT_ACE) != 0 &&
            maskGrantsFullControl(aceMask(ace)))
        {
            ++matches;
        }
    }

    if (matches != 1)
    {
        printError(L"expected exactly one explicit inheritable Full Control ACE for target user on " +
                   path + L", found " + std::to_wstring(matches));
        return false;
    }
    return true;
}

bool verifyForbiddenGroupsNotFullControl(const std::wstring& path)
{
    for (const WELL_KNOWN_SID_TYPE type :
         {WinWorldSid, WinBuiltinUsersSid, WinAuthenticatedUserSid})
    {
        SidBuffer sid = wellKnownSid(type);
        if (!sid.sid())
        {
            printError(L"could not create well-known SID");
            return false;
        }
        bool hasFullControl = false;
        std::wstring error;
        if (!daclHasFullControlAce(path, sid.sid(), false, hasFullControl, error))
        {
            printError(error);
            return false;
        }
        if (hasFullControl)
        {
            printError(L"forbidden broad user group has Full Control on " + path);
            return false;
        }
    }
    return true;
}

bool verifySystemAndAdministratorsFullControl(const std::wstring& path)
{
    for (const WELL_KNOWN_SID_TYPE type : {WinLocalSystemSid, WinBuiltinAdministratorsSid})
    {
        SidBuffer sid = wellKnownSid(type);
        if (!sid.sid())
        {
            printError(L"could not create well-known SID");
            return false;
        }
        if (!verifyEffectiveFullControl(path, sid.sid(), L"SYSTEM/Administrators"))
        {
            return false;
        }
    }
    return true;
}

bool writeProbeFile(const std::wstring& path)
{
    Handle file(CreateFileW(path.c_str(),
                            GENERIC_READ | GENERIC_WRITE | DELETE,
                            FILE_SHARE_READ | FILE_SHARE_DELETE,
                            nullptr,
                            CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_TEMPORARY,
                            nullptr));
    if (!file.value || file.value == INVALID_HANDLE_VALUE)
    {
        printError(L"CreateFileW failed for " + path + L": " + lastErrorMessage(GetLastError()));
        return false;
    }
    const char payload[] = "VaporView permission probe\n";
    DWORD written = 0;
    if (!WriteFile(file.value, payload, static_cast<DWORD>(sizeof(payload) - 1), &written, nullptr))
    {
        printError(L"WriteFile failed for " + path + L": " + lastErrorMessage(GetLastError()));
        return false;
    }
    return true;
}

bool verifyCreateRenameDelete(const std::wstring& targetDir, PSID targetSid)
{
    const std::wstring suffix = L".vaporview-permission-probe-" + std::to_wstring(GetCurrentProcessId());
    const std::wstring probeFile = joinPath(targetDir, suffix + L".tmp");
    const std::wstring renamedFile = joinPath(targetDir, suffix + L".renamed.tmp");
    const std::wstring probeDirectory = joinPath(targetDir, suffix + L".dir");

    DeleteFileW(probeFile.c_str());
    DeleteFileW(renamedFile.c_str());
    RemoveDirectoryW(probeDirectory.c_str());

    if (!writeProbeFile(probeFile))
    {
        return false;
    }
    if (!verifyEffectiveFullControl(probeFile, targetSid, L"target user"))
    {
        DeleteFileW(probeFile.c_str());
        return false;
    }
    if (!MoveFileExW(probeFile.c_str(), renamedFile.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        printError(L"MoveFileExW failed for probe file: " + lastErrorMessage(GetLastError()));
        DeleteFileW(probeFile.c_str());
        return false;
    }
    if (!DeleteFileW(renamedFile.c_str()))
    {
        printError(L"DeleteFileW failed for probe file: " + lastErrorMessage(GetLastError()));
        return false;
    }

    if (!CreateDirectoryW(probeDirectory.c_str(), nullptr))
    {
        printError(L"CreateDirectoryW failed for probe directory: " + lastErrorMessage(GetLastError()));
        return false;
    }
    const bool directoryRightsOk = verifyEffectiveFullControl(probeDirectory, targetSid, L"target user");
    if (!RemoveDirectoryW(probeDirectory.c_str()))
    {
        printError(L"RemoveDirectoryW failed for probe directory: " + lastErrorMessage(GetLastError()));
        return false;
    }
    return directoryRightsOk;
}

bool parseTargetDirectory(int argc, wchar_t** argv, std::wstring& action, std::wstring& targetDir)
{
    if (argc != 4)
    {
        return false;
    }
    action = argv[1];
    if (action != L"apply" && action != L"verify")
    {
        return false;
    }
    if (std::wstring(argv[2]) != L"--target-dir")
    {
        return false;
    }
    targetDir = argv[3] ? argv[3] : L"";
    return true;
}

int applyPermissions(const std::wstring& rawTargetDir)
{
    std::wstring targetDir;
    std::wstring error;
    if (!validateTargetDirectory(rawTargetDir, targetDir, error))
    {
        printError(error);
        return 10;
    }

    SidBuffer targetSid = interactiveShellUserSid();
    if (!targetSid.sid())
    {
        return 11;
    }

    std::wcout << L"TargetDir=" << targetDir << L"\n";
    std::wcout << L"TargetUserSid=" << sidToString(targetSid.sid()) << L"\n";
    if (!applyTree(targetDir, targetSid.sid()))
    {
        return 12;
    }
    return 0;
}

int verifyPermissions(const std::wstring& rawTargetDir)
{
    std::wstring targetDir;
    std::wstring error;
    if (!validateTargetDirectory(rawTargetDir, targetDir, error))
    {
        printError(error);
        return 20;
    }

    SidBuffer targetSid = interactiveShellUserSid();
    if (!targetSid.sid())
    {
        return 21;
    }

    bool ok = true;
    ok = verifyExplicitInheritedAce(targetDir, targetSid.sid()) && ok;
    ok = verifyEffectiveFullControl(targetDir, targetSid.sid(), L"target user") && ok;
    ok = verifySystemAndAdministratorsFullControl(targetDir) && ok;
    ok = verifyForbiddenGroupsNotFullControl(targetDir) && ok;
    ok = verifyCreateRenameDelete(targetDir, targetSid.sid()) && ok;
    if (!ok)
    {
        return 22;
    }

    std::wcout << L"Verified Full Control for SID " << sidToString(targetSid.sid())
               << L" on " << targetDir << L"\n";
    return 0;
}

void printUsage()
{
    std::wcerr << L"Usage:\n"
               << L"  VaporViewPermissionTool.exe apply --target-dir <directory>\n"
               << L"  VaporViewPermissionTool.exe verify --target-dir <directory>\n";
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    std::wstring action;
    std::wstring targetDir;
    if (!parseTargetDirectory(argc, argv, action, targetDir))
    {
        printUsage();
        return 2;
    }

    if (action == L"apply")
    {
        return applyPermissions(targetDir);
    }
    return verifyPermissions(targetDir);
}
