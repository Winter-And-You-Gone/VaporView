#include "shared/config/ApplicationConfig.h"

#include "shared/config/SettingsWriteBarrier.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>

namespace VaporView
{
namespace
{

QString configFilePathInWritableDirectory(const QDir& directory)
{
    const QString path = directory.filePath(QStringLiteral("vaporview.ini"));
    const QFileInfo configFile(path);
    if (configFile.exists())
    {
        QFile file(path);
        return configFile.isFile() && file.open(QIODevice::ReadWrite) ? path : QString();
    }

    QTemporaryFile writeProbe(directory.filePath(QStringLiteral(".vaporview-config-write-XXXXXX")));
    writeProbe.setAutoRemove(true);
    return writeProbe.open() ? path : QString();
}

QString resolveApplicationConfigFilePath()
{
    const QString explicitPath = qEnvironmentVariable("VAPORVIEW_CONFIG_FILE").trimmed();
    if (!explicitPath.isEmpty())
    {
        const QFileInfo fileInfo(explicitPath);
        QDir().mkpath(fileInfo.absolutePath());
        return fileInfo.absoluteFilePath();
    }

    QSettings scopedConfig(
        QSettings::IniFormat,
        QSettings::UserScope,
        QStringLiteral("VaporView"),
        QStringLiteral("VaporView"));
    if (QSettings::defaultFormat() == QSettings::IniFormat)
    {
        return scopedConfig.fileName();
    }

    const QDir applicationDirectory(QCoreApplication::applicationDirPath());
    QDir directory(applicationDirectory);
    for (int depth = 0; depth < 6; ++depth)
    {
        if (QFileInfo::exists(directory.filePath(QStringLiteral("CMakeLists.txt"))) &&
            QFileInfo::exists(directory.filePath(QStringLiteral("src/shared/config/ApplicationConfig.cpp"))))
        {
            const QString projectConfigPath = configFilePathInWritableDirectory(directory);
            if (!projectConfigPath.isEmpty())
            {
                return projectConfigPath;
            }
            break;
        }
        if (!directory.cdUp())
        {
            break;
        }
    }

    const QString portableConfigPath = configFilePathInWritableDirectory(applicationDirectory);
    return portableConfigPath.isEmpty() ? scopedConfig.fileName() : portableConfigPath;
}

bool isMainWindowApplicationConfigKey(const QString& key)
{
    for (const QString& prefix : {
             QStringLiteral("serial/"),
             QStringLiteral("rate/"),
             QStringLiteral("sensor/"),
             QStringLiteral("source/"),
             QStringLiteral("telemetry/"),
             QStringLiteral("tdlas/")})
    {
        if (key.startsWith(prefix, Qt::CaseInsensitive))
        {
            return true;
        }
    }

    if (key.startsWith(QStringLiteral("epsilon_custom_packet_rate_"), Qt::CaseInsensitive) ||
        key.startsWith(QStringLiteral("epsilon_last_config_"), Qt::CaseInsensitive))
    {
        return true;
    }

    const QString normalized = key.toLower();
    static const QStringList applicationKeys{
        QStringLiteral("epsilon_rtcm_device_port_index"),
        QStringLiteral("epsilon_rtcm_forward_port"),
        QStringLiteral("epsilon_rtcm_forward_baud"),
        QStringLiteral("recording_export_rate_hz"),
        QStringLiteral("imu_recording_rate_hz"),
        QStringLiteral("waveform_recording_rate_hz"),
        QStringLiteral("waveform_split_minutes"),
        QStringLiteral("epsilon_port"),
        QStringLiteral("epsilon_baud"),
        QStringLiteral("epsilon_rate"),
        QStringLiteral("ptb_port"),
        QStringLiteral("ptb_baud"),
        QStringLiteral("ptb_rate"),
        QStringLiteral("hmp_port"),
        QStringLiteral("hmp_baud"),
        QStringLiteral("hmp_rate"),
        QStringLiteral("lidar_port"),
        QStringLiteral("lidar_baud"),
        QStringLiteral("lidar_rate"),
        QStringLiteral("temperature_port"),
        QStringLiteral("temperature_baud"),
        QStringLiteral("temperature_rate")};
    return applicationKeys.contains(normalized);
}

template <typename Predicate>
void migrateLegacyScope(QSettings& target,
                        const QString& applicationName,
                        Predicate shouldMigrate)
{
    QSettings source(QSettings::defaultFormat(),
                     QSettings::UserScope,
                     QStringLiteral("VaporView"),
                     applicationName);
    if (source.fileName().compare(target.fileName(), Qt::CaseInsensitive) == 0)
    {
        return;
    }

    QStringList migratedKeys;
    for (const QString& key : source.allKeys())
    {
        if (!shouldMigrate(key))
        {
            continue;
        }
        const QString targetKey = applicationName + QLatin1Char('/') + key;
        target.setValue(targetKey, source.value(key));
        migratedKeys.push_back(key);
    }
    if (migratedKeys.isEmpty())
    {
        return;
    }

    target.sync();
    if (target.status() != QSettings::NoError)
    {
        return;
    }

    for (const QString& key : migratedKeys)
    {
        const QString targetKey = applicationName + QLatin1Char('/') + key;
        if (target.contains(targetKey) && target.value(targetKey) == source.value(key))
        {
            source.remove(key);
        }
    }
    source.sync();
}

} // namespace

QString applicationConfigFilePath()
{
    static const QString path = resolveApplicationConfigFilePath();
    return path;
}

QSettings applicationConfigSettings()
{
    return QSettings(applicationConfigFilePath(), QSettings::IniFormat);
}

void migrateLegacyApplicationConfig()
{
    if (settingsWritesSuspended())
    {
        return;
    }

    QSettings target = applicationConfigSettings();
    migrateLegacyScope(target, QStringLiteral("MainWindow"), isMainWindowApplicationConfigKey);
    migrateLegacyScope(target, QStringLiteral("TcpWavePanel"), [](const QString& key) {
        return key.startsWith(QStringLiteral("connection/"), Qt::CaseInsensitive);
    });
    migrateLegacyScope(target, QStringLiteral("TrajectoryViewer"), [](const QString& key) {
        return key.compare(QStringLiteral("map/tianditu_key"), Qt::CaseInsensitive) == 0;
    });
    migrateLegacyScope(target, QStringLiteral("SessionViewer"), [](const QString& key) {
        return key.startsWith(QStringLiteral("kf_gins/"), Qt::CaseInsensitive);
    });
    migrateLegacyScope(target, QStringLiteral("Map3D"), [](const QString& key) {
        return key.compare(QStringLiteral("mapManifestUrl"), Qt::CaseInsensitive) == 0;
    });
}

} // namespace VaporView
