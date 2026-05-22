#ifndef VaporView_MAIN_WINDOW_H_
#define VaporView_MAIN_WINDOW_H_

#include "data_collector.h"
#include "data_types.h"
#include "GroundTelemetryService.h"
#include "TelemetryTypes.h"
#include "TcpWaveEncoding.h"
#include <QByteArray>
#include <QMainWindow>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>
#include <QTextEdit>
#include <QGroupBox>
#include <QFrame>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTimer>
#include <QAction>
#include <QActionGroup>
#include <QScrollArea>
#include <QLineEdit>
#include <QVector>
#include <QHash>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

class RtkConfigDialog;
class QFile;
class QEvent;
class QToolButton;
class TcpWavePanel;
class SessionViewerWindow;
class EpsilonPanel;
namespace VaporView { class SkyDeviceConfigDialog; }

class GnssPanel : public QWidget
{
    Q_OBJECT

public:
    explicit GnssPanel(QWidget *parent = nullptr);
    void updateData(const VaporView::GnssData& gnss_data, quint64 timestamp_us = 0);
    void updateRate(double hz);
    void setEnglish(bool english);

private:
    void setupUi();

    QLabel *rate_label_;
    QLabel *status_label_;
    QLabel *time_label_;
    QLabel *lat_label_;
    QLabel *lon_label_;
    QLabel *alt_label_;
    QLabel *vel_n_label_;
    QLabel *vel_e_label_;
    QLabel *vel_ground_label_;
    QLabel *heading_label_;
    QLabel *pitch_label_;
    QLabel *heading_len_label_;
    QLabel *heading_type_label_;
    QLabel *heading_sats_label_;
    QLabel *sats_label_;
    QLabel *gdop_label_;
    QLabel *pdop_label_;
    QLabel *hdop_label_;
    QLabel *htdop_label_;
    QLabel *tdop_label_;
    QLabel *diff_age_label_;
    QLabel *undulation_label_;
    QLabel *sigma_lat_label_;
    QLabel *sigma_lon_label_;
    QLabel *sigma_alt_label_;
    QLabel *cutoff_label_;

    QLabel *status_lbl_;
    QLabel *time_lbl_;
    QLabel *lat_lbl_;
    QLabel *lon_lbl_;
    QLabel *alt_lbl_;
    QLabel *vel_n_lbl_;
    QLabel *vel_e_lbl_;
    QLabel *vel_ground_lbl_;
    QLabel *heading_lbl_;
    QLabel *pitch_lbl_;
    QLabel *heading_type_lbl_;
    QLabel *heading_len_lbl_;
    QLabel *heading_sats_lbl_;
    QLabel *sats_lbl_;
    QLabel *diff_lbl_;
    QLabel *gdop_lbl_;
    QLabel *pdop_lbl_;
    QLabel *hdop_lbl_;
    QLabel *htdop_lbl_;
    QLabel *tdop_lbl_;
    QLabel *cutoff_lbl_;
    QLabel *undulation_lbl_;
    QLabel *sigma_lat_lbl_;
    QLabel *sigma_lon_lbl_;
    QLabel *sigma_alt_lbl_;

    bool is_english_;
};

class ImuPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ImuPanel(QWidget *parent = nullptr);
    void updateData(const VaporView::ImuData& imu_data, quint64 gnss_timestamp_us = 0);
    void updateRate(double hz);
    void setEnglish(bool english);

private:
    void setupUi();

    QLabel *rate_label_;
    QLabel *acc_x_label_;
    QLabel *acc_y_label_;
    QLabel *acc_z_label_;
    QLabel *gyr_x_label_;
    QLabel *gyr_y_label_;
    QLabel *gyr_z_label_;
    QLabel *roll_label_;
    QLabel *pitch_label_;
    QLabel *yaw_label_;
    QLabel *quat_w_label_;
    QLabel *quat_x_label_;
    QLabel *quat_y_label_;
    QLabel *quat_z_label_;
    QLabel *temp_label_;
    QLabel *press_label_;
    QLabel *source_label_;
    QLabel *time_label_;
    QLabel *pps_label_;

    QLabel *source_lbl_;
    QLabel *time_lbl_;
    QLabel *pps_lbl_;
    QLabel *accel_sep_;
    QLabel *gyro_sep_;
    QLabel *attitude_sep_;
    QLabel *quat_sep_;
    QLabel *env_sep_;
    QLabel *temp_lbl_;
    QLabel *press_lbl_;
    QLabel *acc_x_lbl_;
    QLabel *acc_y_lbl_;
    QLabel *acc_z_lbl_;
    QLabel *gyr_x_lbl_;
    QLabel *gyr_y_lbl_;
    QLabel *gyr_z_lbl_;
    QLabel *roll_lbl_;
    QLabel *pitch_lbl_;
    QLabel *yaw_lbl_;
    QLabel *quat_w_lbl_;
    QLabel *quat_x_lbl_;
    QLabel *quat_y_lbl_;
    QLabel *quat_z_lbl_;

    bool is_english_;
};

class PtbPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PtbPanel(QWidget *parent = nullptr);
    void updateData(const VaporView::PtbData& ptb_data);
    void updateRate(double hz);
    void setEnglish(bool english);

private:
    void setupUi();

    QLabel *rate_label_;
    QLabel *pressure_label_;
    QLabel *status_label_;
    QLabel *pressure_lbl_;

    bool is_english_;
};

class HmpPanel : public QWidget
{
    Q_OBJECT

public:
    explicit HmpPanel(QWidget *parent = nullptr);
    void updateData(const VaporView::HmpData& hmp_data);
    void updateRate(double hz);
    void setEnglish(bool english);

private:
    void setupUi();

    QLabel *rate_label_;
    QLabel *humidity_label_;
    QLabel *temperature_label_;
    QLabel *status_label_;
    QLabel *temp_lbl_;
    QLabel *humidity_lbl_;

    bool is_english_;
};

class LidarPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LidarPanel(QWidget *parent = nullptr);
    void updateData(const VaporView::LidarData& lidar_data);
    void updateRate(double hz);
    void setEnglish(bool english);

private:
    void setupUi();

    QLabel *rate_label_;
    QLabel *distance_label_;
    QLabel *strength_label_;
    QLabel *status_label_;
    QLabel *distance_lbl_;
    QLabel *strength_lbl_;

    bool is_english_;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray& eventType, void *message, qintptr *result) override;
#endif
    void changeEvent(QEvent *event) override;

private slots:
    void onConnectClicked();
    void onDisconnectClicked();
    void onEpsilonDataReady();
    void onGnssDataReady();
    void onImuDataReady();
    void onPtbDataReady();
    void onHmpDataReady();
    void onLidarDataReady();
    void onRefreshTimer();
    void onClearLogClicked();
    void onRefreshPortsClicked();
    void onAutoDetectPortsClicked();
    void onChooseRecordingDirectoryClicked();
    void onToggleFullScreen();
    void onSwitchLanguage();
    void onReconfigureEpsilonClicked();
    void onConfigureEpsilonRtcmPortClicked();
    void onConfigureEpsilonPacketRatesClicked();
    void onGlobalRateChanged(const QString& text);
    void onGnssRateChanged(const QString& text);
    void onImuRateChanged(const QString& text);
    void onPtbRateChanged(const QString& text);
    void onHmpRateChanged(const QString& text);
    void onLidarRateChanged(const QString& text);
    void onRtkConfigClicked();
    void onOpenSessionViewerClicked();
    void onFontScaleTriggered(QAction *action);
    void onCancelConnectClicked();
    void onToggleTheme();
    void onTcpRawWaveFrameReady(quint64 timestampUs, QByteArray rawSignalPayload, QByteArray harmonicPayload, VaporView::TcpFloatEncoding floatEncoding);
    void onStartRecordingClicked();
    void onPauseRecordingClicked();
    void onStopRecordingClicked();
    void onDataSourceModeChanged(int index);
    void onSkyDeviceConfigClicked();
    void onRemoteBasicTelemetryUpdated(const VaporView::TelemetryBasic& data);
    void onRemoteWaveformUpdated(const VaporView::DownsampledWaveform& waveform);
    void onRemoteWaveformFeatureUpdated(const VaporView::WaveformFeature& feature);
    void onRemoteTelemetryStatusUpdated(const VaporView::TelemetryStatus& status);
    void onRemoteCommandAckReceived(const VaporView::CommandAck& ack);
    void onRemoteLinkOpenChanged(bool open);

private:
    struct CollectorSnapshot
    {
        std::shared_ptr<VaporView::EpsilonCollector> epsilon;
        std::shared_ptr<VaporView::GnssCollector> gnss;
        std::shared_ptr<VaporView::ImuCollector> imu;
        std::shared_ptr<VaporView::PtbCollector> ptb;
        std::shared_ptr<VaporView::HmpCollector> hmp;
        std::shared_ptr<VaporView::LidarCollector> lidar;
    };

    void setupMenuBar();
    void setupToolBar();
    void setupCustomTitleBar();
    void setupStatusBar();
    void setupCentralWidget();
    void setupConfigPanel();
    void setupDataPanels();
    void setupLogPanel();
    void loadModernStyleSheet();
    void log(const QString& message);
    void updateRecordingStatusLabel();
    void showStatusTaskProgress(const QString& label, int value, int maximum);
    void showBusyStatusTaskProgress(const QString& label);
    void hideStatusTaskProgress();
    void startStatusTaskSpinner();
    void stopStatusTaskSpinner();
    void rebuildRecordingRateMenu();
    void setRecordingExportRateHz(int rate, bool should_log = true);
    bool applyEpsilonMainAntennaLeverArm(double x_m, double y_m, double z_m, QString *error_message);
    void setImuRecordingRateHz(int rate, bool should_log = true);
    void setWaveformRecordingRateHz(int rate, bool should_log = true);
    QString defaultRecordingDirectory() const;
    QString locateRepositoryRoot() const;
    bool startRecordingSession();
    void pauseRecordingSession(bool announce = true);
    void stopRecording(bool announce = true);
    void startRecordingWorkers();
    void stopRecordingWorkers();
    void writeSensorsHeader();
    bool prepareRecordingSessionLayout(const QString& recordsPath, const QString& sessionName);
    bool copyRawDatFormatDocumentToSession();
    bool openUnifiedRawDatFile(std::unique_ptr<QFile>& file, const QString& filename, quint16 sourceId);
    bool writeUnifiedRawRecord(QFile *file,
                               std::atomic<quint64>& recordCount,
                               quint16 sourceId,
                               quint16 recordType,
                               quint32 flags,
                               quint64 hostTimestampUs,
                               const void *payload,
                               size_t payloadSize);
    void closeUnifiedRawDatFiles();
    void resetUnifiedRawDatFiles();
    void writeSessionMetadata(const QString& endTimeUtc = QString());
    void writeDeviceConfigSnapshot();
    void appendEventLogLine(const QString& level, const QString& message);
    void appendErrorLogLine(const QString& message);
    quint64 currentTimestampUs() const;
    quint64 steadyToEpochUs(const std::chrono::steady_clock::time_point& timePoint) const;
    void updateConnectionStatus(bool connected);
    bool anyCollectorRunning() const;
    QStringList getAvailablePorts();
    void setEnglish(bool english);
    void setFontScale(int percent);
    void applyStyleConfiguration();
    QString themedStyleSheet() const;
    QString scaledStyleSheet(const QString& styleSheet) const;
    void applyScaledUiMetrics();
    void updateThemeAction();
    void updateCustomTitleBarTexts();
    void updateCustomTitleBarStyle();
    void updateWindowControlButtons();
    QToolButton *createTitleBarActionButton(QAction *action, QWidget *parent);
    QToolButton *createTitleBarIconButton(const QString& objectName, QWidget *parent);
    void addTitleBarSeparator(QHBoxLayout *layout);
    bool shouldStartWindowMove(QObject *watched) const;
    int scalePixels(int pixels) const;
    void applyAllSampleRates();
    int parseRate(const QString& text) const;
    bool isRateUnspecified(const QString& text) const;
    int effectiveRateOrDefault(const QString& text, int defaultRate, int maxRate = 1000) const;
    void loadRememberedInputState();
    void saveRememberedInputState() const;
    void bindRememberedInputState();
    bool applyImuDeviceProfile(const QString& requestedFormat = QString(), int requestedBaud = -1, int requestedRate = -1);
    bool restartImuCollector(const std::shared_ptr<VaporView::ImuCollector>& collector, const QString& port, int baud, int rate);
    void setImuFormatSelection(const QString& format);
    void setImuBaudSelection(int baud);
    void setImuRateSelection(int rate);
    void stopAllCollectors();
    CollectorSnapshot snapshotCollectors() const;
    void setCollectors(CollectorSnapshot collectors);
    bool shouldAbortConnectionAttempt();
    void finishConnectionAttempt(bool connected);
    void updateRecordingActionStates();
    bool isRemoteSkyMode() const;
    void updateSourceModeUi();
    void clearRemoteSkyDataUi();
    void refreshRemoteSkyDataUi();
    bool remoteDeviceDataValid(VaporView::SkyDeviceId device, qint64 timeout_ms) const;
    QString remoteDeviceInvalidText(VaporView::SkyDeviceId device, qint64 timeout_ms) const;
    void noteRemotePacket(VaporView::MsgType type);
    double remotePacketRate(VaporView::MsgType type) const;
    QString remoteTelemetrySummaryText() const;
    void updateRemoteTelemetrySummaryLabel();
    void updateEnvironmentStatusIcons(bool lidarValid, bool ptbValid, bool hmpValid);
    void sendRemoteDeviceCommand(VaporView::CommandId command, VaporView::SkyDeviceId device);
    void sendRemotePeakSearchRange(quint32 startIndex, quint32 endIndex);
    QPushButton *createRemoteDeviceButton(const QString& text, VaporView::CommandId command, VaporView::SkyDeviceId device);
    void setRemoteDeviceButtonsEnabled(bool enabled);
    void updateRemoteDeviceButtonText(VaporView::SkyDeviceId device, VaporView::DeviceState state);

    QWidget *central_widget_;
    QVBoxLayout *main_layout_;
    QWidget *custom_title_bar_;
    QLabel *custom_title_label_;
    QToolButton *title_menu_btn_;
    QToolButton *window_minimize_btn_;
    QToolButton *window_maximize_btn_;
    QToolButton *window_close_btn_;

    EpsilonPanel *epsilon_panel_;
    GnssPanel *gnss_panel_;
    ImuPanel *imu_panel_;
    PtbPanel *ptb_panel_;
    HmpPanel *hmp_panel_;
    LidarPanel *lidar_panel_;

    QTextEdit *log_text_edit_;
    QLabel *status_label_;
    QProgressBar *status_task_progress_bar_;
    QLabel *status_task_spinner_label_;
    QTimer *status_task_spinner_timer_;
    QLabel *recording_status_label_;
    QPushButton *auto_detect_ports_btn_;

    QComboBox *epsilon_port_combo_;
    QComboBox *gnss_port_combo_;
    QComboBox *imu_port_combo_;
    QComboBox *ptb_port_combo_;
    QComboBox *hmp_port_combo_;
    QComboBox *lidar_port_combo_;
    QComboBox *epsilon_baud_combo_;
    QComboBox *gnss_baud_combo_;
    QComboBox *imu_baud_combo_;
    QComboBox *ptb_baud_combo_;
    QComboBox *hmp_baud_combo_;
    QComboBox *lidar_baud_combo_;
    QAction *connect_btn_;
    QAction *cancel_connect_btn_;
    QAction *disconnect_btn_;
    QAction *start_recording_btn_;
    QAction *pause_recording_btn_;
    QAction *stop_recording_btn_;
    QAction *refresh_ports_btn_;
    QAction *fullscreen_menu_action_;
    QAction *fullscreen_toolbar_action_;
    QAction *lang_action_;
    QAction *theme_toggle_action_;
    QAction *clear_log_action_;
    QAction *session_viewer_action_;
    QAction *epsilon_reconfigure_action_;
    QAction *epsilon_rtcm_port_action_;
    QAction *epsilon_packet_rates_action_;
    QAction *recording_directory_action_;
    QAction *exit_action_;
    QAction *about_action_;
    QActionGroup *font_scale_group_;
    QAction *font_tiny_action_;
    QAction *font_extra_small_action_;
    QAction *font_small_action_;
    QAction *font_normal_action_;
    QAction *font_large_action_;
    QAction *font_extra_large_action_;
    QMenu *data_menu_;
    QMenu *devices_menu_;
    QMenu *view_menu_;
    QMenu *font_menu_;
    QMenu *language_menu_;
    QMenu *help_menu_;
    QMenu *recording_rate_menu_;
    QMenu *title_application_menu_;

    QGroupBox *config_group_;
    QGroupBox *data_group_;
    QFrame *log_group_;
    QGroupBox *tcp_wave_group_;
    QGroupBox *epsilon_group_;
    QGroupBox *gnss_group_;
    QGroupBox *imu_group_;
    QGroupBox *ptb_group_;
    QGroupBox *hmp_group_;
    QGroupBox *env_group_;
    QGroupBox *lidar_group_;

    QLabel *epsilon_lbl_;
    QLabel *gnss_lbl_;
    QLabel *imu_lbl_;
    QLabel *ptb_lbl_;
    QLabel *hmp_lbl_;
    QLabel *lidar_lbl_;
    QWidget *data_telemetry_summary_card_;
    QLabel *data_telemetry_summary_lbl_;
    QLabel *log_inline_title_lbl_;
    QLabel *epsilon_inline_title_lbl_;
    QLabel *gnss_inline_title_lbl_;
    QLabel *imu_inline_title_lbl_;
    QLabel *env_inline_title_lbl_;
    QLabel *env_lidar_status_icon_;
    QLabel *env_ptb_status_icon_;
    QLabel *env_hmp_status_icon_;
    QLabel *config_inline_title_lbl_;
    QLabel *global_rate_lbl_;
    QLabel *epsilon_rate_lbl_;
    QLabel *gnss_rate_lbl_;
    QLabel *imu_rate_lbl_;
    QLabel *ptb_rate_lbl_;
    QLabel *hmp_rate_lbl_;
    QLabel *lidar_rate_lbl_;
    QLabel *data_source_mode_lbl_;
    QLabel *sky_telemetry_port_lbl_;
    QLabel *sky_telemetry_baud_lbl_;
    QWidget *sky_telemetry_row_widget_;

    QComboBox *global_rate_combo_;
    QComboBox *epsilon_rate_combo_;
    QComboBox *gnss_rate_combo_;
    QComboBox *imu_rate_combo_;
    QComboBox *ptb_rate_combo_;
    QComboBox *hmp_rate_combo_;
    QComboBox *lidar_rate_combo_;
    QComboBox *data_source_mode_combo_;
    QComboBox *sky_telemetry_port_combo_;
    QComboBox *sky_telemetry_baud_combo_;
    QComboBox *imu_format_combo_;
    QPushButton *epsilon_packet_rates_btn_;
    QPushButton *sky_device_config_btn_;
    QPushButton *epsilon_remote_connect_btn_;
    QPushButton *epsilon_remote_disconnect_btn_;
    QPushButton *epsilon_remote_reconnect_btn_;
    QWidget *epsilon_remote_buttons_widget_;
    QPushButton *ptb_remote_connect_btn_;
    QPushButton *ptb_remote_disconnect_btn_;
    QPushButton *ptb_remote_reconnect_btn_;
    QWidget *ptb_remote_buttons_widget_;
    QPushButton *hmp_remote_connect_btn_;
    QPushButton *hmp_remote_disconnect_btn_;
    QPushButton *hmp_remote_reconnect_btn_;
    QWidget *hmp_remote_buttons_widget_;
    QPushButton *lidar_remote_connect_btn_;
    QPushButton *lidar_remote_disconnect_btn_;
    QPushButton *lidar_remote_reconnect_btn_;
    QWidget *lidar_remote_buttons_widget_;
    QPushButton *imu_apply_btn_;
    QPushButton *imu_hi91_btn_;
    QPushButton *imu_hi92_btn_;
    QPushButton *imu_baud_115200_btn_;
    QPushButton *imu_baud_921600_btn_;
    QPushButton *imu_rate_100_btn_;
    QPushButton *imu_rate_200_btn_;
    QPushButton *imu_rate_500_btn_;
    QPushButton *imu_rate_1000_btn_;

    mutable std::mutex collector_mutex_;
    std::shared_ptr<VaporView::EpsilonCollector> epsilon_collector_;
    std::shared_ptr<VaporView::GnssCollector> gnss_collector_;
    std::shared_ptr<VaporView::ImuCollector> imu_collector_;
    std::shared_ptr<VaporView::PtbCollector> ptb_collector_;
    std::shared_ptr<VaporView::HmpCollector> hmp_collector_;
    std::shared_ptr<VaporView::LidarCollector> lidar_collector_;

    QTimer *refresh_timer_;

    VaporView::EpsilonData current_epsilon_;
    VaporView::GnssData current_gnss_;
    VaporView::ImuData current_imu_;
    VaporView::PtbData current_ptb_;
    VaporView::HmpData current_hmp_;
    VaporView::LidarData current_lidar_;

    bool is_fullscreen_;
    bool is_english_;
    bool has_inline_progress_log_;
    bool connection_attempt_in_progress_;
    bool port_detection_in_progress_;
    bool epsilon_reconfigure_in_progress_;
    bool is_connected_;
    bool remote_sky_mode_;
    bool remote_sky_online_;
    bool remote_wave_stream_requested_;
    bool remote_wave_stream_enable_pending_;
    bool remote_wave_stream_auto_start_;
    int remote_recording_state_;
    QHash<VaporView::SkyDeviceId, VaporView::DeviceState> remote_device_states_;
    QHash<VaporView::SkyDeviceId, qint64> remote_last_data_ms_;
    QHash<int, QVector<qint64>> remote_packet_arrivals_ms_;
    QHash<quint16, VaporView::PeakSearchRange> remote_peak_search_commands_;
    qint64 remote_last_status_ms_;
    VaporView::TelemetryStatus remote_status_;
    std::atomic<bool> cancel_connection_requested_;
    std::thread connection_thread_;
    std::thread port_detection_thread_;
    std::thread epsilon_reconfigure_thread_;
    std::thread recording_thread_;
    std::atomic<bool> recording_thread_running_;
    bool recording_paused_;
    int font_scale_percent_;
    bool dark_theme_enabled_;
    double base_font_point_size_;
    QString base_style_sheet_;
    QSize base_window_size_;
    QSize base_minimum_window_size_;
    int epsilon_sample_rate_;
    int gnss_sample_rate_;
    int imu_sample_rate_;
    int ptb_sample_rate_;
    int hmp_sample_rate_;
    int lidar_sample_rate_;
    int recording_export_rate_hz_;
    int imu_recording_rate_hz_;
    int waveform_recording_rate_hz_;
    int status_task_spinner_index_;
    std::chrono::steady_clock::time_point steady_clock_anchor_;
    std::chrono::system_clock::time_point system_clock_anchor_;
    std::unique_ptr<QFile> sensors_file_;
    std::unique_ptr<QFile> raw_epsilon_file_;
    std::unique_ptr<QFile> raw_ptb_file_;
    std::unique_ptr<QFile> raw_hmp_file_;
    std::unique_ptr<QFile> raw_lidar_file_;
    std::unique_ptr<QFile> raw_tcp_wave_file_;
    std::unique_ptr<QFile> event_log_file_;
    std::unique_ptr<QFile> error_log_file_;
    QString recording_directory_;
    QString session_directory_;
    QString session_name_;
    QString session_start_time_utc_;
    quint64 session_start_time_us_;
    QString sensors_filename_;
    QString raw_epsilon_filename_;
    QString raw_ptb_filename_;
    QString raw_hmp_filename_;
    QString raw_lidar_filename_;
    QString raw_tcp_wave_filename_;
    QString raw_dat_doc_filename_;
    QString session_metadata_filename_;
    QString event_log_filename_;
    QString error_log_filename_;
    QString device_config_filename_;
    std::atomic<qint64> recording_entry_count_;
    std::atomic<qint64> waveform_frame_count_;
    std::atomic<qint64> waveform_file_count_;
    std::atomic<quint64> raw_epsilon_record_count_;
    std::atomic<quint64> raw_ptb_record_count_;
    std::atomic<quint64> raw_hmp_record_count_;
    std::atomic<quint64> raw_lidar_record_count_;
    std::atomic<quint64> raw_tcp_wave_record_count_;
    std::atomic<quint64> last_imu_record_timestamp_us_;
    std::mutex recording_files_mutex_;

    QAction *rtk_config_action_;
    RtkConfigDialog *rtk_config_dialog_;
    TcpWavePanel *tcp_wave_panel_;
    SessionViewerWindow *session_viewer_window_;
    VaporView::GroundTelemetryService *ground_telemetry_service_;
    VaporView::SkyDeviceConfigDialog *sky_device_config_dialog_;
};

#endif

