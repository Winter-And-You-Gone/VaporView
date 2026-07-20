#include "ground/session/SessionViewerPages.h"

#include "ground/session/SessionIndex.h"
#include "ground/session/SessionViewerWidgets.h"
#include "ground/widgets/CustomTitleBar.h"
#include "ground/widgets/RangeSelectionAxisWidget.h"
#include "shared/theme/AppTheme.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPalette>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QTableView>
#include <QTimeZone>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace VaporView::Ground::SessionUi
{
using VaporView::AppThemeColor;
using VaporView::appThemeColor;

namespace
{

constexpr int kMinimumVisibleCsvRows = 5;

QString fixedTextField(const QString& text, int width, Qt::Alignment alignment = Qt::AlignRight)
{
    const int targetWidth = std::max(width, static_cast<int>(text.size()));
    return alignment == Qt::AlignLeft
        ? text.leftJustified(targetWidth, QLatin1Char(' '))
        : text.rightJustified(targetWidth, QLatin1Char(' '));
}

QString fixedIntegerField(qulonglong value, int width)
{
    return fixedTextField(QString::number(value), width);
}

QString fixedDecimalField(double value, int decimals, int width)
{
    return std::isfinite(value)
        ? fixedTextField(QString::number(value, 'f', decimals), width)
        : fixedTextField(QStringLiteral("---"), width);
}

QString fixedSignedDecimalField(double value, int decimals, int width)
{
    if (!std::isfinite(value))
    {
        return fixedTextField(QStringLiteral("---"), width, Qt::AlignLeft);
    }
    QString text = QString::number(value, 'f', decimals);
    if (!text.startsWith(QLatin1Char('-')) && !text.startsWith(QLatin1Char('+')))
    {
        text.prepend(QLatin1Char(' '));
    }
    return text.leftJustified(std::max(width, static_cast<int>(text.size())), QLatin1Char(' '));
}

QString formatTimestampUs(quint64 timestampUs)
{
    if (timestampUs == 0)
    {
        return QObject::tr("N/A");
    }
    const qint64 millis = static_cast<qint64>(timestampUs / 1000ULL);
    const int micros = static_cast<int>(timestampUs % 1000000ULL);
    return QStringLiteral("%1.%2")
        .arg(QDateTime::fromMSecsSinceEpoch(millis, QTimeZone::UTC)
                 .toLocalTime()
                 .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
        .arg(micros, 6, 10, QChar('0'));
}

QString formatSignedDeltaMs(qint64 deltaUs)
{
    const double deltaMs = static_cast<double>(deltaUs) / 1000.0;
    return QStringLiteral("%1%2 ms")
        .arg(deltaMs >= 0.0 ? QStringLiteral("+") : QString())
        .arg(QString::number(deltaMs, 'f', 3));
}

QString formatOptionalValue(double value, int decimals, int width, const QString& unit)
{
    const QString number = std::isfinite(value)
        ? QString::number(value, 'f', decimals).rightJustified(width, QLatin1Char(' '))
        : QStringLiteral("---").rightJustified(width, QLatin1Char(' '));
    return QStringLiteral("%1 %2").arg(number, unit);
}

}  // namespace

QString formatSessionMeasuredRateText(
    const QVector<quint64>& timestampsUs,
    int metadataRateHz,
    const QString& metadataMode,
    bool english)
{
    const double measuredRateHz = Session::measuredRateHz(timestampsUs);
    if (measuredRateHz > 0.0)
    {
        return QStringLiteral("%1 Hz").arg(QString::number(
            measuredRateHz,
            'f',
            measuredRateHz >= 10.0 ? 2 : 3));
    }
    const int validCount = static_cast<int>(std::count_if(
        timestampsUs.cbegin(), timestampsUs.cend(),
        [](quint64 timestampUs) { return timestampUs != 0; }));
    if (validCount == 1)
    {
        return english ? QStringLiteral("Single frame") : QStringLiteral("仅 1 条数据");
    }
    if (metadataMode == QStringLiteral("per_frame"))
    {
        return english ? QStringLiteral("Per-frame") : QStringLiteral("逐帧导出");
    }
    return metadataRateHz > 0
        ? QStringLiteral("%1 Hz").arg(metadataRateHz)
        : QStringLiteral("---");
}

SessionOverviewWidget::SessionOverviewWidget(QWidget *parent)
    : QWidget(parent)
    , session_path_edit_(new QLineEdit(this))
    , choose_session_btn_(new QPushButton(this))
    , reload_btn_(new QPushButton(this))
    , trajectory_view_btn_(new QPushButton(this))
    , raw_data_parser_btn_(new QPushButton(this))
    , clear_view_btn_(new QPushButton(this))
    , status_label_(new QLabel(this))
    , summary_group_(new QGroupBox(this))
    , summary_layout_(new QGridLayout(summary_group_))
{
    setObjectName(QStringLiteral("sessionOverviewWidget"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *controlLayout = new QGridLayout();
    controlLayout->setHorizontalSpacing(8);
    controlLayout->setVerticalSpacing(4);
    auto *pathTitle = new QLabel(tr("Session:"), this);
    pathTitle->setObjectName(QStringLiteral("fieldLabel"));
    controlLayout->addWidget(pathTitle, 0, 0);
    session_path_edit_->setReadOnly(true);
    controlLayout->addWidget(session_path_edit_, 0, 1);
    controlLayout->addWidget(choose_session_btn_, 0, 2);
    controlLayout->addWidget(reload_btn_, 0, 3);
    controlLayout->addWidget(trajectory_view_btn_, 0, 4);
    controlLayout->addWidget(raw_data_parser_btn_, 0, 5);
    controlLayout->addWidget(clear_view_btn_, 0, 6);
    status_label_->setWordWrap(true);
    status_label_->setFocusPolicy(Qt::StrongFocus);
    controlLayout->addWidget(status_label_, 1, 0, 1, 7);
    layout->addLayout(controlLayout);

    summary_group_->setObjectName(QStringLiteral("sensorGroupBox"));
    summary_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    summary_layout_->setContentsMargins(8, 28, 8, 8);
    summary_layout_->setHorizontalSpacing(8);
    summary_layout_->setVerticalSpacing(4);
    auto createSummaryRow = [this](QLabel*& title, QLabel*& value) {
        title = new QLabel(summary_group_);
        title->setObjectName(QStringLiteral("fieldLabel"));
        title->setMinimumWidth(64);
        title->setMaximumWidth(156);
        title->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        value = new QLabel(QStringLiteral("---"), summary_group_);
        value->setObjectName(QStringLiteral("valueLabel"));
        value->setMinimumWidth(120);
        value->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        value->setWordWrap(false);
    };
    createSummaryRow(session_name_title_, session_name_value_);
    createSummaryRow(start_time_title_, start_time_value_);
    createSummaryRow(end_time_title_, end_time_value_);
    createSummaryRow(duration_title_, duration_value_);
    createSummaryRow(sensor_export_rate_title_, sensor_export_rate_value_);
    createSummaryRow(sensor_rows_title_, sensor_rows_value_);
    createSummaryRow(waveform_export_rate_title_, waveform_export_rate_value_);
    createSummaryRow(waveform_files_title_, waveform_files_value_);
    createSummaryRow(waveform_frames_title_, waveform_frames_value_);
    layout->addWidget(summary_group_);

    connect(choose_session_btn_, &QPushButton::clicked, this, &SessionOverviewWidget::chooseSessionRequested);
    connect(reload_btn_, &QPushButton::clicked, this, &SessionOverviewWidget::reloadRequested);
    connect(trajectory_view_btn_, &QPushButton::clicked, this, &SessionOverviewWidget::trajectoryRequested);
    connect(raw_data_parser_btn_, &QPushButton::clicked, this, &SessionOverviewWidget::rawDataParserRequested);
    connect(clear_view_btn_, &QPushButton::clicked, this, &SessionOverviewWidget::clearRequested);
    setEnglish(false);
    setTrajectoryAvailable(false);
    relayoutSummaryFields();
}

void SessionOverviewWidget::setEnglish(bool english)
{
    choose_session_btn_->setText(english ? QStringLiteral("Open Data...") : QStringLiteral("打开数据..."));
    reload_btn_->setText(english ? QStringLiteral("Reload") : QStringLiteral("重新加载"));
    trajectory_view_btn_->setText(english ? QStringLiteral("View Trajectory") : QStringLiteral("轨迹查看"));
    raw_data_parser_btn_->setText(english ? QStringLiteral("Raw Data Parser...") : QStringLiteral("原始数据解析..."));
    clear_view_btn_->setText(english ? QStringLiteral("Clear Page") : QStringLiteral("清空页面"));
    summary_group_->setTitle(english ? QStringLiteral("Data Summary") : QStringLiteral("数据概览"));
    session_name_title_->setText(english ? QStringLiteral("Session:") : QStringLiteral("会话:"));
    start_time_title_->setText(english ? QStringLiteral("Start:") : QStringLiteral("开始时间:"));
    end_time_title_->setText(english ? QStringLiteral("End:") : QStringLiteral("结束时间:"));
    duration_title_->setText(english ? QStringLiteral("Duration:") : QStringLiteral("记录时间:"));
    sensor_export_rate_title_->setText(english ? QStringLiteral("CSV Rate:") : QStringLiteral("设备CSV文件记录频率:"));
    sensor_rows_title_->setText(english ? QStringLiteral("Sensor Rows:") : QStringLiteral("传感器行数:"));
    waveform_export_rate_title_->setText(english ? QStringLiteral("Wave Rate:") : QStringLiteral("波形记录频率:"));
    waveform_files_title_->setText(english ? QStringLiteral("Wave Files:") : QStringLiteral("波形文件数:"));
    waveform_frames_title_->setText(english ? QStringLiteral("Wave Frames:") : QStringLiteral("波形帧数:"));
    relayoutSummaryFields();
}

void SessionOverviewWidget::setSessionPath(const QString& path)
{
    session_path_edit_->setText(path);
}

void SessionOverviewWidget::clearSessionPath()
{
    session_path_edit_->clear();
}

void SessionOverviewWidget::setStatusText(const QString& text)
{
    status_label_->setText(text);
}

QString SessionOverviewWidget::statusText() const
{
    return status_label_->text();
}

void SessionOverviewWidget::setStatusToolTip(const QString& text)
{
    status_label_->setToolTip(text);
}

void SessionOverviewWidget::focusStatus()
{
    status_label_->setFocus(Qt::OtherFocusReason);
}

void SessionOverviewWidget::setControlsEnabled(bool enabled)
{
    controls_enabled_ = enabled;
    choose_session_btn_->setEnabled(enabled);
    reload_btn_->setEnabled(enabled);
    raw_data_parser_btn_->setEnabled(enabled);
    clear_view_btn_->setEnabled(enabled);
    trajectory_view_btn_->setEnabled(enabled && trajectory_available_);
}

void SessionOverviewWidget::setTrajectoryAvailable(bool available)
{
    trajectory_available_ = available;
    trajectory_view_btn_->setEnabled(controls_enabled_ && available);
}

void SessionOverviewWidget::setSummary(const SessionOverviewSummary& summary)
{
    session_name_value_->setText(summary.sessionName);
    start_time_value_->setText(summary.startTime);
    end_time_value_->setText(summary.endTime);
    duration_value_->setText(summary.duration);
    sensor_export_rate_value_->setText(summary.sensorRate);
    sensor_rows_value_->setText(summary.sensorRows);
    waveform_export_rate_value_->setText(summary.waveformRate);
    waveform_files_value_->setText(summary.waveformFiles);
    waveform_frames_value_->setText(summary.waveformFrames);
}

void SessionOverviewWidget::relayoutSummaryFields()
{
    while (summary_layout_->count() > 0)
    {
        delete summary_layout_->takeAt(0);
    }

    const QVector<QPair<QLabel*, QLabel*>> longFields = {
        {session_name_title_, session_name_value_},
        {start_time_title_, start_time_value_},
        {end_time_title_, end_time_value_},
    };
    const QVector<QPair<QLabel*, QLabel*>> shortFields = {
        {duration_title_, duration_value_},
        {sensor_export_rate_title_, sensor_export_rate_value_},
        {sensor_rows_title_, sensor_rows_value_},
        {waveform_export_rate_title_, waveform_export_rate_value_},
        {waveform_files_title_, waveform_files_value_},
        {waveform_frames_title_, waveform_frames_value_},
    };
    const int availableWidth = std::max({240, summary_group_->width(), summary_group_->contentsRect().width()});
    for (int column = 0; column < 12; ++column)
    {
        summary_layout_->setColumnStretch(column, 0);
        summary_layout_->setColumnMinimumWidth(column, 0);
    }
    auto addFieldPair = [this](const QPair<QLabel*, QLabel*>& field, int row, int pairColumn) {
        summary_layout_->addWidget(field.first, row, pairColumn * 2);
        summary_layout_->addWidget(field.second, row, pairColumn * 2 + 1);
        summary_layout_->setColumnStretch(pairColumn * 2 + 1, 1);
    };
    if (availableWidth >= 1720)
    {
        for (int index = 0; index < longFields.size(); ++index) addFieldPair(longFields.at(index), 0, index);
        for (int index = 0; index < shortFields.size(); ++index) addFieldPair(shortFields.at(index), 1, index);
        return;
    }
    if (availableWidth >= 1280)
    {
        for (int index = 0; index < longFields.size(); ++index) addFieldPair(longFields.at(index), 0, index);
        for (int index = 0; index < shortFields.size(); ++index) addFieldPair(shortFields.at(index), 1 + index / 3, index % 3);
        return;
    }
    if (availableWidth >= 980)
    {
        for (int index = 0; index < longFields.size(); ++index) addFieldPair(longFields.at(index), 0, index);
        for (int index = 0; index < shortFields.size(); ++index) addFieldPair(shortFields.at(index), 1 + index / 2, index % 2);
        return;
    }
    const QVector<QPair<QLabel*, QLabel*>> allFields = longFields + shortFields;
    const int pairColumns = availableWidth >= 640 ? 2 : 1;
    for (int index = 0; index < allFields.size(); ++index)
    {
        addFieldPair(allFields.at(index), index / pairColumns, index % pairColumns);
    }
}

void SessionOverviewWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayoutSummaryFields();
}

SessionWaveformWidget::SessionWaveformWidget(QWidget *parent)
    : QGroupBox(parent)
{
    setObjectName(QStringLiteral("sensorGroupBox"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 30, 10, 10);
    layout->setSpacing(6);
    auto *frameLayout = new QGridLayout();
    frameLayout->setHorizontalSpacing(8);
    frameLayout->setVerticalSpacing(4);
    frame_title_ = new QLabel(this);
    frame_title_->setObjectName(QStringLiteral("fieldLabel"));
    frameLayout->addWidget(frame_title_, 0, 0);
    frame_slider_ = new QSlider(Qt::Horizontal, this);
    frame_slider_->setEnabled(false);
    frame_slider_->setTracking(false);
    frameLayout->addWidget(frame_slider_, 0, 1);
    frame_spin_ = new QSpinBox(this);
    frame_spin_->setRange(0, 0);
    frame_spin_->setEnabled(false);
    frameLayout->addWidget(frame_spin_, 0, 2);
    frame_total_label_ = new QLabel(QStringLiteral("---"), this);
    frame_total_label_->setFont(numericFontFrom(frame_total_label_->font()));
    frame_total_label_->setFixedWidth(QFontMetrics(frame_total_label_->font()).horizontalAdvance(QStringLiteral("/ 999999999")) + 8);
    frameLayout->addWidget(frame_total_label_, 0, 3);
    frame_info_label_ = new QLabel(this);
    frame_info_label_->setFont(numericFontFrom(frame_info_label_->font()));
    frame_info_label_->setWordWrap(true);
    frameLayout->addWidget(frame_info_label_, 1, 0, 1, 4);
    layout->addLayout(frameLayout);

    waveform_plot_title_ = new QLabel(this);
    waveform_plot_title_->setObjectName(QStringLiteral("fieldLabel"));
    layout->addWidget(waveform_plot_title_);
    waveform_plot_ = createSessionWavePlotWidget(this);
    waveform_plot_->setObjectName(QStringLiteral("sessionViewerWaveformPlot"));
    layout->addWidget(waveform_plot_, 1);

    auto *peakHeaderLayout = new QHBoxLayout();
    peakHeaderLayout->setContentsMargins(0, 0, 0, 0);
    peakHeaderLayout->setSpacing(8);
    waveform_peak_plot_title_ = new QLabel(this);
    waveform_peak_plot_title_->setObjectName(QStringLiteral("fieldLabel"));
    peakHeaderLayout->addWidget(waveform_peak_plot_title_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    auto *rangeAxis = new RangeSelectionAxisWidget(this);
    rangeAxis->setCompactMode(true);
    rangeAxis->setMinimumWidth(240);
    peakHeaderLayout->addWidget(rangeAxis, 1, Qt::AlignVCenter);
    waveform_frame_filter_btn_ = new QPushButton(this);
    peakHeaderLayout->addWidget(waveform_frame_filter_btn_, 0, Qt::AlignVCenter | Qt::AlignRight);
    waveform_peak_filter_btn_ = new QPushButton(this);
    peakHeaderLayout->addWidget(waveform_peak_filter_btn_, 0, Qt::AlignVCenter | Qt::AlignRight);
    waveform_peak_mode_btn_ = new QPushButton(this);
    peakHeaderLayout->addWidget(waveform_peak_mode_btn_, 0, Qt::AlignVCenter | Qt::AlignRight);
    layout->addLayout(peakHeaderLayout);
    waveform_peak_plot_ = createSessionPeakPlotWidget(this);
    waveform_peak_plot_->setObjectName(QStringLiteral("sessionViewerPeakPlot"));
    layout->addWidget(waveform_peak_plot_, 1);

    temperature_plot_title_ = new QLabel(this);
    temperature_plot_title_->setObjectName(QStringLiteral("fieldLabel"));
    layout->addWidget(temperature_plot_title_);
    temperature_plot_ = createSingleSeriesTrendPlotWidget(
        appThemeColor(AppThemeColor::PlotSeriesTemperature, false),
        QStringLiteral("没有温度趋势数据"), QStringLiteral("°C"), this);
    temperature_plot_->setObjectName(QStringLiteral("sessionViewerTemperaturePlot"));
    layout->addWidget(temperature_plot_);
    humidity_plot_title_ = new QLabel(this);
    humidity_plot_title_->setObjectName(QStringLiteral("fieldLabel"));
    layout->addWidget(humidity_plot_title_);
    humidity_plot_ = createSingleSeriesTrendPlotWidget(
        appThemeColor(AppThemeColor::PlotSeriesHumidity, false),
        QStringLiteral("没有湿度趋势数据"), QStringLiteral("%RH"), this);
    humidity_plot_->setObjectName(QStringLiteral("sessionViewerHumidityPlot"));
    layout->addWidget(humidity_plot_);
    pressure_plot_title_ = new QLabel(this);
    pressure_plot_title_->setObjectName(QStringLiteral("fieldLabel"));
    layout->addWidget(pressure_plot_title_);
    pressure_plot_ = createSingleSeriesTrendPlotWidget(
        appThemeColor(AppThemeColor::PlotSeriesPressure, false),
        QStringLiteral("没有气压趋势数据"), QStringLiteral("hPa"), this);
    pressure_plot_->setObjectName(QStringLiteral("sessionViewerPressurePlot"));
    layout->addWidget(pressure_plot_);
    environment_info_label_ = new QLabel(this);
    environment_info_label_->setFont(numericFontFrom(environment_info_label_->font()));
    environment_info_label_->setWordWrap(true);
    environment_info_label_->setObjectName(QStringLiteral("fieldLabel"));
    layout->addWidget(environment_info_label_);

    connect(frame_slider_, &QSlider::sliderMoved, this, &SessionWaveformWidget::frameSliderMoved);
    connect(frame_slider_, &QSlider::valueChanged, this, &SessionWaveformWidget::frameSliderChanged);
    connect(frame_spin_, &QSpinBox::valueChanged, this, &SessionWaveformWidget::frameSpinChanged);
    connect(waveform_frame_filter_btn_, &QPushButton::clicked, this, &SessionWaveformWidget::frameFilterRequested);
    connect(waveform_peak_filter_btn_, &QPushButton::clicked, this, &SessionWaveformWidget::peakFilterRequested);
    connect(waveform_peak_mode_btn_, &QPushButton::clicked, this, &SessionWaveformWidget::plotModeRequested);
    waveform_peak_plot_->setViewChangedCallback([this, rangeAxis](int totalCount, int startIndex, int visibleCount) {
        rangeAxis->setRange(totalCount, startIndex, visibleCount);
        emit visibleRangeChanged(startIndex, visibleCount);
    });
    rangeAxis->setRangeChangedCallback([this](int startIndex, int visibleCount) {
        waveform_peak_plot_->setViewRange(startIndex, visibleCount);
    });
    setEnglish(false);
    setPlotMode(true);
}

void SessionWaveformWidget::setEnglish(bool english)
{
    setTitle(english ? QStringLiteral("Normalized Second Harmonic") : QStringLiteral("归一化二次谐波"));
    frame_title_->setText(english ? QStringLiteral("Frame:") : QStringLiteral("帧:"));
    waveform_plot_title_->setText(english ? QStringLiteral("Current Frame Waveform") : QStringLiteral("当前帧波形"));
    waveform_peak_plot_title_->setText(english ? QStringLiteral("Peak Value of Each Frame") : QStringLiteral("每帧峰值"));
    waveform_peak_plot_->setEnglish(english);
    temperature_plot_title_->setText(english ? QStringLiteral("Temperature  °C") : QStringLiteral("温度  ℃"));
    humidity_plot_title_->setText(english ? QStringLiteral("Humidity  %RH") : QStringLiteral("湿度  %RH"));
    pressure_plot_title_->setText(english ? QStringLiteral("Pressure  hPa") : QStringLiteral("气压  hPa"));
}

void SessionWaveformWidget::setActionTexts(
    const QString& frameFilterText,
    const QString& peakFilterText,
    const QString& plotModeText)
{
    waveform_frame_filter_btn_->setText(frameFilterText);
    waveform_peak_filter_btn_->setText(peakFilterText);
    waveform_peak_mode_btn_->setText(plotModeText);
}

void SessionWaveformWidget::setControlsEnabled(bool enabled)
{
    controls_enabled_ = enabled;
    frame_slider_->setEnabled(enabled && has_frames_);
    frame_spin_->setEnabled(enabled && has_frames_);
    waveform_frame_filter_btn_->setEnabled(enabled);
    waveform_peak_filter_btn_->setEnabled(enabled);
    waveform_peak_mode_btn_->setEnabled(enabled);
}

void SessionWaveformWidget::configureFrames(quint64 totalFrames)
{
    has_frames_ = totalFrames > 0;
    const QSignalBlocker sliderBlocker(frame_slider_);
    const QSignalBlocker spinBlocker(frame_spin_);
    frame_slider_->setEnabled(controls_enabled_ && has_frames_);
    frame_spin_->setEnabled(controls_enabled_ && has_frames_);
    if (has_frames_)
    {
        const int maximum = static_cast<int>(std::min<quint64>(totalFrames, static_cast<quint64>(std::numeric_limits<int>::max())));
        frame_slider_->setRange(1, maximum);
        frame_spin_->setRange(1, maximum);
        if (frame_spin_->value() < 1 || frame_spin_->value() > maximum)
        {
            frame_slider_->setValue(1);
            frame_spin_->setValue(1);
        }
    }
    else
    {
        frame_slider_->setRange(0, 0);
        frame_spin_->setRange(0, 0);
        frame_slider_->setValue(0);
        frame_spin_->setValue(0);
    }
    const int digits = std::max(1, static_cast<int>(QString::number(std::max<quint64>(totalFrames, 1ULL)).size()));
    frame_total_label_->setText(QStringLiteral("/ %1").arg(fixedIntegerField(totalFrames, digits)));
}

void SessionWaveformWidget::setFrameValueSilently(int value)
{
    const QSignalBlocker sliderBlocker(frame_slider_);
    const QSignalBlocker spinBlocker(frame_spin_);
    frame_slider_->setValue(value);
    frame_spin_->setValue(value);
}

int SessionWaveformWidget::frameValue() const
{
    return frame_spin_->value();
}

bool SessionWaveformWidget::frameValueInRange(int value) const
{
    return value >= frame_spin_->minimum() && value <= frame_spin_->maximum();
}

void SessionWaveformWidget::setFrameInfoText(const QString& text)
{
    frame_info_label_->setText(text);
}

void SessionWaveformWidget::setFramePreviewInfo(
    quint64 frameIndex,
    quint64 totalFrames,
    bool english)
{
    const int digits = std::max(1, static_cast<int>(QString::number(totalFrames).size()));
    setFrameInfoText(QString(english
        ? "Previewing frame %1 / %2. Release the slider to sync CSV and details."
        : "正在预览第 %1 / %2 帧。松开滑块后同步 CSV 和详细信息。")
        .arg(fixedIntegerField(frameIndex + 1, digits))
        .arg(fixedIntegerField(totalFrames, digits)));
}

void SessionWaveformWidget::setFrameDetails(
    quint64 frameIndex,
    quint64 totalFrames,
    quint64 timestampUs,
    const QString& exportMode,
    int exportRateHz,
    double minimum,
    double maximum,
    double peak,
    const QString& sourceFilename,
    const QString& csvMatchText,
    bool english)
{
    const QString exportText = (exportMode == QStringLiteral("per_frame") || exportRateHz <= 0)
        ? (english ? QStringLiteral("per-frame export") : QStringLiteral("逐帧导出"))
        : QString(english ? "%1 Hz export" : "%1 Hz 导出").arg(fixedDecimalField(exportRateHz, 2, 8));
    const QString peakText = std::isfinite(peak)
        ? fixedSignedDecimalField(peak, 6, 14)
        : fixedTextField(english ? QStringLiteral("No valid value") : QStringLiteral("无有效值"), 14, Qt::AlignLeft);
    const int digits = std::max(1, static_cast<int>(QString::number(totalFrames).size()));
    setFrameInfoText(QString(english
        ? "Frame %1 / %2 | %3 | %4 | min=%5 max=%6 peak=%7 | %8"
        : "第 %1 / %2 帧 | %3 | %4 | min=%5 max=%6 峰值=%7 | %8")
        .arg(fixedIntegerField(frameIndex + 1, digits))
        .arg(fixedIntegerField(totalFrames, digits))
        .arg(formatTimestampUs(timestampUs))
        .arg(exportText)
        .arg(fixedSignedDecimalField(minimum, 6, 14))
        .arg(fixedSignedDecimalField(maximum, 6, 14))
        .arg(peakText)
        .arg(QFileInfo(sourceFilename).fileName())
        + (csvMatchText.isEmpty() ? QString() : QStringLiteral(" | ") + csvMatchText));
}

void SessionWaveformWidget::setWaveformSamples(const QVector<float>& samples, int firstSampleIndex)
{
    waveform_plot_->setSamples(samples, firstSampleIndex);
}

void SessionWaveformWidget::setPeakValues(const QVector<float>& values)
{
    waveform_peak_plot_->setPeakValues(values);
}

void SessionWaveformWidget::setCurrentPeakFrame(int frameIndex)
{
    waveform_peak_plot_->setCurrentFrame(frameIndex);
}

void SessionWaveformWidget::setPlotMode(bool scatter)
{
    waveform_peak_plot_->setPlotMode(scatter ? SessionPeakPlotWidget::PlotMode::Scatter : SessionPeakPlotWidget::PlotMode::Polyline);
    temperature_plot_->setPlotMode(scatter ? SingleSeriesTrendPlotWidget::PlotMode::Scatter : SingleSeriesTrendPlotWidget::PlotMode::Polyline);
    humidity_plot_->setPlotMode(scatter ? SingleSeriesTrendPlotWidget::PlotMode::Scatter : SingleSeriesTrendPlotWidget::PlotMode::Polyline);
    pressure_plot_->setPlotMode(scatter ? SingleSeriesTrendPlotWidget::PlotMode::Scatter : SingleSeriesTrendPlotWidget::PlotMode::Polyline);
}

void SessionWaveformWidget::repaintPlots()
{
    waveform_peak_plot_->repaint();
    temperature_plot_->repaint();
    humidity_plot_->repaint();
    pressure_plot_->repaint();
}

void SessionWaveformWidget::setEnvironmentSeries(
    const QVector<double>& temperature,
    const QVector<double>& humidity,
    const QVector<double>& pressure)
{
    temperature_values_ = temperature;
    humidity_values_ = humidity;
    pressure_values_ = pressure;
    temperature_plot_->setValues(temperature_values_);
    humidity_plot_->setValues(humidity_values_);
    pressure_plot_->setValues(pressure_values_);
    setEnvironmentCurrentIndex(-1, false);
}

void SessionWaveformWidget::setEnvironmentCurrentIndex(int row, bool english)
{
    temperature_plot_->setCurrentIndex(row);
    humidity_plot_->setCurrentIndex(row);
    pressure_plot_->setCurrentIndex(row);
    if (row < 0)
    {
        return;
    }
    environment_info_label_->setText(QString(english
        ? "Red: temperature %1, blue: humidity %2, green: pressure %3 (CSV row %4)."
        : "红色: 温度 %1，蓝色: 湿度 %2，绿色: 气压 %3（CSV 第%4行）。")
        .arg(formatOptionalValue(row < temperature_values_.size() ? temperature_values_.at(row) : std::numeric_limits<double>::quiet_NaN(), 2, 8, QStringLiteral("°C")))
        .arg(formatOptionalValue(row < humidity_values_.size() ? humidity_values_.at(row) : std::numeric_limits<double>::quiet_NaN(), 2, 8, QStringLiteral("%RH")))
        .arg(formatOptionalValue(row < pressure_values_.size() ? pressure_values_.at(row) : std::numeric_limits<double>::quiet_NaN(), 2, 9, QStringLiteral("hPa")))
        .arg(fixedIntegerField(row + 1, 8)));
}

void SessionWaveformWidget::setEnvironmentRange(int startIndex, int count)
{
    temperature_plot_->setViewRange(startIndex, count);
    humidity_plot_->setViewRange(startIndex, count);
    pressure_plot_->setViewRange(startIndex, count);
}

void SessionWaveformWidget::setEnvironmentInfoText(const QString& text)
{
    environment_info_label_->setText(text);
}

void SessionWaveformWidget::clear()
{
    setWaveformSamples({});
    setPeakValues({});
    setCurrentPeakFrame(-1);
    setEnvironmentSeries({}, {}, {});
    configureFrames(0);
}

SessionDeviceDataWidget::SessionDeviceDataWidget(QWidget *parent)
    : QGroupBox(parent)
    , csv_info_label_(new QLabel(this))
    , csv_table_(new QTableView(this))
    , csv_model_(createSessionCsvTableModel(this))
{
    setObjectName(QStringLiteral("sensorGroupBox"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 30, 10, 10);
    layout->setSpacing(6);
    csv_info_label_->setWordWrap(true);
    layout->addWidget(csv_info_label_);
    csv_table_->setObjectName(QStringLiteral("sessionViewerCsvTable"));
    csv_table_->viewport()->setObjectName(QStringLiteral("sessionViewerCsvViewport"));
    csv_table_->setModel(csv_model_);
    csv_table_->setAlternatingRowColors(false);
    csv_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    csv_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    csv_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    csv_table_->setWordWrap(false);
    auto *csvHeader = csv_table_->horizontalHeader();
    csvHeader->setSectionsMovable(true);
    csvHeader->setSectionResizeMode(QHeaderView::Interactive);
    csvHeader->setResizeContentsPrecision(2);
    csvHeader->setDefaultSectionSize(140);
    csv_table_->verticalHeader()->setVisible(false);
    layout->addWidget(csv_table_, 1);
    setEnglish(false);
    applyTheme();
}

void SessionDeviceDataWidget::setEnglish(bool english)
{
    is_english_ = english;
    setTitle(english ? QStringLiteral("Sensors CSV") : QStringLiteral("传感器 CSV"));
    updateDisplayHeaders();
}

void SessionDeviceDataWidget::setInfoText(const QString& text)
{
    csv_info_label_->setText(text);
}

void SessionDeviceDataWidget::setRows(const QStringList& headers, QVector<QStringList>&& rows)
{
    csv_headers_ = headers;
    QStringList displayHeaders;
    displayHeaders.reserve(headers.size() + 2);
    displayHeaders << (is_english_ ? QStringLiteral("No.") : QStringLiteral("序号"));
    displayHeaders << (is_english_ ? QStringLiteral("Delta") : QStringLiteral("时间误差"));
    displayHeaders << headers;
    csv_model_->setRows(displayHeaders, std::move(rows));
    csv_model_->setTheme(sessionTableThemeFor(this));
    csv_table_->resizeColumnsToContents();
}

void SessionDeviceDataWidget::clear()
{
    csv_headers_.clear();
    csv_model_->clear();
}

void SessionDeviceDataWidget::applyTheme()
{
    const SessionTableTheme theme = sessionTableThemeFor(this);
    QPalette tablePalette = csv_table_->palette();
    tablePalette.setColor(QPalette::Base, theme.background);
    tablePalette.setColor(QPalette::AlternateBase, theme.background);
    tablePalette.setColor(QPalette::Text, theme.text);
    tablePalette.setColor(QPalette::WindowText, theme.text);
    tablePalette.setColor(QPalette::Window, theme.background);
    tablePalette.setColor(QPalette::Highlight, theme.selectedBackground);
    tablePalette.setColor(QPalette::HighlightedText, theme.selectedText);
    csv_table_->setPalette(tablePalette);
    csv_table_->viewport()->setPalette(tablePalette);
    csv_table_->viewport()->setBackgroundRole(QPalette::Base);
    csv_table_->viewport()->setAutoFillBackground(true);
    csv_table_->horizontalHeader()->setPalette(tablePalette);
    csv_table_->setStyleSheet(QStringLiteral(
        "QTableView { background-color: %1; alternate-background-color: %1; border: 1px solid %3; color: %2; gridline-color: %3; selection-background-color: %6; selection-color: %7; }"
        "QWidget#sessionViewerCsvViewport { background-color: %1; }"
        "QTableView::item { color: %2; }"
        "QTableView::item:selected, QTableView::item:selected:active, QTableView::item:selected:!active { background-color: %6; color: %7; }"
        "QHeaderView::section { background-color: %4; color: %5; border: 0px; border-right: 1px solid %3; border-bottom: 1px solid %3; padding: 4px 8px; }"
        "QTableCornerButton::section { background-color: %4; border: 0px; border-right: 1px solid %3; border-bottom: 1px solid %3; }")
        .arg(theme.background.name(), theme.text.name(), theme.grid.name(),
             theme.headerBackground.name(), theme.headerText.name(),
             theme.selectedBackground.name(), theme.selectedText.name()));
    csv_model_->setTheme(theme);
    updateMinimumTableHeight();
    csv_table_->viewport()->update();
}

void SessionDeviceDataWidget::updateMinimumTableHeight()
{
    const int minimumTableHeight = csv_table_->horizontalHeader()->sizeHint().height()
        + csv_table_->verticalHeader()->defaultSectionSize() * kMinimumVisibleCsvRows
        + csv_table_->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, csv_table_)
        + csv_table_->frameWidth() * 2;
    csv_table_->setMinimumHeight(minimumTableHeight);
}

SessionCsvHighlightResult SessionDeviceDataWidget::highlightTimestamp(
    const QVector<quint64>& timestampsUs,
    quint64 timestampUs,
    bool scrollToRow)
{
    SessionCsvHighlightResult result;
    if (timestampsUs.isEmpty() || csv_model_->rowCount() == 0)
    {
        csv_model_->setHighlightedRows({}, -1, {});
        return result;
    }
    const auto it = std::lower_bound(timestampsUs.cbegin(), timestampsUs.cend(), timestampUs);
    QVector<int> rows;
    rows.reserve(2);
    if (it == timestampsUs.cbegin())
    {
        rows.push_back(0);
        if (timestampsUs.size() > 1) rows.push_back(1);
    }
    else if (it == timestampsUs.cend())
    {
        if (timestampsUs.size() > 1) rows.push_back(timestampsUs.size() - 2);
        rows.push_back(timestampsUs.size() - 1);
    }
    else
    {
        const int upperIndex = static_cast<int>(it - timestampsUs.cbegin());
        rows.push_back(upperIndex - 1);
        rows.push_back(upperIndex);
    }
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    qint64 bestDelta = std::numeric_limits<qint64>::max();
    QHash<int, QString> deltaTextByRow;
    QStringList descriptions;
    for (int row : rows)
    {
        const qint64 deltaUs = static_cast<qint64>(timestampsUs.at(row)) - static_cast<qint64>(timestampUs);
        if (std::llabs(deltaUs) < bestDelta)
        {
            bestDelta = std::llabs(deltaUs);
            result.primaryRow = row;
        }
        const QString deltaText = formatSignedDeltaMs(deltaUs);
        deltaTextByRow.insert(row, deltaText);
        descriptions.append(is_english_
            ? QStringLiteral("CSV row %1 (%2)").arg(fixedIntegerField(row + 1, 8), fixedTextField(deltaText, 12))
            : QStringLiteral("CSV 第%1行（%2）").arg(fixedIntegerField(row + 1, 8), fixedTextField(deltaText, 12)));
    }
    csv_model_->setHighlightedRows(rows, result.primaryRow, deltaTextByRow);
    if (scrollToRow && result.primaryRow >= 0)
    {
        csv_table_->scrollTo(
            csv_model_->index(result.primaryRow, 0),
            QAbstractItemView::PositionAtCenter);
    }
    csv_table_->viewport()->update();
    result.description = descriptions.join(QStringLiteral(" | "));
    return result;
}

void SessionDeviceDataWidget::updateDisplayHeaders()
{
    QStringList displayHeaders;
    displayHeaders.reserve(csv_headers_.size() + 2);
    displayHeaders << (is_english_ ? QStringLiteral("No.") : QStringLiteral("序号"));
    displayHeaders << (is_english_ ? QStringLiteral("Delta") : QStringLiteral("时间误差"));
    displayHeaders << csv_headers_;
    csv_model_->setHeaders(displayHeaders);
    csv_table_->resizeColumnsToContents();
}

bool editSessionPeakSettings(
    QWidget *parent,
    bool english,
    int searchStartIndex,
    int searchEndIndex,
    const SessionPeakFilterSettings& filter,
    SessionPeakSettingsInput& output)
{
    using PeakFilterMode = SessionPeakFilterMode;
    QDialog dialog(parent);
    dialog.setWindowTitle(english ? QStringLiteral("Peak Settings") : QStringLiteral("峰值设置"));
    VaporView::installCustomTitleBar(&dialog, false);
    QWidget *content = dialog.findChild<QWidget *>(QStringLiteral("customTitleBarContent"));
    if (!content) content = &dialog;
    auto *layout = qobject_cast<QVBoxLayout *>(content->layout());
    if (!layout) layout = new QVBoxLayout(content);
    layout->setContentsMargins(22, 18, 22, 18);
    layout->setSpacing(14);
    auto *formWidget = new QWidget(content);
    auto *formLayout = new QGridLayout(formWidget);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setHorizontalSpacing(14);
    formLayout->setVerticalSpacing(10);
    const int labelColumnWidth = english ? 104 : 86;
    const int inputColumnWidth = 240;
    auto addFormRow = [formWidget, formLayout, labelColumnWidth](int row, const QString& text, QWidget *editor) {
        auto *label = new QLabel(text, formWidget);
        label->setMinimumWidth(labelColumnWidth);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        editor->setMinimumHeight(34);
        formLayout->addWidget(label, row, 0, Qt::AlignRight | Qt::AlignVCenter);
        formLayout->addWidget(editor, row, 1);
    };
    auto *startSpin = new QSpinBox(formWidget);
    startSpin->setRange(0, 10000000);
    startSpin->setSingleStep(1000);
    startSpin->setValue(searchStartIndex);
    startSpin->setMinimumWidth(inputColumnWidth);
    addFormRow(0, english ? QStringLiteral("Search Start") : QStringLiteral("搜索起点"), startSpin);
    auto *endSpin = new QSpinBox(formWidget);
    endSpin->setRange(0, 10000000);
    endSpin->setSingleStep(1000);
    endSpin->setSpecialValueText(english ? QStringLiteral("Full Frame") : QStringLiteral("整帧"));
    endSpin->setValue(std::max(0, searchEndIndex));
    endSpin->setMinimumWidth(inputColumnWidth);
    addFormRow(1, english ? QStringLiteral("Search End") : QStringLiteral("搜索终点"), endSpin);
    auto *modeCombo = new QComboBox(formWidget);
    modeCombo->addItem(english ? QStringLiteral("Off") : QStringLiteral("关闭"), static_cast<int>(PeakFilterMode::None));
    modeCombo->addItem(english ? QStringLiteral("IQR Outlier Filter") : QStringLiteral("IQR 异常值过滤"), static_cast<int>(PeakFilterMode::IqrOutlier));
    modeCombo->addItem(english ? QStringLiteral("Keep Range") : QStringLiteral("保留区间"), static_cast<int>(PeakFilterMode::KeepRange));
    modeCombo->addItem(english ? QStringLiteral("Exclude Range") : QStringLiteral("排除区间"), static_cast<int>(PeakFilterMode::ExcludeRange));
    modeCombo->setCurrentIndex(std::max(0, modeCombo->findData(static_cast<int>(filter.mode))));
    modeCombo->setMinimumWidth(inputColumnWidth);
    VaporView::configureComboBoxPopup(modeCombo, VaporView::isDarkThemeEnabled());
    addFormRow(2, english ? QStringLiteral("Method") : QStringLiteral("方式"), modeCombo);
    auto *minEdit = new QLineEdit(QString::number(filter.minValue, 'f', 6), formWidget);
    auto *maxEdit = new QLineEdit(QString::number(filter.maxValue, 'f', 6), formWidget);
    minEdit->setMinimumWidth(inputColumnWidth);
    maxEdit->setMinimumWidth(inputColumnWidth);
    addFormRow(3, english ? QStringLiteral("Range Min") : QStringLiteral("区间最小值"), minEdit);
    addFormRow(4, english ? QStringLiteral("Range Max") : QStringLiteral("区间最大值"), maxEdit);
    formLayout->setColumnMinimumWidth(0, labelColumnWidth);
    formLayout->setColumnMinimumWidth(1, inputColumnWidth);
    formLayout->setColumnStretch(1, 1);
    layout->addWidget(formWidget);
    auto *hintLabel = new QLabel(
        english
            ? QStringLiteral("Peak search uses sample indexes [start, end). Search End = Full Frame uses all remaining samples. IQR removes statistical outliers. Keep Range keeps only values inside [min, max]. Exclude Range removes values inside [min, max]. If no peak remains after filtering, the plot shows no valid values.")
            : QStringLiteral("峰值搜索使用采样点下标 [起点, 终点)。搜索终点为“整帧”时表示一直搜索到本帧末尾。IQR 会过滤统计异常值。保留区间只保留 [最小值, 最大值] 内的峰值。排除区间会过滤 [最小值, 最大值] 内的峰值。过滤后没有峰值时，趋势图显示无有效值。"),
        content);
    hintLabel->setWordWrap(true);
    hintLabel->setMinimumWidth(labelColumnWidth + inputColumnWidth + formLayout->horizontalSpacing());
    layout->addWidget(hintLabel);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, content);
    buttons->button(QDialogButtonBox::Ok)->setText(english ? QStringLiteral("OK") : QStringLiteral("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(english ? QStringLiteral("Cancel") : QStringLiteral("取消"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.setMinimumSize(english ? QSize(520, 430) : QSize(500, 420));
    dialog.resize(dialog.minimumSize());
    if (dialog.exec() != QDialog::Accepted) return false;

    bool minOk = false;
    bool maxOk = false;
    const double minValue = minEdit->text().trimmed().toDouble(&minOk);
    const double maxValue = maxEdit->text().trimmed().toDouble(&maxOk);
    const int start = startSpin->value();
    const int end = endSpin->value();
    const PeakFilterMode mode = static_cast<PeakFilterMode>(modeCombo->currentData().toInt());
    if (end > 0 && end <= start)
    {
        QMessageBox::warning(parent, dialog.windowTitle(), english
            ? QStringLiteral("Search End must be greater than Search Start, or set to Full Frame.")
            : QStringLiteral("搜索终点必须大于搜索起点，或者设置为整帧。"));
        return false;
    }
    if ((mode == PeakFilterMode::KeepRange || mode == PeakFilterMode::ExcludeRange) && (!minOk || !maxOk))
    {
        QMessageBox::warning(parent, dialog.windowTitle(), english
            ? QStringLiteral("Please enter valid numeric range values.")
            : QStringLiteral("请输入有效的数值区间。"));
        return false;
    }
    output.searchStartIndex = start;
    output.searchEndIndex = end;
    output.filter.mode = mode;
    output.filter.minValue = minValue;
    output.filter.maxValue = maxValue;
    output.hasMinValue = minOk;
    output.hasMaxValue = maxOk;
    return true;
}

SessionLoadingDialog::SessionLoadingDialog(QWidget *owner)
    : owner_(owner)
{
}

SessionLoadingDialog::~SessionLoadingDialog()
{
    delete dialog_;
}

void SessionLoadingDialog::begin(const QString& text, bool english)
{
    if (!dialog_)
    {
        dialog_ = new QProgressDialog(owner_);
        dialog_->setWindowModality(Qt::NonModal);
        dialog_->setModal(false);
        dialog_->setCancelButton(nullptr);
        dialog_->setMinimumDuration(0);
        dialog_->setAutoClose(false);
        dialog_->setAutoReset(false);
        dialog_->setRange(0, 100);
        dialog_->setMinimumWidth(360);
        dialog_->setAttribute(Qt::WA_StyledBackground, true);
        dialog_->setAutoFillBackground(true);
        VaporView::installCustomTitleBar(dialog_, false);
        if (QWidget *content = dialog_->findChild<QWidget *>(QStringLiteral("customTitleBarContent")))
        {
            auto *layout = qobject_cast<QVBoxLayout *>(content->layout());
            if (!layout) layout = new QVBoxLayout(content);
            label_ = new QLabel(content);
            label_->setAlignment(Qt::AlignCenter);
            label_->setWordWrap(true);
            progress_bar_ = new QProgressBar(content);
            progress_bar_->setRange(0, 100);
            progress_bar_->setFormat(QStringLiteral("%p%"));
            progress_bar_->setTextVisible(true);
            progress_bar_->setMinimumHeight(14);
            layout->addWidget(label_);
            layout->addWidget(progress_bar_);
        }
    }
    dialog_->setWindowTitle(english ? QStringLiteral("Loading Data") : QStringLiteral("正在加载数据"));
    progress_percent_ = 0;
    applyTheme();
    update(text, 0);
    dialog_->show();
    dialog_->raise();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void SessionLoadingDialog::update(const QString& text, int percent)
{
    if (!dialog_) return;
    progress_percent_ = std::clamp(percent, 0, 100);
    dialog_->setLabelText(text);
    dialog_->setValue(progress_percent_);
    if (label_) label_->setText(text);
    if (progress_bar_)
    {
        progress_bar_->setVisible(true);
        progress_bar_->setRange(0, 100);
        progress_bar_->setValue(progress_percent_);
    }
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void SessionLoadingDialog::finish(const QString& finalText)
{
    if (!dialog_) return;
    update(finalText, 100);
    dialog_->hide();
}

void SessionLoadingDialog::applyTheme()
{
    if (!dialog_ || !owner_) return;
    const QPalette sourcePalette = owner_->palette();
    QColor windowColor = sourcePalette.color(QPalette::Window);
    if (!windowColor.isValid() || windowColor.alpha() == 0) windowColor = sourcePalette.color(QPalette::Base);
    const bool dark = windowColor.lightness() < 128;
    const QColor panelColor = appThemeColor(dark ? AppThemeColor::Window : AppThemeColor::Surface, dark);
    const QColor fieldColor = appThemeColor(AppThemeColor::FieldBackground, dark);
    const QColor borderColor = appThemeColor(AppThemeColor::FieldBorder, dark);
    const QColor textColor = appThemeColor(AppThemeColor::TextStrong, dark);
    const QColor chunkColor = appThemeColor(AppThemeColor::ProgressChunk, dark);
    QPalette palette = dialog_->palette();
    palette.setColor(QPalette::Window, panelColor);
    palette.setColor(QPalette::Base, panelColor);
    palette.setColor(QPalette::Text, textColor);
    palette.setColor(QPalette::WindowText, textColor);
    dialog_->setPalette(palette);
    if (QWidget *content = dialog_->findChild<QWidget *>(QStringLiteral("customTitleBarContent")))
    {
        content->setAutoFillBackground(true);
        content->setPalette(palette);
        if (auto *layout = qobject_cast<QVBoxLayout *>(content->layout()))
        {
            layout->setContentsMargins(22, 18, 22, 18);
            layout->setSpacing(14);
        }
    }
    dialog_->setStyleSheet(QStringLiteral(
        "QProgressDialog, QWidget#customTitleBarContent { background-color: %1; color: %2; }"
        "QWidget#customTitleBarContent QLabel { background-color: transparent; color: %2; font-size: 14px; }"
        "QWidget#customTitleBarContent QProgressBar { background-color: %3; border: 1px solid %4; border-radius: 4px; min-height: 10px; text-align: center; color: %2; }"
        "QWidget#customTitleBarContent QProgressBar::chunk { background-color: %5; border-radius: 3px; }")
        .arg(panelColor.name(), textColor.name(), fieldColor.name(), borderColor.name(), chunkColor.name()));
}

}  // namespace VaporView::Ground::SessionUi
