#pragma once

#include "ground/main/MainWindow.h"

namespace VaporView::Ground::Main
{

enum class AppSidebarMode
{
    Collapsed,
    IconsOnly,
    Full
};

struct RemoteTelemetrySummarySections
{
    struct Item
    {
        QString label;
        QString value;
        QString valueWidthText;
        bool hasData = false;
    };

    QList<Item> rateItems;
    QList<Item> linkItems;
    QList<Item> deviceItems;
};

struct DeviceConfigPageWidgets
{
    QWidget *page = nullptr;
    QLabel *serial_title_lbl = nullptr;
    QLabel *data_source_mode_lbl = nullptr;
    QLabel *sky_telemetry_transport_lbl = nullptr;
    QLabel *sky_telemetry_port_lbl = nullptr;
    QLabel *sky_telemetry_baud_lbl = nullptr;
    QLabel *sky_telemetry_tcp_host_lbl = nullptr;
    QLabel *sky_telemetry_tcp_port_lbl = nullptr;
    QLabel *epsilon_lbl = nullptr;
    QLabel *ptb_lbl = nullptr;
    QLabel *hmp_lbl = nullptr;
    QLabel *lidar_lbl = nullptr;
    QLabel *temperature_lbl = nullptr;
    QLabel *epsilon_rate_lbl = nullptr;
    QLabel *ptb_rate_lbl = nullptr;
    QLabel *hmp_rate_lbl = nullptr;
    QLabel *lidar_rate_lbl = nullptr;
    QLabel *temperature_rate_lbl = nullptr;
    QWidget *sky_telemetry_row_widget = nullptr;
    QFrame *data_telemetry_summary_card = nullptr;
    QLabel *data_telemetry_summary_title_lbl = nullptr;
    QVBoxLayout *data_telemetry_rate_summary_layout = nullptr;
    QVBoxLayout *data_telemetry_link_summary_layout = nullptr;
    QVBoxLayout *data_telemetry_device_summary_layout = nullptr;
    QFrame *epsilon_config_card = nullptr;
    QLabel *epsilon_config_title_lbl = nullptr;
    QLabel *epsilon_config_hint_lbl = nullptr;
    QCheckBox *epsilon_packet_custom_check = nullptr;
    QVector<QLabel *> epsilon_packet_rate_labels;
    QVector<QComboBox *> epsilon_packet_rate_combos;
    QPushButton *epsilon_packet_defaults_btn = nullptr;
    QPushButton *epsilon_packet_grouped_btn = nullptr;
    QPushButton *epsilon_packet_save_btn = nullptr;
    QPushButton *auto_detect_ports_btn = nullptr;
    QPushButton *sky_device_config_btn = nullptr;
    QPushButton *epsilon_rtcm_port_btn = nullptr;
    QPushButton *epsilon_reconfigure_btn = nullptr;
    QPushButton *rtk_config_btn = nullptr;
    QComboBox *data_source_mode_combo = nullptr;
    QComboBox *sky_telemetry_transport_combo = nullptr;
    QComboBox *sky_telemetry_port_combo = nullptr;
    QComboBox *sky_telemetry_baud_combo = nullptr;
    QLineEdit *sky_telemetry_tcp_host_edit = nullptr;
    QSpinBox *sky_telemetry_tcp_port_spin = nullptr;
    QComboBox *epsilon_port_combo = nullptr;
    QComboBox *epsilon_baud_combo = nullptr;
    QComboBox *ptb_port_combo = nullptr;
    QComboBox *ptb_baud_combo = nullptr;
    QComboBox *ptb_source_combo = nullptr;
    QComboBox *hmp_port_combo = nullptr;
    QComboBox *hmp_baud_combo = nullptr;
    QComboBox *hmp_source_combo = nullptr;
    QComboBox *lidar_port_combo = nullptr;
    QComboBox *lidar_baud_combo = nullptr;
    QComboBox *temperature_port_combo = nullptr;
    QComboBox *temperature_baud_combo = nullptr;
    QComboBox *ptb_rate_combo = nullptr;
    QComboBox *hmp_rate_combo = nullptr;
    QComboBox *lidar_rate_combo = nullptr;
    QComboBox *temperature_rate_combo = nullptr;
    QToolButton *epsilon_remote_action_btn = nullptr;
    QWidget *epsilon_remote_buttons_widget = nullptr;
    QToolButton *ptb_remote_action_btn = nullptr;
    QWidget *ptb_remote_buttons_widget = nullptr;
    QToolButton *hmp_remote_action_btn = nullptr;
    QWidget *hmp_remote_buttons_widget = nullptr;
    QToolButton *lidar_remote_action_btn = nullptr;
    QWidget *lidar_remote_buttons_widget = nullptr;
    QToolButton *temperature_remote_action_btn = nullptr;
    QWidget *temperature_remote_buttons_widget = nullptr;
};

struct MainWindowState
{
    MainWindowState();
    ~MainWindowState();

    QWidget *central_widget_;
    QVBoxLayout *main_layout_;
    QFrame *window_border_top_;
    QFrame *window_border_right_;
    QFrame *window_border_bottom_;
    QFrame *window_border_left_;
    QVector<QWidget *> window_resize_handles_;
    QWidget *custom_title_bar_;
    QLabel *custom_logo_label_;
    QLabel *custom_title_label_;
    QToolButton *title_menu_btn_;
    QToolButton *title_language_btn_;
    QToolButton *log_side_panel_toggle_btn_;
    QToolButton *window_minimize_btn_;
    QToolButton *window_maximize_btn_;
    QToolButton *window_close_btn_;

    VaporView::Ground::Widgets::EpsilonPanel *epsilon_panel_;
    GnssPanel *gnss_panel_;
    ImuPanel *imu_panel_;
    PtbPanel *ptb_panel_;
    HmpPanel *hmp_panel_;
    LidarPanel *lidar_panel_;
    TemperatureControllerPanel *temperature_controller_panel_;
    std::unique_ptr<VaporView::Ground::Widgets::DevicePanelCoordinator> device_panel_coordinator_;

    QTextEdit *log_text_edit_;
    QToolButton *log_filter_btn_;
    QToolButton *log_clear_btn_;
    QLabel *status_label_;
    QProgressBar *status_task_progress_bar_;
    QLabel *status_task_spinner_label_;
    QTimer *status_task_spinner_timer_;
    QFrame *recording_status_card_;
    QLabel *recording_status_title_lbl_;
    QLabel *recording_status_label_;
    QPushButton *auto_detect_ports_btn_;

    QComboBox *epsilon_port_combo_;
    QComboBox *gnss_port_combo_;
    QComboBox *imu_port_combo_;
    QComboBox *ptb_port_combo_;
    QComboBox *hmp_port_combo_;
    QComboBox *lidar_port_combo_;
    QComboBox *temperature_port_combo_;
    QComboBox *epsilon_baud_combo_;
    QComboBox *gnss_baud_combo_;
    QComboBox *imu_baud_combo_;
    QComboBox *ptb_baud_combo_;
    QComboBox *hmp_baud_combo_;
    QComboBox *lidar_baud_combo_;
    QComboBox *temperature_baud_combo_;
    QAction *connect_btn_;
    QAction *cancel_connect_btn_;
    QAction *disconnect_btn_;
    QAction *scheduled_recording_action_;
    QAction *start_recording_btn_;
    QAction *pause_recording_btn_;
    QAction *stop_recording_btn_;
    QAction *refresh_ports_btn_;
    QAction *lang_action_;
    QAction *theme_toggle_action_;
    QAction *log_filter_ack_action_;
    QAction *log_filter_config_action_;
    QAction *log_filter_connection_action_;
    QAction *log_filter_recording_action_;
    QAction *clear_log_action_;
    QAction *session_viewer_action_;
#ifdef VAPORVIEW_HAS_OSGEARTH
    QAction *map3d_action_;
    QAction *map3d_diagnostics_action_;
#endif
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
    VaporView::SingleLevelPopupMenu *log_filter_menu_;
    QFrame *title_application_panel_;
    QFrame *title_application_sub_panel_;
    QFrame *title_application_nested_panel_;

    QSplitter *app_layout_splitter_;
    QSplitter *main_content_splitter_;
    QSplitter *home_overview_splitter_;
    QWidget *app_sidebar_;
    QButtonGroup *app_nav_button_group_;
    QPushButton *home_nav_btn_;
    QPushButton *temperature_nav_btn_;
    QPushButton *rtk_config_nav_btn_;
    QPushButton *device_config_nav_btn_;
    QStackedWidget *main_page_stack_;
    AppSidebarMode app_sidebar_mode_;
    bool app_sidebar_adjusting_;
    int app_sidebar_drag_width_;
    bool app_sidebar_drag_width_valid_;
    int last_app_sidebar_visible_width_;
    bool custom_logo_hovered_;
    QWidget *home_page_;
    QWidget *temperature_page_;
    DeviceConfigPageWidgets device_config_;
    QScrollArea *main_cards_scroll_area_;
    QGroupBox *config_group_;
    QGroupBox *data_group_;
    QWidget *sensor_row_widget_;
    QHBoxLayout *sensor_layout_;
    QWidget *log_side_panel_;
    QFrame *log_group_;
    QGroupBox *tcp_wave_group_;
    QGroupBox *epsilon_group_;
    QGroupBox *gnss_group_;
    QGroupBox *imu_group_;
    QGroupBox *ptb_group_;
    QGroupBox *hmp_group_;
    QGroupBox *env_group_;
    QGroupBox *temperature_overview_group_;
    QGroupBox *temperature_controller_group_;
    QGroupBox *lidar_group_;

    QLabel *epsilon_lbl_;
    QLabel *gnss_lbl_;
    QLabel *imu_lbl_;
    QLabel *ptb_lbl_;
    QLabel *hmp_lbl_;
    QLabel *lidar_lbl_;
    QLabel *temperature_lbl_;
    QLabel *home_epsilon_status_lbl_;
    QLabel *home_ptb_status_lbl_;
    QLabel *home_hmp_status_lbl_;
    QLabel *home_lidar_status_lbl_;
    QLabel *home_temperature_status_lbl_;
    QLabel *home_wave_status_lbl_;
    QToolButton *home_epsilon_action_btn_;
    QToolButton *home_ptb_action_btn_;
    QToolButton *home_hmp_action_btn_;
    QToolButton *home_lidar_action_btn_;
    QToolButton *home_temperature_action_btn_;
    QToolButton *home_wave_action_btn_;
    QWidget *data_telemetry_summary_card_;
    QVBoxLayout *data_telemetry_summary_layout_;
    QVBoxLayout *data_telemetry_link_summary_layout_;
    QVBoxLayout *data_telemetry_device_summary_layout_;
    QLabel *log_inline_title_lbl_;
    QLabel *epsilon_inline_title_lbl_;
    QLabel *gnss_inline_title_lbl_;
    QLabel *imu_inline_title_lbl_;
    QLabel *env_inline_title_lbl_;
    QLabel *env_lidar_status_icon_;
    QLabel *env_ptb_status_icon_;
    QLabel *env_hmp_status_icon_;
    QLabel *temperature_overview_inline_title_lbl_;
    QLabel *temperature_controller_inline_title_lbl_;
    QComboBox *temperature_title_port_combo_;
    VaporView::Ground::Widgets::TemperatureControllerOverviewPanel *temperature_overview_panel_;
    QLabel *config_inline_title_lbl_;
    QLabel *global_rate_lbl_;
    QLabel *epsilon_rate_lbl_;
    QLabel *gnss_rate_lbl_;
    QLabel *imu_rate_lbl_;
    QLabel *ptb_rate_lbl_;
    QLabel *hmp_rate_lbl_;
    QLabel *lidar_rate_lbl_;
    QLabel *temperature_rate_lbl_;
    QLabel *data_source_mode_lbl_;
    VaporView::Ground::Widgets::SourceModeOverviewSwitchButton *source_mode_switch_;
    QLabel *sky_telemetry_transport_lbl_;
    QLabel *sky_telemetry_port_lbl_;
    QLabel *sky_telemetry_baud_lbl_;
    QLabel *sky_telemetry_tcp_host_lbl_;
    QLabel *sky_telemetry_tcp_port_lbl_;
    QWidget *sky_telemetry_row_widget_;

    QComboBox *global_rate_combo_;
    QComboBox *epsilon_rate_combo_;
    QComboBox *gnss_rate_combo_;
    QComboBox *imu_rate_combo_;
    QComboBox *ptb_rate_combo_;
    QComboBox *hmp_rate_combo_;
    QComboBox *lidar_rate_combo_;
    QComboBox *temperature_rate_combo_;
    QComboBox *data_source_mode_combo_;
    QComboBox *sky_telemetry_transport_combo_;
    QComboBox *sky_telemetry_port_combo_;
    QComboBox *sky_telemetry_baud_combo_;
    QLineEdit *sky_telemetry_tcp_host_edit_;
    QSpinBox *sky_telemetry_tcp_port_spin_;
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
    QPushButton *temperature_remote_connect_btn_;
    QPushButton *temperature_remote_disconnect_btn_;
    QPushButton *temperature_remote_reconnect_btn_;
    QWidget *temperature_remote_buttons_widget_;
    QPushButton *imu_apply_btn_;
    QPushButton *imu_hi91_btn_;
    QPushButton *imu_hi92_btn_;
    QPushButton *imu_baud_115200_btn_;
    QPushButton *imu_baud_921600_btn_;
    QPushButton *imu_rate_100_btn_;
    QPushButton *imu_rate_200_btn_;
    QPushButton *imu_rate_500_btn_;
    QPushButton *imu_rate_1000_btn_;

    std::unique_ptr<VaporView::Ground::Devices::LocalDeviceConnectionController> local_connection_controller_;
    std::unique_ptr<VaporView::Ground::Session::GroundRecordingService> recording_service_;
    std::unique_ptr<VaporView::Ground::Session::RecordingScheduleController> recording_schedule_controller_;

    QTimer *refresh_timer_;
    QTimer *scheduled_recording_timer_;
    QTimer *home_device_action_spinner_timer_;

    VaporView::EpsilonData current_epsilon_;
    VaporView::GnssData current_gnss_;
    VaporView::ImuData current_imu_;
    VaporView::PtbData current_ptb_;
    VaporView::HmpData current_hmp_;
    VaporView::LidarData current_lidar_;
    VaporView::TemperatureControllerData current_temperature_controller_;

    bool is_english_;
    bool log_filter_ack_enabled_;
    bool log_filter_config_enabled_;
    bool log_filter_connection_enabled_;
    bool log_filter_recording_enabled_;
    bool language_switch_in_progress_;
    bool has_inline_progress_log_;
    bool connection_attempt_in_progress_;
    bool port_detection_in_progress_;
    bool epsilon_reconfigure_in_progress_;
    bool is_connected_;
    bool compact_home_layout_;
    bool responsive_home_layout_refresh_pending_;
    bool log_side_panel_width_initialized_;
    bool log_side_panel_collapsed_;
    int last_log_side_panel_width_;
    bool remote_sky_mode_;
    bool remote_sky_online_;
    bool remote_wave_stream_requested_;
    bool remote_wave_stream_enable_pending_;
    bool remote_wave_stream_auto_start_;
    int remote_recording_state_;
    QHash<VaporView::SkyDeviceId, qint64> home_device_action_spinner_until_ms_;
    std::unique_ptr<VaporView::Ground::Devices::RemoteSkyController> remote_sky_controller_;
    QHash<quint16, VaporView::TemperatureControllerCommand> remote_temperature_commands_;
    QHash<quint16, VaporView::PeakSearchRange> remote_peak_search_commands_;
#ifdef VAPORVIEW_HAS_OSGEARTH
    std::unique_ptr<VaporView::Ground::Map3DController> map3d_controller_;
#endif
    VaporView::TelemetryStatus remote_status_;
    VaporView::TelemetryStatus last_remote_recording_status_;
    bool has_last_remote_recording_status_;
    std::atomic<bool> cancel_connection_requested_;
    std::function<void(VaporView::CommandId)> local_temperature_command_test_observer_;
    std::thread port_detection_thread_;
    std::thread epsilon_reconfigure_thread_;
    int font_scale_percent_;
    bool dark_theme_enabled_;
    double base_font_point_size_;
    QString base_style_sheet_;
    QSize base_window_size_;
    QSize base_minimum_window_size_;
    QRect normal_window_geometry_;
    int epsilon_sample_rate_;
    int gnss_sample_rate_;
    int imu_sample_rate_;
    int ptb_sample_rate_;
    int hmp_sample_rate_;
    int lidar_sample_rate_;
    int temperature_sample_rate_;
    int recording_export_rate_hz_;
    int imu_recording_rate_hz_;
    int waveform_recording_rate_hz_;
    int status_task_spinner_index_;
    int home_device_action_spinner_step_;
    QVector<QString> log_entries_;
    QString recording_directory_;

    QAction *rtk_config_action_;
    RtkConfigDialog *rtk_config_dialog_;
    bool rtk_service_running_;
    TcpWavePanel *tcp_wave_panel_;
    SessionViewerWindow *session_viewer_window_;
    VaporView::SkyDeviceConfigDialog *sky_device_config_dialog_;
};

} // namespace VaporView::Ground::Main
