#pragma once

#include "ground/session/SessionWaveformRepository.h"

#include <QGroupBox>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QLabel;
class QGridLayout;
class QLineEdit;
class QProgressBar;
class QProgressDialog;
class QPushButton;
class QResizeEvent;
class QSlider;
class QSpinBox;
class QTableView;

namespace VaporView::Ground::SessionUi
{

class SessionCsvTableModel;
class SessionPeakPlotWidget;
class SessionWavePlotWidget;
class SingleSeriesTrendPlotWidget;

QString formatSessionMeasuredRateText(const QVector<quint64>& timestampsUs,
                                      int metadataRateHz,
                                      const QString& metadataMode,
                                      bool english);

struct SessionOverviewSummary
{
    QString sessionName;
    QString startTime;
    QString endTime;
    QString duration;
    QString sensorRate;
    QString sensorRows;
    QString waveformRate;
    QString waveformFiles;
    QString waveformFrames;
};

class SessionOverviewWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit SessionOverviewWidget(QWidget *parent = nullptr);

    void setEnglish(bool english);
    void setSessionPath(const QString& path);
    void clearSessionPath();
    void setStatusText(const QString& text);
    QString statusText() const;
    void setStatusToolTip(const QString& text);
    void focusStatus();
    void setControlsEnabled(bool enabled);
    void setTrajectoryAvailable(bool available);
    void setSummary(const SessionOverviewSummary& summary);
    void relayoutSummaryFields();

signals:
    void chooseSessionRequested();
    void reloadRequested();
    void trajectoryRequested();
    void rawDataParserRequested();
    void clearRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
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
    bool controls_enabled_ = true;
    bool trajectory_available_ = false;
};

class SessionWaveformWidget final : public QGroupBox
{
    Q_OBJECT

public:
    explicit SessionWaveformWidget(QWidget *parent = nullptr);

    void setEnglish(bool english);
    void setActionTexts(const QString& frameFilterText,
                        const QString& peakFilterText,
                        const QString& plotModeText);
    void setControlsEnabled(bool enabled);
    void configureFrames(quint64 totalFrames);
    void setFrameValueSilently(int value);
    int frameValue() const;
    bool frameValueInRange(int value) const;
    void setFrameInfoText(const QString& text);
    void setFramePreviewInfo(quint64 frameIndex, quint64 totalFrames, bool english);
    void setFrameDetails(quint64 frameIndex,
                         quint64 totalFrames,
                         quint64 timestampUs,
                         const QString& exportMode,
                         int exportRateHz,
                         double minimum,
                         double maximum,
                         double peak,
                         const QString& sourceFilename,
                         const QString& csvMatchText,
                         bool english);
    void setWaveformSamples(const QVector<float>& samples, int firstSampleIndex = 0);
    void setPeakValues(const QVector<float>& values);
    void setCurrentPeakFrame(int frameIndex);
    void setPlotMode(bool scatter);
    void repaintPlots();
    void setEnvironmentSeries(const QVector<double>& temperature,
                              const QVector<double>& humidity,
                              const QVector<double>& pressure);
    void setEnvironmentCurrentIndex(int row, bool english);
    void setEnvironmentRange(int startIndex, int count);
    void setEnvironmentInfoText(const QString& text);
    void clear();

signals:
    void frameSliderMoved(int value);
    void frameSliderChanged(int value);
    void frameSpinChanged(int value);
    void frameFilterRequested();
    void peakFilterRequested();
    void plotModeRequested();
    void visibleRangeChanged(int startIndex, int visibleCount);

private:
    QLabel *frame_title_;
    QSlider *frame_slider_;
    QSpinBox *frame_spin_;
    QLabel *frame_total_label_;
    QLabel *frame_info_label_;
    QLabel *waveform_plot_title_;
    SessionWavePlotWidget *waveform_plot_;
    QLabel *waveform_peak_plot_title_;
    QPushButton *waveform_frame_filter_btn_;
    QPushButton *waveform_peak_filter_btn_;
    QPushButton *waveform_peak_mode_btn_;
    SessionPeakPlotWidget *waveform_peak_plot_;
    QLabel *temperature_plot_title_;
    SingleSeriesTrendPlotWidget *temperature_plot_;
    QLabel *humidity_plot_title_;
    SingleSeriesTrendPlotWidget *humidity_plot_;
    QLabel *pressure_plot_title_;
    SingleSeriesTrendPlotWidget *pressure_plot_;
    QLabel *environment_info_label_;
    QVector<double> temperature_values_;
    QVector<double> humidity_values_;
    QVector<double> pressure_values_;
    bool controls_enabled_ = true;
    bool has_frames_ = false;
};

struct SessionCsvHighlightResult
{
    int primaryRow = -1;
    QString description;
};

class SessionDeviceDataWidget final : public QGroupBox
{
    Q_OBJECT

public:
    explicit SessionDeviceDataWidget(QWidget *parent = nullptr);

    void setEnglish(bool english);
    void setInfoText(const QString& text);
    void setRows(const QStringList& headers, QVector<QStringList>&& rows);
    void clear();
    void applyTheme();
    SessionCsvHighlightResult highlightTimestamp(
        const QVector<quint64>& timestampsUs,
        quint64 timestampUs,
        bool scrollToRow);

private:
    void updateDisplayHeaders();

    QLabel *csv_info_label_;
    QTableView *csv_table_;
    SessionCsvTableModel *csv_model_;
    QStringList csv_headers_;
    bool is_english_ = false;
};

struct SessionPeakSettingsInput
{
    int searchStartIndex = 0;
    int searchEndIndex = 0;
    SessionPeakFilterSettings filter;
    bool hasMinValue = false;
    bool hasMaxValue = false;
};

bool editSessionPeakSettings(QWidget *parent,
                             bool english,
                             int searchStartIndex,
                             int searchEndIndex,
                             const SessionPeakFilterSettings& filter,
                             SessionPeakSettingsInput& output);

class SessionLoadingDialog final
{
public:
    explicit SessionLoadingDialog(QWidget *owner);
    ~SessionLoadingDialog();

    void begin(const QString& text, bool english);
    void update(const QString& text, int percent);
    void finish(const QString& finalText);
    void applyTheme();

private:
    QWidget *owner_;
    QProgressDialog *dialog_ = nullptr;
    QLabel *label_ = nullptr;
    QProgressBar *progress_bar_ = nullptr;
    int progress_percent_ = 0;
};

}  // namespace VaporView::Ground::SessionUi
