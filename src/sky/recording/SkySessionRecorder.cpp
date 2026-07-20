#include "SkySessionRecorder.h"
#include "geo/CoordinateTransform.h"
#include "shared/session/SessionManifest.h"
#include "shared/session/SessionPackageInitializer.h"
#include "shared/session/SessionPackageLayout.h"
#include "shared/session/SessionSensorCsv.h"
#include "shared/session/UnifiedRawDat.h"

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
bool hasUsableEpsilonLlh(const EpsilonData& data)
{
    return std::isfinite(data.latitude_deg) &&
           std::isfinite(data.longitude_deg) &&
           std::isfinite(data.height_m) &&
           data.latitude_deg >= -90.0 &&
           data.latitude_deg <= 90.0 &&
           data.longitude_deg >= -180.0 &&
           data.longitude_deg <= 180.0 &&
           (std::abs(data.latitude_deg) > 1.0e-9 ||
            std::abs(data.longitude_deg) > 1.0e-9 ||
            std::abs(data.height_m) > 1.0e-9);
}

bool resolveEpsilonEcefFromLlh(EpsilonData& data)
{
    if (Geo::isPlausibleEcef(data.ecef_x_m, data.ecef_y_m, data.ecef_z_m))
    {
        return true;
    }
    if (!hasUsableEpsilonLlh(data))
    {
        return false;
    }

    Geo::EcefPoint derived;
    if (!Geo::deriveEcefFromLlh(data.latitude_deg, data.longitude_deg, data.height_m, derived))
    {
        return false;
    }
    data.ecef_x_m = derived.xM;
    data.ecef_y_m = derived.yM;
    data.ecef_z_m = derived.zM;
    return true;
}

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

QString applicationSoftwareVersion()
{
    return QCoreApplication::applicationVersion().isEmpty()
        ? QStringLiteral("dev")
        : QCoreApplication::applicationVersion();
}

QJsonObject skyDeviceConfigJson(const QString& telemetryTransport,
                                const QString& telemetryEndpoint,
                                const QString& telemetryPort,
                                int telemetryBaud)
{
    QJsonObject root;
    root.insert(QStringLiteral("recording_origin"), QStringLiteral("sky"));
    root.insert(QStringLiteral("epsilon_schema_version"), 1);
    root.insert(QStringLiteral("raw_export_mode"), QStringLiteral("unified_raw_dat"));
    root.insert(QStringLiteral("raw_dat_format_version"), static_cast<int>(SessionRawDat::kCurrentFormatVersion));
    QJsonObject telemetry;
    telemetry.insert(QStringLiteral("transport"), telemetryTransport.isEmpty()
        ? QJsonValue(QJsonValue::Null)
        : QJsonValue(telemetryTransport));
    telemetry.insert(QStringLiteral("endpoint"), telemetryEndpoint.isEmpty()
        ? QJsonValue(QJsonValue::Null)
        : QJsonValue(telemetryEndpoint));
    telemetry.insert(QStringLiteral("port"), telemetryPort.isEmpty()
        ? QJsonValue(QJsonValue::Null)
        : QJsonValue(telemetryPort));
    telemetry.insert(QStringLiteral("baud"), telemetryBaud > 0
        ? QJsonValue(telemetryBaud)
        : QJsonValue(QJsonValue::Null));
    root.insert(QStringLiteral("telemetry"), telemetry);
    return root;
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
    session_start_time_utc_ = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    telemetry_port_ = telemetryPort;
    telemetry_baud_ = telemetryBaud;
    telemetry_transport_ = telemetryTransport.trimmed().isEmpty() ? QStringLiteral("serial") : telemetryTransport.trimmed();
    telemetry_endpoint_ = telemetryEndpoint.trimmed().isEmpty() ? telemetryPort : telemetryEndpoint.trimmed();
    recording_start_time_us_ = nowUs();

    VaporView::Session::SessionPackageInitOptions initOptions;
    initOptions.origin = VaporView::Session::RecordingOrigin::Sky;
    initOptions.sessionName = baseSessionName;
    initOptions.outputDirectory = baseDirectory;
    initOptions.softwareVersion = applicationSoftwareVersion();
    initOptions.startTimeUtc = session_start_time_utc_;
    initOptions.startTimeUs = recording_start_time_us_;
    initOptions.sensorExportRateHz = 0;
    initOptions.otherDevicesExportRateHz = 0;
    initOptions.waveformExportRateHz = 0;
    initOptions.waveformPointsPerFrame = 0;
    initOptions.capture.telemetryTransport = telemetry_transport_;
    initOptions.capture.telemetryEndpoint = telemetry_endpoint_;
    initOptions.capture.telemetryPort = telemetry_port_;
    initOptions.capture.telemetryBaud = telemetry_baud_ > 0 ? QString::number(telemetry_baud_) : QString();
    initOptions.initialDeviceConfig = skyDeviceConfigJson(telemetry_transport_,
                                                          telemetry_endpoint_,
                                                          telemetry_port_,
                                                          telemetry_baud_);

    const VaporView::Session::SessionPackageInitResult initResult =
        VaporView::Session::initializeSessionPackage(initOptions);
    if (!initResult.success)
    {
        if (errorMessage) *errorMessage = initResult.error;
        return false;
    }

    session_name_ = initResult.sessionName;
    session_directory_ = initResult.sessionDirectory;
    const VaporView::Session::SessionPackageLayout& packageLayout = initResult.layout;
    session_metadata_filename_ = VaporView::Session::sessionPackageFilePath(session_directory_, packageLayout.manifestPath);
    sensors_filename_ = VaporView::Session::sessionPackageFilePath(session_directory_, packageLayout.devicesCsvPath);
    feature_filename_ = VaporView::Session::sessionPackageFilePath(session_directory_, packageLayout.waveformFeaturesCsvPath);
    temperature_controller_filename_ = VaporView::Session::sessionPackageFilePath(session_directory_, packageLayout.temperatureControllerCsvPath);
    basic_record_file_.setFileName(sensors_filename_);
    feature_record_file_.setFileName(feature_filename_);
    temperature_controller_record_file_.setFileName(temperature_controller_filename_);
    raw_epsilon_filename_ = VaporView::Session::sessionPackageFilePath(session_directory_, packageLayout.epsilonRawPath);
    raw_ptb_filename_ = VaporView::Session::sessionPackageFilePath(session_directory_, packageLayout.ptbRawPath);
    raw_hmp_filename_ = VaporView::Session::sessionPackageFilePath(session_directory_, packageLayout.hmpRawPath);
    raw_lidar_filename_ = VaporView::Session::sessionPackageFilePath(session_directory_, packageLayout.lidarRawPath);
    raw_tcp_wave_filename_ = VaporView::Session::sessionPackageFilePath(session_directory_, packageLayout.tcpWaveRawPath);
    raw_tcp_wave_peak_index_filename_ = VaporView::Session::sessionPackageFilePath(session_directory_, packageLayout.tcpWavePeaksCsvPath);
    raw_tcp_wave_peak_index_file_.setFileName(raw_tcp_wave_peak_index_filename_);

    if (!basic_record_file_.open(QIODevice::WriteOnly | QIODevice::Text) ||
        !feature_record_file_.open(QIODevice::WriteOnly | QIODevice::Text) ||
        !temperature_controller_record_file_.open(QIODevice::WriteOnly | QIODevice::Text) ||
        !raw_tcp_wave_peak_index_file_.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
        !openRawDatFile(raw_epsilon_file_, raw_epsilon_filename_, SessionRawDat::kSourceEpsilon, errorMessage) ||
        !openRawDatFile(raw_ptb_file_, raw_ptb_filename_, SessionRawDat::kSourcePtb, errorMessage) ||
        !openRawDatFile(raw_hmp_file_, raw_hmp_filename_, SessionRawDat::kSourceHmp, errorMessage) ||
        !openRawDatFile(raw_lidar_file_, raw_lidar_filename_, SessionRawDat::kSourceLidar, errorMessage) ||
        !openRawDatFile(raw_tcp_wave_file_, raw_tcp_wave_filename_, SessionRawDat::kSourceTcpWave, errorMessage))
    {
        if (errorMessage && errorMessage->isEmpty()) *errorMessage = QStringLiteral("cannot open session files");
        closeFiles();
        return false;
    }

    QTextStream basicOut(&basic_record_file_);
    basicOut << SessionSensorCsv::header();

    QTextStream featureOut(&feature_record_file_);
    featureOut << VaporView::Session::waveformFeaturesCsvHeader();

    QTextStream temperatureOut(&temperature_controller_record_file_);
    temperatureOut << VaporView::Session::temperatureControllerCsvHeader();

    QTextStream peakIndexOut(&raw_tcp_wave_peak_index_file_);
    peakIndexOut << VaporView::Session::tcpWavePeaksCsvHeader();
    peakIndexOut.flush();

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

    EpsilonData resolvedEpsilon = epsilon;
    if (hasEpsilon && resolvedEpsilon.valid)
    {
        resolveEpsilonEcefFromLlh(resolvedEpsilon);
    }

    QTextStream out(&basic_record_file_);
    out << SessionSensorCsv::formatRow(
        hostTimeUs,
        epsilonHostTimeUs,
        resolvedEpsilon,
        hasEpsilon && resolvedEpsilon.valid,
        ptb,
        hasPtb && ptb.valid,
        hmp,
        hasHmp && hmp.valid,
        lidar,
        hasLidar && lidar.valid);
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
                   SessionRawDat::kSourceEpsilon,
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
                   SessionRawDat::kSourcePtb,
                   SessionRawDat::kRecordTypePtbResponse,
                   0u,
                   hostTimeUs,
                   response.constData(),
                   response.size());
}

void SkySessionRecorder::recordRawHmpResponse(quint64 hostTimeUs, const QByteArray& response)
{
    writeRawRecord(raw_hmp_file_,
                   raw_hmp_record_count_,
                   SessionRawDat::kSourceHmp,
                   SessionRawDat::kRecordTypeHmpModbusResponse,
                   0u,
                   hostTimeUs,
                   response.constData(),
                   response.size());
}

void SkySessionRecorder::recordRawLidarFrame(quint64 hostTimeUs, quint16 protocol, const QByteArray& frame)
{
    writeRawRecord(raw_lidar_file_,
                   raw_lidar_record_count_,
                   SessionRawDat::kSourceLidar,
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

    if (!SessionRawDat::writeFileHeader(file, sourceId, errorMessage))
    {
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
        payloadSize > static_cast<qsizetype>(SessionRawDat::kMaxPayloadSize))
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(files_mutex_);
    if (!file.isOpen())
    {
        return false;
    }

    const quint64 sequence = recordCount;
    SessionRawDat::RawRecordHeader header;
    header.hostTimestampUs = hostTimeUs;
    header.sourceId = sourceId;
    header.recordType = recordType;
    header.flags = flags;
    header.sequence = sequence;
    const QByteArrayView payloadView = payloadSize > 0
        ? QByteArrayView(static_cast<const char *>(payload), payloadSize)
        : QByteArrayView();
    if (!SessionRawDat::writeRecord(file, header, payloadView))
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
    if (!SessionRawDat::encodeTcpWavePayload(rawPayload, harmonicPayload, &payload))
    {
        return false;
    }

    if (!writeRawRecord(raw_tcp_wave_file_,
                        raw_tcp_wave_record_count_,
                        SessionRawDat::kSourceTcpWave,
                        SessionRawDat::kRecordTypeTcpWavePayload,
                        SessionRawDat::kTcpWaveCombinedPayloadFlag | tcpFloatEncodingToRawDatFlags(floatEncoding),
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

    const quint64 endTimeUs = endTimeUtc.isEmpty() ? 0 : nowUs();
    VaporView::Session::SessionManifest manifest;
    manifest.recordingOrigin = VaporView::Session::RecordingOrigin::Sky;
    manifest.sessionName = session_name_;
    manifest.state = endTimeUtc.isEmpty()
        ? VaporView::Session::SessionState::Recording
        : VaporView::Session::SessionState::Complete;
    manifest.startTimeUtc = session_start_time_utc_;
    manifest.endTimeUtc = endTimeUtc;
    manifest.startTimeUs = recording_start_time_us_;
    manifest.endTimeUs = endTimeUs;
    manifest.elapsedMs = recordingElapsedMs();
    manifest.softwareVersion = applicationSoftwareVersion();
    manifest.rawDatFormatVersion = static_cast<int>(SessionRawDat::kCurrentFormatVersion);
    manifest.sensorExportRateHz = 0;
    manifest.otherDevicesExportRateHz = 0;
    manifest.waveformExportRateHz = 0;
    manifest.waveformPointsPerFrame = static_cast<int>(std::min<quint64>(
        waveform_points_per_frame_,
        static_cast<quint64>(std::numeric_limits<int>::max())));
    manifest.waveformFileCount = waveform_file_count_;
    manifest.capture.telemetryTransport = telemetry_transport_;
    manifest.capture.telemetryEndpoint = telemetry_endpoint_;
    manifest.capture.telemetryPort = telemetry_port_;
    manifest.capture.telemetryBaud = telemetry_baud_ > 0 ? QString::number(telemetry_baud_) : QString();
    manifest.counts.sensorRows = telemetry_row_count_;
    manifest.counts.temperatureControllerRows = temperature_controller_count_;
    manifest.counts.waveformFrames = raw_tcp_wave_record_count_;
    manifest.counts.waveformFeatureRows = waveform_feature_count_;
    manifest.counts.eventRows = 0;
    manifest.counts.errorRows = 0;
    manifest.rawRecords.epsilon = raw_epsilon_record_count_;
    manifest.rawRecords.ptb = raw_ptb_record_count_;
    manifest.rawRecords.hmp = raw_hmp_record_count_;
    manifest.rawRecords.lidar = raw_lidar_record_count_;
    manifest.rawRecords.tcpWave = raw_tcp_wave_record_count_;
    return VaporView::Session::writeSessionManifestAtomically(session_metadata_filename_,
                                                              manifest,
                                                              errorMessage);
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
