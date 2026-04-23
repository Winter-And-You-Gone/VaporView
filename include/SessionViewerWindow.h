#ifndef VaporView_SESSION_VIEWER_WINDOW_H_
#define VaporView_SESSION_VIEWER_WINDOW_H_

#include <QByteArray>
#include <QMainWindow>
#include <QStringList>
#include <QVector>

#include "TcpWaveEncoding.h"

struct RtkTrackPoint
{
    double latitude = 0.0;
    double longitude = 0.0;
    double height_m = 0.0;
    quint64 timestamp_us = 0;
    float peak_value = 0.0f;
    bool has_height = false;
    bool has_peak_value = false;
};

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
class RawDataParserWindow;
class TrajectoryViewerDialog;

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
    void onViewTrajectoryClicked();
    void onRawDataParserClicked();
    void onFrameSliderChanged(int value);
    void onFrameSpinChanged(int value);
    void onTogglePeakPlotModeClicked();
    void onConfigurePeakFilterClicked();

private:
    enum class PeakFilterMode
    {
        None,
        IqrOutlier,
        KeepRange,
        ExcludeRange
    };

    struct PeakFilterSettings
    {
        PeakFilterMode mode = PeakFilterMode::None;
        double min_value = 0.0;
        double max_value = 0.0;
    };

    struct WaveformSegment
    {
        QString filename;
        quint64 start_frame = 0;
        quint64 frame_count = 0;
    };

    struct RawTcpWaveFrame
    {
        QString filename;
        quint64 harmonic_payload_offset = 0;
        quint32 harmonic_payload_size = 0;
        quint64 timestamp_us = 0;
        VaporView::TcpFloatEncoding float_encoding = VaporView::TcpFloatEncoding::Unknown;
    };

    void setupUi();
    void updateTexts();
    void updateSummaryLabels();
    void relayoutSummaryFields();
    void updateWaveformControls();
    void setStatusText(const QString& text);
    void clearLoadedData(bool clearPathEdit = true);
    void restoreLastSessionPath(const QString& path);
    QString resolveSessionDirectory(const QString& path) const;
    QString formatMeasuredRateText(const QVector<quint64>& timestampsUs, int metadataRateHz, const QString& metadataMode) const;
    bool loadSessionDirectory(QString sessionDirectory);
    bool loadSessionMetadata(const QString& sessionDirectory);
    bool loadSensorsCsv();
    bool loadWaveformSegments();
    bool loadWaveformPeakSeries();
    bool loadWaveformFrame(quint64 frameIndex);
    bool loadUnifiedRawTcpWaveFrames();
    bool readWaveformFrameSamples(quint64 frameIndex, quint64& timestampUs, QVector<float>& samples);
    int findClosestCsvRow(quint64 timestampUs) const;
    void applyPeakFilter();
    void updateRtkTrackPeakValues();
    void syncEnvironmentRangeToWaveformRange(int startFrameIndex, int visibleFrameCount);
    QString highlightClosestSensorRow(quint64 timestampUs);
    void updatePeakPlotModeButtonText();
    void updatePeakFilterButtonText();
    QString peakFilterModeText(PeakFilterMode mode) const;

    QWidget *central_widget_;
    QLineEdit *session_path_edit_;
    QPushButton *choose_session_btn_;
    QPushButton *reload_btn_;
    QPushButton *trajectory_view_btn_;
    QPushButton *raw_data_parser_btn_;
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
    QPushButton *waveform_peak_filter_btn_;
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
    QString raw_tcp_wave_filename_;
    QString session_name_;
    QString start_time_utc_;
    QString end_time_utc_;
    QStringList csv_headers_;
    QVector<quint64> csv_timestamps_us_;
    QVector<double> temperature_values_;
    QVector<double> humidity_values_;
    QVector<double> pressure_values_;
    QVector<RtkTrackPoint> rtk_track_points_;
    QVector<quint64> waveform_timestamps_us_;
    QVector<WaveformSegment> waveform_segments_;
    QVector<RawTcpWaveFrame> raw_tcp_wave_frames_;
    QVector<float> waveform_peak_raw_values_;
    QVector<float> waveform_peak_values_;
    PeakFilterSettings peak_filter_settings_;
    int peak_search_start_index_;
    int peak_search_end_index_;
    bool is_english_;
    bool updating_frame_controls_;
    bool waveform_peak_scatter_mode_;
    QVector<int> highlighted_csv_rows_;
    TrajectoryViewerDialog *trajectory_viewer_dialog_;
    RawDataParserWindow *raw_data_parser_window_;
    int points_per_frame_;
    int sensor_export_rate_hz_;
    int waveform_export_rate_hz_;
    QString waveform_export_mode_;
    quint64 total_sensor_rows_;
    quint64 total_waveform_frames_;
};

#endif
