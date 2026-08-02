#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{

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
    if (directory.empty() || directory.back() == L'\\' || directory.back() == L'/')
    {
        return directory + fileName;
    }
    return directory + L"\\" + fileName;
}

std::string manifestText(const std::wstring& executable)
{
    HMODULE module = LoadLibraryExW(executable.c_str(),
                                    nullptr,
                                    LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!module)
    {
        std::wcerr << L"LoadLibraryExW failed for " << executable << L": " << GetLastError() << L"\n";
        std::exit(1);
    }

    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(1), RT_MANIFEST);
    if (!resource)
    {
        std::wcerr << L"RT_MANIFEST resource not found in " << executable << L"\n";
        FreeLibrary(module);
        std::exit(1);
    }

    HGLOBAL loaded = LoadResource(module, resource);
    const DWORD size = SizeofResource(module, resource);
    const auto* bytes = static_cast<const unsigned char*>(LockResource(loaded));
    if (!loaded || !bytes || size == 0)
    {
        std::wcerr << L"RT_MANIFEST resource could not be loaded from " << executable << L"\n";
        FreeLibrary(module);
        std::exit(1);
    }

    std::string text;
    text.reserve(size);
    for (DWORD i = 0; i < size; ++i)
    {
        if (bytes[i] != 0)
        {
            text.push_back(static_cast<char>(bytes[i]));
        }
    }
    FreeLibrary(module);
    return text;
}

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

} // namespace

int main()
{
    const std::wstring binaryDir = utf8ToWide(VAPORVIEW_BINARY_DIR);
    const std::vector<std::wstring> executables = {
        L"VaporView.exe",
        L"VaporViewSky.exe",
        L"VaporViewSkyCore.exe",
        L"VaporViewSkyTui.exe",
        L"VaporViewUpdateRelauncher.exe",
        L"VaporViewPermissionTool.exe",
    };

    for (const std::wstring& executableName : executables)
    {
        const std::wstring executablePath = joinPath(binaryDir, executableName);
        require(GetFileAttributesW(executablePath.c_str()) != INVALID_FILE_ATTRIBUTES,
                "missing executable under build/Release");
        const std::string manifest = manifestText(executablePath);
        require(manifest.find("requestedExecutionLevel") != std::string::npos,
                "manifest missing requestedExecutionLevel");
        require(manifest.find("asInvoker") != std::string::npos,
                "manifest missing asInvoker");
        require(manifest.find("uiAccess=\"false\"") != std::string::npos ||
                    manifest.find("uiAccess='false'") != std::string::npos,
                "manifest missing uiAccess=false");
        require(manifest.find("requireAdministrator") == std::string::npos,
                "manifest still contains requireAdministrator");
        require(manifest.find("highestAvailable") == std::string::npos,
                "manifest still contains highestAvailable");
    }

    std::cout << "manifest_policy_test passed\n";
    return 0;
}
