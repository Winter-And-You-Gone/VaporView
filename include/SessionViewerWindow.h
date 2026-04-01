#ifndef VaporView_SESSION_VIEWER_WINDOW_H_
#define VaporView_SESSION_VIEWER_WINDOW_H_

#include <QMainWindow>
#include <QStringList>
#include <QVector>

class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSlider;
class QSpinBox;
class QTableWidget;
class QWidget;

class SessionViewerWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit SessionViewerWindow(QWidget *parent = nullptr);
    void setEnglish(bool english);
    bool openSessionPath(const QString& path);

private slots:
    void onChooseSessionClicked();
    void onReloadClicked();
    void onClearViewClicked();
    void onFrameSliderChanged(int value);
    void onFrameSpinChanged(int value);

private:
    struct WaveformSegment
    {
        QString filename;
        quint64 start_frame = 0;
        quint64 frame_count = 0;
    };

    void setupUi();
    void updateTexts();
    void updateSummaryLabels();
    void updateWaveformControls();
    void setStatusText(const QString& text);
    void clearLoadedData(bool clearPathEdit = true);
    QString resolveSessionDirectory(const QString& path) const;
    bool loadSessionDirectory(const QString& sessionDirectory);
    bool loadSessionMetadata(const QString& sessionDirectory);
    bool loadSensorsCsv();
    bool loadWaveformSegments();
    bool loadWaveformFrame(quint64 frameIndex);
    void highlightClosestSensorRow(quint64 timestampUs);

    QWidget *central_widget_;
    QLineEdit *session_path_edit_;
    QPushButton *choose_session_btn_;
    QPushButton *reload_btn_;
    QPushButton *clear_view_btn_;
    QLabel *status_label_;
    QGroupBox *summary_group_;
    QLabel *session_name_title_;
    QLabel *session_name_value_;
    QLabel *start_time_title_;
    QLabel *start_time_value_;
    QLabel *end_time_title_;
    QLabel *end_time_value_;
    QLabel *sensor_rows_title_;
    QLabel *sensor_rows_value_;
    QLabel *waveform_files_title_;
    QLabel *waveform_files_value_;
    QLabel *waveform_frames_title_;
    QLabel *waveform_frames_value_;
    QGroupBox *waveform_group_;
    QLabel *frame_title_;
    QSlider *frame_slider_;
    QSpinBox *frame_spin_;
    QLabel *frame_total_label_;
    QLabel *frame_info_label_;
    QWidget *waveform_plot_;
    QGroupBox *csv_group_;
    QLabel *csv_info_label_;
    QTableWidget *csv_table_;

    QString session_directory_;
    QString metadata_filename_;
    QString sensors_csv_filename_;
    QString waveform_directory_;
    QString session_name_;
    QString start_time_utc_;
    QString end_time_utc_;
    QStringList csv_headers_;
    QVector<quint64> csv_timestamps_us_;
    QVector<WaveformSegment> waveform_segments_;
    bool is_english_;
    bool updating_frame_controls_;
    int points_per_frame_;
    int waveform_export_rate_hz_;
    quint64 total_sensor_rows_;
    quint64 total_waveform_frames_;
};

#endif
