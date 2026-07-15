#ifndef VaporView_SESSION_VIEWER_WINDOW_H_
#define VaporView_SESSION_VIEWER_WINDOW_H_

#include <QMainWindow>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <memory>

#include "ground/SessionData.h"
#include "ground/session/SessionWaveformRepository.h"

class QGroupBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QProgressDialog;
class QPushButton;
class QScrollArea;
class QSlider;
class QSpinBox;
class QTableView;
class QWidget;
class QGridLayout;
class QEvent;
template <typename T> class QFutureWatcher;
class QResizeEvent;
class QShowEvent;
class RawDataParserWindow;
class SessionCsvTableModel;
class TrajectoryViewerDialog;
namespace VaporView::Ground
{
class SessionPlaybackController;
}

class SessionViewerWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit SessionViewerWindow(QWidget *parent = nullptr);
    ~SessionViewerWindow() override;
    void setEnglish(bool english);
    void setDefaultDataDirectory(const QString& directory);
    bool openSessionPath(const QString& path);

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void onChooseSessionClicked();
    void onReloadClicked();
    void onClearViewClicked();
    void onViewTrajectoryClicked();
    void onRawDataParserClicked();
    void onFrameSliderMoved(int value);
    void onFrameSliderChanged(int value);
    void onFrameSpinChanged(int value);
    void onToggleWaveformFrameFilterClicked();
    void onTogglePeakPlotModeClicked();
    void onConfigurePeakFilterClicked();

private:
    using PeakFilterMode = VaporView::Ground::SessionPeakFilterMode;

    void setupUi();
    void updateTexts();
    void updateSummaryLabels();
    void relayoutSummaryFields();
    void updateWaveformControls();
    void updateCsvDisplayHeaders();
    void applyCsvTableTheme();
    void refreshCsvItemTheme();
    void setStatusText(const QString& text);
    void beginSessionLoading(const QString& text);
    void updateSessionLoadingText(const QString& text);
    void updateSessionLoadingProgress(const QString& text, int percent);
    void finishSessionLoading();
    void setSessionLoadingControlsEnabled(bool enabled);
    void updateSessionLoadingDialogTheme();
    void clearLoadedData(bool clearPathEdit = true);
    void restoreLastSessionPath(const QString& path);
    QString resolveSessionDirectory(const QString& path) const;
    QString formatMeasuredRateText(const QVector<quint64>& timestampsUs, int metadataRateHz, const QString& metadataMode) const;
    bool loadSessionDirectory(QString sessionDirectory);
    bool loadSessionMetadata(const QString& sessionDirectory);
    bool loadSensorsCsv();
    bool loadWaveformSegments();
    bool loadWaveformPeakSeries(bool allowBackground = false);
    void startBackgroundWaveformPeakSeries();
    void cancelBackgroundWaveformPeakSeries(bool waitForFinished = false);
    bool ensureTrajectoryPeakValuesReady();
    bool previewWaveformFrame(quint64 frameIndex);
    bool loadWaveformFrame(quint64 frameIndex, bool scrollToCsvRow = true);
    bool readWaveformFrameSamples(quint64 frameIndex, quint64& timestampUs, QVector<float>& samples);
    int findClosestCsvRow(quint64 timestampUs) const;
    void applyPeakFilter(int startPercent = 0, int endPercent = 96);
    void updateRtkTrackPeakValues();
    void focusTrajectoryPoint(int trackPointIndex);
    void syncEnvironmentRangeToWaveformRange(int startFrameIndex, int visibleFrameCount);
    void previewClosestSensorRow(quint64 timestampUs);
    QString highlightClosestSensorRow(quint64 timestampUs, bool scrollToCsvRow = true);
    void updateWaveformFrameFilterButtonText();
    void updatePeakPlotModeButtonText();
    void updatePeakFilterButtonText();
    QString peakFilterModeText(PeakFilterMode mode) const;
    QString peakSearchRangeText() const;
    void syncPeakSettingsToTrajectoryViewer();
    bool applyPeakSettings(int searchStartIndex,
                           int searchEndIndex,
                           PeakFilterMode mode,
                           double minValue,
                           double maxValue,
                           bool hasMinValue,
                           bool hasMaxValue,
                           const QString& recalculatingText,
                           const QString& filteringText);
    void applyPeakSettingsFromTrajectory(int searchStartIndex, int searchEndIndex, int filterMode, double minValue, double maxValue);
    QVector<float> visibleWaveformSamples(const QVector<float>& samples, int& firstSampleIndex) const;

    QWidget *central_widget_;
    QLineEdit *session_path_edit_;
    QPushButton *choose_session_btn_;
    QPushButton *reload_btn_;
    QPushButton *trajectory_view_btn_;
    QPushButton *raw_data_parser_btn_;
    QPushButton *clear_view_btn_;
    QLabel *status_label_;
    QProgressDialog *loading_dialog_;
    QLabel *loading_dialog_label_;
    QProgressBar *loading_dialog_progress_bar_;
    int loading_dialog_progress_percent_;
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
    QPushButton *waveform_frame_filter_btn_;
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
    QTableView *csv_table_;
    SessionCsvTableModel *csv_model_;

    QString session_directory_;
    QString metadata_filename_;
    QString sensors_csv_filename_;
    QString waveform_directory_;
    QString waveform_index_filename_;
    QString waveform_peak_index_filename_;
    QString raw_tcp_wave_filename_;
    QString default_data_directory_;
    QString session_name_;
    QString start_time_utc_;
    QString end_time_utc_;
    QStringList csv_headers_;
    QVector<quint64> csv_timestamps_us_;
    QVector<double> temperature_values_;
    QVector<double> humidity_values_;
    QVector<double> pressure_values_;
    QVector<RtkTrackPoint> rtk_track_points_;
    RtkTrackStats rtk_track_stats_;
    QVector<quint64> waveform_timestamps_us_;
    VaporView::Ground::SessionWaveformCatalog waveform_catalog_;
    VaporView::Ground::SessionPlaybackController *playback_controller_;
    QVector<float> current_waveform_frame_samples_;
    QVector<float> waveform_peak_raw_values_;
    QVector<float> waveform_peak_values_;
    VaporView::Ground::SessionPeakFilterSettings peak_filter_settings_;
    int peak_search_start_index_;
    int peak_search_end_index_;
    bool is_english_;
    bool updating_frame_controls_;
    bool waveform_peak_scatter_mode_;
    bool waveform_show_filtered_frame_;
    bool session_loading_;
    quint64 peak_series_request_id_;
    QFutureWatcher<VaporView::Ground::SessionWaveformPeakSeriesResult> *peak_series_watcher_;
    std::shared_ptr<std::atomic_bool> peak_series_cancel_flag_;
    QVector<int> highlighted_csv_rows_;
    int primary_highlighted_csv_row_;
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
