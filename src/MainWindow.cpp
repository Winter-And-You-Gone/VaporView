#include "MainWindow.h"
#include "data_collector.h"
#include "data_types.h"
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QGridLayout>
#include <QFrame>
#include <QScrollArea>
#include <QSplitter>
#include <QTimer>
#include <QDir>
#include <QDirIterator>
#include <QScrollBar>
#include <QShortcut>
#include <QSpacerItem>
#include <memory>

GnssPanel::GnssPanel(QWidget *parent)
    : QWidget(parent)
    , rate_label_(nullptr)
    , status_label_(nullptr)
    , lat_label_(nullptr)
    , lon_label_(nullptr)
    , alt_label_(nullptr)
    , vel_n_label_(nullptr)
    , vel_e_label_(nullptr)
    , heading_label_(nullptr)
    , pitch_label_(nullptr)
    , sats_label_(nullptr)
    , gdop_label_(nullptr)
    , pdop_label_(nullptr)
    , hdop_label_(nullptr)
    , diff_age_label_(nullptr)
    , status_lbl_(nullptr)
    , lat_lbl_(nullptr)
    , lon_lbl_(nullptr)
    , alt_lbl_(nullptr)
    , vel_n_lbl_(nullptr)
    , vel_e_lbl_(nullptr)
    , heading_lbl_(nullptr)
    , pitch_lbl_(nullptr)
    , sats_lbl_(nullptr)
    , gdop_lbl_(nullptr)
    , pdop_lbl_(nullptr)
    , hdop_lbl_(nullptr)
    , diff_lbl_(nullptr)
    , is_english_(false)
{
    setupUi();
}

void GnssPanel::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(3);
    mainLayout->setContentsMargins(3, 3, 3, 3);

    rate_label_ = new QLabel(this);
    rate_label_->setStyleSheet("font-weight: bold; color: #666; font-size: 9px; text-align: right;");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    mainLayout->addWidget(rate_label_);

    auto *layout = new QGridLayout();
    layout->setSpacing(3);
    layout->setContentsMargins(0, 0, 0, 0);

    int row = 0;

    auto createRow = [this, &row, layout](QLabel*& lbl, QLabel*& valueLabel) {
        lbl = new QLabel(this);
        lbl->setStyleSheet("font-weight: bold; color: #555; font-size: 9px;");
        valueLabel = new QLabel("---", this);
        valueLabel->setStyleSheet("font-family: monospace; font-size: 10px; color: #0078d7;");
        layout->addWidget(lbl, row, 0);
        layout->addWidget(valueLabel, row, 1);
        row++;
    };

    createRow(status_lbl_, status_label_);
    createRow(lat_lbl_, lat_label_);
    createRow(lon_lbl_, lon_label_);
    createRow(alt_lbl_, alt_label_);
    createRow(vel_n_lbl_, vel_n_label_);
    createRow(vel_e_lbl_, vel_e_label_);
    createRow(heading_lbl_, heading_label_);
    createRow(pitch_lbl_, pitch_label_);
    createRow(sats_lbl_, sats_label_);
    createRow(gdop_lbl_, gdop_label_);
    createRow(pdop_lbl_, pdop_label_);
    createRow(hdop_lbl_, hdop_label_);
    createRow(diff_lbl_, diff_age_label_);

    layout->setColumnStretch(1, 1);
    mainLayout->addLayout(layout);
    mainLayout->addStretch();
    setEnglish(false);
}

void GnssPanel::updateRate(double hz)
{
    if (rate_label_)
    {
        rate_label_->setText(QString::asprintf("%.1f Hz", hz));
    }
}

void GnssPanel::setEnglish(bool english)
{
    is_english_ = english;
    if (english)
    {
        status_lbl_->setText("Status:");
        lat_lbl_->setText("Lat:");
        lon_lbl_->setText("Lon:");
        alt_lbl_->setText("Alt:");
        vel_n_lbl_->setText("Vel N:");
        vel_e_lbl_->setText("Vel E:");
        heading_lbl_->setText("Heading:");
        pitch_lbl_->setText("Pitch:");
        sats_lbl_->setText("Sats:");
        gdop_lbl_->setText("GDOP:");
        pdop_lbl_->setText("PDOP:");
        hdop_lbl_->setText("HDOP:");
        diff_lbl_->setText("Diff:");
    }
    else
    {
        status_lbl_->setText("状态:");
        lat_lbl_->setText("纬度:");
        lon_lbl_->setText("经度:");
        alt_lbl_->setText("高度:");
        vel_n_lbl_->setText("北速:");
        vel_e_lbl_->setText("东速:");
        heading_lbl_->setText("航向:");
        pitch_lbl_->setText("俯仰:");
        sats_lbl_->setText("卫星:");
        gdop_lbl_->setText("GDOP:");
        pdop_lbl_->setText("PDOP:");
        hdop_lbl_->setText("HDOP:");
        diff_lbl_->setText("差分龄:");
    }
}

void GnssPanel::updateData(const VaproView::GnssData& data)
{
    if (data.valid)
    {
        status_label_->setText(QString::fromStdString(data.position_status));
        status_label_->setStyleSheet("font-family: monospace; font-size: 10px; color: green; font-weight: bold;");

        lat_label_->setText(QString::asprintf("%.6f°", data.latitude));
        lon_label_->setText(QString::asprintf("%.6f°", data.longitude));
        alt_label_->setText(QString::asprintf("%.2f m", data.altitude));
        vel_n_label_->setText(QString::asprintf("%.2f m/s", data.vel_north));
        vel_e_label_->setText(QString::asprintf("%.2f m/s", data.vel_east));
        heading_label_->setText(QString::asprintf("%.1f°", data.heading));
        pitch_label_->setText(QString::asprintf("%.1f°", data.heading_pitch));
        sats_label_->setText(QString("%1/%2").arg(data.num_satellites_used).arg(data.num_satellites_tracked));
        gdop_label_->setText(QString::asprintf("%.1f", data.gdop));
        pdop_label_->setText(QString::asprintf("%.1f", data.pdop));
        hdop_label_->setText(QString::asprintf("%.1f", data.hdop));
        diff_age_label_->setText(QString::asprintf("%.1f s", data.diff_age));
    }
    else
    {
        status_label_->setText(QString::fromStdString(data.error_message));
        status_label_->setStyleSheet("font-family: monospace; font-size: 10px; color: red;");
    }
}

ImuPanel::ImuPanel(QWidget *parent)
    : QWidget(parent)
    , rate_label_(nullptr)
    , acc_x_label_(nullptr)
    , acc_y_label_(nullptr)
    , acc_z_label_(nullptr)
    , gyr_x_label_(nullptr)
    , gyr_y_label_(nullptr)
    , gyr_z_label_(nullptr)
    , roll_label_(nullptr)
    , pitch_label_(nullptr)
    , yaw_label_(nullptr)
    , temp_label_(nullptr)
    , press_label_(nullptr)
    , source_label_(nullptr)
    , source_lbl_(nullptr)
    , accel_sep_(nullptr)
    , gyro_sep_(nullptr)
    , attitude_sep_(nullptr)
    , env_sep_(nullptr)
    , temp_lbl_(nullptr)
    , press_lbl_(nullptr)
    , acc_x_lbl_(nullptr)
    , acc_y_lbl_(nullptr)
    , acc_z_lbl_(nullptr)
    , gyr_x_lbl_(nullptr)
    , gyr_y_lbl_(nullptr)
    , gyr_z_lbl_(nullptr)
    , roll_lbl_(nullptr)
    , pitch_lbl_(nullptr)
    , yaw_lbl_(nullptr)
    , is_english_(false)
{
    setupUi();
}

void ImuPanel::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(3);
    mainLayout->setContentsMargins(3, 3, 3, 3);

    rate_label_ = new QLabel(this);
    rate_label_->setStyleSheet("font-weight: bold; color: #666; font-size: 9px; text-align: right;");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    mainLayout->addWidget(rate_label_);

    auto *layout = new QGridLayout();
    layout->setSpacing(3);
    layout->setContentsMargins(0, 0, 0, 0);

    int row = 0;

    auto createRow = [this, &row, layout](QLabel*& lbl, QLabel*& valueLabel) {
        lbl = new QLabel(this);
        lbl->setStyleSheet("font-weight: bold; color: #555; font-size: 9px;");
        valueLabel = new QLabel("---", this);
        valueLabel->setStyleSheet("font-family: monospace; font-size: 10px; color: #0078d7;");
        layout->addWidget(lbl, row, 0);
        layout->addWidget(valueLabel, row, 1);
        row++;
    };

    createRow(source_lbl_, source_label_);

    accel_sep_ = new QLabel(this);
    accel_sep_->setStyleSheet("color: #888; font-size: 9px;");
    layout->addWidget(accel_sep_, row++, 0, 1, 2);
    createRow(acc_x_lbl_, acc_x_label_);
    createRow(acc_y_lbl_, acc_y_label_);
    createRow(acc_z_lbl_, acc_z_label_);

    gyro_sep_ = new QLabel(this);
    gyro_sep_->setStyleSheet("color: #888; font-size: 9px;");
    layout->addWidget(gyro_sep_, row++, 0, 1, 2);
    createRow(gyr_x_lbl_, gyr_x_label_);
    createRow(gyr_y_lbl_, gyr_y_label_);
    createRow(gyr_z_lbl_, gyr_z_label_);

    attitude_sep_ = new QLabel(this);
    attitude_sep_->setStyleSheet("color: #888; font-size: 9px;");
    layout->addWidget(attitude_sep_, row++, 0, 1, 2);
    createRow(roll_lbl_, roll_label_);
    createRow(pitch_lbl_, pitch_label_);
    createRow(yaw_lbl_, yaw_label_);

    env_sep_ = new QLabel(this);
    env_sep_->setStyleSheet("color: #888; font-size: 9px;");
    layout->addWidget(env_sep_, row++, 0, 1, 2);
    createRow(temp_lbl_, temp_label_);
    createRow(press_lbl_, press_label_);

    layout->setColumnStretch(1, 1);
    mainLayout->addLayout(layout);
    mainLayout->addStretch();
    setEnglish(false);
}

void ImuPanel::updateRate(double hz)
{
    if (rate_label_)
    {
        rate_label_->setText(QString::asprintf("%.1f Hz", hz));
    }
}

void ImuPanel::setEnglish(bool english)
{
    is_english_ = english;
    if (english)
    {
        source_lbl_->setText("Source:");
        accel_sep_->setText("— Accel —");
        gyro_sep_->setText("— Gyro —");
        attitude_sep_->setText("— Attitude —");
        env_sep_->setText("— Env —");
        temp_lbl_->setText("Temp:");
        press_lbl_->setText("Press:");
        acc_x_lbl_->setText("X:");
        acc_y_lbl_->setText("Y:");
        acc_z_lbl_->setText("Z:");
        gyr_x_lbl_->setText("X:");
        gyr_y_lbl_->setText("Y:");
        gyr_z_lbl_->setText("Z:");
        roll_lbl_->setText("Roll:");
        pitch_lbl_->setText("Pitch:");
        yaw_lbl_->setText("Yaw:");
    }
    else
    {
        source_lbl_->setText("数据源:");
        accel_sep_->setText("— 加速度 —");
        gyro_sep_->setText("— 陀螺仪 —");
        attitude_sep_->setText("— 姿态 —");
        env_sep_->setText("— 环境 —");
        temp_lbl_->setText("温度:");
        press_lbl_->setText("气压:");
        acc_x_lbl_->setText("X:");
        acc_y_lbl_->setText("Y:");
        acc_z_lbl_->setText("Z:");
        gyr_x_lbl_->setText("X:");
        gyr_y_lbl_->setText("Y:");
        gyr_z_lbl_->setText("Z:");
        roll_lbl_->setText("横滚:");
        pitch_lbl_->setText("俯仰:");
        yaw_lbl_->setText("航向:");
    }
}

void ImuPanel::updateData(const VaproView::ImuData& data)
{
    if (data.valid)
    {
        source_label_->setText(data.from_hi83 ? "HI83" : "HI91/HI81");
        source_label_->setStyleSheet("font-family: monospace; font-size: 10px; color: green; font-weight: bold;");

        acc_x_label_->setText(QString::asprintf("%.3f", data.acceleration[0]));
        acc_y_label_->setText(QString::asprintf("%.3f", data.acceleration[1]));
        acc_z_label_->setText(QString::asprintf("%.3f", data.acceleration[2]));

        gyr_x_label_->setText(QString::asprintf("%.2f", data.gyroscope[0]));
        gyr_y_label_->setText(QString::asprintf("%.2f", data.gyroscope[1]));
        gyr_z_label_->setText(QString::asprintf("%.2f", data.gyroscope[2]));

        roll_label_->setText(QString::asprintf("%.1f°", data.rpy[0]));
        pitch_label_->setText(QString::asprintf("%.1f°", data.rpy[1]));
        yaw_label_->setText(QString::asprintf("%.1f°", data.rpy[2]));

        temp_label_->setText(QString::asprintf("%.1f°C", data.temperature));
        press_label_->setText(QString::asprintf("%.1f hPa", data.air_pressure));
    }
    else
    {
        source_label_->setText(QString::fromStdString(data.error_message));
        source_label_->setStyleSheet("font-family: monospace; font-size: 10px; color: red;");
    }
}

PtbPanel::PtbPanel(QWidget *parent)
    : QWidget(parent)
    , rate_label_(nullptr)
    , pressure_label_(nullptr)
    , status_label_(nullptr)
    , pressure_lbl_(nullptr)
    , is_english_(false)
{
    setupUi();
}

void PtbPanel::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(3);
    layout->setContentsMargins(3, 3, 3, 3);

    rate_label_ = new QLabel(this);
    rate_label_->setStyleSheet("font-weight: bold; color: #666; font-size: 9px; text-align: right;");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(rate_label_);

    auto *pressLayout = new QHBoxLayout();
    pressure_lbl_ = new QLabel(this);
    pressure_lbl_->setStyleSheet("font-weight: bold; color: #555; font-size: 9px;");
    pressLayout->addWidget(pressure_lbl_);
    pressure_label_ = new QLabel("--- hPa", this);
    pressure_label_->setStyleSheet("font-family: monospace; font-size: 12px; font-weight: bold; color: #0078d7;");
    pressLayout->addWidget(pressure_label_);
    pressLayout->addStretch();
    layout->addLayout(pressLayout);

    status_label_ = new QLabel(this);
    status_label_->setStyleSheet("color: #888; font-size: 9px;");
    layout->addWidget(status_label_);

    layout->addStretch();
    setEnglish(false);
}

void PtbPanel::updateRate(double hz)
{
    if (rate_label_)
    {
        rate_label_->setText(QString::asprintf("%.1f Hz", hz));
    }
}

void PtbPanel::setEnglish(bool english)
{
    is_english_ = english;
    if (english)
    {
        pressure_lbl_->setText("Pressure:");
        status_label_->setText("Waiting...");
    }
    else
    {
        pressure_lbl_->setText("气压:");
        status_label_->setText("等待数据...");
    }
}

void PtbPanel::updateData(const VaproView::PtbData& data)
{
    if (data.valid)
    {
        pressure_label_->setText(QString::asprintf("%.2f hPa", data.pressure_hpa));
        pressure_label_->setStyleSheet("font-family: monospace; font-size: 12px; font-weight: bold; color: green;");
        status_label_->setText(is_english_ ? "Valid" : "有效");
        status_label_->setStyleSheet("color: green; font-size: 9px;");
    }
    else
    {
        pressure_label_->setText("--- hPa");
        pressure_label_->setStyleSheet("font-family: monospace; font-size: 12px; font-weight: bold; color: red;");
        status_label_->setText(QString::fromStdString(data.error_message));
        status_label_->setStyleSheet("color: red; font-size: 9px;");
    }
}

HmpPanel::HmpPanel(QWidget *parent)
    : QWidget(parent)
    , rate_label_(nullptr)
    , humidity_label_(nullptr)
    , temperature_label_(nullptr)
    , status_label_(nullptr)
    , temp_lbl_(nullptr)
    , humidity_lbl_(nullptr)
    , is_english_(false)
{
    setupUi();
}

void HmpPanel::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(3);
    layout->setContentsMargins(3, 3, 3, 3);

    rate_label_ = new QLabel(this);
    rate_label_->setStyleSheet("font-weight: bold; color: #666; font-size: 9px; text-align: right;");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(rate_label_);

    auto *tempLayout = new QHBoxLayout();
    temp_lbl_ = new QLabel(this);
    temp_lbl_->setStyleSheet("font-weight: bold; color: #555; font-size: 9px;");
    tempLayout->addWidget(temp_lbl_);
    temperature_label_ = new QLabel("--- °C", this);
    temperature_label_->setStyleSheet("font-family: monospace; font-size: 12px; font-weight: bold; color: #0078d7;");
    tempLayout->addWidget(temperature_label_);
    tempLayout->addStretch();
    layout->addLayout(tempLayout);

    auto *humidLayout = new QHBoxLayout();
    humidity_lbl_ = new QLabel(this);
    humidity_lbl_->setStyleSheet("font-weight: bold; color: #555; font-size: 9px;");
    humidLayout->addWidget(humidity_lbl_);
    humidity_label_ = new QLabel("--- %RH", this);
    humidity_label_->setStyleSheet("font-family: monospace; font-size: 12px; font-weight: bold; color: #0078d7;");
    humidLayout->addWidget(humidity_label_);
    humidLayout->addStretch();
    layout->addLayout(humidLayout);

    status_label_ = new QLabel(this);
    status_label_->setStyleSheet("color: #888; font-size: 9px;");
    layout->addWidget(status_label_);

    layout->addStretch();
    setEnglish(false);
}

void HmpPanel::updateRate(double hz)
{
    if (rate_label_)
    {
        rate_label_->setText(QString::asprintf("%.1f Hz", hz));
    }
}

void HmpPanel::setEnglish(bool english)
{
    is_english_ = english;
    if (english)
    {
        temp_lbl_->setText("Temp:");
        humidity_lbl_->setText("Humidity:");
        status_label_->setText("Waiting...");
    }
    else
    {
        temp_lbl_->setText("温度:");
        humidity_lbl_->setText("湿度:");
        status_label_->setText("等待数据...");
    }
}

void HmpPanel::updateData(const VaproView::HmpData& data)
{
    if (data.valid)
    {
        temperature_label_->setText(QString::asprintf("%.1f °C", data.temperature));
        humidity_label_->setText(QString::asprintf("%.1f %%RH", data.humidity));
        temperature_label_->setStyleSheet("font-family: monospace; font-size: 12px; font-weight: bold; color: green;");
        humidity_label_->setStyleSheet("font-family: monospace; font-size: 12px; font-weight: bold; color: green;");
        status_label_->setText(is_english_ ? "Valid" : "有效");
        status_label_->setStyleSheet("color: green; font-size: 9px;");
    }
    else
    {
        temperature_label_->setText("--- °C");
        humidity_label_->setText("--- %RH");
        temperature_label_->setStyleSheet("font-family: monospace; font-size: 12px; font-weight: bold; color: red;");
        humidity_label_->setStyleSheet("font-family: monospace; font-size: 12px; font-weight: bold; color: red;");
        status_label_->setText(QString::fromStdString(data.error_message));
        status_label_->setStyleSheet("color: red; font-size: 9px;");
    }
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , central_widget_(nullptr)
    , main_layout_(nullptr)
    , gnss_panel_(nullptr)
    , imu_panel_(nullptr)
    , ptb_panel_(nullptr)
    , hmp_panel_(nullptr)
    , log_text_edit_(nullptr)
    , status_label_(nullptr)
    , gnss_port_combo_(nullptr)
    , imu_port_combo_(nullptr)
    , ptb_port_combo_(nullptr)
    , hmp_port_combo_(nullptr)
    , gnss_baud_combo_(nullptr)
    , imu_baud_combo_(nullptr)
    , ptb_baud_combo_(nullptr)
    , hmp_baud_combo_(nullptr)
    , connect_btn_(nullptr)
    , disconnect_btn_(nullptr)
    , export_btn_(nullptr)
    , refresh_ports_btn_(nullptr)
    , fullscreen_btn_(nullptr)
    , lang_action_(nullptr)
    , clear_log_action_(nullptr)
    , export_action_(nullptr)
    , exit_action_(nullptr)
    , about_action_(nullptr)
    , config_group_(nullptr)
    , data_group_(nullptr)
    , log_group_(nullptr)
    , gnss_group_(nullptr)
    , imu_group_(nullptr)
    , ptb_group_(nullptr)
    , hmp_group_(nullptr)
    , gnss_lbl_(nullptr)
    , imu_lbl_(nullptr)
    , ptb_lbl_(nullptr)
    , hmp_lbl_(nullptr)
    , global_rate_lbl_(nullptr)
    , gnss_rate_lbl_(nullptr)
    , imu_rate_lbl_(nullptr)
    , ptb_rate_lbl_(nullptr)
    , hmp_rate_lbl_(nullptr)
    , global_rate_combo_(nullptr)
    , gnss_rate_combo_(nullptr)
    , imu_rate_combo_(nullptr)
    , ptb_rate_combo_(nullptr)
    , hmp_rate_combo_(nullptr)
    , gnss_collector_(nullptr)
    , imu_collector_(nullptr)
    , ptb_collector_(nullptr)
    , hmp_collector_(nullptr)
    , refresh_timer_(nullptr)
    , is_fullscreen_(false)
    , is_english_(false)
    , gnss_sample_rate_(1)
    , imu_sample_rate_(1)
    , ptb_sample_rate_(1)
    , hmp_sample_rate_(1)
{
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
    setupCentralWidget();

    resize(1280, 720);
    setMaximumSize(1368, 768);
    setMinimumSize(800, 600);

    refresh_timer_ = new QTimer(this);
    connect(refresh_timer_, &QTimer::timeout, this, &MainWindow::onRefreshTimer);
    refresh_timer_->start(100);

    setEnglish(false);

    updateConnectionStatus(false);
}

MainWindow::~MainWindow()
{
    if (gnss_collector_) gnss_collector_->stop();
    if (imu_collector_) imu_collector_->stop();
    if (ptb_collector_) ptb_collector_->stop();
    if (hmp_collector_) hmp_collector_->stop();
}

void MainWindow::setupMenuBar()
{
    QMenu *fileMenu = menuBar()->addMenu("");

    export_action_ = new QAction(this);
    export_action_->setShortcut(QKeySequence::Save);
    connect(export_action_, &QAction::triggered, this, &MainWindow::onExportClicked);
    fileMenu->addAction(export_action_);

    fileMenu->addSeparator();

    exit_action_ = new QAction(this);
    exit_action_->setShortcut(QKeySequence::Quit);
    connect(exit_action_, &QAction::triggered, this, &QMainWindow::close);
    fileMenu->addAction(exit_action_);

    QMenu *viewMenu = menuBar()->addMenu("");

    fullscreen_btn_ = new QAction(this);
    fullscreen_btn_->setShortcut(QKeySequence(Qt::Key_F11));
    connect(fullscreen_btn_, &QAction::triggered, this, &MainWindow::onToggleFullScreen);
    viewMenu->addAction(fullscreen_btn_);

    QMenu *langMenu = menuBar()->addMenu("");

    lang_action_ = new QAction(this);
    connect(lang_action_, &QAction::triggered, this, &MainWindow::onSwitchLanguage);
    langMenu->addAction(lang_action_);

    QMenu *helpMenu = menuBar()->addMenu("");

    about_action_ = new QAction(this);
    connect(about_action_, &QAction::triggered, [this]() {
        QString title = is_english_ ? "About VaporView" : "关于 VaporView";
        QString text = is_english_ ?
            "VaporView Application\n\n"
            "Version 1.0.0\n\n"
            "Navigation System with RTK and IMU support.\n\n"
            "Supported devices:\n"
            "- UM982 RTK Receiver (PVTSLN)\n"
            "- HiPNUC IMU (HI81/HI83/HI91)\n"
            "- PTB210 Barometer\n"
            "- HMP3 Temperature/Humidity Sensor\n\n"
            "Press F11 for fullscreen mode." :
            "VaporView 应用程序\n\n"
            "版本 1.0.0\n\n"
            "导航系统，支持 RTK 和 IMU。\n\n"
            "支持的设备:\n"
            "- UM982 RTK 接收机 (PVTSLN)\n"
            "- HiPNUC IMU (HI81/HI83/HI91)\n"
            "- PTB210 气压计\n"
            "- HMP3 温湿度传感器\n\n"
            "按 F11 进入全屏模式。";
        QMessageBox::about(this, title, text);
    });
    helpMenu->addAction(about_action_);
}

void MainWindow::setupToolBar()
{
    QToolBar *toolbar = addToolBar("");
    toolbar->setMovable(false);

    refresh_ports_btn_ = new QAction(this);
    refresh_ports_btn_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    connect(refresh_ports_btn_, &QAction::triggered, this, &MainWindow::onRefreshPortsClicked);
    toolbar->addAction(refresh_ports_btn_);

    toolbar->addSeparator();

    connect_btn_ = new QAction(this);
    connect_btn_->setIcon(style()->standardIcon(QStyle::SP_DialogYesButton));
    connect(connect_btn_, &QAction::triggered, this, &MainWindow::onConnectClicked);
    toolbar->addAction(connect_btn_);

    disconnect_btn_ = new QAction(this);
    disconnect_btn_->setIcon(style()->standardIcon(QStyle::SP_DialogNoButton));
    disconnect_btn_->setEnabled(false);
    connect(disconnect_btn_, &QAction::triggered, this, &MainWindow::onDisconnectClicked);
    toolbar->addAction(disconnect_btn_);

    toolbar->addSeparator();

    clear_log_action_ = new QAction(this);
    connect(clear_log_action_, &QAction::triggered, this, &MainWindow::onClearLogClicked);
    toolbar->addAction(clear_log_action_);

    toolbar->addSeparator();

    export_btn_ = new QAction(this);
    export_btn_->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    connect(export_btn_, &QAction::triggered, this, &MainWindow::onExportClicked);
    toolbar->addAction(export_btn_);

    toolbar->addSeparator();

    fullscreen_btn_ = new QAction(this);
    fullscreen_btn_->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    connect(fullscreen_btn_, &QAction::triggered, this, &MainWindow::onToggleFullScreen);
    toolbar->addAction(fullscreen_btn_);
}

void MainWindow::setupStatusBar()
{
    status_label_ = new QLabel(this);
    statusBar()->addWidget(status_label_);
}

void MainWindow::setupCentralWidget()
{
    central_widget_ = new QWidget(this);
    setCentralWidget(central_widget_);

    main_layout_ = new QVBoxLayout(central_widget_);
    main_layout_->setSpacing(3);
    main_layout_->setContentsMargins(3, 3, 3, 3);

    setupConfigPanel();
    setupDataPanels();
    setupLogPanel();
}

QStringList MainWindow::getAvailablePorts()
{
    QStringList ports;
    QDir devDir("/dev");

    QStringList filters;
    filters << "ttyUSB*" << "ttyACM*" << "ttyCOM*" << "ttyIMU*" << "ttyBARO*" << "ttyHMP*"
            << "ttyS*" << "ttyTHS*" << "ttyGS*" << "ttyAMA*" << "ttyMFD*";
    devDir.setNameFilters(filters);
    devDir.setFilter(QDir::System | QDir::Files);

    QFileInfoList fileList = devDir.entryInfoList(QDir::AllEntries | QDir::System, QDir::Name);
    for (const QFileInfo& info : fileList)
    {
        ports.append(info.absoluteFilePath());
    }

    QDirIterator it("/dev/serial/by-id", QStringList(), QDir::NoSymLinks | QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        it.next();
        QString path = it.filePath();
        QFileInfo linkInfo(path);
        if (linkInfo.isSymLink())
        {
            QString target = linkInfo.symLinkTarget();
            if (!target.isEmpty())
            {
                ports.append(path);
            }
        }
    }

    QDirIterator it2("/dev/serial/by-path", QStringList(), QDir::NoSymLinks | QDir::Files, QDirIterator::Subdirectories);
    while (it2.hasNext())
    {
        it2.next();
        QString path = it2.filePath();
        QFileInfo linkInfo(path);
        if (linkInfo.isSymLink())
        {
            QString target = linkInfo.symLinkTarget();
            if (!target.isEmpty())
            {
                ports.append(path);
            }
        }
    }

    ports.removeDuplicates();
    ports.sort();
    return ports;
}

void MainWindow::setupConfigPanel()
{
    config_group_ = new QGroupBox(this);
    auto *config_layout = new QGridLayout(config_group_);
    config_layout->setSpacing(3);

    QStringList baudRates = {"9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600"};
    QStringList ports = getAvailablePorts();

    auto createPortRow = [this, config_layout, &baudRates, &ports](QLabel*& lbl, QComboBox*& portCombo, QComboBox*& baudCombo, const QString& defaultPort, const QString& defaultBaud, int row) {
        lbl = new QLabel(this);
        lbl->setStyleSheet("font-weight: bold; font-size: 9px;");
        config_layout->addWidget(lbl, row, 0);

        portCombo = new QComboBox(this);
        portCombo->addItem(is_english_ ? "-- Select --" : "-- 选择 --");
        portCombo->addItems(ports);
        portCombo->setEditable(true);
        portCombo->setMinimumWidth(140);
        portCombo->setMaxVisibleItems(15);

        int defaultIdx = portCombo->findText(defaultPort);
        if (defaultIdx >= 0)
        {
            portCombo->setCurrentIndex(defaultIdx);
        }
        else
        {
            portCombo->setEditText(defaultPort);
        }
        config_layout->addWidget(portCombo, row, 1);

        baudCombo = new QComboBox(this);
        baudCombo->addItems(baudRates);
        baudCombo->setCurrentText(defaultBaud);
        baudCombo->setMinimumWidth(70);
        config_layout->addWidget(baudCombo, row, 2);
    };

    auto createRateRow = [this, config_layout](QLabel*& lbl, QComboBox*& combo, int row) {
        lbl = new QLabel(this);
        lbl->setStyleSheet("font-weight: bold; font-size: 9px;");
        config_layout->addWidget(lbl, row, 0);

        combo = new QComboBox(this);
        combo->addItem("1");
        combo->addItem("2");
        combo->addItem("5");
        combo->addItem("10");
        combo->addItem("20");
        combo->setCurrentIndex(0);
        combo->setEditable(true);
        combo->setMinimumWidth(80);
        combo->setValidator(new QIntValidator(1, 20, combo));
        config_layout->addWidget(combo, row, 1);
    };

    int row = 0;
    createPortRow(gnss_lbl_, gnss_port_combo_, gnss_baud_combo_, "/dev/ttyCOM3", "115200", row++);
    createPortRow(imu_lbl_, imu_port_combo_, imu_baud_combo_, "/dev/ttyIMU", "115200", row++);
    createPortRow(ptb_lbl_, ptb_port_combo_, ptb_baud_combo_, "/dev/ttyBARO", "9600", row++);
    createPortRow(hmp_lbl_, hmp_port_combo_, hmp_baud_combo_, "/dev/ttyHMP", "19200", row++);

    config_layout->addItem(new QSpacerItem(20, 10, QSizePolicy::Fixed, QSizePolicy::Fixed), row++, 0);

    QLabel *global_sep = new QLabel(this);
    global_sep->setStyleSheet("font-weight: bold; color: #666; font-size: 10px; border-bottom: 1px solid #ccc;");
    config_layout->addWidget(global_sep, row++, 0, 1, 3);

    createRateRow(global_rate_lbl_, global_rate_combo_, row++);
    connect(global_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onGlobalRateChanged);

    config_layout->addItem(new QSpacerItem(10, 5, QSizePolicy::Fixed, QSizePolicy::Fixed), row++, 0);

    QLabel *individual_sep = new QLabel(this);
    individual_sep->setStyleSheet("font-weight: bold; color: #666; font-size: 10px; border-bottom: 1px solid #ccc;");
    config_layout->addWidget(individual_sep, row++, 0, 1, 3);

    createRateRow(gnss_rate_lbl_, gnss_rate_combo_, row++);
    createRateRow(imu_rate_lbl_, imu_rate_combo_, row++);
    createRateRow(ptb_rate_lbl_, ptb_rate_combo_, row++);
    createRateRow(hmp_rate_lbl_, hmp_rate_combo_, row++);

    connect(gnss_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onGnssRateChanged);
    connect(imu_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onImuRateChanged);
    connect(ptb_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onPtbRateChanged);
    connect(hmp_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onHmpRateChanged);

    main_layout_->addWidget(config_group_);
}

void MainWindow::setupDataPanels()
{
    data_group_ = new QGroupBox(this);
    auto *data_layout = new QHBoxLayout(data_group_);
    data_layout->setSpacing(3);

    gnss_group_ = new QGroupBox(this);
    auto *gnss_layout = new QVBoxLayout(gnss_group_);
    gnss_layout->setContentsMargins(2, 2, 2, 2);
    gnss_panel_ = new GnssPanel(this);
    gnss_layout->addWidget(gnss_panel_);
    data_layout->addWidget(gnss_group_);

    imu_group_ = new QGroupBox(this);
    auto *imu_layout = new QVBoxLayout(imu_group_);
    imu_layout->setContentsMargins(2, 2, 2, 2);
    imu_panel_ = new ImuPanel(this);
    imu_layout->addWidget(imu_panel_);
    data_layout->addWidget(imu_group_);

    ptb_group_ = new QGroupBox(this);
    auto *ptb_layout = new QVBoxLayout(ptb_group_);
    ptb_layout->setContentsMargins(2, 2, 2, 2);
    ptb_panel_ = new PtbPanel(this);
    ptb_layout->addWidget(ptb_panel_);
    data_layout->addWidget(ptb_group_);

    hmp_group_ = new QGroupBox(this);
    auto *hmp_layout = new QVBoxLayout(hmp_group_);
    hmp_layout->setContentsMargins(2, 2, 2, 2);
    hmp_panel_ = new HmpPanel(this);
    hmp_layout->addWidget(hmp_panel_);
    data_layout->addWidget(hmp_group_);

    main_layout_->addWidget(data_group_, 1);
}

void MainWindow::setupLogPanel()
{
    log_group_ = new QGroupBox(this);
    auto *log_layout = new QVBoxLayout(log_group_);
    log_layout->setContentsMargins(3, 3, 3, 3);

    log_text_edit_ = new QTextEdit(this);
    log_text_edit_->setReadOnly(true);
    log_text_edit_->setStyleSheet("background-color: #1e1e1e; color: #00ff99; font-family: monospace; font-size: 9px;");
    log_text_edit_->setMaximumHeight(70);
    log_layout->addWidget(log_text_edit_);

    main_layout_->addWidget(log_group_);
}

void MainWindow::setEnglish(bool english)
{
    is_english_ = english;

    menuBar()->actions().at(0)->menu()->setTitle(english ? "&File" : "文件(&F)");
    export_action_->setText(english ? "&Export Data..." : "导出数据(&E)...");
    exit_action_->setText(english ? "E&xit" : "退出(&X)");

    menuBar()->actions().at(1)->menu()->setTitle(english ? "&View" : "视图(&V)");
    fullscreen_btn_->setText(english ? "&Fullscreen" : "全屏(&F)");

    menuBar()->actions().at(2)->menu()->setTitle(english ? "&Language" : "语言(&L)");
    lang_action_->setText(english ? "Switch to Chinese" : "切换到英文");

    menuBar()->actions().at(3)->menu()->setTitle(english ? "&Help" : "帮助(&H)");
    about_action_->setText(english ? "&About" : "关于(&A)");

    refresh_ports_btn_->setText(english ? "Refresh" : "刷新");
    connect_btn_->setText(english ? "Connect" : "连接");
    disconnect_btn_->setText(english ? "Disconnect" : "断开");
    clear_log_action_->setText(english ? "Clear" : "清空");
    export_btn_->setText(english ? "Export" : "导出");
    fullscreen_btn_->setText(english ? "Fullscreen" : "全屏");

    status_label_->setText(english ? "Ready" : "就绪");

    config_group_->setTitle(english ? "Serial Port Configuration" : "串口配置");
    data_group_->setTitle(english ? "Sensor Data" : "传感器数据");
    log_group_->setTitle(english ? "Log" : "日志");

    gnss_group_->setTitle(english ? "GNSS / RTK" : "GNSS / RTK");
    imu_group_->setTitle(english ? "IMU" : "IMU");
    ptb_group_->setTitle(english ? "PTB210" : "PTB210");
    hmp_group_->setTitle(english ? "HMP3" : "HMP3");

    gnss_lbl_->setText(english ? "GNSS:" : "GNSS:");
    imu_lbl_->setText(english ? "IMU:" : "IMU:");
    ptb_lbl_->setText(english ? "PTB210:" : "PTB210:");
    hmp_lbl_->setText(english ? "HMP3:" : "HMP3:");

    global_rate_lbl_->setText(english ? "Global Rate:" : "统一频率:");
    gnss_rate_lbl_->setText(english ? "GNSS Rate:" : "GNSS频率:");
    imu_rate_lbl_->setText(english ? "IMU Rate:" : "IMU频率:");
    ptb_rate_lbl_->setText(english ? "PTB Rate:" : "PTB频率:");
    hmp_rate_lbl_->setText(english ? "HMP Rate:" : "HMP频率:");

    gnss_panel_->setEnglish(english);
    imu_panel_->setEnglish(english);
    ptb_panel_->setEnglish(english);
    hmp_panel_->setEnglish(english);
}

void MainWindow::onSwitchLanguage()
{
    is_english_ = !is_english_;
    setEnglish(is_english_);
    log(is_english_ ? "Language switched to English" : "语言已切换为中文");
}

int MainWindow::parseRate(const QString& text)
{
    bool ok;
    int rate = text.toInt(&ok);
    if (ok && rate >= 1 && rate <= 20)
    {
        return rate;
    }
    return 1;
}

void MainWindow::onGlobalRateChanged(const QString& text)
{
    int rate = parseRate(text);
    gnss_sample_rate_ = rate;
    imu_sample_rate_ = rate;
    ptb_sample_rate_ = rate;
    hmp_sample_rate_ = rate;
    
    gnss_rate_combo_->blockSignals(true);
    imu_rate_combo_->blockSignals(true);
    ptb_rate_combo_->blockSignals(true);
    hmp_rate_combo_->blockSignals(true);
    
    gnss_rate_combo_->setCurrentText(text);
    imu_rate_combo_->setCurrentText(text);
    ptb_rate_combo_->setCurrentText(text);
    hmp_rate_combo_->setCurrentText(text);
    
    gnss_rate_combo_->blockSignals(false);
    imu_rate_combo_->blockSignals(false);
    ptb_rate_combo_->blockSignals(false);
    hmp_rate_combo_->blockSignals(false);
    
    applyAllSampleRates();
}

void MainWindow::onGnssRateChanged(const QString& text)
{
    gnss_sample_rate_ = parseRate(text);
    if (gnss_collector_) 
    {
        gnss_collector_->setSampleRate(gnss_sample_rate_);
        gnss_collector_->setDeviceSampleRate(gnss_sample_rate_);
    }
    log(QString(is_english_ ? "GNSS sample rate set to %1 Hz" : "GNSS采样频率已设置为 %1 Hz").arg(gnss_sample_rate_));
}

void MainWindow::onImuRateChanged(const QString& text)
{
    imu_sample_rate_ = parseRate(text);
    if (imu_collector_) imu_collector_->setSampleRate(imu_sample_rate_);
    log(QString(is_english_ ? "IMU sample rate set to %1 Hz" : "IMU采样频率已设置为 %1 Hz").arg(imu_sample_rate_));
}

void MainWindow::onPtbRateChanged(const QString& text)
{
    ptb_sample_rate_ = parseRate(text);
    if (ptb_collector_) ptb_collector_->setSampleRate(ptb_sample_rate_);
    log(QString(is_english_ ? "PTB sample rate set to %1 Hz" : "PTB采样频率已设置为 %1 Hz").arg(ptb_sample_rate_));
}

void MainWindow::onHmpRateChanged(const QString& text)
{
    hmp_sample_rate_ = parseRate(text);
    if (hmp_collector_) hmp_collector_->setSampleRate(hmp_sample_rate_);
    log(QString(is_english_ ? "HMP sample rate set to %1 Hz" : "HMP采样频率已设置为 %1 Hz").arg(hmp_sample_rate_));
}

void MainWindow::applyAllSampleRates()
{
    if (gnss_collector_) gnss_collector_->setSampleRate(gnss_sample_rate_);
    if (imu_collector_) imu_collector_->setSampleRate(imu_sample_rate_);
    if (ptb_collector_) ptb_collector_->setSampleRate(ptb_sample_rate_);
    if (hmp_collector_) hmp_collector_->setSampleRate(hmp_sample_rate_);
 log(QString(is_english_ ? "All sample rates set to %1 Hz" : "所有采样频率已设置为 %1 Hz").arg(gnss_sample_rate_));
}

void MainWindow::onToggleFullScreen()
{
    if (is_fullscreen_)
    {
        showNormal();
        setMaximumSize(1368, 768);
        resize(1280, 720);
        menuBar()->show();
        is_fullscreen_ = false;
    }
    else
    {
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        showFullScreen();
        menuBar()->hide();
        is_fullscreen_ = true;
    }
}

void MainWindow::log(const QString& message)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    log_text_edit_->append(QString("[%1] %2").arg(timestamp, message));
}

void MainWindow::updateConnectionStatus(bool connected)
{
    connect_btn_->setEnabled(!connected);
    disconnect_btn_->setEnabled(connected);
    refresh_ports_btn_->setEnabled(!connected);

    gnss_port_combo_->setEnabled(!connected);
    imu_port_combo_->setEnabled(!connected);
    ptb_port_combo_->setEnabled(!connected);
    hmp_port_combo_->setEnabled(!connected);
    gnss_baud_combo_->setEnabled(!connected);
    imu_baud_combo_->setEnabled(!connected);
    ptb_baud_combo_->setEnabled(!connected);
    hmp_baud_combo_->setEnabled(!connected);

    if (connected)
    {
        status_label_->setText(is_english_ ? "Connected" : "已连接");
        status_label_->setStyleSheet("color: green; font-weight: bold;");
    }
    else
    {
        status_label_->setText(is_english_ ? "Disconnected" : "未连接");
        status_label_->setStyleSheet("color: red;");
    }
}

void MainWindow::onRefreshPortsClicked()
{
    QStringList ports = getAvailablePorts();

    auto updateCombo = [this, &ports](QComboBox* combo) {
        QString current = combo->currentText();
        combo->clear();
        combo->addItem(is_english_ ? "-- Select --" : "-- 选择 --");
        combo->addItems(ports);
        int idx = combo->findText(current);
        if (idx >= 0)
        {
            combo->setCurrentIndex(idx);
        }
        else
        {
            combo->setEditText(current);
        }
    };

    updateCombo(gnss_port_combo_);
    updateCombo(imu_port_combo_);
    updateCombo(ptb_port_combo_);
    updateCombo(hmp_port_combo_);

    log(QString(is_english_ ? "Ports refreshed: %1 found" : "端口已刷新: 发现 %1 个").arg(ports.size()));
}

void MainWindow::onConnectClicked()
{
    log(is_english_ ? "Connecting..." : "正在连接...");

    gnss_collector_ = std::make_unique<VaproView::GnssCollector>();
    imu_collector_ = std::make_unique<VaproView::ImuCollector>();
    ptb_collector_ = std::make_unique<VaproView::PtbCollector>();
    hmp_collector_ = std::make_unique<VaproView::HmpCollector>();

    gnss_collector_->setSampleRate(gnss_sample_rate_);
    imu_collector_->setSampleRate(imu_sample_rate_);
    ptb_collector_->setSampleRate(ptb_sample_rate_);
    hmp_collector_->setSampleRate(hmp_sample_rate_);

    auto logCallback = [this](const std::string& msg) {
        QMetaObject::invokeMethod(this, [this, msg]() {
            log(QString::fromStdString(msg));
        }, Qt::QueuedConnection);
    };
    
    gnss_collector_->setLogCallback(logCallback);
    imu_collector_->setLogCallback(logCallback);
    ptb_collector_->setLogCallback(logCallback);
    hmp_collector_->setLogCallback(logCallback);

    bool any_connected = false;
    QString selectText = is_english_ ? "-- Select --" : "-- 选择 --";

    QString gnss_port = gnss_port_combo_->currentText();
    if (gnss_port != selectText && !gnss_port.isEmpty())
    {
        VaproView::SerialConfig gnss_config = VaproView::SerialConfig::N81(gnss_baud_combo_->currentText().toInt());
        if (gnss_collector_->start(gnss_port.toStdString(), gnss_config))
        {
            log(QString("GNSS: %1 @ %2").arg(gnss_port, gnss_baud_combo_->currentText()));
            gnss_collector_->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onGnssDataReady", Qt::QueuedConnection); });
            any_connected = true;
        }
        else
        {
            log(QString(is_english_ ? "GNSS failed: %1" : "GNSS 连接失败: %1").arg(QString::fromStdString(gnss_collector_->getLastError())));
        }
    }

    QString imu_port = imu_port_combo_->currentText();
    if (imu_port != selectText && !imu_port.isEmpty())
    {
        VaproView::SerialConfig imu_config = VaproView::SerialConfig::N81(imu_baud_combo_->currentText().toInt());
        if (imu_collector_->start(imu_port.toStdString(), imu_config))
        {
            log(QString("IMU: %1 @ %2").arg(imu_port, imu_baud_combo_->currentText()));
            imu_collector_->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onImuDataReady", Qt::QueuedConnection); });
            any_connected = true;
        }
        else
        {
            log(QString(is_english_ ? "IMU failed: %1" : "IMU 连接失败: %1").arg(QString::fromStdString(imu_collector_->getLastError())));
        }
    }

    QString ptb_port = ptb_port_combo_->currentText();
    if (ptb_port != selectText && !ptb_port.isEmpty())
    {
        VaproView::SerialConfig ptb_config = VaproView::SerialConfig::E71(ptb_baud_combo_->currentText().toInt());
        if (ptb_collector_->start(ptb_port.toStdString(), ptb_config))
        {
            log(QString("PTB210: %1 @ %2").arg(ptb_port, ptb_baud_combo_->currentText()));
            ptb_collector_->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onPtbDataReady", Qt::QueuedConnection); });
            any_connected = true;
        }
        else
        {
            log(QString(is_english_ ? "PTB210 failed: %1" : "PTB210 连接失败: %1").arg(QString::fromStdString(ptb_collector_->getLastError())));
        }
    }

    QString hmp_port = hmp_port_combo_->currentText();
    if (hmp_port != selectText && !hmp_port.isEmpty())
    {
        VaproView::SerialConfig hmp_config = VaproView::SerialConfig::N82(hmp_baud_combo_->currentText().toInt());
        if (hmp_collector_->start(hmp_port.toStdString(), hmp_config))
        {
            log(QString("HMP3: %1 @ %2").arg(hmp_port, hmp_baud_combo_->currentText()));
            hmp_collector_->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onHmpDataReady", Qt::QueuedConnection); });
            any_connected = true;
        }
        else
        {
            log(QString(is_english_ ? "HMP3 failed: %1" : "HMP3 连接失败: %1").arg(QString::fromStdString(hmp_collector_->getLastError())));
        }
    }

    if (any_connected)
    {
        updateConnectionStatus(true);
    }
    else
    {
        log(is_english_ ? "No ports connected" : "没有端口连接成功");
    }
}

void MainWindow::onDisconnectClicked()
{
    log(is_english_ ? "Disconnecting..." : "正在断开...");

    if (gnss_collector_)
    {
        gnss_collector_->stop();
        gnss_collector_.reset();
    }
    if (imu_collector_)
    {
        imu_collector_->stop();
        imu_collector_.reset();
    }
    if (ptb_collector_)
    {
        ptb_collector_->stop();
        ptb_collector_.reset();
    }
    if (hmp_collector_)
    {
        hmp_collector_->stop();
        hmp_collector_.reset();
    }

    updateConnectionStatus(false);
    log(is_english_ ? "Disconnected" : "已断开");
}

void MainWindow::onGnssDataReady()
{
    if (gnss_collector_)
    {
        current_gnss_ = gnss_collector_->getLatestData();
    }
}

void MainWindow::onImuDataReady()
{
    if (imu_collector_)
    {
        current_imu_ = imu_collector_->getLatestData();
    }
}

void MainWindow::onPtbDataReady()
{
    if (ptb_collector_)
    {
        current_ptb_ = ptb_collector_->getLatestData();
    }
}

void MainWindow::onHmpDataReady()
{
    if (hmp_collector_)
    {
        current_hmp_ = hmp_collector_->getLatestData();
    }
}

void MainWindow::onRefreshTimer()
{
    gnss_panel_->updateData(current_gnss_);
    imu_panel_->updateData(current_imu_);
    ptb_panel_->updateData(current_ptb_);
    hmp_panel_->updateData(current_hmp_);

    if (gnss_collector_) gnss_panel_->updateRate(gnss_collector_->getActualRate());
    if (imu_collector_) imu_panel_->updateRate(imu_collector_->getActualRate());
    if (ptb_collector_) ptb_panel_->updateRate(ptb_collector_->getActualRate());
    if (hmp_collector_) hmp_panel_->updateRate(hmp_collector_->getActualRate());
}

void MainWindow::onExportClicked()
{
    QString filter = is_english_ ? "CSV Files (*.csv);;JSON Files (*.json)" : "CSV 文件 (*.csv);;JSON 文件 (*.json)";
    QString filename = QFileDialog::getSaveFileName(this, is_english_ ? "Export Data" : "导出数据", QString(), filter);
    if (filename.isEmpty())
    {
        return;
    }

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, is_english_ ? "Error" : "错误", is_english_ ? "Failed to open file for writing" : "无法打开文件进行写入");
        return;
    }

    QTextStream out(&file);

    if (filename.endsWith(".json", Qt::CaseInsensitive))
    {
        out << "{\n";
        out << "  \"gnss\": {\n";
        out << "    \"latitude\": " << current_gnss_.latitude << ",\n";
        out << "    \"longitude\": " << current_gnss_.longitude << ",\n";
        out << "    \"altitude\": " << current_gnss_.altitude << ",\n";
        out << "    \"valid\": " << (current_gnss_.valid ? "true" : "false") << "\n";
        out << "  },\n";
        out << "  \"imu\": {\n";
        out << "    \"acc_x\": " << current_imu_.acceleration[0] << ",\n";
        out << "    \"acc_y\": " << current_imu_.acceleration[1] << ",\n";
        out << "    \"acc_z\": " << current_imu_.acceleration[2] << ",\n";
        out << "    \"valid\": " << (current_imu_.valid ? "true" : "false") << "\n";
        out << "  },\n";
        out << "  \"ptb\": {\n";
        out << "    \"pressure\": " << current_ptb_.pressure_hpa << ",\n";
        out << "    \"valid\": " << (current_ptb_.valid ? "true" : "false") << "\n";
        out << "  },\n";
        out << "  \"hmp\": {\n";
        out << "    \"humidity\": " << current_hmp_.humidity << ",\n";
        out << "    \"temperature\": " << current_hmp_.temperature << ",\n";
        out << "    \"valid\": " << (current_hmp_.valid ? "true" : "false") << "\n";
        out << "  }\n";
        out << "}\n";
    }
    else
    {
        out << "Category,Parameter,Value,Unit,Valid\n";
        out << "GNSS,Latitude," << current_gnss_.latitude << ",deg," << (current_gnss_.valid ? "Yes" : "No") << "\n";
        out << "GNSS,Longitude," << current_gnss_.longitude << ",deg," << (current_gnss_.valid ? "Yes" : "No") << "\n";
        out << "GNSS,Altitude," << current_gnss_.altitude << ",m," << (current_gnss_.valid ? "Yes" : "No") << "\n";
        out << "IMU,AccX," << current_imu_.acceleration[0] << ",m/s²," << (current_imu_.valid ? "Yes" : "No") << "\n";
        out << "IMU,AccY," << current_imu_.acceleration[1] << ",m/s²," << (current_imu_.valid ? "Yes" : "No") << "\n";
        out << "IMU,AccZ," << current_imu_.acceleration[2] << ",m/s²," << (current_imu_.valid ? "Yes" : "No") << "\n";
        out << "PTB,Pressure," << current_ptb_.pressure_hpa << ",hPa," << (current_ptb_.valid ? "Yes" : "No") << "\n";
        out << "HMP,Humidity," << current_hmp_.humidity << ",%RH," << (current_hmp_.valid ? "Yes" : "No") << "\n";
        out << "HMP,Temperature," << current_hmp_.temperature << ",°C," << (current_hmp_.valid ? "Yes" : "No") << "\n";
    }

    file.close();
    log(QString(is_english_ ? "Exported: %1" : "已导出: %1").arg(filename));
}

void MainWindow::onClearLogClicked()
{
    log_text_edit_->clear();
    log(is_english_ ? "Log cleared" : "日志已清空");
}
