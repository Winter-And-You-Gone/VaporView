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
class QGridLayout;
class QResizeEvent;
class QShowEvent;

class SessionViewerWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit SessionViewerWindow(QWidget *parent = nullptr);
    void setEnglish(bool english);
    bool openSessionPath(const QString& path);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void onChooseSessionClicked();
    void onReloadClicked();
    void onClearViewClicked();
    void onFrameSliderChanged(int value);
    void onFrameSpinChanged(int value);
    void onTogglePeakPlotModeClicked();

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
    void relayoutSummaryFields();
    void updateWaveformControls();
    void setStatusText(const QString& text);
    void clearLoadedData(bool clearPathEdit = true);
    QString resolveSessionDirectory(const QString& path) const;
    bool loadSessionDirectory(const QString& sessionDirectory);
    bool loadSessionMetadata(const QString& sessionDirectory);
    bool loadSensorsCsv();
    bool loadWaveformSegments();
    bool loadWaveformPeakSeries();
    bool loadWaveformFrame(quint64 frameIndex);
    int findClosestCsvRow(quint64 timestampUs) const;
    void syncEnvironmentRangeToWaveformRange(int startFrameIndex, int visibleFrameCount);
    QString highlightClosestSensorRow(quint64 timestampUs);
    void updatePeakPlotModeButtonText();

    QWidget *central_widget_;
    QLineEdit *session_path_edit_;
    QPushButton *choose_session_btn_;
    QPushButton *reload_btn_;
    QPushButton *clear_view_btn_;
    QLabel *status_label_;
    QGroupBox *summary_group_;
    QGridLayout *summary_layout_;
    QLabel *session_name_title_;
    QLabel *session_name_value_;
    QLabel *start_time_title_;
    QLabel *start_time_value_;
    QLabel *end_time_title_;
    QLabel *end_time_value_;
    QLabel *duration_title_;
    QLabel *duration_value_;
    QLabel *sensor_export_rate_title_;
    QLabel *sensor_export_rate_value_;
    QLabel *sensor_rows_title_;
    QLabel *sensor_rows_value_;
    QLabel *waveform_export_rate_title_;
    QLabel *waveform_export_rate_value_;
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
    QLabel *waveform_plot_title_;
    QWidget *waveform_plot_;
    QLabel *waveform_peak_plot_title_;
    QPushButton *waveform_peak_mode_btn_;
    QWidget *waveform_peak_plot_;
    QLabel *temperature_plot_title_;
    QWidget *temperature_plot_;
    QLabel *humidity_plot_title_;
    QWidget *humidity_plot_;
    QLabel *pressure_plot_title_;
    QWidget *pressure_plot_;
    QLabel *environment_info_label_;
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
    QVector<double> temperature_values_;
    QVector<double> humidity_values_;
    QVector<double> pressure_values_;
    QVector<quint64> waveform_timestamps_us_;
    QVector<WaveformSegment> waveform_segments_;
    QVector<float> waveform_peak_values_;
    bool is_english_;
    bool updating_frame_controls_;
    bool waveform_peak_scatter_mode_;
    QVector<int> highlighted_csv_rows_;
    int points_per_frame_;
    int sensor_export_rate_hz_;
    int waveform_export_rate_hz_;
    QString waveform_export_mode_;
    quint64 total_sensor_rows_;
    quint64 total_waveform_frames_;
};

#endif
