#ifndef VaporView_SESSION_VIEWER_WINDOW_H_
#define VaporView_SESSION_VIEWER_WINDOW_H_

#include "ground/session/SessionTrajectoryController.h"
#include "ground/session/SessionWaveformRepository.h"
#include "shared/session/RecordingOrigin.h"

#include <QMainWindow>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <memory>

class QEvent;
template <typename T> class QFutureWatcher;
class RawDataParserWindow;

namespace VaporView::Ground
{
class SessionMapCoordinator;
class SessionPlaybackController;
}

namespace VaporView::Ground::SessionUi
{
class SessionDeviceDataWidget;
class SessionLoadingDialog;
class SessionOverviewWidget;
class SessionWaveformWidget;
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
    void setUiTestMode(bool enabled);

protected:
    void changeEvent(QEvent *event) override;

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
    void updateWaveformControls();
    void setStatusText(const QString& text);
    void beginSessionLoading(const QString& text);
    void updateSessionLoadingProgress(const QString& text, int percent);
    void finishSessionLoading();
    void setSessionLoadingControlsEnabled(bool enabled);
    void clearLoadedData(bool clearPathEdit = true);
    void restoreLastSessionPath(const QString& path);
    QString resolveSessionDirectory(const QString& path) const;
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
    void updateWaveformActionTexts();
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
    void applyPeakSettingsFromTrajectory(int searchStartIndex,
                                         int searchEndIndex,
                                         int filterMode,
                                         double minValue,
                                         double maxValue);
    QVector<float> visibleWaveformSamples(const QVector<float>& samples, int& firstSampleIndex) const;

    VaporView::Ground::SessionUi::SessionOverviewWidget *overview_page_;
    VaporView::Ground::SessionUi::SessionWaveformWidget *waveform_page_;
    VaporView::Ground::SessionUi::SessionDeviceDataWidget *device_data_page_;
    std::unique_ptr<VaporView::Ground::SessionUi::SessionLoadingDialog> loading_dialog_;
    VaporView::Ground::SessionMapCoordinator *map_coordinator_;
    VaporView::Ground::SessionTrajectoryController trajectory_controller_;
    VaporView::Ground::SessionPlaybackController *playback_controller_;
    RawDataParserWindow *raw_data_parser_window_;

    QString session_directory_;
    QString metadata_filename_;
    VaporView::Session::RecordingOrigin recording_origin_;
    QString sensors_csv_filename_;
    QString waveform_directory_;
    QString waveform_index_filename_;
    QString waveform_peak_index_filename_;
    QString waveform_raw_filename_;
    QString session_load_warning_;
    QString default_data_directory_;
    QString session_name_;
    QString start_time_utc_;
    QString end_time_utc_;
    QStringList csv_headers_;
    QVector<quint64> csv_timestamps_us_;
    QVector<double> temperature_values_;
    QVector<double> humidity_values_;
    QVector<double> pressure_values_;
    QVector<quint64> waveform_timestamps_us_;
    VaporView::Ground::SessionWaveformCatalog waveform_catalog_;
    QVector<float> current_waveform_frame_samples_;
    QVector<float> waveform_peak_raw_values_;
    QVector<float> waveform_peak_values_;
    VaporView::Ground::SessionPeakFilterSettings peak_filter_settings_;
    int peak_search_start_index_;
    int peak_search_end_index_;
    bool ui_test_mode_ = false;
    QString ui_test_saved_default_data_directory_;
    VaporView::Ground::SessionPeakFilterSettings ui_test_saved_peak_filter_settings_;
    int ui_test_saved_peak_search_start_index_ = 0;
    int ui_test_saved_peak_search_end_index_ = 0;
    bool is_english_;
    bool updating_frame_controls_;
    bool waveform_peak_scatter_mode_;
    bool waveform_show_filtered_frame_;
    bool session_loading_;
    quint64 peak_series_request_id_;
    QFutureWatcher<VaporView::Ground::SessionWaveformPeakSeriesResult> *peak_series_watcher_;
    std::shared_ptr<std::atomic_bool> peak_series_cancel_flag_;
    int points_per_frame_;
    int sensor_export_rate_hz_;
    int waveform_export_rate_hz_;
    QString waveform_export_mode_;
    quint64 total_sensor_rows_;
    quint64 total_waveform_frames_;
};

#endif
