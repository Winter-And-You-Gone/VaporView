#pragma once

#include <QSettings>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <qt_windows.h>
#endif

namespace VaporViewTest::SettingsSandbox
{
inline QTemporaryDir *directory = nullptr;
#ifdef Q_OS_WIN
inline HKEY originalUserRegistry = nullptr;
inline HKEY isolatedUserRegistry = nullptr;
inline std::wstring registryPath;
#endif

inline void cleanup()
{
#ifdef Q_OS_WIN
    RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
    if (isolatedUserRegistry)
        RegCloseKey(isolatedUserRegistry);
    if (originalUserRegistry)
    {
        RegDeleteTreeW(originalUserRegistry, registryPath.c_str());
        RegCloseKey(originalUserRegistry);
    }
#endif
    delete directory;
}

// Initialize before QApplication or any test window. INI redirection alone does
// not isolate QSettings(organization, application) on Windows. atexit also runs
// when a require() assertion terminates a test via std::exit.
inline const bool initialized = [] {
    directory = new QTemporaryDir();
    std::atexit(cleanup);
    bool ok = directory->isValid();
#ifdef Q_OS_WIN
    registryPath = L"Software\\VaporViewTestSandbox-" + std::to_wstring(GetCurrentProcessId());
    ok = ok && RegOpenKeyExW(HKEY_CURRENT_USER, L"", 0, KEY_ALL_ACCESS, &originalUserRegistry) == ERROR_SUCCESS;
    ok = ok && RegCreateKeyExW(originalUserRegistry, registryPath.c_str(), 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr,
                              &isolatedUserRegistry, nullptr) == ERROR_SUCCESS;
    ok = ok && RegOverridePredefKey(HKEY_CURRENT_USER, isolatedUserRegistry) == ERROR_SUCCESS;
#endif
    if (!ok)
    {
        std::fputs("FAIL: cannot isolate GUI test settings\n", stderr);
        std::exit(1);
    }
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory->path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, directory->path());
    qputenv("VAPORVIEW_CONFIG_FILE", directory->filePath(QStringLiteral("vaporview.ini")).toUtf8());
    return true;
}();
}
