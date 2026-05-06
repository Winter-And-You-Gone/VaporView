#ifndef VaporView_SKY_SESSION_RECORDER_H_
#define VaporView_SKY_SESSION_RECORDER_H_

#include "TelemetryTypes.h"

#include <QFile>
#include <QString>
#include <QVector>

namespace VaporView
{

class SkySessionRecorder
{
public:
    bool start(const QString& baseDirectory,
               const QString& telemetryPort,
               int telemetryBaud,
               QString *errorMessage = nullptr);
    void pause();
    void stop();

    bool isRecording() const;
    bool isPaused() const;
    quint8 recordingState() const;
    QString sessionName() const;
    QString sessionDirectory() const;

    void recordBasicTelemetry(const TelemetryBasic& data);
    void recordWaveformFeature(const WaveformFeature& feature);
    void recordWaveformSnapshot(quint64 hostTimeUs, quint64 epsilonTimeUs, const QVector<float>& samples);

private:
    void closeFiles();

    quint8 recording_state_ = 0;
    QString session_name_;
    QString session_directory_;
    QFile basic_record_file_;
    QFile feature_record_file_;
    QFile waveform_index_file_;
    quint64 telemetry_row_count_ = 0;
    quint64 waveform_file_count_ = 0;
};

}  // namespace VaporView

#endif
