#ifndef VAPROVIEW_RTK_CONFIG_DIALOG_H
#define VAPROVIEW_RTK_CONFIG_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTextEdit>
#include <QLabel>
#include <QGroupBox>
#include <QProcess>
#include <QSizePolicy>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QCheckBox>
#include <chrono>
#include <deque>
#include <memory>

#include "serial_port.h"

class RtkConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RtkConfigDialog(QWidget *parent = nullptr);
    ~RtkConfigDialog() override;

    void appendLog(const QString& message);
    bool isRunning() const;
    void setEnglish(bool english);
    void setFontScale(int percent);

private slots:
    void onStartClicked();
    void onStopClicked();
    void onTestClicked();
    void onGgaToggleClicked();
    void onProcessReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
    void onRefreshPortsClicked();
    void onFetchMountpointsClicked();
    void onSaveConfigClicked();
    void onLoadConfigClicked();
    void onClearLogClicked();
    void onGgaPollTimer();

private:
    void setupUi();
    void loadSettings();
    void saveSettings();
    QString buildCommandLine() const;
    void updateButtonStates();
    QStringList getAvailablePorts() const;
    QString textFor(const QString& english, const QString& chinese) const;
    int scalePixels(int pixels) const;
    void applyScaledUiMetrics();
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
    int currentGgaBaudrate() const;
    QString ggaPortName() const;
    void refreshPortCombos();

    QVBoxLayout *main_layout_;
    QGridLayout *config_layout_;
    QGridLayout *output_layout_;
    QHBoxLayout *button_layout_;
    QVBoxLayout *log_layout_;
    QHBoxLayout *log_button_layout_;
    QVBoxLayout *gga_layout_;
    QHBoxLayout *gga_header_layout_;
    QVBoxLayout *gga_text_container_layout_;
    QSpacerItem *gga_button_spacer_;
    QGroupBox *config_group_;
    QGroupBox *output_group_;
    QGroupBox *gga_group_;
    QGroupBox *log_group_;
    QWidget *gga_text_container_;
    QLabel *server_label_;
    QLabel *port_label_;
    QLabel *username_label_;
    QLabel *password_label_;
    QLabel *mountpoint_label_;
    QLabel *output_port_label_;
    QLabel *baudrate_label_;
    QLabel *timeout_label_;
    QLabel *reconnect_label_;
    QLabel *gga_port_info_label_;
    QLabel *gga_status_label_;
    QLabel *gga_frequency_label_;
    QLineEdit *server_edit_;
    QLineEdit *port_edit_;
    QLineEdit *username_edit_;
    QLineEdit *password_edit_;
    QLineEdit *mountpoint_edit_;
    QComboBox *output_port_combo_;
    QComboBox *baudrate_combo_;
    QComboBox *timeout_combo_;
    QComboBox *reconnect_combo_;
    QComboBox *gga_port_combo_;
    QCheckBox *background_check_;
    QTextEdit *gga_text_edit_;
    QTextEdit *log_text_edit_;
    QPushButton *start_btn_;
    QPushButton *stop_btn_;
    QPushButton *test_btn_;
    QPushButton *gga_toggle_btn_;
    QPushButton *refresh_ports_btn_;
    QPushButton *fetch_mountpoints_btn_;
    QPushButton *save_config_btn_;
    QPushButton *load_config_btn_;
    QPushButton *clear_log_btn_;
    QLabel *status_label_;

    QProcess *str2str_process_;
    bool is_running_;
    bool is_english_;
    int font_scale_percent_;
    QSize base_dialog_size_;
    QSize base_minimum_dialog_size_;
    QString config_file_path_;
    QTimer *gga_poll_timer_;
    VaproView::SerialPort gga_serial_;
    QString gga_buffer_;
    QString gga_status_message_;
    std::chrono::steady_clock::time_point gga_last_open_attempt_;
    std::chrono::steady_clock::time_point gga_last_sentence_time_;
    std::deque<double> gga_recent_intervals_sec_;
    bool gga_has_sentence_time_;
    bool gga_monitor_enabled_;
};

#endif
