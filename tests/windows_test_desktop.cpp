#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <string>

namespace {
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

std::wstring quote(const wchar_t *argument)
{
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (const wchar_t *p = argument; *p; ++p) {
        if (*p == L'\\') { ++slashes; continue; }
        result.append(*p == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        result += *p;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}

int error(const char *operation)
{
    std::fprintf(stderr, "Test desktop: %s failed (%lu); no interactive fallback.\n", operation, GetLastError());
    return 2;
}

bool duplicateStandardHandle(DWORD id, Handle& target)
{
    return DuplicateHandle(GetCurrentProcess(), GetStdHandle(id), GetCurrentProcess(),
                           &target.value, 0, TRUE, DUPLICATE_SAME_ACCESS);
}
}

int wmain(int argc, wchar_t **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "Usage: vaporview_test_desktop.exe <executable> [arguments...]\n");
        return 2;
    }
    const auto name = L"VaporViewTests_" + std::to_wstring(GetCurrentProcessId());
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    const auto desktop = CreateDesktopW(name.c_str(), nullptr, nullptr, 0,
        DESKTOP_CREATEWINDOW | DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS |
        DESKTOP_ENUMERATE | DESKTOP_CREATEMENU, &security);
    if (!desktop) return error("CreateDesktop");
    // Never switch the user's input desktop. Descendants inherit this desktop.
    Handle job;
    job.value = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
                                             JOB_OBJECT_LIMIT_PRIORITY_CLASS;
    limits.BasicLimitInformation.PriorityClass = BELOW_NORMAL_PRIORITY_CLASS;
    if (!job.value || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        const auto result = error("CreateJobObject/SetInformationJobObject");
        CloseDesktop(desktop);
        return result;
    }
    Handle output, errors, input;
    if (!duplicateStandardHandle(STD_OUTPUT_HANDLE, output) || !duplicateStandardHandle(STD_ERROR_HANDLE, errors)) {
        const auto result = error("DuplicateHandle");
        CloseDesktop(desktop);
        return result;
    }
    input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &security, OPEN_EXISTING, 0, nullptr);
    if (input.value == INVALID_HANDLE_VALUE) {
        const auto result = error("CreateFile(NUL)");
        CloseDesktop(desktop);
        return result;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.lpDesktop = const_cast<wchar_t *>(name.c_str());
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_FORCEOFFFEEDBACK;
    startup.hStdInput = input.value;
    startup.hStdOutput = output.value;
    startup.hStdError = errors.value;
    std::wstring command;
    for (int i = 1; i < argc; ++i) {
        if (i > 1) command += L' ';
        command += quote(argv[i]);
    }
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS,
                        nullptr, nullptr, &startup, &process)) {
        const auto result = error("CreateProcess");
        CloseDesktop(desktop);
        return result;
    }
    Handle child, thread;
    child.value = process.hProcess;
    thread.value = process.hThread;
    if (!AssignProcessToJobObject(job.value, child.value)) {
        const auto result = error("AssignProcessToJobObject");
        TerminateProcess(child.value, 2);
        WaitForSingleObject(child.value, INFINITE);
        CloseDesktop(desktop);
        return result;
    }
    std::printf("Test desktop: VaporViewTests_%lu; child PID %lu; below-normal priority.\n",
                GetCurrentProcessId(), process.dwProcessId);
    std::fflush(stdout);
    if (ResumeThread(thread.value) == static_cast<DWORD>(-1) ||
        WaitForSingleObject(child.value, INFINITE) != WAIT_OBJECT_0) {
        const auto result = error("ResumeThread/WaitForSingleObject");
        CloseHandle(job.value);
        job.value = nullptr;
        CloseDesktop(desktop);
        return result;
    }
    DWORD result = 2;
    GetExitCodeProcess(child.value, &result);
    // Closing the job also reclaims any child accidentally left by a test.
    CloseHandle(job.value);
    job.value = nullptr;
    CloseDesktop(desktop);
    return static_cast<int>(result);
}
