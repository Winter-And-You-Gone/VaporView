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
#include <QEvent>
#include <QLocale>
#include <QCoreApplication>
#include <memory>

GnssPanel::GnssPanel(QWidget *parent)
    : QWidget(parent)
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
    , raw_label_(nullptr)
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
{
    setupUi();
}

void GnssPanel::setupUi()
{
    auto *layout = new QGridLayout(this);
    layout->setSpacing(3);
    layout->setContentsMargins(3, 3, 3, 3);

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
    retranslateUi();
}

void GnssPanel::retranslateUi()
{
    status_lbl_->setText(tr("Status:"));
    lat_lbl_->setText(tr("Lat:"));
    lon_lbl_->setText(tr("Lon:"));
    alt_lbl_->setText(tr("Alt:"));
    vel_n_lbl_->setText(tr("Vel N:"));
    vel_e_lbl_->setText(tr("Vel E:"));
    heading_lbl_->setText(tr("Heading:"));
    pitch_lbl_->setText(tr("Pitch:"));
    sats_lbl_->setText(tr("Sats:"));
    gdop_lbl_->setText(tr("GDOP:"));
    pdop_lbl_->setText(tr("PDOP:"));
    hdop_lbl_->setText(tr("HDOP:"));
    diff_lbl_->setText(tr("Diff:"));
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
    , acc_x_label_(nullptr)
    , acc_y_label_(nullptr)
    , acc_z_label_(nullptr)
    , gyr_x_label_(nullptr)
    , gyr_y_label_(nullptr)
    , gyr_z_label_(nullptr)
    , roll_label_(nullptr)
    , pitch_label_(nullptr)
    , yaw_label_(nullptr)
    , quat_w_label_(nullptr)
    , quat_x_label_(nullptr)
    , quat_y_label_(nullptr)
    , quat_z_label_(nullptr)
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
{
    setupUi();
}

void ImuPanel::setupUi()
{
    auto *layout = new QGridLayout(this);
    layout->setSpacing(3);
    layout->setContentsMargins(3, 3, 3, 3);

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
    createRow(acc_x_label_, acc_x_label_);
    createRow(acc_y_label_, acc_y_label_);
    createRow(acc_z_label_, acc_z_label_);

    gyro_sep_ = new QLabel(this);
    gyro_sep_->setStyleSheet("color: #888; font-size: 9px;");
    layout->addWidget(gyro_sep_, row++, 0, 1, 2);
    createRow(gyr_x_label_, gyr_x_label_);
    createRow(gyr_y_label_, gyr_y_label_);
    createRow(gyr_z_label_, gyr_z_label_);

    attitude_sep_ = new QLabel(this);
    attitude_sep_->setStyleSheet("color: #888; font-size: 9px;");
    layout->addWidget(attitude_sep_, row++, 0, 1, 2);
    createRow(roll_label_, roll_label_);
    createRow(pitch_label_, pitch_label_);
    createRow(yaw_label_, yaw_label_);

    env_sep_ = new QLabel(this);
    env_sep_->setStyleSheet("color: #888; font-size: 9px;");
    layout->addWidget(env_sep_, row++, 0, 1, 2);
    createRow(temp_lbl_, temp_label_);
    createRow(press_lbl_, press_label_);

    layout->setColumnStretch(1, 1);
    retranslateUi();
}

void ImuPanel::retranslateUi()
{
    source_lbl_->setText(tr("Source:"));
    accel_sep_->setText(tr("— Accel —"));
    gyro_sep_->setText(tr("— Gyro —"));
    attitude_sep_->setText(tr("— Attitude —"));
    env_sep_->setText(tr("— Env —"));
    temp_lbl_->setText(tr("Temp:"));
    press_lbl_->setText(tr("Press:"));
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
    , pressure_label_(nullptr)
    , status_label_(nullptr)
    , pressure_lbl_(nullptr)
{
    setupUi();
}

void PtbPanel::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(3);
    layout->setContentsMargins(3, 3, 3, 3);

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
    retranslateUi();
}

void PtbPanel::retranslateUi()
{
    pressure_lbl_->setText(tr("Pressure:"));
    status_label_->setText(tr("Waiting..."));
}

void PtbPanel::updateData(const VaproView::PtbData& data)
{
    if (data.valid)
    {
        pressure_label_->setText(QString::asprintf("%.2f hPa", data.pressure_hpa));
        pressure_label_->setStyleSheet("font-family: monospace; font-size: 12px; font-weight: bold; color: green;");
        status_label_->setText(tr("Valid"));
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
    , humidity_label_(nullptr)
    , temperature_label_(nullptr)
    , status_label_(nullptr)
    , temp_lbl_(nullptr)
    , humidity_lbl_(nullptr)
{
    setupUi();
}

void HmpPanel::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(3);
    layout->setContentsMargins(3, 3, 3, 3);

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
    retranslateUi();
}

void HmpPanel::retranslateUi()
{
    temp_lbl_->setText(tr("Temp:"));
    humidity_lbl_->setText(tr("Humidity:"));
    status_label_->setText(tr("Waiting..."));
}

void HmpPanel::updateData(const VaproView::HmpData& data)
{
    if (data.valid)
    {
        temperature_label_->setText(QString::asprintf("%.1f °C", data.temperature));
        humidity_label_->setText(QString::asprintf("%.1f %%RH", data.humidity));
        temperature_label_->setStyleSheet("font-family: monospace; font-size: 12px; font-weight: bold; color: green;");
        humidity_label_->setStyleSheet("font-family: monospace; font-size: 12px; font-weight: bold; color: green;");
        status_label_->setText(tr("Valid"));
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
    , config_group_(nullptr)
    , data_group_(nullptr)
    , log_group_(nullptr)
    , gnss_group_(nullptr)
    , imu_group_(nullptr)
    , ptb_group_(nullptr)
    , hmp_group_(nullptr)
    , gnss_collector_(nullptr)
    , imu_collector_(nullptr)
    , ptb_collector_(nullptr)
    , hmp_collector_(nullptr)
    , refresh_timer_(nullptr)
    , is_fullscreen_(false)
    , is_english_(false)
{
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
    setupCentralWidget();

    resize(1280, 720);
    setMaximumSize(1280, 720);

    refresh_timer_ = new QTimer(this);
    connect(refresh_timer_, &QTimer::timeout, this, &MainWindow::onRefreshTimer);
    refresh_timer_->start(100);

    retranslateUi();

    updateConnectionStatus(false);
}

MainWindow::~MainWindow()
{
    if (gnss_collector_) gnss_collector_->stop();
    if (imu_collector_) imu_collector_->stop();
    if (ptb_collector_) ptb_collector_->stop();
    if (hmp_collector_) hmp_collector_->stop();
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        retranslateUi();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::setupMenuBar()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));

    QAction *exportAction = new QAction(this);
    exportAction->setShortcut(QKeySequence::Save);
    connect(exportAction, &QAction::triggered, this, &MainWindow::onExportClicked);
    fileMenu->addAction(exportAction);

    fileMenu->addSeparator();

    QAction *exitAction = new QAction(this);
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QMainWindow::close);
    fileMenu->addAction(exitAction);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));

    fullscreen_btn_ = new QAction(this);
    fullscreen_btn_->setShortcut(QKeySequence(Qt::Key_F11));
    connect(fullscreen_btn_, &QAction::triggered, this, &MainWindow::onToggleFullScreen);
    viewMenu->addAction(fullscreen_btn_);

    QMenu *langMenu = menuBar()->addMenu(tr("&Language"));

    lang_action_ = new QAction(this);
    connect(lang_action_, &QAction::triggered, this, &MainWindow::onSwitchLanguage);
    langMenu->addAction(lang_action_);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));

    QAction *aboutAction = new QAction(this);
    connect(aboutAction, &QAction::triggered, [this]() {
        QMessageBox::about(this, tr("About VaporView"),
            tr("VaporView Application\n\n"
               "Version 1.0.0\n\n"
               "Navigation System with RTK and IMU support.\n\n"
               "Supported devices:\n"
               "- UM982 RTK Receiver (PVTSLN)\n"
               "- HiPNUC IMU (HI81/HI83/HI91)\n"
               "- PTB210 Barometer\n"
               "- HMP3 Temperature/Humidity Sensor\n\n"
               "Press F11 for fullscreen mode."));
    });
    helpMenu->addAction(aboutAction);
}

void MainWindow::setupToolBar()
{
    QToolBar *toolbar = addToolBar(tr("Main Toolbar"));
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

    QAction *clearLogAction = new QAction(this);
    connect(clearLogAction, &QAction::triggered, this, &MainWindow::onClearLogClicked);
    toolbar->addAction(clearLogAction);

    toolbar->addSeparator();

    export_btn_ = new QAction(this);
    export_btn_->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    connect(export_btn_, &QAction::triggered, this, &MainWindow::onExportClicked);
    toolbar->addAction(export_btn_);

    toolbar->addSeparator();

    QAction *fullscreenAction = new QAction(this);
    fullscreenAction->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    connect(fullscreenAction, &QAction::triggered, this, &MainWindow::onToggleFullScreen);
    toolbar->addAction(fullscreenAction);
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

    auto createPortRow = [this, config_layout, &baudRates, &ports](const QString& name, QComboBox*& portCombo, QComboBox*& baudCombo, const QString& defaultPort, const QString& defaultBaud, int row) {
        auto *lbl = new QLabel(name, this);
        lbl->setStyleSheet("font-weight: bold; font-size: 9px;");
        config_layout->addWidget(lbl, row, 0);

        portCombo = new QComboBox(this);
        portCombo->addItem(tr("-- Select --"));
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

    int row = 0;
    createPortRow(tr("GNSS:"), gnss_port_combo_, gnss_baud_combo_, "/dev/ttyCOM3", "115200", row++);
    createPortRow(tr("IMU:"), imu_port_combo_, imu_baud_combo_, "/dev/ttyIMU", "115200", row++);
    createPortRow(tr("PTB210:"), ptb_port_combo_, ptb_baud_combo_, "/dev/ttyBARO", "9600", row++);
    createPortRow(tr("HMP3:"), hmp_port_combo_, hmp_baud_combo_, "/dev/ttyHMP", "19200", row++);

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

void MainWindow::retranslateUi()
{
    menuBar()->actions().at(0)->menu()->setTitle(tr("&File"));
    menuBar()->actions().at(0)->menu()->actions().at(0)->setText(tr("&Export Data..."));
    menuBar()->actions().at(0)->menu()->actions().at(2)->setText(tr("E&xit"));

    menuBar()->actions().at(1)->menu()->setTitle(tr("&View"));
    fullscreen_btn_->setText(tr("&Fullscreen"));

    menuBar()->actions().at(2)->menu()->setTitle(tr("&Language"));
    lang_action_->setText(is_english_ ? tr("Switch to Chinese") : tr("Switch to English"));

    menuBar()->actions().at(3)->menu()->setTitle(tr("&Help"));
    menuBar()->actions().at(3)->menu()->actions().at(0)->setText(tr("&About"));

    refresh_ports_btn_->setText(tr("Refresh"));
    connect_btn_->setText(tr("Connect"));
    disconnect_btn_->setText(tr("Disconnect"));
    export_btn_->setText(tr("Export"));

    status_label_->setText(tr("Ready"));

    config_group_->setTitle(tr("Serial Port Configuration"));
    data_group_->setTitle(tr("Sensor Data"));
    log_group_->setTitle(tr("Log"));

    gnss_group_->setTitle(tr("GNSS / RTK"));
    imu_group_->setTitle(tr("IMU"));
    ptb_group_->setTitle(tr("PTB210"));
    hmp_group_->setTitle(tr("HMP3"));

    gnss_panel_->retranslateUi();
    imu_panel_->retranslateUi();
    ptb_panel_->retranslateUi();
    hmp_panel_->retranslateUi();
}

void MainWindow::switchToLanguage(const QString& lang)
{
    qApp->removeTranslator(&translator_);

    if (lang == "zh")
    {
        QLocale::setDefault(QLocale(QLocale::Chinese));
    }
    else
    {
        QLocale::setDefault(QLocale(QLocale::English));
    }

    QEvent event(QEvent::LanguageChange);
    QCoreApplication::sendEvent(this, &event);
}

void MainWindow::onSwitchLanguage()
{
    is_english_ = !is_english_;
    switchToLanguage(is_english_ ? "en" : "zh");
    retranslateUi();
    log(tr("Language switched"));
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
        status_label_->setText(tr("Connected"));
        status_label_->setStyleSheet("color: green; font-weight: bold;");
    }
    else
    {
        status_label_->setText(tr("Disconnected"));
        status_label_->setStyleSheet("color: red;");
    }
}

void MainWindow::onToggleFullScreen()
{
    if (is_fullscreen_)
    {
        showNormal();
        setMaximumSize(1280, 720);
        resize(1280, 720);
        is_fullscreen_ = false;
        fullscreen_btn_->setText(tr("&Fullscreen"));
    }
    else
    {
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        showFullScreen();
        is_fullscreen_ = true;
        fullscreen_btn_->setText(tr("&Exit Fullscreen"));
    }
}

void MainWindow::onRefreshPortsClicked()
{
    QStringList ports = getAvailablePorts();

    auto updateCombo = [this, &ports](QComboBox* combo) {
        QString current = combo->currentText();
        combo->clear();
        combo->addItem(tr("-- Select --"));
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

    log(tr("Ports refreshed: %1 found").arg(ports.size()));
}

void MainWindow::onConnectClicked()
{
    log(tr("Connecting..."));

    gnss_collector_ = std::make_unique<VaproView::GnssCollector>();
    imu_collector_ = std::make_unique<VaproView::ImuCollector>();
    ptb_collector_ = std::make_unique<VaproView::PtbCollector>();
    hmp_collector_ = std::make_unique<VaproView::HmpCollector>();

    bool any_connected = false;

    QString gnss_port = gnss_port_combo_->currentText();
    if (gnss_port != tr("-- Select --") && !gnss_port.isEmpty())
    {
        VaproView::SerialConfig gnss_config = VaproView::SerialConfig::N81(gnss_baud_combo_->currentText().toInt());
        if (gnss_collector_->start(gnss_port.toStdString(), gnss_config))
        {
            log(tr("GNSS: %1 @ %2").arg(gnss_port, gnss_baud_combo_->currentText()));
            gnss_collector_->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onGnssDataReady", Qt::QueuedConnection); });
            any_connected = true;
        }
        else
        {
            log(tr("GNSS failed: %1").arg(QString::fromStdString(gnss_collector_->getLastError())));
        }
    }

    QString imu_port = imu_port_combo_->currentText();
    if (imu_port != tr("-- Select --") && !imu_port.isEmpty())
    {
        VaproView::SerialConfig imu_config = VaproView::SerialConfig::N81(imu_baud_combo_->currentText().toInt());
        if (imu_collector_->start(imu_port.toStdString(), imu_config))
        {
            log(tr("IMU: %1 @ %2").arg(imu_port, imu_baud_combo_->currentText()));
            imu_collector_->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onImuDataReady", Qt::QueuedConnection); });
            any_connected = true;
        }
        else
        {
            log(tr("IMU failed: %1").arg(QString::fromStdString(imu_collector_->getLastError())));
        }
    }

    QString ptb_port = ptb_port_combo_->currentText();
    if (ptb_port != tr("-- Select --") && !ptb_port.isEmpty())
    {
        VaproView::SerialConfig ptb_config = VaproView::SerialConfig::E71(ptb_baud_combo_->currentText().toInt());
        if (ptb_collector_->start(ptb_port.toStdString(), ptb_config))
        {
            log(tr("PTB210: %1 @ %2").arg(ptb_port, ptb_baud_combo_->currentText()));
            ptb_collector_->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onPtbDataReady", Qt::QueuedConnection); });
            any_connected = true;
        }
        else
        {
            log(tr("PTB210 failed: %1").arg(QString::fromStdString(ptb_collector_->getLastError())));
        }
    }

    QString hmp_port = hmp_port_combo_->currentText();
    if (hmp_port != tr("-- Select --") && !hmp_port.isEmpty())
    {
        VaproView::SerialConfig hmp_config = VaproView::SerialConfig::N82(hmp_baud_combo_->currentText().toInt());
        if (hmp_collector_->start(hmp_port.toStdString(), hmp_config))
        {
            log(tr("HMP3: %1 @ %2").arg(hmp_port, hmp_baud_combo_->currentText()));
            hmp_collector_->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onHmpDataReady", Qt::QueuedConnection); });
            any_connected = true;
        }
        else
        {
            log(tr("HMP3 failed: %1").arg(QString::fromStdString(hmp_collector_->getLastError())));
        }
    }

    if (any_connected)
    {
        updateConnectionStatus(true);
    }
    else
    {
        log(tr("No ports connected"));
    }
}

void MainWindow::onDisconnectClicked()
{
    log(tr("Disconnecting..."));

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
    log(tr("Disconnected"));
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
}

void MainWindow::onExportClicked()
{
    QString filename = QFileDialog::getSaveFileName(this, tr("Export Data"), QString(), tr("CSV Files (*.csv);;JSON Files (*.json)"));
    if (filename.isEmpty())
    {
        return;
    }

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("Error"), tr("Failed to open file for writing"));
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
    log(tr("Exported: %1").arg(filename));
}

void MainWindow::onClearLogClicked()
{
    log_text_edit_->clear();
    log(tr("Log cleared"));
}
