#include "shared/session/SessionPackageLayout.h"

#include "shared/session/UnifiedRawDat.h"

#include <QDir>

namespace VaporView::Session
{

const SessionPackageLayout& standardSessionPackageLayout()
{
    static const SessionPackageLayout layout{
        QStringLiteral("session.json"),
        QStringLiteral("sensors/sensor_summary.csv"),
        QStringLiteral("sensors/temperature_controller.csv"),
        QStringLiteral("sensors/waveform_features.csv"),
        QStringLiteral("raw/navigation.dat"),
        QStringLiteral("raw/pressure.dat"),
        QStringLiteral("raw/temperature_humidity.dat"),
        QStringLiteral("raw/distance.dat"),
        QStringLiteral("raw/waveform.dat"),
        QStringLiteral("raw/waveform_peaks.csv"),
        QStringLiteral("logs/event_log.csv"),
        QStringLiteral("logs/error_log.txt"),
        QStringLiteral("config/device_config.json"),
        QStringLiteral("raw_dat_format.md")
    };
    return layout;
}

QStringList standardSessionDirectories()
{
    return {
        QStringLiteral("sensors"),
        QStringLiteral("raw"),
        QStringLiteral("logs"),
        QStringLiteral("config")
    };
}

QStringList standardSessionFiles()
{
    const SessionPackageLayout& layout = standardSessionPackageLayout();
    return {
        layout.manifestPath,
        layout.rawFormatDocumentPath,
        layout.sensorSummaryCsvPath,
        layout.temperatureControllerCsvPath,
        layout.waveformFeaturesCsvPath,
        layout.navigationRawPath,
        layout.pressureRawPath,
        layout.temperatureHumidityRawPath,
        layout.distanceRawPath,
        layout.waveformRawPath,
        layout.waveformPeaksCsvPath,
        layout.eventLogPath,
        layout.errorLogPath,
        layout.deviceConfigPath
    };
}

QVector<RawFileDefinition> standardRawFileDefinitions()
{
    const SessionPackageLayout& layout = standardSessionPackageLayout();
    return {
        {QStringLiteral("navigation"), layout.navigationRawPath,
         SessionFileKind::NavigationRaw, SessionRawDat::kSourceEpsilon},
        {QStringLiteral("pressure"), layout.pressureRawPath,
         SessionFileKind::PressureRaw, SessionRawDat::kSourcePtb},
        {QStringLiteral("temperature_humidity"), layout.temperatureHumidityRawPath,
         SessionFileKind::TemperatureHumidityRaw, SessionRawDat::kSourceHmp},
        {QStringLiteral("distance"), layout.distanceRawPath,
         SessionFileKind::DistanceRaw, SessionRawDat::kSourceLidar},
        {QStringLiteral("waveform"), layout.waveformRawPath,
         SessionFileKind::WaveformRaw, SessionRawDat::kSourceTcpWave}
    };
}

const SessionPathAliases& sessionPathAliases(SessionFileKind kind)
{
    const SessionPackageLayout& layout = standardSessionPackageLayout();
    static const QVector<SessionPathAliases> aliases{
        {layout.sensorSummaryCsvPath,
         {QStringLiteral("sensors/devices.csv")},
         {QStringLiteral("sensor_summary_csv"), QStringLiteral("devices_csv")},
         {}},
        {layout.temperatureControllerCsvPath,
         {QStringLiteral("sensors/rd105_temperature_controller.csv")},
         {QStringLiteral("temperature_controller_csv")},
         {}},
        {layout.waveformFeaturesCsvPath,
         {},
         {QStringLiteral("waveform_features_csv")},
         {}},
        {layout.navigationRawPath,
         {QStringLiteral("raw/epsilon.dat")},
         {QStringLiteral("navigation_raw"), QStringLiteral("epsilon_raw")},
         {QStringLiteral("navigation"), QStringLiteral("epsilon")}},
        {layout.pressureRawPath,
         {QStringLiteral("raw/ptb.dat")},
         {QStringLiteral("pressure_raw"), QStringLiteral("ptb_raw")},
         {QStringLiteral("pressure"), QStringLiteral("ptb")}},
        {layout.temperatureHumidityRawPath,
         {QStringLiteral("raw/hmp.dat")},
         {QStringLiteral("temperature_humidity_raw"), QStringLiteral("hmp_raw")},
         {QStringLiteral("temperature_humidity"), QStringLiteral("hmp")}},
        {layout.distanceRawPath,
         {QStringLiteral("raw/lidar.dat")},
         {QStringLiteral("distance_raw"), QStringLiteral("lidar_raw")},
         {QStringLiteral("distance"), QStringLiteral("lidar")}},
        {layout.waveformRawPath,
         {QStringLiteral("raw/tcp_wave.dat")},
         {QStringLiteral("waveform_raw"), QStringLiteral("tcp_wave_raw")},
         {QStringLiteral("waveform"), QStringLiteral("tcp_wave")}},
        {layout.waveformPeaksCsvPath,
         {QStringLiteral("raw/tcp_wave_peaks.csv")},
         {QStringLiteral("waveform_peaks_csv"),
          QStringLiteral("tcp_wave_peaks_csv"),
          QStringLiteral("waveform_peak_index")},
         {}}
    };
    return aliases.at(static_cast<int>(kind));
}

QString sessionPackageFilePath(const QString& sessionDirectory, const QString& relativePath)
{
    return QDir::fromNativeSeparators(QDir(sessionDirectory).filePath(relativePath));
}

QString waveformFeaturesCsvHeader()
{
    return QStringLiteral(
        "host_time_us,epsilon_time_us,original_point_count,search_start_index,search_end_index,"
        "channel_id,peak,mean,rms,peak_index,peak_x,min_value,max_value,quality_flags\n");
}

QString temperatureControllerCsvHeader()
{
    return QStringLiteral(
        "host_time_us,valid,internal_temperature_c,error_code,"
        "ch1_target_c,ch1_measured_c,ch1_output_percent,ch1_output_current_a,ch1_enabled,"
        "ch1_mode,ch1_max_output_percent,ch1_kp,ch1_ki,ch1_kd,"
        "ch2_target_c,ch2_measured_c,ch2_output_percent,ch2_output_current_a,ch2_enabled,"
        "ch2_mode,ch2_max_output_percent,ch2_kp,ch2_ki,ch2_kd\n");
}

QString tcpWavePeaksCsvHeader()
{
    return QStringLiteral("host_time_us,peak_value,peak_index,point_count,search_start_index,search_end_index\n");
}

QString eventLogCsvHeader()
{
    return QStringLiteral("timestamp_utc,timestamp_us,level,message\n");
}

QString rawDatFormatDocumentText()
{
    return QStringLiteral(
        "# VaporView RAW DAT Format\n\n"
        "This session package uses the built-in VaporView unified RAW DAT format.\n\n"
        "- Format version: 2\n"
        "- File magic: VVRAWDAT\n"
        "- File header size: 20 bytes\n"
        "- Record header size: 36 bytes\n"
        "- Timestamp unit: microseconds\n"
        "- Standard sources: navigation=1, pressure=2, temperature_humidity=3, distance=4, waveform=5\n"
        "  (the historical source ID values are unchanged)\n\n"
        "Zero-record files still contain a valid file header for their source. "
        "The byte layout, source IDs, record types and flags are defined by "
        "VaporView::SessionRawDat and are shared by ground and sky recordings.\n");
}

}  // namespace VaporView::Session
