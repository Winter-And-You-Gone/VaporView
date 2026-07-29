#pragma once

#include <QSettings>
#include <QString>
#include <QVariant>

#include <atomic>

namespace VaporView
{

inline std::atomic_bool g_settingsWritesSuspended{false};

inline bool settingsWritesSuspended()
{
    return g_settingsWritesSuspended.load(std::memory_order_acquire);
}

inline void setSettingsWritesSuspended(bool suspended)
{
    g_settingsWritesSuspended.store(suspended, std::memory_order_release);
}

inline void setPersistentSetting(QSettings& settings,
                                 const QString& key,
                                 const QVariant& value)
{
    if (!settingsWritesSuspended())
    {
        settings.setValue(key, value);
    }
}

inline void removePersistentSetting(QSettings& settings, const QString& key)
{
    if (!settingsWritesSuspended())
    {
        settings.remove(key);
    }
}

} // namespace VaporView
