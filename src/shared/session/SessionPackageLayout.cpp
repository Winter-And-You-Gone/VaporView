#include "shared/session/SessionPackageLayout.h"

#include "shared/session/UnifiedRawDat.h"

#include <QDir>

namespace VaporView::Session
{

const SessionPackageLayout& standardSessionPackageLayout()
{
    static const SessionPackageLayout layout{
        QStringLiteral("session.json"),
        QStringLiteral("sensors/devices.csv"),
        QStringLiteral("sensors/rd105_temperature_controller.csv"),
        QStringLiteral("sensors/waveform_features.csv"),
        QStringLiteral("raw/epsilon.dat"),
        QStringLiteral("raw/ptb.dat"),
        QStringLiteral("raw/hmp.dat"),
        QStringLiteral("raw/lidar.dat"),
        QStringLiteral("raw/tcp_wave.dat"),
        QStringLiteral("raw/tcp_wave_peaks.csv"),
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
        layout.devicesCsvPath,
        layout.temperatureControllerCsvPath,
        layout.waveformFeaturesCsvPath,
        layout.epsilonRawPath,
        layout.ptbRawPath,
        layout.hmpRawPath,
        layout.lidarRawPath,
        layout.tcpWaveRawPath,
        layout.tcpWavePeaksCsvPath,
        layout.eventLogPath,
        layout.errorLogPath,
        layout.deviceConfigPath
    };
}

QVector<RawFileDefinition> standardRawFileDefinitions()
{
    const SessionPackageLayout& layout = standardSessionPackageLayout();
    return {
        {QStringLiteral("epsilon"), layout.epsilonRawPath, SessionRawDat::kSourceEpsilon},
        {QStringLiteral("ptb"), layout.ptbRawPath, SessionRawDat::kSourcePtb},
        {QStringLiteral("hmp"), layout.hmpRawPath, SessionRawDat::kSourceHmp},
        {QStringLiteral("lidar"), layout.lidarRawPath, SessionRawDat::kSourceLidar},
        {QStringLiteral("tcp_wave"), layout.tcpWaveRawPath, SessionRawDat::kSourceTcpWave}
    };
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
        "- Standard sources: epsilon=1, ptb=2, hmp=3, lidar=4, tcp_wave=5\n\n"
        "Zero-record files still contain a valid file header for their source. "
        "The byte layout, source IDs, record types and flags are defined by "
        "VaporView::SessionRawDat and are shared by ground and sky recordings.\n");
}

}  // namespace VaporView::Session
