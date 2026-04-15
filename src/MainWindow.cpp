#include "MainWindow.h"
#include "RtkConfigDialog.h"
#include "SessionViewerWindow.h"
#include "TcpWavePanel.h"
#include "data_collector.h"
#include "data_types.h"
#include "serial_probe_utils.h"
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QSaveFile>
#include <QTextStream>
#include <QStringConverter>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>
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
#include <QStringList>
#include <QApplication>
#include <QLayout>
#include <QIntValidator>
#include <QSerialPortInfo>
#include <QRegularExpression>
#include <QHash>
#include <QSet>
#include <QSignalBlocker>
#include <QSettings>
#include <QThread>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <cstring>
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
constexpr int kMainPageInputHeight = 30;
constexpr int kMainPageButtonHeight = 38;
constexpr quint64 kImuPpsSyncWindowUs = 2ULL * 1000ULL * 1000ULL;
constexpr char kImuRawMagic[8] = {'V', 'V', 'I', 'M', 'U', 'R', 'A', 'W'};

#pragma pack(push, 1)
struct ImuRawFileHeader
{
    char magic[8];
    quint32 version;
    quint32 header_size;
};

struct ImuRawRecordHeader
{
    quint32 marker;
    quint32 payload_size;
    quint64 host_timestamp_us;
    quint8 frame_tag;
    quint8 reserved[3];
};
#pragma pack(pop)

QString recordingTimestampUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString recordingSessionDirectoryTimestamp()
{
    return QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
}

QString waveformSegmentTimestamp(quint64 timestampUs)
{
    return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(timestampUs / 1000ULL), QTimeZone::UTC)
        .toLocalTime()
        .toString("yyyy-MM-dd_HH-mm-ss");
}

QString csvEscape(const QString &value)
{
    QString escaped = value;
    escaped.replace("\"", "\"\"");
    if (escaped.contains(',') || escaped.contains('"') || escaped.contains('\n') || escaped.contains('\r'))
    {
        escaped = QString("\"%1\"").arg(escaped);
    }
    return escaped;
}

QString csvBool(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

QString imuFrameTypeName(VaporView::ImuFrameType type)
{
    switch (type)
    {
    case VaporView::ImuFrameType::HI81:
        return QStringLiteral("HI81");
    case VaporView::ImuFrameType::HI83:
        return QStringLiteral("HI83");
    case VaporView::ImuFrameType::HI91:
        return QStringLiteral("HI91");
    case VaporView::ImuFrameType::HI92:
        return QStringLiteral("HI92");
    case VaporView::ImuFrameType::Unknown:
    default:
        return QStringLiteral("Unknown");
    }
}

QString imuRatePeriodText(int hz)
{
    switch (hz)
    {
    case 1: return QStringLiteral("1");
    case 2: return QStringLiteral("0.5");
    case 5: return QStringLiteral("0.2");
    case 10: return QStringLiteral("0.1");
    case 20: return QStringLiteral("0.05");
    case 50: return QStringLiteral("0.02");
    case 100: return QStringLiteral("0.01");
    case 200: return QStringLiteral("0.005");
    case 250: return QStringLiteral("0.004");
    case 500: return QStringLiteral("0.002");
    case 1000: return QStringLiteral("0.001");
    default: return QString();
    }
}

void applyComboText(QComboBox *combo, const QString& value)
{
    if (!combo || value.isEmpty())
    {
        return;
    }
    const QSignalBlocker blocker(combo);
    const int idx = combo->findText(value);
    if (idx >= 0)
    {
        combo->setCurrentIndex(idx);
        return;
    }
    if (combo->isEditable())
    {
        combo->setEditText(value);
    }
    else
    {
        combo->setCurrentText(value);
    }
}

bool shouldMirrorToErrorLog(const QString& message)
{
    static const QStringList keywords = {
        QStringLiteral("error"),
        QStringLiteral("failed"),
        QStringLiteral("timeout"),
        QStringLiteral("exception"),
        QStringLiteral("disconnect"),
        QStringLiteral("异常"),
        QStringLiteral("失败"),
        QStringLiteral("错误"),
        QStringLiteral("超时"),
        QStringLiteral("掉线"),
        QStringLiteral("断开"),
    };
    const QString lower = message.toLower();
    for (const QString& keyword : keywords)
    {
        if (lower.contains(keyword.toLower()))
        {
            return true;
        }
    }
    return false;
}
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
    , time_label_(nullptr)
    , lat_label_(nullptr)
    , lon_label_(nullptr)
    , alt_label_(nullptr)
    , vel_n_label_(nullptr)
    , vel_e_label_(nullptr)
    , vel_ground_label_(nullptr)
    , heading_label_(nullptr)
    , pitch_label_(nullptr)
    , heading_len_label_(nullptr)
    , heading_type_label_(nullptr)
    , heading_sats_label_(nullptr)
    , sats_label_(nullptr)
    , gdop_label_(nullptr)
    , pdop_label_(nullptr)
    , hdop_label_(nullptr)
    , htdop_label_(nullptr)
    , tdop_label_(nullptr)
    , diff_age_label_(nullptr)
    , undulation_label_(nullptr)
    , sigma_lat_label_(nullptr)
    , sigma_lon_label_(nullptr)
    , sigma_alt_label_(nullptr)
    , cutoff_label_(nullptr)
    , status_lbl_(nullptr)
    , time_lbl_(nullptr)
    , lat_lbl_(nullptr)
    , lon_lbl_(nullptr)
    , alt_lbl_(nullptr)
    , vel_n_lbl_(nullptr)
    , vel_e_lbl_(nullptr)
    , vel_ground_lbl_(nullptr)
    , heading_lbl_(nullptr)
    , pitch_lbl_(nullptr)
    , heading_type_lbl_(nullptr)
    , heading_len_lbl_(nullptr)
    , heading_sats_lbl_(nullptr)
    , sats_lbl_(nullptr)
    , diff_lbl_(nullptr)
    , gdop_lbl_(nullptr)
    , pdop_lbl_(nullptr)
    , hdop_lbl_(nullptr)
    , htdop_lbl_(nullptr)
    , tdop_lbl_(nullptr)
    , cutoff_lbl_(nullptr)
    , undulation_lbl_(nullptr)
    , sigma_lat_lbl_(nullptr)
    , sigma_lon_lbl_(nullptr)
    , sigma_alt_lbl_(nullptr)
    , is_english_(false)
{
    setupUi();
}

void GnssPanel::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(4);
    mainLayout->setContentsMargins(6, 2, 6, 6);

    rate_label_ = new QLabel(this);
    rate_label_->setObjectName("rateLabel");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    mainLayout->addWidget(rate_label_);

    auto *colsLayout = new QHBoxLayout();
    colsLayout->setSpacing(12);

    auto *leftLayout = new QGridLayout();
    leftLayout->setVerticalSpacing(4);
    leftLayout->setHorizontalSpacing(1);

    auto *midLayout = new QGridLayout();
    midLayout->setVerticalSpacing(4);
    midLayout->setHorizontalSpacing(1);

    auto *rightLayout = new QGridLayout();
    rightLayout->setVerticalSpacing(4);
    rightLayout->setHorizontalSpacing(1);

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
    createRow(leftLayout, 1, time_lbl_, time_label_, this);
    createRow(leftLayout, 2, lat_lbl_, lat_label_, this);
    createRow(leftLayout, 3, lon_lbl_, lon_label_, this);
    createRow(leftLayout, 4, alt_lbl_, alt_label_, this);
    createRow(leftLayout, 5, sigma_lat_lbl_, sigma_lat_label_, this);
    createRow(leftLayout, 6, sigma_lon_lbl_, sigma_lon_label_, this);
    createRow(leftLayout, 7, sigma_alt_lbl_, sigma_alt_label_, this);
    createRow(leftLayout, 8, undulation_lbl_, undulation_label_, this);

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

    if (time_label_)
    {
        time_label_->setWordWrap(true);
        time_label_->setMinimumHeight(40);
        time_label_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    }

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
        time_lbl_->setText("Time:");
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
        time_lbl_->setText("时间:");
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

void GnssPanel::updateData(const VaporView::GnssData& data, quint64 timestamp_us)
{
    if (data.valid)
    {
        status_label_->setText(QString::fromStdString(data.position_status));
        status_label_->setProperty("data-valid", true);
        status_label_->style()->unpolish(status_label_);
        status_label_->style()->polish(status_label_);

        const QString formattedText = timestamp_us > 0
            ? QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(timestamp_us / 1000ULL), QTimeZone::UTC)
                  .toString("yyyy-MM-dd HH:mm:ss.zzz 'UTC'")
            : QStringLiteral("---");
        const QString rawText = timestamp_us > 0 ? QString::number(timestamp_us) + "us" : QStringLiteral("---");
        time_label_->setText(QString("%1\n%2").arg(formattedText, rawText));

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
        time_label_->setText(QStringLiteral("---\n---"));
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
    , time_label_(nullptr)
    , pps_label_(nullptr)
    , source_lbl_(nullptr)
    , time_lbl_(nullptr)
    , pps_lbl_(nullptr)
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
    mainLayout->setSpacing(4);
    mainLayout->setContentsMargins(6, 2, 6, 6);

    rate_label_ = new QLabel(this);
    rate_label_->setObjectName("rateLabel");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rate_label_->setFixedHeight(24);
    mainLayout->addWidget(rate_label_);

    auto *colsLayout = new QHBoxLayout();
    colsLayout->setSpacing(12);

    auto *leftLayout = new QGridLayout();
    leftLayout->setVerticalSpacing(4);
    leftLayout->setHorizontalSpacing(1);

    auto *rightLayout = new QGridLayout();
    rightLayout->setVerticalSpacing(4);
    rightLayout->setHorizontalSpacing(1);

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
    createRow(leftLayout, 1, time_lbl_, time_label_, this);
    createRow(leftLayout, 2, pps_lbl_, pps_label_, this);
    createSeparator(leftLayout, 3, accel_sep_, this);
    createRow(leftLayout, 4, acc_x_lbl_, acc_x_label_, this);
    createRow(leftLayout, 5, acc_y_lbl_, acc_y_label_, this);
    createRow(leftLayout, 6, acc_z_lbl_, acc_z_label_, this);
    createSeparator(leftLayout, 7, gyro_sep_, this);
    createRow(leftLayout, 8, gyr_x_lbl_, gyr_x_label_, this);
    createRow(leftLayout, 9, gyr_y_lbl_, gyr_y_label_, this);
    createRow(leftLayout, 10, gyr_z_lbl_, gyr_z_label_, this);

    createSeparator(rightLayout, 0, attitude_sep_, this);
    createRow(rightLayout, 1, roll_lbl_, roll_label_, this);
    createRow(rightLayout, 2, pitch_lbl_, pitch_label_, this);
    createRow(rightLayout, 3, yaw_lbl_, yaw_label_, this);
    createSeparator(rightLayout, 4, quat_sep_, this);
    createRow(rightLayout, 5, quat_w_lbl_, quat_w_label_, this);
    createRow(rightLayout, 6, quat_x_lbl_, quat_x_label_, this);
    createRow(rightLayout, 7, quat_y_lbl_, quat_y_label_, this);
    createRow(rightLayout, 8, quat_z_lbl_, quat_z_label_, this);

    if (time_label_)
    {
        time_label_->setWordWrap(true);
        time_label_->setMinimumHeight(40);
        time_label_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    }

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
        time_lbl_->setText("Time:");
        pps_lbl_->setText("PPS:");
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
        time_lbl_->setText("时间:");
        pps_lbl_->setText("PPS有效:");
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

void ImuPanel::updateData(const VaporView::ImuData& data, quint64 gnss_timestamp_us)
{
    if (data.valid)
    {
        source_label_->setText(imuFrameTypeName(data.frame_type));
        source_label_->setProperty("data-valid", true);
        source_label_->style()->unpolish(source_label_);
        source_label_->style()->polish(source_label_);
        quint64 imuTimestampUs = 0;
        if (data.from_hi83 && data.system_time_us > 0)
        {
            imuTimestampUs = static_cast<quint64>(data.system_time_us);
        }
        else if (data.system_time_ms > 0)
        {
            imuTimestampUs = static_cast<quint64>(data.system_time_ms) * 1000ULL;
        }

        bool ppsValid = false;
        quint64 deltaUs = 0;
        if (imuTimestampUs > 0 && gnss_timestamp_us > 0)
        {
            deltaUs = (imuTimestampUs > gnss_timestamp_us) ? (imuTimestampUs - gnss_timestamp_us)
                                                           : (gnss_timestamp_us - imuTimestampUs);
            ppsValid = deltaUs <= kImuPpsSyncWindowUs;
        }

        const QString formattedText = (imuTimestampUs > 0 && ppsValid)
            ? QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(imuTimestampUs / 1000ULL), QTimeZone::UTC)
                  .toString("yyyy-MM-dd HH:mm:ss.zzz 'UTC'")
            : QStringLiteral("---");
        QString rawText = QStringLiteral("---");
        if (data.from_hi83 && data.system_time_us > 0)
        {
            rawText = QString::number(data.system_time_us) + "us";
        }
        else if (data.system_time_ms > 0)
        {
            rawText = QString::number(data.system_time_ms) + "ms";
        }
        time_label_->setText(QString("%1\n%2").arg(formattedText, rawText));

        if (gnss_timestamp_us == 0 || imuTimestampUs == 0)
        {
            pps_label_->setText(is_english_ ? "Unknown" : "未知");
        }
        else if (ppsValid)
        {
            pps_label_->setText(is_english_
                ? QString("Valid (Δ%1 ms)").arg(QString::number(deltaUs / 1000ULL))
                : QString("有效 (差值%1 ms)").arg(QString::number(deltaUs / 1000ULL)));
        }
        else
        {
            pps_label_->setText(is_english_
                ? QString("Invalid (Δ%1 ms)").arg(QString::number(deltaUs / 1000ULL))
                : QString("无效 (差值%1 ms)").arg(QString::number(deltaUs / 1000ULL)));
        }

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
        time_label_->setText(QStringLiteral("---\n---"));
        pps_label_->setText(is_english_ ? "Unknown" : "未知");
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
    layout->setContentsMargins(6, 2, 6, 6);

    rate_label_ = new QLabel(this);
    rate_label_->setObjectName("rateLabel");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rate_label_->setMinimumHeight(22);
    layout->addWidget(rate_label_);

    auto *pressLayout = new QHBoxLayout();
    pressLayout->setSpacing(1);
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

void PtbPanel::updateData(const VaporView::PtbData& data)
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
    layout->setSpacing(3);
    layout->setContentsMargins(6, 2, 6, 6);

    rate_label_ = new QLabel(this);
    rate_label_->setObjectName("rateLabel");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rate_label_->setMinimumHeight(22);
    layout->addWidget(rate_label_);

    auto *tempLayout = new QHBoxLayout();
    tempLayout->setSpacing(1);
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
    humidLayout->setSpacing(1);
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

void HmpPanel::updateData(const VaporView::HmpData& data)
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

LidarPanel::LidarPanel(QWidget *parent)
    : QWidget(parent)
    , rate_label_(nullptr)
    , distance_label_(nullptr)
    , strength_label_(nullptr)
    , status_label_(nullptr)
    , distance_lbl_(nullptr)
    , strength_lbl_(nullptr)
    , is_english_(false)
{
    setupUi();
}

void LidarPanel::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(3);
    layout->setContentsMargins(6, 2, 6, 6);

    rate_label_ = new QLabel("0.0 Hz", this);
    rate_label_->setObjectName("rateBadge");
    layout->addWidget(rate_label_, 0, Qt::AlignRight);

    auto *distanceLayout = new QHBoxLayout();
    distanceLayout->setSpacing(1);
    distance_lbl_ = new QLabel(this);
    distance_lbl_->setObjectName("fieldLabel");
    distance_lbl_->setMinimumHeight(22);
    distanceLayout->addWidget(distance_lbl_);
    distance_label_ = new QLabel("--- m", this);
    distance_label_->setObjectName("highlightedValue");
    distance_label_->setMinimumHeight(22);
    distanceLayout->addWidget(distance_label_);
    distanceLayout->addStretch();
    layout->addLayout(distanceLayout);

    auto *strengthLayout = new QHBoxLayout();
    strengthLayout->setSpacing(1);
    strength_lbl_ = new QLabel(this);
    strength_lbl_->setObjectName("fieldLabel");
    strength_lbl_->setMinimumHeight(22);
    strengthLayout->addWidget(strength_lbl_);
    strength_label_ = new QLabel("---", this);
    strength_label_->setObjectName("valueLabel");
    strength_label_->setMinimumHeight(22);
    strengthLayout->addWidget(strength_label_);
    strengthLayout->addStretch();
    layout->addLayout(strengthLayout);

    status_label_ = new QLabel(this);
    status_label_->setObjectName("statusIndicator");
    status_label_->setMinimumHeight(22);
    layout->addWidget(status_label_);

    layout->addStretch();
    setEnglish(false);
}

void LidarPanel::updateRate(double hz)
{
    if (rate_label_)
    {
        rate_label_->setText(QString::asprintf("%.1f Hz", hz));
    }
}

void LidarPanel::setEnglish(bool english)
{
    is_english_ = english;
    if (english)
    {
        distance_lbl_->setText("Distance:");
        strength_lbl_->setText("Strength:");
        status_label_->setText("Waiting...");
    }
    else
    {
        distance_lbl_->setText("距离:");
        strength_lbl_->setText("强度:");
        status_label_->setText("等待中...");
    }
}

void LidarPanel::updateData(const VaporView::LidarData& data)
{
    if (data.valid)
    {
        distance_label_->setText(QString::asprintf("%.2f m", data.distance_m));
        strength_label_->setText(QString::number(data.signal_strength));
        distance_label_->setProperty("data-valid", true);
        distance_label_->style()->unpolish(distance_label_);
        distance_label_->style()->polish(distance_label_);
        status_label_->setText(is_english_ ? "Valid" : "有效");
        status_label_->setProperty("status", "connected");
        status_label_->style()->unpolish(status_label_);
        status_label_->style()->polish(status_label_);
    }
    else
    {
        distance_label_->setText("--- m");
        strength_label_->setText("---");
        distance_label_->setProperty("data-valid", false);
        distance_label_->style()->unpolish(distance_label_);
        distance_label_->style()->polish(distance_label_);
        status_label_->setText(is_english_ ? "No echo" : "无回波");
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
    , lidar_panel_(nullptr)
    , log_text_edit_(nullptr)
    , status_label_(nullptr)
    , recording_status_label_(nullptr)
    , recording_rate_toolbar_label_(nullptr)
    , auto_detect_ports_btn_(nullptr)
    , gnss_port_combo_(nullptr)
    , imu_port_combo_(nullptr)
    , ptb_port_combo_(nullptr)
    , hmp_port_combo_(nullptr)
    , lidar_port_combo_(nullptr)
    , gnss_baud_combo_(nullptr)
    , imu_baud_combo_(nullptr)
    , ptb_baud_combo_(nullptr)
    , hmp_baud_combo_(nullptr)
    , lidar_baud_combo_(nullptr)
    , connect_btn_(nullptr)
    , cancel_connect_btn_(nullptr)
    , disconnect_btn_(nullptr)
    , start_recording_btn_(nullptr)
    , pause_recording_btn_(nullptr)
    , stop_recording_btn_(nullptr)
    , refresh_ports_btn_(nullptr)
    , fullscreen_menu_action_(nullptr)
    , fullscreen_toolbar_action_(nullptr)
    , lang_action_(nullptr)
    , clear_log_action_(nullptr)
    , session_viewer_action_(nullptr)
    , recording_directory_action_(nullptr)
    , exit_action_(nullptr)
    , about_action_(nullptr)
    , font_scale_group_(nullptr)
    , font_tiny_action_(nullptr)
    , font_extra_small_action_(nullptr)
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
    , lidar_group_(nullptr)
    , gnss_lbl_(nullptr)
    , imu_lbl_(nullptr)
    , ptb_lbl_(nullptr)
    , hmp_lbl_(nullptr)
    , lidar_lbl_(nullptr)
    , data_inline_title_lbl_(nullptr)
    , log_inline_title_lbl_(nullptr)
    , gnss_inline_title_lbl_(nullptr)
    , imu_inline_title_lbl_(nullptr)
    , env_inline_title_lbl_(nullptr)
    , config_inline_title_lbl_(nullptr)
    , global_rate_lbl_(nullptr)
    , waveform_split_lbl_(nullptr)
    , gnss_rate_lbl_(nullptr)
    , imu_rate_lbl_(nullptr)
    , ptb_rate_lbl_(nullptr)
    , hmp_rate_lbl_(nullptr)
    , lidar_rate_lbl_(nullptr)
    , global_rate_combo_(nullptr)
    , recording_rate_combo_(nullptr)
    , waveform_split_spin_(nullptr)
    , gnss_rate_combo_(nullptr)
    , imu_rate_combo_(nullptr)
    , ptb_rate_combo_(nullptr)
    , hmp_rate_combo_(nullptr)
    , lidar_rate_combo_(nullptr)
    , imu_format_combo_(nullptr)
    , imu_apply_btn_(nullptr)
    , imu_hi91_btn_(nullptr)
    , imu_hi92_btn_(nullptr)
    , imu_baud_115200_btn_(nullptr)
    , imu_baud_921600_btn_(nullptr)
    , imu_rate_100_btn_(nullptr)
    , imu_rate_200_btn_(nullptr)
    , imu_rate_500_btn_(nullptr)
    , imu_rate_1000_btn_(nullptr)
    , gnss_collector_(nullptr)
    , imu_collector_(nullptr)
    , ptb_collector_(nullptr)
    , hmp_collector_(nullptr)
    , lidar_collector_(nullptr)
    , refresh_timer_(nullptr)
    , is_fullscreen_(false)
    , is_english_(false)
    , has_inline_progress_log_(false)
    , connection_attempt_in_progress_(false)
    , port_detection_in_progress_(false)
    , is_connected_(false)
    , cancel_connection_requested_(false)
    , recording_thread_running_(false)
    , waveform_writer_running_(false)
    , recording_paused_(false)
    , font_scale_percent_(100)
    , base_font_point_size_(0.0)
    , base_window_size_(1440, 860)
    , base_minimum_window_size_(800, 600)
    , gnss_sample_rate_(1)
    , imu_sample_rate_(200)
    , ptb_sample_rate_(1)
    , hmp_sample_rate_(1)
    , lidar_sample_rate_(1)
    , recording_export_rate_hz_(20)
    , waveform_split_minutes_(1)
    , steady_clock_anchor_(std::chrono::steady_clock::now())
    , system_clock_anchor_(std::chrono::system_clock::now())
    , sensors_file_(nullptr)
    , imu_raw_file_(nullptr)
    , event_log_file_(nullptr)
    , error_log_file_(nullptr)
    , recording_directory_()
    , session_directory_()
    , session_name_()
    , session_start_time_utc_()
    , session_start_time_us_(0)
    , sensors_filename_()
    , imu_raw_filename_()
    , imu_raw_doc_filename_()
    , session_metadata_filename_()
    , event_log_filename_()
    , error_log_filename_()
    , device_config_filename_()
    , waveform_directory_()
    , recording_entry_count_(0)
    , waveform_frame_count_(0)
    , waveform_file_count_(0)
    , rtk_config_action_(nullptr)
    , rtk_config_dialog_(nullptr)
    , tcp_wave_panel_(nullptr)
    , session_viewer_window_(nullptr)
{
    const double currentPointSize = qApp->font().pointSizeF();
    base_font_point_size_ = currentPointSize > 0.0 ? currentPointSize : 10.0;

    QSettings settings("VaporView", "MainWindow");
    font_scale_percent_ = settings.value("font_scale_percent", 100).toInt();
    if (font_scale_percent_ < 70 || font_scale_percent_ > 150)
    {
        font_scale_percent_ = 100;
    }
    recording_directory_ = settings.value("recording_directory", defaultRecordingDirectory()).toString();
    if (recording_directory_.isEmpty())
    {
        recording_directory_ = defaultRecordingDirectory();
    }
    waveform_split_minutes_ = settings.value("waveform_split_minutes", 1).toInt();
    if (waveform_split_minutes_ < 1 || waveform_split_minutes_ > 5)
    {
        waveform_split_minutes_ = 1;
    }
    recording_export_rate_hz_ = settings.value("recording_export_rate_hz", 20).toInt();
    if (recording_export_rate_hz_ < 1 || recording_export_rate_hz_ > 200)
    {
        recording_export_rate_hz_ = 20;
    }

    loadModernStyleSheet();
    
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
    setupCentralWidget();
    loadRememberedInputState();
    bindRememberedInputState();

    resize(base_window_size_);
    setMinimumSize(base_minimum_window_size_);

    refresh_timer_ = new QTimer(this);
    connect(refresh_timer_, &QTimer::timeout, this, &MainWindow::onRefreshTimer);
    refresh_timer_->start(100);

    setEnglish(false);
    applyStyleConfiguration();

    updateRecordingStatusLabel();
    updateConnectionStatus(false);
}

MainWindow::~MainWindow()
{
    saveRememberedInputState();
    cancel_connection_requested_.store(true);
    if (port_detection_thread_.joinable())
    {
        port_detection_thread_.join();
    }
    if (connection_thread_.joinable())
    {
        connection_thread_.join();
    }
    stopRecording(false);
    stopAllCollectors();
}

void MainWindow::loadModernStyleSheet()
{
    QString stylePath = QCoreApplication::applicationDirPath() + "/../resources/modern_style.qss";
    QFile styleFile(stylePath);
    
    if (styleFile.open(QFile::ReadOnly | QFile::Text))
    {
        base_style_sheet_ = QString::fromUtf8(styleFile.readAll());
        styleFile.close();

        const QFileInfo styleInfo(stylePath);
        const QString resourceDir = styleInfo.absolutePath();
        const QString comboArrowPath = QDir(resourceDir).absoluteFilePath("combo_arrow_down.xpm").replace('\\', '/');
        const QString comboArrowUpPath = QDir(resourceDir).absoluteFilePath("combo_arrow_up.xpm").replace('\\', '/');
        base_style_sheet_.replace("url(combo_arrow_down.xpm)", QString("url(%1)").arg(comboArrowPath));
        base_style_sheet_.replace("url(combo_arrow_up.xpm)", QString("url(%1)").arg(comboArrowUpPath));
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
            "QGroupBox { background-color: #ffffff; border: 1px solid #e0e0e0; border-radius: 8px; margin-top: 0px; padding: 1px 1px 1px 1px; font-size: 15px; font-weight: bold; color: #333333; }"
            "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 12px; padding: 0px 8px; background-color: #ffffff; color: #1976d2; }"
            "QLabel { color: #333333; background-color: transparent; border: none; }"
            "QLabel#sectionTitleLabel { color: #1976d2; font-size: 16px; font-weight: bold; }"
            "QComboBox { background-color: #ffffff; border: 1px solid #e0e0e0; border-radius: 6px; padding: 4px 10px; min-height: 30px; color: #333333; font-size: 14px; }"
            "QComboBox:hover { border-color: #bdbdbd; }"
            "QComboBox:focus { border-color: #1976d2; border-width: 2px; }"
            "QComboBox QAbstractItemView { background-color: #ffffff; border: 1px solid #e0e0e0; border-radius: 6px; selection-background-color: #e3f2fd; selection-color: #1976d2; padding: 4px; outline: none; }"
            "QLineEdit { background-color: #ffffff; border: 1px solid #e0e0e0; border-radius: 6px; padding: 4px 10px; min-height: 30px; color: #333333; font-size: 14px; }"
            "QLineEdit:hover { border-color: #bdbdbd; }"
            "QLineEdit:focus { border-color: #1976d2; border-width: 2px; }"
            "QLineEdit:disabled { background-color: #f5f5f5; color: #bdbdbd; }"
            "QSpinBox { background-color: #ffffff; border: 1px solid #e0e0e0; border-radius: 6px; padding: 4px 28px 4px 10px; min-height: 30px; color: #333333; font-size: 14px; }"
            "QSpinBox:hover { border-color: #bdbdbd; }"
            "QSpinBox:focus { border-color: #1976d2; border-width: 2px; }"
            "QSpinBox:disabled { background-color: #f5f5f5; color: #bdbdbd; }"
            "QSpinBox::up-button, QSpinBox::down-button { width: 20px; border: none; background-color: transparent; subcontrol-origin: border; }"
            "QSpinBox::up-button { subcontrol-position: top right; border-top-right-radius: 6px; }"
            "QSpinBox::down-button { subcontrol-position: bottom right; border-bottom-right-radius: 6px; }"
            "QSpinBox::up-button:hover, QSpinBox::down-button:hover { background-color: #f5f5f5; }"
            "QSpinBox::up-arrow, QSpinBox::down-arrow { width: 0px; height: 0px; margin-right: 6px; border-left: 4px solid transparent; border-right: 4px solid transparent; }"
            "QSpinBox::up-arrow { border-bottom: 5px solid #757575; }"
            "QSpinBox::down-arrow { border-top: 5px solid #757575; }"
            "QTextEdit { background-color: #ffffff; color: #222222; border: 1px solid #e0e0e0; border-radius: 6px; padding: 10px; font-family: \"Consolas\", \"Monaco\", \"Courier New\", monospace; font-size: 13px; }"
            "QScrollBar:vertical { background-color: #f5f5f5; width: 12px; border-radius: 6px; }"
            "QScrollBar::handle:vertical { background-color: #bdbdbd; min-height: 30px; border-radius: 6px; margin: 2px; }"
            "QScrollBar::handle:vertical:hover { background-color: #9e9e9e; }"
            "QScrollBar:horizontal { background-color: #f5f5f5; height: 12px; border-radius: 6px; }"
            "QScrollBar::handle:horizontal { background-color: #bdbdbd; min-width: 30px; border-radius: 6px; margin: 2px; }"
            "QScrollBar::handle:horizontal:hover { background-color: #9e9e9e; }"
            "QSplitter::handle { background-color: transparent; }"
            "QSplitter::handle:horizontal { width: 0px; }"
            "QSplitter::handle:vertical { height: 0px; }"
            "QPushButton { background-color: #1976d2; color: #ffffff; border: none; border-radius: 6px; padding: 0px 18px; font-size: 15px; font-weight: 500; min-height: 38px; max-height: 38px; }"
            "QPushButton:hover { background-color: #1565c0; }"
            "QPushButton:pressed { background-color: #0d47a1; }"
            "QPushButton:disabled { background-color: #bdbdbd; color: #ffffff; }"
            "QPushButton#compactTcpButton { padding: 0px 14px; min-height: 38px; max-height: 38px; font-size: 14px; }"
            "QPushButton#compactTcpStartButton { padding: 0px 14px; min-height: 38px; max-height: 38px; font-size: 14px; }"
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
        qsizetype start;
        qsizetype length;
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
    if (percent < 70 || percent > 150 || font_scale_percent_ == percent)
    {
        return;
    }

    QSize targetSize = size();
    if (!isFullScreen() && !isMaximized())
    {
        targetSize = QSize(
            std::max(1, static_cast<int>(std::lround(base_window_size_.width() * percent / 100.0))),
            std::max(1, static_cast<int>(std::lround(base_window_size_.height() * percent / 100.0)))
        );
    }

    font_scale_percent_ = percent;
    applyStyleConfiguration();
    if (!isFullScreen() && !isMaximized())
    {
        targetSize = targetSize.expandedTo(minimumSize()).expandedTo(minimumSizeHint());
        if (targetSize != size())
        {
            resize(targetSize);
        }
    }
    if (rtk_config_dialog_)
    {
        rtk_config_dialog_->setFontScale(font_scale_percent_);
    }

    QSettings settings("VaporView", "MainWindow");
    settings.setValue("font_scale_percent", font_scale_percent_);
}

void MainWindow::loadRememberedInputState()
{
    QSettings settings("VaporView", "MainWindow");

    applyComboText(gnss_port_combo_, settings.value("serial/gnss_port", gnss_port_combo_->currentText()).toString());
    applyComboText(imu_port_combo_, settings.value("serial/imu_port", imu_port_combo_->currentText()).toString());
    applyComboText(ptb_port_combo_, settings.value("serial/ptb_port", ptb_port_combo_->currentText()).toString());
    applyComboText(hmp_port_combo_, settings.value("serial/hmp_port", hmp_port_combo_->currentText()).toString());
    applyComboText(lidar_port_combo_, settings.value("serial/lidar_port", lidar_port_combo_->currentText()).toString());

    applyComboText(gnss_baud_combo_, settings.value("serial/gnss_baud", gnss_baud_combo_->currentText()).toString());
    applyComboText(imu_baud_combo_, settings.value("serial/imu_baud", imu_baud_combo_->currentText()).toString());
    applyComboText(ptb_baud_combo_, settings.value("serial/ptb_baud", ptb_baud_combo_->currentText()).toString());
    applyComboText(hmp_baud_combo_, settings.value("serial/hmp_baud", hmp_baud_combo_->currentText()).toString());
    applyComboText(lidar_baud_combo_, settings.value("serial/lidar_baud", lidar_baud_combo_->currentText()).toString());

    applyComboText(global_rate_combo_, settings.value("rate/global", global_rate_combo_->currentText()).toString());
    applyComboText(gnss_rate_combo_, settings.value("rate/gnss", gnss_rate_combo_->currentText()).toString());
    applyComboText(imu_rate_combo_, settings.value("rate/imu", QStringLiteral("200")).toString());
    applyComboText(ptb_rate_combo_, settings.value("rate/ptb", ptb_rate_combo_->currentText()).toString());
    applyComboText(hmp_rate_combo_, settings.value("rate/hmp", hmp_rate_combo_->currentText()).toString());
    applyComboText(lidar_rate_combo_, settings.value("rate/lidar", lidar_rate_combo_->currentText()).toString());
    applyComboText(recording_rate_combo_, settings.value("recording_export_rate_hz", QString::number(recording_export_rate_hz_)).toString());
    applyComboText(imu_format_combo_, settings.value("serial/imu_format", QStringLiteral("HI91")).toString());

    recording_export_rate_hz_ = std::clamp(parseRate(recording_rate_combo_ ? recording_rate_combo_->currentText() : QStringLiteral("20")), 1, 200);

    if (waveform_split_spin_)
    {
        const QSignalBlocker blocker(waveform_split_spin_);
        waveform_split_spin_->setValue(settings.value("waveform_split_minutes", waveform_split_spin_->value()).toInt());
        waveform_split_minutes_ = waveform_split_spin_->value();
    }
}

void MainWindow::saveRememberedInputState() const
{
    QSettings settings("VaporView", "MainWindow");
    settings.setValue("serial/gnss_port", gnss_port_combo_->currentText());
    settings.setValue("serial/imu_port", imu_port_combo_->currentText());
    settings.setValue("serial/ptb_port", ptb_port_combo_->currentText());
    settings.setValue("serial/hmp_port", hmp_port_combo_->currentText());
    settings.setValue("serial/lidar_port", lidar_port_combo_->currentText());

    settings.setValue("serial/gnss_baud", gnss_baud_combo_->currentText());
    settings.setValue("serial/imu_baud", imu_baud_combo_->currentText());
    settings.setValue("serial/imu_format", imu_format_combo_ ? imu_format_combo_->currentText() : QStringLiteral("HI91"));
    settings.setValue("serial/ptb_baud", ptb_baud_combo_->currentText());
    settings.setValue("serial/hmp_baud", hmp_baud_combo_->currentText());
    settings.setValue("serial/lidar_baud", lidar_baud_combo_->currentText());

    settings.setValue("rate/global", global_rate_combo_->currentText());
    settings.setValue("rate/gnss", gnss_rate_combo_->currentText());
    settings.setValue("rate/imu", imu_rate_combo_->currentText());
    settings.setValue("rate/ptb", ptb_rate_combo_->currentText());
    settings.setValue("rate/hmp", hmp_rate_combo_->currentText());
    settings.setValue("rate/lidar", lidar_rate_combo_->currentText());
    settings.setValue("recording_export_rate_hz", recording_rate_combo_ ? recording_rate_combo_->currentText() : QString::number(recording_export_rate_hz_));

    if (waveform_split_spin_)
    {
        settings.setValue("waveform_split_minutes", waveform_split_spin_->value());
    }
}

void MainWindow::bindRememberedInputState()
{
    auto bindCombo = [this](QComboBox *combo) {
        if (!combo)
        {
            return;
        }
        connect(combo, &QComboBox::currentTextChanged, this, [this](const QString&) {
            saveRememberedInputState();
        });
    };

    bindCombo(gnss_port_combo_);
    bindCombo(imu_port_combo_);
    bindCombo(ptb_port_combo_);
    bindCombo(hmp_port_combo_);
    bindCombo(lidar_port_combo_);
    bindCombo(gnss_baud_combo_);
    bindCombo(imu_baud_combo_);
    bindCombo(imu_format_combo_);
    bindCombo(ptb_baud_combo_);
    bindCombo(hmp_baud_combo_);
    bindCombo(lidar_baud_combo_);
    bindCombo(global_rate_combo_);
    bindCombo(recording_rate_combo_);
    bindCombo(gnss_rate_combo_);
    bindCombo(imu_rate_combo_);
    bindCombo(ptb_rate_combo_);
    bindCombo(hmp_rate_combo_);
    bindCombo(lidar_rate_combo_);

    if (waveform_split_spin_)
    {
        connect(waveform_split_spin_, &QSpinBox::valueChanged, this, [this](int) {
            saveRememberedInputState();
        });
    }
}

void MainWindow::setImuFormatSelection(const QString& format)
{
    applyComboText(imu_format_combo_, format);
}

void MainWindow::setImuBaudSelection(int baud)
{
    applyComboText(imu_baud_combo_, QString::number(baud));
}

void MainWindow::setImuRateSelection(int rate)
{
    applyComboText(imu_rate_combo_, QString::number(rate));
    imu_sample_rate_ = parseRate(imu_rate_combo_->currentText());
}

bool MainWindow::restartImuCollector(const std::shared_ptr<VaporView::ImuCollector>& collector, const QString& port, int baud, int rate)
{
    if (!collector)
    {
        return false;
    }

    collector->setSampleRate(rate);
    if (!collector->start(port.toStdString(), VaporView::SerialConfig::N81(baud)))
    {
        log(QString(is_english_ ? "[IMU] Failed to reopen IMU port: %1" : "[IMU] 重新打开 IMU 串口失败: %1")
                .arg(QString::fromStdString(collector->getLastError())));
        return false;
    }
    collector->setOutputMessageType(imu_format_combo_ ? imu_format_combo_->currentText().toStdString() : std::string("HI91"));
    if (!collector->checkDeviceResponse())
    {
        log(is_english_ ? "[IMU] No response after reopening IMU port" : "[IMU] 重新打开 IMU 串口后未收到设备响应");
        collector->stop();
        return false;
    }
    if (!collector->startStreaming())
    {
        log(is_english_ ? "[IMU] Failed to restart IMU data stream" : "[IMU] 重新启动 IMU 数据流失败");
        collector->stop();
        return false;
    }
    log(QString(is_english_ ? "[IMU] Reconnected at %1 baud, %2 Hz, %3" : "[IMU] 已按 %1 波特率、%2 Hz、%3 重新连接")
            .arg(baud)
            .arg(rate)
            .arg(imu_format_combo_ ? imu_format_combo_->currentText() : QStringLiteral("HI91")));
    return true;
}

bool MainWindow::applyImuDeviceProfile(const QString& requestedFormat, int requestedBaud, int requestedRate)
{
    if (connection_attempt_in_progress_ || port_detection_in_progress_)
    {
        return false;
    }

    const QString selectText = is_english_ ? "-- Select --" : "-- 选择 --";
    const QString port = imu_port_combo_ ? imu_port_combo_->currentText().trimmed() : QString();
    if (port.isEmpty() || port == selectText)
    {
        log(is_english_ ? "Select an IMU serial port first" : "请先选择 IMU 串口");
        return false;
    }

    bool baudOk = false;
    const int currentBaud = (imu_baud_combo_ ? imu_baud_combo_->currentText() : QStringLiteral("921600")).toInt(&baudOk);
    const int effectiveCurrentBaud = baudOk && currentBaud > 0 ? currentBaud : 921600;
    const QString currentFormat = imu_format_combo_ ? imu_format_combo_->currentText().trimmed().toUpper() : QStringLiteral("HI91");
    const int currentRate = parseRate(imu_rate_combo_ ? imu_rate_combo_->currentText() : QStringLiteral("200"));

    const QString targetFormat = requestedFormat.isEmpty() ? currentFormat : requestedFormat.trimmed().toUpper();
    const int targetBaud = requestedBaud > 0 ? requestedBaud : effectiveCurrentBaud;
    const int targetRate = requestedRate > 0 ? requestedRate : currentRate;
    const QString targetPeriod = imuRatePeriodText(targetRate);

    if ((targetFormat != QStringLiteral("HI91") && targetFormat != QStringLiteral("HI92")) || targetPeriod.isEmpty())
    {
        log(is_english_ ? "Unsupported IMU format or rate" : "IMU 输出格式或频率不受支持");
        return false;
    }

    setImuFormatSelection(targetFormat);
    setImuBaudSelection(targetBaud);
    setImuRateSelection(targetRate);
    saveRememberedInputState();

    const CollectorSnapshot collectors = snapshotCollectors();
    const auto imuCollector = collectors.imu;
    const bool collectorRunning = imuCollector && imuCollector->isRunning();

    auto sendCommand = [this](auto&& sender, const QString& command, int waitMs = 80) -> bool {
        const std::string stdCommand = command.toStdString();
        if (!sender(stdCommand, waitMs))
        {
            return false;
        }
        log(QString("[IMU TX] %1").arg(command.trimmed()));
        return true;
    };

    bool configured = false;
    bool needRestart = false;

    if (collectorRunning)
    {
        imuCollector->setOutputMessageType(targetFormat.toStdString());
        if (!sendCommand([&](const std::string& cmd, int waitMs) { return imuCollector->sendAsciiCommand(cmd, waitMs); }, QStringLiteral("LOG HI91 ONTIME 0\r\n")))
        {
            return false;
        }
        if (!sendCommand([&](const std::string& cmd, int waitMs) { return imuCollector->sendAsciiCommand(cmd, waitMs); }, QStringLiteral("LOG HI92 ONTIME 0\r\n")))
        {
            return false;
        }
        if (!sendCommand([&](const std::string& cmd, int waitMs) { return imuCollector->sendAsciiCommand(cmd, waitMs); },
                         QStringLiteral("LOG %1 ONTIME %2\r\n").arg(targetFormat, targetPeriod)))
        {
            return false;
        }
        if (!sendCommand([&](const std::string& cmd, int waitMs) { return imuCollector->sendAsciiCommand(cmd, waitMs); },
                         QStringLiteral("SAVECONFIG\r\n"), 120))
        {
            return false;
        }
        configured = true;

        if (targetBaud != effectiveCurrentBaud)
        {
            if (!sendCommand([&](const std::string& cmd, int waitMs) { return imuCollector->sendAsciiCommand(cmd, waitMs); },
                             QStringLiteral("SERIALCONFIG %1\r\n").arg(targetBaud), 150))
            {
                return false;
            }
            needRestart = true;
        }
    }
    else
    {
        VaporView::SerialPort tempPort;
        if (!tempPort.open(port.toStdString(), VaporView::SerialConfig::N81(effectiveCurrentBaud)))
        {
            log(QString(is_english_
                ? "[IMU] Unable to open %1 for direct configuration, saved for next connection"
                : "[IMU] 无法打开 %1 直接配置，已保存到下次连接时应用").arg(port));
            return true;
        }

        auto directSend = [&](const std::string& cmd, int waitMs) -> bool {
            const bool ok = tempPort.write(cmd.c_str(), cmd.size()) == static_cast<ssize_t>(cmd.size());
            if (ok && waitMs > 0)
            {
                QThread::msleep(waitMs);
            }
            return ok;
        };

        if (!sendCommand(directSend, QStringLiteral("LOG HI91 ONTIME 0\r\n")))
        {
            return false;
        }
        if (!sendCommand(directSend, QStringLiteral("LOG HI92 ONTIME 0\r\n")))
        {
            return false;
        }
        if (!sendCommand(directSend, QStringLiteral("LOG %1 ONTIME %2\r\n").arg(targetFormat, targetPeriod)))
        {
            return false;
        }
        if (!sendCommand(directSend, QStringLiteral("SAVECONFIG\r\n"), 120))
        {
            return false;
        }
        configured = true;
        if (targetBaud != effectiveCurrentBaud)
        {
            if (!sendCommand(directSend, QStringLiteral("SERIALCONFIG %1\r\n").arg(targetBaud), 150))
            {
                return false;
            }
            tempPort.close();
            if (tempPort.open(port.toStdString(), VaporView::SerialConfig::N81(targetBaud)))
            {
                if (!sendCommand(directSend, QStringLiteral("SAVECONFIG\r\n"), 120))
                {
                    return false;
                }
            }
        }
        tempPort.close();
    }

    if (collectorRunning && needRestart)
    {
        imuCollector->stop();
        if (!restartImuCollector(imuCollector, port, targetBaud, targetRate))
        {
            return false;
        }
        if (!imuCollector->sendAsciiCommand("SAVECONFIG\r\n", 120))
        {
            log(is_english_ ? "[IMU] Failed to persist baud rate after reconnect" : "[IMU] 重连后保存波特率配置失败");
        }
    }
    else if (collectorRunning)
    {
        imuCollector->setSampleRate(targetRate);
    }

    if (configured)
    {
        log(QString(is_english_
            ? "IMU profile applied: %1, %2 baud, %3 Hz"
            : "IMU 配置已应用: %1, %2 波特率, %3 Hz")
            .arg(targetFormat)
            .arg(targetBaud)
            .arg(targetRate));
    }
    return configured;
}

void MainWindow::setupMenuBar()
{
    QMenu *fileMenu = menuBar()->addMenu("");

    recording_directory_action_ = new QAction(this);
    connect(recording_directory_action_, &QAction::triggered, this, &MainWindow::onChooseRecordingDirectoryClicked);
    fileMenu->addAction(recording_directory_action_);

    session_viewer_action_ = new QAction(this);
    connect(session_viewer_action_, &QAction::triggered, this, &MainWindow::onOpenSessionViewerClicked);
    fileMenu->addAction(session_viewer_action_);

    fileMenu->addSeparator();

    exit_action_ = new QAction(this);
    exit_action_->setShortcut(QKeySequence::Quit);
    connect(exit_action_, &QAction::triggered, this, &QMainWindow::close);
    fileMenu->addAction(exit_action_);

    QMenu *viewMenu = menuBar()->addMenu("");

    fullscreen_menu_action_ = new QAction(this);
    fullscreen_menu_action_->setShortcut(QKeySequence(Qt::Key_F11));
    connect(fullscreen_menu_action_, &QAction::triggered, this, &MainWindow::onToggleFullScreen);
    viewMenu->addAction(fullscreen_menu_action_);

    QMenu *fontMenu = menuBar()->addMenu("");
    font_scale_group_ = new QActionGroup(this);
    font_scale_group_->setExclusive(true);

    font_tiny_action_ = new QAction(this);
    font_tiny_action_->setCheckable(true);
    font_tiny_action_->setData(70);
    font_scale_group_->addAction(font_tiny_action_);
    fontMenu->addAction(font_tiny_action_);

    font_extra_small_action_ = new QAction(this);
    font_extra_small_action_->setCheckable(true);
    font_extra_small_action_->setData(80);
    font_scale_group_->addAction(font_extra_small_action_);
    fontMenu->addAction(font_extra_small_action_);

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

    if (font_scale_percent_ <= 75)
    {
        font_tiny_action_->setChecked(true);
    }
    else if (font_scale_percent_ <= 85)
    {
        font_extra_small_action_->setChecked(true);
    }
    else if (font_scale_percent_ <= 95)
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
            "- HiPNUC IMU (HI81/HI83/HI91/HI92)\n"
            "- PTB210 Barometer\n"
            "- HMP3 Temperature/Humidity Sensor\n"
            "- TF03 / TFA1500-L Laser Rangefinder\n\n"
            "Press F11 for fullscreen mode." :
            "VaporView 应用程序\n\n"
            "版本 1.0.0\n\n"
            "导航系统，支持 RTK 和 IMU。\n\n"
            "支持的设备:\n"
            "- UM982 RTK 接收机 (PVTSLN)\n"
            "- HiPNUC IMU (HI81/HI83/HI91/HI92)\n"
            "- PTB210 气压计\n"
            "- HMP3 温湿度传感器\n"
            "- TF03 / TFA1500-L 激光测距模块\n\n"
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

    cancel_connect_btn_ = new QAction(this);
    cancel_connect_btn_->setIcon(style()->standardIcon(QStyle::SP_BrowserStop));
    cancel_connect_btn_->setEnabled(false);
    connect(cancel_connect_btn_, &QAction::triggered, this, &MainWindow::onCancelConnectClicked);
    toolbar->addAction(cancel_connect_btn_);

    disconnect_btn_ = new QAction(this);
    disconnect_btn_->setIcon(style()->standardIcon(QStyle::SP_DialogNoButton));
    disconnect_btn_->setEnabled(false);
    connect(disconnect_btn_, &QAction::triggered, this, &MainWindow::onDisconnectClicked);
    toolbar->addAction(disconnect_btn_);

    toolbar->addSeparator();

    start_recording_btn_ = new QAction(this);
    start_recording_btn_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    start_recording_btn_->setEnabled(false);
    connect(start_recording_btn_, &QAction::triggered, this, &MainWindow::onStartRecordingClicked);
    toolbar->addAction(start_recording_btn_);

    pause_recording_btn_ = new QAction(this);
    pause_recording_btn_->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    pause_recording_btn_->setEnabled(false);
    connect(pause_recording_btn_, &QAction::triggered, this, &MainWindow::onPauseRecordingClicked);
    toolbar->addAction(pause_recording_btn_);

    stop_recording_btn_ = new QAction(this);
    stop_recording_btn_->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    stop_recording_btn_->setEnabled(false);
    connect(stop_recording_btn_, &QAction::triggered, this, &MainWindow::onStopRecordingClicked);
    toolbar->addAction(stop_recording_btn_);

    recording_rate_toolbar_label_ = new QLabel(toolbar);
    recording_rate_toolbar_label_->setContentsMargins(8, 0, 4, 0);
    toolbar->addWidget(recording_rate_toolbar_label_);

    recording_rate_combo_ = new QComboBox(toolbar);
    recording_rate_combo_->setEditable(true);
    recording_rate_combo_->addItems({QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("5"), QStringLiteral("10"),
                                     QStringLiteral("20"), QStringLiteral("50"), QStringLiteral("100"), QStringLiteral("200")});
    recording_rate_combo_->setValidator(new QIntValidator(1, 200, recording_rate_combo_));
    recording_rate_combo_->setFixedHeight(kMainPageInputHeight);
    recording_rate_combo_->setFixedWidth(80);
    recording_rate_combo_->setCurrentText(QString::number(recording_export_rate_hz_));
    connect(recording_rate_combo_, &QComboBox::currentTextChanged, this, [this](const QString& text) {
        const int rate = std::clamp(parseRate(text), 1, 200);
        recording_export_rate_hz_ = rate;
        if (recording_rate_combo_ && recording_rate_combo_->currentText() != QString::number(rate))
        {
            const QSignalBlocker blocker(recording_rate_combo_);
            recording_rate_combo_->setCurrentText(QString::number(rate));
        }
        saveRememberedInputState();
        log(QString(is_english_ ? "Recording rate set to %1 Hz" : "记录频率已设置为 %1 Hz").arg(recording_export_rate_hz_));
    });
    toolbar->addWidget(recording_rate_combo_);

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

    session_viewer_action_->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    toolbar->addAction(session_viewer_action_);

    toolbar->addSeparator();

    fullscreen_toolbar_action_ = new QAction(this);
    fullscreen_toolbar_action_->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    connect(fullscreen_toolbar_action_, &QAction::triggered, this, &MainWindow::onToggleFullScreen);
    toolbar->addAction(fullscreen_toolbar_action_);
}

void MainWindow::setupStatusBar()
{
    status_label_ = new QLabel(this);
    statusBar()->addWidget(status_label_);

    recording_status_label_ = new QLabel(this);
    statusBar()->addPermanentWidget(recording_status_label_);
}

void MainWindow::setupCentralWidget()
{
    central_widget_ = new QWidget(this);
    setCentralWidget(central_widget_);

    auto *main_h_layout = new QHBoxLayout(central_widget_);
    main_h_layout->setSpacing(0);
    main_h_layout->setContentsMargins(2, 2, 2, 2);

    auto *left_widget = new QWidget(this);
    main_layout_ = new QVBoxLayout(left_widget);
    main_layout_->setSpacing(0);
    main_layout_->setContentsMargins(0, 0, 0, 0);

    setupConfigPanel();
    setupDataPanels();

    auto *left_scroll_area = new QScrollArea(this);
    left_scroll_area->setWidgetResizable(true);
    left_scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    left_scroll_area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    left_scroll_area->setFrameShape(QFrame::NoFrame);
    left_scroll_area->setWidget(left_widget);

    setupLogPanel();

    auto *main_splitter = new QSplitter(Qt::Horizontal, central_widget_);
    main_splitter->setChildrenCollapsible(false);
    main_splitter->setHandleWidth(0);
    main_splitter->addWidget(left_scroll_area);
    main_splitter->addWidget(log_group_);
    main_splitter->setStretchFactor(0, 6);
    main_splitter->setStretchFactor(1, 1);
    main_splitter->setSizes({1120, 260});
    main_h_layout->addWidget(main_splitter);
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
    config_group_->setMinimumWidth(860);

    auto *config_root_layout = new QVBoxLayout(config_group_);
    config_root_layout->setSpacing(8);
    config_root_layout->setContentsMargins(8, 4, 8, 8);

    auto *config_layout = new QGridLayout();
    config_layout->setVerticalSpacing(8);
    config_layout->setHorizontalSpacing(8);
    config_layout->setColumnStretch(0, 0);
    config_layout->setColumnStretch(1, 0);
    config_layout->setColumnStretch(2, 0);
    config_layout->setColumnStretch(3, 0);
    config_layout->setColumnStretch(4, 0);
    config_layout->setColumnStretch(5, 1);
    config_layout->setColumnMinimumWidth(0, 110);
    config_layout->setColumnMinimumWidth(1, 170);
    config_layout->setColumnMinimumWidth(2, 100);
    config_layout->setColumnMinimumWidth(3, 80);
    config_layout->setColumnMinimumWidth(4, 100);

    QStringList baudRates = {"9600", "19200", "38400", "57600", "115200", "230400", "460800", "500000", "921600"};
    QStringList ports = getAvailablePorts();

    auto createRateCombo = [this](int maxRate = 500) {
        auto *combo = new QComboBox(this);
        const QList<int> supportedRates = {1, 2, 5, 10, 20, 50, 100, 200, 250, 500, 1000};
        for (int rate : supportedRates)
        {
            if (rate <= maxRate)
            {
                combo->addItem(QString::number(rate));
            }
        }
        const int preferredIndex = combo->findText(maxRate >= 200 ? QStringLiteral("200") : QStringLiteral("20"));
        combo->setCurrentIndex(preferredIndex >= 0 ? preferredIndex : 0);
        combo->setEditable(true);
        combo->setFixedHeight(kMainPageInputHeight);
        combo->setFixedWidth(100);
        combo->setValidator(new QIntValidator(1, maxRate, combo));
        return combo;
    };

    auto createPortRow = [this, config_layout, &baudRates, &ports, &createRateCombo](QLabel*& lbl, QComboBox*& portCombo, QComboBox*& baudCombo, QLabel*& rateLbl, QComboBox*& rateCombo, const QString& defaultPort, const QString& defaultBaud, int row, int maxRate = 500) {
        lbl = new QLabel(this);
        lbl->setObjectName("fieldLabel");
        lbl->setFixedHeight(kMainPageInputHeight);
        lbl->setFixedWidth(80);
        config_layout->addWidget(lbl, row, 0, Qt::AlignVCenter | Qt::AlignLeft);

        portCombo = new QComboBox(this);
        portCombo->addItem(is_english_ ? "-- Select --" : "-- 选择 --");
        portCombo->addItems(ports);
        portCombo->setEditable(true);
        portCombo->setMinimumContentsLength(10);
        portCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        portCombo->setFixedHeight(kMainPageInputHeight);
        portCombo->setMinimumWidth(160);
        portCombo->setMaximumWidth(190);
        portCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
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
        baudCombo->setFixedHeight(kMainPageInputHeight);
        baudCombo->setFixedWidth(100);
        config_layout->addWidget(baudCombo, row, 2, Qt::AlignVCenter);

        rateLbl = new QLabel(this);
        rateLbl->setObjectName("fieldLabel");
        rateLbl->setFixedHeight(kMainPageInputHeight);
        config_layout->addWidget(rateLbl, row, 3, Qt::AlignVCenter | Qt::AlignRight);

        rateCombo = createRateCombo(maxRate);
        config_layout->addWidget(rateCombo, row, 4, Qt::AlignVCenter);
    };

    config_inline_title_lbl_ = new QLabel(this);
    config_inline_title_lbl_->setObjectName("sectionTitleLabel");
    config_inline_title_lbl_->setFixedHeight(kMainPageInputHeight);
    config_layout->addWidget(config_inline_title_lbl_, 0, 0, Qt::AlignVCenter | Qt::AlignLeft);

    auto_detect_ports_btn_ = new QPushButton(this);
    auto_detect_ports_btn_->setFixedHeight(kMainPageButtonHeight);
    auto_detect_ports_btn_->setMinimumWidth(120);
    connect(auto_detect_ports_btn_, &QPushButton::clicked, this, &MainWindow::onAutoDetectPortsClicked);
    config_layout->addWidget(auto_detect_ports_btn_, 0, 1, 1, 2, Qt::AlignVCenter | Qt::AlignLeft);

    global_rate_lbl_ = new QLabel(this);
    global_rate_lbl_->setObjectName("fieldLabel");
    global_rate_lbl_->setFixedHeight(kMainPageInputHeight);
    config_layout->addWidget(global_rate_lbl_, 0, 3, Qt::AlignVCenter | Qt::AlignRight);

    global_rate_combo_ = createRateCombo();
    config_layout->addWidget(global_rate_combo_, 0, 4, Qt::AlignVCenter);
    connect(global_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onGlobalRateChanged);

    int row = 1;

#ifdef _WIN32
    createPortRow(gnss_lbl_, gnss_port_combo_, gnss_baud_combo_, gnss_rate_lbl_, gnss_rate_combo_, "COM3", "115200", row++);
    createPortRow(imu_lbl_, imu_port_combo_, imu_baud_combo_, imu_rate_lbl_, imu_rate_combo_, "COM4", "921600", row++, 1000);
    createPortRow(ptb_lbl_, ptb_port_combo_, ptb_baud_combo_, ptb_rate_lbl_, ptb_rate_combo_, "COM5", "9600", row++);
    createPortRow(hmp_lbl_, hmp_port_combo_, hmp_baud_combo_, hmp_rate_lbl_, hmp_rate_combo_, "COM6", "19200", row++);
    createPortRow(lidar_lbl_, lidar_port_combo_, lidar_baud_combo_, lidar_rate_lbl_, lidar_rate_combo_, "COM7", "500000", row++, 100);
#else
    createPortRow(gnss_lbl_, gnss_port_combo_, gnss_baud_combo_, gnss_rate_lbl_, gnss_rate_combo_, "/dev/ttyCOM3", "115200", row++);
    createPortRow(imu_lbl_, imu_port_combo_, imu_baud_combo_, imu_rate_lbl_, imu_rate_combo_, "/dev/ttyIMU", "921600", row++, 1000);
    createPortRow(ptb_lbl_, ptb_port_combo_, ptb_baud_combo_, ptb_rate_lbl_, ptb_rate_combo_, "/dev/ttyBARO", "9600", row++);
    createPortRow(hmp_lbl_, hmp_port_combo_, hmp_baud_combo_, hmp_rate_lbl_, hmp_rate_combo_, "/dev/ttyHMP", "19200", row++);
    createPortRow(lidar_lbl_, lidar_port_combo_, lidar_baud_combo_, lidar_rate_lbl_, lidar_rate_combo_, "/dev/ttyLidar", "500000", row++, 100);
#endif

    if (lidar_rate_combo_)
    {
        lidar_rate_combo_->addItem(is_english_ ? "No Set" : "不设定");
        lidar_rate_combo_->setValidator(nullptr);
    }

    imu_rate_combo_->setCurrentText(QStringLiteral("200"));

    connect(gnss_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onGnssRateChanged);
    connect(imu_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onImuRateChanged);
    connect(ptb_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onPtbRateChanged);
    connect(hmp_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onHmpRateChanged);
    connect(lidar_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onLidarRateChanged);

    config_root_layout->addLayout(config_layout);
    main_layout_->addWidget(config_group_);
}

void MainWindow::setupDataPanels()
{
    data_group_ = new QGroupBox(this);
    auto *data_layout = new QVBoxLayout(data_group_);
    data_layout->setSpacing(0);
    data_layout->setContentsMargins(0, 0, 0, 0);

    data_inline_title_lbl_ = new QLabel(this);
    data_inline_title_lbl_->setObjectName("sectionTitleLabel");
    data_inline_title_lbl_->setContentsMargins(6, 2, 6, 0);
    data_layout->addWidget(data_inline_title_lbl_, 0, Qt::AlignLeft);

    auto *sensor_splitter = new QSplitter(Qt::Horizontal, data_group_);
    sensor_splitter->setChildrenCollapsible(false);
    sensor_splitter->setHandleWidth(0);

    gnss_group_ = new QGroupBox(this);
    gnss_group_->setObjectName("sensorGroupBox");
    auto *gnss_layout = new QVBoxLayout(gnss_group_);
    gnss_layout->setContentsMargins(1, 0, 1, 1);
    gnss_layout->setSpacing(0);
    gnss_inline_title_lbl_ = new QLabel(this);
    gnss_inline_title_lbl_->setObjectName("sectionTitleLabel");
    gnss_layout->addWidget(gnss_inline_title_lbl_, 0, Qt::AlignLeft);
    gnss_panel_ = new GnssPanel(this);
    gnss_layout->addWidget(gnss_panel_);
    sensor_splitter->addWidget(gnss_group_);

    imu_group_ = new QGroupBox(this);
    imu_group_->setObjectName("sensorGroupBox");
    auto *imu_layout = new QVBoxLayout(imu_group_);
    imu_layout->setContentsMargins(1, 0, 1, 1);
    imu_layout->setSpacing(0);
    imu_inline_title_lbl_ = new QLabel(this);
    imu_inline_title_lbl_->setObjectName("sectionTitleLabel");
    imu_layout->addWidget(imu_inline_title_lbl_, 0, Qt::AlignLeft);
    imu_panel_ = new ImuPanel(this);
    imu_layout->addWidget(imu_panel_);
    sensor_splitter->addWidget(imu_group_);

    auto *env_group = new QGroupBox(this);
    env_group->setObjectName("sensorGroupBox");
    auto *env_layout = new QVBoxLayout(env_group);
    env_layout->setContentsMargins(1, 0, 1, 1);
    env_layout->setSpacing(0);
    env_inline_title_lbl_ = new QLabel(this);
    env_inline_title_lbl_->setObjectName("sectionTitleLabel");
    env_layout->addWidget(env_inline_title_lbl_, 0, Qt::AlignLeft);

    lidar_panel_ = new LidarPanel(this);
    env_layout->addWidget(lidar_panel_);

    ptb_panel_ = new PtbPanel(this);
    env_layout->addWidget(ptb_panel_);

    hmp_panel_ = new HmpPanel(this);
    env_layout->addWidget(hmp_panel_);

    sensor_splitter->addWidget(env_group);
    sensor_splitter->setStretchFactor(0, 4);
    sensor_splitter->setStretchFactor(1, 4);
    sensor_splitter->setStretchFactor(2, 4);
    sensor_splitter->setSizes({420, 420, 420});

    data_layout->addWidget(sensor_splitter, 1);
    env_group_ = env_group;

    lidar_group_ = nullptr;
    ptb_group_ = nullptr;
    hmp_group_ = nullptr;

    main_layout_->addWidget(data_group_, 1);

    tcp_wave_group_ = new QGroupBox(this);
    tcp_wave_group_->setObjectName("sensorGroupBox");
    tcp_wave_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tcp_wave_group_->setMinimumHeight(430);
    auto *tcpWaveLayout = new QVBoxLayout(tcp_wave_group_);
    tcpWaveLayout->setContentsMargins(0, 0, 0, 0);
    tcpWaveLayout->setSpacing(0);

    waveform_split_lbl_ = new QLabel(this);
    waveform_split_lbl_->setObjectName("fieldLabel");
    waveform_split_lbl_->setFixedHeight(kMainPageInputHeight);

    waveform_split_spin_ = new QSpinBox(this);
    waveform_split_spin_->setRange(1, 5);
    waveform_split_spin_->setValue(waveform_split_minutes_);
    waveform_split_spin_->setSuffix(is_english_ ? " min" : " 分钟");
    waveform_split_spin_->setFixedHeight(kMainPageInputHeight);
    waveform_split_spin_->setFixedWidth(100);
    connect(waveform_split_spin_, &QSpinBox::valueChanged, this, [this](int value) {
        waveform_split_minutes_ = value;
        QSettings settings("VaporView", "MainWindow");
        settings.setValue("waveform_split_minutes", waveform_split_minutes_);
        if (waveform_split_spin_)
        {
            waveform_split_spin_->setSuffix(is_english_ ? " min" : " 分钟");
        }
        log(QString(is_english_
            ? "Waveform split duration set to %1 minute(s)"
            : "波形分文件时长已设置为 %1 分钟").arg(value));
    });

    tcp_wave_panel_ = new TcpWavePanel(this);
    tcp_wave_panel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tcp_wave_panel_->attachWaveformSplitControls(waveform_split_lbl_, waveform_split_spin_);
    connect(tcp_wave_panel_, &TcpWavePanel::normalizedSecondHarmonicFrameReady,
            this, &MainWindow::onNormalizedSecondHarmonicFrameReady);
    connect(tcp_wave_panel_, &TcpWavePanel::connectionStateChanged, this, [this](bool) {
        updateRecordingActionStates();
    });
    tcpWaveLayout->addWidget(tcp_wave_panel_);
    main_layout_->addWidget(tcp_wave_group_, 0);
}

void MainWindow::setupLogPanel()
{
    log_group_ = new QGroupBox(this);
    log_group_->setMinimumWidth(220);
    log_group_->setMaximumWidth(340);
    auto *log_layout = new QVBoxLayout(log_group_);
    log_layout->setContentsMargins(0, 0, 0, 0);
    log_layout->setSpacing(0);

    log_inline_title_lbl_ = new QLabel(this);
    log_inline_title_lbl_->setObjectName("sectionTitleLabel");
    log_layout->addWidget(log_inline_title_lbl_, 0, Qt::AlignLeft);

    log_text_edit_ = new QTextEdit(this);
    log_text_edit_->setReadOnly(true);
    log_text_edit_->setMinimumWidth(200);
    log_layout->addWidget(log_text_edit_);
}

void MainWindow::setEnglish(bool english)
{
    is_english_ = english;

    menuBar()->actions().at(0)->menu()->setTitle(english ? "&File" : "文件(&F)");
    recording_directory_action_->setText(english ? "Recording Folder..." : "记录目录...");
    session_viewer_action_->setText(english ? "Data Viewer..." : "数据查看器...");
    exit_action_->setText(english ? "E&xit" : "退出(&X)");

    menuBar()->actions().at(1)->menu()->setTitle(english ? "&View" : "视图(&V)");
    fullscreen_menu_action_->setText(english ? "&Fullscreen" : "全屏(&F)");

    menuBar()->actions().at(2)->menu()->setTitle(english ? "Font &Size" : "字号(&S)");
    font_tiny_action_->setText(english ? "Tiny (70%)" : "超小 (70%)");
    font_extra_small_action_->setText(english ? "Extra Small (80%)" : "特小 (80%)");
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
    cancel_connect_btn_->setText(english ? "Cancel" : "取消");
    disconnect_btn_->setText(english ? "Disconnect" : "断开");
    start_recording_btn_->setText(english ? "Start Recording" : "开始记录");
    pause_recording_btn_->setText(english ? "Pause Recording" : "暂停记录");
    stop_recording_btn_->setText(english ? "Stop Recording" : "结束记录");
    if (recording_rate_toolbar_label_)
    {
        recording_rate_toolbar_label_->setText(english ? "Record Hz:" : "记录频率:");
    }
    if (recording_rate_combo_)
    {
        const QString tooltip = english
            ? "Controls how often devices.csv writes the latest sensor snapshot while recording."
            : "控制记录时 devices.csv 写入最新传感器快照的频率。";
        recording_rate_combo_->setToolTip(tooltip);
        if (recording_rate_toolbar_label_)
        {
            recording_rate_toolbar_label_->setToolTip(tooltip);
        }
    }
    clear_log_action_->setText(english ? "Clear" : "清空");
    fullscreen_toolbar_action_->setText(english ? "Fullscreen" : "全屏");
    rtk_config_action_->setText(english ? "RTK Config" : "RTK配置");

    status_label_->setText(english ? "Ready" : "就绪");

    config_group_->setTitle(QString());
    data_group_->setTitle(QString());
    tcp_wave_group_->setTitle(QString());
    log_group_->setTitle(QString());

    gnss_group_->setTitle(QString());
    imu_group_->setTitle(QString());
    env_group_->setTitle(QString());

    gnss_lbl_->setText(english ? "GNSS:" : "GNSS:");
    imu_lbl_->setText(english ? "IMU:" : "IMU:");
    ptb_lbl_->setText(english ? "PTB210:" : "PTB210:");
    hmp_lbl_->setText(english ? "HMP3:" : "HMP3:");
    lidar_lbl_->setText(english ? "TF03 / TFA1500-L:" : "TF03 / TFA1500-L:");

    if (config_inline_title_lbl_)
    {
        config_inline_title_lbl_->setText(english ? "Serial Port Configuration" : "串口配置");
    }
    if (auto_detect_ports_btn_)
    {
        auto_detect_ports_btn_->setText(english ? "Auto Detect Ports" : "自动识别串口");
        auto_detect_ports_btn_->setToolTip(english
            ? "Probe available serial ports and automatically assign detected devices."
            : "扫描可用串口，并将识别出的设备自动填入对应端口。");
    }
    if (data_inline_title_lbl_)
    {
        data_inline_title_lbl_->setText(english ? "Sensor Data" : "传感器数据");
    }
    if (log_inline_title_lbl_)
    {
        log_inline_title_lbl_->setText(english ? "Log" : "日志");
    }
    if (gnss_inline_title_lbl_)
    {
        gnss_inline_title_lbl_->setText(english ? "GNSS / RTK" : "GNSS / RTK");
    }
    if (imu_inline_title_lbl_)
    {
        imu_inline_title_lbl_->setText(english ? "IMU" : "IMU");
    }
    if (env_inline_title_lbl_)
    {
        env_inline_title_lbl_->setText(english ? "Environment / Range" : "环境与测距");
    }
    global_rate_lbl_->setText(english ? "Global Rate:" : "统一频率:");
    waveform_split_lbl_->setText(english ? "Wave Split:" : "波形分段:");
    if (waveform_split_spin_)
    {
        waveform_split_spin_->setSuffix(english ? " min" : " 分钟");
        const QString tooltip = english
            ? "Controls how long each waveform .dat file keeps recording before a new segment file is created. "
              "For example, 1 minute means the waveform writer rolls to a new file every minute."
            : "用于控制每个波形 .dat 文件连续记录多久后切换到新的分段文件。"
              "例如设置为 1 分钟时，波形写盘线程会每分钟滚动生成一个新文件。";
        waveform_split_lbl_->setToolTip(tooltip);
        waveform_split_spin_->setToolTip(tooltip);
    }
    gnss_rate_lbl_->setText(english ? "Rate:" : "频率:");
    imu_rate_lbl_->setText(english ? "Rate:" : "频率:");
    if (imu_apply_btn_)
    {
        imu_apply_btn_->setText(english ? "Apply IMU" : "应用IMU");
        imu_apply_btn_->setToolTip(english ? "Apply the selected IMU format, baud rate, and output frequency" : "应用当前选择的 IMU 输出格式、波特率和输出频率");
    }
    if (imu_hi91_btn_)
    {
        imu_hi91_btn_->setToolTip(english ? "Switch IMU output to HI91 immediately" : "立即切换 IMU 输出为 HI91");
    }
    if (imu_hi92_btn_)
    {
        imu_hi92_btn_->setToolTip(english ? "Switch IMU output to HI92 immediately" : "立即切换 IMU 输出为 HI92");
    }
    if (imu_baud_115200_btn_)
    {
        imu_baud_115200_btn_->setToolTip(english ? "Switch IMU baud rate to 115200" : "一键切换 IMU 波特率到 115200");
    }
    if (imu_baud_921600_btn_)
    {
        imu_baud_921600_btn_->setToolTip(english ? "Switch IMU baud rate to 921600" : "一键切换 IMU 波特率到 921600");
    }
    if (imu_rate_100_btn_)
    {
        imu_rate_100_btn_->setToolTip(english ? "Switch IMU output frequency to 100 Hz" : "一键切换 IMU 输出频率到 100 Hz");
    }
    if (imu_rate_200_btn_)
    {
        imu_rate_200_btn_->setToolTip(english ? "Switch IMU output frequency to 200 Hz" : "一键切换 IMU 输出频率到 200 Hz");
    }
    if (imu_rate_500_btn_)
    {
        imu_rate_500_btn_->setToolTip(english ? "Switch IMU output frequency to 500 Hz" : "一键切换 IMU 输出频率到 500 Hz");
    }
    if (imu_rate_1000_btn_)
    {
        imu_rate_1000_btn_->setToolTip(english ? "Switch IMU output frequency to 1000 Hz" : "一键切换 IMU 输出频率到 1000 Hz");
    }
    ptb_rate_lbl_->setText(english ? "Rate:" : "频率:");
    hmp_rate_lbl_->setText(english ? "Rate:" : "频率:");
    lidar_rate_lbl_->setText(english ? "Rate:" : "频率:");
    if (lidar_rate_combo_)
    {
        const QString oldText = english ? QStringLiteral("不设定") : QStringLiteral("No Set");
        const QString newText = english ? QStringLiteral("No Set") : QStringLiteral("不设定");
        const int idx = lidar_rate_combo_->findText(oldText);
        if (idx >= 0)
        {
            lidar_rate_combo_->setItemText(idx, newText);
        }
    }

    gnss_panel_->setEnglish(english);
    imu_panel_->setEnglish(english);
    ptb_panel_->setEnglish(english);
    hmp_panel_->setEnglish(english);
    lidar_panel_->setEnglish(english);
    tcp_wave_panel_->setEnglish(english);

    if (rtk_config_dialog_)
    {
        rtk_config_dialog_->setEnglish(english);
    }
    if (session_viewer_window_)
    {
        session_viewer_window_->setEnglish(english);
    }

    updateRecordingStatusLabel();
}

void MainWindow::onOpenSessionViewerClicked()
{
    if (!session_viewer_window_)
    {
        session_viewer_window_ = new SessionViewerWindow(this);
        session_viewer_window_->setEnglish(is_english_);
    }

    if (!session_directory_.isEmpty())
    {
        session_viewer_window_->openSessionPath(session_directory_);
    }

    session_viewer_window_->show();
    session_viewer_window_->raise();
    session_viewer_window_->activateWindow();
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

int MainWindow::parseRate(const QString& text) const
{
    bool ok;
    int rate = text.toInt(&ok);
    if (ok && rate >= 1 && rate <= 1000)
    {
        return rate;
    }
    return 20;
}

bool MainWindow::isLidarRateUnspecified(const QString& text) const
{
    const QString trimmed = text.trimmed();
    return trimmed.compare(QStringLiteral("No Set"), Qt::CaseInsensitive) == 0
        || trimmed == QStringLiteral("不设定");
}

int MainWindow::effectiveLidarSampleRate(const QString& text) const
{
    if (isLidarRateUnspecified(text))
    {
        return 100;
    }
    return std::min(parseRate(text), 100);
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
    lidar_rate_combo_->blockSignals(true);
    
    gnss_rate_combo_->setCurrentText(text);
    imu_rate_combo_->setCurrentText(text);
    ptb_rate_combo_->setCurrentText(text);
    hmp_rate_combo_->setCurrentText(text);
    if (!isLidarRateUnspecified(lidar_rate_combo_->currentText()))
    {
        lidar_rate_combo_->setCurrentText(QString::number(std::min(rate, 100)));
    }
    
    gnss_rate_combo_->blockSignals(false);
    imu_rate_combo_->blockSignals(false);
    ptb_rate_combo_->blockSignals(false);
    hmp_rate_combo_->blockSignals(false);
    lidar_rate_combo_->blockSignals(false);
    
    const CollectorSnapshot collectors = snapshotCollectors();

    if (collectors.gnss && collectors.gnss->isRunning())
    {
        collectors.gnss->setSampleRate(rate);
        collectors.gnss->setDeviceSampleRate(rate);
    }
    if (collectors.imu && collectors.imu->isRunning())
    {
        collectors.imu->setSampleRate(rate);
        collectors.imu->setDeviceSampleRate(rate);
    }
    if (collectors.ptb && collectors.ptb->isRunning())
    {
        collectors.ptb->setSampleRate(rate);
        collectors.ptb->setDeviceSampleRate(rate);
    }
    if (collectors.hmp && collectors.hmp->isRunning())
    {
        collectors.hmp->setSampleRate(rate);
    }
    if (collectors.lidar && collectors.lidar->isRunning())
    {
        const int lidarRate = std::min(rate, 100);
        collectors.lidar->setSampleRate(lidarRate);
        if (!isLidarRateUnspecified(lidar_rate_combo_->currentText()))
        {
            collectors.lidar->setDeviceSampleRate(lidarRate);
        }
    }
    
    log(QString(is_english_ ? "All rates set to %1 Hz" : "所有频率已设置为 %1 Hz").arg(rate));
}

void MainWindow::onGnssRateChanged(const QString& text)
{
    gnss_sample_rate_ = parseRate(text);
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.gnss)
    {
        collectors.gnss->setSampleRate(gnss_sample_rate_);
        collectors.gnss->setDeviceSampleRate(gnss_sample_rate_);
    }
    log(QString(is_english_ ? "GNSS sample rate set to %1 Hz" : "GNSS采样频率已设置为 %1 Hz").arg(gnss_sample_rate_));
}

void MainWindow::onImuRateChanged(const QString& text)
{
    imu_sample_rate_ = parseRate(text);
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.imu)
    {
        collectors.imu->setSampleRate(imu_sample_rate_);
        if (collectors.imu->isRunning())
        {
            collectors.imu->setDeviceSampleRate(imu_sample_rate_);
        }
    }
    log(QString(is_english_ ? "IMU sample rate set to %1 Hz" : "IMU采样频率已设置为 %1 Hz").arg(imu_sample_rate_));
}

void MainWindow::onPtbRateChanged(const QString& text)
{
    ptb_sample_rate_ = parseRate(text);
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.ptb)
    {
        collectors.ptb->setSampleRate(ptb_sample_rate_);
        if (collectors.ptb->isRunning())
        {
            collectors.ptb->setDeviceSampleRate(ptb_sample_rate_);
        }
    }
    log(QString(is_english_ ? "PTB sample rate set to %1 Hz" : "PTB采样频率已设置为 %1 Hz").arg(ptb_sample_rate_));
}

void MainWindow::onHmpRateChanged(const QString& text)
{
    hmp_sample_rate_ = parseRate(text);
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.hmp) collectors.hmp->setSampleRate(hmp_sample_rate_);
    log(QString(is_english_ ? "HMP sample rate set to %1 Hz" : "HMP采样频率已设置为 %1 Hz").arg(hmp_sample_rate_));
}

void MainWindow::onLidarRateChanged(const QString& text)
{
    const bool skipDeviceRate = isLidarRateUnspecified(text);
    lidar_sample_rate_ = effectiveLidarSampleRate(text);
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.lidar)
    {
        collectors.lidar->setSampleRate(lidar_sample_rate_);
        if (collectors.lidar->isRunning() && !skipDeviceRate)
        {
            collectors.lidar->setDeviceSampleRate(lidar_sample_rate_);
        }
    }
    if (skipDeviceRate)
    {
        log(is_english_ ? "Lidar output-rate command disabled; using device default/adaptive output" : "已禁用激光测距仪输出频率下发，使用设备默认/自适应输出");
    }
    else
    {
        log(QString(is_english_ ? "Lidar sample rate set to %1 Hz" : "激光测距仪采样频率已设置为 %1 Hz").arg(lidar_sample_rate_));
    }
}

void MainWindow::applyAllSampleRates()
{
    int rate = parseRate(global_rate_combo_->currentText());
    const CollectorSnapshot collectors = snapshotCollectors();

    if (collectors.gnss && collectors.gnss->isRunning())
    {
        collectors.gnss->setSampleRate(rate);
        collectors.gnss->setDeviceSampleRate(rate);
    }
    if (collectors.imu && collectors.imu->isRunning())
    {
        collectors.imu->setSampleRate(rate);
        collectors.imu->setDeviceSampleRate(rate);
    }
    if (collectors.ptb && collectors.ptb->isRunning())
    {
        collectors.ptb->setSampleRate(rate);
        collectors.ptb->setDeviceSampleRate(rate);
    }
    if (collectors.hmp && collectors.hmp->isRunning())
    {
        collectors.hmp->setSampleRate(rate);
    }
    if (collectors.lidar && collectors.lidar->isRunning())
    {
        const int lidarRate = std::min(rate, 100);
        collectors.lidar->setSampleRate(lidarRate);
        if (!isLidarRateUnspecified(lidar_rate_combo_->currentText()))
        {
            collectors.lidar->setDeviceSampleRate(lidarRate);
        }
    }

    gnss_rate_combo_->blockSignals(true);
    imu_rate_combo_->blockSignals(true);
    ptb_rate_combo_->blockSignals(true);
    hmp_rate_combo_->blockSignals(true);
    lidar_rate_combo_->blockSignals(true);

    gnss_rate_combo_->setCurrentText(QString::number(rate));
    imu_rate_combo_->setCurrentText(QString::number(rate));
    ptb_rate_combo_->setCurrentText(QString::number(rate));
    hmp_rate_combo_->setCurrentText(QString::number(rate));
    if (!isLidarRateUnspecified(lidar_rate_combo_->currentText()))
    {
        lidar_rate_combo_->setCurrentText(QString::number(std::min(rate, 100)));
    }

    gnss_rate_combo_->blockSignals(false);
    imu_rate_combo_->blockSignals(false);
    ptb_rate_combo_->blockSignals(false);
    hmp_rate_combo_->blockSignals(false);
    lidar_rate_combo_->blockSignals(false);

    gnss_sample_rate_ = rate;
    imu_sample_rate_ = rate;
    ptb_sample_rate_ = rate;
    hmp_sample_rate_ = rate;
    lidar_sample_rate_ = isLidarRateUnspecified(lidar_rate_combo_->currentText())
        ? 100
        : std::min(rate, 100);

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
        if (QScrollBar *scrollBar = log_text_edit_->verticalScrollBar())
        {
            scrollBar->setValue(scrollBar->maximum());
        }
        return;
    }

    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    log_text_edit_->append(QString("[%1] %2").arg(timestamp, message));
    if (QScrollBar *scrollBar = log_text_edit_->verticalScrollBar())
    {
        scrollBar->setValue(scrollBar->maximum());
    }
    has_inline_progress_log_ = false;

    appendEventLogLine(QStringLiteral("info"), message);
    if (shouldMirrorToErrorLog(message))
    {
        appendErrorLogLine(message);
    }
}

void MainWindow::updateRecordingStatusLabel()
{
    if (!recording_status_label_)
    {
        return;
    }

    if (sensors_file_ && sensors_file_->isOpen())
    {
        const QFileInfo info(session_directory_);
        if (recording_paused_)
        {
            recording_status_label_->setText(
                QString(is_english_ ? "Recording: Paused | %1 sensor rows | %2 waveform frames | %3"
                                    : "记录: 已暂停 | 设备 %1 行 | 波形 %2 帧 | %3")
                    .arg(static_cast<qlonglong>(recording_entry_count_.load()))
                    .arg(static_cast<qlonglong>(waveform_frame_count_.load()))
                    .arg(info.fileName()));
            recording_status_label_->setProperty("status", "connecting");
        }
        else
        {
            recording_status_label_->setText(
                QString(is_english_ ? "Recording: %1 sensor rows | %2 waveform frames | %3"
                                    : "记录中: 设备 %1 行 | 波形 %2 帧 | %3")
                    .arg(static_cast<qlonglong>(recording_entry_count_.load()))
                    .arg(static_cast<qlonglong>(waveform_frame_count_.load()))
                    .arg(info.fileName()));
            recording_status_label_->setProperty("status", "connected");
        }
    }
    else
    {
        recording_status_label_->setText(is_english_ ? "Recording: Off" : "记录: 未记录");
        recording_status_label_->setProperty("status", "disconnected");
    }

    recording_status_label_->style()->unpolish(recording_status_label_);
    recording_status_label_->style()->polish(recording_status_label_);
    updateRecordingActionStates();
}

QString MainWindow::defaultRecordingDirectory() const
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i)
    {
        if (QFileInfo::exists(dir.filePath("CMakeLists.txt")) && QFileInfo::exists(dir.filePath("README.md")))
        {
            return dir.filePath("data");
        }
        if (!dir.cdUp())
        {
            break;
        }
    }

    return QDir(QCoreApplication::applicationDirPath()).filePath("data");
}

QString MainWindow::locateRepositoryRoot() const
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i)
    {
        if (QFileInfo::exists(dir.filePath("CMakeLists.txt")) && QFileInfo::exists(dir.filePath("README.md")))
        {
            return dir.path();
        }
        if (!dir.cdUp())
        {
            break;
        }
    }

    return QString();
}

bool MainWindow::prepareRecordingSessionLayout(const QString& recordsPath, const QString& sessionName)
{
    QDir recordsDir(recordsPath);
    if (!recordsDir.exists() && !recordsDir.mkpath("."))
    {
        return false;
    }

    QString finalSessionName = sessionName;
    QString finalSessionDirectory = recordsDir.filePath(finalSessionName);
    int suffix = 1;
    while (QFileInfo::exists(finalSessionDirectory))
    {
        finalSessionName = QString("%1_%2").arg(sessionName).arg(suffix++);
        finalSessionDirectory = recordsDir.filePath(finalSessionName);
    }

    QDir sessionDir(finalSessionDirectory);
    if (!recordsDir.mkpath(finalSessionName) ||
        !sessionDir.mkpath("waveform") ||
        !sessionDir.mkpath("sensors") ||
        !sessionDir.mkpath("logs") ||
        !sessionDir.mkpath("config"))
    {
        return false;
    }

    session_name_ = finalSessionName;
    session_directory_ = QDir::fromNativeSeparators(finalSessionDirectory);
    waveform_directory_ = QDir::fromNativeSeparators(sessionDir.filePath("waveform"));
    sensors_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("sensors/devices.csv"));
    imu_raw_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("sensors/imu_raw.dat"));
    imu_raw_doc_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("imu_raw_dat_format.md"));
    session_metadata_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("session.json"));
    event_log_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("logs/event_log.csv"));
    error_log_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("logs/error_log.txt"));
    device_config_filename_ = QDir::fromNativeSeparators(sessionDir.filePath("config/device_config.json"));
    return true;
}

bool MainWindow::copyImuRawFormatDocumentToSession()
{
    if (session_directory_.isEmpty() || imu_raw_doc_filename_.isEmpty())
    {
        return false;
    }

    const QString repositoryRoot = locateRepositoryRoot();
    if (repositoryRoot.isEmpty())
    {
        return false;
    }

    const QString sourcePath = QDir(repositoryRoot).filePath("docs/imu_raw_dat_format.md");
    if (!QFileInfo::exists(sourcePath))
    {
        return false;
    }

    QFile::remove(imu_raw_doc_filename_);
    return QFile::copy(sourcePath, imu_raw_doc_filename_);
}

void MainWindow::appendEventLogLine(const QString& level, const QString& message)
{
    std::lock_guard<std::mutex> lock(recording_files_mutex_);
    if (!event_log_file_ || !event_log_file_->isOpen())
    {
        return;
    }

    QTextStream out(event_log_file_.get());
    out.setEncoding(QStringConverter::Utf8);
    out << csvEscape(recordingTimestampUtc()) << ','
        << currentTimestampUs() << ','
        << csvEscape(level) << ','
        << csvEscape(message) << '\n';
    out.flush();
}

void MainWindow::appendErrorLogLine(const QString& message)
{
    std::lock_guard<std::mutex> lock(recording_files_mutex_);
    if (!error_log_file_ || !error_log_file_->isOpen())
    {
        return;
    }

    QTextStream out(error_log_file_.get());
    out.setEncoding(QStringConverter::Utf8);
    out << '[' << recordingTimestampUtc() << "] " << message << '\n';
    out.flush();
}

quint64 MainWindow::currentTimestampUs() const
{
    return static_cast<quint64>(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) * 1000ULL;
}

quint64 MainWindow::steadyToEpochUs(const std::chrono::steady_clock::time_point& timePoint) const
{
    if (timePoint == std::chrono::steady_clock::time_point{})
    {
        return 0;
    }

    const auto delta = timePoint - steady_clock_anchor_;
    const auto systemPoint = system_clock_anchor_ + std::chrono::duration_cast<std::chrono::system_clock::duration>(delta);
    return static_cast<quint64>(std::chrono::duration_cast<std::chrono::microseconds>(systemPoint.time_since_epoch()).count());
}

void MainWindow::writeSessionMetadata(const QString& endTimeUtc)
{
    if (session_metadata_filename_.isEmpty() || session_directory_.isEmpty())
    {
        return;
    }

    QDir sessionDir(session_directory_);
    QJsonObject root;
    root["session_name"] = session_name_;
    root["start_time_utc"] = session_start_time_utc_;
    root["start_time_us"] = QString::number(session_start_time_us_);
    root["end_time_utc"] = endTimeUtc;
    root["software_version"] = QCoreApplication::applicationVersion().isEmpty()
        ? QStringLiteral("dev")
        : QCoreApplication::applicationVersion();
    root["waveform_points_per_frame"] = 50000;
    root["sensor_export_rate_hz"] = recording_export_rate_hz_;
    root["waveform_export_rate_hz"] = 0;
    root["waveform_export_mode"] = QStringLiteral("per_frame");
    root["waveform_value_type"] = QStringLiteral("float32");
    root["waveform_timestamp_type"] = QStringLiteral("uint64");
    root["timestamp_unit"] = QStringLiteral("microseconds");
    root["waveform_split_minutes"] = waveform_split_minutes_;
    root["sensor_rows"] = QString::number(recording_entry_count_.load());
    root["waveform_frames"] = QString::number(waveform_frame_count_.load());
    root["waveform_file_count"] = QString::number(waveform_file_count_.load());

    QJsonObject paths;
    paths["waveform_directory"] = sessionDir.relativeFilePath(waveform_directory_);
    paths["devices_csv"] = sessionDir.relativeFilePath(sensors_filename_);
    paths["imu_raw_dat"] = sessionDir.relativeFilePath(imu_raw_filename_);
    paths["imu_raw_format_doc"] = sessionDir.relativeFilePath(imu_raw_doc_filename_);
    paths["event_log"] = sessionDir.relativeFilePath(event_log_filename_);
    paths["error_log"] = sessionDir.relativeFilePath(error_log_filename_);
    paths["device_config"] = sessionDir.relativeFilePath(device_config_filename_);
    root["paths"] = paths;

    QFile file(session_metadata_filename_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
}

void MainWindow::writeDeviceConfigSnapshot()
{
    if (device_config_filename_.isEmpty())
    {
        return;
    }

    QJsonObject root;
    root["recording_directory"] = recording_directory_;
    root["session_directory"] = session_directory_;
    root["sensor_export_rate_hz"] = recording_export_rate_hz_;
    root["waveform_split_minutes"] = waveform_split_minutes_;

    QJsonObject waveform;
    waveform["host"] = tcp_wave_panel_ ? tcp_wave_panel_->host() : QStringLiteral("127.0.0.1");
    waveform["port"] = tcp_wave_panel_ ? tcp_wave_panel_->port() : 8888;
    waveform["frame_rate_hz"] = 0;
    waveform["frame_rate_mode"] = QStringLiteral("per_frame");
    waveform["points_per_frame"] = 50000;
    waveform["value_type"] = QStringLiteral("float32");
    waveform["timestamp_type"] = QStringLiteral("uint64");
    root["waveform"] = waveform;

    QJsonObject sensors;
    auto addSerialConfig = [&sensors](const QString& name, QComboBox* port, QComboBox* baud, QComboBox* rate) {
        QJsonObject obj;
        obj["port"] = port ? port->currentText() : QString();
        obj["baud"] = baud ? baud->currentText() : QString();
        obj["rate_hz"] = rate ? rate->currentText() : QString();
        sensors[name] = obj;
    };
    addSerialConfig("gnss", gnss_port_combo_, gnss_baud_combo_, gnss_rate_combo_);
    addSerialConfig("imu", imu_port_combo_, imu_baud_combo_, imu_rate_combo_);
    QJsonObject imuConfig = sensors.value("imu").toObject();
    imuConfig["format"] = imu_format_combo_ ? imu_format_combo_->currentText() : QStringLiteral("HI91");
    sensors["imu"] = imuConfig;
    addSerialConfig("ptb", ptb_port_combo_, ptb_baud_combo_, ptb_rate_combo_);
    addSerialConfig("hmp", hmp_port_combo_, hmp_baud_combo_, hmp_rate_combo_);
    addSerialConfig("tf03", lidar_port_combo_, lidar_baud_combo_, lidar_rate_combo_);
    root["sensors"] = sensors;

    QFile file(device_config_filename_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
}

void MainWindow::startRecordingWorkers()
{
    if (recording_thread_running_.load() || waveform_writer_running_.load())
    {
        return;
    }

    recording_paused_ = false;
    {
        std::lock_guard<std::mutex> lock(waveform_queue_mutex_);
        waveform_queue_.clear();
    }

    waveform_writer_running_.store(true);
    waveform_writer_thread_ = std::thread([this]() {
        runWaveformWriter();
    });

    QFile *filePtr = sensors_file_.get();
    recording_thread_running_.store(true);
    recording_thread_ = std::thread([this, filePtr]() {
        const int exportRateHz = std::max(1, recording_export_rate_hz_);
        const auto exportPeriod = std::chrono::microseconds(1000000 / exportRateHz);
        auto nextTick = std::chrono::steady_clock::now();
        while (recording_thread_running_.load())
        {
            const auto tickTime = std::chrono::steady_clock::now();
            const quint64 recordTimestampUs = currentTimestampUs();
            const QString recordTimestampUtc = recordingTimestampUtc();
            const CollectorSnapshot collectors = snapshotCollectors();
            const VaporView::GnssData gnssSample = collectors.gnss ? collectors.gnss->getLatestData() : VaporView::GnssData();
            const VaporView::ImuData imuSample = collectors.imu ? collectors.imu->getLatestData() : VaporView::ImuData();
            const VaporView::PtbData ptbSample = collectors.ptb ? collectors.ptb->getLatestData() : VaporView::PtbData();
            const VaporView::HmpData hmpSample = collectors.hmp ? collectors.hmp->getLatestData() : VaporView::HmpData();
            const VaporView::LidarData lidarSample = collectors.lidar ? collectors.lidar->getLatestData() : VaporView::LidarData();

            QStringList row;
            row.reserve(64);
            row << QString::number(recordTimestampUs) << recordTimestampUtc;

            auto appendEmptyColumns = [&row](int count) {
                for (int i = 0; i < count; ++i)
                {
                    row << QString();
                }
            };
            auto appendBool = [&row](bool value) {
                row << csvBool(value);
            };
            auto isFresh = [tickTime](auto* collector, const auto& sample) {
                if (!collector || sample.timestamp == std::chrono::steady_clock::time_point{})
                {
                    return false;
                }
                const int rate = std::max(1, collector->getSampleRate());
                const int timeoutMs = std::max(250, static_cast<int>(std::ceil(3000.0 / rate)));
                const auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(tickTime - sample.timestamp).count();
                return ageMs >= 0 && ageMs <= timeoutMs;
            };

            if (isFresh(collectors.gnss.get(), gnssSample))
            {
                row
                    << QString::number(steadyToEpochUs(gnssSample.timestamp))
                    << QString::number(gnssSample.latitude, 'f', 9)
                    << QString::number(gnssSample.longitude, 'f', 9)
                    << QString::number(gnssSample.altitude, 'f', 6)
                    << QString::fromStdString(gnssSample.position_status)
                    << QString::number(gnssSample.num_satellites_used)
                    << QString::number(gnssSample.heading, 'f', 6)
                    << QString::number(gnssSample.heading_pitch, 'f', 6)
                    << QString::number(gnssSample.vel_north, 'f', 6)
                    << QString::number(gnssSample.vel_east, 'f', 6)
                    << QString::number(gnssSample.vel_down, 'f', 6);
                appendBool(gnssSample.valid);
                row << QString::fromStdString(gnssSample.error_message);
            }
            else
            {
                appendEmptyColumns(13);
            }

            if (isFresh(collectors.imu.get(), imuSample))
            {
                row
                    << QString::number(steadyToEpochUs(imuSample.timestamp))
                    << QString::number(imuSample.acceleration[0], 'f', 6)
                    << QString::number(imuSample.acceleration[1], 'f', 6)
                    << QString::number(imuSample.acceleration[2], 'f', 6)
                    << QString::number(imuSample.gyroscope[0], 'f', 6)
                    << QString::number(imuSample.gyroscope[1], 'f', 6)
                    << QString::number(imuSample.gyroscope[2], 'f', 6)
                    << QString::number(imuSample.rpy[0], 'f', 6)
                    << QString::number(imuSample.rpy[1], 'f', 6)
                    << QString::number(imuSample.rpy[2], 'f', 6);
                appendBool(imuSample.valid);
                row << QString::fromStdString(imuSample.error_message);
            }
            else
            {
                appendEmptyColumns(12);
            }

            if (isFresh(collectors.hmp.get(), hmpSample))
            {
                row
                    << QString::number(steadyToEpochUs(hmpSample.timestamp))
                    << QString::number(hmpSample.temperature, 'f', 6)
                    << QString::number(hmpSample.humidity, 'f', 6);
                appendBool(hmpSample.valid);
                row << QString::fromStdString(hmpSample.error_message);
            }
            else
            {
                appendEmptyColumns(5);
            }

            if (isFresh(collectors.ptb.get(), ptbSample))
            {
                row << QString::number(steadyToEpochUs(ptbSample.timestamp))
                    << QString::number(ptbSample.pressure_hpa, 'f', 6);
                appendBool(ptbSample.valid);
                row << QString::fromStdString(ptbSample.error_message);
            }
            else
            {
                appendEmptyColumns(4);
            }

            if (isFresh(collectors.lidar.get(), lidarSample))
            {
                row
                    << QString::number(steadyToEpochUs(lidarSample.timestamp))
                    << QString::number(lidarSample.distance_m, 'f', 6)
                    << QString::number(lidarSample.signal_strength);
                appendBool(lidarSample.valid);
                row << QString::fromStdString(lidarSample.error_message);
            }
            else
            {
                appendEmptyColumns(4);
            }

            {
                std::lock_guard<std::mutex> lock(recording_files_mutex_);
                QTextStream out(filePtr);
                out.setEncoding(QStringConverter::Utf8);
                for (int i = 0; i < row.size(); ++i)
                {
                    if (i > 0)
                    {
                        out << ',';
                    }
                    out << csvEscape(row.at(i));
                }
                out << '\n';
                out.flush();
            }

            recording_entry_count_.fetch_add(1);
            QMetaObject::invokeMethod(this, [this]() {
                updateRecordingStatusLabel();
            }, Qt::QueuedConnection);

            nextTick += exportPeriod;
            std::this_thread::sleep_until(nextTick);
        }
    });
}

void MainWindow::stopRecordingWorkers()
{
    recording_thread_running_.store(false);
    if (recording_thread_.joinable())
    {
        recording_thread_.join();
    }

    waveform_writer_running_.store(false);
    waveform_queue_cv_.notify_all();
    if (waveform_writer_thread_.joinable())
    {
        waveform_writer_thread_.join();
    }
}

bool MainWindow::startRecordingSession()
{
    if (sensors_file_ && sensors_file_->isOpen())
    {
        if (!recording_paused_)
        {
            return true;
        }

        startRecordingWorkers();
        updateRecordingStatusLabel();
        log(QString(is_english_ ? "Resumed recording session: %1" : "已继续记录会话: %1").arg(session_directory_));
        return true;
    }

    QString recordsPath = recording_directory_.trimmed();
    if (recordsPath.isEmpty())
    {
        recordsPath = defaultRecordingDirectory();
        recording_directory_ = recordsPath;
    }

    const QString sessionName = QStringLiteral("session_%1").arg(recordingSessionDirectoryTimestamp());
    if (!prepareRecordingSessionLayout(recordsPath, sessionName))
    {
        QMessageBox::warning(
            this,
            is_english_ ? "Error" : "错误",
            is_english_ ? "Failed to create session directories" : "无法创建会话目录结构");
        return false;
    }

    sensors_file_ = std::make_unique<QFile>(sensors_filename_);
    imu_raw_file_ = std::make_unique<QFile>(imu_raw_filename_);
    event_log_file_ = std::make_unique<QFile>(event_log_filename_);
    error_log_file_ = std::make_unique<QFile>(error_log_filename_);
    if (!sensors_file_->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
        !imu_raw_file_->open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        !event_log_file_->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
        !error_log_file_->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        sensors_file_.reset();
        imu_raw_file_.reset();
        event_log_file_.reset();
        error_log_file_.reset();
        QMessageBox::warning(
            this,
            is_english_ ? "Error" : "错误",
            is_english_ ? "Failed to open session files for writing" : "无法打开会话文件进行写入");
        return false;
    }

    session_start_time_utc_ = recordingTimestampUtc();
    session_start_time_us_ = currentTimestampUs();
    recording_entry_count_.store(0);
    waveform_frame_count_.store(0);
    waveform_file_count_.store(0);
    {
        std::lock_guard<std::mutex> lock(waveform_queue_mutex_);
        waveform_queue_.clear();
    }

    {
        QTextStream eventOut(event_log_file_.get());
        eventOut.setEncoding(QStringConverter::Utf8);
        eventOut << "timestamp_utc,timestamp_us,level,message\n";
        eventOut.flush();
    }

    {
        const ImuRawFileHeader imuHeader{{kImuRawMagic[0], kImuRawMagic[1], kImuRawMagic[2], kImuRawMagic[3],
                                          kImuRawMagic[4], kImuRawMagic[5], kImuRawMagic[6], kImuRawMagic[7]},
                                         1u,
                                         static_cast<quint32>(sizeof(ImuRawFileHeader))};
        imu_raw_file_->write(reinterpret_cast<const char*>(&imuHeader), sizeof(imuHeader));
        imu_raw_file_->flush();
    }

    writeSensorsHeader();
    if (!copyImuRawFormatDocumentToSession())
    {
        log(QString(is_english_
            ? "Warning: failed to copy IMU raw format document into session folder"
            : "警告：未能将 IMU 原始格式说明复制到当前会话目录"));
    }
    writeSessionMetadata();
    writeDeviceConfigSnapshot();
    startRecordingWorkers();
    updateRecordingStatusLabel();
    log(QString(is_english_ ? "Started recording session: %1" : "已开始记录会话: %1").arg(session_directory_));
    return true;
}

void MainWindow::onChooseRecordingDirectoryClicked()
{
    const QString currentDirectory = recording_directory_.isEmpty() ? defaultRecordingDirectory() : recording_directory_;
    const QString selectedDirectory = QFileDialog::getExistingDirectory(
        this,
        is_english_ ? "Select Recording Folder" : "选择记录目录",
        currentDirectory,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (selectedDirectory.isEmpty())
    {
        return;
    }

    recording_directory_ = QDir::fromNativeSeparators(selectedDirectory);
    QSettings settings("VaporView", "MainWindow");
    settings.setValue("recording_directory", recording_directory_);
    log(QString(is_english_ ? "Recording folder set to: %1" : "记录目录已设置为: %1").arg(recording_directory_));
}

void MainWindow::onStartRecordingClicked()
{
    const bool tcpConnected = tcp_wave_panel_ && tcp_wave_panel_->isConnected();
    if (!is_connected_ && !tcpConnected)
    {
        log(is_english_ ? "At least one serial device or the TCP wave link must be connected before recording"
                        : "开始记录前，至少需要一个串口设备在线或 TCP 波形链路已连接");
        return;
    }

    if (!startRecordingSession())
    {
        log(is_english_ ? "Failed to start recording session" : "启动记录会话失败");
    }
}

void MainWindow::onPauseRecordingClicked()
{
    pauseRecordingSession(true);
}

void MainWindow::onStopRecordingClicked()
{
    stopRecording(true);
}

void MainWindow::pauseRecordingSession(bool announce)
{
    if (!sensors_file_ || !sensors_file_->isOpen() || recording_paused_)
    {
        return;
    }

    stopRecordingWorkers();
    recording_paused_ = true;
    writeSessionMetadata();
    updateRecordingStatusLabel();

    if (announce)
    {
        log(QString(is_english_ ? "Paused recording session: %1" : "已暂停记录会话: %1").arg(session_directory_));
    }
}

void MainWindow::stopRecording(bool announce)
{
    const bool hadOpenSession = sensors_file_ && sensors_file_->isOpen();
    stopRecordingWorkers();

    if (!hadOpenSession)
    {
        recording_paused_ = false;
        updateRecordingStatusLabel();
        return;
    }

    const qint64 entryCount = recording_entry_count_.load();
    const qint64 waveformCount = waveform_frame_count_.load();
    const QString sessionPath = session_directory_;

    if (sensors_file_ && sensors_file_->isOpen())
    {
        writeSessionMetadata(recordingTimestampUtc());
    }

    {
        std::lock_guard<std::mutex> lock(recording_files_mutex_);
        if (sensors_file_ && sensors_file_->isOpen())
        {
            sensors_file_->flush();
            sensors_file_->close();
        }
        if (imu_raw_file_ && imu_raw_file_->isOpen())
        {
            imu_raw_file_->flush();
            imu_raw_file_->close();
        }
        if (event_log_file_ && event_log_file_->isOpen())
        {
            event_log_file_->flush();
            event_log_file_->close();
        }
        if (error_log_file_ && error_log_file_->isOpen())
        {
            error_log_file_->flush();
            error_log_file_->close();
        }
    }

    sensors_file_.reset();
    imu_raw_file_.reset();
    event_log_file_.reset();
    error_log_file_.reset();
    recording_entry_count_.store(0);
    waveform_frame_count_.store(0);
    waveform_file_count_.store(0);
    recording_paused_ = false;
    session_directory_.clear();
    session_name_.clear();
    session_start_time_utc_.clear();
    session_start_time_us_ = 0;
    sensors_filename_.clear();
    imu_raw_filename_.clear();
    imu_raw_doc_filename_.clear();
    session_metadata_filename_.clear();
    event_log_filename_.clear();
    error_log_filename_.clear();
    device_config_filename_.clear();
    waveform_directory_.clear();

    updateRecordingStatusLabel();

    if (announce)
    {
        log(QString(is_english_
            ? "Stopped recording (%1 sensor rows, %2 waveform frames): %3"
            : "记录已结束（设备 %1 行，波形 %2 帧）: %3")
            .arg(entryCount)
            .arg(waveformCount)
            .arg(sessionPath));
    }
}

void MainWindow::writeSensorsHeader()
{
    if (!sensors_file_ || !sensors_file_->isOpen())
    {
        return;
    }

    QTextStream out(sensors_file_.get());
    out.setEncoding(QStringConverter::Utf8);
    out.setGenerateByteOrderMark(true);
    out
        << "record_timestamp_us,record_timestamp_utc,"
        << "rtk_timestamp_us,rtk_lat,rtk_lon,rtk_alt,rtk_fix,rtk_sat,rtk_heading,rtk_pitch,rtk_vel_n,rtk_vel_e,rtk_vel_d,rtk_valid,rtk_error_message,"
        << "imu_timestamp_us,imu_ax,imu_ay,imu_az,imu_gx,imu_gy,imu_gz,imu_roll,imu_pitch,imu_yaw,imu_valid,imu_error_message,"
        << "th_timestamp_us,temp_c,humidity_rh,th_valid,th_error_message,"
        << "baro_timestamp_us,baro_hpa,baro_valid,baro_error_message,"
        << "tf03_distance_m,tf03_signal_strength,tf03_valid,tf03_error_message\n";
    out.flush();
}

void MainWindow::onNormalizedSecondHarmonicFrameReady(quint64 timestampUs, QVector<float> samples)
{
    if (!waveform_writer_running_.load())
    {
        return;
    }

    WaveformFrame frame;
    frame.timestamp_us = timestampUs;
    frame.samples = std::move(samples);

    {
        std::lock_guard<std::mutex> lock(waveform_queue_mutex_);
        waveform_queue_.push_back(std::move(frame));
    }
    waveform_queue_cv_.notify_one();
}

void MainWindow::runWaveformWriter()
{
    constexpr int kExpectedSamplesPerFrame = 50000;
    const quint64 kSegmentDurationUs = static_cast<quint64>(std::max(1, waveform_split_minutes_)) * 60ULL * 1000ULL * 1000ULL;

    std::unique_ptr<QFile> waveformFile;
    quint64 currentSegmentStartUs = 0;

    auto openSegment = [&](quint64 segmentStartUs) -> bool {
        const QString filename = QDir(waveform_directory_).filePath(
            QString("waveform_%1.dat").arg(waveformSegmentTimestamp(segmentStartUs)));
        waveformFile = std::make_unique<QFile>(filename);
        if (!waveformFile->open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            QMetaObject::invokeMethod(this, [this, filename]() {
                const QString message = QString(is_english_
                    ? "Failed to open waveform file: %1"
                    : "无法打开波形文件: %1").arg(filename);
                log(message);
                appendErrorLogLine(message);
            }, Qt::QueuedConnection);
            waveformFile.reset();
            return false;
        }

        currentSegmentStartUs = segmentStartUs;
        waveform_file_count_.fetch_add(1);
        return true;
    };

    while (true)
    {
        WaveformFrame frame;
        {
            std::unique_lock<std::mutex> lock(waveform_queue_mutex_);
            waveform_queue_cv_.wait(lock, [this]() {
                return !waveform_writer_running_.load() || !waveform_queue_.empty();
            });
            if (waveform_queue_.empty() && !waveform_writer_running_.load())
            {
                break;
            }
            if (waveform_queue_.empty())
            {
                continue;
            }
            frame = std::move(waveform_queue_.front());
            waveform_queue_.pop_front();
        }

        if (frame.samples.size() != kExpectedSamplesPerFrame)
        {
            QMetaObject::invokeMethod(this, [this, sampleCount = frame.samples.size()]() {
                const QString message = QString(is_english_
                    ? "Skipped waveform frame with unexpected sample count: %1"
                    : "已跳过采样点数量异常的波形帧: %1").arg(sampleCount);
                log(message);
                appendErrorLogLine(message);
            }, Qt::QueuedConnection);
            continue;
        }

        const quint64 segmentStartUs = frame.timestamp_us - (frame.timestamp_us % kSegmentDurationUs);
        if (!waveformFile || currentSegmentStartUs != segmentStartUs)
        {
            if (waveformFile && waveformFile->isOpen())
            {
                waveformFile->flush();
                waveformFile->close();
            }
            if (!openSegment(segmentStartUs))
            {
                continue;
            }
        }

        QByteArray block;
        block.resize(static_cast<int>(sizeof(quint64) + kExpectedSamplesPerFrame * sizeof(float)));
        std::memcpy(block.data(), &frame.timestamp_us, sizeof(quint64));
        std::memcpy(block.data() + sizeof(quint64), frame.samples.constData(), kExpectedSamplesPerFrame * sizeof(float));

        if (waveformFile->write(block) != block.size())
        {
            const QString filename = waveformFile->fileName();
            QMetaObject::invokeMethod(this, [this, filename]() {
                const QString message = QString(is_english_
                    ? "Failed to write waveform frame into %1"
                    : "写入波形帧失败: %1").arg(filename);
                log(message);
                appendErrorLogLine(message);
            }, Qt::QueuedConnection);
            continue;
        }

        waveform_frame_count_.fetch_add(1);
        QMetaObject::invokeMethod(this, [this]() {
            updateRecordingStatusLabel();
        }, Qt::QueuedConnection);
    }

    if (waveformFile && waveformFile->isOpen())
    {
        waveformFile->flush();
        waveformFile->close();
    }
}

void MainWindow::updateRecordingActionStates()
{
    const bool tcpConnected = tcp_wave_panel_ && tcp_wave_panel_->isConnected();
    const bool recordingSourceAvailable = is_connected_ || tcpConnected;
    const bool sessionOpen = sensors_file_ && sensors_file_->isOpen();
    const bool recordingActive = sessionOpen && !recording_paused_ && recording_thread_running_.load();
    const bool uiBusy = connection_attempt_in_progress_ || port_detection_in_progress_;
    const bool canStart = recordingSourceAvailable && !uiBusy && (!sessionOpen || recording_paused_);
    const bool canPause = !uiBusy && recordingActive;
    const bool canStop = sessionOpen && !uiBusy;

    if (start_recording_btn_)
    {
        start_recording_btn_->setEnabled(canStart);
    }
    if (pause_recording_btn_)
    {
        pause_recording_btn_->setEnabled(canPause);
    }
    if (stop_recording_btn_)
    {
        stop_recording_btn_->setEnabled(canStop);
    }
}

void MainWindow::updateConnectionStatus(bool connected)
{
    is_connected_ = connected;
    const bool inputsEnabled = !connected && !connection_attempt_in_progress_ && !port_detection_in_progress_;

    connect_btn_->setEnabled(inputsEnabled);
    cancel_connect_btn_->setEnabled(connection_attempt_in_progress_);
    disconnect_btn_->setEnabled(connected && !connection_attempt_in_progress_);
    refresh_ports_btn_->setEnabled(inputsEnabled);
    if (auto_detect_ports_btn_)
    {
        auto_detect_ports_btn_->setEnabled(inputsEnabled);
    }

    gnss_port_combo_->setEnabled(inputsEnabled);
    imu_port_combo_->setEnabled(inputsEnabled);
    ptb_port_combo_->setEnabled(inputsEnabled);
    hmp_port_combo_->setEnabled(inputsEnabled);
    lidar_port_combo_->setEnabled(inputsEnabled);
    gnss_baud_combo_->setEnabled(inputsEnabled);
    imu_baud_combo_->setEnabled(inputsEnabled);
    ptb_baud_combo_->setEnabled(inputsEnabled);
    hmp_baud_combo_->setEnabled(inputsEnabled);
    lidar_baud_combo_->setEnabled(inputsEnabled);
    if (imu_format_combo_)
    {
        imu_format_combo_->setEnabled(!connection_attempt_in_progress_ && !port_detection_in_progress_);
    }
    for (QPushButton* button : {imu_apply_btn_, imu_hi91_btn_, imu_hi92_btn_, imu_baud_115200_btn_, imu_baud_921600_btn_,
                                imu_rate_100_btn_, imu_rate_200_btn_, imu_rate_500_btn_, imu_rate_1000_btn_})
    {
        if (button)
        {
            button->setEnabled(!connection_attempt_in_progress_ && !port_detection_in_progress_);
        }
    }

    if (port_detection_in_progress_)
    {
        status_label_->setText(is_english_ ? "Detecting Ports..." : "正在识别串口...");
        status_label_->setProperty("status", "connecting");
    }
    else if (connection_attempt_in_progress_)
    {
        status_label_->setText(is_english_ ? "Connecting..." : "正在连接...");
        status_label_->setProperty("status", "connecting");
    }
    else if (connected)
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
    updateRecordingActionStates();
}

MainWindow::CollectorSnapshot MainWindow::snapshotCollectors() const
{
    std::lock_guard<std::mutex> lock(collector_mutex_);
    return {gnss_collector_, imu_collector_, ptb_collector_, hmp_collector_, lidar_collector_};
}

void MainWindow::setCollectors(CollectorSnapshot collectors)
{
    std::lock_guard<std::mutex> lock(collector_mutex_);
    gnss_collector_ = std::move(collectors.gnss);
    imu_collector_ = std::move(collectors.imu);
    ptb_collector_ = std::move(collectors.ptb);
    hmp_collector_ = std::move(collectors.hmp);
    lidar_collector_ = std::move(collectors.lidar);
}

void MainWindow::stopAllCollectors()
{
    CollectorSnapshot collectors;
    {
        std::lock_guard<std::mutex> lock(collector_mutex_);
        collectors.gnss = std::move(gnss_collector_);
        collectors.imu = std::move(imu_collector_);
        collectors.ptb = std::move(ptb_collector_);
        collectors.hmp = std::move(hmp_collector_);
        collectors.lidar = std::move(lidar_collector_);
    }

    if (collectors.gnss)
    {
        collectors.gnss->stop();
    }
    if (collectors.imu)
    {
        collectors.imu->stop();
    }
    if (collectors.ptb)
    {
        collectors.ptb->stop();
    }
    if (collectors.hmp)
    {
        collectors.hmp->stop();
    }
    if (collectors.lidar)
    {
        collectors.lidar->stop();
    }
}

bool MainWindow::shouldAbortConnectionAttempt()
{
    return cancel_connection_requested_.load();
}

void MainWindow::finishConnectionAttempt(bool connected)
{
    connection_attempt_in_progress_ = false;
    cancel_connection_requested_.store(false);
    if (!connected && sensors_file_ && sensors_file_->isOpen())
    {
        stopRecording(true);
    }
    updateConnectionStatus(connected);
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
    updateCombo(lidar_port_combo_);

    log(QString(is_english_ ? "Ports refreshed: %1 serial ports"
                            : "端口已刷新: %1 个串口")
            .arg(ports.size()));
}

void MainWindow::onAutoDetectPortsClicked()
{
    if (is_connected_ || connection_attempt_in_progress_ || port_detection_in_progress_)
    {
        return;
    }

    if (port_detection_thread_.joinable())
    {
        port_detection_thread_.join();
    }

    onRefreshPortsClicked();
    port_detection_in_progress_ = true;
    updateConnectionStatus(is_connected_);
    log(is_english_ ? "Starting automatic serial-port detection..." : "开始自动识别串口...");

    const QString imuProbeBaudText = imu_baud_combo_ ? imu_baud_combo_->currentText().trimmed() : QStringLiteral("921600");
    bool imuProbeBaudOk = false;
    const int imuProbeBaud = imuProbeBaudText.toInt(&imuProbeBaudOk);
    const int effectiveImuProbeBaud = imuProbeBaudOk && imuProbeBaud > 0 ? imuProbeBaud : 921600;
    const QString effectiveImuProbeBaudText = QString::number(effectiveImuProbeBaud);

    port_detection_thread_ = std::thread([this, effectiveImuProbeBaud, effectiveImuProbeBaudText]() {
        struct ProbeSpec
        {
            QString key;
            QString label;
            QString baud_text;
            std::function<bool(const QString&)> probe;
        };

        struct DetectionResult
        {
            QString key;
            QString port_name;
            QString baud_text;
        };

        const bool english = is_english_;
        auto postLog = [this](const QString& message) {
            QMetaObject::invokeMethod(this, [this, message]() { log(message); }, Qt::QueuedConnection);
        };
        auto finishOnUi = [this](QVector<DetectionResult> detections) {
            QMetaObject::invokeMethod(this, [this, detections = std::move(detections)]() {
                const QString selectText = is_english_ ? "-- Select --" : "-- 选择 --";
                auto applySelection = [&selectText](QComboBox* combo, const QString& value) {
                    if (!combo)
                    {
                        return;
                    }
                    const int idx = combo->findText(value);
                    if (idx >= 0)
                    {
                        combo->setCurrentIndex(idx);
                    }
                    else if (!value.isEmpty() && value != selectText)
                    {
                        combo->setEditText(value);
                    }
                };
                auto normalizePort = [&selectText](const QString& value) {
                    return (value.isEmpty() || value == selectText) ? selectText : value;
                };

                QHash<QString, QString> plannedPorts{
                    {"gnss", normalizePort(gnss_port_combo_->currentText())},
                    {"imu", normalizePort(imu_port_combo_->currentText())},
                    {"ptb", normalizePort(ptb_port_combo_->currentText())},
                    {"hmp", normalizePort(hmp_port_combo_->currentText())},
                    {"lidar", normalizePort(lidar_port_combo_->currentText())},
                };

                QHash<QString, QString> detectedBaud;
                for (const DetectionResult& detection : detections)
                {
                    const QString portName = normalizePort(detection.port_name);
                    if (portName == selectText)
                    {
                        continue;
                    }
                    if (!plannedPorts.contains(detection.key))
                    {
                        continue;
                    }
                    plannedPorts[detection.key] = portName;
                    detectedBaud[detection.key] = detection.baud_text;
                }

                QHash<QString, int> portUseCount;
                for (auto it = plannedPorts.cbegin(); it != plannedPorts.cend(); ++it)
                {
                    if (it.value() != selectText)
                    {
                        portUseCount[it.value()] += 1;
                    }
                }

                QSet<QString> duplicatePorts;
                for (auto it = portUseCount.cbegin(); it != portUseCount.cend(); ++it)
                {
                    if (it.value() > 1)
                    {
                        duplicatePorts.insert(it.key());
                    }
                }

                for (auto it = plannedPorts.begin(); it != plannedPorts.end(); ++it)
                {
                    if (duplicatePorts.contains(it.value()))
                    {
                        it.value() = selectText;
                    }
                }

                applySelection(gnss_port_combo_, plannedPorts.value("gnss", selectText));
                applySelection(imu_port_combo_, plannedPorts.value("imu", selectText));
                applySelection(ptb_port_combo_, plannedPorts.value("ptb", selectText));
                applySelection(hmp_port_combo_, plannedPorts.value("hmp", selectText));
                applySelection(lidar_port_combo_, plannedPorts.value("lidar", selectText));

                if (plannedPorts.value("gnss", selectText) != selectText && detectedBaud.contains("gnss"))
                {
                    gnss_baud_combo_->setCurrentText(detectedBaud.value("gnss"));
                }
                if (plannedPorts.value("imu", selectText) != selectText && detectedBaud.contains("imu"))
                {
                    imu_baud_combo_->setCurrentText(detectedBaud.value("imu"));
                }
                if (plannedPorts.value("ptb", selectText) != selectText && detectedBaud.contains("ptb"))
                {
                    ptb_baud_combo_->setCurrentText(detectedBaud.value("ptb"));
                }
                if (plannedPorts.value("hmp", selectText) != selectText && detectedBaud.contains("hmp"))
                {
                    hmp_baud_combo_->setCurrentText(detectedBaud.value("hmp"));
                }
                if (plannedPorts.value("lidar", selectText) != selectText && detectedBaud.contains("lidar"))
                {
                    lidar_baud_combo_->setCurrentText(detectedBaud.value("lidar"));
                }

                port_detection_in_progress_ = false;
                updateConnectionStatus(is_connected_);
            }, Qt::QueuedConnection);
        };

        auto probeCollector = [](const QString& port_name, auto&& collector, const VaporView::SerialConfig& config) {
            if (!collector->start(port_name.toStdString(), config))
            {
                return false;
            }

            const bool responded = collector->checkDeviceResponse();
            collector->stop();
            return responded;
        };

        QVector<ProbeSpec> probe_specs = {
            {"gnss", "GNSS", "115200", [](const QString& port_name) {
                const auto probeResult = VaporView::probeSerialPortForHeader(
                    port_name,
                    {QStringLiteral("115200")},
                    VaporView::SerialHeaderProbeKind::GnssPvt);
                return probeResult.matched;
            }},
            {"imu", "IMU", effectiveImuProbeBaudText, [probeCollector, effectiveImuProbeBaud](const QString& port_name) {
                auto collector = std::make_unique<VaporView::ImuCollector>();
                return probeCollector(port_name, std::move(collector), VaporView::SerialConfig::N81(effectiveImuProbeBaud));
            }},
            {"lidar", "TFA1500-L", "500000", [probeCollector](const QString& port_name) {
                auto collector = std::make_unique<VaporView::LidarCollector>();
                return probeCollector(port_name, std::move(collector), VaporView::SerialConfig::N81(500000));
            }},
            {"lidar", "TF03", "115200", [probeCollector](const QString& port_name) {
                auto collector = std::make_unique<VaporView::LidarCollector>();
                return probeCollector(port_name, std::move(collector), VaporView::SerialConfig::N81(115200));
            }},
            {"ptb", "PTB210", "9600", [probeCollector](const QString& port_name) {
                auto collector = std::make_unique<VaporView::PtbCollector>();
                return probeCollector(port_name, std::move(collector), VaporView::SerialConfig::E71(9600));
            }},
            {"hmp", "HMP3", "19200", [probeCollector](const QString& port_name) {
                auto collector = std::make_unique<VaporView::HmpCollector>();
                return probeCollector(port_name, std::move(collector), VaporView::SerialConfig::N82(19200));
            }},
        };

        QStringList port_names = getAvailablePorts();
        if (port_names.isEmpty())
        {
            postLog(english ? "Auto detect stopped: no serial ports found." : "自动识别结束：当前没有发现可用串口。");
            finishOnUi({});
            return;
        }

        QVector<DetectionResult> detections;
        postLog(QString(english ? "Auto detect: probing %1 serial ports..." : "自动识别：开始探测 %1 个串口...")
                    .arg(port_names.size()));

        for (const QString& port_name : port_names)
        {
            bool matched = false;

            for (const ProbeSpec& spec : probe_specs)
            {
                const bool already_detected = std::any_of(detections.cbegin(), detections.cend(),
                    [&spec](const DetectionResult& result) { return result.key == spec.key; });
                if (already_detected)
                {
                    continue;
                }

                postLog(QString(english ? "[Auto Detect] Probing %1 on %2 @ %3..." : "[自动识别] 正在探测 %1: %2 @ %3 ...")
                            .arg(spec.label, port_name, spec.baud_text));

                const bool responded = spec.probe(port_name);
                if (!responded)
                {
                    continue;
                }

                detections.push_back({spec.key, port_name, spec.baud_text});
                postLog(QString(english ? "[Auto Detect] Identified %1 on %2 @ %3" : "[自动识别] 已识别 %1: %2 @ %3")
                            .arg(spec.label, port_name, spec.baud_text));
                matched = true;
                break;
            }

            if (!matched)
            {
                postLog(QString(english ? "[Auto Detect] No known device signature found on %1" : "[自动识别] 未在 %1 上识别到已知设备")
                            .arg(port_name));
            }
        }

        for (const ProbeSpec& spec : probe_specs)
        {
            const bool found = std::any_of(detections.cbegin(), detections.cend(),
                [&spec](const DetectionResult& result) { return result.key == spec.key; });
            if (!found)
            {
                postLog(QString(english ? "[Auto Detect] %1 not found" : "[自动识别] 未找到 %1").arg(spec.label));
            }
        }

        postLog(QString(english ? "Auto detect finished: identified %1 device(s)." : "自动识别完成：共识别出 %1 个设备。")
                    .arg(detections.size()));
        finishOnUi(std::move(detections));
    });
}

void MainWindow::onConnectClicked()
{
    if (connection_thread_.joinable())
    {
        connection_thread_.join();
    }

    connection_attempt_in_progress_ = true;
    cancel_connection_requested_.store(false);
    updateConnectionStatus(false);

    log(is_english_ ? "Connecting..." : "正在连接...");

    current_gnss_ = VaporView::GnssData();
    current_imu_ = VaporView::ImuData();
    current_ptb_ = VaporView::PtbData();
    current_hmp_ = VaporView::HmpData();
    current_lidar_ = VaporView::LidarData();

    gnss_panel_->updateData(current_gnss_, 0);
    imu_panel_->updateData(current_imu_, 0);
    ptb_panel_->updateData(current_ptb_);
    hmp_panel_->updateData(current_hmp_);
    lidar_panel_->updateData(current_lidar_);

    gnss_panel_->updateRate(0.0);
    imu_panel_->updateRate(0.0);
    ptb_panel_->updateRate(0.0);
    hmp_panel_->updateRate(0.0);
    lidar_panel_->updateRate(0.0);

    const bool english = is_english_;
    const QString selectText = english ? "-- Select --" : "-- 选择 --";
    const QString gnssPort = gnss_port_combo_->currentText();
    const QString imuPort = imu_port_combo_->currentText();
    const QString ptbPort = ptb_port_combo_->currentText();
    const QString hmpPort = hmp_port_combo_->currentText();
    const QString lidarPort = lidar_port_combo_->currentText();
    const QString gnssBaudText = gnss_baud_combo_->currentText();
    const QString imuBaudText = imu_baud_combo_->currentText();
    const QString ptbBaudText = ptb_baud_combo_->currentText();
    const QString hmpBaudText = hmp_baud_combo_->currentText();
    const QString lidarBaudText = lidar_baud_combo_->currentText();
    const int gnssRate = parseRate(gnss_rate_combo_->currentText());
    const int imuRate = parseRate(imu_rate_combo_->currentText());
    const int ptbRate = parseRate(ptb_rate_combo_->currentText());
    const int hmpRate = parseRate(hmp_rate_combo_->currentText());
    const QString lidarRateText = lidar_rate_combo_->currentText();
    const bool skipLidarDeviceRate = isLidarRateUnspecified(lidarRateText);
    const int lidarRate = effectiveLidarSampleRate(lidarRateText);
    gnss_sample_rate_ = gnssRate;
    imu_sample_rate_ = imuRate;
    ptb_sample_rate_ = ptbRate;
    hmp_sample_rate_ = hmpRate;
    lidar_sample_rate_ = lidarRate;

    stopAllCollectors();

    connection_thread_ = std::thread([this,
                                      english,
                                      selectText,
                                      gnssPort,
                                      imuPort,
                                      ptbPort,
                                      hmpPort,
                                      lidarPort,
                                      gnssBaudText,
                                      imuBaudText,
                                      ptbBaudText,
                                      hmpBaudText,
                                      lidarBaudText,
                                      gnssRate,
                                      imuRate,
                                      ptbRate,
                                      hmpRate,
                                      lidarRate,
                                      skipLidarDeviceRate]() {
        auto postLog = [this](const QString& message) {
            QMetaObject::invokeMethod(this, [this, message]() { log(message); }, Qt::QueuedConnection);
        };
        auto finishOnUi = [this](bool connected) {
            QMetaObject::invokeMethod(this, [this, connected]() { finishConnectionAttempt(connected); }, Qt::QueuedConnection);
        };

        CollectorSnapshot collectors;
        collectors.gnss = std::make_shared<VaporView::GnssCollector>();
        collectors.imu = std::make_shared<VaporView::ImuCollector>();
        collectors.ptb = std::make_shared<VaporView::PtbCollector>();
        collectors.hmp = std::make_shared<VaporView::HmpCollector>();
        collectors.lidar = std::make_shared<VaporView::LidarCollector>();
        setCollectors(collectors);

        auto logCallback = [this](const std::string& msg) {
            const QString qmsg = QString::fromStdString(msg);
            QMetaObject::invokeMethod(this, [this, qmsg]() { log(qmsg); }, Qt::QueuedConnection);
        };
        auto cancelCallback = [this]() { return cancel_connection_requested_.load(); };

        collectors.gnss->setSampleRate(gnssRate);
        collectors.imu->setSampleRate(imuRate);
        collectors.ptb->setSampleRate(ptbRate);
        collectors.hmp->setSampleRate(hmpRate);
        collectors.lidar->setSampleRate(lidarRate);

        collectors.gnss->setLogCallback(logCallback);
        collectors.imu->setLogCallback(logCallback);
        collectors.ptb->setLogCallback(logCallback);
        collectors.hmp->setLogCallback(logCallback);
        collectors.lidar->setLogCallback(logCallback);
        collectors.gnss->setCancelCallback(cancelCallback);
        collectors.imu->setCancelCallback(cancelCallback);
        collectors.ptb->setCancelCallback(cancelCallback);
        collectors.hmp->setCancelCallback(cancelCallback);
        collectors.lidar->setCancelCallback(cancelCallback);
        collectors.imu->setRawPacketCallback([this](uint64_t hostTimestampUs, uint8_t frameTag, const uint8_t* data, size_t size) {
            if (!recording_thread_running_.load())
            {
                return;
            }

            std::lock_guard<std::mutex> lock(recording_files_mutex_);
            if (!imu_raw_file_ || !imu_raw_file_->isOpen())
            {
                return;
            }

            const ImuRawRecordHeader header{
                0x524D5549u,
                static_cast<quint32>(size),
                static_cast<quint64>(hostTimestampUs),
                static_cast<quint8>(frameTag),
                {0, 0, 0}
            };
            imu_raw_file_->write(reinterpret_cast<const char*>(&header), sizeof(header));
            imu_raw_file_->write(reinterpret_cast<const char*>(data), static_cast<qint64>(size));
        });

        int total_devices = 0;
        int connected_devices = 0;

        auto cancelAttempt = [&]() {
            stopAllCollectors();
            postLog(english ? "Connection canceled" : "连接已取消");
            finishOnUi(false);
        };
        auto abortIfRequested = [&]() {
            if (!shouldAbortConnectionAttempt())
            {
                return false;
            }
            cancelAttempt();
            return true;
        };
        auto connectCollector = [&](const QString& tag,
                                    const QString& port,
                                    const QString& baudText,
                                    auto* collector,
                                    const VaporView::SerialConfig& config,
                                    auto&& onReady) -> int {
            if (port == selectText || port.isEmpty())
            {
                postLog(QString(english ? "[%1] Skipped (not selected)" : "[%1] 跳过 (未选择)").arg(tag));
                return 0;
            }

            total_devices++;
            postLog(QString(english ? "[%1] Checking port: %2" : "[%1] 检查端口: %2").arg(tag, port));
            if (abortIfRequested()) return -1;

            postLog(QString(english ? "[%1] Port selected, connecting..." : "[%1] 已选择端口，正在连接...").arg(tag));
            if (abortIfRequested()) return -1;

            if (!collector->start(port.toStdString(), config))
            {
                postLog(QString(english ? "[%1] Failed to open port: %2" : "[%1] 打开端口失败: %2")
                            .arg(tag, QString::fromStdString(collector->getLastError())));
                return 0;
            }

            postLog(QString(english ? "[%1] Serial port opened, checking device response..." : "[%1] 串口已打开，正在检测设备响应...").arg(tag));
            if (abortIfRequested()) return -1;

            if (!collector->checkDeviceResponse())
            {
                if (abortIfRequested()) return -1;
                postLog(QString(english ? "[%1] Device not responding! Check power and cables." : "[%1] 设备无响应！请检查电源和连接线。").arg(tag));
                collector->stop();
                return 0;
            }

            postLog(QString(english ? "[%1] Device responding, connected: %2 @ %3 baud" : "[%1] 设备响应正常，连接成功: %2 @ %3 波特率")
                        .arg(tag, port, baudText));
            if (!onReady())
            {
                collector->stop();
                return 0;
            }

            connected_devices++;
            return 1;
        };

        postLog(english ? "========== Starting Connection ==========" : "========== 开始连接 ==========");
        if (abortIfRequested()) return;

        if (connectCollector("GNSS", gnssPort, gnssBaudText, collectors.gnss.get(),
                             VaporView::SerialConfig::N81(gnssBaudText.toInt()),
                             [&]() {
                                 collectors.gnss->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onGnssDataReady", Qt::QueuedConnection); });
                                 collectors.gnss->setSampleRate(gnssRate);
                                 collectors.gnss->setDeviceSampleRate(gnssRate);
                                 postLog(QString(english ? "[GNSS] Sample rate set to %1 Hz" : "[GNSS] 采样频率设置为 %1 Hz").arg(gnssRate));
                                 if (collectors.gnss->startStreaming()) return true;
                                 postLog(english ? "[GNSS] Failed to start data stream." : "[GNSS] 启动数据流失败。");
                                 return false;
                             }) < 0) return;

        if (connectCollector("IMU", imuPort, imuBaudText, collectors.imu.get(),
                             VaporView::SerialConfig::N81(imuBaudText.toInt()),
                             [&]() {
                                 const QString imuFormat = imu_format_combo_
                                     ? imu_format_combo_->currentText().trimmed().toUpper()
                                     : QStringLiteral("HI91");
                                 collectors.imu->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onImuDataReady", Qt::QueuedConnection); });
                                 collectors.imu->setSampleRate(imuRate);
                                 collectors.imu->setOutputMessageType(imuFormat.toStdString());
                                 collectors.imu->setDeviceSampleRate(imuRate);
                                 postLog(QString(english
                                                     ? "[IMU] Output format set to %1, sample rate command sent: %2 Hz"
                                                     : "[IMU] 输出格式已设为 %1，已发送采样频率指令：%2 Hz")
                                             .arg(imuFormat)
                                             .arg(imuRate));
                                 if (collectors.imu->startStreaming()) return true;
                                 postLog(english ? "[IMU] Failed to start data stream." : "[IMU] 启动数据流失败。");
                                 return false;
                             }) < 0) return;

        if (connectCollector("PTB", ptbPort, ptbBaudText, collectors.ptb.get(),
                             VaporView::SerialConfig::E71(ptbBaudText.toInt()),
                             [&]() {
                                 collectors.ptb->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onPtbDataReady", Qt::QueuedConnection); });
                                 collectors.ptb->setSampleRate(ptbRate);
                                 collectors.ptb->setDeviceSampleRate(ptbRate);
                                 postLog(QString(english ? "[PTB] Sample rate set to %1 Hz" : "[PTB] 采样频率设置为 %1 Hz").arg(ptbRate));
                                 if (collectors.ptb->startStreaming()) return true;
                                 postLog(english ? "[PTB] Failed to start data stream." : "[PTB] 启动数据流失败。");
                                 return false;
                             }) < 0) return;

        if (connectCollector("HMP", hmpPort, hmpBaudText, collectors.hmp.get(),
                             VaporView::SerialConfig::N82(hmpBaudText.toInt()),
                             [&]() {
                                 collectors.hmp->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onHmpDataReady", Qt::QueuedConnection); });
                                 collectors.hmp->setSampleRate(hmpRate);
                                 postLog(QString(english ? "[HMP] Sample rate set to %1 Hz" : "[HMP] 采样频率设置为 %1 Hz").arg(hmpRate));
                                 if (collectors.hmp->startStreaming()) return true;
                                 postLog(english ? "[HMP] Failed to start data stream." : "[HMP] 启动数据流失败。");
                                 return false;
                             }) < 0) return;

        if (connectCollector("LIDAR", lidarPort, lidarBaudText, collectors.lidar.get(),
                             VaporView::SerialConfig::N81(lidarBaudText.toInt()),
                             [&]() {
                                 collectors.lidar->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onLidarDataReady", Qt::QueuedConnection); });
                                 collectors.lidar->setSampleRate(lidarRate);
                                 if (skipLidarDeviceRate)
                                 {
                                     postLog(english ? "[Lidar] Skip output-rate command; using device default/adaptive output." : "[Lidar] 跳过输出频率下发，使用设备默认/自适应输出。");
                                 }
                                 else if (!collectors.lidar->setDeviceSampleRate(lidarRate))
                                 {
                                     postLog(QString(english ? "[Lidar] Failed to apply output rate %1 Hz, using device default." : "[Lidar] 应用 %1 Hz 输出频率失败，使用设备默认输出。").arg(lidarRate));
                                 }
                                 else
                                 {
                                     postLog(QString(english ? "[Lidar] Output rate set to %1 Hz or host-side limit updated" : "[Lidar] 输出频率已设置为 %1 Hz，或已更新主机侧限频").arg(lidarRate));
                                 }
                                 if (collectors.lidar->startStreaming()) return true;
                                 postLog(english ? "[Lidar] Failed to start data stream." : "[Lidar] 启动数据流失败。");
                                 return false;
                             }) < 0) return;

        postLog(QString(english ? "========== Connection Summary: %1/%2 devices connected ==========" : "========== 连接摘要: %1/%2 设备已连接 ==========").arg(connected_devices).arg(total_devices));
        if (connected_devices == 0)
        {
            postLog(english ? "No ports connected" : "没有端口连接成功");
            finishOnUi(false);
            return;
        }

        finishOnUi(true);
    });
}

void MainWindow::onDisconnectClicked()
{
    log(is_english_ ? "Disconnecting..." : "正在断开...");

    stopRecording(true);
    stopAllCollectors();
    if (connection_thread_.joinable())
    {
        connection_thread_.join();
    }
    finishConnectionAttempt(false);
    log(is_english_ ? "Disconnected" : "已断开");
}

void MainWindow::onCancelConnectClicked()
{
    if (!connection_attempt_in_progress_)
    {
        return;
    }

    cancel_connection_requested_.store(true);
    log(is_english_ ? "Cancel requested, stopping connection attempt..." : "已请求取消，正在停止连接流程...");
    QApplication::processEvents(QEventLoop::AllEvents);
}

void MainWindow::onGnssDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.gnss)
    {
        current_gnss_ = collectors.gnss->getLatestData();
    }
}

void MainWindow::onImuDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.imu)
    {
        current_imu_ = collectors.imu->getLatestData();
    }
}

void MainWindow::onPtbDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.ptb)
    {
        current_ptb_ = collectors.ptb->getLatestData();
    }
}

void MainWindow::onHmpDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.hmp)
    {
        current_hmp_ = collectors.hmp->getLatestData();
    }
}

void MainWindow::onLidarDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.lidar)
    {
        current_lidar_ = collectors.lidar->getLatestData();
    }
}

void MainWindow::onRefreshTimer()
{
    const CollectorSnapshot collectors = snapshotCollectors();

    const quint64 gnssTimestampUs = current_gnss_.valid ? steadyToEpochUs(current_gnss_.timestamp) : 0;
    gnss_panel_->updateData(current_gnss_, gnssTimestampUs);
    imu_panel_->updateData(current_imu_, gnssTimestampUs);
    ptb_panel_->updateData(current_ptb_);
    hmp_panel_->updateData(current_hmp_);
    lidar_panel_->updateData(current_lidar_);

    if (collectors.gnss)
    {
        const double rate = collectors.gnss->getActualRate();
        gnss_panel_->updateRate(rate);
    }
    if (collectors.imu)
    {
        const double rate = collectors.imu->getActualRate();
        imu_panel_->updateRate(rate);
    }
    if (collectors.ptb)
    {
        const double rate = collectors.ptb->getActualRate();
        ptb_panel_->updateRate(rate);
    }
    if (collectors.hmp)
    {
        const double rate = collectors.hmp->getActualRate();
        hmp_panel_->updateRate(rate);
    }
    if (collectors.lidar)
    {
        const double rate = collectors.lidar->getActualRate();
        lidar_panel_->updateRate(rate);
    }
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
    rtk_config_dialog_->setFontScale(font_scale_percent_);
    rtk_config_dialog_->setEnglish(is_english_);
    rtk_config_dialog_->show();
    rtk_config_dialog_->raise();
    rtk_config_dialog_->activateWindow();
}
