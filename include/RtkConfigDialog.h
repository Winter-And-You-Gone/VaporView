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
#include <QTimer>
#include <QSpinBox>
#include <QCheckBox>
#include <memory>

class RtkConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RtkConfigDialog(QWidget *parent = nullptr);
    ~RtkConfigDialog() override;

    void appendLog(const QString& message);
    bool isRunning() const;

private slots:
    void onStartClicked();
    void onStopClicked();
    void onTestClicked();
    void onProcessReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
    void onRefreshPortsClicked();
    void onSaveConfigClicked();
    void onLoadConfigClicked();
    void onClearLogClicked();

private:
    void setupUi();
    void loadSettings();
    void saveSettings();
    QString buildCommandLine() const;
    void updateButtonStates();
    QStringList getAvailablePorts() const;

    QLineEdit *server_edit_;
    QLineEdit *port_edit_;
    QLineEdit *username_edit_;
    QLineEdit *password_edit_;
    QLineEdit *mountpoint_edit_;
    QComboBox *output_port_combo_;
    QComboBox *baudrate_combo_;
    QSpinBox *timeout_spin_;
    QSpinBox *reconnect_spin_;
    QCheckBox *background_check_;
    QTextEdit *log_text_edit_;
    QPushButton *start_btn_;
    QPushButton *stop_btn_;
    QPushButton *test_btn_;
    QPushButton *refresh_ports_btn_;
    QPushButton *save_config_btn_;
    QPushButton *load_config_btn_;
    QPushButton *clear_log_btn_;
    QLabel *status_label_;

    QProcess *str2str_process_;
    bool is_running_;
    QString config_file_path_;
};

#endif
