#pragma once

#include "LogRecord.h"
#include "ground/devices/LocalDeviceConnectionController.h"
#include "ground/main/MainWindow.h"
#include "ground/main/UiLogModel.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QVariantMap>

namespace VaporView::Ground::Main
{

class RecordingStatusView;

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
        bool compactAvailabilityValue = false;
    };

    QList<Item> rateItems;
    QList<Item> linkItems;
    QList<Item> linkStatusItems;
    QList<Item> deviceItems;
};

struct UiTestWidgetStateEntry
{
    QPointer<QWidget> widget;
    QVariantMap state;
};

struct DeviceConfigPageWidgets
{
    QWidget *page = nullptr;
    QLabel *serial_title_lbl = nullptr;
    VaporView::Ground::Widgets::SourceModeOverviewSwitchButton *data_source_mode_switch = nullptr;
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
    QLabel *ai8_temperature_lbl = nullptr;
    QLabel *tcp_wave_lbl = nullptr;
    QLineEdit *tcp_wave_host_edit = nullptr;
    QSpinBox *tcp_wave_port_spin = nullptr;
    QLabel *tcp_wave_transport_hint_lbl = nullptr;
    QLabel *tcp_wave_enabled_hint_lbl = nullptr;
    QLabel *tcp_wave_source_hint_lbl = nullptr;
    QLabel *device_header_lbl = nullptr;
    QLabel *port_header_lbl = nullptr;
    QLabel *baud_header_lbl = nullptr;
    QLabel *rate_header_lbl = nullptr;
    QLabel *enabled_header_lbl = nullptr;
    QLabel *source_header_lbl = nullptr;
    QLabel *action_header_lbl = nullptr;
    QLabel *epsilon_rate_lbl = nullptr;
    QLabel *ptb_rate_lbl = nullptr;
    QLabel *hmp_rate_lbl = nullptr;
    QLabel *lidar_rate_lbl = nullptr;
    QLabel *temperature_rate_lbl = nullptr;
    QLabel *ai8_temperature_rate_lbl = nullptr;
    QWidget *sky_telemetry_row_widget = nullptr;
    QFrame *data_telemetry_summary_card = nullptr;
    QLabel *data_telemetry_summary_title_lbl = nullptr;
    QGroupBox *remote_sky_config_card = nullptr;
    QLabel *remote_sky_config_title_lbl = nullptr;
    QLabel *remote_sky_services_title_lbl = nullptr;
    QLabel *remote_sky_sync_title_lbl = nullptr;
    QLabel *remote_sky_advanced_title_lbl = nullptr;
    QLabel *remote_sky_config_status_lbl = nullptr;
    QLabel *remote_sky_wave_enabled_lbl = nullptr;
    QLabel *remote_sky_wave_host_lbl = nullptr;
    QLabel *remote_sky_wave_port_lbl = nullptr;
    QLabel *remote_sky_wave_downsample_lbl = nullptr;
    QLabel *remote_sky_telemetry_basic_lbl = nullptr;
    QLabel *remote_sky_telemetry_feature_lbl = nullptr;
    QLabel *remote_sky_telemetry_waveform_lbl = nullptr;
    QLabel *remote_sky_telemetry_heartbeat_lbl = nullptr;
    QLabel *remote_sky_telemetry_status_lbl = nullptr;
    QVBoxLayout *data_telemetry_rate_summary_layout = nullptr;
    QVBoxLayout *data_telemetry_link_summary_layout = nullptr;
    QVBoxLayout *data_telemetry_device_summary_layout = nullptr;
    QPushButton *auto_detect_ports_btn = nullptr;
    QComboBox *sky_telemetry_transport_combo = nullptr;
    QComboBox *sky_telemetry_port_combo = nullptr;
    QComboBox *sky_telemetry_baud_combo = nullptr;
    QLineEdit *sky_telemetry_tcp_host_edit = nullptr;
    QSpinBox *sky_telemetry_tcp_port_spin = nullptr;
    QComboBox *epsilon_port_combo = nullptr;
    QComboBox *epsilon_baud_combo = nullptr;
    QComboBox *epsilon_rate_combo = nullptr;
    QPushButton *epsilon_packet_rates_btn = nullptr;
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
    QComboBox *ai8_temperature_port_combo = nullptr;
    QComboBox *ai8_temperature_baud_combo = nullptr;
    QComboBox *ptb_rate_combo = nullptr;
    QComboBox *hmp_rate_combo = nullptr;
    QComboBox *lidar_rate_combo = nullptr;
    QComboBox *temperature_rate_combo = nullptr;
    QComboBox *ai8_temperature_rate_combo = nullptr;
    QCheckBox *epsilon_enabled_check = nullptr;
    QCheckBox *ptb_enabled_check = nullptr;
    QCheckBox *hmp_enabled_check = nullptr;
    QCheckBox *lidar_enabled_check = nullptr;
    QCheckBox *temperature_enabled_check = nullptr;
    QCheckBox *ai8_temperature_enabled_check = nullptr;
    QCheckBox *remote_sky_wave_enabled_check = nullptr;
    QLineEdit *remote_sky_wave_host_edit = nullptr;
    QSpinBox *remote_sky_wave_port_spin = nullptr;
    QSpinBox *remote_sky_wave_downsample_spin = nullptr;
    QDoubleSpinBox *remote_sky_telemetry_basic_spin = nullptr;
    QDoubleSpinBox *remote_sky_telemetry_feature_spin = nullptr;
    QDoubleSpinBox *remote_sky_telemetry_waveform_spin = nullptr;
    QDoubleSpinBox *remote_sky_telemetry_heartbeat_spin = nullptr;
    QDoubleSpinBox *remote_sky_telemetry_status_spin = nullptr;
    QPushButton *remote_sky_read_btn = nullptr;
    QPushButton *remote_sky_apply_btn = nullptr;
    QPushButton *remote_sky_save_btn = nullptr;
    QPushButton *remote_sky_raw_mode_btn = nullptr;
    QPlainTextEdit *remote_sky_raw_json_edit = nullptr;
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
    QToolButton *ai8_temperature_remote_action_btn = nullptr;
    QWidget *ai8_temperature_remote_buttons_widget = nullptr;
    QToolButton *tcp_wave_remote_action_btn = nullptr;
    QWidget *tcp_wave_remote_buttons_widget = nullptr;
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
    QLabel *ui_test_mode_badge_;
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
    VaporView::Ground::Widgets::Ai8TemperatureControllerPanel *ai8_temperature_controller_panel_;
    std::unique_ptr<VaporView::Ground::Widgets::DevicePanelCoordinator> device_panel_coordinator_;

    QListView *log_list_view_;
    QLineEdit *log_search_edit_;
    UiLogModel *log_model_;
    UiLogFilterProxyModel *log_filter_proxy_;
    UiLogItemDelegate *log_item_delegate_;
    QTimer *log_flush_timer_;
    QWidget *log_new_entries_row_;
    QWidget *epsilon_reconfigure_progress_row_;
    QLabel *epsilon_reconfigure_progress_label_;
    QProgressBar *epsilon_reconfigure_progress_bar_;
    QTimer *epsilon_reconfigure_progress_timer_;
    QPushButton *log_new_entries_btn_;
    QToolButton *log_search_btn_;
    QMenu *log_search_menu_;
    QToolButton *log_filter_btn_;
    QToolButton *log_clear_btn_;
    QFrame *recording_status_card_;
    QLabel *recording_status_title_lbl_;
    RecordingStatusView *recording_status_view_;
    QComboBox *gnss_port_combo_;
    QComboBox *imu_port_combo_;
    QComboBox *gnss_baud_combo_;
    QComboBox *imu_baud_combo_;
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
    QAction *log_filter_source_category_action_;
    QAction *log_filter_debug_action_;
    QAction *log_filter_warning_action_;
    QAction *log_filter_qt_action_;
    QAction *clear_log_action_;
    QAction *session_viewer_action_;
    QAction *ui_test_mode_action_;
    QActionGroup *ui_test_scenario_group_;
    QAction *ui_test_normal_action_;
    QAction *ui_test_partial_failure_action_;
    QAction *ui_test_stalled_action_;
#ifdef VAPORVIEW_HAS_OSGEARTH
    QAction *map3d_action_;
    QAction *map3d_diagnostics_action_;
#endif
    QAction *epsilon_reconfigure_action_;
    QAction *epsilon_rtcm_port_action_;
    QAction *epsilon_packet_rates_action_;
    QAction *recording_directory_action_;
    QAction *exit_action_;
    QAction *check_updates_action_;
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
    QMenu *developer_menu_;
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
    VaporView::Ground::Navigation::CombinationNavigationPage *combination_navigation_page_;
    VaporView::Ground::Navigation::EpsilonConfigPanel *epsilon_config_panel_;
    DeviceConfigPageWidgets device_config_;
    QScrollArea *main_cards_scroll_area_;
    QGroupBox *config_group_;
    QGroupBox *data_group_;
    QWidget *sensor_row_widget_;
    QHBoxLayout *sensor_layout_;
    QSplitter *sensor_card_splitter_;
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
    QGroupBox *ai8_temperature_controller_group_;
    QGroupBox *lidar_group_;

    QLabel *home_epsilon_status_lbl_;
    QLabel *home_ptb_status_lbl_;
    QLabel *home_hmp_status_lbl_;
    QLabel *home_lidar_status_lbl_;
    QLabel *home_temperature_status_lbl_;
    QLabel *home_wave_status_lbl_;
    QLabel *home_ai8_temperature_status_lbl_;
    QToolButton *home_epsilon_action_btn_;
    QToolButton *home_ptb_action_btn_;
    QToolButton *home_hmp_action_btn_;
    QToolButton *home_lidar_action_btn_;
    QToolButton *home_temperature_action_btn_;
    QToolButton *home_wave_action_btn_;
    QToolButton *home_ai8_temperature_action_btn_;
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
    QLabel *ai8_temperature_controller_inline_title_lbl_;
    QComboBox *temperature_title_port_combo_;
    QComboBox *ai8_temperature_title_port_combo_;
    QToolButton *temperature_title_action_btn_;
    QToolButton *ai8_temperature_title_action_btn_;
    QLabel *ai8_temperature_title_status_lbl_;
    VaporView::Ground::Widgets::TemperatureControllerOverviewPanel *temperature_overview_panel_;
    QLabel *config_inline_title_lbl_;
    VaporView::Ground::Widgets::SourceModeOverviewSwitchButton *source_mode_switch_;
    QLabel *sky_telemetry_transport_lbl_;
    QLabel *sky_telemetry_port_lbl_;
    QLabel *sky_telemetry_baud_lbl_;
    QLabel *sky_telemetry_tcp_host_lbl_;
    QLabel *sky_telemetry_tcp_port_lbl_;
    QWidget *sky_telemetry_row_widget_;

    QComboBox *data_source_mode_combo_;
    QComboBox *sky_telemetry_transport_combo_;
    QComboBox *sky_telemetry_port_combo_;
    QComboBox *sky_telemetry_baud_combo_;
    QLineEdit *sky_telemetry_tcp_host_edit_;
    QSpinBox *sky_telemetry_tcp_port_spin_;
    QComboBox *imu_format_combo_;
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
    std::unique_ptr<VaporView::Ground::Devices::LocalConnectionCoordinator> local_connection_coordinator_;
    VaporView::Ground::Devices::LocalDeviceConfig local_device_config_;
    std::unique_ptr<VaporView::Ground::Session::GroundRecordingService> recording_service_;
    std::unique_ptr<VaporView::Ground::Session::RecordingScheduleController> recording_schedule_controller_;

    QTimer *refresh_timer_;
    QTimer *scheduled_recording_timer_;
    QTimer *home_device_action_spinner_timer_;

    VaporView::EpsilonData current_epsilon_;
    quint32 current_remote_epsilon_validity_flags_ = 0;
    VaporView::GnssData current_gnss_;
    VaporView::ImuData current_imu_;
    VaporView::PtbData current_ptb_;
    VaporView::HmpData current_hmp_;
    VaporView::LidarData current_lidar_;
    VaporView::TemperatureControllerData current_temperature_controller_;

    bool is_english_;
    LogUiViewMode log_view_mode_;
    bool log_auto_follow_enabled_;
    bool log_hide_source_category_enabled_;
    int log_new_visible_count_;
    int log_unread_warning_count_;
    int log_unread_error_count_;
    int log_unread_status_count_;
    quint64 pending_ui_log_records_dropped_;
    bool log_bottom_follow_scheduled_;
    bool language_switch_in_progress_;
    bool restoring_persistent_settings_;
    bool has_inline_progress_log_;
    bool connection_attempt_in_progress_;
    bool port_detection_in_progress_;
    bool epsilon_reconfigure_in_progress_;
    bool epsilon_reconfigure_progress_visible_;
    int epsilon_reconfigure_progress_current_;
    int epsilon_reconfigure_progress_total_;
    QString epsilon_reconfigure_progress_stage_;
    QElapsedTimer epsilon_reconfigure_progress_elapsed_;
    bool is_connected_;
    bool ui_test_mode_enabled_;
    bool ui_test_application_closing_;
    bool ui_test_connection_in_progress_;
    int ui_test_recording_state_;
    qint64 ui_test_recording_started_ms_;
    qint64 ui_test_recording_elapsed_ms_;
    qint64 ui_test_started_ms_;
    int ui_test_saved_page_index_;
    int ui_test_saved_sidebar_width_;
    int ui_test_saved_font_scale_percent_;
    bool ui_test_saved_dark_theme_enabled_;
    bool ui_test_session_viewer_existed_;
#ifdef VAPORVIEW_HAS_OSGEARTH
    bool ui_test_map3d_window_existed_;
#endif
    QString ui_test_saved_recording_directory_;
    QVector<QPointer<QWidget>> ui_test_existing_top_levels_;
    QVector<UiTestWidgetStateEntry> ui_test_widget_states_;
    std::unique_ptr<VaporView::Ground::Devices::UiTestDataModel> ui_test_model_;
    bool compact_home_layout_;
    bool responsive_home_layout_refresh_pending_;
    bool log_side_panel_width_initialized_;
    bool log_side_panel_collapsed_;
    int last_log_side_panel_width_;
    bool remote_sky_mode_;
    bool remote_sky_online_;
    VaporView::SkyConfig remote_sky_config_;
    VaporView::SkyConfig remote_sky_baseline_config_;
    bool remote_sky_config_loaded_;
    bool remote_sky_config_dirty_;
    bool remote_sky_config_loading_;
    bool remote_sky_config_applying_;
    bool remote_sky_config_saving_;
    bool remote_sky_config_raw_mode_;
    bool remote_sky_config_updating_ui_;
    bool remote_serial_detection_pending_ = false;
    quint16 remote_serial_detection_seq_ = 0;
    quint16 remote_serial_detection_cancel_seq_ = 0;
    QString remote_sky_config_status_text_;
    bool remote_sky_config_status_error_;
    quint16 remote_sky_config_read_seq_;
    quint16 remote_sky_config_apply_seq_;
    quint16 remote_sky_config_save_seq_;
    quint64 remote_sky_config_read_generation_;
    quint64 remote_sky_config_apply_generation_;
    quint64 remote_sky_config_loaded_generation_;
    bool remote_wave_stream_requested_;
    bool remote_wave_stream_enable_pending_;
    bool remote_wave_stream_auto_start_;
    bool remote_wave_connect_after_config_read_;
    bool remote_wave_connect_after_config_apply_;
    QString remote_wave_pending_host_;
    int remote_wave_pending_port_;
    int remote_recording_state_;
    QHash<VaporView::SkyDeviceId, qint64> home_device_action_spinner_until_ms_;
    QHash<VaporView::SkyDeviceId, qint64> home_device_action_spinner_started_ms_;
    std::unique_ptr<VaporView::Ground::Devices::RemoteSkyController> remote_sky_controller_;
    std::unique_ptr<VaporView::Ground::Devices::Ai8DeviceSession> ai8_device_session_;
    std::unique_ptr<VaporView::Ground::Devices::EpsilonDeviceSession> epsilon_device_session_;
    std::unique_ptr<VaporView::Ground::Devices::Rd105DeviceSession> rd105_device_session_;
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
    QVector<VaporView::LogRecord> pending_ui_log_records_;
    QString recording_directory_;

    QAction *rtk_config_action_;
    RtkConfigDialog *rtk_config_dialog_;
    bool rtk_service_running_;
    TcpWavePanel *tcp_wave_panel_;
    SessionViewerWindow *session_viewer_window_;
};

} // namespace VaporView::Ground::Main
