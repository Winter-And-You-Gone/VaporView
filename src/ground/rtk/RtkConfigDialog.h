#ifndef VaporView_RTK_CONFIG_DIALOG_H
#define VaporView_RTK_CONFIG_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTextEdit>
#include <QLabel>
#include <QGroupBox>
#include <QToolButton>
#include <QSizePolicy>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>

#include "data_types.h"
#include "shared/theme/AppTheme.h"
#include "serial_port.h"
#include "RtkStreamService.h"

class QCloseEvent;
class QEvent;

namespace VaporView { class SingleLevelPopupMenu; }

class RtkConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RtkConfigDialog(QWidget *parent = nullptr, bool embedded = false);
    ~RtkConfigDialog() override;

    void appendLog(const QString& message);
    void appendRawLogLine(const QString& line);
    bool isRunning() const;
    void setEnglish(bool english);
    void setFontScale(int percent);
    void setPreferredOutputPortAndBaud(const QString& portName, const QString& baudText);
    void setEpsilonMainPortAndBaud(const QString& portName, const QString& baudText);
    void setEpsilonDataProvider(std::function<VaporView::EpsilonData()> provider);
    using EpsilonLeverArmCompletion = std::function<void(bool, const QString&)>;
    using EpsilonLeverArmApplier = std::function<void(double, double, double, EpsilonLeverArmCompletion)>;
    void setEpsilonMainAntennaLeverArmApplier(EpsilonLeverArmApplier applier);

signals:
    void rtkRunningChanged(bool running);

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private slots:
    void onStartClicked();
    void onStopClicked();
    void onTestClicked();
    void onGgaToggleClicked();
    void onRtkStatusTimer();
    void onRefreshPortsClicked();
    void onFetchMountpointsClicked();
    void onAutoDetectPortsClicked();
    void onApplyMainAntennaLeverArmClicked();
    void onMainAntennaLeverHelpClicked();
    void onClearLogClicked();
    void onGgaPollTimer();

private:
    void setupUi();
    void loadSettings();
    void saveSettings();
    bool buildRtkStreamConfig(RtkStreamConfig *config,
                              QString *description = nullptr,
                              QString *validationError = nullptr) const;
    void updateButtonStates();
    QStringList getAvailablePorts() const;
    QString textFor(const QString& english, const QString& chinese) const;
    int scalePixels(int pixels) const;
    void applyScaledUiMetrics();
    void updateMountpointComboWidth();
    void startGgaMonitor();
    void stopGgaMonitor();
    void updateGgaMonitorText();
    void updateGgaFrequency(double hz);
    void updateGgaStatusLabel(const QString& message, bool healthy);
    void updateGgaMonitorButton();
    void processGgaBuffer();
    void handleGgaSentence(const QString& sentence);
    void trimGgaDisplay();
    bool tryOpenGgaPort();
    bool isMainGgaSourceSelected() const;
    QString mainGgaSourceLabel() const;
    QString savedGgaSourceValue() const;
    void applySavedGgaSource(const QString& source);
    void pollMainGgaSource();
    int currentGgaBaudrate() const;
    int currentOutputBaudrate() const;
    QString ggaPortName() const;
    void refreshPortCombos();
    void pollRtkServiceStatus(bool forceLog = false);
    void joinBackgroundTasks();
    bool isBackgroundTaskRunning() const;
    bool sendReceiverCommands(const QStringList& commands, QString *errorMessage = nullptr);
    bool parseMainAntennaLeverArm(double *x, double *y, double *z, QString *errorMessage = nullptr) const;
    QString mainAntennaLeverArmHelpText() const;
    void applyDetectedOutputAndGgaPort(const QString& portName, const QString& baudText);
    void setServiceStatus(const QString& text, const QString& iconName, VaporView::AppThemeColor color);
    void refreshServiceStatusAppearance();
    QVBoxLayout *createCardLayout(QGroupBox *group,
                                  QLabel *&titleLabel,
                                  const QString& iconName,
                                  QWidget **titleBarOut = nullptr);

    QVBoxLayout *main_layout_;
    QGridLayout *config_layout_;
    QVBoxLayout *output_layout_;
    QHBoxLayout *button_layout_;
    QVBoxLayout *log_layout_;
    QHBoxLayout *log_button_layout_;
    QHBoxLayout *gga_layout_;
    QVBoxLayout *gga_controls_layout_;
    QGridLayout *gga_header_layout_;
    QVBoxLayout *gga_text_container_layout_;
    QVBoxLayout *log_text_container_layout_;
    QSpacerItem *gga_button_spacer_;
    QGroupBox *config_group_;
    QGroupBox *output_group_;
    QGroupBox *gga_group_;
    QGroupBox *log_group_;
    QGroupBox *action_group_;
    QLabel *config_title_label_;
    QLabel *output_title_label_;
    QLabel *gga_title_label_;
    QLabel *log_title_label_;
    QLabel *action_title_label_;
    QWidget *action_status_widget_;
    QWidget *gga_text_container_;
    QWidget *gga_controls_container_;
    QWidget *log_text_container_;
    QLabel *server_label_;
    QLabel *port_label_;
    QLabel *username_label_;
    QLabel *password_label_;
    QLabel *mountpoint_label_;
    QLabel *output_port_label_;
    QLabel *baudrate_label_;
    QLabel *main_antenna_lever_label_;
    QLabel *timeout_label_;
    QLabel *reconnect_label_;
    QLabel *gga_port_info_label_;
    QLabel *gga_status_label_;
    QLabel *gga_frequency_label_;
    QLineEdit *server_edit_;
    QLineEdit *port_edit_;
    QLineEdit *username_edit_;
    QLineEdit *password_edit_;
    QComboBox *mountpoint_combo_;
    QLineEdit *main_antenna_lever_x_edit_;
    QLineEdit *main_antenna_lever_y_edit_;
    QLineEdit *main_antenna_lever_z_edit_;
    QComboBox *output_port_combo_;
    QComboBox *baudrate_combo_;
    QComboBox *timeout_combo_;
    QComboBox *reconnect_combo_;
    QComboBox *gga_port_combo_;
    QTextEdit *gga_text_edit_;
    QTextEdit *log_text_edit_;
    QPushButton *start_btn_;
    QPushButton *stop_btn_;
    QPushButton *test_btn_;
    QPushButton *gga_toggle_btn_;
    QPushButton *refresh_ports_btn_;
    QPushButton *auto_detect_ports_btn_;
    QPushButton *fetch_mountpoints_btn_;
    QToolButton *main_antenna_lever_help_btn_;
    VaporView::SingleLevelPopupMenu *main_antenna_lever_help_popup_;
    QLabel *main_antenna_lever_help_popup_label_;
    QPushButton *apply_main_antenna_lever_btn_;
    QPushButton *clear_log_btn_;
    QLabel *status_icon_label_;
    QLabel *status_label_;
    QString service_status_icon_name_;
    VaporView::AppThemeColor service_status_color_;

    bool embedded_;
    std::unique_ptr<RtkStreamService> rtk_service_;
    bool is_running_;
    bool is_english_;
    int font_scale_percent_;
    std::function<VaporView::EpsilonData()> epsilon_data_provider_;
    EpsilonLeverArmApplier epsilon_main_antenna_lever_arm_applier_;
    QString epsilon_main_port_;
    int epsilon_main_baudrate_;
    QSize base_dialog_size_;
    QSize base_minimum_dialog_size_;
    QString config_file_path_;
    QTimer *rtk_status_timer_;
    QTimer *gga_poll_timer_;
    QString last_rtk_status_message_;
    VaporView::SerialPort gga_serial_;
    QString gga_buffer_;
    QString gga_status_message_;
    bool gga_status_healthy_;
    std::chrono::steady_clock::time_point gga_last_open_attempt_;
    std::chrono::steady_clock::time_point gga_last_sentence_time_;
    std::chrono::steady_clock::time_point gga_last_epsilon_sample_time_;
    uint64_t gga_last_epsilon_device_timestamp_us_;
    std::deque<double> gga_recent_intervals_sec_;
    bool gga_has_sentence_time_;
    bool gga_monitor_enabled_;
    bool metrics_refresh_pending_;
    std::atomic<bool> fetch_mountpoints_in_progress_{false};
    std::atomic<bool> port_detection_in_progress_{false};
    std::atomic<bool> test_in_progress_{false};
    bool lever_arm_apply_in_progress_ = false;
    std::atomic<bool> shutdown_requested_{false};
    std::thread fetch_mountpoints_thread_;
    std::thread port_detection_thread_;
    std::thread test_thread_;
};

#endif

