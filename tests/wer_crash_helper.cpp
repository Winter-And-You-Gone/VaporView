#include <windows.h>

#include <cstdio>
#include <cwchar>

// This executable is a diagnostic stand-in, not any of the production apps.
// Copy it to a private VM directory under a target image name before use.
int wmain(int argc, wchar_t **argv)
{
    wchar_t path[32768] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, 32768);
    if (!length || length >= 32768) return 2;
    const wchar_t *name = std::wcsrchr(path, L'\\');
    name = name ? name + 1 : path;

    if (argc == 2 && std::wcscmp(argv[1], L"--describe") == 0) {
        std::wprintf(L"VaporView WER test helper v1 7E4A9C52: %ls\n", name);
        return 0;
    }
    wchar_t confirmation[64] = {};
    const DWORD count = GetEnvironmentVariableW(L"VAPORVIEW_WER_TEST_CONFIRM", confirmation, 64);
    const bool targetName = _wcsicmp(name, L"VaporView.exe") == 0 ||
        _wcsicmp(name, L"VaporViewSkyCore.exe") == 0 ||
        _wcsicmp(name, L"VaporViewSkyTui.exe") == 0;
    if (argc != 2 || std::wcscmp(argv[1], L"--trigger-controlled-crash") != 0 ||
        !targetName || count == 0 || count >= 64 ||
        std::wcscmp(confirmation, L"ISOLATED_VM_ONLY") != 0) {
        std::wprintf(L"Refusing: use --describe or follow the isolated VM WER test procedure.\n");
        return 2;
    }
    // A noncontinuable application exception, distinct from common system faults.
    RaiseException(0xE0425644, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    return 3;
}
