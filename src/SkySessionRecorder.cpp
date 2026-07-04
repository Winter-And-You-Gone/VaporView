#include "SkySessionRecorder.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStringList>
#include <QTextStream>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace VaporView
{
namespace
{
constexpr char kUnifiedRawMagic[8] = {'V', 'V', 'R', 'A', 'W', 'D', 'A', 'T'};
constexpr quint32 kUnifiedRawFormatVersion = 2u;
constexpr quint32 kUnifiedRawRecordMarker = 0x44525756u;
constexpr quint16 kRawSourceEpsilon = 1u;
constexpr quint16 kRawSourcePtb = 2u;
constexpr quint16 kRawSourceHmp = 3u;
constexpr quint16 kRawSourceLidar = 4u;
constexpr quint16 kRawSourceTcpWave = 5u;
constexpr quint16 kRawRecordTypeGeneric = 1u;
constexpr quint32 kRawTcpWaveCombinedPayloadFlag = 0x00000001u;
constexpr const char *kTcpWavePeakIndexCsvHeader =
    "host_time_us,peak_value,peak_index,point_count,search_start_index,search_end_index\n";
constexpr const char *kDevicesCsvHeader =
    "record_timestamp_us,"
    "epsilon_host_timestamp_us,epsilon_device_timestamp_us,epsilon_utc_unix_s,epsilon_utc_microseconds,"
    "nav_lat_deg,nav_lon_deg,nav_height_m,"
    "ecef_x_m,ecef_y_m,ecef_z_m,"
    "ned_n_m,ned_e_m,ned_d_m,"
    "vel_n_mps,vel_e_mps,vel_d_mps,"
    "body_vel_x_mps,body_vel_y_mps,body_vel_z_mps,"
    "body_acc_x_mps2,body_acc_y_mps2,body_acc_z_mps2,"
    "roll_deg,pitch_deg,yaw_deg,"
    "quat_w,quat_x,quat_y,quat_z,"
    "ang_vel_x_radps,ang_vel_y_radps,ang_vel_z_radps,"
    "imu_acc_x_mps2,imu_acc_y_mps2,imu_acc_z_mps2,"
    "imu_gyr_x_radps,imu_gyr_y_radps,imu_gyr_z_radps,"
    "mag_x_mg,mag_y_mg,mag_z_mg,"
    "gnss_fix,gnss_satellites,hdop,vdop,hacc_m,vacc_m,"
    "lat_std_m,lon_std_m,height_std_m,diff_age_s,"
    "heading_valid,system_status_bits,filter_status_bits,update_status_bits,"
    "epsilon_imu_packet_rate_hz,epsilon_ahrs_packet_rate_hz,"
    "epsilon_insgps_packet_rate_hz,epsilon_sys_state_packet_rate_hz,"
    "epsilon_raw_gnss_packet_rate_hz,epsilon_satellite_packet_rate_hz,"
    "epsilon_geodetic_packet_rate_hz,epsilon_ecef_packet_rate_hz,"
    "epsilon_valid,epsilon_error_message,"
    "hmp_temperature_c,hmp_humidity_rh,ptb_pressure_hpa,lidar_distance_m,lidar_signal_strength,lidar_valid\n";

#pragma pack(push, 1)
struct UnifiedRawFileHeader
{
    char magic[8];
    quint32 version;
    quint32 header_size;
    quint16 source_id;
    quint16 reserved;
};

struct UnifiedRawRecordHeader
{
    quint32 marker;
    quint32 header_size;
    quint64 host_timestamp_us;
    quint32 payload_size;
    quint16 source_id;
    quint16 record_type;
    quint32 flags;
    quint64 sequence;
};
#pragma pack(pop)

quint64 nowUs()
{
    return static_cast<quint64>(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) * 1000ULL;
}

QString timestampForSessionName()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
}

bool writeJsonFileAtomically(const QString& filename, const QJsonObject& object, QString *errorMessage)
{
    QSaveFile file(filename);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }

    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size())
    {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }

    if (!file.commit())
    {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }

    return true;
}

QString boolText(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

QString csvEscape(const QString& value)
{
    QString escaped = value;
    escaped.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    if (escaped.contains(',') || escaped.contains('"') || escaped.contains('\n') || escaped.contains('\r'))
    {
        escaped = QStringLiteral("\"%1\"").arg(escaped);
    }
    return escaped;
}

QByteArray encodeLittleEndianFloatPayload(const QVector<float>& samples)
{
    if (samples.size() > std::numeric_limits<int>::max() / static_cast<int>(sizeof(float)))
    {
        return QByteArray();
    }

    QByteArray payload;
    payload.resize(samples.size() * static_cast<int>(sizeof(float)));
    char *cursor = payload.data();
    for (float sample : samples)
    {
        quint32 bits = 0;
        std::memcpy(&bits, &sample, sizeof(bits));
        const quint32 littleEndianBits = qToLittleEndian(bits);
        std::memcpy(cursor, &littleEndianBits, sizeof(littleEndianBits));
        cursor += sizeof(littleEndianBits);
    }
    return payload;
}

struct TcpWavePeakSummary
{
    float value = std::numeric_limits<float>::quiet_NaN();
    int index = -1;
    quint32 point_count = 0;
};

TcpWavePeakSummary summarizeTcpWavePeakSamples(const QByteArray& payload, TcpFloatEncoding encoding)
{
    TcpWavePeakSummary summary;
    if (payload.isEmpty() || payload.size() % static_cast<int>(sizeof(float)) != 0)
    {
        return summary;
    }

    const int sampleCount = payload.size() / static_cast<int>(sizeof(float));
    summary.point_count = static_cast<quint32>(sampleCount);
    const TcpFloatEncoding effectiveEncoding = encoding == TcpFloatEncoding::Unknown
        ? autoDetectTcpFloatEncoding(payload)
        : encoding;

    bool hasPeak = false;
    float peakValue = std::numeric_limits<float>::lowest();
    int peakIndex = -1;
    const char *samples = payload.constData();
    for (int index = 0; index < sampleCount; ++index)
    {
        const float value = decodeTcpFloatSample(samples + index * static_cast<int>(sizeof(float)), effectiveEncoding);
        if (!std::isfinite(value))
        {
            continue;
        }
        if (!hasPeak || value > peakValue)
        {
            hasPeak = true;
            peakValue = value;
            peakIndex = index;
        }
    }

    if (hasPeak)
    {
        summary.value = peakValue;
        summary.index = peakIndex;
    }
    return summary;
}

QString peakValueCsvText(float value)
{
    return std::isfinite(value)
        ? QString::number(static_cast<double>(value), 'g', 9)
        : QString();
}

}  // namespace

bool SkySessionRecorder::start(const QString& baseDirectory,
                               const QString& telemetryPort,
                               int telemetryBaud,
                               QString *errorMessage)
{
    return start(baseDirectory,
                 telemetryPort,
                 telemetryBaud,
                 errorMessage,
                 QStringLiteral("serial"),
                 telemetryPort);
}

bool SkySessionRecorder::start(const QString& baseDirectory,
                               const QString& telemetryPort,
                               int telemetryBaud,
                               QString *errorMessage,
                               const QString& telemetryTransport,
                               const QString& telemetryEndpoint)
{
    if (recording_state_ == 2 && !session_directory_.isEmpty())
    {
        const quint64 now = nowUs();
        recording_start_time_us_ = now >= recording_elapsed_ms_ * 1000ULL
            ? now - recording_elapsed_ms_ * 1000ULL
            : now;
        recording_state_ = 1;
        return true;
    }

    closeFiles();

    const QString baseSessionName = QStringLiteral("session_%1").arg(timestampForSessionName());

    QDir recordsDir(baseDirectory);
    if (!recordsDir.exists() && !recordsDir.mkpath(QStringLiteral(".")))
    {
        if (errorMessage) *errorMessage = QStringLiteral("cannot create recording directory");
        return false;
    }

    QString finalSessionName = baseSessionName;
    QString finalSessionDirectory = recordsDir.filePath(finalSessionName);
    int suffix = 1;
    while (QFileInfo::exists(finalSessionDirectory))
    {
        finalSessionName = QStringLiteral("%1_%2").arg(baseSessionName).arg(suffix++);
        finalSessionDirectory = recordsDir.filePath(finalSessionName);
    }

    session_name_ = finalSessionName;
    if (!recordsDir.mkpath(session_name_))
    {
        if (errorMessage) *errorMessage = QStringLiteral("cannot create session directory");
        return false;
    }

    session_directory_ = finalSessionDirectory;
    QDir sessionDir(session_directory_);
    if (!sessionDir.mkpath(QStringLiteral("raw")) ||
        !sessionDir.mkpath(QStringLiteral("sensors")) ||
        !sessionDir.mkpath(QStringLiteral("logs")) ||
        !sessionDir.mkpath(QStringLiteral("config")))
    {
        if (errorMessage) *errorMessage = QStringLiteral("cannot create session subdirectories");
        return false;
    }

    session_metadata_filename_ = sessionDir.filePath(QStringLiteral("session.json"));
    sensors_filename_ = sessionDir.filePath(QStringLiteral("sensors/devices.csv"));
    feature_filename_ = sessionDir.filePath(QStringLiteral("waveform_features.csv"));
    temperature_controller_filename_ = sessionDir.filePath(QStringLiteral("sensors/rd105_temperature_controller.csv"));
    basic_record_file_.setFileName(sensors_filename_);
    feature_record_file_.setFileName(feature_filename_);
    temperature_controller_record_file_.setFileName(temperature_controller_filename_);
    raw_epsilon_filename_ = sessionDir.filePath(QStringLiteral("raw/epsilon.dat"));
    raw_ptb_filename_ = sessionDir.filePath(QStringLiteral("raw/ptb.dat"));
    raw_hmp_filename_ = sessionDir.filePath(QStringLiteral("raw/hmp.dat"));
    raw_lidar_filename_ = sessionDir.filePath(QStringLiteral("raw/lidar.dat"));
    raw_tcp_wave_filename_ = sessionDir.filePath(QStringLiteral("raw/tcp_wave.dat"));
    raw_tcp_wave_peak_index_filename_ = sessionDir.filePath(QStringLiteral("raw/tcp_wave_peaks.csv"));
    raw_tcp_wave_peak_index_file_.setFileName(raw_tcp_wave_peak_index_filename_);

    if (!basic_record_file_.open(QIODevice::WriteOnly | QIODevice::Text) ||
        !feature_record_file_.open(QIODevice::WriteOnly | QIODevice::Text) ||
        !temperature_controller_record_file_.open(QIODevice::WriteOnly | QIODevice::Text) ||
        !raw_tcp_wave_peak_index_file_.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
        !openRawDatFile(raw_epsilon_file_, raw_epsilon_filename_, kRawSourceEpsilon, errorMessage) ||
        !openRawDatFile(raw_ptb_file_, raw_ptb_filename_, kRawSourcePtb, errorMessage) ||
        !openRawDatFile(raw_hmp_file_, raw_hmp_filename_, kRawSourceHmp, errorMessage) ||
        !openRawDatFile(raw_lidar_file_, raw_lidar_filename_, kRawSourceLidar, errorMessage) ||
        !openRawDatFile(raw_tcp_wave_file_, raw_tcp_wave_filename_, kRawSourceTcpWave, errorMessage))
    {
        if (errorMessage && errorMessage->isEmpty()) *errorMessage = QStringLiteral("cannot open session files");
        closeFiles();
        return false;
    }

    QTextStream basicOut(&basic_record_file_);
    basicOut << kDevicesCsvHeader;

    QTextStream featureOut(&feature_record_file_);
    featureOut << "host_time_us,epsilon_time_us,original_point_count,search_start_index,search_end_index,channel_id,peak,mean,rms,peak_index,peak_x,min_value,max_value,quality_flags\n";

    QTextStream temperatureOut(&temperature_controller_record_file_);
    temperatureOut << "host_time_us,valid,internal_temperature_c,error_code,"
                      "ch1_target_c,ch1_measured_c,ch1_output_percent,ch1_output_current_a,ch1_enabled,ch1_mode,ch1_max_output_percent,ch1_kp,ch1_ki,ch1_kd,"
                      "ch2_target_c,ch2_measured_c,ch2_output_percent,ch2_output_current_a,ch2_enabled,ch2_mode,ch2_max_output_percent,ch2_kp,ch2_ki,ch2_kd\n";

    QTextStream peakIndexOut(&raw_tcp_wave_peak_index_file_);
    peakIndexOut << kTcpWavePeakIndexCsvHeader;
    peakIndexOut.flush();

    session_start_time_utc_ = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    telemetry_port_ = telemetryPort;
    telemetry_baud_ = telemetryBaud;
    telemetry_transport_ = telemetryTransport.trimmed().isEmpty() ? QStringLiteral("serial") : telemetryTransport.trimmed();
    telemetry_endpoint_ = telemetryEndpoint.trimmed().isEmpty() ? telemetryPort : telemetryEndpoint.trimmed();
    recording_start_time_us_ = nowUs();
    recording_elapsed_ms_ = 0;
    telemetry_row_count_ = 0;
    waveform_feature_count_ = 0;
    temperature_controller_count_ = 0;
    waveform_file_count_ = 0;
    waveform_points_per_frame_ = 0;
    raw_epsilon_record_count_ = 0;
    raw_ptb_record_count_ = 0;
    raw_hmp_record_count_ = 0;
    raw_lidar_record_count_ = 0;
    raw_tcp_wave_record_count_ = 0;
    native_raw_tcp_wave_record_count_ = 0;
    recording_state_ = 1;
    if (!writeSessionMetadata(QString(), errorMessage))
    {
        recording_state_ = 0;
        closeFiles();
        return false;
    }
    return true;
}

void SkySessionRecorder::pause()
{
    if (recording_state_ == 1)
    {
        recording_elapsed_ms_ = recordingElapsedMs();
        recording_state_ = 2;
    }
    else if (recording_state_ != 0)
    {
        recording_state_ = 2;
    }
}

bool SkySessionRecorder::stop(QString *errorMessage)
{
    recording_elapsed_ms_ = recordingElapsedMs();
    const bool metadataWritten = writeSessionMetadata(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
                                                      errorMessage);
    closeFiles();
    recording_state_ = 0;
    return metadataWritten;
}

bool SkySessionRecorder::isRecording() const
{
    return recording_state_ == 1;
}

bool SkySessionRecorder::isPaused() const
{
    return recording_state_ == 2;
}

quint8 SkySessionRecorder::recordingState() const
{
    return recording_state_;
}

QString SkySessionRecorder::sessionName() const
{
    return session_name_;
}

QString SkySessionRecorder::sessionDirectory() const
{
    return session_directory_;
}

quint64 SkySessionRecorder::recordingStartTimeUs() const
{
    return recording_start_time_us_;
}

quint64 SkySessionRecorder::recordingElapsedMs() const
{
    if (recording_state_ != 1 || recording_start_time_us_ == 0)
    {
        return recording_elapsed_ms_;
    }
    const quint64 now = nowUs();
    return now >= recording_start_time_us_ ? (now - recording_start_time_us_) / 1000ULL : 0;
}

quint64 SkySessionRecorder::telemetryRecordCount() const
{
    return telemetry_row_count_;
}

quint64 SkySessionRecorder::waveformFeatureRecordCount() const
{
    return waveform_feature_count_;
}

quint64 SkySessionRecorder::temperatureControllerRecordCount() const
{
    return temperature_controller_count_;
}

quint64 SkySessionRecorder::waveformSnapshotRecordCount() const
{
    return raw_tcp_wave_record_count_;
}

quint64 SkySessionRecorder::rawEpsilonRecordCount() const
{
    return raw_epsilon_record_count_;
}

quint64 SkySessionRecorder::rawPtbRecordCount() const
{
    return raw_ptb_record_count_;
}

quint64 SkySessionRecorder::rawHmpRecordCount() const
{
    return raw_hmp_record_count_;
}

quint64 SkySessionRecorder::rawLidarRecordCount() const
{
    return raw_lidar_record_count_;
}

quint64 SkySessionRecorder::rawTcpWaveRecordCount() const
{
    return raw_tcp_wave_record_count_;
}

void SkySessionRecorder::recordBasicTelemetry(const TelemetryBasic& data)
{
    const bool hasEpsilon = (data.validity_flags & (BasicHasEpsilonTime | BasicHasPosition | BasicHasEcef)) != 0;
    const bool hasLidar = (data.validity_flags & BasicHasLidar) != 0;
    const bool hasTemperatureHumidity =
        (data.validity_flags & BasicHasTemperature) != 0 &&
        (data.validity_flags & BasicHasHumidity) != 0;
    const bool hasPressure = (data.validity_flags & BasicHasPressure) != 0;

    EpsilonData epsilon;
    epsilon.valid = hasEpsilon;
    epsilon.device_timestamp_us = data.epsilon_time_us;
    epsilon.utc_unix_s = data.epsilon_time_us / 1'000'000ULL;
    epsilon.utc_microseconds = static_cast<quint32>(data.epsilon_time_us % 1'000'000ULL);
    epsilon.latitude_deg = data.latitude_deg;
    epsilon.longitude_deg = data.longitude_deg;
    epsilon.height_m = data.height_m;
    epsilon.ecef_x_m = data.ecef_x_m;
    epsilon.ecef_y_m = data.ecef_y_m;
    epsilon.ecef_z_m = data.ecef_z_m;
    epsilon.system_status_bits = data.status_bits;
    epsilon.filter_status_bits = data.filter_status_bits;
    epsilon.update_status_bits = data.update_status_bits;
    epsilon.gnss_fix_code = data.gnss_fix_code;
    epsilon.gnss_fix_text = std::to_string(data.gnss_fix_code);

    PtbData ptb;
    ptb.valid = hasPressure;
    ptb.pressure_hpa = data.pressure_hpa;

    HmpData hmp;
    hmp.valid = hasTemperatureHumidity;
    hmp.temperature = data.temperature_c;
    hmp.humidity = data.humidity_percent;

    LidarData lidar;
    lidar.valid = hasLidar;
    lidar.distance_m = data.lidar_height_m;

    recordDeviceSnapshot(data.host_time_us,
                         data.host_time_us,
                         epsilon,
                         hasEpsilon,
                         ptb,
                         hasPressure,
                         hmp,
                         hasTemperatureHumidity,
                         lidar,
                         hasLidar);
}

void SkySessionRecorder::recordDeviceSnapshot(quint64 hostTimeUs,
                                              quint64 epsilonHostTimeUs,
                                              const EpsilonData& epsilon,
                                              bool hasEpsilon,
                                              const PtbData& ptb,
                                              bool hasPtb,
                                              const HmpData& hmp,
                                              bool hasHmp,
                                              const LidarData& lidar,
                                              bool hasLidar)
{
    if (!isRecording() || !basic_record_file_.isOpen())
    {
        return;
    }

    QStringList row;
    row.reserve(64);
    row << QString::number(hostTimeUs);

    auto appendEmptyColumns = [&row](int count) {
        for (int i = 0; i < count; ++i)
        {
            row << QString();
        }
    };

    if (hasEpsilon && epsilon.valid)
    {
        row << QString::number(epsilonHostTimeUs)
            << QString::number(epsilon.device_timestamp_us)
            << QString::number(epsilon.utc_unix_s)
            << QString::number(epsilon.utc_microseconds)
            << QString::number(epsilon.latitude_deg, 'f', 9)
            << QString::number(epsilon.longitude_deg, 'f', 9)
            << QString::number(epsilon.height_m, 'f', 6)
            << QString::number(epsilon.ecef_x_m, 'f', 6)
            << QString::number(epsilon.ecef_y_m, 'f', 6)
            << QString::number(epsilon.ecef_z_m, 'f', 6)
            << QString::number(epsilon.ned_n_m, 'f', 6)
            << QString::number(epsilon.ned_e_m, 'f', 6)
            << QString::number(epsilon.ned_d_m, 'f', 6)
            << QString::number(epsilon.vel_n_mps, 'f', 6)
            << QString::number(epsilon.vel_e_mps, 'f', 6)
            << QString::number(epsilon.vel_d_mps, 'f', 6)
            << QString::number(epsilon.body_vel_x_mps, 'f', 6)
            << QString::number(epsilon.body_vel_y_mps, 'f', 6)
            << QString::number(epsilon.body_vel_z_mps, 'f', 6)
            << QString::number(epsilon.body_acc_x_mps2, 'f', 6)
            << QString::number(epsilon.body_acc_y_mps2, 'f', 6)
            << QString::number(epsilon.body_acc_z_mps2, 'f', 6)
            << QString::number(epsilon.roll_deg, 'f', 6)
            << QString::number(epsilon.pitch_deg, 'f', 6)
            << QString::number(epsilon.yaw_deg, 'f', 6)
            << QString::number(epsilon.quat_w, 'f', 8)
            << QString::number(epsilon.quat_x, 'f', 8)
            << QString::number(epsilon.quat_y, 'f', 8)
            << QString::number(epsilon.quat_z, 'f', 8)
            << QString::number(epsilon.ang_vel_x_radps, 'f', 8)
            << QString::number(epsilon.ang_vel_y_radps, 'f', 8)
            << QString::number(epsilon.ang_vel_z_radps, 'f', 8)
            << QString::number(epsilon.imu_acc_x_mps2, 'f', 6)
            << QString::number(epsilon.imu_acc_y_mps2, 'f', 6)
            << QString::number(epsilon.imu_acc_z_mps2, 'f', 6)
            << QString::number(epsilon.imu_gyr_x_radps, 'f', 8)
            << QString::number(epsilon.imu_gyr_y_radps, 'f', 8)
            << QString::number(epsilon.imu_gyr_z_radps, 'f', 8)
            << QString::number(epsilon.mag_x_mg, 'f', 6)
            << QString::number(epsilon.mag_y_mg, 'f', 6)
            << QString::number(epsilon.mag_z_mg, 'f', 6)
            << QString::fromStdString(epsilon.gnss_fix_text)
            << QString::number(epsilon.gnss_satellites)
            << QString::number(epsilon.hdop, 'f', 4)
            << QString::number(epsilon.vdop, 'f', 4)
            << QString::number(epsilon.hacc_m, 'f', 4)
            << QString::number(epsilon.vacc_m, 'f', 4)
            << QString::number(epsilon.lat_std_m, 'f', 4)
            << QString::number(epsilon.lon_std_m, 'f', 4)
            << QString::number(epsilon.height_std_m, 'f', 4)
            << (std::isfinite(epsilon.diff_age_s) ? QString::number(epsilon.diff_age_s, 'f', 4) : QString())
            << boolText(epsilon.heading_valid)
            << QString::number(epsilon.system_status_bits)
            << QString::number(epsilon.filter_status_bits)
            << QString::number(epsilon.update_status_bits)
            << QString::number(epsilon.imu_packet_rate_hz, 'f', 4)
            << QString::number(epsilon.ahrs_packet_rate_hz, 'f', 4)
            << QString::number(epsilon.insgps_packet_rate_hz, 'f', 4)
            << QString::number(epsilon.sys_state_packet_rate_hz, 'f', 4)
            << QString::number(epsilon.raw_gnss_packet_rate_hz, 'f', 4)
            << QString::number(epsilon.satellite_packet_rate_hz, 'f', 4)
            << QString::number(epsilon.geodetic_packet_rate_hz, 'f', 4)
            << QString::number(epsilon.ecef_packet_rate_hz, 'f', 4)
            << boolText(true)
            << QString::fromStdString(epsilon.error_message);
    }
    else
    {
        appendEmptyColumns(65);
    }

    if (hasHmp && hmp.valid)
    {
        row << QString::number(hmp.temperature, 'f', 6)
            << QString::number(hmp.humidity, 'f', 6);
    }
    else
    {
        appendEmptyColumns(2);
    }

    if (hasPtb && ptb.valid)
    {
        row << QString::number(ptb.pressure_hpa, 'f', 6);
    }
    else
    {
        appendEmptyColumns(1);
    }

    if (hasLidar && lidar.valid)
    {
        row << QString::number(lidar.distance_m, 'f', 6)
            << QString::number(lidar.signal_strength)
            << boolText(lidar.valid);
    }
    else
    {
        appendEmptyColumns(3);
    }

    QTextStream out(&basic_record_file_);
    for (int i = 0; i < row.size(); ++i)
    {
        if (i > 0)
        {
            out << ',';
        }
        out << csvEscape(row.at(i));
    }
    out << '\n';
    ++telemetry_row_count_;
}

void SkySessionRecorder::recordWaveformFeature(const WaveformFeature& feature)
{
    if (!isRecording() || !feature_record_file_.isOpen())
    {
        return;
    }
    QTextStream out(&feature_record_file_);
    out << feature.host_time_us << ','
        << feature.epsilon_time_us << ','
        << feature.original_point_count << ','
        << feature.search_start_index << ','
        << feature.search_end_index << ','
        << feature.channel_id << ','
        << QString::number(feature.peak, 'f', 6) << ','
        << QString::number(feature.mean, 'f', 6) << ','
        << QString::number(feature.rms, 'f', 6) << ','
        << QString::number(feature.peak_index, 'f', 6) << ','
        << QString::number(feature.peak_x, 'f', 6) << ','
        << QString::number(feature.min_value, 'f', 6) << ','
        << QString::number(feature.max_value, 'f', 6) << ','
        << feature.quality_flags << '\n';
    ++waveform_feature_count_;
}

void SkySessionRecorder::recordTemperatureControllerStatus(quint64 hostTimeUs, const TemperatureControllerData& data)
{
    if (!isRecording() || !temperature_controller_record_file_.isOpen())
    {
        return;
    }

    QStringList row;
    row.reserve(24);
    row << QString::number(hostTimeUs)
        << boolText(data.valid)
        << (std::isfinite(data.internal_temperature_c) ? QString::number(data.internal_temperature_c, 'f', 6) : QString())
        << QString::number(data.error_code);
    for (const TemperatureControllerChannelData& channel : data.channels)
    {
        row << (std::isfinite(channel.target_temperature_c) ? QString::number(channel.target_temperature_c, 'f', 6) : QString())
            << (std::isfinite(channel.measured_temperature_c) ? QString::number(channel.measured_temperature_c, 'f', 6) : QString())
            << (std::isfinite(channel.output_percent) ? QString::number(channel.output_percent, 'f', 6) : QString())
            << (std::isfinite(channel.output_current_a) ? QString::number(channel.output_current_a, 'f', 6) : QString())
            << boolText(channel.output_enabled)
            << QString::number(channel.output_mode)
            << QString::number(channel.max_output_percent)
            << QString::number(channel.kp)
            << QString::number(channel.ki)
            << QString::number(channel.kd);
    }

    QTextStream out(&temperature_controller_record_file_);
    for (int i = 0; i < row.size(); ++i)
    {
        if (i > 0)
        {
            out << ',';
        }
        out << csvEscape(row.at(i));
    }
    out << '\n';
    ++temperature_controller_count_;
}

void SkySessionRecorder::recordWaveformSnapshot(quint64 hostTimeUs,
                                                quint64 epsilonTimeUs,
                                                const QVector<float>& rawSamples,
                                                const QVector<float>& harmonicSamples)
{
    Q_UNUSED(epsilonTimeUs);
    if (!isRecording() || (rawSamples.isEmpty() && harmonicSamples.isEmpty()) || native_raw_tcp_wave_record_count_ > 0)
    {
        return;
    }

    const QByteArray rawPayload = encodeLittleEndianFloatPayload(rawSamples);
    const QByteArray harmonicPayload = encodeLittleEndianFloatPayload(harmonicSamples);
    if ((!rawSamples.isEmpty() && rawPayload.isEmpty()) ||
        (!harmonicSamples.isEmpty() && harmonicPayload.isEmpty()))
    {
        return;
    }

    writeRawTcpWavePayload(hostTimeUs,
                           rawPayload,
                           harmonicPayload,
                           TcpFloatEncoding::LittleEndian);
}

void SkySessionRecorder::recordRawEpsilonFrame(quint64 hostTimeUs,
                                               quint8 packetId,
                                               quint8 serialNumber,
                                               const QByteArray& frame)
{
    writeRawRecord(raw_epsilon_file_,
                   raw_epsilon_record_count_,
                   kRawSourceEpsilon,
                   packetId,
                   serialNumber,
                   hostTimeUs,
                   frame.constData(),
                   frame.size());
}

void SkySessionRecorder::recordRawPtbResponse(quint64 hostTimeUs, const QByteArray& response)
{
    writeRawRecord(raw_ptb_file_,
                   raw_ptb_record_count_,
                   kRawSourcePtb,
                   kRawRecordTypeGeneric,
                   0u,
                   hostTimeUs,
                   response.constData(),
                   response.size());
}

void SkySessionRecorder::recordRawHmpResponse(quint64 hostTimeUs, const QByteArray& response)
{
    writeRawRecord(raw_hmp_file_,
                   raw_hmp_record_count_,
                   kRawSourceHmp,
                   0x03u,
                   0u,
                   hostTimeUs,
                   response.constData(),
                   response.size());
}

void SkySessionRecorder::recordRawLidarFrame(quint64 hostTimeUs, quint16 protocol, const QByteArray& frame)
{
    writeRawRecord(raw_lidar_file_,
                   raw_lidar_record_count_,
                   kRawSourceLidar,
                   protocol,
                   0u,
                   hostTimeUs,
                   frame.constData(),
                   frame.size());
}

void SkySessionRecorder::recordRawTcpWaveFrame(quint64 hostTimeUs,
                                               const QByteArray& rawPayload,
                                               const QByteArray& harmonicPayload,
                                               TcpFloatEncoding floatEncoding)
{
    if (writeRawTcpWavePayload(hostTimeUs, rawPayload, harmonicPayload, floatEncoding))
    {
        ++native_raw_tcp_wave_record_count_;
    }
}

bool SkySessionRecorder::openRawDatFile(QFile& file, const QString& filename, quint16 sourceId, QString *errorMessage)
{
    file.setFileName(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (errorMessage) *errorMessage = QStringLiteral("cannot open raw DAT file: %1").arg(filename);
        return false;
    }

    UnifiedRawFileHeader header{};
    std::memcpy(header.magic, kUnifiedRawMagic, sizeof(header.magic));
    header.version = qToLittleEndian(kUnifiedRawFormatVersion);
    header.header_size = qToLittleEndian(static_cast<quint32>(sizeof(UnifiedRawFileHeader)));
    header.source_id = qToLittleEndian(sourceId);
    header.reserved = 0;

    if (file.write(reinterpret_cast<const char*>(&header), sizeof(header)) != static_cast<qint64>(sizeof(header)))
    {
        if (errorMessage) *errorMessage = QStringLiteral("cannot write raw DAT header: %1").arg(filename);
        file.close();
        return false;
    }
    file.flush();
    return true;
}

bool SkySessionRecorder::writeRawRecord(QFile& file,
                                        quint64& recordCount,
                                        quint16 sourceId,
                                        quint16 recordType,
                                        quint32 flags,
                                        quint64 hostTimeUs,
                                        const void *payload,
                                        qsizetype payloadSize)
{
    if (!isRecording() || !file.isOpen() || (payloadSize > 0 && !payload) ||
        payloadSize > static_cast<qsizetype>(std::numeric_limits<quint32>::max()))
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(files_mutex_);
    if (!file.isOpen())
    {
        return false;
    }

    const quint64 sequence = recordCount;
    UnifiedRawRecordHeader header{};
    header.marker = qToLittleEndian(kUnifiedRawRecordMarker);
    header.header_size = qToLittleEndian(static_cast<quint32>(sizeof(UnifiedRawRecordHeader)));
    header.host_timestamp_us = qToLittleEndian(hostTimeUs);
    header.payload_size = qToLittleEndian(static_cast<quint32>(payloadSize));
    header.source_id = qToLittleEndian(sourceId);
    header.record_type = qToLittleEndian(recordType);
    header.flags = qToLittleEndian(flags);
    header.sequence = qToLittleEndian(sequence);

    if (file.write(reinterpret_cast<const char*>(&header), sizeof(header)) != static_cast<qint64>(sizeof(header)))
    {
        return false;
    }
    if (payloadSize > 0 &&
        file.write(reinterpret_cast<const char*>(payload), payloadSize) != payloadSize)
    {
        return false;
    }
    ++recordCount;
    return true;
}

bool SkySessionRecorder::writeRawTcpWavePayload(quint64 hostTimeUs,
                                                const QByteArray& rawPayload,
                                                const QByteArray& harmonicPayload,
                                                TcpFloatEncoding floatEncoding)
{
    if (!isRecording() ||
        static_cast<quint64>(rawPayload.size()) > std::numeric_limits<quint32>::max() ||
        static_cast<quint64>(harmonicPayload.size()) > std::numeric_limits<quint32>::max())
    {
        return false;
    }

    QByteArray payload;
    payload.resize(static_cast<int>(sizeof(quint32) * 2 + rawPayload.size() + harmonicPayload.size()));
    char *cursor = payload.data();
    const quint32 rawSize = qToLittleEndian(static_cast<quint32>(rawPayload.size()));
    const quint32 harmonicSize = qToLittleEndian(static_cast<quint32>(harmonicPayload.size()));
    std::memcpy(cursor, &rawSize, sizeof(rawSize));
    cursor += sizeof(rawSize);
    std::memcpy(cursor, &harmonicSize, sizeof(harmonicSize));
    cursor += sizeof(harmonicSize);
    if (!rawPayload.isEmpty())
    {
        std::memcpy(cursor, rawPayload.constData(), rawPayload.size());
        cursor += rawPayload.size();
    }
    if (!harmonicPayload.isEmpty())
    {
        std::memcpy(cursor, harmonicPayload.constData(), harmonicPayload.size());
    }

    if (!writeRawRecord(raw_tcp_wave_file_,
                        raw_tcp_wave_record_count_,
                        kRawSourceTcpWave,
                        kRawRecordTypeGeneric,
                        kRawTcpWaveCombinedPayloadFlag | tcpFloatEncodingToRawDatFlags(floatEncoding),
                        hostTimeUs,
                        payload.constData(),
                        payload.size()))
    {
        return false;
    }

    appendTcpWavePeakIndexLine(hostTimeUs, harmonicPayload, floatEncoding);
    waveform_file_count_ = 1;
    if (harmonicPayload.size() > 0 && harmonicPayload.size() % static_cast<int>(sizeof(float)) == 0)
    {
        waveform_points_per_frame_ = static_cast<quint64>(harmonicPayload.size() / static_cast<int>(sizeof(float)));
    }
    return true;
}

void SkySessionRecorder::appendTcpWavePeakIndexLine(quint64 hostTimeUs,
                                                    const QByteArray& harmonicPayload,
                                                    TcpFloatEncoding floatEncoding)
{
    const TcpWavePeakSummary summary = summarizeTcpWavePeakSamples(harmonicPayload, floatEncoding);

    std::lock_guard<std::mutex> lock(files_mutex_);
    if (!raw_tcp_wave_peak_index_file_.isOpen())
    {
        return;
    }

    QTextStream out(&raw_tcp_wave_peak_index_file_);
    out << hostTimeUs << ','
        << peakValueCsvText(summary.value) << ','
        << summary.index << ','
        << summary.point_count << ",0,0\n";
    out.flush();
}

bool SkySessionRecorder::writeSessionMetadata(const QString& endTimeUtc, QString *errorMessage)
{
    if (session_metadata_filename_.isEmpty() || session_directory_.isEmpty())
    {
        if (errorMessage) *errorMessage = QStringLiteral("session metadata path is empty");
        return false;
    }

    QDir sessionDir(session_directory_);
    QJsonObject root;
    root.insert(QStringLiteral("mode"), QStringLiteral("sky"));
    root.insert(QStringLiteral("session_name"), session_name_);
    root.insert(QStringLiteral("start_time_utc"), session_start_time_utc_);
    root.insert(QStringLiteral("start_time_us"), QString::number(recording_start_time_us_));
    root.insert(QStringLiteral("end_time_utc"), endTimeUtc);
    root.insert(QStringLiteral("elapsed_ms"), QString::number(recordingElapsedMs()));
    root.insert(QStringLiteral("software_version"), QCoreApplication::applicationVersion().isEmpty()
        ? QStringLiteral("dev")
        : QCoreApplication::applicationVersion());
    root.insert(QStringLiteral("telemetry_port"), telemetry_port_);
    root.insert(QStringLiteral("telemetry_baud"), telemetry_baud_);
    root.insert(QStringLiteral("telemetry_transport"), telemetry_transport_);
    root.insert(QStringLiteral("telemetry_endpoint"), telemetry_endpoint_);
    root.insert(QStringLiteral("epsilon_schema_version"), QStringLiteral("epsilon.v1"));
    root.insert(QStringLiteral("waveform_points_per_frame"),
                static_cast<int>(std::min<quint64>(waveform_points_per_frame_,
                                                   static_cast<quint64>(std::numeric_limits<int>::max()))));
    root.insert(QStringLiteral("sensor_export_rate_hz"), 0);
    root.insert(QStringLiteral("other_devices_export_rate_hz"), 0);
    root.insert(QStringLiteral("raw_export_mode"), QStringLiteral("unified_raw_dat"));
    root.insert(QStringLiteral("raw_dat_format_version"), static_cast<int>(kUnifiedRawFormatVersion));
    root.insert(QStringLiteral("waveform_export_rate_hz"), 0);
    root.insert(QStringLiteral("waveform_export_mode"), QStringLiteral("per_frame"));
    root.insert(QStringLiteral("waveform_value_type"), QStringLiteral("float32"));
    root.insert(QStringLiteral("waveform_timestamp_type"), QStringLiteral("uint64"));
    root.insert(QStringLiteral("timestamp_unit"), QStringLiteral("microseconds"));
    root.insert(QStringLiteral("sensor_rows"), QString::number(telemetry_row_count_));
    root.insert(QStringLiteral("temperature_controller_rows"), QString::number(temperature_controller_count_));
    root.insert(QStringLiteral("waveform_features"), QString::number(waveform_feature_count_));
    root.insert(QStringLiteral("waveform_frames"), QString::number(raw_tcp_wave_record_count_));
    root.insert(QStringLiteral("waveform_file_count"), QString::number(waveform_file_count_));

    QJsonObject rawFiles;
    auto addRawFile = [&rawFiles, &sessionDir](const QString& name,
                                               const QString& filename,
                                               quint16 sourceId,
                                               quint64 recordCount) {
        QJsonObject raw;
        raw.insert(QStringLiteral("path"), sessionDir.relativeFilePath(filename));
        raw.insert(QStringLiteral("source_id"), static_cast<int>(sourceId));
        raw.insert(QStringLiteral("format_version"), static_cast<int>(kUnifiedRawFormatVersion));
        raw.insert(QStringLiteral("record_count"), QString::number(recordCount));
        rawFiles.insert(name, raw);
    };
    addRawFile(QStringLiteral("epsilon"), raw_epsilon_filename_, kRawSourceEpsilon, raw_epsilon_record_count_);
    addRawFile(QStringLiteral("ptb"), raw_ptb_filename_, kRawSourcePtb, raw_ptb_record_count_);
    addRawFile(QStringLiteral("hmp"), raw_hmp_filename_, kRawSourceHmp, raw_hmp_record_count_);
    addRawFile(QStringLiteral("lidar"), raw_lidar_filename_, kRawSourceLidar, raw_lidar_record_count_);
    addRawFile(QStringLiteral("tcp_wave"), raw_tcp_wave_filename_, kRawSourceTcpWave, raw_tcp_wave_record_count_);
    root.insert(QStringLiteral("raw_files"), rawFiles);

    QJsonObject paths;
    paths.insert(QStringLiteral("raw_directory"), QStringLiteral("raw"));
    paths.insert(QStringLiteral("devices_csv"), sessionDir.relativeFilePath(sensors_filename_));
    paths.insert(QStringLiteral("temperature_controller_csv"), sessionDir.relativeFilePath(temperature_controller_filename_));
    paths.insert(QStringLiteral("waveform_features"), sessionDir.relativeFilePath(feature_filename_));
    paths.insert(QStringLiteral("waveform_peak_index"), sessionDir.relativeFilePath(raw_tcp_wave_peak_index_filename_));
    root.insert(QStringLiteral("paths"), paths);

    return writeJsonFileAtomically(session_metadata_filename_, root, errorMessage);
}

void SkySessionRecorder::closeFiles()
{
    std::lock_guard<std::mutex> lock(files_mutex_);
    for (QFile *file : {&basic_record_file_,
                        &feature_record_file_,
                        &temperature_controller_record_file_,
                        &raw_epsilon_file_,
                        &raw_ptb_file_,
                        &raw_hmp_file_,
                        &raw_lidar_file_,
                        &raw_tcp_wave_file_,
                        &raw_tcp_wave_peak_index_file_})
    {
        if (file->isOpen())
        {
            file->flush();
            file->close();
        }
    }
}

}  // namespace VaporView
