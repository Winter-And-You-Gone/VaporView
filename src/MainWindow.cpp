#include "MainWindow.h"
#include "RtkConfigDialog.h"
#include "data_collector.h"
#include "data_types.h"
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QTextStream>
#include <QStringConverter>
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
#include <QStringList>
#include <QApplication>
#include <QLayout>
#include <QIntValidator>
#include <QSerialPortInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QThread>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QVector>
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

struct TdlasTrendSeriesView
{
    QString title;
    QString subtitle;
    QVector<double> values;
};

QString recordingTimestampUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString recordingSessionFileTimestamp()
{
    return QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
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

QString jsonEscape(const QString &value)
{
    QString escaped = value;
    escaped.replace("\\", "\\\\");
    escaped.replace("\"", "\\\"");
    escaped.replace("\n", "\\n");
    escaped.replace("\r", "\\r");
    escaped.replace("\t", "\\t");
    return escaped;
}

QString csvBool(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

QString tdlasMetricSummaryText(const VaporView::TdlasData& data, bool english)
{
    QStringList lines;
    for (const VaporView::TdlasMetric& metric : data.metrics)
    {
        const QString label = english ? QString::fromStdString(metric.label_en) : QString::fromStdString(metric.label_zh);
        const QString status = metric.valid
            ? QString::number(metric.value, 'f', 3)
            : (english ? QStringLiteral("pending") : QStringLiteral("待确认"));
        QString line = QString("%1 @%2 %3: %4")
            .arg(label)
            .arg(metric.offset)
            .arg(QString::fromStdString(metric.wire_type.empty() ? std::string("---") : metric.wire_type))
            .arg(status);
        if (metric.valid && !metric.unit.empty())
        {
            line += QStringLiteral(" %1").arg(QString::fromStdString(metric.unit));
        }
        if (!metric.raw_hex.empty())
        {
            line += QStringLiteral(" {raw %1}").arg(QString::fromStdString(metric.raw_hex));
        }
        line += QStringLiteral(" [%1]").arg(QString::fromStdString(metric.confidence));
        lines << line;
    }

    if (lines.isEmpty())
    {
        return english ? "No decoded business metrics yet" : "暂无可解码业务指标";
    }
    return lines.join('\n');
}

QString tdlasWordStatsSummaryText(const VaporView::TdlasData& data, bool english)
{
    if (data.word_stats.empty())
    {
        return english ? "Awaiting word statistics" : "等待word统计";
    }

    QStringList lines;
    const int limit = std::min<int>(12, static_cast<int>(data.word_stats.size()));
    for (int i = 0; i < limit; ++i)
    {
        const VaporView::TdlasWordStat &stat = data.word_stats.at(static_cast<size_t>(i));
        lines << (english
            ? QString("w%1 @%2 = %3 range[%4,%5] unique %6 %7 {raw %8}")
                  .arg(stat.word_index, 2, 10, QChar('0'))
                  .arg(stat.offset)
                  .arg(stat.latest_value)
                  .arg(stat.min_value)
                  .arg(stat.max_value)
                  .arg(stat.unique_count)
                  .arg(stat.stable ? "stable" : "dynamic")
                  .arg(QString::fromStdString(stat.raw_hex))
            : QString("w%1 @%2 = %3 范围[%4,%5] 唯一值 %6 %7 {raw %8}")
                  .arg(stat.word_index, 2, 10, QChar('0'))
                  .arg(stat.offset)
                  .arg(stat.latest_value)
                  .arg(stat.min_value)
                  .arg(stat.max_value)
                  .arg(stat.unique_count)
                  .arg(stat.stable ? "稳定" : "变化")
                  .arg(QString::fromStdString(stat.raw_hex)));
    }

    if (data.word_stats.size() > static_cast<size_t>(limit))
    {
        lines << (english
            ? QString("... %1 more words exported in snapshot").arg(data.word_stats.size() - static_cast<size_t>(limit))
            : QString("... 其余 %1 个word已导出到快照").arg(data.word_stats.size() - static_cast<size_t>(limit)));
    }

    return lines.join('\n');
}

QString tdlasHeaderSummaryText(const VaporView::TdlasData& data, bool english)
{
    const auto macOrDash = [](const std::string& value) {
        return QString::fromStdString(value.empty() ? std::string("---") : value);
    };

    return english
        ? QString("ETH %1 -> %2 | IPv4 v%3 ihl %4 ttl %5 proto %6 | UDP len %7 checksum 0x%8")
              .arg(macOrDash(data.headers.ethernet.source.mac))
              .arg(macOrDash(data.headers.ethernet.destination.mac))
              .arg(data.headers.ipv4.version)
              .arg(data.headers.ipv4.ihl)
              .arg(data.headers.ipv4.ttl)
              .arg(data.headers.ipv4.protocol)
              .arg(data.headers.udp.length)
              .arg(QString::number(data.headers.udp.checksum, 16))
        : QString("以太网 %1 -> %2 | IPv4 v%3 ihl %4 ttl %5 协议 %6 | UDP 长度 %7 校验和 0x%8")
              .arg(macOrDash(data.headers.ethernet.source.mac))
              .arg(macOrDash(data.headers.ethernet.destination.mac))
              .arg(data.headers.ipv4.version)
              .arg(data.headers.ipv4.ihl)
              .arg(data.headers.ipv4.ttl)
              .arg(data.headers.ipv4.protocol)
              .arg(data.headers.udp.length)
              .arg(QString::number(data.headers.udp.checksum, 16));
}

QString tdlasCounterSummaryText(const VaporView::TdlasData& data, bool english)
{
    return english
        ? QString("non-IPv4 %1 | non-UDP %2 | filter drop %3 | parse ok %4 | parse fail %5")
              .arg(static_cast<qulonglong>(data.non_ipv4_packets))
              .arg(static_cast<qulonglong>(data.non_udp_packets))
              .arg(static_cast<qulonglong>(data.filter_mismatch_packets))
              .arg(static_cast<qulonglong>(data.parse_success_count))
              .arg(static_cast<qulonglong>(data.parse_failure_count))
        : QString("非IPv4 %1 | 非UDP %2 | 过滤丢弃 %3 | 解析成功 %4 | 解析失败 %5")
              .arg(static_cast<qulonglong>(data.non_ipv4_packets))
              .arg(static_cast<qulonglong>(data.non_udp_packets))
              .arg(static_cast<qulonglong>(data.filter_mismatch_packets))
              .arg(static_cast<qulonglong>(data.parse_success_count))
              .arg(static_cast<qulonglong>(data.parse_failure_count));
}

QString tdlasPayloadPreviewText(const std::string& payloadHex)
{
    const QStringList bytes = QString::fromStdString(payloadHex).split(' ', Qt::SkipEmptyParts);
    if (bytes.isEmpty())
    {
        return QStringLiteral("---");
    }

    QStringList lines;
    QStringList currentLine;
    for (int i = 0; i < bytes.size(); ++i)
    {
        currentLine << bytes.at(i);
        if (currentLine.size() == 16)
        {
            lines << currentLine.join(' ');
            currentLine.clear();
        }
    }
    if (!currentLine.isEmpty())
    {
        lines << currentLine.join(' ');
    }
    return lines.join('\n');
}

QVector<TdlasTrendSeriesView> tdlasTrendSeriesViews(const VaporView::TdlasData& data, bool english)
{
    QVector<TdlasTrendSeriesView> result;
    if (data.recent_metric_samples.empty() || data.metrics.empty())
    {
        return result;
    }

    for (const VaporView::TdlasMetric& latestMetric : data.metrics)
    {
        if (!latestMetric.valid)
        {
            continue;
        }

        TdlasTrendSeriesView series;
        series.title = english ? QString::fromStdString(latestMetric.label_en) : QString::fromStdString(latestMetric.label_zh);
        series.subtitle = QString("%1 @%2 %3")
            .arg(QString::fromStdString(latestMetric.wire_type.empty() ? std::string("---") : latestMetric.wire_type))
            .arg(latestMetric.offset)
            .arg(QString::fromStdString(latestMetric.confidence));

        for (const VaporView::TdlasMetricSample& sample : data.recent_metric_samples)
        {
            for (const VaporView::TdlasMetric& metric : sample.metrics)
            {
                if (metric.key == latestMetric.key && metric.valid)
                {
                    series.values.append(metric.value);
                    break;
                }
            }
        }

        if (!series.values.isEmpty())
        {
            result.append(series);
        }
        if (result.size() >= 2)
        {
            break;
        }
    }

    return result;
}

void rememberBaseMetric(QObject *object, const char *propertyName, int value)
{
    if (!object->property(propertyName).isValid())
    {
        object->setProperty(propertyName, value);
    }
}
}

TdlasTrendSparkline::TdlasTrendSparkline(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(64);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void TdlasTrendSparkline::setSeries(const QString &title, const QString &subtitle, const QVector<double> &values)
{
    title_ = title;
    subtitle_ = subtitle;
    values_ = values;
    update();
}

void TdlasTrendSparkline::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF frame = rect().adjusted(1.0, 1.0, -1.0, -1.0);
    painter.setPen(QColor("#C8D6E5"));
    painter.setBrush(QColor("#F8FBFD"));
    painter.drawRoundedRect(frame, 8.0, 8.0);

    painter.setPen(QColor("#274C67"));
    QFont titleFont = painter.font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 0.5);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.drawText(QRectF(frame.left() + 10.0, frame.top() + 8.0, frame.width() - 20.0, 18.0),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     title_.isEmpty() ? QStringLiteral("---") : title_);

    QFont bodyFont = painter.font();
    bodyFont.setBold(false);
    bodyFont.setPointSizeF(bodyFont.pointSizeF() - 0.5);
    painter.setFont(bodyFont);
    painter.setPen(QColor("#5A7387"));
    painter.drawText(QRectF(frame.left() + 10.0, frame.top() + 26.0, frame.width() - 20.0, 14.0),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     subtitle_);

    if (values_.size() < 2)
    {
        painter.setPen(QColor("#7D8FA0"));
        painter.drawText(QRectF(frame.left() + 10.0, frame.top() + 42.0, frame.width() - 20.0, 18.0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("..."));
        return;
    }

    QRectF plot = frame.adjusted(10.0, 44.0, -10.0, -10.0);
    if (plot.width() <= 0.0 || plot.height() <= 0.0)
    {
        return;
    }

    auto [minIt, maxIt] = std::minmax_element(values_.cbegin(), values_.cend());
    double minValue = *minIt;
    double maxValue = *maxIt;
    if (qFuzzyCompare(minValue, maxValue))
    {
        minValue -= 1.0;
        maxValue += 1.0;
    }

    painter.setPen(QColor("#D6E3EC"));
    painter.drawLine(QPointF(plot.left(), plot.bottom()), QPointF(plot.right(), plot.bottom()));
    painter.drawLine(QPointF(plot.left(), plot.top()), QPointF(plot.right(), plot.top()));

    QPolygonF polyline;
    polyline.reserve(values_.size());
    for (int i = 0; i < values_.size(); ++i)
    {
        const double xRatio = values_.size() == 1 ? 0.0 : static_cast<double>(i) / static_cast<double>(values_.size() - 1);
        const double yRatio = (values_.at(i) - minValue) / (maxValue - minValue);
        const qreal x = plot.left() + static_cast<qreal>(xRatio) * plot.width();
        const qreal y = plot.bottom() - static_cast<qreal>(yRatio) * plot.height();
        polyline << QPointF(x, y);
    }

    painter.setPen(QPen(QColor("#19A58D"), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPolyline(polyline);

    painter.setBrush(QColor("#19A58D"));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(polyline.last(), 3.5, 3.5);
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

void GnssPanel::updateData(const VaporView::GnssData& data)
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

void ImuPanel::updateData(const VaporView::ImuData& data)
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
    layout->setSpacing(6);
    layout->setContentsMargins(6, 6, 6, 6);

    rate_label_ = new QLabel("0.0 Hz", this);
    rate_label_->setObjectName("rateBadge");
    layout->addWidget(rate_label_, 0, Qt::AlignRight);

    auto *distanceLayout = new QHBoxLayout();
    distanceLayout->setSpacing(6);
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
    strengthLayout->setSpacing(6);
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

TdlasPanel::TdlasPanel(QWidget *parent)
    : QWidget(parent)
    , rate_label_(nullptr)
    , status_label_(nullptr)
    , packet_rate_label_(nullptr)
    , endpoint_label_(nullptr)
    , header_label_(nullptr)
    , counters_label_(nullptr)
    , traffic_label_(nullptr)
    , word_stats_label_(nullptr)
    , packet_label_(nullptr)
    , metrics_label_(nullptr)
    , payload_label_(nullptr)
    , warning_label_(nullptr)
    , trend_lbl_(nullptr)
    , trend_primary_(nullptr)
    , trend_secondary_(nullptr)
    , status_lbl_(nullptr)
    , packet_rate_lbl_(nullptr)
    , endpoint_lbl_(nullptr)
    , header_lbl_(nullptr)
    , counters_lbl_(nullptr)
    , traffic_lbl_(nullptr)
    , word_stats_lbl_(nullptr)
    , packet_lbl_(nullptr)
    , metrics_lbl_(nullptr)
    , payload_lbl_(nullptr)
    , trend_title_lbl_(nullptr)
    , is_english_(false)
{
    setupUi();
}

void TdlasPanel::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(6);
    layout->setContentsMargins(6, 6, 6, 6);

    rate_label_ = new QLabel(this);
    rate_label_->setObjectName("rateLabel");
    rate_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(rate_label_);

    auto createRow = [this, layout](QLabel*& title, QLabel*& value) {
        auto *row = new QHBoxLayout();
        row->setSpacing(6);
        title = new QLabel(this);
        title->setObjectName("fieldLabel");
        value = new QLabel(this);
        value->setObjectName("valueLabel");
        value->setWordWrap(true);
        row->addWidget(title, 0, Qt::AlignTop);
        row->addWidget(value, 1);
        layout->addLayout(row);
    };

    createRow(status_lbl_, status_label_);
    createRow(packet_rate_lbl_, packet_rate_label_);
    createRow(endpoint_lbl_, endpoint_label_);
    createRow(header_lbl_, header_label_);
    createRow(counters_lbl_, counters_label_);
    createRow(traffic_lbl_, traffic_label_);
    createRow(word_stats_lbl_, word_stats_label_);
    createRow(packet_lbl_, packet_label_);
    createRow(metrics_lbl_, metrics_label_);
    createRow(payload_lbl_, payload_label_);

    trend_title_lbl_ = new QLabel(this);
    trend_title_lbl_->setObjectName("fieldLabel");
    layout->addWidget(trend_title_lbl_);

    trend_primary_ = new TdlasTrendSparkline(this);
    layout->addWidget(trend_primary_);

    trend_secondary_ = new TdlasTrendSparkline(this);
    layout->addWidget(trend_secondary_);

    warning_label_ = new QLabel(this);
    warning_label_->setObjectName("statusIndicator");
    warning_label_->setWordWrap(true);
    layout->addWidget(warning_label_);
    layout->addStretch();

    setEnglish(false);
}

void TdlasPanel::updateRate(double hz)
{
    if (rate_label_)
    {
        rate_label_->setText(QString::asprintf("%.1f Hz matched", hz));
    }
}

void TdlasPanel::setEnglish(bool english)
{
    is_english_ = english;
    status_lbl_->setText(english ? "Status:" : "状态:");
    packet_rate_lbl_->setText(english ? "Rates:" : "速率:");
    endpoint_lbl_->setText(english ? "Endpoints:" : "端点:");
    header_lbl_->setText(english ? "Headers:" : "头部:");
    counters_lbl_->setText(english ? "Counters:" : "计数:");
    traffic_lbl_->setText(english ? "Traffic:" : "流量画像:");
    word_stats_lbl_->setText(english ? "Words:" : "Word统计:");
    packet_lbl_->setText(english ? "Packet:" : "数据包:");
    metrics_lbl_->setText(english ? "Metrics:" : "指标:");
    payload_lbl_->setText(english ? "Payload Preview:" : "负载预览:");
    trend_title_lbl_->setText(english ? "Recent Trends:" : "最近趋势:");
    warning_label_->setText(english
        ? "unverified mapping: business labels come from the legacy VI and still need live capture confirmation"
        : "unverified mapping：当前业务标签来自旧VI界面，仍需真实抓包确认");
}

void TdlasPanel::updateData(const VaporView::TdlasData& data)
{
    if (!data.capture_session_active && data.adapter_name.empty())
    {
        status_label_->setText(is_english_ ? "Disconnected" : "未连接");
        status_label_->setProperty("status", "disconnected");
        packet_rate_label_->setText(is_english_ ? "Total 0.0 Hz / Matched 0.0 Hz" : "总包 0.0 Hz / 匹配 0.0 Hz");
        endpoint_label_->setText("---");
        header_label_->setText("---");
        counters_label_->setText("---");
        traffic_label_->setText("---");
        word_stats_label_->setText("---");
        packet_label_->setText("---");
        metrics_label_->setText(is_english_ ? "No decoded business metrics yet" : "暂无可解码业务指标");
        payload_label_->setText("---");
        trend_primary_->setSeries(is_english_ ? "Trend A" : "趋势A",
                                  is_english_ ? "waiting for matched packets" : "等待匹配数据包",
                                  {});
        trend_secondary_->setSeries(is_english_ ? "Trend B" : "趋势B",
                                    is_english_ ? "waiting for matched packets" : "等待匹配数据包",
                                    {});
    }
    else
    {
        QString statusText;
        if (!data.capture_session_active)
        {
            statusText = is_english_ ? "Disconnected" : "未连接";
            status_label_->setProperty("status", "disconnected");
        }
        else if (data.matched_packets == 0)
        {
            statusText = is_english_ ? "Connected, waiting for matching traffic" : "已连接，等待匹配流量";
            status_label_->setProperty("status", "connecting");
        }
        else if (data.valid)
        {
            statusText = is_english_ ? "Matched packet decoded" : "已匹配并完成解码";
            status_label_->setProperty("status", "connected");
        }
        else
        {
            statusText = is_english_ ? "Matched packet observed, mapping pending" : "已匹配到数据包，映射待确认";
            status_label_->setProperty("status", "connecting");
        }
        status_label_->setText(statusText);

        packet_rate_label_->setText(QString(is_english_ ? "Total %1 Hz / Matched %2 Hz" : "总包 %1 Hz / 匹配 %2 Hz")
                                        .arg(QString::number(data.total_rate_hz, 'f', 1))
                                        .arg(QString::number(data.matched_rate_hz, 'f', 1)));

        const QString endpointText = QString("%1:%2 -> %3:%4")
            .arg(QString::fromStdString(data.headers.ipv4.source.ip.empty() ? std::string("---") : data.headers.ipv4.source.ip))
            .arg(data.headers.udp.source_port)
            .arg(QString::fromStdString(data.headers.ipv4.destination.ip.empty() ? std::string("---") : data.headers.ipv4.destination.ip))
            .arg(data.headers.udp.destination_port);
        endpoint_label_->setText(endpointText);
        header_label_->setText(tdlasHeaderSummaryText(data, is_english_));
        counters_label_->setText(tdlasCounterSummaryText(data, is_english_));
        traffic_label_->setText(QString("%1\n%2")
                                    .arg(QString::fromStdString(data.payload_signature.empty() ? std::string("---") : data.payload_signature))
                                    .arg(QString::fromStdString(data.payload_variation_summary.empty() ? std::string("---") : data.payload_variation_summary)));
        word_stats_label_->setText(tdlasWordStatsSummaryText(data, is_english_));

        packet_label_->setText(QString(is_english_ ? "Len %1, last match %2, total %3, matched %4, session %5"
                                                   : "长度 %1，最近匹配 %2，总包 %3，匹配 %4，会话 %5")
                                   .arg(data.packet_length)
                                   .arg(QString::fromStdString(data.last_match_time_utc.empty() ? std::string("---") : data.last_match_time_utc))
                                   .arg(static_cast<qulonglong>(data.total_packets))
                                   .arg(static_cast<qulonglong>(data.matched_packets))
                                   .arg(QString::fromStdString(data.adapter_name.empty() ? std::string("---") : data.adapter_name)));
        metrics_label_->setText(tdlasMetricSummaryText(data, is_english_));
        payload_label_->setText(data.payload_hex.empty() ? QStringLiteral("---") : tdlasPayloadPreviewText(data.payload_hex));

        const QVector<TdlasTrendSeriesView> trends = tdlasTrendSeriesViews(data, is_english_);
        if (!trends.isEmpty())
        {
            trend_primary_->setSeries(trends.at(0).title, trends.at(0).subtitle, trends.at(0).values);
        }
        else
        {
            trend_primary_->setSeries(is_english_ ? "Trend A" : "趋势A",
                                      is_english_ ? "no valid samples yet" : "暂无有效样本",
                                      {});
        }

        if (trends.size() > 1)
        {
            trend_secondary_->setSeries(trends.at(1).title, trends.at(1).subtitle, trends.at(1).values);
        }
        else
        {
            trend_secondary_->setSeries(is_english_ ? "Trend B" : "趋势B",
                                        is_english_ ? "no second metric yet" : "暂无第二指标",
                                        {});
        }
    }

    status_label_->style()->unpolish(status_label_);
    status_label_->style()->polish(status_label_);
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
    , tdlas_panel_(nullptr)
    , log_text_edit_(nullptr)
    , status_label_(nullptr)
    , recording_status_label_(nullptr)
    , gnss_port_combo_(nullptr)
    , imu_port_combo_(nullptr)
    , ptb_port_combo_(nullptr)
    , hmp_port_combo_(nullptr)
    , lidar_port_combo_(nullptr)
    , tdlas_adapter_combo_(nullptr)
    , gnss_baud_combo_(nullptr)
    , imu_baud_combo_(nullptr)
    , ptb_baud_combo_(nullptr)
    , hmp_baud_combo_(nullptr)
    , lidar_baud_combo_(nullptr)
    , tdlas_remote_ip_edit_(nullptr)
    , tdlas_remote_port_edit_(nullptr)
    , tdlas_local_port_edit_(nullptr)
    , connect_btn_(nullptr)
    , cancel_connect_btn_(nullptr)
    , disconnect_btn_(nullptr)
    , refresh_ports_btn_(nullptr)
    , fullscreen_menu_action_(nullptr)
    , fullscreen_toolbar_action_(nullptr)
    , lang_action_(nullptr)
    , clear_log_action_(nullptr)
    , recording_directory_action_(nullptr)
    , export_tdlas_snapshot_action_(nullptr)
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
    , lidar_group_(nullptr)
    , tdlas_group_(nullptr)
    , gnss_lbl_(nullptr)
    , imu_lbl_(nullptr)
    , ptb_lbl_(nullptr)
    , hmp_lbl_(nullptr)
    , lidar_lbl_(nullptr)
    , tdlas_lbl_(nullptr)
    , global_rate_lbl_(nullptr)
    , gnss_rate_lbl_(nullptr)
    , imu_rate_lbl_(nullptr)
    , ptb_rate_lbl_(nullptr)
    , hmp_rate_lbl_(nullptr)
    , lidar_rate_lbl_(nullptr)
    , tdlas_rate_lbl_(nullptr)
    , global_rate_combo_(nullptr)
    , gnss_rate_combo_(nullptr)
    , imu_rate_combo_(nullptr)
    , ptb_rate_combo_(nullptr)
    , hmp_rate_combo_(nullptr)
    , lidar_rate_combo_(nullptr)
    , tdlas_rate_combo_(nullptr)
    , gnss_collector_(nullptr)
    , imu_collector_(nullptr)
    , ptb_collector_(nullptr)
    , hmp_collector_(nullptr)
    , lidar_collector_(nullptr)
    , tdlas_collector_(nullptr)
    , refresh_timer_(nullptr)
    , is_fullscreen_(false)
    , is_english_(false)
    , has_inline_progress_log_(false)
    , connection_attempt_in_progress_(false)
    , cancel_connection_requested_(false)
    , recording_thread_running_(false)
    , font_scale_percent_(100)
    , base_font_point_size_(0.0)
    , base_window_size_(1280, 720)
    , base_minimum_window_size_(800, 600)
    , gnss_sample_rate_(1)
    , imu_sample_rate_(1)
    , ptb_sample_rate_(1)
    , hmp_sample_rate_(1)
    , lidar_sample_rate_(1)
    , tdlas_sample_rate_(20)
    , recording_file_(nullptr)
    , recording_directory_()
    , recording_filename_()
    , recording_entry_count_(0)
    , rtk_config_action_(nullptr)
    , rtk_config_dialog_(nullptr)
{
    const double currentPointSize = qApp->font().pointSizeF();
    base_font_point_size_ = currentPointSize > 0.0 ? currentPointSize : 10.0;

    QSettings settings("VaporView", "MainWindow");
    font_scale_percent_ = settings.value("font_scale_percent", 100).toInt();
    if (font_scale_percent_ < 85 || font_scale_percent_ > 150)
    {
        font_scale_percent_ = 100;
    }
    recording_directory_ = settings.value("recording_directory", defaultRecordingDirectory()).toString();
    if (recording_directory_.isEmpty())
    {
        recording_directory_ = defaultRecordingDirectory();
    }

    loadModernStyleSheet();
    
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
    setupCentralWidget();

    resize(base_window_size_);
    setMinimumSize(base_minimum_window_size_);

    refresh_timer_ = new QTimer(this);
    connect(refresh_timer_, &QTimer::timeout, this, &MainWindow::onRefreshTimer);
    refresh_timer_->start(100);

    if (tdlas_adapter_combo_)
    {
        const QString adapterName = settings.value("tdlas/adapter_name").toString();
        for (int i = 1; i < tdlas_adapter_combo_->count(); ++i)
        {
            if (tdlas_adapter_combo_->itemData(i).toString() == adapterName)
            {
                tdlas_adapter_combo_->setCurrentIndex(i);
                break;
            }
        }
    }
    if (tdlas_remote_ip_edit_)
    {
        tdlas_remote_ip_edit_->setText(settings.value("tdlas/remote_ip", "192.168.1.2").toString());
    }
    if (tdlas_remote_port_edit_)
    {
        tdlas_remote_port_edit_->setText(settings.value("tdlas/remote_port", "1600").toString());
    }
    if (tdlas_local_port_edit_)
    {
        tdlas_local_port_edit_->setText(settings.value("tdlas/local_port", "8080").toString());
    }
    if (tdlas_rate_combo_)
    {
        tdlas_rate_combo_->setCurrentText(settings.value("tdlas/sample_rate", QString::number(tdlas_sample_rate_)).toString());
    }

    setEnglish(false);
    applyStyleConfiguration();

    updateRecordingStatusLabel();
    updateConnectionStatus(false);
}

MainWindow::~MainWindow()
{
    cancel_connection_requested_.store(true);
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
            "QGroupBox { background-color: #ffffff; border: 1px solid #e0e0e0; border-radius: 8px; margin-top: 16px; padding: 16px 12px 12px 12px; font-size: 15px; font-weight: bold; color: #333333; }"
            "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 12px; padding: 0px 8px; background-color: #ffffff; color: #1976d2; }"
            "QLabel { color: #333333; background-color: transparent; border: none; }"
            "QComboBox { background-color: #ffffff; border: 1px solid #e0e0e0; border-radius: 6px; padding: 8px 12px; min-height: 34px; color: #333333; font-size: 14px; }"
            "QComboBox:hover { border-color: #bdbdbd; }"
            "QComboBox:focus { border-color: #1976d2; border-width: 2px; }"
            "QComboBox QAbstractItemView { background-color: #ffffff; border: 1px solid #e0e0e0; border-radius: 6px; selection-background-color: #e3f2fd; selection-color: #1976d2; padding: 4px; outline: none; }"
            "QSpinBox { background-color: #ffffff; border: 1px solid #e0e0e0; border-radius: 6px; padding: 8px 30px 8px 12px; min-height: 34px; color: #333333; font-size: 14px; }"
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
    if (percent < 85 || percent > 150 || font_scale_percent_ == percent)
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

void MainWindow::setupMenuBar()
{
    QMenu *fileMenu = menuBar()->addMenu("");

    recording_directory_action_ = new QAction(this);
    connect(recording_directory_action_, &QAction::triggered, this, &MainWindow::onChooseRecordingDirectoryClicked);
    fileMenu->addAction(recording_directory_action_);

    export_tdlas_snapshot_action_ = new QAction(this);
    connect(export_tdlas_snapshot_action_, &QAction::triggered, this, &MainWindow::onExportTdlasSnapshotClicked);
    fileMenu->addAction(export_tdlas_snapshot_action_);

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

    rtk_config_action_ = new QAction(this);
    rtk_config_action_->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
    connect(rtk_config_action_, &QAction::triggered, this, &MainWindow::onRtkConfigClicked);
    toolbar->addAction(rtk_config_action_);

    toolbar->addSeparator();

    clear_log_action_ = new QAction(this);
    connect(clear_log_action_, &QAction::triggered, this, &MainWindow::onClearLogClicked);
    toolbar->addAction(clear_log_action_);

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

void MainWindow::refreshTdlasAdapters()
{
    if (!tdlas_adapter_combo_)
    {
        return;
    }

    const QString current = tdlas_adapter_combo_->currentData().toString();
    tdlas_adapter_combo_->clear();
    tdlas_adapter_combo_->addItem(is_english_ ? "-- Select Adapter --" : "-- 选择适配器 --", QString());

    const auto adapters = VaporView::EthernetCaptureCollector::listAvailableAdapters();
    for (const VaporView::TdlasAdapterInfo& adapter : adapters)
    {
        tdlas_adapter_combo_->addItem(QString::fromStdString(adapter.display_name), QString::fromStdString(adapter.name));
    }

    if (!current.isEmpty())
    {
        for (int i = 1; i < tdlas_adapter_combo_->count(); ++i)
        {
            if (tdlas_adapter_combo_->itemData(i).toString() == current)
            {
                tdlas_adapter_combo_->setCurrentIndex(i);
                break;
            }
        }
    }
}

void MainWindow::setupConfigPanel()
{
    config_group_ = new QGroupBox(this);
    config_group_->setMinimumWidth(980);
    auto *config_layout = new QGridLayout(config_group_);
    config_layout->setVerticalSpacing(8);
    config_layout->setHorizontalSpacing(8);
    config_layout->setContentsMargins(8, 4, 8, 8);

    config_layout->setColumnStretch(0, 0);
    config_layout->setColumnStretch(1, 1);
    config_layout->setColumnStretch(2, 0);
    config_layout->setColumnStretch(3, 0);
    config_layout->setColumnStretch(4, 0);
    config_layout->setColumnStretch(5, 0);
    config_layout->setColumnStretch(6, 0);
    config_layout->setColumnMinimumWidth(0, 110);
    config_layout->setColumnMinimumWidth(1, 260);
    config_layout->setColumnMinimumWidth(2, 110);
    config_layout->setColumnMinimumWidth(3, 100);
    config_layout->setColumnMinimumWidth(4, 110);
    config_layout->setColumnMinimumWidth(5, 60);
    config_layout->setColumnMinimumWidth(6, 110);

    QStringList baudRates = {"9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600"};
    QStringList ports = getAvailablePorts();

    auto createRateCombo = [this](int maxRate = 500) {
        auto *combo = new QComboBox(this);
        const QList<int> supportedRates = {1, 2, 5, 10, 20, 50, 100, 200, 500};
        for (int rate : supportedRates)
        {
            if (rate <= maxRate)
            {
                combo->addItem(QString::number(rate));
            }
        }
        combo->setCurrentIndex(4);
        combo->setEditable(true);
        combo->setFixedHeight(30);
        combo->setFixedWidth(100);
        combo->setValidator(new QIntValidator(1, maxRate, combo));
        return combo;
    };

    auto createPortRow = [this, config_layout, &baudRates, &ports, &createRateCombo](QLabel*& lbl, QComboBox*& portCombo, QComboBox*& baudCombo, QLabel*& rateLbl, QComboBox*& rateCombo, const QString& defaultPort, const QString& defaultBaud, int row, int maxRate = 500) {
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

        rateCombo = createRateCombo(maxRate);
        config_layout->addWidget(rateCombo, row, 4, Qt::AlignVCenter);
    };

    int row = 0;

    global_rate_lbl_ = new QLabel(this);
    global_rate_lbl_->setObjectName("fieldLabel");
    global_rate_lbl_->setFixedHeight(28);
    config_layout->addWidget(global_rate_lbl_, row, 5, Qt::AlignVCenter | Qt::AlignRight);

    global_rate_combo_ = createRateCombo();
    config_layout->addWidget(global_rate_combo_, row, 6, Qt::AlignVCenter);
    connect(global_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onGlobalRateChanged);
    ++row;

#ifdef _WIN32
    createPortRow(gnss_lbl_, gnss_port_combo_, gnss_baud_combo_, gnss_rate_lbl_, gnss_rate_combo_, "COM3", "115200", row++);
    createPortRow(imu_lbl_, imu_port_combo_, imu_baud_combo_, imu_rate_lbl_, imu_rate_combo_, "COM4", "115200", row++);
    createPortRow(ptb_lbl_, ptb_port_combo_, ptb_baud_combo_, ptb_rate_lbl_, ptb_rate_combo_, "COM5", "9600", row++);
    createPortRow(hmp_lbl_, hmp_port_combo_, hmp_baud_combo_, hmp_rate_lbl_, hmp_rate_combo_, "COM6", "19200", row++);
    createPortRow(lidar_lbl_, lidar_port_combo_, lidar_baud_combo_, lidar_rate_lbl_, lidar_rate_combo_, "COM7", "115200", row++, 100);
#else
    createPortRow(gnss_lbl_, gnss_port_combo_, gnss_baud_combo_, gnss_rate_lbl_, gnss_rate_combo_, "/dev/ttyCOM3", "115200", row++);
    createPortRow(imu_lbl_, imu_port_combo_, imu_baud_combo_, imu_rate_lbl_, imu_rate_combo_, "/dev/ttyIMU", "115200", row++);
    createPortRow(ptb_lbl_, ptb_port_combo_, ptb_baud_combo_, ptb_rate_lbl_, ptb_rate_combo_, "/dev/ttyBARO", "9600", row++);
    createPortRow(hmp_lbl_, hmp_port_combo_, hmp_baud_combo_, hmp_rate_lbl_, hmp_rate_combo_, "/dev/ttyHMP", "19200", row++);
    createPortRow(lidar_lbl_, lidar_port_combo_, lidar_baud_combo_, lidar_rate_lbl_, lidar_rate_combo_, "/dev/ttyTF03", "115200", row++, 100);
#endif

    tdlas_lbl_ = new QLabel(this);
    tdlas_lbl_->setObjectName("fieldLabel");
    tdlas_lbl_->setFixedHeight(28);
    tdlas_lbl_->setFixedWidth(100);
    config_layout->addWidget(tdlas_lbl_, row, 0, Qt::AlignVCenter | Qt::AlignLeft);

    tdlas_adapter_combo_ = new QComboBox(this);
    tdlas_adapter_combo_->setFixedHeight(30);
    tdlas_adapter_combo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    tdlas_adapter_combo_->setMaxVisibleItems(15);
    config_layout->addWidget(tdlas_adapter_combo_, row, 1, Qt::AlignVCenter);

    tdlas_remote_ip_edit_ = new QLineEdit(this);
    tdlas_remote_ip_edit_->setFixedHeight(30);
    tdlas_remote_ip_edit_->setClearButtonEnabled(true);
    config_layout->addWidget(tdlas_remote_ip_edit_, row, 2, Qt::AlignVCenter);

    tdlas_remote_port_edit_ = new QLineEdit(this);
    tdlas_remote_port_edit_->setFixedHeight(30);
    tdlas_remote_port_edit_->setClearButtonEnabled(true);
    tdlas_remote_port_edit_->setValidator(new QIntValidator(0, 65535, tdlas_remote_port_edit_));
    config_layout->addWidget(tdlas_remote_port_edit_, row, 3, Qt::AlignVCenter);

    tdlas_local_port_edit_ = new QLineEdit(this);
    tdlas_local_port_edit_->setFixedHeight(30);
    tdlas_local_port_edit_->setClearButtonEnabled(true);
    tdlas_local_port_edit_->setValidator(new QIntValidator(0, 65535, tdlas_local_port_edit_));
    config_layout->addWidget(tdlas_local_port_edit_, row, 4, Qt::AlignVCenter);

    tdlas_rate_lbl_ = new QLabel(this);
    tdlas_rate_lbl_->setObjectName("fieldLabel");
    tdlas_rate_lbl_->setFixedHeight(28);
    config_layout->addWidget(tdlas_rate_lbl_, row, 5, Qt::AlignVCenter | Qt::AlignRight);

    tdlas_rate_combo_ = createRateCombo();
    config_layout->addWidget(tdlas_rate_combo_, row, 6, Qt::AlignVCenter);
    refreshTdlasAdapters();

    connect(gnss_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onGnssRateChanged);
    connect(imu_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onImuRateChanged);
    connect(ptb_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onPtbRateChanged);
    connect(hmp_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onHmpRateChanged);
    connect(lidar_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onLidarRateChanged);
    connect(tdlas_rate_combo_, &QComboBox::currentTextChanged, this, &MainWindow::onTdlasRateChanged);

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

    tdlas_group_ = new QGroupBox(this);
    tdlas_group_->setObjectName("sensorGroupBox");
    auto *tdlas_layout = new QVBoxLayout(tdlas_group_);
    tdlas_layout->setContentsMargins(2, 2, 2, 2);
    tdlas_panel_ = new TdlasPanel(this);
    tdlas_layout->addWidget(tdlas_panel_);
    data_layout->addWidget(tdlas_group_);

    auto *env_group = new QGroupBox(this);
    env_group->setObjectName("sensorGroupBox");
    auto *env_layout = new QVBoxLayout(env_group);
    env_layout->setContentsMargins(2, 2, 2, 2);
    env_layout->setSpacing(2);

    lidar_panel_ = new LidarPanel(this);
    env_layout->addWidget(lidar_panel_);

    ptb_panel_ = new PtbPanel(this);
    env_layout->addWidget(ptb_panel_);

    hmp_panel_ = new HmpPanel(this);
    env_layout->addWidget(hmp_panel_);

    data_layout->addWidget(env_group);
    env_group_ = env_group;

    lidar_group_ = nullptr;
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
    recording_directory_action_->setText(english ? "Recording Folder..." : "记录目录...");
    export_tdlas_snapshot_action_->setText(english ? "Export TDLAS Verification Snapshot..." : "导出TDLAS验证快照...");
    exit_action_->setText(english ? "E&xit" : "退出(&X)");

    menuBar()->actions().at(1)->menu()->setTitle(english ? "&View" : "视图(&V)");
    fullscreen_menu_action_->setText(english ? "&Fullscreen" : "全屏(&F)");

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
    cancel_connect_btn_->setText(english ? "Cancel" : "取消");
    disconnect_btn_->setText(english ? "Disconnect" : "断开");
    clear_log_action_->setText(english ? "Clear" : "清空");
    fullscreen_toolbar_action_->setText(english ? "Fullscreen" : "全屏");
    rtk_config_action_->setText(english ? "RTK Config" : "RTK配置");

    status_label_->setText(english ? "Ready" : "就绪");

    config_group_->setTitle(english ? "Serial Port Configuration" : "串口配置");
    data_group_->setTitle(english ? "Sensor Data" : "传感器数据");
    log_group_->setTitle(english ? "Log" : "日志");

    gnss_group_->setTitle(english ? "GNSS / RTK" : "GNSS / RTK");
    imu_group_->setTitle(english ? "IMU" : "IMU");
    tdlas_group_->setTitle(english ? "TDLAS Ethernet" : "TDLAS 以太网");
    env_group_->setTitle(english ? "Environment / Range" : "环境与测距");

    gnss_lbl_->setText(english ? "GNSS:" : "GNSS:");
    imu_lbl_->setText(english ? "IMU:" : "IMU:");
    ptb_lbl_->setText(english ? "PTB210:" : "PTB210:");
    hmp_lbl_->setText(english ? "HMP3:" : "HMP3:");
    lidar_lbl_->setText(english ? "TF03:" : "TF03:");
    tdlas_lbl_->setText(english ? "TDLAS:" : "TDLAS:");

    global_rate_lbl_->setText(english ? "Global Rate:" : "统一频率:");
    gnss_rate_lbl_->setText(english ? "Rate:" : "频率:");
    imu_rate_lbl_->setText(english ? "Rate:" : "频率:");
    ptb_rate_lbl_->setText(english ? "Rate:" : "频率:");
    hmp_rate_lbl_->setText(english ? "Rate:" : "频率:");
    lidar_rate_lbl_->setText(english ? "Rate:" : "频率:");
    tdlas_rate_lbl_->setText(english ? "Rate:" : "频率:");

    tdlas_adapter_combo_->setToolTip(english ? "Capture adapter" : "抓包适配器");
    tdlas_remote_ip_edit_->setPlaceholderText(english ? "Remote IP" : "远端IP");
    tdlas_remote_port_edit_->setPlaceholderText(english ? "Remote Port" : "远端端口");
    tdlas_local_port_edit_->setPlaceholderText(english ? "Local Port" : "本地端口");
    tdlas_remote_ip_edit_->setToolTip(english ? "Leave empty to match any remote IP" : "留空表示匹配任意远端IP");
    tdlas_remote_port_edit_->setToolTip(english ? "0 or empty means any remote port" : "0或留空表示匹配任意远端端口");
    tdlas_local_port_edit_->setToolTip(english ? "0 or empty means any local port" : "0或留空表示匹配任意本地端口");
    refreshTdlasAdapters();

    gnss_panel_->setEnglish(english);
    imu_panel_->setEnglish(english);
    ptb_panel_->setEnglish(english);
    hmp_panel_->setEnglish(english);
    lidar_panel_->setEnglish(english);
    tdlas_panel_->setEnglish(english);

    if (rtk_config_dialog_)
    {
        rtk_config_dialog_->setEnglish(english);
    }

    updateRecordingStatusLabel();
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
    tdlas_sample_rate_ = rate;
    
    gnss_rate_combo_->blockSignals(true);
    imu_rate_combo_->blockSignals(true);
    ptb_rate_combo_->blockSignals(true);
    hmp_rate_combo_->blockSignals(true);
    tdlas_rate_combo_->blockSignals(true);
    
    gnss_rate_combo_->setCurrentText(text);
    imu_rate_combo_->setCurrentText(text);
    ptb_rate_combo_->setCurrentText(text);
    hmp_rate_combo_->setCurrentText(text);
    tdlas_rate_combo_->setCurrentText(text);
    
    gnss_rate_combo_->blockSignals(false);
    imu_rate_combo_->blockSignals(false);
    ptb_rate_combo_->blockSignals(false);
    hmp_rate_combo_->blockSignals(false);
    tdlas_rate_combo_->blockSignals(false);
    
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
        collectors.lidar->setDeviceSampleRate(lidarRate);
    }
    if (collectors.tdlas && collectors.tdlas->isRunning())
    {
        collectors.tdlas->setSampleRate(rate);
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
    lidar_sample_rate_ = std::min(parseRate(text), 100);
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.lidar)
    {
        collectors.lidar->setSampleRate(lidar_sample_rate_);
        if (collectors.lidar->isRunning())
        {
            collectors.lidar->setDeviceSampleRate(lidar_sample_rate_);
        }
    }
    log(QString(is_english_ ? "TF03 sample rate set to %1 Hz" : "TF03采样频率已设置为 %1 Hz").arg(lidar_sample_rate_));
}

void MainWindow::onTdlasRateChanged(const QString& text)
{
    tdlas_sample_rate_ = parseRate(text);
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.tdlas)
    {
        collectors.tdlas->setSampleRate(tdlas_sample_rate_);
    }
    log(QString(is_english_ ? "TDLAS sample rate set to %1 Hz" : "TDLAS采样频率已设置为 %1 Hz").arg(tdlas_sample_rate_));
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
        collectors.lidar->setDeviceSampleRate(lidarRate);
    }
    if (collectors.tdlas && collectors.tdlas->isRunning())
    {
        collectors.tdlas->setSampleRate(rate);
    }

    gnss_rate_combo_->blockSignals(true);
    imu_rate_combo_->blockSignals(true);
    ptb_rate_combo_->blockSignals(true);
    hmp_rate_combo_->blockSignals(true);
    lidar_rate_combo_->blockSignals(true);
    tdlas_rate_combo_->blockSignals(true);

    gnss_rate_combo_->setCurrentText(QString::number(rate));
    imu_rate_combo_->setCurrentText(QString::number(rate));
    ptb_rate_combo_->setCurrentText(QString::number(rate));
    hmp_rate_combo_->setCurrentText(QString::number(rate));
    lidar_rate_combo_->setCurrentText(QString::number(std::min(rate, 100)));
    tdlas_rate_combo_->setCurrentText(QString::number(rate));

    gnss_rate_combo_->blockSignals(false);
    imu_rate_combo_->blockSignals(false);
    ptb_rate_combo_->blockSignals(false);
    hmp_rate_combo_->blockSignals(false);
    lidar_rate_combo_->blockSignals(false);
    tdlas_rate_combo_->blockSignals(false);

    gnss_sample_rate_ = rate;
    imu_sample_rate_ = rate;
    ptb_sample_rate_ = rate;
    hmp_sample_rate_ = rate;
    lidar_sample_rate_ = std::min(rate, 100);
    tdlas_sample_rate_ = rate;

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
}

void MainWindow::updateRecordingStatusLabel()
{
    if (!recording_status_label_)
    {
        return;
    }

    if (recording_file_ && recording_file_->isOpen())
    {
        const QFileInfo info(recording_filename_);
        recording_status_label_->setText(
            QString(is_english_ ? "Recording: %1 rows | %2" : "记录中: %1 行 | %2")
                .arg(static_cast<qlonglong>(recording_entry_count_.load()))
                .arg(info.fileName()));
        recording_status_label_->setProperty("status", "connected");
    }
    else
    {
        recording_status_label_->setText(is_english_ ? "Recording: Off" : "记录: 未记录");
        recording_status_label_->setProperty("status", "disconnected");
    }

    recording_status_label_->style()->unpolish(recording_status_label_);
    recording_status_label_->style()->polish(recording_status_label_);
}

QString MainWindow::defaultRecordingDirectory() const
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i)
    {
        if (QFileInfo::exists(dir.filePath("CMakeLists.txt")) && QFileInfo::exists(dir.filePath("README.md")))
        {
            return dir.filePath("records");
        }
        if (!dir.cdUp())
        {
            break;
        }
    }

    return QDir(QCoreApplication::applicationDirPath()).filePath("records");
}

bool MainWindow::startRecordingSession()
{
    stopRecording(false);

    QString recordsPath = recording_directory_.trimmed();
    if (recordsPath.isEmpty())
    {
        recordsPath = defaultRecordingDirectory();
        recording_directory_ = recordsPath;
    }

    QDir recordsDir(recordsPath);
    if (!recordsDir.exists() && !recordsDir.mkpath("."))
    {
        QMessageBox::warning(
            this,
            is_english_ ? "Error" : "错误",
            is_english_ ? "Failed to create recording directory" : "无法创建记录目录");
        return false;
    }

    QString baseName = recordingSessionFileTimestamp();
    QString filename = recordsDir.filePath(baseName + ".csv");
    int suffix = 1;
    while (QFileInfo::exists(filename))
    {
        filename = recordsDir.filePath(QString("%1_%2.csv").arg(baseName).arg(suffix++));
    }

    auto file = std::make_unique<QFile>(filename);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        QMessageBox::warning(
            this,
            is_english_ ? "Error" : "错误",
            is_english_ ? "Failed to open recording file for writing" : "无法打开记录文件进行写入");
        return false;
    }

    QFile *filePtr = file.get();
    recording_filename_ = filename;
    recording_entry_count_.store(0);
    recording_file_ = std::move(file);
    writeRecordingHeader();
    recording_thread_running_.store(true);
    recording_thread_ = std::thread([this, filePtr]() {
        auto nextTick = std::chrono::steady_clock::now();
        while (recording_thread_running_.load())
        {
            const auto tickTime = std::chrono::steady_clock::now();
            const CollectorSnapshot collectors = snapshotCollectors();
            const VaporView::GnssData gnssSample = collectors.gnss ? collectors.gnss->getLatestData() : VaporView::GnssData();
            const VaporView::ImuData imuSample = collectors.imu ? collectors.imu->getLatestData() : VaporView::ImuData();
            const VaporView::PtbData ptbSample = collectors.ptb ? collectors.ptb->getLatestData() : VaporView::PtbData();
            const VaporView::HmpData hmpSample = collectors.hmp ? collectors.hmp->getLatestData() : VaporView::HmpData();
            const VaporView::LidarData lidarSample = collectors.lidar ? collectors.lidar->getLatestData() : VaporView::LidarData();
            const VaporView::TdlasData tdlasSample = collectors.tdlas ? collectors.tdlas->getLatestData() : VaporView::TdlasData();

            QStringList row;
            row.reserve(74);
            row << recordingTimestampUtc();

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
                    << QString::number(gnssSample.latitude)
                    << QString::number(gnssSample.longitude)
                    << QString::number(gnssSample.altitude)
                    << QString::number(gnssSample.vel_north)
                    << QString::number(gnssSample.vel_east)
                    << QString::number(gnssSample.vel_down)
                    << QString::number(gnssSample.vel_ground)
                    << QString::number(gnssSample.heading)
                    << QString::number(gnssSample.heading_pitch)
                    << QString::number(gnssSample.heading_length)
                    << QString::fromStdString(gnssSample.heading_type)
                    << QString::number(gnssSample.heading_trackedsvs)
                    << QString::number(gnssSample.heading_solnsvs)
                    << QString::number(gnssSample.sigma_lat)
                    << QString::number(gnssSample.sigma_lon)
                    << QString::number(gnssSample.sigma_alt)
                    << QString::fromStdString(gnssSample.position_status)
                    << QString::number(gnssSample.num_satellites_used)
                    << QString::number(gnssSample.num_satellites_tracked)
                    << QString::number(gnssSample.gdop)
                    << QString::number(gnssSample.pdop)
                    << QString::number(gnssSample.hdop)
                    << QString::number(gnssSample.htdop)
                    << QString::number(gnssSample.tdop)
                    << QString::number(gnssSample.diff_age)
                    << QString::number(gnssSample.undulation)
                    << QString::number(gnssSample.elevation_cutoff)
                    << QString::fromStdString(gnssSample.raw_sentence);
                appendBool(gnssSample.valid);
                row << QString::fromStdString(gnssSample.error_message);
            }
            else
            {
                appendEmptyColumns(30);
            }

            if (isFresh(collectors.imu.get(), imuSample))
            {
                row
                    << QString::number(imuSample.acceleration[0])
                    << QString::number(imuSample.acceleration[1])
                    << QString::number(imuSample.acceleration[2])
                    << QString::number(imuSample.gyroscope[0])
                    << QString::number(imuSample.gyroscope[1])
                    << QString::number(imuSample.gyroscope[2])
                    << QString::number(imuSample.rpy[0])
                    << QString::number(imuSample.rpy[1])
                    << QString::number(imuSample.rpy[2])
                    << QString::number(imuSample.quaternion[0])
                    << QString::number(imuSample.quaternion[1])
                    << QString::number(imuSample.quaternion[2])
                    << QString::number(imuSample.quaternion[3])
                    << QString::number(imuSample.temperature)
                    << QString::number(imuSample.air_pressure)
                    << QString::number(static_cast<qulonglong>(imuSample.system_time_us))
                    << QString::number(imuSample.system_time_ms)
                    << csvBool(imuSample.from_hi83)
                    << QString::fromStdString(imuSample.raw_sentence);
                appendBool(imuSample.valid);
                row << QString::fromStdString(imuSample.error_message);
            }
            else
            {
                appendEmptyColumns(21);
            }

            if (isFresh(collectors.ptb.get(), ptbSample))
            {
                row << QString::number(ptbSample.pressure_hpa);
                appendBool(ptbSample.valid);
                row << QString::fromStdString(ptbSample.error_message);
            }
            else
            {
                appendEmptyColumns(3);
            }

            if (isFresh(collectors.hmp.get(), hmpSample))
            {
                row
                    << QString::number(hmpSample.humidity)
                    << QString::number(hmpSample.temperature);
                appendBool(hmpSample.valid);
                row << QString::fromStdString(hmpSample.error_message);
            }
            else
            {
                appendEmptyColumns(4);
            }

            if (isFresh(collectors.lidar.get(), lidarSample))
            {
                row
                    << QString::number(lidarSample.distance_m)
                    << QString::number(lidarSample.signal_strength);
                appendBool(lidarSample.valid);
                row << QString::fromStdString(lidarSample.error_message);
            }
            else
            {
                appendEmptyColumns(4);
            }

            if (isFresh(collectors.tdlas.get(), tdlasSample))
            {
                row
                    << QString::fromStdString(tdlasSample.adapter_name)
                    << QString::fromStdString(tdlasSample.headers.ipv4.source.ip)
                    << QString::fromStdString(tdlasSample.headers.ipv4.destination.ip)
                    << QString::number(tdlasSample.headers.udp.source_port)
                    << QString::number(tdlasSample.headers.udp.destination_port)
                    << QString::number(tdlasSample.packet_length);
                appendBool(tdlasSample.valid);
                row << QString::fromStdString(tdlasSample.error_message)
                    << QString::fromStdString(tdlasSample.payload_hex)
                    << tdlasMetricsJsonForRecording(tdlasSample);
            }
            else
            {
                appendEmptyColumns(10);
            }

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

            recording_entry_count_.fetch_add(1);
            QMetaObject::invokeMethod(this, [this]() {
                updateRecordingStatusLabel();
            }, Qt::QueuedConnection);

            nextTick += std::chrono::milliseconds(50);
            std::this_thread::sleep_until(nextTick);
        }
    });
    updateRecordingStatusLabel();
    log(QString(is_english_ ? "Started automatic session recording: %1" : "已开始自动会话记录: %1").arg(filename));
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

void MainWindow::stopRecording(bool announce)
{
    recording_thread_running_.store(false);
    if (recording_thread_.joinable())
    {
        recording_thread_.join();
    }

    if (!recording_file_ || !recording_file_->isOpen())
    {
        recording_file_.reset();
        recording_filename_.clear();
        recording_entry_count_.store(0);
        updateRecordingStatusLabel();
        return;
    }

    recording_file_->flush();
    recording_file_->close();

    const QString filename = recording_filename_;
    const qint64 entryCount = recording_entry_count_.load();
    const bool removeEmpty = (entryCount == 0);

    recording_file_.reset();
    recording_filename_.clear();
    recording_entry_count_.store(0);

    if (removeEmpty)
    {
        QFile::remove(filename);
    }

    updateRecordingStatusLabel();

    if (announce)
    {
        if (removeEmpty)
        {
            log(QString(is_english_
                ? "Stopped automatic recording with no samples, removed empty file: %1"
                : "自动记录已停止，因无采样数据已删除空文件: %1").arg(filename));
        }
        else
        {
            log(QString(is_english_
                ? "Stopped automatic recording (%1 rows): %2"
                : "自动记录已停止（%1 行）: %2").arg(entryCount).arg(filename));
        }
    }
}

void MainWindow::writeRecordingHeader()
{
    if (!recording_file_ || !recording_file_->isOpen())
    {
        return;
    }

    QTextStream out(recording_file_.get());
    out.setEncoding(QStringConverter::Utf8);
    out.setGenerateByteOrderMark(true);
    out
        << "timestamp_utc,"
        << "gnss_latitude,gnss_longitude,gnss_altitude,gnss_vel_north,gnss_vel_east,gnss_vel_down,gnss_vel_ground,gnss_heading,gnss_heading_pitch,gnss_heading_length,gnss_heading_type,gnss_heading_trackedsvs,gnss_heading_solnsvs,gnss_sigma_lat,gnss_sigma_lon,gnss_sigma_alt,gnss_position_status,gnss_num_satellites_used,gnss_num_satellites_tracked,gnss_gdop,gnss_pdop,gnss_hdop,gnss_htdop,gnss_tdop,gnss_diff_age,gnss_undulation,gnss_elevation_cutoff,gnss_raw_sentence,gnss_valid,gnss_error_message,"
        << "imu_acc_x,imu_acc_y,imu_acc_z,imu_gyr_x,imu_gyr_y,imu_gyr_z,imu_roll,imu_pitch,imu_yaw,imu_quat_w,imu_quat_x,imu_quat_y,imu_quat_z,imu_temperature,imu_air_pressure,imu_system_time_us,imu_system_time_ms,imu_from_hi83,imu_raw_sentence,imu_valid,imu_error_message,"
        << "ptb_pressure_hpa,ptb_valid,ptb_error_message,"
        << "hmp_humidity,hmp_temperature,hmp_valid,hmp_error_message,"
        << "tf03_distance_m,tf03_signal_strength,tf03_valid,tf03_error_message,"
        << "tdlas_adapter,tdlas_src_ip,tdlas_dst_ip,tdlas_src_port,tdlas_dst_port,tdlas_packet_len,tdlas_valid,tdlas_error_message,tdlas_payload_hex,tdlas_metrics_json\n";
    out.flush();
}

QString MainWindow::tdlasMetricsJsonForRecording(const VaporView::TdlasData& data) const
{
    return QString::fromStdString(data.metrics_json);
}

QString MainWindow::tdlasSnapshotJson(const VaporView::TdlasData& data) const
{
    QString json;
    QTextStream out(&json);
    out.setEncoding(QStringConverter::Utf8);

    auto writeMetricArray = [&out](const std::vector<VaporView::TdlasMetric> &metrics) {
        out << "[\n";
        for (size_t i = 0; i < metrics.size(); ++i)
        {
            const VaporView::TdlasMetric &metric = metrics[i];
            out << "      {\n"
                << "        \"key\": \"" << jsonEscape(QString::fromStdString(metric.key)) << "\",\n"
                << "        \"label_zh\": \"" << jsonEscape(QString::fromStdString(metric.label_zh)) << "\",\n"
                << "        \"label_en\": \"" << jsonEscape(QString::fromStdString(metric.label_en)) << "\",\n"
                << "        \"unit\": \"" << jsonEscape(QString::fromStdString(metric.unit)) << "\",\n"
                << "        \"wire_type\": \"" << jsonEscape(QString::fromStdString(metric.wire_type)) << "\",\n"
                << "        \"raw_hex\": \"" << jsonEscape(QString::fromStdString(metric.raw_hex)) << "\",\n"
                << "        \"offset\": " << metric.offset << ",\n"
                << "        \"value\": " << metric.value << ",\n"
                << "        \"valid\": " << (metric.valid ? "true" : "false") << ",\n"
                << "        \"confidence\": \"" << jsonEscape(QString::fromStdString(metric.confidence)) << "\"\n"
                << "      }";
            if (i + 1 < metrics.size())
            {
                out << ',';
            }
            out << '\n';
        }
        out << "    ]";
    };

    out << "{\n"
        << "  \"exported_at_utc\": \"" << jsonEscape(recordingTimestampUtc()) << "\",\n"
        << "  \"adapter_name\": \"" << jsonEscape(QString::fromStdString(data.adapter_name)) << "\",\n"
        << "  \"capture_session_active\": " << (data.capture_session_active ? "true" : "false") << ",\n"
        << "  \"matched\": " << (data.matched ? "true" : "false") << ",\n"
        << "  \"valid\": " << (data.valid ? "true" : "false") << ",\n"
        << "  \"mapping_unverified\": " << (data.mapping_unverified ? "true" : "false") << ",\n"
        << "  \"last_match_time_utc\": \"" << jsonEscape(QString::fromStdString(data.last_match_time_utc)) << "\",\n"
        << "  \"error_message\": \"" << jsonEscape(QString::fromStdString(data.error_message)) << "\",\n"
        << "  \"payload_hex\": \"" << jsonEscape(QString::fromStdString(data.payload_hex)) << "\",\n"
        << "  \"payload_signature\": \"" << jsonEscape(QString::fromStdString(data.payload_signature)) << "\",\n"
        << "  \"payload_variation_summary\": \"" << jsonEscape(QString::fromStdString(data.payload_variation_summary)) << "\",\n"
        << "  \"packet_length\": " << data.packet_length << ",\n"
        << "  \"rates\": {\n"
        << "    \"total_rate_hz\": " << data.total_rate_hz << ",\n"
        << "    \"matched_rate_hz\": " << data.matched_rate_hz << "\n"
        << "  },\n"
        << "  \"counters\": {\n"
        << "    \"total_packets\": " << data.total_packets << ",\n"
        << "    \"matched_packets\": " << data.matched_packets << ",\n"
        << "    \"non_ipv4_packets\": " << data.non_ipv4_packets << ",\n"
        << "    \"non_udp_packets\": " << data.non_udp_packets << ",\n"
        << "    \"filter_mismatch_packets\": " << data.filter_mismatch_packets << ",\n"
        << "    \"parse_success_count\": " << data.parse_success_count << ",\n"
        << "    \"parse_failure_count\": " << data.parse_failure_count << "\n"
        << "  },\n"
        << "  \"headers\": {\n"
        << "    \"ethernet\": {\n"
        << "      \"source_mac\": \"" << jsonEscape(QString::fromStdString(data.headers.ethernet.source.mac)) << "\",\n"
        << "      \"destination_mac\": \"" << jsonEscape(QString::fromStdString(data.headers.ethernet.destination.mac)) << "\",\n"
        << "      \"ether_type\": " << data.headers.ethernet.ether_type << "\n"
        << "    },\n"
        << "    \"ipv4\": {\n"
        << "      \"source_ip\": \"" << jsonEscape(QString::fromStdString(data.headers.ipv4.source.ip)) << "\",\n"
        << "      \"destination_ip\": \"" << jsonEscape(QString::fromStdString(data.headers.ipv4.destination.ip)) << "\",\n"
        << "      \"version\": " << data.headers.ipv4.version << ",\n"
        << "      \"ihl\": " << data.headers.ipv4.ihl << ",\n"
        << "      \"ttl\": " << data.headers.ipv4.ttl << ",\n"
        << "      \"protocol\": " << data.headers.ipv4.protocol << ",\n"
        << "      \"total_length\": " << data.headers.ipv4.total_length << "\n"
        << "    },\n"
        << "    \"udp\": {\n"
        << "      \"source_port\": " << data.headers.udp.source_port << ",\n"
        << "      \"destination_port\": " << data.headers.udp.destination_port << ",\n"
        << "      \"length\": " << data.headers.udp.length << ",\n"
        << "      \"checksum\": " << data.headers.udp.checksum << "\n"
        << "    }\n"
        << "  },\n"
        << "  \"word_stats\": [\n";
    for (size_t i = 0; i < data.word_stats.size(); ++i)
    {
        const VaporView::TdlasWordStat &stat = data.word_stats[i];
        out << "    {\n"
            << "      \"word_index\": " << stat.word_index << ",\n"
            << "      \"offset\": " << stat.offset << ",\n"
            << "      \"raw_hex\": \"" << jsonEscape(QString::fromStdString(stat.raw_hex)) << "\",\n"
            << "      \"latest_value\": " << stat.latest_value << ",\n"
            << "      \"min_value\": " << stat.min_value << ",\n"
            << "      \"max_value\": " << stat.max_value << ",\n"
            << "      \"unique_count\": " << stat.unique_count << ",\n"
            << "      \"stable\": " << (stat.stable ? "true" : "false") << "\n"
            << "    }";
        if (i + 1 < data.word_stats.size())
        {
            out << ',';
        }
        out << '\n';
    }
    out << "  ],\n"
        << "  \"current_metrics\": ";
    writeMetricArray(data.metrics);
    out << ",\n"
        << "  \"recent_metric_samples\": [\n";

    for (size_t i = 0; i < data.recent_metric_samples.size(); ++i)
    {
        const VaporView::TdlasMetricSample &sample = data.recent_metric_samples[i];
        out << "    {\n"
            << "      \"timestamp_utc\": \"" << jsonEscape(QString::fromStdString(sample.timestamp_utc)) << "\",\n"
            << "      \"metrics\": ";
        writeMetricArray(sample.metrics);
        out << "\n    }";
        if (i + 1 < data.recent_metric_samples.size())
        {
            out << ',';
        }
        out << '\n';
    }

    out << "  ]\n"
        << "}\n";
    return json;
}

void MainWindow::onExportTdlasSnapshotClicked()
{
    if (!current_tdlas_.capture_session_active && current_tdlas_.adapter_name.empty())
    {
        QMessageBox::information(this,
                                 is_english_ ? "No TDLAS Data" : "无TDLAS数据",
                                 is_english_ ? "No TDLAS verification data is available yet." : "当前还没有可导出的 TDLAS 验证数据。");
        return;
    }

    const QString initialDir = recording_directory_.isEmpty() ? defaultRecordingDirectory() : recording_directory_;
    QDir().mkpath(initialDir);
    const QString defaultName = QDir(initialDir).filePath(QString("tdlas_snapshot_%1.json").arg(recordingSessionFileTimestamp()));
    const QString filename = QFileDialog::getSaveFileName(
        this,
        is_english_ ? "Export TDLAS Verification Snapshot" : "导出TDLAS验证快照",
        defaultName,
        is_english_ ? "JSON Files (*.json)" : "JSON 文件 (*.json)");

    if (filename.isEmpty())
    {
        return;
    }

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        QMessageBox::warning(this,
                             is_english_ ? "Export Failed" : "导出失败",
                             QString(is_english_ ? "Failed to write snapshot: %1" : "写入快照失败: %1").arg(file.errorString()));
        return;
    }

    const QString snapshotJson = tdlasSnapshotJson(current_tdlas_);
    file.write(snapshotJson.toUtf8());
    file.close();

    log(QString(is_english_ ? "Exported TDLAS verification snapshot: %1" : "已导出TDLAS验证快照: %1").arg(filename));
}

void MainWindow::updateConnectionStatus(bool connected)
{
    const bool inputsEnabled = !connected && !connection_attempt_in_progress_;

    connect_btn_->setEnabled(inputsEnabled);
    cancel_connect_btn_->setEnabled(connection_attempt_in_progress_);
    disconnect_btn_->setEnabled(connected && !connection_attempt_in_progress_);
    refresh_ports_btn_->setEnabled(inputsEnabled);

    gnss_port_combo_->setEnabled(inputsEnabled);
    imu_port_combo_->setEnabled(inputsEnabled);
    ptb_port_combo_->setEnabled(inputsEnabled);
    hmp_port_combo_->setEnabled(inputsEnabled);
    lidar_port_combo_->setEnabled(inputsEnabled);
    tdlas_adapter_combo_->setEnabled(inputsEnabled);
    gnss_baud_combo_->setEnabled(inputsEnabled);
    imu_baud_combo_->setEnabled(inputsEnabled);
    ptb_baud_combo_->setEnabled(inputsEnabled);
    hmp_baud_combo_->setEnabled(inputsEnabled);
    lidar_baud_combo_->setEnabled(inputsEnabled);
    tdlas_remote_ip_edit_->setEnabled(inputsEnabled);
    tdlas_remote_port_edit_->setEnabled(inputsEnabled);
    tdlas_local_port_edit_->setEnabled(inputsEnabled);
    tdlas_rate_combo_->setEnabled(inputsEnabled);

    if (connection_attempt_in_progress_)
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
}

MainWindow::CollectorSnapshot MainWindow::snapshotCollectors() const
{
    std::lock_guard<std::mutex> lock(collector_mutex_);
    return {gnss_collector_, imu_collector_, ptb_collector_, hmp_collector_, lidar_collector_, tdlas_collector_};
}

void MainWindow::setCollectors(CollectorSnapshot collectors)
{
    std::lock_guard<std::mutex> lock(collector_mutex_);
    gnss_collector_ = std::move(collectors.gnss);
    imu_collector_ = std::move(collectors.imu);
    ptb_collector_ = std::move(collectors.ptb);
    hmp_collector_ = std::move(collectors.hmp);
    lidar_collector_ = std::move(collectors.lidar);
    tdlas_collector_ = std::move(collectors.tdlas);
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
        collectors.tdlas = std::move(tdlas_collector_);
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
    if (collectors.tdlas)
    {
        collectors.tdlas->stop();
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
    if (connected)
    {
        if (!startRecordingSession())
        {
            log(is_english_ ? "Automatic session recording failed to start" : "自动会话记录启动失败");
        }
    }
    else
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
    refreshTdlasAdapters();

    log(QString(is_english_ ? "Ports refreshed: %1 serial ports, %2 capture adapters"
                            : "端口已刷新: %1 个串口，%2 个抓包适配器")
            .arg(ports.size())
            .arg(std::max(0, tdlas_adapter_combo_->count() - 1)));
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
    current_tdlas_ = VaporView::TdlasData();

    gnss_panel_->updateData(current_gnss_);
    imu_panel_->updateData(current_imu_);
    ptb_panel_->updateData(current_ptb_);
    hmp_panel_->updateData(current_hmp_);
    lidar_panel_->updateData(current_lidar_);
    tdlas_panel_->updateData(current_tdlas_);

    gnss_panel_->updateRate(0.0);
    imu_panel_->updateRate(0.0);
    ptb_panel_->updateRate(0.0);
    hmp_panel_->updateRate(0.0);
    lidar_panel_->updateRate(0.0);
    tdlas_panel_->updateRate(0.0);

    const bool english = is_english_;
    const QString selectText = english ? "-- Select --" : "-- 选择 --";
    const QString gnssPort = gnss_port_combo_->currentText();
    const QString imuPort = imu_port_combo_->currentText();
    const QString ptbPort = ptb_port_combo_->currentText();
    const QString hmpPort = hmp_port_combo_->currentText();
    const QString lidarPort = lidar_port_combo_->currentText();
    const QString tdlasAdapterName = tdlas_adapter_combo_->currentData().toString();
    const QString tdlasAdapterLabel = tdlas_adapter_combo_->currentText();
    const QString tdlasRemoteIp = tdlas_remote_ip_edit_->text().trimmed();
    const QString tdlasRemotePortText = tdlas_remote_port_edit_->text().trimmed();
    const QString tdlasLocalPortText = tdlas_local_port_edit_->text().trimmed();
    const QString gnssBaudText = gnss_baud_combo_->currentText();
    const QString imuBaudText = imu_baud_combo_->currentText();
    const QString ptbBaudText = ptb_baud_combo_->currentText();
    const QString hmpBaudText = hmp_baud_combo_->currentText();
    const QString lidarBaudText = lidar_baud_combo_->currentText();
    const int gnssRate = parseRate(gnss_rate_combo_->currentText());
    const int imuRate = parseRate(imu_rate_combo_->currentText());
    const int ptbRate = parseRate(ptb_rate_combo_->currentText());
    const int hmpRate = parseRate(hmp_rate_combo_->currentText());
    const int lidarRate = std::min(parseRate(lidar_rate_combo_->currentText()), 100);
    const int tdlasRate = parseRate(tdlas_rate_combo_->currentText());
    gnss_sample_rate_ = gnssRate;
    imu_sample_rate_ = imuRate;
    ptb_sample_rate_ = ptbRate;
    hmp_sample_rate_ = hmpRate;
    lidar_sample_rate_ = lidarRate;
    tdlas_sample_rate_ = tdlasRate;

    QSettings settings("VaporView", "MainWindow");
    settings.setValue("tdlas/adapter_name", tdlasAdapterName);
    settings.setValue("tdlas/remote_ip", tdlasRemoteIp);
    settings.setValue("tdlas/remote_port", tdlasRemotePortText);
    settings.setValue("tdlas/local_port", tdlasLocalPortText);
    settings.setValue("tdlas/sample_rate", QString::number(tdlasRate));

    stopAllCollectors();

    connection_thread_ = std::thread([this,
                                      english,
                                      selectText,
                                      gnssPort,
                                      imuPort,
                                      ptbPort,
                                      hmpPort,
                                      lidarPort,
                                      tdlasAdapterName,
                                      tdlasAdapterLabel,
                                      tdlasRemoteIp,
                                      tdlasRemotePortText,
                                      tdlasLocalPortText,
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
                                      tdlasRate]() {
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
        collectors.tdlas = std::make_shared<VaporView::EthernetCaptureCollector>();
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
        collectors.tdlas->setSampleRate(tdlasRate);

        collectors.gnss->setLogCallback(logCallback);
        collectors.imu->setLogCallback(logCallback);
        collectors.ptb->setLogCallback(logCallback);
        collectors.hmp->setLogCallback(logCallback);
        collectors.lidar->setLogCallback(logCallback);
        collectors.tdlas->setLogCallback(logCallback);
        collectors.gnss->setCancelCallback(cancelCallback);
        collectors.imu->setCancelCallback(cancelCallback);
        collectors.ptb->setCancelCallback(cancelCallback);
        collectors.hmp->setCancelCallback(cancelCallback);
        collectors.lidar->setCancelCallback(cancelCallback);
        collectors.tdlas->setCancelCallback(cancelCallback);

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
                                 collectors.imu->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onImuDataReady", Qt::QueuedConnection); });
                                 collectors.imu->setSampleRate(imuRate);
                                 collectors.imu->setDeviceSampleRate(imuRate);
                                 postLog(QString(english ? "[IMU] Sample rate set to %1 Hz" : "[IMU] 采样频率设置为 %1 Hz").arg(imuRate));
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

        if (connectCollector("TF03", lidarPort, lidarBaudText, collectors.lidar.get(),
                             VaporView::SerialConfig::N81(lidarBaudText.toInt()),
                             [&]() {
                                 collectors.lidar->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onLidarDataReady", Qt::QueuedConnection); });
                                 collectors.lidar->setSampleRate(lidarRate);
                                 if (!collectors.lidar->setDeviceSampleRate(lidarRate))
                                 {
                                     postLog(QString(english ? "[TF03] Failed to apply frame rate %1 Hz, using device default." : "[TF03] 应用 %1 Hz 输出频率失败，使用设备默认频率。").arg(lidarRate));
                                 }
                                 else
                                 {
                                     postLog(QString(english ? "[TF03] Frame rate set to %1 Hz" : "[TF03] 输出频率设置为 %1 Hz").arg(lidarRate));
                                 }
                                 if (collectors.lidar->startStreaming()) return true;
                                 postLog(english ? "[TF03] Failed to start data stream." : "[TF03] 启动数据流失败。");
                                 return false;
                             }) < 0) return;

        if (!tdlasAdapterName.isEmpty())
        {
            total_devices++;
            postLog(QString(english ? "[TDLAS] Opening capture adapter: %1" : "[TDLAS] 正在打开抓包适配器: %1").arg(tdlasAdapterLabel));
            if (abortIfRequested()) return;

            collectors.tdlas->setDataCallback([this]() { QMetaObject::invokeMethod(this, "onTdlasDataReady", Qt::QueuedConnection); });
            collectors.tdlas->setSampleRate(tdlasRate);

            VaporView::TdlasCaptureConfig captureConfig;
            captureConfig.adapter_name = tdlasAdapterName.toStdString();
            captureConfig.remote_ip = tdlasRemoteIp.toStdString();
            captureConfig.remote_port = static_cast<uint16_t>(tdlasRemotePortText.isEmpty() ? 0 : tdlasRemotePortText.toUShort());
            captureConfig.local_port = static_cast<uint16_t>(tdlasLocalPortText.isEmpty() ? 0 : tdlasLocalPortText.toUShort());

            if (!collectors.tdlas->start(captureConfig))
            {
                postLog(QString(english ? "[TDLAS] Failed to start capture: %1" : "[TDLAS] 启动抓包失败: %1")
                            .arg(QString::fromStdString(collectors.tdlas->getLastError())));
            }
            else
            {
                connected_devices++;
                postLog(english ? "[TDLAS] Capture started, waiting for matching traffic." : "[TDLAS] 抓包已启动，等待匹配流量。");
            }
        }
        else
        {
            postLog(english ? "[TDLAS] Skipped (adapter not selected)" : "[TDLAS] 跳过 (未选择适配器)");
        }

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

    stopRecording(false);
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

void MainWindow::onTdlasDataReady()
{
    const CollectorSnapshot collectors = snapshotCollectors();
    if (collectors.tdlas)
    {
        current_tdlas_ = collectors.tdlas->getLatestData();
    }
}

void MainWindow::onRefreshTimer()
{
    const CollectorSnapshot collectors = snapshotCollectors();

    gnss_panel_->updateData(current_gnss_);
    imu_panel_->updateData(current_imu_);
    ptb_panel_->updateData(current_ptb_);
    hmp_panel_->updateData(current_hmp_);
    lidar_panel_->updateData(current_lidar_);
    tdlas_panel_->updateData(current_tdlas_);

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
    if (collectors.tdlas)
    {
        const double rate = collectors.tdlas->getActualRate();
        tdlas_panel_->updateRate(rate);
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

