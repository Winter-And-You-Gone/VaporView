#include "SkySessionRecorder.h"

#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QtEndian>
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
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
}

}  // namespace

bool SkySessionRecorder::start(const QString& baseDirectory,
                               const QString& telemetryPort,
                               int telemetryBaud,
                               QString *errorMessage)
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

    const QString timestamp = timestampForSessionName();
    session_name_ = QStringLiteral("sky_session_%1").arg(timestamp);

    QDir recordsDir(baseDirectory);
    if (!recordsDir.mkpath(QStringLiteral("records")) || !recordsDir.cd(QStringLiteral("records")))
    {
        if (errorMessage) *errorMessage = QStringLiteral("cannot create records directory");
        return false;
    }
    if (!recordsDir.mkpath(session_name_))
    {
        if (errorMessage) *errorMessage = QStringLiteral("cannot create session directory");
        return false;
    }

    session_directory_ = recordsDir.filePath(session_name_);
    QDir sessionDir(session_directory_);
    if (!sessionDir.mkpath(QStringLiteral("waveforms")) ||
        !sessionDir.mkpath(QStringLiteral("raw")) ||
        !sessionDir.mkpath(QStringLiteral("sensors")))
    {
        if (errorMessage) *errorMessage = QStringLiteral("cannot create session subdirectories");
        return false;
    }

    basic_record_file_.setFileName(sessionDir.filePath(QStringLiteral("telemetry_basic.csv")));
    feature_record_file_.setFileName(sessionDir.filePath(QStringLiteral("waveform_features.csv")));
    waveform_index_file_.setFileName(sessionDir.filePath(QStringLiteral("waveform_index.csv")));
    raw_epsilon_filename_ = sessionDir.filePath(QStringLiteral("raw/epsilon.dat"));
    raw_ptb_filename_ = sessionDir.filePath(QStringLiteral("raw/ptb.dat"));
    raw_hmp_filename_ = sessionDir.filePath(QStringLiteral("raw/hmp.dat"));
    raw_lidar_filename_ = sessionDir.filePath(QStringLiteral("raw/lidar.dat"));
    raw_tcp_wave_filename_ = sessionDir.filePath(QStringLiteral("raw/tcp_wave.dat"));

    if (!basic_record_file_.open(QIODevice::WriteOnly | QIODevice::Text) ||
        !feature_record_file_.open(QIODevice::WriteOnly | QIODevice::Text) ||
        !waveform_index_file_.open(QIODevice::WriteOnly | QIODevice::Text) ||
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
    basicOut << "host_time_us,epsilon_time_us,latitude_deg,longitude_deg,height_m,ecef_x_m,ecef_y_m,ecef_z_m,lidar_height_m,temperature_c,humidity_percent,pressure_hpa,status_bits,filter_status_bits,update_status_bits,gnss_fix_code,validity_flags\n";

    QTextStream featureOut(&feature_record_file_);
    featureOut << "host_time_us,epsilon_time_us,original_point_count,search_start_index,search_end_index,channel_id,peak,mean,rms,peak_index,peak_x,min_value,max_value,quality_flags\n";

    QTextStream waveformOut(&waveform_index_file_);
    waveformOut << "host_time_us,epsilon_time_us,point_count,filename\n";

    QFile metadata(sessionDir.filePath(QStringLiteral("metadata.json")));
    if (metadata.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QJsonObject object;
        object.insert(QStringLiteral("mode"), QStringLiteral("sky"));
        object.insert(QStringLiteral("session_name"), session_name_);
        object.insert(QStringLiteral("start_time_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        object.insert(QStringLiteral("telemetry_port"), telemetryPort);
        object.insert(QStringLiteral("telemetry_baud"), telemetryBaud);
        object.insert(QStringLiteral("raw_export_mode"), QStringLiteral("unified_raw_dat"));
        object.insert(QStringLiteral("raw_dat_format_version"), static_cast<int>(kUnifiedRawFormatVersion));
        metadata.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    }

    recording_start_time_us_ = nowUs();
    recording_elapsed_ms_ = 0;
    telemetry_row_count_ = 0;
    waveform_feature_count_ = 0;
    waveform_file_count_ = 0;
    raw_epsilon_record_count_ = 0;
    raw_ptb_record_count_ = 0;
    raw_hmp_record_count_ = 0;
    raw_lidar_record_count_ = 0;
    raw_tcp_wave_record_count_ = 0;
    recording_state_ = 1;
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

void SkySessionRecorder::stop()
{
    recording_elapsed_ms_ = recordingElapsedMs();
    if (!session_directory_.isEmpty())
    {
        QFile metadata(QDir(session_directory_).filePath(QStringLiteral("metadata_end.json")));
        if (metadata.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            QJsonObject object;
            object.insert(QStringLiteral("session_name"), session_name_);
            object.insert(QStringLiteral("end_time_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
            object.insert(QStringLiteral("elapsed_ms"), QString::number(recording_elapsed_ms_));
            object.insert(QStringLiteral("telemetry_rows"), QString::number(telemetry_row_count_));
            object.insert(QStringLiteral("waveform_features"), QString::number(waveform_feature_count_));
            object.insert(QStringLiteral("waveform_files"), QString::number(waveform_file_count_));
            QJsonObject rawFiles;
            rawFiles.insert(QStringLiteral("epsilon"), QString::number(raw_epsilon_record_count_));
            rawFiles.insert(QStringLiteral("ptb"), QString::number(raw_ptb_record_count_));
            rawFiles.insert(QStringLiteral("hmp"), QString::number(raw_hmp_record_count_));
            rawFiles.insert(QStringLiteral("lidar"), QString::number(raw_lidar_record_count_));
            rawFiles.insert(QStringLiteral("tcp_wave"), QString::number(raw_tcp_wave_record_count_));
            object.insert(QStringLiteral("raw_records"), rawFiles);
            metadata.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
        }
    }
    closeFiles();
    recording_state_ = 0;
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

quint64 SkySessionRecorder::waveformSnapshotRecordCount() const
{
    return waveform_file_count_;
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
    if (!isRecording() || !basic_record_file_.isOpen())
    {
        return;
    }
    QTextStream out(&basic_record_file_);
    out << data.host_time_us << ','
        << data.epsilon_time_us << ','
        << data.latitude_deg << ','
        << data.longitude_deg << ','
        << data.height_m << ','
        << data.ecef_x_m << ','
        << data.ecef_y_m << ','
        << data.ecef_z_m << ','
        << data.lidar_height_m << ','
        << data.temperature_c << ','
        << data.humidity_percent << ','
        << data.pressure_hpa << ','
        << data.status_bits << ','
        << data.filter_status_bits << ','
        << data.update_status_bits << ','
        << static_cast<int>(data.gnss_fix_code) << ','
        << data.validity_flags << '\n';
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
        << feature.peak << ','
        << feature.mean << ','
        << feature.rms << ','
        << feature.peak_index << ','
        << feature.peak_x << ','
        << feature.min_value << ','
        << feature.max_value << ','
        << feature.quality_flags << '\n';
    ++waveform_feature_count_;
}

void SkySessionRecorder::recordWaveformSnapshot(quint64 hostTimeUs, quint64 epsilonTimeUs, const QVector<float>& samples)
{
    if (!isRecording() || samples.isEmpty() || !waveform_index_file_.isOpen())
    {
        return;
    }

    QDir sessionDir(session_directory_);
    const QString filename = QStringLiteral("waveforms/harmonic_%1.bin").arg(hostTimeUs);
    QFile waveformFile(sessionDir.filePath(filename));
    if (!waveformFile.open(QIODevice::WriteOnly))
    {
        return;
    }
    waveformFile.write(reinterpret_cast<const char *>(samples.constData()),
                       static_cast<qint64>(samples.size() * sizeof(float)));

    QTextStream out(&waveform_index_file_);
    out << hostTimeUs << ','
        << epsilonTimeUs << ','
        << samples.size() << ','
        << filename << '\n';
    ++waveform_file_count_;
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
    if (!isRecording() ||
        static_cast<quint64>(rawPayload.size()) > std::numeric_limits<quint32>::max() ||
        static_cast<quint64>(harmonicPayload.size()) > std::numeric_limits<quint32>::max())
    {
        return;
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

    writeRawRecord(raw_tcp_wave_file_,
                   raw_tcp_wave_record_count_,
                   kRawSourceTcpWave,
                   kRawRecordTypeGeneric,
                   kRawTcpWaveCombinedPayloadFlag | tcpFloatEncodingToRawDatFlags(floatEncoding),
                   hostTimeUs,
                   payload.constData(),
                   payload.size());
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

void SkySessionRecorder::closeFiles()
{
    std::lock_guard<std::mutex> lock(files_mutex_);
    for (QFile *file : {&basic_record_file_,
                        &feature_record_file_,
                        &waveform_index_file_,
                        &raw_epsilon_file_,
                        &raw_ptb_file_,
                        &raw_hmp_file_,
                        &raw_lidar_file_,
                        &raw_tcp_wave_file_})
    {
        if (file->isOpen())
        {
            file->flush();
            file->close();
        }
    }
}

}  // namespace VaporView
