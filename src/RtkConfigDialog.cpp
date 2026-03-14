#include "RtkConfigDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QRegularExpression>
#include <QSerialPortInfo>

RtkConfigDialog::RtkConfigDialog(QWidget *parent)
    : QDialog(parent)
    , server_edit_(nullptr)
    , port_edit_(nullptr)
    , username_edit_(nullptr)
    , password_edit_(nullptr)
    , mountpoint_edit_(nullptr)
    , output_port_combo_(nullptr)
    , baudrate_combo_(nullptr)
    , timeout_spin_(nullptr)
    , reconnect_spin_(nullptr)
    , background_check_(nullptr)
    , log_text_edit_(nullptr)
    , start_btn_(nullptr)
    , stop_btn_(nullptr)
    , test_btn_(nullptr)
    , refresh_ports_btn_(nullptr)
    , save_config_btn_(nullptr)
    , load_config_btn_(nullptr)
    , clear_log_btn_(nullptr)
    , status_label_(nullptr)
    , str2str_process_(nullptr)
    , is_running_(false)
{
    setupUi();
    loadSettings();
    updateButtonStates();

    setWindowTitle(tr("RTK NTRIP Configuration"));
    setMinimumSize(560, 680);
    resize(620, 740);

    config_file_path_ = QDir::homePath() + "/.config/VaproView/rtk_config.ini";
}

RtkConfigDialog::~RtkConfigDialog()
{
    if (str2str_process_)
    {
        if (str2str_process_->state() == QProcess::Running)
        {
            str2str_process_->terminate();
            if (!str2str_process_->waitForFinished(3000))
            {
                str2str_process_->kill();
            }
        }
        delete str2str_process_;
    }
    saveSettings();
}

void RtkConfigDialog::setupUi()
{
    auto *main_layout = new QVBoxLayout(this);
    main_layout->setSpacing(8);
    main_layout->setContentsMargins(12, 12, 12, 12);

    auto *config_group = new QGroupBox(tr("NTRIP Server Configuration"), this);
    auto *config_layout = new QGridLayout(config_group);
    config_layout->setSpacing(6);

    int row = 0;
    config_layout->addWidget(new QLabel(tr("Server:"), this), row, 0);
    server_edit_ = new QLineEdit(this);
    server_edit_->setPlaceholderText(tr("e.g. rtk.ntrip.org"));
    config_layout->addWidget(server_edit_, row, 1);

    config_layout->addWidget(new QLabel(tr("Port:"), this), row, 2);
    port_edit_ = new QLineEdit(this);
    port_edit_->setText("2101");
    port_edit_->setMaximumWidth(80);
    config_layout->addWidget(port_edit_, row, 3);
    row++;

    config_layout->addWidget(new QLabel(tr("Username:"), this), row, 0);
    username_edit_ = new QLineEdit(this);
    config_layout->addWidget(username_edit_, row, 1, 1, 3);
    row++;

    config_layout->addWidget(new QLabel(tr("Password:"), this), row, 0);
    password_edit_ = new QLineEdit(this);
    password_edit_->setEchoMode(QLineEdit::Password);
    config_layout->addWidget(password_edit_, row, 1, 1, 3);
    row++;

    config_layout->addWidget(new QLabel(tr("Mountpoint:"), this), row, 0);
    mountpoint_edit_ = new QLineEdit(this);
    mountpoint_edit_->setPlaceholderText(tr("e.g. RTCM33"));
    config_layout->addWidget(mountpoint_edit_, row, 1, 1, 3);
    row++;

    main_layout->addWidget(config_group);

    auto *output_group = new QGroupBox(tr("RTCM Output Configuration"), this);
    auto *output_layout = new QGridLayout(output_group);
    output_layout->setSpacing(6);

    row = 0;
    output_layout->addWidget(new QLabel(tr("Output Port:"), this), row, 0);
    output_port_combo_ = new QComboBox(this);
    output_port_combo_->setEditable(true);
    output_layout->addWidget(output_port_combo_, row, 1);

    refresh_ports_btn_ = new QPushButton(tr("Refresh"), this);
    refresh_ports_btn_->setFixedWidth(80);
    connect(refresh_ports_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onRefreshPortsClicked);
    output_layout->addWidget(refresh_ports_btn_, row, 2);
    row++;

    output_layout->addWidget(new QLabel(tr("Baudrate:"), this), row, 0);
    baudrate_combo_ = new QComboBox(this);
    baudrate_combo_->addItems({"9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600"});
    baudrate_combo_->setCurrentText("115200");
    output_layout->addWidget(baudrate_combo_, row, 1, 1, 2);
    row++;

    output_layout->addWidget(new QLabel(tr("Timeout (ms):"), this), row, 0);
    timeout_spin_ = new QSpinBox(this);
    timeout_spin_->setRange(1000, 60000);
    timeout_spin_->setValue(5000);
    timeout_spin_->setSingleStep(1000);
    output_layout->addWidget(timeout_spin_, row, 1, 1, 2);
    row++;

    output_layout->addWidget(new QLabel(tr("Reconnect (ms):"), this), row, 0);
    reconnect_spin_ = new QSpinBox(this);
    reconnect_spin_->setRange(1000, 60000);
    reconnect_spin_->setValue(1000);
    reconnect_spin_->setSingleStep(1000);
    output_layout->addWidget(reconnect_spin_, row, 1, 1, 2);
    row++;

    background_check_ = new QCheckBox(tr("Run in background"), this);
    output_layout->addWidget(background_check_, row, 0, 1, 3);

    main_layout->addWidget(output_group);

    auto *btn_layout = new QHBoxLayout();
    btn_layout->setSpacing(6);

    start_btn_ = new QPushButton(tr("Start"), this);
    start_btn_->setFixedWidth(80);
    connect(start_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onStartClicked);

    stop_btn_ = new QPushButton(tr("Stop"), this);
    stop_btn_->setFixedWidth(80);
    stop_btn_->setEnabled(false);
    connect(stop_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onStopClicked);

    test_btn_ = new QPushButton(tr("Test Connection"), this);
    test_btn_->setFixedWidth(120);
    connect(test_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onTestClicked);

    save_config_btn_ = new QPushButton(tr("Save Config"), this);
    save_config_btn_->setFixedWidth(100);
    connect(save_config_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onSaveConfigClicked);

    load_config_btn_ = new QPushButton(tr("Load Config"), this);
    load_config_btn_->setFixedWidth(100);
    connect(load_config_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onLoadConfigClicked);

    btn_layout->addWidget(start_btn_);
    btn_layout->addWidget(stop_btn_);
    btn_layout->addWidget(test_btn_);
    btn_layout->addStretch();
    btn_layout->addWidget(save_config_btn_);
    btn_layout->addWidget(load_config_btn_);

    main_layout->addLayout(btn_layout);

    auto *log_group = new QGroupBox(tr("RTK Service Log"), this);
    auto *log_layout = new QVBoxLayout(log_group);
    log_layout->setSpacing(4);

    log_text_edit_ = new QTextEdit(this);
    log_text_edit_->setReadOnly(true);
    log_text_edit_->setStyleSheet(
        "QTextEdit { background-color: #ffffff; color: #222222; "
        "font-family: Consolas, Monaco, monospace; font-size: 13px; }"
    );
    log_layout->addWidget(log_text_edit_);

    auto *log_btn_layout = new QHBoxLayout();
    clear_log_btn_ = new QPushButton(tr("Clear Log"), this);
    clear_log_btn_->setFixedWidth(80);
    connect(clear_log_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onClearLogClicked);
    log_btn_layout->addStretch();
    log_btn_layout->addWidget(clear_log_btn_);
    log_layout->addLayout(log_btn_layout);

    main_layout->addWidget(log_group, 1);

    status_label_ = new QLabel(tr("Status: Stopped"), this);
    status_label_->setStyleSheet("QLabel { color: #666666; font-weight: bold; }");
    main_layout->addWidget(status_label_);
}

void RtkConfigDialog::loadSettings()
{
    QSettings settings("VaproView", "RtkConfig");

    server_edit_->setText(settings.value("server", "").toString());
    port_edit_->setText(settings.value("port", "2101").toString());
    username_edit_->setText(settings.value("username", "").toString());
    password_edit_->setText(settings.value("password", "").toString());
    mountpoint_edit_->setText(settings.value("mountpoint", "").toString());
#ifdef _WIN32
    output_port_combo_->setCurrentText(settings.value("output_port", "COM3").toString());
#else
    output_port_combo_->setCurrentText(settings.value("output_port", "/dev/ttyCOM3").toString());
#endif
    baudrate_combo_->setCurrentText(settings.value("baudrate", "115200").toString());
    timeout_spin_->setValue(settings.value("timeout", 5000).toInt());
    reconnect_spin_->setValue(settings.value("reconnect", 1000).toInt());
    background_check_->setChecked(settings.value("background", false).toBool());
}

void RtkConfigDialog::saveSettings()
{
    QSettings settings("VaproView", "RtkConfig");

    settings.setValue("server", server_edit_->text());
    settings.setValue("port", port_edit_->text());
    settings.setValue("username", username_edit_->text());
    settings.setValue("password", password_edit_->text());
    settings.setValue("mountpoint", mountpoint_edit_->text());
    settings.setValue("output_port", output_port_combo_->currentText());
    settings.setValue("baudrate", baudrate_combo_->currentText());
    settings.setValue("timeout", timeout_spin_->value());
    settings.setValue("reconnect", reconnect_spin_->value());
    settings.setValue("background", background_check_->isChecked());
}

QString RtkConfigDialog::buildCommandLine() const
{
    QString server = server_edit_->text().trimmed();
    QString port = port_edit_->text().trimmed();
    QString username = username_edit_->text().trimmed();
    QString password = password_edit_->text();
    QString mountpoint = mountpoint_edit_->text().trimmed();
    QString output_port = output_port_combo_->currentText().trimmed();
    QString baudrate = baudrate_combo_->currentText();
    int timeout = timeout_spin_->value();
    int reconnect = reconnect_spin_->value();

    if (server.isEmpty() || mountpoint.isEmpty() || output_port.isEmpty())
    {
        return QString();
    }

    QString ntrip_url;
    if (!username.isEmpty())
    {
        ntrip_url = QString("ntrip://%1:%2@%3:%4/%5")
            .arg(username)
            .arg(password)
            .arg(server)
            .arg(port)
            .arg(mountpoint);
    }
    else
    {
        ntrip_url = QString("ntrip://%1:%2/%3")
            .arg(server)
            .arg(port)
            .arg(mountpoint);
    }

    QString serial_url = QString("serial://%1:%2:8:n:1:off")
        .arg(output_port)
        .arg(baudrate);

    QString cmd = QString("str2str -in %1 -out %2 -s %3 -r %4")
        .arg(ntrip_url)
        .arg(serial_url)
        .arg(timeout)
        .arg(reconnect);

    if (background_check_->isChecked())
    {
        cmd += " -b 1";
    }

    return cmd;
}

void RtkConfigDialog::updateButtonStates()
{
    start_btn_->setEnabled(!is_running_);
    stop_btn_->setEnabled(is_running_);
    test_btn_->setEnabled(!is_running_);

    if (is_running_)
    {
        status_label_->setText(tr("Status: Running"));
        status_label_->setStyleSheet("QLabel { color: #43a047; font-weight: bold; }");
    }
    else
    {
        status_label_->setText(tr("Status: Stopped"));
        status_label_->setStyleSheet("QLabel { color: #666666; font-weight: bold; }");
    }
}

QStringList RtkConfigDialog::getAvailablePorts() const
{
    QStringList ports;
    const auto infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo& info : infos)
    {
        ports.append(info.portName());
    }

    ports.removeDuplicates();
    ports.sort();
    return ports;
}

void RtkConfigDialog::onRefreshPortsClicked()
{
    QStringList ports = getAvailablePorts();
    QString current = output_port_combo_->currentText();

    output_port_combo_->clear();
    output_port_combo_->addItems(ports);

    int idx = output_port_combo_->findText(current);
    if (idx >= 0)
    {
        output_port_combo_->setCurrentIndex(idx);
    }
    else if (!current.isEmpty())
    {
        output_port_combo_->setEditText(current);
    }

    appendLog(tr("Ports refreshed: %1 found").arg(ports.size()));
}

void RtkConfigDialog::onStartClicked()
{
    QString cmd = buildCommandLine();
    if (cmd.isEmpty())
    {
        QMessageBox::warning(this, tr("Error"), tr("Please fill in server, mountpoint and output port."));
        return;
    }

    if (str2str_process_)
    {
        delete str2str_process_;
        str2str_process_ = nullptr;
    }

    str2str_process_ = new QProcess(this);
    connect(str2str_process_, &QProcess::readyReadStandardOutput, this, &RtkConfigDialog::onProcessReadyRead);
    connect(str2str_process_, &QProcess::readyReadStandardError, this, &RtkConfigDialog::onProcessReadyRead);
    connect(str2str_process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &RtkConfigDialog::onProcessFinished);
    connect(str2str_process_, &QProcess::errorOccurred, this, &RtkConfigDialog::onProcessError);

    appendLog(tr("Starting RTK service..."));
    appendLog(tr("Command: %1").arg(cmd));

    str2str_process_->start("bash", QStringList() << "-c" << cmd);

    if (str2str_process_->waitForStarted(3000))
    {
        is_running_ = true;
        updateButtonStates();
        appendLog(tr("RTK service started successfully"));
    }
    else
    {
        appendLog(tr("Failed to start RTK service: %1").arg(str2str_process_->errorString()));
        delete str2str_process_;
        str2str_process_ = nullptr;
    }
}

void RtkConfigDialog::onStopClicked()
{
    if (str2str_process_ && str2str_process_->state() == QProcess::Running)
    {
        appendLog(tr("Stopping RTK service..."));

        str2str_process_->terminate();
        if (!str2str_process_->waitForFinished(3000))
        {
            appendLog(tr("Process not responding, killing..."));
            str2str_process_->kill();
            str2str_process_->waitForFinished(1000);
        }

        is_running_ = false;
        updateButtonStates();
        appendLog(tr("RTK service stopped"));
    }
}

void RtkConfigDialog::onTestClicked()
{
    QString server = server_edit_->text().trimmed();
    QString port = port_edit_->text().trimmed();
    QString username = username_edit_->text().trimmed();
    QString password = password_edit_->text();
    QString mountpoint = mountpoint_edit_->text().trimmed();

    if (server.isEmpty())
    {
        QMessageBox::warning(this, tr("Error"), tr("Please enter server address."));
        return;
    }

    appendLog(tr("Testing connection to %1:%2...").arg(server, port));

    QString testCmd;
    const QString nullSink =
#ifdef _WIN32
        "NUL";
#else
        "/dev/null";
#endif

    if (!username.isEmpty())
    {
        testCmd = QString("curl -s -o %1 -w '%%{http_code}' --connect-timeout 5 -u %2:%3 http://%4:%5/%6")
            .arg(nullSink, username, password, server, port, mountpoint.isEmpty() ? "" : mountpoint);
    }
    else
    {
        testCmd = QString("curl -s -o %1 -w '%%{http_code}' --connect-timeout 5 http://%2:%3/%4")
            .arg(nullSink, server, port, mountpoint.isEmpty() ? "" : mountpoint);
    }

    QProcess test_process;
#ifdef _WIN32
    test_process.start("cmd", QStringList() << "/C" << testCmd);
#else
    test_process.start("bash", QStringList() << "-c" << testCmd);
#endif
    test_process.waitForFinished(10000);

    QString output = test_process.readAllStandardOutput().trimmed();
    QString error = test_process.readAllStandardError();

    if (output == "200" || output == "401")
    {
        appendLog(tr("Connection test successful (HTTP %1)").arg(output));
        QMessageBox::information(this, tr("Success"), tr("Connection test successful!"));
    }
    else if (!error.isEmpty())
    {
        appendLog(tr("Connection test failed: %1").arg(error));
        QMessageBox::warning(this, tr("Failed"), tr("Connection test failed: %1").arg(error));
    }
    else
    {
        appendLog(tr("Connection test returned: %1").arg(output));
        QMessageBox::information(this, tr("Result"), tr("Server responded with code: %1").arg(output));
    }
}

void RtkConfigDialog::onProcessReadyRead()
{
    if (!str2str_process_) return;

    QString output = str2str_process_->readAllStandardOutput();
    QString error = str2str_process_->readAllStandardError();

    if (!output.isEmpty())
    {
        appendLog(output.trimmed());
    }
    if (!error.isEmpty())
    {
        appendLog(tr("[ERROR] %1").arg(error.trimmed()));
    }
}

void RtkConfigDialog::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    is_running_ = false;
    updateButtonStates();

    QString status = (exitStatus == QProcess::CrashExit) ? tr("crashed") : tr("finished");
    appendLog(tr("RTK service %1 with exit code %2").arg(status).arg(exitCode));
}

void RtkConfigDialog::onProcessError(QProcess::ProcessError error)
{
    QString errorStr;
    switch (error)
    {
        case QProcess::FailedToStart:
            errorStr = tr("Failed to start process");
            break;
        case QProcess::Crashed:
            errorStr = tr("Process crashed");
            break;
        case QProcess::Timedout:
            errorStr = tr("Process timed out");
            break;
        case QProcess::WriteError:
            errorStr = tr("Write error");
            break;
        case QProcess::ReadError:
            errorStr = tr("Read error");
            break;
        default:
            errorStr = tr("Unknown error");
    }

    appendLog(tr("[ERROR] Process error: %1").arg(errorStr));
    is_running_ = false;
    updateButtonStates();
}

void RtkConfigDialog::onSaveConfigClicked()
{
    QString filename = QFileDialog::getSaveFileName(
        this, tr("Save RTK Configuration"),
        QDir::homePath() + "/rtk_config.ini",
        tr("INI Files (*.ini);;All Files (*)")
    );

    if (filename.isEmpty()) return;

    QSettings settings(filename, QSettings::IniFormat);
    settings.setValue("server", server_edit_->text());
    settings.setValue("port", port_edit_->text());
    settings.setValue("username", username_edit_->text());
    settings.setValue("password", password_edit_->text());
    settings.setValue("mountpoint", mountpoint_edit_->text());
    settings.setValue("output_port", output_port_combo_->currentText());
    settings.setValue("baudrate", baudrate_combo_->currentText());
    settings.setValue("timeout", timeout_spin_->value());
    settings.setValue("reconnect", reconnect_spin_->value());
    settings.setValue("background", background_check_->isChecked());

    appendLog(tr("Configuration saved to: %1").arg(filename));
    QMessageBox::information(this, tr("Saved"), tr("Configuration saved successfully!"));
}

void RtkConfigDialog::onLoadConfigClicked()
{
    QString filename = QFileDialog::getOpenFileName(
        this, tr("Load RTK Configuration"),
        QDir::homePath(),
        tr("INI Files (*.ini);;All Files (*)")
    );

    if (filename.isEmpty()) return;

    QSettings settings(filename, QSettings::IniFormat);

    server_edit_->setText(settings.value("server", "").toString());
    port_edit_->setText(settings.value("port", "2101").toString());
    username_edit_->setText(settings.value("username", "").toString());
    password_edit_->setText(settings.value("password", "").toString());
    mountpoint_edit_->setText(settings.value("mountpoint", "").toString());
    output_port_combo_->setCurrentText(settings.value("output_port", "").toString());
    baudrate_combo_->setCurrentText(settings.value("baudrate", "115200").toString());
    timeout_spin_->setValue(settings.value("timeout", 5000).toInt());
    reconnect_spin_->setValue(settings.value("reconnect", 1000).toInt());
    background_check_->setChecked(settings.value("background", false).toBool());

    appendLog(tr("Configuration loaded from: %1").arg(filename));
}

void RtkConfigDialog::onClearLogClicked()
{
    log_text_edit_->clear();
}

void RtkConfigDialog::appendLog(const QString& message)
{
    if (!log_text_edit_) return;

    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    log_text_edit_->append(QString("[%1] %2").arg(timestamp, message));

    QTextCursor cursor = log_text_edit_->textCursor();
    cursor.movePosition(QTextCursor::End);
    log_text_edit_->setTextCursor(cursor);
}

bool RtkConfigDialog::isRunning() const
{
    return is_running_;
}
