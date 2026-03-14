#include "MainWindow.h"
#include "RtkConfigDialog.h"
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
#include <QApplication>
#include <QLayout>
#include <QSerialPortInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <memory>

namespace
{
constexpr const char *kBaseMinWidthProperty = "_vv_base_min_width";
constexpr const char *kBaseMinHeightProperty = "_vv_base_min_height";
constexpr const char *kBaseMaxWidthProperty = "_vv_base_max_width";
constexpr const char *kBaseMaxHeightProperty = "_vv_base_max_height";
constexpr const char *kBaseSpacingProperty = "_vv_base_spacing";
constexpr const char *kBaseMarginsLeftProperty = "_vv_base_margin_left";
constexpr const char *kBaseMarginsTopProperty = "_vv_base_margin_top";
constexpr const char *kBaseMarginsRightProperty = "_vv_base_margin_right";
constexpr const char *kBaseMarginsBottomProperty = "_vv_base_margin_bottom";

void rememberBaseMetric(QObject *object, const char *propertyName, int value)
{
    if (!object->property(propertyName).isValid())
    {
        object->setProperty(propertyName, value);
    }
}
}

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
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(6, 6, 6, 6);

    rate_label_ = new QLabel(this);
    rate_label_->setObjectName("rateLabel");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    mainLayout->addWidget(rate_label_);

    auto *colsLayout = new QHBoxLayout();
    colsLayout->setSpacing(12);

    auto *leftLayout = new QGridLayout();
    leftLayout->setVerticalSpacing(4);
    leftLayout->setHorizontalSpacing(6);

    auto *midLayout = new QGridLayout();
    midLayout->setVerticalSpacing(4);
    midLayout->setHorizontalSpacing(6);

    auto *rightLayout = new QGridLayout();
    rightLayout->setVerticalSpacing(4);
    rightLayout->setHorizontalSpacing(6);

    auto createRow = [](QGridLayout* grid, int row, QLabel*& lbl, QLabel*& valueLabel, QWidget* parent) {
        lbl = new QLabel(parent);
        lbl->setObjectName("fieldLabel");
        lbl->setMinimumHeight(22);
        valueLabel = new QLabel("---", parent);
        valueLabel->setObjectName("valueLabel");
        valueLabel->setMinimumHeight(22);
        grid->addWidget(lbl, row, 0);
        grid->addWidget(valueLabel, row, 1);
    };

    createRow(leftLayout, 0, status_lbl_, status_label_, this);
    createRow(leftLayout, 1, lat_lbl_, lat_label_, this);
    createRow(leftLayout, 2, lon_lbl_, lon_label_, this);
    createRow(leftLayout, 3, alt_lbl_, alt_label_, this);
    createRow(leftLayout, 4, sigma_lat_lbl_, sigma_lat_label_, this);
    createRow(leftLayout, 5, sigma_lon_lbl_, sigma_lon_label_, this);
    createRow(leftLayout, 6, sigma_alt_lbl_, sigma_alt_label_, this);
    createRow(leftLayout, 7, undulation_lbl_, undulation_label_, this);

    createRow(midLayout, 0, vel_n_lbl_, vel_n_label_, this);
    createRow(midLayout, 1, vel_e_lbl_, vel_e_label_, this);
    createRow(midLayout, 2, vel_ground_lbl_, vel_ground_label_, this);
    createRow(midLayout, 3, heading_lbl_, heading_label_, this);
    createRow(midLayout, 4, pitch_lbl_, pitch_label_, this);
    createRow(midLayout, 5, heading_type_lbl_, heading_type_label_, this);
    createRow(midLayout, 6, heading_len_lbl_, heading_len_label_, this);
    createRow(midLayout, 7, heading_sats_lbl_, heading_sats_label_, this);
    createRow(midLayout, 8, sats_lbl_, sats_label_, this);
    createRow(midLayout, 9, diff_lbl_, diff_age_label_, this);

    createRow(rightLayout, 0, gdop_lbl_, gdop_label_, this);
    createRow(rightLayout, 1, pdop_lbl_, pdop_label_, this);
    createRow(rightLayout, 2, hdop_lbl_, hdop_label_, this);
    createRow(rightLayout, 3, htdop_lbl_, htdop_label_, this);
    createRow(rightLayout, 4, tdop_lbl_, tdop_label_, this);
    createRow(rightLayout, 5, cutoff_lbl_, cutoff_label_, this);

    leftLayout->setColumnStretch(1, 1);
    midLayout->setColumnStretch(1, 1);
    rightLayout->setColumnStretch(1, 1);

    colsLayout->addLayout(leftLayout, 1);
    colsLayout->addLayout(midLayout, 1);
    colsLayout->addLayout(rightLayout, 1);

    mainLayout->addLayout(colsLayout);
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
        sigma_lat_lbl_->setText("σ Lat:");
        sigma_lon_lbl_->setText("σ Lon:");
        sigma_alt_lbl_->setText("σ Alt:");
        undulation_lbl_->setText("Undul:");
        vel_n_lbl_->setText("Vel N:");
        vel_e_lbl_->setText("Vel E:");
        vel_ground_lbl_->setText("Vel Gnd:");
        heading_lbl_->setText("Heading:");
        pitch_lbl_->setText("Pitch:");
        heading_type_lbl_->setText("Hd Type:");
        heading_len_lbl_->setText("Base L:");
        heading_sats_lbl_->setText("Hd Sats:");
        sats_lbl_->setText("Sats:");
        diff_lbl_->setText("Diff:");
        gdop_lbl_->setText("GDOP:");
        pdop_lbl_->setText("PDOP:");
        hdop_lbl_->setText("HDOP:");
        htdop_lbl_->setText("HTDOP:");
        tdop_lbl_->setText("TDOP:");
        cutoff_lbl_->setText("Cutoff:");
    }
    else
    {
        status_lbl_->setText("状态:");
        lat_lbl_->setText("纬度:");
        lon_lbl_->setText("经度:");
        alt_lbl_->setText("高度:");
        sigma_lat_lbl_->setText("纬度σ:");
        sigma_lon_lbl_->setText("经度σ:");
        sigma_alt_lbl_->setText("高度σ:");
        undulation_lbl_->setText("异常高:");
        vel_n_lbl_->setText("北速:");
        vel_e_lbl_->setText("东速:");
        vel_ground_lbl_->setText("地速:");
        heading_lbl_->setText("航向:");
        pitch_lbl_->setText("俯仰:");
        heading_type_lbl_->setText("定向类型:");
        heading_len_lbl_->setText("基线长:");
        heading_sats_lbl_->setText("定向卫星:");
        sats_lbl_->setText("卫星:");
        diff_lbl_->setText("差分龄:");
        gdop_lbl_->setText("GDOP:");
        pdop_lbl_->setText("PDOP:");
        hdop_lbl_->setText("HDOP:");
        htdop_lbl_->setText("HTDOP:");
        tdop_lbl_->setText("TDOP:");
        cutoff_lbl_->setText("截止角:");
    }
}

void GnssPanel::updateData(const VaproView::GnssData& data)
{
    if (data.valid)
    {
        status_label_->setText(QString::fromStdString(data.position_status));
        status_label_->setProperty("data-valid", true);
        status_label_->style()->unpolish(status_label_);
        status_label_->style()->polish(status_label_);

        lat_label_->setText(QString::asprintf("%.8f°", data.latitude));
        lon_label_->setText(QString::asprintf("%.8f°", data.longitude));
        alt_label_->setText(QString::asprintf("%.3f m", data.altitude));
        sigma_lat_label_->setText(QString::asprintf("%.3f m", data.sigma_lat));
        sigma_lon_label_->setText(QString::asprintf("%.3f m", data.sigma_lon));
        sigma_alt_label_->setText(QString::asprintf("%.3f m", data.sigma_alt));
        undulation_label_->setText(QString::asprintf("%.3f m", data.undulation));
        vel_n_label_->setText(QString::asprintf("%.3f m/s", data.vel_north));
        vel_e_label_->setText(QString::asprintf("%.3f m/s", data.vel_east));
        vel_ground_label_->setText(QString::asprintf("%.3f m/s", data.vel_ground));
        heading_label_->setText(QString::asprintf("%.2f°", data.heading));
        pitch_label_->setText(QString::asprintf("%.2f°", data.heading_pitch));
        heading_type_label_->setText(QString::fromStdString(data.heading_type));
        heading_len_label_->setText(QString::asprintf("%.3f m", data.heading_length));
        heading_sats_label_->setText(QString("%1/%2").arg(data.heading_solnsvs).arg(data.heading_trackedsvs));
        sats_label_->setText(QString("%1/%2").arg(data.num_satellites_used).arg(data.num_satellites_tracked));
        diff_age_label_->setText(QString::asprintf("%.1f s", data.diff_age));
        gdop_label_->setText(QString::asprintf("%.2f", data.gdop));
        pdop_label_->setText(QString::asprintf("%.2f", data.pdop));
        hdop_label_->setText(QString::asprintf("%.2f", data.hdop));
        htdop_label_->setText(QString::asprintf("%.2f", data.htdop));
        tdop_label_->setText(QString::asprintf("%.2f", data.tdop));
        cutoff_label_->setText(QString::asprintf("%.1f°", data.elevation_cutoff));
    }
    else
    {
        status_label_->setText(QString::fromStdString(data.error_message));
        status_label_->setProperty("data-valid", false);
        status_label_->style()->unpolish(status_label_);
        status_label_->style()->polish(status_label_);
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
    , quat_sep_(nullptr)
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
    , quat_w_lbl_(nullptr)
    , quat_x_lbl_(nullptr)
    , quat_y_lbl_(nullptr)
    , quat_z_lbl_(nullptr)
    , is_english_(false)
{
    setupUi();
}

void ImuPanel::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(6, 6, 6, 6);

    rate_label_ = new QLabel(this);
    rate_label_->setObjectName("rateLabel");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rate_label_->setFixedHeight(28);
    mainLayout->addWidget(rate_label_);

    auto *colsLayout = new QHBoxLayout();
    colsLayout->setSpacing(12);

    auto *leftLayout = new QGridLayout();
    leftLayout->setVerticalSpacing(4);
    leftLayout->setHorizontalSpacing(6);

    auto *rightLayout = new QGridLayout();
    rightLayout->setVerticalSpacing(4);
    rightLayout->setHorizontalSpacing(6);

    auto createRow = [](QGridLayout* grid, int row, QLabel*& lbl, QLabel*& valueLabel, QWidget* parent) {
        lbl = new QLabel(parent);
        lbl->setObjectName("fieldLabel");
        lbl->setMinimumHeight(22);
        valueLabel = new QLabel("---", parent);
        valueLabel->setObjectName("valueLabel");
        valueLabel->setMinimumHeight(22);
        grid->addWidget(lbl, row, 0);
        grid->addWidget(valueLabel, row, 1);
    };

    auto createSeparator = [](QGridLayout* grid, int row, QLabel*& sep, QWidget* parent) {
        sep = new QLabel(parent);
        sep->setObjectName("separatorLabel");
        sep->setMinimumHeight(26);
        grid->addWidget(sep, row, 0, 1, 2);
    };

    createRow(leftLayout, 0, source_lbl_, source_label_, this);
    createSeparator(leftLayout, 1, accel_sep_, this);
    createRow(leftLayout, 2, acc_x_lbl_, acc_x_label_, this);
    createRow(leftLayout, 3, acc_y_lbl_, acc_y_label_, this);
    createRow(leftLayout, 4, acc_z_lbl_, acc_z_label_, this);
    createSeparator(leftLayout, 5, gyro_sep_, this);
    createRow(leftLayout, 6, gyr_x_lbl_, gyr_x_label_, this);
    createRow(leftLayout, 7, gyr_y_lbl_, gyr_y_label_, this);
    createRow(leftLayout, 8, gyr_z_lbl_, gyr_z_label_, this);

    createSeparator(rightLayout, 0, attitude_sep_, this);
    createRow(rightLayout, 1, roll_lbl_, roll_label_, this);
    createRow(rightLayout, 2, pitch_lbl_, pitch_label_, this);
    createRow(rightLayout, 3, yaw_lbl_, yaw_label_, this);
    createSeparator(rightLayout, 4, quat_sep_, this);
    createRow(rightLayout, 5, quat_w_lbl_, quat_w_label_, this);
    createRow(rightLayout, 6, quat_x_lbl_, quat_x_label_, this);
    createRow(rightLayout, 7, quat_y_lbl_, quat_y_label_, this);
    createRow(rightLayout, 8, quat_z_lbl_, quat_z_label_, this);

    leftLayout->setColumnStretch(1, 1);
    rightLayout->setColumnStretch(1, 1);

    colsLayout->addLayout(leftLayout, 1);
    colsLayout->addLayout(rightLayout, 1);

    mainLayout->addLayout(colsLayout);
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
        quat_sep_->setText("— Quaternion —");
        acc_x_lbl_->setText("X:");
        acc_y_lbl_->setText("Y:");
        acc_z_lbl_->setText("Z:");
        gyr_x_lbl_->setText("X:");
        gyr_y_lbl_->setText("Y:");
        gyr_z_lbl_->setText("Z:");
        roll_lbl_->setText("Roll:");
        pitch_lbl_->setText("Pitch:");
        yaw_lbl_->setText("Yaw:");
        quat_w_lbl_->setText("W:");
        quat_x_lbl_->setText("X:");
        quat_y_lbl_->setText("Y:");
        quat_z_lbl_->setText("Z:");
    }
    else
    {
        source_lbl_->setText("数据源:");
        accel_sep_->setText("— 加速度 —");
        gyro_sep_->setText("— 陀螺仪 —");
        attitude_sep_->setText("— 姿态 —");
        quat_sep_->setText("— 四元数 —");
        acc_x_lbl_->setText("X:");
        acc_y_lbl_->setText("Y:");
        acc_z_lbl_->setText("Z:");
        gyr_x_lbl_->setText("X:");
        gyr_y_lbl_->setText("Y:");
        gyr_z_lbl_->setText("Z:");
        roll_lbl_->setText("横滚:");
        pitch_lbl_->setText("俯仰:");
        yaw_lbl_->setText("航向:");
        quat_w_lbl_->setText("W:");
        quat_x_lbl_->setText("X:");
        quat_y_lbl_->setText("Y:");
        quat_z_lbl_->setText("Z:");
    }
}

void ImuPanel::updateData(const VaproView::ImuData& data)
{
    if (data.valid)
    {
        source_label_->setText(data.from_hi83 ? "HI83" : "HI91/HI81");
        source_label_->setProperty("data-valid", true);
        source_label_->style()->unpolish(source_label_);
        source_label_->style()->polish(source_label_);

        acc_x_label_->setText(QString::asprintf("%.3f", data.acceleration[0]));
        acc_y_label_->setText(QString::asprintf("%.3f", data.acceleration[1]));
        acc_z_label_->setText(QString::asprintf("%.3f", data.acceleration[2]));

        gyr_x_label_->setText(QString::asprintf("%.3f", data.gyroscope[0]));
        gyr_y_label_->setText(QString::asprintf("%.3f", data.gyroscope[1]));
        gyr_z_label_->setText(QString::asprintf("%.3f", data.gyroscope[2]));

        roll_label_->setText(QString::asprintf("%.2f°", data.rpy[0]));
        pitch_label_->setText(QString::asprintf("%.2f°", data.rpy[1]));
        yaw_label_->setText(QString::asprintf("%.2f°", data.rpy[2]));

        quat_w_label_->setText(QString::asprintf("%.4f", data.quaternion[0]));
        quat_x_label_->setText(QString::asprintf("%.4f", data.quaternion[1]));
        quat_y_label_->setText(QString::asprintf("%.4f", data.quaternion[2]));
        quat_z_label_->setText(QString::asprintf("%.4f", data.quaternion[3]));
    }
    else
    {
        source_label_->setText(QString::fromStdString(data.error_message));
        source_label_->setProperty("data-valid", false);
        source_label_->style()->unpolish(source_label_);
        source_label_->style()->polish(source_label_);
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
    layout->setSpacing(4);
    layout->setContentsMargins(6, 6, 6, 6);

    rate_label_ = new QLabel(this);
    rate_label_->setObjectName("rateLabel");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rate_label_->setMinimumHeight(22);
    layout->addWidget(rate_label_);

    auto *pressLayout = new QHBoxLayout();
    pressLayout->setSpacing(6);
    pressure_lbl_ = new QLabel(this);
    pressure_lbl_->setObjectName("fieldLabel");
    pressure_lbl_->setMinimumHeight(22);
    pressLayout->addWidget(pressure_lbl_);
    pressure_label_ = new QLabel("--- hPa", this);
    pressure_label_->setObjectName("highlightedValue");
    pressure_label_->setMinimumHeight(22);
    pressLayout->addWidget(pressure_label_);
    pressLayout->addStretch();
    layout->addLayout(pressLayout);

    status_label_ = new QLabel(this);
    status_label_->setObjectName("statusIndicator");
    status_label_->setMinimumHeight(22);
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
        pressure_label_->setProperty("data-valid", true);
        pressure_label_->style()->unpolish(pressure_label_);
        pressure_label_->style()->polish(pressure_label_);
        status_label_->setText(is_english_ ? "Valid" : "有效");
        status_label_->setProperty("status", "connected");
        status_label_->style()->unpolish(status_label_);
        status_label_->style()->polish(status_label_);
    }
    else
    {
        pressure_label_->setText("--- hPa");
        pressure_label_->setProperty("data-valid", false);
        pressure_label_->style()->unpolish(pressure_label_);
        pressure_label_->style()->polish(pressure_label_);
        status_label_->setText(QString::fromStdString(data.error_message));
        status_label_->setProperty("status", "disconnected");
        status_label_->style()->unpolish(status_label_);
        status_label_->style()->polish(status_label_);
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
    layout->setSpacing(4);
    layout->setContentsMargins(6, 6, 6, 6);

    rate_label_ = new QLabel(this);
    rate_label_->setObjectName("rateLabel");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rate_label_->setMinimumHeight(22);
    layout->addWidget(rate_label_);

    auto *tempLayout = new QHBoxLayout();
    tempLayout->setSpacing(6);
    temp_lbl_ = new QLabel(this);
    temp_lbl_->setObjectName("fieldLabel");
    temp_lbl_->setMinimumHeight(22);
    tempLayout->addWidget(temp_lbl_);
    temperature_label_ = new QLabel("--- °C", this);
    temperature_label_->setObjectName("highlightedValue");
    temperature_label_->setMinimumHeight(22);
    tempLayout->addWidget(temperature_label_);
    tempLayout->addStretch();
    layout->addLayout(tempLayout);

    auto *humidLayout = new QHBoxLayout();
    humidLayout->setSpacing(6);
    humidity_lbl_ = new QLabel(this);
    humidity_lbl_->setObjectName("fieldLabel");
    humidity_lbl_->setMinimumHeight(22);
    humidLayout->addWidget(humidity_lbl_);
    humidity_label_ = new QLabel("--- %RH", this);
    humidity_label_->setObjectName("highlightedValue");
    humidity_label_->setMinimumHeight(22);
    humidLayout->addWidget(humidity_label_);
    humidLayout->addStretch();
    layout->addLayout(humidLayout);

    status_label_ = new QLabel(this);
    status_label_->setObjectName("statusIndicator");
    status_label_->setMinimumHeight(22);
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
        temperature_label_->setProperty("data-valid", true);
        temperature_label_->style()->unpolish(temperature_label_);
        temperature_label_->style()->polish(temperature_label_);
        humidity_label_->setProperty("data-valid", true);
        humidity_label_->style()->unpolish(humidity_label_);
        humidity_label_->style()->polish(humidity_label_);
        status_label_->setText(is_english_ ? "Valid" : "有效");
        status_label_->setProperty("status", "connected");
        status_label_->style()->unpolish(status_label_);
        status_label_->style()->polish(status_label_);
    }
    else
    {
        temperature_label_->setText("--- °C");
        humidity_label_->setText("--- %RH");
        temperature_label_->setProperty("data-valid", false);
        temperature_label_->style()->unpolish(temperature_label_);
        temperature_label_->style()->polish(temperature_label_);
        humidity_label_->setProperty("data-valid", false);
        humidity_label_->style()->unpolish(humidity_label_);
        humidity_label_->style()->polish(humidity_label_);
        status_label_->setText(QString::fromStdString(data.error_message));
        status_label_->setProperty("status", "disconnected");
        status_label_->style()->unpolish(status_label_);
        status_label_->style()->polish(status_label_);
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
    , font_scale_group_(nullptr)
    , font_small_action_(nullptr)
    , font_normal_action_(nullptr)
    , font_large_action_(nullptr)
    , font_extra_large_action_(nullptr)
    , config_group_(nullptr)
    , data_group_(nullptr)
    , log_group_(nullptr)
    , gnss_group_(nullptr)
    , imu_group_(nullptr)
    , ptb_group_(nullptr)
    , hmp_group_(nullptr)
    , env_group_(nullptr)
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
    , has_inline_progress_log_(false)
    , font_scale_percent_(100)
    , base_font_point_size_(0.0)
    , gnss_sample_rate_(1)
    , imu_sample_rate_(1)
    , ptb_sample_rate_(1)
    , hmp_sample_rate_(1)
    , rtk_config_action_(nullptr)
    , rtk_config_dialog_(nullptr)
{
    const double currentPointSize = qApp->font().pointSizeF();
    base_font_point_size_ = currentPointSize > 0.0 ? currentPointSize : 10.0;

    QSettings settings("VaproView", "MainWindow");
    settings.remove("font_scale_percent");
    font_scale_percent_ = 100;

    loadModernStyleSheet();
    
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
    setupCentralWidget();

    resize(1280, 720);
    setMinimumSize(800, 600);

    refresh_timer_ = new QTimer(this);
    connect(refresh_timer_, &QTimer::timeout, this, &MainWindow::onRefreshTimer);
    refresh_timer_->start(100);

    setEnglish(false);
    applyStyleConfiguration();

    updateConnectionStatus(false);
}

MainWindow::~MainWindow()
{
    if (gnss_collector_) gnss_collector_->stop();
    if (imu_collector_) imu_collector_->stop();
    if (ptb_collector_) ptb_collector_->stop();
    if (hmp_collector_) hmp_collector_->stop();
}

void MainWindow::loadModernStyleSheet()
{
    QString stylePath = QCoreApplication::applicationDirPath() + "/../resources/modern_style.qss";
    QFile styleFile(stylePath);
    
    if (styleFile.open(QFile::ReadOnly | QFile::Text))
    {
        base_style_sheet_ = QString::fromUtf8(styleFile.readAll());
        styleFile.close();
    }
    else
    {
        base_style_sheet_ =
            "* { font-family: \"Segoe UI\", \"Microsoft YaHei\", \"PingFang SC\", sans-serif; }"
            "QMainWindow { background-color: #f5f5f5; }"
            "QMenuBar { background-color: #ffffff; border-bottom: 1px solid #e0e0e0; padding: 4px 8px; }"
            "QMenuBar::item { background-color: transparent; padding: 6px 12px; border-radius: 4px; color: #333333; }"
            "QMenuBar::item:selected { background-color: #e3f2fd; color: #1976d2; }"
            "QMenu { background-color: #ffffff; border: 1px solid #e0e0e0; border-radius: 6px; padding: 8px 0px; }"
            "QMenu::item { padding: 8px 32px 8px 16px; color: #333333; }"
            "QMenu::item:selected { background-color: #e3f2fd; color: #1976d2; }"
            "QToolBar { background-color: #ffffff; border-bottom: 1px solid #e0e0e0; padding: 8px 12px; spacing: 8px; }"
            "QToolBar QToolButton { background-color: transparent; border: none; border-radius: 6px; padding: 10px 14px; color: #555555; font-size: 15px; }"
            "QToolBar QToolButton:hover { background-color: #f0f0f0; }"
            "QToolBar QToolButton:disabled { color: #bdbdbd; }"
            "QStatusBar { background-color: #ffffff; border-top: 1px solid #e0e0e0; padding: 4px 12px; color: #666666; font-size: 14px; }"
            "QGroupBox { background-color: #ffffff; border: 1px solid #e0e0e0; border-radius: 8px; margin-top: 16px; padding: 16px 12px 12px 12px; font-size: 15px; font-weight: bold; color: #333333; }"
            "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 12px; padding: 0px 8px; background-color: #ffffff; color: #1976d2; }"
            "QLabel { color: #333333; background-color: transparent; border: none; }"
            "QComboBox { background-color: #ffffff; border: 1px solid #e0e0e0; border-radius: 6px; padding: 8px 12px; min-height: 34px; color: #333333; font-size: 14px; }"
            "QComboBox:hover { border-color: #bdbdbd; }"
            "QComboBox:focus { border-color: #1976d2; border-width: 2px; }"
            "QComboBox QAbstractItemView { background-color: #ffffff; border: 1px solid #e0e0e0; border-radius: 6px; selection-background-color: #e3f2fd; selection-color: #1976d2; padding: 4px; outline: none; }"
            "QTextEdit { background-color: #ffffff; color: #222222; border: 1px solid #e0e0e0; border-radius: 6px; padding: 10px; font-family: \"Consolas\", \"Monaco\", \"Courier New\", monospace; font-size: 13px; }"
            "QScrollBar:vertical { background-color: #f5f5f5; width: 12px; border-radius: 6px; }"
            "QScrollBar::handle:vertical { background-color: #bdbdbd; min-height: 30px; border-radius: 6px; margin: 2px; }"
            "QScrollBar::handle:vertical:hover { background-color: #9e9e9e; }"
            "QScrollBar:horizontal { background-color: #f5f5f5; height: 12px; border-radius: 6px; }"
            "QScrollBar::handle:horizontal { background-color: #bdbdbd; min-width: 30px; border-radius: 6px; margin: 2px; }"
            "QScrollBar::handle:horizontal:hover { background-color: #9e9e9e; }"
            "QPushButton { background-color: #1976d2; color: #ffffff; border: none; border-radius: 6px; padding: 10px 18px; font-size: 15px; font-weight: 500; min-height: 38px; }"
            "QPushButton:hover { background-color: #1565c0; }"
            "QPushButton:pressed { background-color: #0d47a1; }"
            "QPushButton:disabled { background-color: #bdbdbd; color: #ffffff; }"
            "QToolTip { background-color: #424242; color: #ffffff; border: none; border-radius: 4px; padding: 6px 10px; font-size: 13px; }";
    }

    applyStyleConfiguration();
}

QString MainWindow::scaledStyleSheet(const QString& styleSheet) const
{
    const QRegularExpression pixelRegex(R"((\d+)px)");
    QString scaled = styleSheet;
    QRegularExpressionMatchIterator it = pixelRegex.globalMatch(styleSheet);
    struct Replacement
    {
        int start;
        int length;
        QString text;
    };
    QList<Replacement> replacements;

    while (it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();
        const int originalPx = match.captured(1).toInt();
        const int scaledPx = originalPx == 0 ? 0 : std::max(1, scalePixels(originalPx));
        replacements.append({match.capturedStart(0), match.capturedLength(0), QString("%1px").arg(scaledPx)});
    }

    for (auto replacementIt = replacements.crbegin(); replacementIt != replacements.crend(); ++replacementIt)
    {
        scaled.replace(replacementIt->start, replacementIt->length, replacementIt->text);
    }

    return scaled;
}

int MainWindow::scalePixels(int pixels) const
{
    return static_cast<int>(std::lround(pixels * font_scale_percent_ / 100.0));
}

void MainWindow::applyScaledUiMetrics()
{
    auto applyWidgetMetrics = [this](QWidget *widget) {
        if (!widget)
        {
            return;
        }

        const int minimumWidth = widget->minimumWidth();
        if (minimumWidth > 0)
        {
            rememberBaseMetric(widget, kBaseMinWidthProperty, minimumWidth);
            widget->setMinimumWidth(std::max(1, scalePixels(widget->property(kBaseMinWidthProperty).toInt())));
        }

        const int minimumHeight = widget->minimumHeight();
        if (minimumHeight > 0)
        {
            rememberBaseMetric(widget, kBaseMinHeightProperty, minimumHeight);
            widget->setMinimumHeight(std::max(1, scalePixels(widget->property(kBaseMinHeightProperty).toInt())));
        }

        const int maximumWidth = widget->maximumWidth();
        if (maximumWidth > 0 && maximumWidth < QWIDGETSIZE_MAX)
        {
            rememberBaseMetric(widget, kBaseMaxWidthProperty, maximumWidth);
            widget->setMaximumWidth(std::max(1, scalePixels(widget->property(kBaseMaxWidthProperty).toInt())));
        }

        const int maximumHeight = widget->maximumHeight();
        if (maximumHeight > 0 && maximumHeight < QWIDGETSIZE_MAX)
        {
            rememberBaseMetric(widget, kBaseMaxHeightProperty, maximumHeight);
            widget->setMaximumHeight(std::max(1, scalePixels(widget->property(kBaseMaxHeightProperty).toInt())));
        }
    };

    applyWidgetMetrics(this);
    for (QWidget *widget : findChildren<QWidget*>())
    {
        applyWidgetMetrics(widget);
    }

    auto applyLayoutMetrics = [this](QLayout *layout) {
        if (!layout)
        {
            return;
        }

        if (layout->spacing() >= 0)
        {
            rememberBaseMetric(layout, kBaseSpacingProperty, layout->spacing());
            layout->setSpacing(std::max(0, scalePixels(layout->property(kBaseSpacingProperty).toInt())));
        }

        const QMargins margins = layout->contentsMargins();
        rememberBaseMetric(layout, kBaseMarginsLeftProperty, margins.left());
        rememberBaseMetric(layout, kBaseMarginsTopProperty, margins.top());
        rememberBaseMetric(layout, kBaseMarginsRightProperty, margins.right());
        rememberBaseMetric(layout, kBaseMarginsBottomProperty, margins.bottom());
        layout->setContentsMargins(
            std::max(0, scalePixels(layout->property(kBaseMarginsLeftProperty).toInt())),
            std::max(0, scalePixels(layout->property(kBaseMarginsTopProperty).toInt())),
            std::max(0, scalePixels(layout->property(kBaseMarginsRightProperty).toInt())),
            std::max(0, scalePixels(layout->property(kBaseMarginsBottomProperty).toInt()))
        );
    };

    if (layout())
    {
        applyLayoutMetrics(layout());
    }

    for (QLayout *layout : findChildren<QLayout*>())
    {
        applyLayoutMetrics(layout);
    }
}

void MainWindow::applyStyleConfiguration()
{
    QFont appFont = qApp->font();
    appFont.setPointSizeF(base_font_point_size_ * font_scale_percent_ / 100.0);
    qApp->setFont(appFont);
    qApp->setStyleSheet(scaledStyleSheet(base_style_sheet_));
    applyScaledUiMetrics();

    if (!isFullScreen() && !isMaximized())
    {
        const QSize targetSize = size().expandedTo(minimumSize()).expandedTo(minimumSizeHint());
        if (targetSize != size())
        {
            resize(targetSize);
        }
    }
}

void MainWindow::setFontScale(int percent)
{
    if (percent < 85 || percent > 150 || font_scale_percent_ == percent)
    {
        return;
    }

    font_scale_percent_ = percent;
    applyStyleConfiguration();
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

    QMenu *fontMenu = menuBar()->addMenu("");
    font_scale_group_ = new QActionGroup(this);
    font_scale_group_->setExclusive(true);

    font_small_action_ = new QAction(this);
    font_small_action_->setCheckable(true);
    font_small_action_->setData(90);
    font_scale_group_->addAction(font_small_action_);
    fontMenu->addAction(font_small_action_);

    font_normal_action_ = new QAction(this);
    font_normal_action_->setCheckable(true);
    font_normal_action_->setData(100);
    font_scale_group_->addAction(font_normal_action_);
    fontMenu->addAction(font_normal_action_);

    font_large_action_ = new QAction(this);
    font_large_action_->setCheckable(true);
    font_large_action_->setData(115);
    font_scale_group_->addAction(font_large_action_);
    fontMenu->addAction(font_large_action_);

    font_extra_large_action_ = new QAction(this);
    font_extra_large_action_->setCheckable(true);
    font_extra_large_action_->setData(130);
    font_scale_group_->addAction(font_extra_large_action_);
    fontMenu->addAction(font_extra_large_action_);

    connect(font_scale_group_, &QActionGroup::triggered, this, &MainWindow::onFontScaleTriggered);

    if (font_scale_percent_ <= 95)
    {
        font_small_action_->setChecked(true);
    }
    else if (font_scale_percent_ <= 107)
    {
        font_normal_action_->setChecked(true);
    }
    else if (font_scale_percent_ <= 122)
    {
        font_large_action_->setChecked(true);
    }
    else
    {
        font_extra_large_action_->setChecked(true);
    }

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

    rtk_config_action_ = new QAction(this);
    rtk_config_action_->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
    connect(rtk_config_action_, &QAction::triggered, this, &MainWindow::onRtkConfigClicked);
    toolbar->addAction(rtk_config_action_);

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

    auto *main_h_layout = new QHBoxLayout(central_widget_);
    main_h_layout->setSpacing(2);
    main_h_layout->setContentsMargins(2, 2, 2, 2);

    auto *left_widget = new QWidget(this);
    main_layout_ = new QVBoxLayout(left_widget);
    main_layout_->setSpacing(2);
    main_layout_->setContentsMargins(0, 0, 0, 0);

    setupConfigPanel();
    setupDataPanels();

    main_h_layout->addWidget(left_widget, 3);

    setupLogPanel();
    main_h_layout->addWidget(log_group_, 1);
}

QStringList MainWindow::getAvailablePorts()
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

void MainWindow::setupConfigPanel()
{
    config_group_ = new QGroupBox(this);
    config_group_->setMinimumWidth(760);
    auto *config_layout = new QGridLayout(config_group_);
    config_layout->setVerticalSpacing(8);
    config_layout->setHorizontalSpacing(8);
    config_layout->setContentsMargins(8, 4, 8, 8);

    config_layout->setColumnStretch(0, 0);
    config_layout->setColumnStretch(1, 1);
    config_layout->setColumnStretch(2, 0);
    config_layout->setColumnStretch(3, 0);
    config_layout->setColumnStretch(4, 0);
    config_layout->setColumnMinimumWidth(0, 90);
    config_layout->setColumnMinimumWidth(1, 260);
    config_layout->setColumnMinimumWidth(2, 110);
    config_layout->setColumnMinimumWidth(3, 60);
    config_layout->setColumnMinimumWidth(4, 110);

    QStringList baudRates = {"9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600"};
    QStringList ports = getAvailablePorts();

    auto createRateCombo = [this]() {
        auto *combo = new QComboBox(this);
        combo->addItem("1");
        combo->addItem("2");
        combo->addItem("5");
        combo->addItem("10");
        combo->addItem("20");
        combo->addItem("50");
        combo->addItem("100");
        combo->addItem("200");
        combo->addItem("500");
        combo->setCurrentIndex(4);
        combo->setEditable(true);
        combo->setFixedHeight(30);
        combo->setFixedWidth(100);
        combo->setValidator(new QIntValidator(1, 500, combo));
        return combo;
    };

    auto createPortRow = [this, config_layout, &baudRates, &ports, &createRateCombo](QLabel*& lbl, QComboBox*& portCombo, QComboBox*& baudCombo, QLabel*& rateLbl, QComboBox*& rateCombo, const QString& defaultPort, const QString& defaultBaud, int row) {
        lbl = new QLabel(this);
        lbl->setObjectName("fieldLabel");
        lbl->setFixedHeight(28);
        lbl->setFixedWidth(80);
        config_layout->addWidget(lbl, row, 0, Qt::AlignVCenter | Qt::AlignLeft);

        portCombo = new QComboBox(this);
        portCombo->addItem(is_english_ ? "-- Select --" : "-- 选择 --");
        portCombo->addItems(ports);
        portCombo->setEditable(true);
        portCombo->setFixedHeight(30);
        portCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
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
        config_layout->addWidget(portCombo, row, 1, Qt::AlignVCenter);

        baudCombo = new QComboBox(this);
        baudCombo->addItems(baudRates);
        baudCombo->setCurrentText(defaultBaud);
        baudCombo->setFixedHeight(30);
        baudCombo->setFixedWidth(100);
        config_layout->addWidget(baudCombo, row, 2, Qt::AlignVCenter);

        rateLbl = new QLabel(this);
        rateLbl->setObjectName("fieldLabel");
        rateLbl->setFixedHeight(28);
        config_layout->addWidget(rateLbl, row, 3, Qt::AlignVCenter | Qt::AlignRight);

        rateCombo = createRateCombo();
        config_layout->addWidget(rateCombo, row, 4, Qt::AlignVCenter);
    };

    int row = 0;

    global_rate_lbl_ = new QLabel(this);
    global_rate_lbl_->setObjectName("fieldLabel");
    global_rate_lbl_->setFixedHeight(28);
    config_layout->addWidget(global_rate_lbl_, row, 3, Qt::AlignVCenter | Qt::AlignRight);

    global_rate_combo_ = createRateCombo();
    config_layout->addWidget(global_rate_combo_, row, 4, Qt::AlignVCenter);
    connect(global_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onGlobalRateChanged);
    ++row;

#ifdef _WIN32
    createPortRow(gnss_lbl_, gnss_port_combo_, gnss_baud_combo_, gnss_rate_lbl_, gnss_rate_combo_, "COM3", "115200", row++);
    createPortRow(imu_lbl_, imu_port_combo_, imu_baud_combo_, imu_rate_lbl_, imu_rate_combo_, "COM4", "115200", row++);
    createPortRow(ptb_lbl_, ptb_port_combo_, ptb_baud_combo_, ptb_rate_lbl_, ptb_rate_combo_, "COM5", "9600", row++);
    createPortRow(hmp_lbl_, hmp_port_combo_, hmp_baud_combo_, hmp_rate_lbl_, hmp_rate_combo_, "COM6", "19200", row++);
#else
    createPortRow(gnss_lbl_, gnss_port_combo_, gnss_baud_combo_, gnss_rate_lbl_, gnss_rate_combo_, "/dev/ttyCOM3", "115200", row++);
    createPortRow(imu_lbl_, imu_port_combo_, imu_baud_combo_, imu_rate_lbl_, imu_rate_combo_, "/dev/ttyIMU", "115200", row++);
    createPortRow(ptb_lbl_, ptb_port_combo_, ptb_baud_combo_, ptb_rate_lbl_, ptb_rate_combo_, "/dev/ttyBARO", "9600", row++);
    createPortRow(hmp_lbl_, hmp_port_combo_, hmp_baud_combo_, hmp_rate_lbl_, hmp_rate_combo_, "/dev/ttyHMP", "19200", row++);
#endif

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
    data_layout->setSpacing(2);
    data_layout->setContentsMargins(2, 2, 2, 2);

    gnss_group_ = new QGroupBox(this);
    gnss_group_->setObjectName("sensorGroupBox");
    auto *gnss_layout = new QVBoxLayout(gnss_group_);
    gnss_layout->setContentsMargins(2, 2, 2, 2);
    gnss_panel_ = new GnssPanel(this);
    gnss_layout->addWidget(gnss_panel_);
    data_layout->addWidget(gnss_group_);

    imu_group_ = new QGroupBox(this);
    imu_group_->setObjectName("sensorGroupBox");
    auto *imu_layout = new QVBoxLayout(imu_group_);
    imu_layout->setContentsMargins(2, 2, 2, 2);
    imu_panel_ = new ImuPanel(this);
    imu_layout->addWidget(imu_panel_);
    data_layout->addWidget(imu_group_);

    auto *env_group = new QGroupBox(this);
    env_group->setObjectName("sensorGroupBox");
    auto *env_layout = new QVBoxLayout(env_group);
    env_layout->setContentsMargins(2, 2, 2, 2);
    env_layout->setSpacing(2);

    ptb_panel_ = new PtbPanel(this);
    env_layout->addWidget(ptb_panel_);

    hmp_panel_ = new HmpPanel(this);
    env_layout->addWidget(hmp_panel_);

    data_layout->addWidget(env_group);
    env_group_ = env_group;

    ptb_group_ = nullptr;
    hmp_group_ = nullptr;

    main_layout_->addWidget(data_group_, 1);
}

void MainWindow::setupLogPanel()
{
    log_group_ = new QGroupBox(this);
    auto *log_layout = new QVBoxLayout(log_group_);
    log_layout->setContentsMargins(4, 4, 4, 4);

    log_text_edit_ = new QTextEdit(this);
    log_text_edit_->setReadOnly(true);
    log_text_edit_->setMinimumWidth(200);
    log_layout->addWidget(log_text_edit_);
}

void MainWindow::setEnglish(bool english)
{
    is_english_ = english;

    menuBar()->actions().at(0)->menu()->setTitle(english ? "&File" : "文件(&F)");
    export_action_->setText(english ? "&Export Data..." : "导出数据(&E)...");
    exit_action_->setText(english ? "E&xit" : "退出(&X)");

    menuBar()->actions().at(1)->menu()->setTitle(english ? "&View" : "视图(&V)");
    fullscreen_btn_->setText(english ? "&Fullscreen" : "全屏(&F)");

    menuBar()->actions().at(2)->menu()->setTitle(english ? "Font &Size" : "字号(&S)");
    font_small_action_->setText(english ? "Small (90%)" : "小号 (90%)");
    font_normal_action_->setText(english ? "Normal (100%)" : "标准 (100%)");
    font_large_action_->setText(english ? "Large (115%)" : "大号 (115%)");
    font_extra_large_action_->setText(english ? "Extra Large (130%)" : "超大 (130%)");

    menuBar()->actions().at(3)->menu()->setTitle(english ? "&Language" : "语言(&L)");
    lang_action_->setText(english ? "Switch to Chinese" : "切换到英文");

    menuBar()->actions().at(4)->menu()->setTitle(english ? "&Help" : "帮助(&H)");
    about_action_->setText(english ? "&About" : "关于(&A)");

    refresh_ports_btn_->setText(english ? "Refresh" : "刷新");
    connect_btn_->setText(english ? "Connect" : "连接");
    disconnect_btn_->setText(english ? "Disconnect" : "断开");
    clear_log_action_->setText(english ? "Clear" : "清空");
    export_btn_->setText(english ? "Export" : "导出");
    fullscreen_btn_->setText(english ? "Fullscreen" : "全屏");
    rtk_config_action_->setText(english ? "RTK Config" : "RTK配置");

    status_label_->setText(english ? "Ready" : "就绪");

    config_group_->setTitle(english ? "Serial Port Configuration" : "串口配置");
    data_group_->setTitle(english ? "Sensor Data" : "传感器数据");
    log_group_->setTitle(english ? "Log" : "日志");

    gnss_group_->setTitle(english ? "GNSS / RTK" : "GNSS / RTK");
    imu_group_->setTitle(english ? "IMU" : "IMU");
    env_group_->setTitle(english ? "Environment" : "环境参数");

    gnss_lbl_->setText(english ? "GNSS:" : "GNSS:");
    imu_lbl_->setText(english ? "IMU:" : "IMU:");
    ptb_lbl_->setText(english ? "PTB210:" : "PTB210:");
    hmp_lbl_->setText(english ? "HMP3:" : "HMP3:");

    global_rate_lbl_->setText(english ? "Global Rate:" : "统一频率:");
    gnss_rate_lbl_->setText(english ? "Rate:" : "频率:");
    imu_rate_lbl_->setText(english ? "Rate:" : "频率:");
    ptb_rate_lbl_->setText(english ? "Rate:" : "频率:");
    hmp_rate_lbl_->setText(english ? "Rate:" : "频率:");

    gnss_panel_->setEnglish(english);
    imu_panel_->setEnglish(english);
    ptb_panel_->setEnglish(english);
    hmp_panel_->setEnglish(english);

    if (rtk_config_dialog_)
    {
        rtk_config_dialog_->setWindowTitle(english ? "RTK NTRIP Configuration" : "RTK NTRIP 配置");
    }
}

void MainWindow::onSwitchLanguage()
{
    is_english_ = !is_english_;
    setEnglish(is_english_);
    log(is_english_ ? "Language switched to English" : "语言已切换为中文");
}

void MainWindow::onFontScaleTriggered(QAction *action)
{
    if (!action)
    {
        return;
    }

    const int percent = action->data().toInt();
    if (percent == font_scale_percent_)
    {
        return;
    }

    setFontScale(percent);
    log(QString(is_english_ ? "Font size set to %1%" : "字体大小已设置为 %1%").arg(percent));
}

int MainWindow::parseRate(const QString& text)
{
    bool ok;
    int rate = text.toInt(&ok);
    if (ok && rate >= 1 && rate <= 500)
    {
        return rate;
    }
    return 20;
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
    
    if (gnss_collector_ && gnss_collector_->isRunning())
    {
        gnss_collector_->setSampleRate(rate);
        gnss_collector_->setDeviceSampleRate(rate);
    }
    if (imu_collector_ && imu_collector_->isRunning())
    {
        imu_collector_->setSampleRate(rate);
        imu_collector_->setDeviceSampleRate(rate);
    }
    if (ptb_collector_ && ptb_collector_->isRunning())
    {
        ptb_collector_->setSampleRate(rate);
        ptb_collector_->setDeviceSampleRate(rate);
    }
    if (hmp_collector_ && hmp_collector_->isRunning())
    {
        hmp_collector_->setSampleRate(rate);
    }
    
    log(QString(is_english_ ? "All rates set to %1 Hz" : "所有频率已设置为 %1 Hz").arg(rate));
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
    if (imu_collector_)
    {
        imu_collector_->setSampleRate(imu_sample_rate_);
        if (imu_collector_->isRunning())
        {
            imu_collector_->setDeviceSampleRate(imu_sample_rate_);
        }
    }
    log(QString(is_english_ ? "IMU sample rate set to %1 Hz" : "IMU采样频率已设置为 %1 Hz").arg(imu_sample_rate_));
}

void MainWindow::onPtbRateChanged(const QString& text)
{
    ptb_sample_rate_ = parseRate(text);
    if (ptb_collector_)
    {
        ptb_collector_->setSampleRate(ptb_sample_rate_);
        if (ptb_collector_->isRunning())
        {
            ptb_collector_->setDeviceSampleRate(ptb_sample_rate_);
        }
    }
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
    int rate = parseRate(global_rate_combo_->currentText());

    if (gnss_collector_ && gnss_collector_->isRunning())
    {
        gnss_collector_->setSampleRate(rate);
        gnss_collector_->setDeviceSampleRate(rate);
    }
    if (imu_collector_ && imu_collector_->isRunning())
    {
        imu_collector_->setSampleRate(rate);
        imu_collector_->setDeviceSampleRate(rate);
    }
    if (ptb_collector_ && ptb_collector_->isRunning())
    {
        ptb_collector_->setSampleRate(rate);
        ptb_collector_->setDeviceSampleRate(rate);
    }
    if (hmp_collector_ && hmp_collector_->isRunning())
    {
        hmp_collector_->setSampleRate(rate);
    }

    gnss_rate_combo_->blockSignals(true);
    imu_rate_combo_->blockSignals(true);
    ptb_rate_combo_->blockSignals(true);
    hmp_rate_combo_->blockSignals(true);

    gnss_rate_combo_->setCurrentText(QString::number(rate));
    imu_rate_combo_->setCurrentText(QString::number(rate));
    ptb_rate_combo_->setCurrentText(QString::number(rate));
    hmp_rate_combo_->setCurrentText(QString::number(rate));

    gnss_rate_combo_->blockSignals(false);
    imu_rate_combo_->blockSignals(false);
    ptb_rate_combo_->blockSignals(false);
    hmp_rate_combo_->blockSignals(false);

    gnss_sample_rate_ = rate;
    imu_sample_rate_ = rate;
    ptb_sample_rate_ = rate;
    hmp_sample_rate_ = rate;

    log(QString(is_english_ ? "All rates set to %1 Hz" : "所有频率已设置为 %1 Hz").arg(rate));
}

void MainWindow::onToggleFullScreen()
{
    if (is_fullscreen_)
    {
        showNormal();
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
    if (message.startsWith('\r'))
    {
        const QString inlineMessage = message.mid(1);
        QTextCursor cursor = log_text_edit_->textCursor();
        cursor.movePosition(QTextCursor::End);

        if (!has_inline_progress_log_)
        {
            if (!log_text_edit_->document()->isEmpty())
            {
                cursor.insertBlock();
            }
            cursor.insertText(inlineMessage);
            has_inline_progress_log_ = true;
        }
        else
        {
            cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            cursor.insertText(inlineMessage);
        }

        log_text_edit_->setTextCursor(cursor);
        log_text_edit_->ensureCursorVisible();
        return;
    }

    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    log_text_edit_->append(QString("[%1] %2").arg(timestamp, message));
    has_inline_progress_log_ = false;
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
        status_label_->setProperty("status", "connected");
    }
    else
    {
        status_label_->setText(is_english_ ? "Disconnected" : "未连接");
        status_label_->setProperty("status", "disconnected");
    }
    status_label_->style()->unpolish(status_label_);
    status_label_->style()->polish(status_label_);
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

    current_gnss_ = VaproView::GnssData();
    current_imu_ = VaproView::ImuData();
    current_ptb_ = VaproView::PtbData();
    current_hmp_ = VaproView::HmpData();

    gnss_panel_->updateData(current_gnss_);
    imu_panel_->updateData(current_imu_);
    ptb_panel_->updateData(current_ptb_);
    hmp_panel_->updateData(current_hmp_);

    gnss_panel_->updateRate(0.0);
    imu_panel_->updateRate(0.0);
    ptb_panel_->updateRate(0.0);
    hmp_panel_->updateRate(0.0);

    gnss_collector_ = std::make_unique<VaproView::GnssCollector>();
    imu_collector_ = std::make_unique<VaproView::ImuCollector>();
    ptb_collector_ = std::make_unique<VaproView::PtbCollector>();
    hmp_collector_ = std::make_unique<VaproView::HmpCollector>();

    gnss_collector_->setSampleRate(gnss_sample_rate_);
    imu_collector_->setSampleRate(imu_sample_rate_);
    ptb_collector_->setSampleRate(ptb_sample_rate_);
    hmp_collector_->setSampleRate(hmp_sample_rate_);

    auto logCallback = [this](const std::string& msg) {
        const QString qmsg = QString::fromStdString(msg);
        if (QThread::currentThread() == thread())
        {
            log(qmsg);
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            return;
        }

        QMetaObject::invokeMethod(this, [this, qmsg]() {
            log(qmsg);
        }, Qt::QueuedConnection);
    };
    
    gnss_collector_->setLogCallback(logCallback);
    imu_collector_->setLogCallback(logCallback);
    ptb_collector_->setLogCallback(logCallback);
    hmp_collector_->setLogCallback(logCallback);

    int total_devices = 0;
    int connected_devices = 0;
    QString selectText = is_english_ ? "-- Select --" : "-- 选择 --";

    log(is_english_ ? "========== Starting Connection ==========" : "========== 开始连接 ==========");
    QApplication::processEvents();

    QString gnss_port = gnss_port_combo_->currentText();
    if (gnss_port != selectText && !gnss_port.isEmpty())
    {
        total_devices++;
        log(QString(is_english_ ? "[GNSS] Checking port: %1" : "[GNSS] 检查端口: %1").arg(gnss_port));
        QApplication::processEvents();
        
        log(QString(is_english_ ? "[GNSS] Port selected, connecting..." : "[GNSS] 已选择端口，正在连接..."));
        QApplication::processEvents();
        
        VaproView::SerialConfig gnss_config = VaproView::SerialConfig::N81(gnss_baud_combo_->currentText().toInt());
        if (gnss_collector_->start(gnss_port.toStdString(), gnss_config))
        {
            log(QString(is_english_ ? "[GNSS] Serial port opened, checking device response..." : "[GNSS] 串口已打开，正在检测设备响应..."));
            QApplication::processEvents();
            
            if (gnss_collector_->checkDeviceResponse())
            {
                log(QString(is_english_ ? "[GNSS] Device responding, connected: %1 @ %2 baud" : "[GNSS] 设备响应正常，连接成功: %1 @ %2 波特率").arg(gnss_port, gnss_baud_combo_->currentText()));
                gnss_collector_->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onGnssDataReady", Qt::QueuedConnection); });
                if (gnss_collector_->startStreaming())
                {
                    connected_devices++;
                }
                else
                {
                    log(is_english_ ? "[GNSS] Failed to start data stream." : "[GNSS] 启动数据流失败。");
                    gnss_collector_->stop();
                }
            }
            else
            {
                log(is_english_ ? "[GNSS] Device not responding! Check power and cables." : "[GNSS] 设备无响应！请检查电源和连接线。");
                gnss_collector_->stop();
            }
        }
        else
        {
            log(QString(is_english_ ? "[GNSS] Failed to open port: %1" : "[GNSS] 打开端口失败: %1").arg(QString::fromStdString(gnss_collector_->getLastError())));
        }
    }
    else
    {
        log(is_english_ ? "[GNSS] Skipped (not selected)" : "[GNSS] 跳过 (未选择)");
    }
    QApplication::processEvents();

    QString imu_port = imu_port_combo_->currentText();
    if (imu_port != selectText && !imu_port.isEmpty())
    {
        total_devices++;
        log(QString(is_english_ ? "[IMU] Checking port: %1" : "[IMU] 检查端口: %1").arg(imu_port));
        QApplication::processEvents();
        
        log(QString(is_english_ ? "[IMU] Port selected, connecting..." : "[IMU] 已选择端口，正在连接..."));
        QApplication::processEvents();
        
        VaproView::SerialConfig imu_config = VaproView::SerialConfig::N81(imu_baud_combo_->currentText().toInt());
        if (imu_collector_->start(imu_port.toStdString(), imu_config))
        {
            log(QString(is_english_ ? "[IMU] Serial port opened, checking device response..." : "[IMU] 串口已打开，正在检测设备响应..."));
            QApplication::processEvents();
            
            if (imu_collector_->checkDeviceResponse())
            {
                log(QString(is_english_ ? "[IMU] Device responding, connected: %1 @ %2 baud" : "[IMU] 设备响应正常，连接成功: %1 @ %2 波特率").arg(imu_port, imu_baud_combo_->currentText()));
                imu_collector_->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onImuDataReady", Qt::QueuedConnection); });
                int imu_rate = parseRate(imu_rate_combo_->currentText());
                imu_collector_->setSampleRate(imu_rate);
                imu_collector_->setDeviceSampleRate(imu_rate);
                log(QString(is_english_ ? "[IMU] Sample rate set to %1 Hz" : "[IMU] 采样频率设置为 %1 Hz").arg(imu_rate));
                if (imu_collector_->startStreaming())
                {
                    connected_devices++;
                }
                else
                {
                    log(is_english_ ? "[IMU] Failed to start data stream." : "[IMU] 启动数据流失败。");
                    imu_collector_->stop();
                }
            }
            else
            {
                log(is_english_ ? "[IMU] Device not responding! Check power and cables." : "[IMU] 设备无响应！请检查电源和连接线。");
                imu_collector_->stop();
            }
        }
        else
        {
            log(QString(is_english_ ? "[IMU] Failed to open port: %1" : "[IMU] 打开端口失败: %1").arg(QString::fromStdString(imu_collector_->getLastError())));
        }
    }
    else
    {
        log(is_english_ ? "[IMU] Skipped (not selected)" : "[IMU] 跳过 (未选择)");
    }
    QApplication::processEvents();

    QString ptb_port = ptb_port_combo_->currentText();
    if (ptb_port != selectText && !ptb_port.isEmpty())
    {
        total_devices++;
        log(QString(is_english_ ? "[PTB] Checking port: %1" : "[PTB] 检查端口: %1").arg(ptb_port));
        QApplication::processEvents();
        
        log(QString(is_english_ ? "[PTB] Port selected, connecting..." : "[PTB] 已选择端口，正在连接..."));
        QApplication::processEvents();
        
        VaproView::SerialConfig ptb_config = VaproView::SerialConfig::E71(ptb_baud_combo_->currentText().toInt());
        if (ptb_collector_->start(ptb_port.toStdString(), ptb_config))
        {
            log(QString(is_english_ ? "[PTB] Serial port opened, checking device response..." : "[PTB] 串口已打开，正在检测设备响应..."));
            QApplication::processEvents();
            
            if (ptb_collector_->checkDeviceResponse())
            {
                log(QString(is_english_ ? "[PTB] Device responding, connected: %1 @ %2 baud" : "[PTB] 设备响应正常，连接成功: %1 @ %2 波特率").arg(ptb_port, ptb_baud_combo_->currentText()));
                ptb_collector_->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onPtbDataReady", Qt::QueuedConnection); });
                int ptb_rate = parseRate(ptb_rate_combo_->currentText());
                ptb_collector_->setSampleRate(ptb_rate);
                ptb_collector_->setDeviceSampleRate(ptb_rate);
                log(QString(is_english_ ? "[PTB] Sample rate set to %1 Hz" : "[PTB] 采样频率设置为 %1 Hz").arg(ptb_rate));
                if (ptb_collector_->startStreaming())
                {
                    connected_devices++;
                }
                else
                {
                    log(is_english_ ? "[PTB] Failed to start data stream." : "[PTB] 启动数据流失败。");
                    ptb_collector_->stop();
                }
            }
            else
            {
                log(is_english_ ? "[PTB] Device not responding! Check power and cables." : "[PTB] 设备无响应！请检查电源和连接线。");
                ptb_collector_->stop();
            }
        }
        else
        {
            log(QString(is_english_ ? "[PTB] Failed to open port: %1" : "[PTB] 打开端口失败: %1").arg(QString::fromStdString(ptb_collector_->getLastError())));
        }
    }
    else
    {
        log(is_english_ ? "[PTB] Skipped (not selected)" : "[PTB] 跳过 (未选择)");
    }
    QApplication::processEvents();

    QString hmp_port = hmp_port_combo_->currentText();
    if (hmp_port != selectText && !hmp_port.isEmpty())
    {
        total_devices++;
        log(QString(is_english_ ? "[HMP] Checking port: %1" : "[HMP] 检查端口: %1").arg(hmp_port));
        QApplication::processEvents();
        
        log(QString(is_english_ ? "[HMP] Port selected, connecting..." : "[HMP] 已选择端口，正在连接..."));
        QApplication::processEvents();
        
        VaproView::SerialConfig hmp_config = VaproView::SerialConfig::N82(hmp_baud_combo_->currentText().toInt());
        if (hmp_collector_->start(hmp_port.toStdString(), hmp_config))
        {
            log(QString(is_english_ ? "[HMP] Serial port opened, checking device response..." : "[HMP] 串口已打开，正在检测设备响应..."));
            QApplication::processEvents();
            
            if (hmp_collector_->checkDeviceResponse())
            {
                log(QString(is_english_ ? "[HMP] Device responding, connected: %1 @ %2 baud" : "[HMP] 设备响应正常，连接成功: %1 @ %2 波特率").arg(hmp_port, hmp_baud_combo_->currentText()));
                hmp_collector_->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onHmpDataReady", Qt::QueuedConnection); });
                int hmp_rate = parseRate(hmp_rate_combo_->currentText());
                hmp_collector_->setSampleRate(hmp_rate);
                log(QString(is_english_ ? "[HMP] Sample rate set to %1 Hz" : "[HMP] 采样频率设置为 %1 Hz").arg(hmp_rate));
                if (hmp_collector_->startStreaming())
                {
                    connected_devices++;
                }
                else
                {
                    log(is_english_ ? "[HMP] Failed to start data stream." : "[HMP] 启动数据流失败。");
                    hmp_collector_->stop();
                }
            }
            else
            {
                log(is_english_ ? "[HMP] Device not responding! Check power and cables." : "[HMP] 设备无响应！请检查电源和连接线。");
                hmp_collector_->stop();
            }
        }
        else
        {
            log(QString(is_english_ ? "[HMP] Failed to open port: %1" : "[HMP] 打开端口失败: %1").arg(QString::fromStdString(hmp_collector_->getLastError())));
        }
    }
    else
    {
        log(is_english_ ? "[HMP] Skipped (not selected)" : "[HMP] 跳过 (未选择)");
    }
    QApplication::processEvents();

    log(QString(is_english_ ? "========== Connection Summary: %1/%2 devices connected ==========" : "========== 连接摘要: %1/%2 设备已连接 ==========").arg(connected_devices).arg(total_devices));

    if (connected_devices > 0)
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

    if (gnss_collector_)
    {
        double rate = gnss_collector_->getActualRate();
        gnss_panel_->updateRate(rate);
        int rate_int = static_cast<int>(std::round(rate));
        if (rate_int >= 1 && rate_int <= 500)
        {
            gnss_rate_combo_->blockSignals(true);
            gnss_rate_combo_->setCurrentText(QString::number(rate_int));
            gnss_rate_combo_->blockSignals(false);
        }
    }
    if (imu_collector_)
    {
        double rate = imu_collector_->getActualRate();
        imu_panel_->updateRate(rate);
        int rate_int = static_cast<int>(std::round(rate));
        if (rate_int >= 1 && rate_int <= 500)
        {
            imu_rate_combo_->blockSignals(true);
            imu_rate_combo_->setCurrentText(QString::number(rate_int));
            imu_rate_combo_->blockSignals(false);
        }
    }
    if (ptb_collector_)
    {
        double rate = ptb_collector_->getActualRate();
        ptb_panel_->updateRate(rate);
        int rate_int = static_cast<int>(std::round(rate));
        if (rate_int >= 1 && rate_int <= 500)
        {
            ptb_rate_combo_->blockSignals(true);
            ptb_rate_combo_->setCurrentText(QString::number(rate_int));
            ptb_rate_combo_->blockSignals(false);
        }
    }
    if (hmp_collector_)
    {
        double rate = hmp_collector_->getActualRate();
        hmp_panel_->updateRate(rate);
        int rate_int = static_cast<int>(std::round(rate));
        if (rate_int >= 1 && rate_int <= 500)
        {
            hmp_rate_combo_->blockSignals(true);
            hmp_rate_combo_->setCurrentText(QString::number(rate_int));
            hmp_rate_combo_->blockSignals(false);
        }
    }
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
    out.setCodec("UTF-8");
    out.setGenerateByteOrderMark(true);

    if (filename.endsWith(".json", Qt::CaseInsensitive))
    {
        out << "{\n";
        out << "  \"gnss\": {\n";
        out << "    \"latitude\": " << current_gnss_.latitude << ",\n";
        out << "    \"longitude\": " << current_gnss_.longitude << ",\n";
        out << "    \"altitude\": " << current_gnss_.altitude << ",\n";
        out << "    \"vel_north\": " << current_gnss_.vel_north << ",\n";
        out << "    \"vel_east\": " << current_gnss_.vel_east << ",\n";
        out << "    \"vel_ground\": " << current_gnss_.vel_ground << ",\n";
        out << "    \"heading\": " << current_gnss_.heading << ",\n";
        out << "    \"pitch\": " << current_gnss_.heading_pitch << ",\n";
        out << "    \"sigma_lat\": " << current_gnss_.sigma_lat << ",\n";
        out << "    \"sigma_lon\": " << current_gnss_.sigma_lon << ",\n";
        out << "    \"sigma_alt\": " << current_gnss_.sigma_alt << ",\n";
        out << "    \"sats_used\": " << current_gnss_.num_satellites_used << ",\n";
        out << "    \"sats_tracked\": " << current_gnss_.num_satellites_tracked << ",\n";
        out << "    \"gdop\": " << current_gnss_.gdop << ",\n";
        out << "    \"pdop\": " << current_gnss_.pdop << ",\n";
        out << "    \"hdop\": " << current_gnss_.hdop << ",\n";
        out << "    \"status\": \"" << QString::fromStdString(current_gnss_.position_status).replace("\"", "\\\"") << "\",\n";
        out << "    \"valid\": " << (current_gnss_.valid ? "true" : "false") << "\n";
        out << "  },\n";
        out << "  \"imu\": {\n";
        out << "    \"acc_x\": " << current_imu_.acceleration[0] << ",\n";
        out << "    \"acc_y\": " << current_imu_.acceleration[1] << ",\n";
        out << "    \"acc_z\": " << current_imu_.acceleration[2] << ",\n";
        out << "    \"gyr_x\": " << current_imu_.gyroscope[0] << ",\n";
        out << "    \"gyr_y\": " << current_imu_.gyroscope[1] << ",\n";
        out << "    \"gyr_z\": " << current_imu_.gyroscope[2] << ",\n";
        out << "    \"roll\": " << current_imu_.rpy[0] << ",\n";
        out << "    \"pitch\": " << current_imu_.rpy[1] << ",\n";
        out << "    \"yaw\": " << current_imu_.rpy[2] << ",\n";
        out << "    \"quat_w\": " << current_imu_.quaternion[0] << ",\n";
        out << "    \"quat_x\": " << current_imu_.quaternion[1] << ",\n";
        out << "    \"quat_y\": " << current_imu_.quaternion[2] << ",\n";
        out << "    \"quat_z\": " << current_imu_.quaternion[3] << ",\n";
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
        out << "GNSS,VelNorth," << current_gnss_.vel_north << ",m/s," << (current_gnss_.valid ? "Yes" : "No") << "\n";
        out << "GNSS,VelEast," << current_gnss_.vel_east << ",m/s," << (current_gnss_.valid ? "Yes" : "No") << "\n";
        out << "GNSS,VelGround," << current_gnss_.vel_ground << ",m/s," << (current_gnss_.valid ? "Yes" : "No") << "\n";
        out << "GNSS,Heading," << current_gnss_.heading << ",deg," << (current_gnss_.valid ? "Yes" : "No") << "\n";
        out << "GNSS,Pitch," << current_gnss_.heading_pitch << ",deg," << (current_gnss_.valid ? "Yes" : "No") << "\n";
        out << "GNSS,SigmaLat," << current_gnss_.sigma_lat << ",m," << (current_gnss_.valid ? "Yes" : "No") << "\n";
        out << "GNSS,SigmaLon," << current_gnss_.sigma_lon << ",m," << (current_gnss_.valid ? "Yes" : "No") << "\n";
        out << "GNSS,SigmaAlt," << current_gnss_.sigma_alt << ",m," << (current_gnss_.valid ? "Yes" : "No") << "\n";
        out << "GNSS,SatsUsed," << current_gnss_.num_satellites_used << ",," << (current_gnss_.valid ? "Yes" : "No") << "\n";
        out << "GNSS,SatsTracked," << current_gnss_.num_satellites_tracked << ",," << (current_gnss_.valid ? "Yes" : "No") << "\n";
        out << "GNSS,GDOP," << current_gnss_.gdop << ",," << (current_gnss_.valid ? "Yes" : "No") << "\n";
        out << "GNSS,PDOP," << current_gnss_.pdop << ",," << (current_gnss_.valid ? "Yes" : "No") << "\n";
        out << "GNSS,HDOP," << current_gnss_.hdop << ",," << (current_gnss_.valid ? "Yes" : "No") << "\n";
        out << "GNSS,Status," << QString::fromStdString(current_gnss_.position_status) << ",," << (current_gnss_.valid ? "Yes" : "No") << "\n";
        out << "IMU,AccX," << current_imu_.acceleration[0] << ",m/s2," << (current_imu_.valid ? "Yes" : "No") << "\n";
        out << "IMU,AccY," << current_imu_.acceleration[1] << ",m/s2," << (current_imu_.valid ? "Yes" : "No") << "\n";
        out << "IMU,AccZ," << current_imu_.acceleration[2] << ",m/s2," << (current_imu_.valid ? "Yes" : "No") << "\n";
        out << "IMU,GyrX," << current_imu_.gyroscope[0] << ",deg/s," << (current_imu_.valid ? "Yes" : "No") << "\n";
        out << "IMU,GyrY," << current_imu_.gyroscope[1] << ",deg/s," << (current_imu_.valid ? "Yes" : "No") << "\n";
        out << "IMU,GyrZ," << current_imu_.gyroscope[2] << ",deg/s," << (current_imu_.valid ? "Yes" : "No") << "\n";
        out << "IMU,Roll," << current_imu_.rpy[0] << ",deg," << (current_imu_.valid ? "Yes" : "No") << "\n";
        out << "IMU,Pitch," << current_imu_.rpy[1] << ",deg," << (current_imu_.valid ? "Yes" : "No") << "\n";
        out << "IMU,Yaw," << current_imu_.rpy[2] << ",deg," << (current_imu_.valid ? "Yes" : "No") << "\n";
        out << "IMU,QuatW," << current_imu_.quaternion[0] << ",," << (current_imu_.valid ? "Yes" : "No") << "\n";
        out << "IMU,QuatX," << current_imu_.quaternion[1] << ",," << (current_imu_.valid ? "Yes" : "No") << "\n";
        out << "IMU,QuatY," << current_imu_.quaternion[2] << ",," << (current_imu_.valid ? "Yes" : "No") << "\n";
        out << "IMU,QuatZ," << current_imu_.quaternion[3] << ",," << (current_imu_.valid ? "Yes" : "No") << "\n";
        out << "PTB,Pressure," << current_ptb_.pressure_hpa << ",hPa," << (current_ptb_.valid ? "Yes" : "No") << "\n";
        out << "HMP,Humidity," << current_hmp_.humidity << ",%RH," << (current_hmp_.valid ? "Yes" : "No") << "\n";
        out << "HMP,Temperature," << current_hmp_.temperature << ",C," << (current_hmp_.valid ? "Yes" : "No") << "\n";
    }

    file.close();
    log(QString(is_english_ ? "Exported: %1" : "已导出: %1").arg(filename));
}

void MainWindow::onClearLogClicked()
{
    log_text_edit_->clear();
    has_inline_progress_log_ = false;
    log(is_english_ ? "Log cleared" : "日志已清空");
}

void MainWindow::onRtkConfigClicked()
{
    if (!rtk_config_dialog_)
    {
        rtk_config_dialog_ = new RtkConfigDialog(this);
    }
    rtk_config_dialog_->show();
    rtk_config_dialog_->raise();
    rtk_config_dialog_->activateWindow();
}
