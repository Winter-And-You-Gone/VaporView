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
#include <QFontMetrics>
#include <QInputDialog>
#include <QIntValidator>
#include <QRegularExpression>
#include <QSerialPortInfo>
#include <cmath>

namespace
{
QComboBox *createTimingComboBox(QWidget *parent, const QString &defaultValue)
{
    auto *combo = new QComboBox(parent);
    combo->setEditable(true);
    combo->addItems({"1000", "2000", "5000", "10000", "30000", "60000"});
    combo->setCurrentText(defaultValue);
    if (combo->lineEdit())
    {
        combo->lineEdit()->setValidator(new QIntValidator(1000, 60000, combo));
    }
    return combo;
}

int comboIntValue(const QComboBox *combo, int defaultValue)
{
    if (!combo)
    {
        return defaultValue;
    }

    bool ok = false;
    const int value = combo->currentText().toInt(&ok);
    return ok ? value : defaultValue;
}
}

RtkConfigDialog::RtkConfigDialog(QWidget *parent)
    : QDialog(parent)
    , main_layout_(nullptr)
    , config_layout_(nullptr)
    , output_layout_(nullptr)
    , button_layout_(nullptr)
    , log_layout_(nullptr)
    , log_button_layout_(nullptr)
    , config_group_(nullptr)
    , output_group_(nullptr)
    , log_group_(nullptr)
    , server_label_(nullptr)
    , port_label_(nullptr)
    , username_label_(nullptr)
    , password_label_(nullptr)
    , mountpoint_label_(nullptr)
    , output_port_label_(nullptr)
    , baudrate_label_(nullptr)
    , timeout_label_(nullptr)
    , reconnect_label_(nullptr)
    , server_edit_(nullptr)
    , port_edit_(nullptr)
    , username_edit_(nullptr)
    , password_edit_(nullptr)
    , mountpoint_edit_(nullptr)
    , output_port_combo_(nullptr)
    , baudrate_combo_(nullptr)
    , timeout_combo_(nullptr)
    , reconnect_combo_(nullptr)
    , background_check_(nullptr)
    , log_text_edit_(nullptr)
    , start_btn_(nullptr)
    , stop_btn_(nullptr)
    , test_btn_(nullptr)
    , refresh_ports_btn_(nullptr)
    , fetch_mountpoints_btn_(nullptr)
    , save_config_btn_(nullptr)
    , load_config_btn_(nullptr)
    , clear_log_btn_(nullptr)
    , status_label_(nullptr)
    , str2str_process_(nullptr)
    , is_running_(false)
    , is_english_(false)
    , font_scale_percent_(100)
{
    setupUi();
    loadSettings();
    setFontScale(100);
    setEnglish(false);

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
    main_layout_ = new QVBoxLayout(this);
    main_layout_->setSpacing(8);
    main_layout_->setContentsMargins(12, 12, 12, 12);

    config_group_ = new QGroupBox(this);
    config_layout_ = new QGridLayout(config_group_);
    config_layout_->setSpacing(6);

    int row = 0;
    server_label_ = new QLabel(this);
    config_layout_->addWidget(server_label_, row, 0);
    server_edit_ = new QLineEdit(this);
    config_layout_->addWidget(server_edit_, row, 1);

    port_label_ = new QLabel(this);
    config_layout_->addWidget(port_label_, row, 2);
    port_edit_ = new QLineEdit(this);
    port_edit_->setText("2101");
    config_layout_->addWidget(port_edit_, row, 3);
    row++;

    username_label_ = new QLabel(this);
    config_layout_->addWidget(username_label_, row, 0);
    username_edit_ = new QLineEdit(this);
    config_layout_->addWidget(username_edit_, row, 1, 1, 3);
    row++;

    password_label_ = new QLabel(this);
    config_layout_->addWidget(password_label_, row, 0);
    password_edit_ = new QLineEdit(this);
    password_edit_->setEchoMode(QLineEdit::Password);
    config_layout_->addWidget(password_edit_, row, 1, 1, 3);
    row++;

    mountpoint_label_ = new QLabel(this);
    config_layout_->addWidget(mountpoint_label_, row, 0);
    mountpoint_edit_ = new QLineEdit(this);
    config_layout_->addWidget(mountpoint_edit_, row, 1);
    fetch_mountpoints_btn_ = new QPushButton(this);
    connect(fetch_mountpoints_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onFetchMountpointsClicked);
    config_layout_->addWidget(fetch_mountpoints_btn_, row, 2, 1, 2);
    row++;

    main_layout_->addWidget(config_group_);

    output_group_ = new QGroupBox(this);
    output_layout_ = new QGridLayout(output_group_);
    output_layout_->setSpacing(6);
    output_layout_->setColumnStretch(1, 1);

    row = 0;
    output_port_label_ = new QLabel(this);
    output_layout_->addWidget(output_port_label_, row, 0);
    output_port_combo_ = new QComboBox(this);
    output_port_combo_->setEditable(true);
    output_layout_->addWidget(output_port_combo_, row, 1);

    refresh_ports_btn_ = new QPushButton(this);
    connect(refresh_ports_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onRefreshPortsClicked);
    output_layout_->addWidget(refresh_ports_btn_, row, 2);
    row++;

    baudrate_label_ = new QLabel(this);
    output_layout_->addWidget(baudrate_label_, row, 0);
    baudrate_combo_ = new QComboBox(this);
    baudrate_combo_->addItems({"9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600"});
    baudrate_combo_->setCurrentText("115200");
    output_layout_->addWidget(baudrate_combo_, row, 1);
    row++;

    timeout_label_ = new QLabel(this);
    output_layout_->addWidget(timeout_label_, row, 0);
    timeout_combo_ = createTimingComboBox(this, "5000");
    output_layout_->addWidget(timeout_combo_, row, 1);
    row++;

    reconnect_label_ = new QLabel(this);
    output_layout_->addWidget(reconnect_label_, row, 0);
    reconnect_combo_ = createTimingComboBox(this, "1000");
    output_layout_->addWidget(reconnect_combo_, row, 1);
    row++;

    background_check_ = new QCheckBox(this);
    output_layout_->addWidget(background_check_, row, 0, 1, 3);

    main_layout_->addWidget(output_group_);

    button_layout_ = new QHBoxLayout();
    button_layout_->setSpacing(6);

    start_btn_ = new QPushButton(this);
    connect(start_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onStartClicked);

    stop_btn_ = new QPushButton(this);
    stop_btn_->setEnabled(false);
    connect(stop_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onStopClicked);

    test_btn_ = new QPushButton(this);
    connect(test_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onTestClicked);

    save_config_btn_ = new QPushButton(this);
    connect(save_config_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onSaveConfigClicked);

    load_config_btn_ = new QPushButton(this);
    connect(load_config_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onLoadConfigClicked);

    button_layout_->addWidget(start_btn_);
    button_layout_->addWidget(stop_btn_);
    button_layout_->addWidget(test_btn_);
    button_layout_->addStretch();
    button_layout_->addWidget(save_config_btn_);
    button_layout_->addWidget(load_config_btn_);

    main_layout_->addLayout(button_layout_);

    log_group_ = new QGroupBox(this);
    log_layout_ = new QVBoxLayout(log_group_);
    log_layout_->setSpacing(4);

    log_text_edit_ = new QTextEdit(this);
    log_text_edit_->setReadOnly(true);
    log_layout_->addWidget(log_text_edit_);

    log_button_layout_ = new QHBoxLayout();
    clear_log_btn_ = new QPushButton(this);
    connect(clear_log_btn_, &QPushButton::clicked, this, &RtkConfigDialog::onClearLogClicked);
    log_button_layout_->addStretch();
    log_button_layout_->addWidget(clear_log_btn_);
    log_layout_->addLayout(log_button_layout_);

    main_layout_->addWidget(log_group_, 1);

    status_label_ = new QLabel(this);
    status_label_->setStyleSheet("QLabel { color: #666666; font-weight: bold; }");
    main_layout_->addWidget(status_label_);
}

QString RtkConfigDialog::textFor(const QString& english, const QString& chinese) const
{
    return is_english_ ? english : chinese;
}

void RtkConfigDialog::setEnglish(bool english)
{
    is_english_ = english;

    setWindowTitle(textFor("RTK NTRIP Configuration", "RTK NTRIP 配置"));
    config_group_->setTitle(textFor("NTRIP Server Configuration", "NTRIP 服务器配置"));
    output_group_->setTitle(textFor("RTCM Output Configuration", "RTCM 输出配置"));
    log_group_->setTitle(textFor("RTK Service Log", "RTK 服务日志"));

    server_label_->setText(textFor("Server:", "服务器:"));
    port_label_->setText(textFor("Port:", "端口:"));
    username_label_->setText(textFor("Username:", "用户名:"));
    password_label_->setText(textFor("Password:", "密码:"));
    mountpoint_label_->setText(textFor("Mountpoint:", "挂载点:"));
    output_port_label_->setText(textFor("Output Port:", "输出串口:"));
    baudrate_label_->setText(textFor("Baudrate:", "波特率:"));
    timeout_label_->setText(textFor("Timeout (ms):", "超时 (ms):"));
    reconnect_label_->setText(textFor("Reconnect (ms):", "重连间隔 (ms):"));

    server_edit_->setPlaceholderText(textFor("e.g. rtk.ntrip.org", "例如: rtk.ntrip.org"));
    mountpoint_edit_->setPlaceholderText(textFor("e.g. RTCM33", "例如: RTCM33"));

    refresh_ports_btn_->setText(textFor("Refresh", "刷新"));
    fetch_mountpoints_btn_->setText(textFor("Detect Mountpoints", "检测挂载点"));
    background_check_->setText(textFor("Run in background", "后台运行"));
    start_btn_->setText(textFor("Start", "启动"));
    stop_btn_->setText(textFor("Stop", "停止"));
    test_btn_->setText(textFor("Test Connection", "测试连接"));
    save_config_btn_->setText(textFor("Save Config", "保存配置"));
    load_config_btn_->setText(textFor("Load Config", "加载配置"));
    clear_log_btn_->setText(textFor("Clear Log", "清空日志"));

    updateButtonStates();
}

int RtkConfigDialog::scalePixels(int pixels) const
{
    return static_cast<int>(std::lround(pixels * font_scale_percent_ / 100.0));
}

void RtkConfigDialog::applyScaledUiMetrics()
{
    auto applyButtonWidth = [this](QPushButton *button, int baseWidth) {
        if (!button)
        {
            return;
        }

        const QFontMetrics metrics(button->font());
        const int textWidth = metrics.horizontalAdvance(button->text());
        button->setFixedWidth(std::max(scalePixels(baseWidth), textWidth + scalePixels(36)));
    };

    if (main_layout_)
    {
        main_layout_->setSpacing(scalePixels(8));
        main_layout_->setContentsMargins(scalePixels(12), scalePixels(12), scalePixels(12), scalePixels(12));
    }

    if (config_layout_)
    {
        config_layout_->setHorizontalSpacing(scalePixels(6));
        config_layout_->setVerticalSpacing(scalePixels(10));
        for (int row = 0; row < 4; ++row)
        {
            config_layout_->setRowMinimumHeight(row, scalePixels(42));
        }
    }

    if (output_layout_)
    {
        output_layout_->setHorizontalSpacing(scalePixels(6));
        output_layout_->setVerticalSpacing(scalePixels(10));
        output_layout_->setColumnMinimumWidth(2, scalePixels(88));
        for (int row = 0; row < 5; ++row)
        {
            output_layout_->setRowMinimumHeight(row, scalePixels(40));
        }
    }

    if (button_layout_)
    {
        button_layout_->setSpacing(scalePixels(6));
    }

    if (log_layout_)
    {
        log_layout_->setSpacing(scalePixels(4));
    }

    server_edit_->setMinimumHeight(scalePixels(34));
    port_edit_->setMaximumWidth(scalePixels(80));
    port_edit_->setMinimumHeight(scalePixels(34));
    username_edit_->setMinimumHeight(scalePixels(34));
    password_edit_->setMinimumHeight(scalePixels(34));
    mountpoint_edit_->setMinimumHeight(scalePixels(34));
    mountpoint_edit_->setMinimumWidth(scalePixels(180));

    output_port_combo_->setMinimumWidth(scalePixels(200));
    output_port_combo_->setMinimumHeight(scalePixels(30));
    baudrate_combo_->setMinimumWidth(scalePixels(200));
    baudrate_combo_->setMinimumHeight(scalePixels(30));
    timeout_combo_->setMinimumWidth(scalePixels(200));
    timeout_combo_->setMinimumHeight(scalePixels(30));
    reconnect_combo_->setMinimumWidth(scalePixels(200));
    reconnect_combo_->setMinimumHeight(scalePixels(30));

    applyButtonWidth(refresh_ports_btn_, 80);
    applyButtonWidth(fetch_mountpoints_btn_, 128);
    applyButtonWidth(start_btn_, 80);
    applyButtonWidth(stop_btn_, 80);
    applyButtonWidth(test_btn_, 120);
    applyButtonWidth(save_config_btn_, 100);
    applyButtonWidth(load_config_btn_, 100);
    applyButtonWidth(clear_log_btn_, 96);

    log_text_edit_->setMinimumWidth(scalePixels(200));

    setMinimumSize(scalePixels(560), scalePixels(680));
    if (!isMaximized() && !isFullScreen())
    {
        resize(size().expandedTo(minimumSize()).expandedTo(QSize(scalePixels(620), scalePixels(740))));
    }
}

void RtkConfigDialog::setFontScale(int percent)
{
    if (percent < 85 || percent > 150)
    {
        percent = 100;
    }

    if (font_scale_percent_ == percent)
    {
        applyScaledUiMetrics();
        return;
    }

    font_scale_percent_ = percent;
    applyScaledUiMetrics();
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
    output_port_combo_->setCurrentText(settings.value("output_port", "COM1").toString());
#else
    output_port_combo_->setCurrentText(settings.value("output_port", "/dev/ttyCOM3").toString());
#endif
    baudrate_combo_->setCurrentText(settings.value("baudrate", "115200").toString());
    timeout_combo_->setCurrentText(settings.value("timeout", "5000").toString());
    reconnect_combo_->setCurrentText(settings.value("reconnect", "1000").toString());
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
    settings.setValue("timeout", timeout_combo_->currentText());
    settings.setValue("reconnect", reconnect_combo_->currentText());
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
    int timeout = comboIntValue(timeout_combo_, 5000);
    int reconnect = comboIntValue(reconnect_combo_, 1000);

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
        status_label_->setText(textFor("Status: Running", "状态: 运行中"));
        status_label_->setStyleSheet("QLabel { color: #43a047; font-weight: bold; }");
    }
    else
    {
        status_label_->setText(textFor("Status: Stopped", "状态: 已停止"));
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

    appendLog(textFor("Ports refreshed: %1 found", "串口已刷新: 发现 %1 个").arg(ports.size()));
}

void RtkConfigDialog::onFetchMountpointsClicked()
{
    const QString server = server_edit_->text().trimmed();
    const QString port = port_edit_->text().trimmed();
    const QString username = username_edit_->text().trimmed();
    const QString password = password_edit_->text();

    if (server.isEmpty() || port.isEmpty())
    {
        QMessageBox::warning(this, textFor("Error", "错误"), textFor("Please enter server address and port first.", "请先填写服务器地址和端口。"));
        return;
    }

    appendLog(textFor("Fetching mountpoint list from %1:%2...", "正在从 %1:%2 获取挂载点列表...").arg(server, port));

    QString fetchCmd;
    if (!username.isEmpty())
    {
        fetchCmd = QString("curl -s --connect-timeout 5 -u %1:%2 http://%3:%4/")
            .arg(username, password, server, port);
    }
    else
    {
        fetchCmd = QString("curl -s --connect-timeout 5 http://%1:%2/")
            .arg(server, port);
    }

    QProcess fetchProcess;
#ifdef _WIN32
    fetchProcess.start("cmd", QStringList() << "/C" << fetchCmd);
#else
    fetchProcess.start("bash", QStringList() << "-c" << fetchCmd);
#endif
    fetchProcess.waitForFinished(10000);

    const QString output = QString::fromUtf8(fetchProcess.readAllStandardOutput());
    const QString error = QString::fromUtf8(fetchProcess.readAllStandardError()).trimmed();

    if (!error.isEmpty() && output.trimmed().isEmpty())
    {
        appendLog(textFor("Failed to fetch mountpoint list: %1", "获取挂载点列表失败: %1").arg(error));
        QMessageBox::warning(this, textFor("Failed", "失败"), textFor("Failed to fetch mountpoint list: %1", "获取挂载点列表失败: %1").arg(error));
        return;
    }

    QStringList mountpoints;
    const QStringList lines = output.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
    for (const QString &line : lines)
    {
        if (!line.startsWith("STR;"))
        {
            continue;
        }

        const QStringList parts = line.split(';');
        if (parts.size() > 1 && !parts.at(1).trimmed().isEmpty())
        {
            mountpoints.append(parts.at(1).trimmed());
        }
    }

    mountpoints.removeDuplicates();
    mountpoints.sort();

    if (mountpoints.isEmpty())
    {
        appendLog(textFor("No mountpoints found in sourcetable response.", "返回的源表中未找到挂载点。"));
        QMessageBox::information(this, textFor("No Data", "无数据"), textFor("No mountpoints were found for this server.", "该服务器未返回可用挂载点。"));
        return;
    }

    bool ok = false;
    const QString currentMountpoint = mountpoint_edit_->text().trimmed();
    const int currentIndex = std::max(0, mountpoints.indexOf(currentMountpoint));
    const QString selected = QInputDialog::getItem(
        this,
        textFor("Select Mountpoint", "选择挂载点"),
        textFor("Available mountpoints:", "可用挂载点:"),
        mountpoints,
        currentIndex,
        false,
        &ok
    );

    appendLog(textFor("Fetched %1 mountpoints.", "已获取 %1 个挂载点。").arg(mountpoints.size()));

    if (ok && !selected.isEmpty())
    {
        mountpoint_edit_->setText(selected);
        appendLog(textFor("Selected mountpoint: %1", "已选择挂载点: %1").arg(selected));
    }
}

void RtkConfigDialog::onStartClicked()
{
    QString cmd = buildCommandLine();
    if (cmd.isEmpty())
    {
        QMessageBox::warning(this, textFor("Error", "错误"), textFor("Please fill in server, mountpoint and output port.", "请填写服务器、挂载点和输出串口。"));
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

    appendLog(textFor("Starting RTK service...", "正在启动 RTK 服务..."));
    appendLog(textFor("Command: %1", "命令: %1").arg(cmd));

    str2str_process_->start("bash", QStringList() << "-c" << cmd);

    if (str2str_process_->waitForStarted(3000))
    {
        is_running_ = true;
        updateButtonStates();
        appendLog(textFor("RTK service started successfully", "RTK 服务启动成功"));
    }
    else
    {
        appendLog(textFor("Failed to start RTK service: %1", "RTK 服务启动失败: %1").arg(str2str_process_->errorString()));
        delete str2str_process_;
        str2str_process_ = nullptr;
    }
}

void RtkConfigDialog::onStopClicked()
{
    if (str2str_process_ && str2str_process_->state() == QProcess::Running)
    {
        appendLog(textFor("Stopping RTK service...", "正在停止 RTK 服务..."));

        str2str_process_->terminate();
        if (!str2str_process_->waitForFinished(3000))
        {
            appendLog(textFor("Process not responding, killing...", "进程无响应，正在强制结束..."));
            str2str_process_->kill();
            str2str_process_->waitForFinished(1000);
        }

        is_running_ = false;
        updateButtonStates();
        appendLog(textFor("RTK service stopped", "RTK 服务已停止"));
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
        QMessageBox::warning(this, textFor("Error", "错误"), textFor("Please enter server address.", "请输入服务器地址。"));
        return;
    }

    appendLog(textFor("Testing connection to %1:%2...", "正在测试连接 %1:%2 ...").arg(server, port));

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
        appendLog(textFor("Connection test successful (HTTP %1)", "连接测试成功 (HTTP %1)").arg(output));
        QMessageBox::information(this, textFor("Success", "成功"), textFor("Connection test successful!", "连接测试成功！"));
    }
    else if (!error.isEmpty())
    {
        appendLog(textFor("Connection test failed: %1", "连接测试失败: %1").arg(error));
        QMessageBox::warning(this, textFor("Failed", "失败"), textFor("Connection test failed: %1", "连接测试失败: %1").arg(error));
    }
    else
    {
        appendLog(textFor("Connection test returned: %1", "连接测试返回: %1").arg(output));
        QMessageBox::information(this, textFor("Result", "结果"), textFor("Server responded with code: %1", "服务器返回代码: %1").arg(output));
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
        appendLog(textFor("[ERROR] %1", "[错误] %1").arg(error.trimmed()));
    }
}

void RtkConfigDialog::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    is_running_ = false;
    updateButtonStates();

    QString status = (exitStatus == QProcess::CrashExit) ? textFor("crashed", "崩溃退出") : textFor("finished", "已结束");
    appendLog(textFor("RTK service %1 with exit code %2", "RTK 服务%1，退出码 %2").arg(status).arg(exitCode));
}

void RtkConfigDialog::onProcessError(QProcess::ProcessError error)
{
    QString errorStr;
    switch (error)
    {
        case QProcess::FailedToStart:
            errorStr = textFor("Failed to start process", "进程启动失败");
            break;
        case QProcess::Crashed:
            errorStr = textFor("Process crashed", "进程崩溃");
            break;
        case QProcess::Timedout:
            errorStr = textFor("Process timed out", "进程超时");
            break;
        case QProcess::WriteError:
            errorStr = textFor("Write error", "写入错误");
            break;
        case QProcess::ReadError:
            errorStr = textFor("Read error", "读取错误");
            break;
        default:
            errorStr = textFor("Unknown error", "未知错误");
    }

    appendLog(textFor("[ERROR] Process error: %1", "[错误] 进程错误: %1").arg(errorStr));
    is_running_ = false;
    updateButtonStates();
}

void RtkConfigDialog::onSaveConfigClicked()
{
    QString filename = QFileDialog::getSaveFileName(
        this, textFor("Save RTK Configuration", "保存 RTK 配置"),
        QDir::homePath() + "/rtk_config.ini",
        textFor("INI Files (*.ini);;All Files (*)", "INI 文件 (*.ini);;所有文件 (*)")
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
    settings.setValue("timeout", timeout_combo_->currentText());
    settings.setValue("reconnect", reconnect_combo_->currentText());
    settings.setValue("background", background_check_->isChecked());

    appendLog(textFor("Configuration saved to: %1", "配置已保存到: %1").arg(filename));
    QMessageBox::information(this, textFor("Saved", "已保存"), textFor("Configuration saved successfully!", "配置保存成功！"));
}

void RtkConfigDialog::onLoadConfigClicked()
{
    QString filename = QFileDialog::getOpenFileName(
        this, textFor("Load RTK Configuration", "加载 RTK 配置"),
        QDir::homePath(),
        textFor("INI Files (*.ini);;All Files (*)", "INI 文件 (*.ini);;所有文件 (*)")
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
    timeout_combo_->setCurrentText(settings.value("timeout", "5000").toString());
    reconnect_combo_->setCurrentText(settings.value("reconnect", "1000").toString());
    background_check_->setChecked(settings.value("background", false).toBool());

    appendLog(textFor("Configuration loaded from: %1", "配置已从以下位置加载: %1").arg(filename));
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
