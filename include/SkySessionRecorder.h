#ifndef VaporView_SKY_SESSION_RECORDER_H_
#define VaporView_SKY_SESSION_RECORDER_H_

#include "TelemetryTypes.h"
#include "data_types.h"
#include "TcpWaveEncoding.h"

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QVector>
#include <mutex>

namespace VaporView
{

class SkySessionRecorder
{
public:
    bool start(const QString& baseDirectory,
               const QString& telemetryPort,
               int telemetryBaud,
               QString *errorMessage = nullptr);
    bool start(const QString& baseDirectory,
               const QString& telemetryPort,
               int telemetryBaud,
               QString *errorMessage,
               const QString& telemetryTransport,
               const QString& telemetryEndpoint = QString());
    void pause();
    void stop();

    bool isRecording() const;
    bool isPaused() const;
    quint8 recordingState() const;
    QString sessionName() const;
    QString sessionDirectory() const;
    quint64 recordingStartTimeUs() const;
    quint64 recordingElapsedMs() const;
    quint64 telemetryRecordCount() const;
    quint64 waveformFeatureRecordCount() const;
    quint64 waveformSnapshotRecordCount() const;
    quint64 temperatureControllerRecordCount() const;
    quint64 rawEpsilonRecordCount() const;
    quint64 rawPtbRecordCount() const;
    quint64 rawHmpRecordCount() const;
    quint64 rawLidarRecordCount() const;
    quint64 rawTcpWaveRecordCount() const;

    void recordBasicTelemetry(const TelemetryBasic& data);
    void recordDeviceSnapshot(quint64 hostTimeUs,
                              quint64 epsilonHostTimeUs,
                              const EpsilonData& epsilon,
                              bool hasEpsilon,
                              const PtbData& ptb,
                              bool hasPtb,
                              const HmpData& hmp,
                              bool hasHmp,
                              const LidarData& lidar,
                              bool hasLidar);
    void recordWaveformFeature(const WaveformFeature& feature);
    void recordWaveformSnapshot(quint64 hostTimeUs,
                                quint64 epsilonTimeUs,
                                const QVector<float>& rawSamples,
                                const QVector<float>& harmonicSamples);
    void recordTemperatureControllerStatus(quint64 hostTimeUs, const TemperatureControllerData& data);
    void recordRawEpsilonFrame(quint64 hostTimeUs,
                               quint8 packetId,
                               quint8 serialNumber,
                               const QByteArray& frame);
    void recordRawPtbResponse(quint64 hostTimeUs, const QByteArray& response);
    void recordRawHmpResponse(quint64 hostTimeUs, const QByteArray& response);
    void recordRawLidarFrame(quint64 hostTimeUs, quint16 protocol, const QByteArray& frame);
    void recordRawTcpWaveFrame(quint64 hostTimeUs,
                               const QByteArray& rawPayload,
                               const QByteArray& harmonicPayload,
                               TcpFloatEncoding floatEncoding);

private:
    bool openRawDatFile(QFile& file, const QString& filename, quint16 sourceId, QString *errorMessage);
    bool writeRawRecord(QFile& file,
                        quint64& recordCount,
                        quint16 sourceId,
                        quint16 recordType,
                        quint32 flags,
                        quint64 hostTimeUs,
                        const void *payload,
                        qsizetype payloadSize);
    bool writeRawTcpWavePayload(quint64 hostTimeUs,
                                const QByteArray& rawPayload,
                                const QByteArray& harmonicPayload,
                                TcpFloatEncoding floatEncoding);
    void writeSessionMetadata(const QString& endTimeUtc = QString());
    void closeFiles();

    quint8 recording_state_ = 0;
    QString session_name_;
    QString session_directory_;
    QString session_metadata_filename_;
    QString sensors_filename_;
    QString feature_filename_;
    QString temperature_controller_filename_;
    QString raw_epsilon_filename_;
    QString raw_ptb_filename_;
    QString raw_hmp_filename_;
    QString raw_lidar_filename_;
    QString raw_tcp_wave_filename_;
    QFile basic_record_file_;
    QFile feature_record_file_;
    QFile temperature_controller_record_file_;
    QFile raw_epsilon_file_;
    QFile raw_ptb_file_;
    QFile raw_hmp_file_;
    QFile raw_lidar_file_;
    QFile raw_tcp_wave_file_;
    mutable std::mutex files_mutex_;
    QString session_start_time_utc_;
    QString telemetry_port_;
    int telemetry_baud_ = 0;
    QString telemetry_transport_ = QStringLiteral("serial");
    QString telemetry_endpoint_;
    quint64 recording_start_time_us_ = 0;
    quint64 recording_elapsed_ms_ = 0;
    quint64 telemetry_row_count_ = 0;
    quint64 waveform_feature_count_ = 0;
    quint64 temperature_controller_count_ = 0;
    quint64 waveform_file_count_ = 0;
    quint64 waveform_points_per_frame_ = 0;
    quint64 raw_epsilon_record_count_ = 0;
    quint64 raw_ptb_record_count_ = 0;
    quint64 raw_hmp_record_count_ = 0;
    quint64 raw_lidar_record_count_ = 0;
    quint64 raw_tcp_wave_record_count_ = 0;
    quint64 native_raw_tcp_wave_record_count_ = 0;
};

}  // namespace VaporView

#endif
