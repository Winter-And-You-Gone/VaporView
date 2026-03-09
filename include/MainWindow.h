#ifndef VAPROVIEW_MAIN_WINDOW_H_
#define VAPROVIEW_MAIN_WINDOW_H_

#include "data_collector.h"
#include "data_types.h"
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
#include <QComboBox>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTimer>
#include <QAction>
#include <QScrollArea>
#include <memory>

class RtkConfigDialog;

class GnssPanel : public QWidget
{
    Q_OBJECT

public:
    explicit GnssPanel(QWidget *parent = nullptr);
    void updateData(const VaproView::GnssData& data);
    void updateRate(double hz);
    void setEnglish(bool english);

private:
    void setupUi();

    QLabel *rate_label_;
    QLabel *status_label_;
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
    void updateData(const VaproView::ImuData& data);
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

    QLabel *source_lbl_;
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
    void updateData(const VaproView::PtbData& data);
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
    void updateData(const VaproView::HmpData& data);
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

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onConnectClicked();
    void onDisconnectClicked();
    void onGnssDataReady();
    void onImuDataReady();
    void onPtbDataReady();
    void onHmpDataReady();
    void onRefreshTimer();
    void onExportClicked();
    void onClearLogClicked();
    void onRefreshPortsClicked();
    void onToggleFullScreen();
    void onSwitchLanguage();
    void onGlobalRateChanged(const QString& text);
    void onGnssRateChanged(const QString& text);
    void onImuRateChanged(const QString& text);
    void onPtbRateChanged(const QString& text);
    void onHmpRateChanged(const QString& text);
    void onRtkConfigClicked();

private:
    void setupMenuBar();
    void setupToolBar();
    void setupStatusBar();
    void setupCentralWidget();
    void setupConfigPanel();
    void setupDataPanels();
    void setupLogPanel();
    void loadModernStyleSheet();
    void log(const QString& message);
    void updateConnectionStatus(bool connected);
    QStringList getAvailablePorts();
    void setEnglish(bool english);
    void applyAllSampleRates();
    int parseRate(const QString& text);

    QWidget *central_widget_;
    QVBoxLayout *main_layout_;

    GnssPanel *gnss_panel_;
    ImuPanel *imu_panel_;
    PtbPanel *ptb_panel_;
    HmpPanel *hmp_panel_;

    QTextEdit *log_text_edit_;
    QLabel *status_label_;

    QComboBox *gnss_port_combo_;
    QComboBox *imu_port_combo_;
    QComboBox *ptb_port_combo_;
    QComboBox *hmp_port_combo_;
    QComboBox *gnss_baud_combo_;
    QComboBox *imu_baud_combo_;
    QComboBox *ptb_baud_combo_;
    QComboBox *hmp_baud_combo_;
    QAction *connect_btn_;
    QAction *disconnect_btn_;
    QAction *export_btn_;
    QAction *refresh_ports_btn_;
    QAction *fullscreen_btn_;
    QAction *lang_action_;
    QAction *clear_log_action_;
    QAction *export_action_;
    QAction *exit_action_;
    QAction *about_action_;

    QGroupBox *config_group_;
    QGroupBox *data_group_;
    QGroupBox *log_group_;
    QGroupBox *gnss_group_;
    QGroupBox *imu_group_;
    QGroupBox *ptb_group_;
    QGroupBox *hmp_group_;
    QGroupBox *env_group_;

    QLabel *gnss_lbl_;
    QLabel *imu_lbl_;
    QLabel *ptb_lbl_;
    QLabel *hmp_lbl_;
    QLabel *global_rate_lbl_;
    QLabel *gnss_rate_lbl_;
    QLabel *imu_rate_lbl_;
    QLabel *ptb_rate_lbl_;
    QLabel *hmp_rate_lbl_;

    QComboBox *global_rate_combo_;
    QComboBox *gnss_rate_combo_;
    QComboBox *imu_rate_combo_;
    QComboBox *ptb_rate_combo_;
    QComboBox *hmp_rate_combo_;

    std::unique_ptr<VaproView::GnssCollector> gnss_collector_;
    std::unique_ptr<VaproView::ImuCollector> imu_collector_;
    std::unique_ptr<VaproView::PtbCollector> ptb_collector_;
    std::unique_ptr<VaproView::HmpCollector> hmp_collector_;

    QTimer *refresh_timer_;

    VaproView::GnssData current_gnss_;
    VaproView::ImuData current_imu_;
    VaproView::PtbData current_ptb_;
    VaproView::HmpData current_hmp_;

    bool is_fullscreen_;
    bool is_english_;
    int gnss_sample_rate_;
    int imu_sample_rate_;
    int ptb_sample_rate_;
    int hmp_sample_rate_;

    QAction *rtk_config_action_;
    RtkConfigDialog *rtk_config_dialog_;
};

#endif
