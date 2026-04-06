#ifndef VAPORVIEW_SESSION_CONTROLLER_H
#define VAPORVIEW_SESSION_CONTROLLER_H

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QStringList>
#include <QVector>

class SessionController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool english READ english NOTIFY englishChanged)
    Q_PROPERTY(QString sessionPath READ sessionPath NOTIFY sessionChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(QString sessionName READ sessionName NOTIFY summaryChanged)
    Q_PROPERTY(QString startTime READ startTime NOTIFY summaryChanged)
    Q_PROPERTY(QString endTime READ endTime NOTIFY summaryChanged)
    Q_PROPERTY(qulonglong sensorRows READ sensorRows NOTIFY summaryChanged)
    Q_PROPERTY(qulonglong waveformFiles READ waveformFiles NOTIFY summaryChanged)
    Q_PROPERTY(qulonglong waveformFrames READ waveformFrames NOTIFY summaryChanged)
    Q_PROPERTY(QString csvInfoText READ csvInfoText NOTIFY csvChanged)
    Q_PROPERTY(QString frameInfoText READ frameInfoText NOTIFY waveformChanged)
    Q_PROPERTY(int currentFrame READ currentFrame NOTIFY waveformChanged)
    Q_PROPERTY(int totalFrames READ totalFrames NOTIFY summaryChanged)
    Q_PROPERTY(int highlightedRow READ highlightedRow NOTIFY waveformChanged)
    Q_PROPERTY(bool peakScatterMode READ peakScatterMode NOTIFY waveformChanged)
    Q_PROPERTY(QVariantList csvColumnSource READ csvColumnSource NOTIFY csvChanged)
    Q_PROPERTY(QVariantList csvRows READ csvRows NOTIFY csvChanged)
    Q_PROPERTY(QVariantList currentWaveSamples READ currentWaveSamples NOTIFY waveformChanged)
    Q_PROPERTY(QVariantList peakValues READ peakValues NOTIFY waveformChanged)

public:
    explicit SessionController(QObject *parent = nullptr);

    bool english() const;
    QString sessionPath() const;
    QString statusText() const;
    QString sessionName() const;
    QString startTime() const;
    QString endTime() const;
    qulonglong sensorRows() const;
    qulonglong waveformFiles() const;
    qulonglong waveformFrames() const;
    QString csvInfoText() const;
    QString frameInfoText() const;
    int currentFrame() const;
    int totalFrames() const;
    int highlightedRow() const;
    bool peakScatterMode() const;
    QVariantList csvColumnSource() const;
    QVariantList csvRows() const;
    QVariantList currentWaveSamples() const;
    QVariantList peakValues() const;

    void setEnglish(bool english);

    Q_INVOKABLE bool loadSessionPath(const QString &path);
    Q_INVOKABLE bool loadSessionUrl(const QUrl &url);
    Q_INVOKABLE bool reload();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void setCurrentFrame(int frameNumber);
    Q_INVOKABLE void togglePeakPlotMode();

signals:
    void englishChanged();
    void sessionChanged();
    void statusChanged();
    void summaryChanged();
    void csvChanged();
    void waveformChanged();

private:
    struct WaveformSegment
    {
        QString filename;
        quint64 startFrame = 0;
        quint64 frameCount = 0;
    };

    QString textFor(const QString &englishText, const QString &chineseText) const;
    QString resolveSessionDirectory(const QString &path) const;
    bool loadSessionDirectory(const QString &sessionDirectory);
    bool loadSessionMetadata(const QString &sessionDirectory);
    bool loadSensorsCsv();
    bool loadWaveformSegments();
    bool loadWaveformPeakSeries();
    bool loadWaveformFrame(quint64 frameIndex);
    void clearLoadedData(bool clearPath = true);
    void rebuildColumnSource();
    void setStatusText(const QString &text);
    void updateFrameInfoForEmptyState();
    void updateCsvInfoText();
    void highlightClosestSensorRow(quint64 timestampUs);
    static QString localFilePath(const QUrl &url);

    bool english_ = false;
    QString session_directory_;
    QString metadata_filename_;
    QString sensors_csv_filename_;
    QString waveform_directory_;
    QString session_name_;
    QString start_time_utc_;
    QString end_time_utc_;
    QString status_text_;
    QString csv_info_text_;
    QString frame_info_text_;
    QStringList csv_headers_;
    QVector<quint64> csv_timestamps_us_;
    QVector<WaveformSegment> waveform_segments_;
    QVector<float> waveform_peak_values_;
    QVariantList csv_column_source_;
    QVariantList csv_rows_;
    QVariantList current_wave_samples_;
    QVariantList peak_values_;
    int current_frame_ = 0;
    int highlighted_row_ = -1;
    bool peak_scatter_mode_ = true;
    int points_per_frame_ = 50000;
    int waveform_export_rate_hz_ = 10;
    quint64 total_sensor_rows_ = 0;
    quint64 total_waveform_frames_ = 0;
};

#endif
