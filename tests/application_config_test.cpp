#include "shared/config/ApplicationConfig.h"
#include "shared/config/SettingsWriteBarrier.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main(int argc, char **argv)
{
    QTemporaryDir settingsDirectory;
    require(settingsDirectory.isValid(), "temporary settings directory created");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDirectory.path());

    QCoreApplication application(argc, argv);

    QSettings mainSettings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
    mainSettings.setValue(QStringLiteral("dark_theme_enabled"), true);
    mainSettings.setValue(QStringLiteral("recording_directory"), QStringLiteral("D:/records"));
    mainSettings.setValue(QStringLiteral("serial/epsilon_port"), QStringLiteral("COM7"));
    mainSettings.setValue(QStringLiteral("telemetry/tcp_host"), QStringLiteral("192.168.1.2"));
    mainSettings.setValue(QStringLiteral("recording_export_rate_hz"), 50);
    mainSettings.setValue(QStringLiteral("epsilon_custom_packet_rate_40"), 250);
    mainSettings.setValue(QStringLiteral("epsilon_rtcm_device_port_index"), 3);
    mainSettings.sync();

    QSettings waveSettings(QStringLiteral("VaporView"), QStringLiteral("TcpWavePanel"));
    waveSettings.setValue(QStringLiteral("connection/host"), QStringLiteral("10.0.0.8"));
    waveSettings.setValue(QStringLiteral("peak_filter/mode"), QStringLiteral("iqr"));
    waveSettings.sync();

    QSettings trajectorySettings(QStringLiteral("VaporView"), QStringLiteral("TrajectoryViewer"));
    trajectorySettings.setValue(QStringLiteral("map/tianditu_key"), QStringLiteral("plain-test-key"));
    trajectorySettings.setValue(QStringLiteral("map/source"), QStringLiteral("tianditu_img"));
    trajectorySettings.sync();

    QSettings sessionSettings(QStringLiteral("VaporView"), QStringLiteral("SessionViewer"));
    sessionSettings.setValue(QStringLiteral("kf_gins/executable_path"), QStringLiteral("D:/KF-GINS/KF-GINS.exe"));
    sessionSettings.setValue(QStringLiteral("last_session_directory"), QStringLiteral("D:/sessions"));
    sessionSettings.sync();

    QSettings mapSettings(QStringLiteral("VaporView"), QStringLiteral("Map3D"));
    mapSettings.setValue(QStringLiteral("mapManifestUrl"), QStringLiteral("https://example.test/maps.json"));
    mapSettings.setValue(QStringLiteral("heatPalette"), 2);
    mapSettings.sync();

    VaporView::migrateLegacyApplicationConfig();

    QSettings applicationSettings = VaporView::applicationConfigSettings();
    require(QFileInfo(VaporView::applicationConfigFilePath()).absoluteFilePath()
                .startsWith(QFileInfo(settingsDirectory.path()).absoluteFilePath()),
            "INI test mode keeps application config in the isolated settings directory");
    require(applicationSettings.value(QStringLiteral("MainWindow/serial/epsilon_port")).toString() ==
                QStringLiteral("COM7"),
            "device serial setting migrated to application config");
    require(applicationSettings.value(QStringLiteral("MainWindow/telemetry/tcp_host")).toString() ==
                QStringLiteral("192.168.1.2"),
            "telemetry setting migrated to application config");
    require(applicationSettings.value(QStringLiteral("MainWindow/recording_export_rate_hz")).toInt() == 50,
            "recording policy migrated to application config");
    require(applicationSettings.value(QStringLiteral("MainWindow/epsilon_custom_packet_rate_40")).toInt() == 250,
            "EPSILON packet-rate value migrated to application config");
    require(applicationSettings.value(QStringLiteral("MainWindow/epsilon_rtcm_device_port_index")).toInt() == 3,
            "EPSILON RTCM device input port selection migrated to application config");
    require(applicationSettings.value(QStringLiteral("TcpWavePanel/connection/host")).toString() ==
                QStringLiteral("10.0.0.8"),
            "wave connection migrated to application config");
    require(applicationSettings.value(QStringLiteral("TrajectoryViewer/map/tianditu_key")).toString() ==
                QStringLiteral("plain-test-key"),
            "plain-text map key migrated to application config");
    require(applicationSettings.value(QStringLiteral("SessionViewer/kf_gins/executable_path")).toString() ==
                QStringLiteral("D:/KF-GINS/KF-GINS.exe"),
            "external tool path migrated to application config");
    require(applicationSettings.value(QStringLiteral("Map3D/mapManifestUrl")).toString() ==
                QStringLiteral("https://example.test/maps.json"),
            "map manifest URL migrated to application config");

    require(mainSettings.value(QStringLiteral("dark_theme_enabled")).toBool(),
            "theme preference remains in user settings");
    require(mainSettings.value(QStringLiteral("recording_directory")).toString() == QStringLiteral("D:/records"),
            "recent recording directory remains in user settings");
    require(!mainSettings.contains(QStringLiteral("serial/epsilon_port")) &&
                !mainSettings.contains(QStringLiteral("telemetry/tcp_host")) &&
                !mainSettings.contains(QStringLiteral("recording_export_rate_hz")) &&
                !mainSettings.contains(QStringLiteral("epsilon_custom_packet_rate_40")) &&
                !mainSettings.contains(QStringLiteral("epsilon_rtcm_device_port_index")),
            "migrated application settings are removed from the legacy scope");
    require(waveSettings.value(QStringLiteral("peak_filter/mode")).toString() == QStringLiteral("iqr") &&
                !waveSettings.contains(QStringLiteral("connection/host")),
            "wave UI state stays in user settings while its connection moves");
    require(trajectorySettings.value(QStringLiteral("map/source")).toString() == QStringLiteral("tianditu_img") &&
                !trajectorySettings.contains(QStringLiteral("map/tianditu_key")),
            "map provider preference stays in user settings while the key moves");
    require(sessionSettings.value(QStringLiteral("last_session_directory")).toString() == QStringLiteral("D:/sessions") &&
                !sessionSettings.contains(QStringLiteral("kf_gins/executable_path")),
            "recent session path stays in user settings while the tool path moves");
    require(mapSettings.value(QStringLiteral("heatPalette")).toInt() == 2 &&
                !mapSettings.contains(QStringLiteral("mapManifestUrl")),
            "3D view state stays in user settings while the manifest URL moves");

    mainSettings.setValue(QStringLiteral("serial/epsilon_port"), QStringLiteral("COM9"));
    mainSettings.sync();
    VaporView::setSettingsWritesSuspended(true);
    VaporView::migrateLegacyApplicationConfig();
    require(mainSettings.contains(QStringLiteral("serial/epsilon_port")),
            "write barrier prevents migration while UI test mode is active");
    VaporView::setSettingsWritesSuspended(false);
    VaporView::migrateLegacyApplicationConfig();
    require(applicationSettings.value(QStringLiteral("MainWindow/serial/epsilon_port")).toString() ==
                QStringLiteral("COM9") &&
                !mainSettings.contains(QStringLiteral("serial/epsilon_port")),
            "later legacy writes are migrated without leaving duplicate state");

    std::cout << "application_config_test passed\n";
    return 0;
}
