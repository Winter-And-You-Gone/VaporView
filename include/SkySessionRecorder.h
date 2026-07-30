#ifndef VaporView_SKY_SESSION_RECORDER_H_
#define VaporView_SKY_SESSION_RECORDER_H_

#include "TelemetryTypes.h"
#include "LogRecord.h"
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
    bool stop(QString *errorMessage = nullptr);

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
    quint64 rawNavigationRecordCount() const;
    quint64 rawPressureRecordCount() const;
    quint64 rawTemperatureHumidityRecordCount() const;
    quint64 rawDistanceRecordCount() const;
    quint64 rawWaveformRecordCount() const;

    bool appendEvent(const LogRecord& record);
    bool appendError(const LogRecord& record);

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
    void appendTcpWavePeakIndexLine(quint64 hostTimeUs,
                                    const QByteArray& harmonicPayload,
                                    TcpFloatEncoding floatEncoding);
    bool writeSessionMetadata(const QString& endTimeUtc = QString(), QString *errorMessage = nullptr);
    void closeFiles();

    quint8 recording_state_ = 0;
    QString session_name_;
    QString session_directory_;
    QString session_metadata_filename_;
    QString sensor_summary_filename_;
    QString feature_filename_;
    QString temperature_controller_filename_;
    QString navigation_raw_filename_;
    QString pressure_raw_filename_;
    QString temperature_humidity_raw_filename_;
    QString distance_raw_filename_;
    QString waveform_raw_filename_;
    QString waveform_peaks_filename_;
    QString event_log_filename_;
    QString error_log_filename_;
    QFile basic_record_file_;
    QFile feature_record_file_;
    QFile temperature_controller_record_file_;
    QFile navigation_raw_file_;
    QFile pressure_raw_file_;
    QFile temperature_humidity_raw_file_;
    QFile distance_raw_file_;
    QFile waveform_raw_file_;
    QFile waveform_peaks_file_;
    QFile event_log_file_;
    QFile error_log_file_;
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
    quint64 raw_navigation_record_count_ = 0;
    quint64 raw_pressure_record_count_ = 0;
    quint64 raw_temperature_humidity_record_count_ = 0;
    quint64 raw_distance_record_count_ = 0;
    quint64 raw_waveform_record_count_ = 0;
    quint64 native_raw_waveform_record_count_ = 0;
    quint64 event_row_count_ = 0;
    quint64 error_row_count_ = 0;
};

}  // namespace VaporView

#endif
