#include "SkySessionRecorder.h"

#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

namespace VaporView
{

bool SkySessionRecorder::start(const QString& baseDirectory,
                               const QString& telemetryPort,
                               int telemetryBaud,
                               QString *errorMessage)
{
    if (recording_state_ == 2 && !session_directory_.isEmpty())
    {
        recording_state_ = 1;
        return true;
    }

    closeFiles();

    const QString timestamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
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
    if (!sessionDir.mkpath(QStringLiteral("waveforms")))
    {
        if (errorMessage) *errorMessage = QStringLiteral("cannot create waveform directory");
        return false;
    }

    basic_record_file_.setFileName(sessionDir.filePath(QStringLiteral("telemetry_basic.csv")));
    feature_record_file_.setFileName(sessionDir.filePath(QStringLiteral("waveform_features.csv")));
    waveform_index_file_.setFileName(sessionDir.filePath(QStringLiteral("waveform_index.csv")));

    if (!basic_record_file_.open(QIODevice::WriteOnly | QIODevice::Text) ||
        !feature_record_file_.open(QIODevice::WriteOnly | QIODevice::Text) ||
        !waveform_index_file_.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (errorMessage) *errorMessage = QStringLiteral("cannot open session files");
        closeFiles();
        return false;
    }

    QTextStream basicOut(&basic_record_file_);
    basicOut << "host_time_us,epsilon_time_us,latitude_deg,longitude_deg,height_m,ecef_x_m,ecef_y_m,ecef_z_m,lidar_height_m,temperature_c,humidity_percent,pressure_hpa,status_bits,filter_status_bits,update_status_bits,gnss_fix_code\n";

    QTextStream featureOut(&feature_record_file_);
    featureOut << "host_time_us,epsilon_time_us,channel_id,peak,mean,rms,peak_index,peak_x,min_value,max_value,quality_flags\n";

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
        metadata.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    }

    telemetry_row_count_ = 0;
    waveform_file_count_ = 0;
    recording_state_ = 1;
    return true;
}

void SkySessionRecorder::pause()
{
    if (recording_state_ != 0)
    {
        recording_state_ = 2;
    }
}

void SkySessionRecorder::stop()
{
    if (!session_directory_.isEmpty())
    {
        QFile metadata(QDir(session_directory_).filePath(QStringLiteral("metadata_end.json")));
        if (metadata.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            QJsonObject object;
            object.insert(QStringLiteral("session_name"), session_name_);
            object.insert(QStringLiteral("end_time_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
            object.insert(QStringLiteral("telemetry_rows"), QString::number(telemetry_row_count_));
            object.insert(QStringLiteral("waveform_files"), QString::number(waveform_file_count_));
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
        << static_cast<int>(data.gnss_fix_code) << '\n';
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
        << feature.channel_id << ','
        << feature.peak << ','
        << feature.mean << ','
        << feature.rms << ','
        << feature.peak_index << ','
        << feature.peak_x << ','
        << feature.min_value << ','
        << feature.max_value << ','
        << feature.quality_flags << '\n';
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

void SkySessionRecorder::closeFiles()
{
    if (basic_record_file_.isOpen()) basic_record_file_.close();
    if (feature_record_file_.isOpen()) feature_record_file_.close();
    if (waveform_index_file_.isOpen()) waveform_index_file_.close();
}

}  // namespace VaporView
