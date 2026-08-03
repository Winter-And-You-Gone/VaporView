#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <aclapi.h>
#include <rpc.h>
#include <sddl.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cwctype>
#include <iostream>
#include <string>
#include <vector>

namespace
{

constexpr wchar_t kPermissionToolName[] = L"VaporViewPermissionTool.exe";
constexpr wchar_t kMarkerFileName[] = L".vaporview-install-root";
constexpr char kMarkerMagic[] = "VAPORVIEW_INSTALL_ROOT_V1\nproduct=VaporView\n";
constexpr DWORD kMaxMarkerBytes = 1024;

constexpr ACCESS_MASK kRequiredFullControl =
    FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE |
    DELETE | READ_CONTROL | WRITE_DAC | WRITE_OWNER;

constexpr ACCESS_MASK kRequiredAccessCheckRights =
    FILE_READ_DATA | FILE_WRITE_DATA | FILE_APPEND_DATA |
    FILE_READ_EA | FILE_WRITE_EA | FILE_EXECUTE | FILE_DELETE_CHILD |
    FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | DELETE | READ_CONTROL |
    WRITE_DAC | WRITE_OWNER | SYNCHRONIZE;

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

std::wstring lastErrorMessage(DWORD error);
void printError(const std::wstring& message);

struct InteractiveUserToken
{
    Handle token;
    SidBuffer sid;
    DWORD sessionId = 0;
};

class ScopedImpersonation final
{
public:
    explicit ScopedImpersonation(HANDLE token)
        : active_(ImpersonateLoggedOnUser(token) != FALSE)
    {
        if (!active_)
        {
            error_ = GetLastError();
        }
    }

    ~ScopedImpersonation()
    {
        if (active_ && !RevertToSelf())
        {
            printError(L"RevertToSelf failed in ScopedImpersonation destructor: " +
                       lastErrorMessage(GetLastError()));
        }
    }

    ScopedImpersonation(const ScopedImpersonation&) = delete;
    ScopedImpersonation& operator=(const ScopedImpersonation&) = delete;

    bool active() const noexcept { return active_; }
    DWORD error() const noexcept { return error_; }

    bool revert()
    {
        if (!active_)
        {
            return true;
        }
        if (!RevertToSelf())
        {
            error_ = GetLastError();
            printError(L"RevertToSelf failed: " + lastErrorMessage(error_));
            return false;
        }
        active_ = false;
        return true;
    }

private:
    bool active_ = false;
    DWORD error_ = ERROR_SUCCESS;
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

std::wstring joinPath(const std::wstring& directory, const std::wstring& name);

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

bool directoryExistsWithoutReparsePoint(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

std::wstring currentExecutablePath()
{
    std::wstring path(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD written = GetModuleFileNameW(nullptr,
                                                 path.data(),
                                                 static_cast<DWORD>(path.size()));
        if (written == 0)
        {
            return {};
        }
        if (written < path.size() - 1)
        {
            path.resize(written);
            return normalizePath(path);
        }
        path.resize(path.size() * 2);
    }
}

bool readMarkerFile(const std::wstring& markerPath, std::string& content, std::wstring& error)
{
    const DWORD attributes = GetFileAttributesW(markerPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        error = L"marker missing: " + markerPath;
        return false;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    {
        error = L"marker is not a regular file: " + markerPath;
        return false;
    }

    Handle file(CreateFileW(markerPath.c_str(),
                            GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_DELETE,
                            nullptr,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL,
                            nullptr));
    if (!file.value || file.value == INVALID_HANDLE_VALUE)
    {
        error = L"CreateFileW(marker) failed for " + markerPath + L": " +
                lastErrorMessage(GetLastError());
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.value, &size))
    {
        error = L"GetFileSizeEx(marker) failed for " + markerPath + L": " +
                lastErrorMessage(GetLastError());
        return false;
    }
    if (size.QuadPart <= 0 || size.QuadPart > kMaxMarkerBytes)
    {
        error = L"marker content size is invalid: " + markerPath;
        return false;
    }

    content.assign(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    if (!ReadFile(file.value,
                  content.data(),
                  static_cast<DWORD>(content.size()),
                  &read,
                  nullptr) ||
        read != content.size())
    {
        error = L"ReadFile(marker) failed for " + markerPath + L": " +
                lastErrorMessage(GetLastError());
        return false;
    }
    return true;
}

bool validateInstallMarker(const std::wstring& targetDir, std::wstring& error)
{
    std::string marker;
    const std::wstring markerPath = joinPath(targetDir, kMarkerFileName);
    if (!readMarkerFile(markerPath, marker, error))
    {
        error = L"target is not a valid VaporView install root: " + error;
        return false;
    }
    if (marker != kMarkerMagic)
    {
        error = L"invalid marker content; target is not a valid VaporView install root: " + markerPath;
        return false;
    }
    return true;
}

bool validatePermissionToolLocation(const std::wstring& targetDir, std::wstring& error)
{
    const std::wstring executablePath = currentExecutablePath();
    if (executablePath.empty())
    {
        error = L"GetModuleFileNameW failed: " + lastErrorMessage(GetLastError());
        return false;
    }
    if (!regularFileExistsWithoutReparsePoint(executablePath) ||
        !iequals(directoryNameOf(executablePath), kPermissionToolName))
    {
        error = L"permission tool executable location is invalid: " + executablePath;
        return false;
    }

    const std::wstring toolDirectory = parentOf(executablePath);
    const std::wstring expectedRootTool = joinPath(targetDir, kPermissionToolName);
    const std::wstring tempToolDirectory = joinPath(targetDir, L"tmpMaintenanceToolApp");
    const std::wstring expectedTempTool = joinPath(tempToolDirectory, kPermissionToolName);

    if (iequals(executablePath, normalizePath(expectedRootTool)))
    {
        return true;
    }
    if (iequals(toolDirectory, normalizePath(tempToolDirectory)) &&
        iequals(executablePath, normalizePath(expectedTempTool)) &&
        directoryExistsWithoutReparsePoint(tempToolDirectory))
    {
        return true;
    }

    error = L"permission tool must be in the install root or valid tmpMaintenanceToolApp: " + executablePath;
    return false;
}

bool validateVaporViewInstallRoot(const std::wstring& targetDir, bool requireComplete, std::wstring& error)
{
    if (!validateInstallMarker(targetDir, error))
    {
        return false;
    }
    if (!validatePermissionToolLocation(targetDir, error))
    {
        return false;
    }

    if (!regularFileExistsWithoutReparsePoint(joinPath(targetDir, kPermissionToolName)))
    {
        error = L"target is not a valid VaporView install root: missing " + std::wstring(kPermissionToolName);
        return false;
    }

    const bool hasMainExe = regularFileExistsWithoutReparsePoint(joinPath(targetDir, L"VaporView.exe"));
    const bool hasResources = directoryExistsWithoutReparsePoint(joinPath(targetDir, L"resources"));
    const bool hasMaintenanceTool =
        regularFileExistsWithoutReparsePoint(joinPath(targetDir, L"VaporViewMaintenanceTool.exe"));

    if (requireComplete)
    {
        if (!hasMainExe || !hasResources)
        {
            error = L"target is not a complete VaporView install root: missing VaporView.exe or resources";
            return false;
        }
    }
    else if (!hasMainExe && !hasResources && !hasMaintenanceTool)
    {
        error = L"target is not a valid VaporView install root: missing product files";
        return false;
    }
    return true;
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

bool acquireInteractiveUserToken(InteractiveUserToken& result)
{
    DWORD currentSession = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &currentSession))
    {
        printError(L"ProcessIdToSessionId failed: " + lastErrorMessage(GetLastError()));
        return false;
    }

    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.value == INVALID_HANDLE_VALUE)
    {
        printError(L"CreateToolhelp32Snapshot failed: " + lastErrorMessage(GetLastError()));
        return false;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.value, &entry))
    {
        printError(L"Process32FirstW failed: " + lastErrorMessage(GetLastError()));
        return false;
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
            printError(L"OpenProcess(explorer.exe) failed: " + lastErrorMessage(GetLastError()));
            continue;
        }

        HANDLE rawToken = nullptr;
        if (!OpenProcessToken(process.value,
                              TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_IMPERSONATE,
                              &rawToken))
        {
            printError(L"OpenProcessToken(explorer.exe) failed: " +
                       lastErrorMessage(GetLastError()));
            continue;
        }
        Handle processToken(rawToken);

        HANDLE rawImpersonationToken = nullptr;
        if (!DuplicateTokenEx(processToken.value,
                              TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_IMPERSONATE,
                              nullptr,
                              SecurityImpersonation,
                              TokenImpersonation,
                              &rawImpersonationToken))
        {
            printError(L"DuplicateTokenEx(explorer.exe) failed: " +
                       lastErrorMessage(GetLastError()));
            continue;
        }

        SidBuffer sid = tokenUserSid(processToken.value);
        if (!sid.sid())
        {
            printError(L"GetTokenInformation(TokenUser) failed for explorer.exe token");
            CloseHandle(rawImpersonationToken);
            continue;
        }

        result.token = Handle(rawImpersonationToken);
        result.sid = std::move(sid);
        result.sessionId = currentSession;
        return true;
    } while (Process32NextW(snapshot.value, &entry));

    printError(L"could not acquire interactive user token: no usable explorer.exe token in the current session");
    return false;
}

SidBuffer interactiveShellUserSid()
{
    InteractiveUserToken user;
    if (!acquireInteractiveUserToken(user))
    {
        return {};
    }
    return std::move(user.sid);
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
            printError(L"forbidden broad user group has explicit Full Control on " + path);
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

bool verifyAccessCheckFullControl(const std::wstring& path, HANDLE impersonationToken)
{
    PSECURITY_DESCRIPTOR rawDescriptor = nullptr;
    const DWORD result = GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),
                                               SE_FILE_OBJECT,
                                               OWNER_SECURITY_INFORMATION |
                                                   GROUP_SECURITY_INFORMATION |
                                                   DACL_SECURITY_INFORMATION,
                                               nullptr,
                                               nullptr,
                                               nullptr,
                                               nullptr,
                                               &rawDescriptor);
    LocalMemory securityDescriptor(rawDescriptor);
    if (result != ERROR_SUCCESS)
    {
        printError(L"GetNamedSecurityInfoW before AccessCheck failed for " + path + L": " +
                   lastErrorMessage(result));
        return false;
    }

    GENERIC_MAPPING mapping = fileGenericMapping();
    ACCESS_MASK desired = kRequiredAccessCheckRights;
    MapGenericMask(&desired, &mapping);

    PRIVILEGE_SET privileges{};
    DWORD privilegeLength = sizeof(privileges);
    DWORD grantedAccess = 0;
    BOOL accessStatus = FALSE;
    if (!AccessCheck(rawDescriptor,
                     impersonationToken,
                     desired,
                     &mapping,
                     &privileges,
                     &privilegeLength,
                     &grantedAccess,
                     &accessStatus))
    {
        printError(L"AccessCheck API failed for " + path + L": " +
                   lastErrorMessage(GetLastError()));
        return false;
    }
    if (!accessStatus || (grantedAccess & desired) != desired)
    {
        printError(L"interactive user effective rights are insufficient for " + path +
                   L": desired=0x" + std::to_wstring(desired) +
                   L", granted=0x" + std::to_wstring(grantedAccess));
        return false;
    }
    return true;
}

std::wstring createGuidSuffix()
{
    UUID uuid{};
    const RPC_STATUS status = UuidCreate(&uuid);
    if (status != RPC_S_OK && status != RPC_S_UUID_LOCAL_ONLY)
    {
        return std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    }

    RPC_WSTR raw = nullptr;
    if (UuidToStringW(&uuid, &raw) != RPC_S_OK || !raw)
    {
        return std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    }
    std::wstring result(reinterpret_cast<wchar_t*>(raw));
    RpcStringFreeW(&raw);
    return result;
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

bool readProbeFile(const std::wstring& path)
{
    Handle file(CreateFileW(path.c_str(),
                            GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_DELETE,
                            nullptr,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL,
                            nullptr));
    if (!file.value || file.value == INVALID_HANDLE_VALUE)
    {
        printError(L"CreateFileW(read probe) failed for " + path + L": " +
                   lastErrorMessage(GetLastError()));
        return false;
    }

    char buffer[64] = {};
    DWORD read = 0;
    if (!ReadFile(file.value, buffer, sizeof(buffer), &read, nullptr))
    {
        printError(L"ReadFile(probe) failed for " + path + L": " +
                   lastErrorMessage(GetLastError()));
        return false;
    }
    return read > 0;
}

bool appendProbeFile(const std::wstring& path)
{
    Handle file(CreateFileW(path.c_str(),
                            FILE_APPEND_DATA | FILE_READ_DATA,
                            FILE_SHARE_READ | FILE_SHARE_DELETE,
                            nullptr,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL,
                            nullptr));
    if (!file.value || file.value == INVALID_HANDLE_VALUE)
    {
        printError(L"CreateFileW(append probe) failed for " + path + L": " +
                   lastErrorMessage(GetLastError()));
        return false;
    }
    const char payload[] = "append\n";
    DWORD written = 0;
    if (!WriteFile(file.value, payload, static_cast<DWORD>(sizeof(payload) - 1), &written, nullptr))
    {
        printError(L"WriteFile(append probe) failed for " + path + L": " +
                   lastErrorMessage(GetLastError()));
        return false;
    }
    return true;
}

void cleanupProbePath(const std::wstring& file,
                      const std::wstring& nestedFile,
                      const std::wstring& nestedDirectory,
                      const std::wstring& rootDirectory)
{
    if (!file.empty())
    {
        SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(file.c_str());
    }
    if (!nestedFile.empty())
    {
        SetFileAttributesW(nestedFile.c_str(), FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(nestedFile.c_str());
    }
    if (!nestedDirectory.empty())
    {
        RemoveDirectoryW(nestedDirectory.c_str());
    }
    if (!rootDirectory.empty())
    {
        RemoveDirectoryW(rootDirectory.c_str());
    }
}

bool verifyImpersonatedProbe(const std::wstring& targetDir,
                             HANDLE impersonationToken,
                             PSID targetSid)
{
    const std::wstring probeRoot = joinPath(targetDir, L".vaporview-permission-probe-" + createGuidSuffix());
    const std::wstring probeFile = joinPath(probeRoot, L"probe.txt");
    const std::wstring renamedFile = joinPath(probeRoot, L"probe-renamed.txt");
    const std::wstring nestedDirectory = joinPath(probeRoot, L"nested");
    const std::wstring nestedFile = joinPath(nestedDirectory, L"inherited.txt");

    bool ok = true;
    {
        ScopedImpersonation impersonation(impersonationToken);
        if (!impersonation.active())
        {
            printError(L"impersonation failed: ImpersonateLoggedOnUser failed: " +
                       lastErrorMessage(impersonation.error()));
            return false;
        }

        if (!CreateDirectoryW(probeRoot.c_str(), nullptr))
        {
            printError(L"target user probe failed: CreateDirectoryW(probe root) failed: " +
                       lastErrorMessage(GetLastError()));
            ok = false;
        }
        if (ok && !writeProbeFile(probeFile))
        {
            ok = false;
        }
        if (ok && !readProbeFile(probeFile))
        {
            ok = false;
        }
        if (ok && !appendProbeFile(probeFile))
        {
            ok = false;
        }
        if (ok && !MoveFileExW(probeFile.c_str(), renamedFile.c_str(), MOVEFILE_REPLACE_EXISTING))
        {
            printError(L"target user probe failed: MoveFileExW failed: " +
                       lastErrorMessage(GetLastError()));
            ok = false;
        }
        if (ok && !SetFileAttributesW(renamedFile.c_str(), FILE_ATTRIBUTE_ARCHIVE))
        {
            printError(L"target user probe failed: SetFileAttributesW failed: " +
                       lastErrorMessage(GetLastError()));
            ok = false;
        }
        if (ok && !CreateDirectoryW(nestedDirectory.c_str(), nullptr))
        {
            printError(L"target user probe failed: CreateDirectoryW(nested) failed: " +
                       lastErrorMessage(GetLastError()));
            ok = false;
        }
        if (ok && !writeProbeFile(nestedFile))
        {
            ok = false;
        }

        if (ok)
        {
            const DWORD nestedAttributes = GetFileAttributesW(nestedFile.c_str());
            if (nestedAttributes == INVALID_FILE_ATTRIBUTES ||
                (nestedAttributes & FILE_ATTRIBUTE_READONLY) != 0)
            {
                printError(L"target user probe failed: inherited file has unexpected ReadOnly attribute");
                ok = false;
            }
        }

        if (ok && !DeleteFileW(nestedFile.c_str()))
        {
            printError(L"target user probe failed: DeleteFileW(nested) failed: " +
                       lastErrorMessage(GetLastError()));
            ok = false;
        }
        if (ok && !RemoveDirectoryW(nestedDirectory.c_str()))
        {
            printError(L"target user probe failed: RemoveDirectoryW(nested) failed: " +
                       lastErrorMessage(GetLastError()));
            ok = false;
        }
        if (ok && !DeleteFileW(renamedFile.c_str()))
        {
            printError(L"target user probe failed: DeleteFileW(renamed) failed: " +
                       lastErrorMessage(GetLastError()));
            ok = false;
        }
        if (ok && !RemoveDirectoryW(probeRoot.c_str()))
        {
            printError(L"target user probe failed: RemoveDirectoryW(probe root) failed: " +
                       lastErrorMessage(GetLastError()));
            ok = false;
        }

        if (!impersonation.revert())
        {
            cleanupProbePath(renamedFile, nestedFile, nestedDirectory, probeRoot);
            return false;
        }
    }

    if (!ok)
    {
        cleanupProbePath(renamedFile, nestedFile, nestedDirectory, probeRoot);
        return false;
    }

    const std::wstring inheritanceRoot = joinPath(targetDir, L".vaporview-permission-probe-" + createGuidSuffix());
    const std::wstring inheritanceDir = joinPath(inheritanceRoot, L"child");
    const std::wstring inheritanceFile = joinPath(inheritanceDir, L"child.txt");
    bool inheritanceOk = true;
    {
        ScopedImpersonation impersonation(impersonationToken);
        if (!impersonation.active())
        {
            printError(L"impersonation failed: ImpersonateLoggedOnUser failed: " +
                       lastErrorMessage(impersonation.error()));
            return false;
        }
        if (!CreateDirectoryW(inheritanceRoot.c_str(), nullptr) ||
            !CreateDirectoryW(inheritanceDir.c_str(), nullptr) ||
            !writeProbeFile(inheritanceFile))
        {
            inheritanceOk = false;
        }
        if (!impersonation.revert())
        {
            cleanupProbePath({}, inheritanceFile, inheritanceDir, inheritanceRoot);
            return false;
        }
    }
    if (!inheritanceOk)
    {
        printError(L"target user probe failed: could not create inheritance probe items");
        cleanupProbePath({}, inheritanceFile, inheritanceDir, inheritanceRoot);
        return false;
    }

    bool inheritedFullControl = false;
    std::wstring error;
    inheritanceOk = daclHasFullControlAce(inheritanceFile, targetSid, true, inheritedFullControl, error) &&
                    inheritedFullControl &&
                    verifyAccessCheckFullControl(inheritanceFile, impersonationToken);
    if (!error.empty())
    {
        printError(error);
    }

    {
        ScopedImpersonation impersonation(impersonationToken);
        if (impersonation.active())
        {
            DeleteFileW(inheritanceFile.c_str());
            RemoveDirectoryW(inheritanceDir.c_str());
            RemoveDirectoryW(inheritanceRoot.c_str());
            impersonation.revert();
        }
    }
    cleanupProbePath({}, inheritanceFile, inheritanceDir, inheritanceRoot);

    if (!inheritanceOk)
    {
        printError(L"new file and directory inheritance validation failed");
        return false;
    }
    return true;
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

    if (!validateVaporViewInstallRoot(targetDir, false, error))
    {
        printError(error);
        return 13;
    }

    InteractiveUserToken targetUser;
    if (!acquireInteractiveUserToken(targetUser))
    {
        return 11;
    }

    std::wcout << L"TargetDir=" << targetDir << L"\n";
    std::wcout << L"TargetUserSid=" << sidToString(targetUser.sid.sid()) << L"\n";
    if (!applyTree(targetDir, targetUser.sid.sid()))
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

    if (!validateVaporViewInstallRoot(targetDir, true, error))
    {
        printError(error);
        return 23;
    }

    InteractiveUserToken targetUser;
    if (!acquireInteractiveUserToken(targetUser))
    {
        return 21;
    }

    bool ok = true;
    ok = verifyExplicitInheritedAce(targetDir, targetUser.sid.sid()) && ok;
    ok = verifyEffectiveFullControl(targetDir, targetUser.sid.sid(), L"target user") && ok;
    ok = verifySystemAndAdministratorsFullControl(targetDir) && ok;
    ok = verifyForbiddenGroupsNotFullControl(targetDir) && ok;
    if (!ok)
    {
        return 22;
    }
    if (!verifyAccessCheckFullControl(targetDir, targetUser.token.value))
    {
        return 24;
    }
    if (!verifyImpersonatedProbe(targetDir, targetUser.token.value, targetUser.sid.sid()))
    {
        return 25;
    }

    std::wcout << L"Verified effective Full Control for SID " << sidToString(targetUser.sid.sid())
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
