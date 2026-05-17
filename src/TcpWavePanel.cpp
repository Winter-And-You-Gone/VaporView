#include "TcpWavePanel.h"
#include "RangeSelectionAxisWidget.h"
#include <QAbstractSocket>
#include <QByteArray>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPolygonF>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTcpSocket>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QDateTime>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace
{
constexpr int kHeaderSize = 4;
constexpr int kFloatSize = 4;
constexpr int kMaxPayloadBytes = 16 * 1024 * 1024;
constexpr int kPreferredPayloadBytes = 200000;
constexpr int kTcpControlHeight = 30;
constexpr int kTcpButtonHeight = 38;
constexpr int kDefaultPeakSearchStartIndex = 0;
constexpr int kDefaultPeakSearchEndIndex = 0;
constexpr int kPeakTrendFrameWindow = 1000;
constexpr int kLiveDisplayRefreshMs = 20;
constexpr qint64 kFrameRateWindowMs = 5000;
constexpr double kMaxReasonableWaveMagnitude = 1.0e6;

QString hexPreview(const QByteArray& data, int limit = 12)
{
    const int count = std::min(limit, static_cast<int>(data.size()));
    QStringList parts;
    parts.reserve(count);
    for (int i = 0; i < count; ++i)
    {
        parts << QString("%1").arg(static_cast<unsigned char>(data.at(i)), 2, 16, QChar('0')).toUpper();
    }
    return parts.join(' ');
}

QString headerOrderLabel(bool english, TcpWavePanel::HeaderByteOrder order)
{
    switch (order)
    {
    case TcpWavePanel::HeaderByteOrder::LittleEndian:
        return english ? "little-endian" : "小端";
    case TcpWavePanel::HeaderByteOrder::BigEndian:
        return english ? "big-endian" : "大端";
    case TcpWavePanel::HeaderByteOrder::Unknown:
    default:
        return english ? "unknown" : "未知";
    }
}

QString formatWaveValue(double value, int fixedDecimals)
{
    if (!std::isfinite(value))
    {
        return QStringLiteral("NaN");
    }

    const double magnitude = std::fabs(value);
    if (magnitude >= 100000.0 || (magnitude > 0.0 && magnitude < 0.0001))
    {
        return QString::number(value, 'g', 6);
    }
    return QString::number(value, 'f', fixedDecimals);
}

bool isReasonableWavePayload(const QVector<float>& values, double maxMagnitude, double *observedMaxMagnitude = nullptr)
{
    double observedMax = 0.0;
    for (float value : values)
    {
        if (!std::isfinite(value))
        {
            if (observedMaxMagnitude)
            {
                *observedMaxMagnitude = std::numeric_limits<double>::infinity();
            }
            return false;
        }

        observedMax = std::max(observedMax, std::fabs(static_cast<double>(value)));
        if (observedMax > maxMagnitude)
        {
            if (observedMaxMagnitude)
            {
                *observedMaxMagnitude = observedMax;
            }
            return false;
        }
    }

    if (observedMaxMagnitude)
    {
        *observedMaxMagnitude = observedMax;
    }
    return true;
}

double percentileValue(QVector<double> values, double percentile)
{
    if (values.isEmpty())
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    std::sort(values.begin(), values.end());
    const double clampedPercentile = std::clamp(percentile, 0.0, 1.0);
    const double scaledIndex = clampedPercentile * static_cast<double>(values.size() - 1);
    const int lowerIndex = static_cast<int>(std::floor(scaledIndex));
    const int upperIndex = static_cast<int>(std::ceil(scaledIndex));
    if (lowerIndex == upperIndex)
    {
        return values.at(lowerIndex);
    }

    const double fraction = scaledIndex - static_cast<double>(lowerIndex);
    return values.at(lowerIndex) * (1.0 - fraction) + values.at(upperIndex) * fraction;
}

float waveformPeakValue(const QVector<float>& samples, int searchStartIndex, int searchEndIndex)
{
    if (samples.isEmpty())
    {
        return std::numeric_limits<float>::quiet_NaN();
    }

    const int sampleCount = samples.size();
    const int startIndex = std::clamp(searchStartIndex, 0, sampleCount);
    const int endIndex = searchEndIndex <= 0
        ? sampleCount
        : std::clamp(searchEndIndex, 0, sampleCount);
    if (startIndex >= endIndex)
    {
        return std::numeric_limits<float>::quiet_NaN();
    }

    bool hasPeak = false;
    float peakValue = std::numeric_limits<float>::lowest();
    for (int index = startIndex; index < endIndex; ++index)
    {
        const float value = samples.at(index);
        if (!std::isfinite(value))
        {
            continue;
        }
        hasPeak = true;
        peakValue = std::max(peakValue, value);
    }
    return hasPeak ? peakValue : std::numeric_limits<float>::quiet_NaN();
}
}

class WavePlotWidget : public QWidget
{
public:
    explicit WavePlotWidget(const QColor& lineColor, QWidget *parent = nullptr)
        : QWidget(parent)
        , line_color_(lineColor)
    {
        setMinimumHeight(120);
        setMaximumHeight(150);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    void setSamples(const QVector<float>& samples)
    {
        samples_ = samples;
        update();
    }

    void setEmptyText(const QString& text)
    {
        empty_text_ = text;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), QColor("#ffffff"));

        if (samples_.isEmpty())
        {
            const QRectF emptyPlotRect = rect().adjusted(18, 8, -2, -18);
            painter.setPen(QPen(QColor("#cfd7e3"), 1));
            painter.drawRect(emptyPlotRect);
            painter.setPen(QColor("#7a8899"));
            painter.drawText(emptyPlotRect, Qt::AlignCenter, empty_text_);
            return;
        }

        auto [minIt, maxIt] = std::minmax_element(samples_.cbegin(), samples_.cend());
        float minValue = *minIt;
        float maxValue = *maxIt;
        if (std::fabs(maxValue - minValue) < 1e-6f)
        {
            const float pad = std::max(1e-6f, std::fabs(maxValue) * 0.05f + 1e-6f);
            minValue -= pad;
            maxValue += pad;
        }

        const QString maxLabel = formatWaveValue(maxValue, 3);
        const QString midLabel = formatWaveValue((maxValue + minValue) * 0.5, 3);
        const QString minLabel = formatWaveValue(minValue, 3);
        const QFontMetrics fm = painter.fontMetrics();
        const int labelWidth = std::max({fm.horizontalAdvance(maxLabel), fm.horizontalAdvance(midLabel), fm.horizontalAdvance(minLabel)});
        const int leftMargin = std::max(18, labelWidth + 4);
        const int bottomMargin = fm.height() + 2;
        const QRectF plotRect = rect().adjusted(leftMargin, 8, -2, -bottomMargin);

        painter.setPen(QPen(QColor("#e3e8ef"), 1));
        for (int i = 0; i <= 10; ++i)
        {
            const qreal x = plotRect.left() + plotRect.width() * i / 10.0;
            painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
        }
        for (int i = 0; i <= 8; ++i)
        {
            const qreal y = plotRect.top() + plotRect.height() * i / 8.0;
            painter.drawLine(QPointF(plotRect.left(), y), QPointF(plotRect.right(), y));
        }

        painter.setPen(QPen(QColor("#cfd7e3"), 1));
        painter.drawRect(plotRect);

        const int columns = std::max(2, static_cast<int>(std::floor(plotRect.width())));
        QPolygonF polyline;
        polyline.reserve(columns);
        const int sampleCount = samples_.size();
        for (int x = 0; x < columns; ++x)
        {
            const double ratio = columns == 1 ? 0.0 : static_cast<double>(x) / static_cast<double>(columns - 1);
            const int index = std::clamp(static_cast<int>(std::llround(ratio * (sampleCount - 1))), 0, sampleCount - 1);
            const float value = samples_.at(index);
            const double normalized = (value - minValue) / std::max(1e-6f, maxValue - minValue);
            const qreal px = plotRect.left() + ratio * plotRect.width();
            const qreal py = plotRect.bottom() - normalized * plotRect.height();
            polyline.append(QPointF(px, py));
        }

        painter.setPen(QPen(line_color_, 1.4));
        painter.drawPolyline(polyline);

        painter.setPen(QColor("#5e6b78"));
        painter.drawText(QRectF(2, plotRect.top() - 2, leftMargin - 4, fm.height()), Qt::AlignRight | Qt::AlignVCenter, maxLabel);
        painter.drawText(QRectF(2, plotRect.center().y() - fm.height() * 0.5, leftMargin - 4, fm.height()), Qt::AlignRight | Qt::AlignVCenter, midLabel);
        painter.drawText(QRectF(2, plotRect.bottom() - fm.height() + 2, leftMargin - 4, fm.height()), Qt::AlignRight | Qt::AlignVCenter, minLabel);
        painter.drawText(QRectF(plotRect.left(), plotRect.bottom() + 2, plotRect.width(), fm.height()), Qt::AlignRight | Qt::AlignVCenter,
                         QString("%1 samples").arg(sampleCount));
    }

private:
    QColor line_color_;
    QVector<float> samples_;
    QString empty_text_ = QStringLiteral("No data");
};

class PeakTrendPlotWidget : public QWidget
{
public:
    enum class PlotMode
    {
        Scatter,
        Polyline
    };

    explicit PeakTrendPlotWidget(QWidget *parent = nullptr)
        : QWidget(parent)
        , plot_mode_(PlotMode::Scatter)
        , view_start_index_(0)
        , view_count_(0)
    {
        setMinimumHeight(150);
        setMaximumHeight(190);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    void setPeakValues(const QVector<float>& values)
    {
        const bool keepTail = peak_values_.isEmpty() ||
            view_count_ <= 0 ||
            (view_start_index_ + visibleCount()) >= peak_values_.size();
        peak_values_ = values;
        normalizeView(keepTail);
        notifyViewChanged();
        update();
    }

    void setPlotMode(PlotMode mode)
    {
        plot_mode_ = mode;
        update();
    }

    void setViewRange(int startIndex, int count)
    {
        if (peak_values_.isEmpty())
        {
            return;
        }

        if (count <= 0 || count >= static_cast<int>(peak_values_.size()))
        {
            view_start_index_ = 0;
            view_count_ = 0;
        }
        else
        {
            view_start_index_ = startIndex;
            view_count_ = count;
            normalizeView(false);
        }

        notifyViewChanged();
        update();
    }

    void setViewChangedCallback(std::function<void(int, int, int)> callback)
    {
        on_view_changed_ = std::move(callback);
        notifyViewChanged();
    }

    void setEmptyText(const QString& text)
    {
        empty_text_ = text;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.fillRect(rect(), QColor("#ffffff"));

        if (peak_values_.isEmpty())
        {
            const QRectF emptyPlotRect = rect().adjusted(18, 8, -2, -18);
            painter.setPen(QPen(QColor("#cfd7e3"), 1));
            painter.drawRect(emptyPlotRect);
            painter.setPen(QColor("#7a8899"));
            painter.drawText(emptyPlotRect, Qt::AlignCenter, empty_text_);
            return;
        }

        const int startIndex = visibleStartIndex();
        const int count = visibleCount();
        QVector<int> finiteIndices;
        finiteIndices.reserve(count);
        float minValue = std::numeric_limits<float>::max();
        float maxValue = std::numeric_limits<float>::lowest();
        for (int i = 0; i < count; ++i)
        {
            const float value = peak_values_.at(startIndex + i);
            if (!std::isfinite(value))
            {
                continue;
            }
            finiteIndices.push_back(i);
            minValue = std::min(minValue, value);
            maxValue = std::max(maxValue, value);
        }
        if (finiteIndices.isEmpty())
        {
            painter.setPen(QColor("#7a8899"));
            painter.drawText(rect().adjusted(18, 8, -2, -18), Qt::AlignCenter, empty_text_);
            return;
        }
        if (std::fabs(maxValue - minValue) < 1e-6f)
        {
            const float pad = std::max(1e-6f, std::fabs(maxValue) * 0.05f + 1e-6f);
            minValue -= pad;
            maxValue += pad;
        }

        const QString maxLabel = QString::number(maxValue, 'f', 3);
        const QString midLabel = QString::number((maxValue + minValue) * 0.5, 'f', 3);
        const QString minLabel = QString::number(minValue, 'f', 3);
        const QFontMetrics fm = painter.fontMetrics();
        const int labelWidth = std::max({fm.horizontalAdvance(maxLabel), fm.horizontalAdvance(midLabel), fm.horizontalAdvance(minLabel)});
        const int leftMargin = std::max(18, labelWidth + 4);
        const int bottomMargin = fm.height() + 2;
        const QRectF plotRect = rect().adjusted(leftMargin, 8, -2, -bottomMargin);

        painter.setPen(QPen(QColor("#e3e8ef"), 1));
        for (int i = 0; i <= 10; ++i)
        {
            const qreal x = plotRect.left() + plotRect.width() * i / 10.0;
            painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
        }
        for (int i = 0; i <= 6; ++i)
        {
            const qreal y = plotRect.top() + plotRect.height() * i / 6.0;
            painter.drawLine(QPointF(plotRect.left(), y), QPointF(plotRect.right(), y));
        }

        painter.setPen(QPen(QColor("#cfd7e3"), 1));
        painter.drawRect(plotRect);

        const QColor seriesColor("#ef8f35");
        if (plot_mode_ == PlotMode::Polyline && count >= 2)
        {
            painter.setPen(QPen(seriesColor, 1.5));
            QPolygonF segment;
            const int sampledCount = std::max(2, std::min(count, static_cast<int>(std::ceil(plotRect.width())) + 1));
            segment.reserve(sampledCount);
            for (int sample = 0; sample < sampledCount; ++sample)
            {
                const int i = std::clamp(
                    static_cast<int>(std::llround(static_cast<double>(sample) * (count - 1) / (sampledCount - 1))),
                    0,
                    count - 1);
                const float value = peak_values_.at(startIndex + i);
                if (!std::isfinite(value))
                {
                    if (segment.size() >= 2)
                    {
                        painter.drawPolyline(segment);
                    }
                    segment.clear();
                    continue;
                }

                const double ratio = static_cast<double>(i) / static_cast<double>(count - 1);
                const double normalized = (value - minValue) / std::max(1e-6f, maxValue - minValue);
                segment.push_back(QPointF(plotRect.left() + ratio * plotRect.width(),
                                          plotRect.bottom() - normalized * plotRect.height()));
            }
            if (segment.size() >= 2)
            {
                painter.drawPolyline(segment);
            }
        }
        else
        {
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(Qt::NoPen);
            painter.setBrush(seriesColor);
            for (int i = 0; i < count; ++i)
            {
                const float value = peak_values_.at(startIndex + i);
                if (!std::isfinite(value))
                {
                    continue;
                }
                const double ratio = count == 1 ? 0.5 : static_cast<double>(i) / static_cast<double>(count - 1);
                const double normalized = (value - minValue) / std::max(1e-6f, maxValue - minValue);
                const QPointF point(plotRect.left() + ratio * plotRect.width(),
                                    plotRect.bottom() - normalized * plotRect.height());
                painter.drawEllipse(point, 2.5, 2.5);
            }
            painter.setRenderHint(QPainter::Antialiasing, false);
        }

        painter.setPen(QColor("#5e6b78"));
        painter.drawText(QRectF(2, plotRect.top() - 2, leftMargin - 4, fm.height()), Qt::AlignRight | Qt::AlignVCenter, maxLabel);
        painter.drawText(QRectF(2, plotRect.center().y() - fm.height() * 0.5, leftMargin - 4, fm.height()), Qt::AlignRight | Qt::AlignVCenter, midLabel);
        painter.drawText(QRectF(2, plotRect.bottom() - fm.height() + 2, leftMargin - 4, fm.height()), Qt::AlignRight | Qt::AlignVCenter, minLabel);
        painter.drawText(QRectF(plotRect.left(), plotRect.bottom() + 2, plotRect.width() * 0.55, fm.height()),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QString("%1-%2 / %3")
                             .arg(startIndex + 1)
                             .arg(startIndex + count)
                             .arg(peak_values_.size()));
        painter.drawText(QRectF(plotRect.left(), plotRect.bottom() + 2, plotRect.width(), fm.height()), Qt::AlignRight | Qt::AlignVCenter,
                         QString("%1 frames").arg(count));
    }

private:
    int visibleStartIndex() const
    {
        const int totalCount = static_cast<int>(peak_values_.size());
        return peak_values_.isEmpty() ? 0 : std::clamp(view_start_index_, 0, std::max(0, totalCount - visibleCount()));
    }

    int visibleCount() const
    {
        const int totalCount = static_cast<int>(peak_values_.size());
        if (peak_values_.isEmpty())
        {
            return 0;
        }
        if (view_count_ <= 0 || view_count_ >= totalCount)
        {
            return totalCount;
        }
        return std::clamp(view_count_, 1, totalCount);
    }

    void normalizeView(bool keepTail)
    {
        const int totalCount = static_cast<int>(peak_values_.size());
        if (peak_values_.isEmpty())
        {
            view_start_index_ = 0;
            view_count_ = 0;
            return;
        }

        if (view_count_ <= 0 || view_count_ >= totalCount)
        {
            view_start_index_ = 0;
            view_count_ = 0;
            return;
        }

        view_count_ = std::clamp(view_count_, 1, totalCount);
        if (keepTail)
        {
            view_start_index_ = std::max(0, totalCount - view_count_);
        }
        else
        {
            view_start_index_ = std::clamp(view_start_index_, 0, std::max(0, totalCount - view_count_));
        }
    }

    void notifyViewChanged()
    {
        if (on_view_changed_)
        {
            on_view_changed_(static_cast<int>(peak_values_.size()), visibleStartIndex(), visibleCount());
        }
    }

    QVector<float> peak_values_;
    PlotMode plot_mode_;
    int view_start_index_;
    int view_count_;
    QString empty_text_ = QStringLiteral("No peak data");
    std::function<void(int, int, int)> on_view_changed_;
};

TcpWavePanel::TcpWavePanel(QWidget *parent)
    : QWidget(parent)
    , host_edit_(nullptr)
    , port_spin_(nullptr)
    , connect_button_(nullptr)
    , host_label_(nullptr)
    , port_label_(nullptr)
    , panel_title_label_(nullptr)
    , frame_rate_label_(nullptr)
    , status_label_(nullptr)
    , hint_label_(nullptr)
    , wave1_title_label_(nullptr)
    , wave4_title_label_(nullptr)
    , peak_title_label_(nullptr)
    , wave1_info_label_(nullptr)
    , wave4_info_label_(nullptr)
    , wave1_group_(nullptr)
    , wave4_group_(nullptr)
    , peak_group_(nullptr)
    , wave1_plot_(nullptr)
    , wave4_plot_(nullptr)
    , peak_plot_(nullptr)
    , peak_range_axis_(nullptr)
    , peak_filter_button_(nullptr)
    , peak_mode_button_(nullptr)
    , peak_clear_button_(nullptr)
    , control_layout_(nullptr)
    , top_controls_layout_(nullptr)
    , socket_(nullptr)
    , live_display_timer_(nullptr)
    , pending_wave1_payload_()
    , peak_raw_history_()
    , pending_wave1_info_text_()
    , pending_wave4_info_text_()
    , pending_live_status_text_()
    , peak_filter_settings_()
    , peak_search_start_index_(kDefaultPeakSearchStartIndex)
    , peak_search_end_index_(kDefaultPeakSearchEndIndex)
    , peak_plot_scatter_mode_(true)
    , parse_mode_(ParseMode::AutoDetect)
    , read_state_(ReadState::Wave1Header)
    , header_byte_order_(HeaderByteOrder::Unknown)
    , float_encoding_(FloatEncoding::Unknown)
    , expected_payload_size_(0)
    , frame_count_(0)
    , frame_arrival_times_ms_()
    , live_display_dirty_(false)
    , is_english_(false)
    , remote_sky_mode_(false)
    , remote_wave_tcp_connected_(false)
    , last_remote_feature_time_us_(0)
    , remote_expected_feature_interval_us_(0)
{
    setupUi();
    loadRememberedInputState();
    setupSocket();
    setEnglish(false);
}

TcpWavePanel::~TcpWavePanel()
{
    saveRememberedInputState();
    requestGracefulDisconnect();
    if (socket_)
    {
        socket_->deleteLater();
        socket_ = nullptr;
    }
}

void TcpWavePanel::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    control_layout_ = new QGridLayout();
    control_layout_->setHorizontalSpacing(1);
    control_layout_->setVerticalSpacing(4);

    top_controls_layout_ = new QHBoxLayout();
    top_controls_layout_->setContentsMargins(0, 0, 0, 0);
    top_controls_layout_->setSpacing(0);
    control_layout_->addLayout(top_controls_layout_, 0, 0, 1, 6, Qt::AlignVCenter | Qt::AlignLeft);

    panel_title_label_ = new QLabel(this);
    panel_title_label_->setObjectName("sectionTitleLabel");
    top_controls_layout_->addWidget(panel_title_label_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    top_controls_layout_->addSpacing(42);

    frame_rate_label_ = new QLabel(this);
    frame_rate_label_->setObjectName("fieldLabel");
    frame_rate_label_->setMinimumWidth(118);
    top_controls_layout_->addWidget(frame_rate_label_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    top_controls_layout_->addSpacing(42);

    auto *hostRowLayout = new QHBoxLayout();
    hostRowLayout->setContentsMargins(0, 0, 0, 0);
    hostRowLayout->setSpacing(4);
    host_label_ = new QLabel(this);
    host_label_->setObjectName("fieldLabel");
    hostRowLayout->addWidget(host_label_, 0, Qt::AlignVCenter | Qt::AlignRight);

    host_edit_ = new QLineEdit(this);
    host_edit_->setText("127.0.0.1");
    host_edit_->setFixedHeight(kTcpControlHeight);
    host_edit_->setMinimumWidth(90);
    host_edit_->setMaximumWidth(110);
    hostRowLayout->addWidget(host_edit_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    top_controls_layout_->addLayout(hostRowLayout, 0);
    top_controls_layout_->addSpacing(28);

    auto *portRowLayout = new QHBoxLayout();
    portRowLayout->setContentsMargins(0, 0, 0, 0);
    portRowLayout->setSpacing(4);
    port_label_ = new QLabel(this);
    port_label_->setObjectName("fieldLabel");
    portRowLayout->addWidget(port_label_, 0, Qt::AlignVCenter | Qt::AlignRight);

    port_spin_ = new QSpinBox(this);
    port_spin_->setRange(1, 65535);
    port_spin_->setValue(8888);
    port_spin_->setFixedHeight(kTcpControlHeight);
    port_spin_->setFixedWidth(108);
    portRowLayout->addWidget(port_spin_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    top_controls_layout_->addLayout(portRowLayout, 0);
    top_controls_layout_->addSpacing(28);

    connect_button_ = new QPushButton(this);
    connect_button_->setObjectName("compactTcpStartButton");
    connect_button_->setFixedHeight(kTcpButtonHeight);
    connect(connect_button_, &QPushButton::clicked, this, &TcpWavePanel::onToggleConnectionClicked);
    top_controls_layout_->addWidget(connect_button_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    top_controls_layout_->addStretch(1);

    status_label_ = new QLabel(this);
    status_label_->setWordWrap(true);
    control_layout_->addWidget(status_label_, 1, 0, 1, 6);

    hint_label_ = new QLabel(this);
    hint_label_->setWordWrap(true);
    control_layout_->addWidget(hint_label_, 2, 0, 1, 6);

    mainLayout->addLayout(control_layout_);

    auto *plotsLayout = new QHBoxLayout();
    plotsLayout->setSpacing(1);

    wave1_group_ = new QGroupBox(this);
    wave1_group_->setObjectName("sensorGroupBox");
    auto *wave1Layout = new QVBoxLayout(wave1_group_);
    wave1Layout->setContentsMargins(2, 2, 2, 2);
    auto *wave1HeaderLayout = new QHBoxLayout();
    wave1HeaderLayout->setContentsMargins(0, 0, 0, 0);
    wave1HeaderLayout->setSpacing(8);
    wave1_title_label_ = new QLabel(this);
    wave1_title_label_->setObjectName("sectionTitleLabel");
    wave1HeaderLayout->addWidget(wave1_title_label_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    wave1_info_label_ = new QLabel(this);
    wave1_info_label_->setObjectName("fieldLabel");
    wave1_info_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    wave1_info_label_->setWordWrap(false);
    wave1HeaderLayout->addWidget(wave1_info_label_, 1, Qt::AlignVCenter | Qt::AlignRight);
    wave1Layout->addLayout(wave1HeaderLayout);
    wave1_plot_ = new WavePlotWidget(QColor("#4e79c7"), this);
    wave1Layout->addWidget(wave1_plot_, 1);
    plotsLayout->addWidget(wave1_group_, 1);

    wave4_group_ = new QGroupBox(this);
    wave4_group_->setObjectName("sensorGroupBox");
    auto *wave4Layout = new QVBoxLayout(wave4_group_);
    wave4Layout->setContentsMargins(2, 2, 2, 2);
    auto *wave4HeaderLayout = new QHBoxLayout();
    wave4HeaderLayout->setContentsMargins(0, 0, 0, 0);
    wave4HeaderLayout->setSpacing(8);
    wave4_title_label_ = new QLabel(this);
    wave4_title_label_->setObjectName("sectionTitleLabel");
    wave4HeaderLayout->addWidget(wave4_title_label_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    wave4_info_label_ = new QLabel(this);
    wave4_info_label_->setObjectName("fieldLabel");
    wave4_info_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    wave4_info_label_->setWordWrap(false);
    wave4HeaderLayout->addWidget(wave4_info_label_, 1, Qt::AlignVCenter | Qt::AlignRight);
    wave4Layout->addLayout(wave4HeaderLayout);
    wave4_plot_ = new WavePlotWidget(QColor("#ef8f35"), this);
    wave4Layout->addWidget(wave4_plot_, 1);
    plotsLayout->addWidget(wave4_group_, 1);

    mainLayout->addLayout(plotsLayout, 1);

    peak_group_ = new QGroupBox(this);
    peak_group_->setObjectName("sensorGroupBox");
    peak_group_->setMinimumHeight(198);
    peak_group_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
    auto *peakLayout = new QVBoxLayout(peak_group_);
    peakLayout->setContentsMargins(0, 1, 0, 0);
    auto *peakHeaderLayout = new QHBoxLayout();
    peakHeaderLayout->setContentsMargins(0, 0, 0, 0);
    peakHeaderLayout->setSpacing(6);
    peak_title_label_ = new QLabel(this);
    peak_title_label_->setObjectName("sectionTitleLabel");
    peakHeaderLayout->addWidget(peak_title_label_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    peak_filter_button_ = new QPushButton(this);
    peak_filter_button_->setObjectName("compactTcpButton");
    peak_filter_button_->setFixedHeight(kTcpButtonHeight);
    peak_filter_button_->setMinimumWidth(134);
    connect(peak_filter_button_, &QPushButton::clicked, this, &TcpWavePanel::onConfigurePeakFilterClicked);
    peakHeaderLayout->addWidget(peak_filter_button_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    peak_mode_button_ = new QPushButton(this);
    peak_mode_button_->setObjectName("compactTcpButton");
    peak_mode_button_->setFixedHeight(kTcpButtonHeight);
    peak_mode_button_->setMinimumWidth(98);
    connect(peak_mode_button_, &QPushButton::clicked, this, &TcpWavePanel::onTogglePeakPlotModeClicked);
    peakHeaderLayout->addWidget(peak_mode_button_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    peak_clear_button_ = new QPushButton(this);
    peak_clear_button_->setObjectName("compactTcpButton");
    peak_clear_button_->setFixedHeight(kTcpButtonHeight);
    peak_clear_button_->setMinimumWidth(72);
    connect(peak_clear_button_, &QPushButton::clicked, this, &TcpWavePanel::onClearPeakPlotClicked);
    peakHeaderLayout->addWidget(peak_clear_button_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    peakHeaderLayout->addStretch(1);
    peakLayout->addLayout(peakHeaderLayout);
    peak_plot_ = new PeakTrendPlotWidget(this);
    peak_plot_->setPlotMode(peak_plot_scatter_mode_ ? PeakTrendPlotWidget::PlotMode::Scatter : PeakTrendPlotWidget::PlotMode::Polyline);
    peakLayout->addWidget(peak_plot_);
    peak_range_axis_ = new RangeSelectionAxisWidget(this);
    peak_plot_->setViewChangedCallback([this](int totalCount, int startIndex, int visibleCount) {
        if (peak_range_axis_)
        {
            peak_range_axis_->setRange(totalCount, startIndex, visibleCount);
        }
    });
    peak_range_axis_->setRangeChangedCallback([this](int startIndex, int visibleCount) {
        if (peak_plot_)
        {
            peak_plot_->setViewRange(startIndex, visibleCount);
        }
    });
    peakLayout->addWidget(peak_range_axis_);
    mainLayout->addWidget(peak_group_, 0);

    connect(host_edit_, &QLineEdit::textChanged, this, [this](const QString&) {
        saveRememberedInputState();
    });
    connect(port_spin_, &QSpinBox::valueChanged, this, [this](int) {
        saveRememberedInputState();
    });

    live_display_timer_ = new QTimer(this);
    live_display_timer_->setTimerType(Qt::PreciseTimer);
    live_display_timer_->setInterval(kLiveDisplayRefreshMs);
    connect(live_display_timer_, &QTimer::timeout, this, &TcpWavePanel::updateLiveDisplay);
    live_display_timer_->start();
}

void TcpWavePanel::loadRememberedInputState()
{
    QSettings settings("VaporView", "TcpWavePanel");
    const QString hostValue = settings.value("connection/host", host_edit_->text()).toString();
    const int portValue = settings.value("connection/port", port_spin_->value()).toInt();
    const QString peakFilterMode = settings.value("peak_filter/mode", QStringLiteral("none")).toString().trimmed().toLower();
    if (peakFilterMode == QStringLiteral("iqr"))
    {
        peak_filter_settings_.mode = PeakFilterMode::IqrOutlier;
    }
    else if (peakFilterMode == QStringLiteral("keep_range"))
    {
        peak_filter_settings_.mode = PeakFilterMode::KeepRange;
    }
    else if (peakFilterMode == QStringLiteral("exclude_range"))
    {
        peak_filter_settings_.mode = PeakFilterMode::ExcludeRange;
    }
    else
    {
        peak_filter_settings_.mode = PeakFilterMode::None;
    }
    peak_filter_settings_.min_value = settings.value("peak_filter/min_value", 0.0).toDouble();
    peak_filter_settings_.max_value = settings.value("peak_filter/max_value", 0.0).toDouble();
    peak_search_start_index_ = std::max(0, settings.value("peak_search/start_index", kDefaultPeakSearchStartIndex).toInt());
    peak_search_end_index_ = std::max(0, settings.value("peak_search/end_index", kDefaultPeakSearchEndIndex).toInt());

    {
        const QSignalBlocker hostBlocker(host_edit_);
        host_edit_->setText(hostValue);
    }
    {
        const QSignalBlocker portBlocker(port_spin_);
        port_spin_->setValue(portValue);
    }

    updatePeakFilterButtonText();
}

void TcpWavePanel::saveRememberedInputState() const
{
    QSettings settings("VaporView", "TcpWavePanel");
    settings.setValue("connection/host", host_edit_->text());
    settings.setValue("connection/port", port_spin_->value());

    QString modeKey = QStringLiteral("none");
    if (peak_filter_settings_.mode == PeakFilterMode::IqrOutlier)
    {
        modeKey = QStringLiteral("iqr");
    }
    else if (peak_filter_settings_.mode == PeakFilterMode::KeepRange)
    {
        modeKey = QStringLiteral("keep_range");
    }
    else if (peak_filter_settings_.mode == PeakFilterMode::ExcludeRange)
    {
        modeKey = QStringLiteral("exclude_range");
    }
    settings.setValue("peak_filter/mode", modeKey);
    settings.setValue("peak_filter/min_value", peak_filter_settings_.min_value);
    settings.setValue("peak_filter/max_value", peak_filter_settings_.max_value);
    settings.setValue("peak_search/start_index", peak_search_start_index_);
    settings.setValue("peak_search/end_index", peak_search_end_index_);
}

void TcpWavePanel::setEnglish(bool english)
{
    is_english_ = english;
    if (panel_title_label_)
    {
        panel_title_label_->setText(english ? "TCP Wave Monitor" : "TCP波形监视");
    }
    resetFrameRateDisplay();
    host_label_->setText(english ? "TCP Host:" : "TCP主机:");
    port_label_->setText(english ? "Port:" : "端口:");
    connect_button_->setText(remote_sky_mode_
        ? (remote_wave_tcp_connected_ ? (english ? "Disconnect Sky Wave" : "断开天空波形")
                                      : (english ? "Connect Sky Wave" : "连接天空波形"))
        : (socket_ && socket_->state() == QAbstractSocket::ConnectedState
            ? (english ? "Disconnect" : "断开")
            : (english ? "Connect" : "连接")));
    wave1_group_->setTitle(QString());
    wave4_group_->setTitle(QString());
    peak_group_->setTitle(QString());
    if (wave1_title_label_)
    {
        wave1_title_label_->setText(english ? "Raw Signal" : "原始信号");
    }
    if (wave4_title_label_)
    {
        wave4_title_label_->setText(english ? "Normalized Second Harmonic" : "归一化二次谐波");
    }
    if (peak_title_label_)
    {
        peak_title_label_->setText(english
            ? QString("Normalized Second Harmonic Peak Trend (latest %1 frames)").arg(kPeakTrendFrameWindow)
            : QString("归一化二次谐波峰值趋势（最近%1帧）").arg(kPeakTrendFrameWindow));
    }
    if (wave1_plot_)
    {
        wave1_plot_->setEmptyText(english ? QStringLiteral("No data") : QStringLiteral("暂无数据"));
    }
    if (wave4_plot_)
    {
        wave4_plot_->setEmptyText(english ? QStringLiteral("No data") : QStringLiteral("暂无数据"));
    }
    if (peak_plot_)
    {
        peak_plot_->setEmptyText(english ? QStringLiteral("No peak data") : QStringLiteral("暂无峰值数据"));
    }
    if (peak_range_axis_)
    {
        peak_range_axis_->setEmptyText(english ? QStringLiteral("No range") : QStringLiteral("暂无数据"));
    }
    if (peak_clear_button_)
    {
        peak_clear_button_->setText(english ? "Clear Trend" : "清空趋势");
    }
    hint_label_->setText(english
        ? "This TCP sender is likely single-client. Do not open the LabVIEW VI receiver and VaporView on port 8888 at the same time."
        : "这个TCP发送端大概率只支持单客户端。不要同时打开 LabVIEW VI 接收端和 VaporView 去抢同一个 8888 连接。");

    wave1_info_label_->setText(english ? "waiting for raw-signal frame" : "等待原始信号数据帧");
    wave4_info_label_->setText(english ? "waiting for normalized second harmonic frame" : "等待归一化二次谐波数据帧");
    updatePeakFilterButtonText();
    updatePeakPlotModeButtonText();

    if (!socket_ || socket_->state() != QAbstractSocket::ConnectedState)
    {
        setStatusText(english ? "Idle" : "空闲");
    }
}

void TcpWavePanel::updatePeakPlotModeButtonText()
{
    if (!peak_mode_button_)
    {
        return;
    }

    peak_mode_button_->setText(peak_plot_scatter_mode_
        ? (is_english_ ? "Trend: Line" : "趋势显示：折线")
        : (is_english_ ? "Trend: Scatter" : "趋势显示：散点"));
}

QString TcpWavePanel::peakFilterModeText(PeakFilterMode mode) const
{
    switch (mode)
    {
    case PeakFilterMode::IqrOutlier:
        return is_english_ ? QStringLiteral("IQR") : QStringLiteral("IQR");
    case PeakFilterMode::KeepRange:
        return is_english_ ? QStringLiteral("Keep") : QStringLiteral("保留");
    case PeakFilterMode::ExcludeRange:
        return is_english_ ? QStringLiteral("Exclude") : QStringLiteral("排除");
    case PeakFilterMode::None:
    default:
        return is_english_ ? QStringLiteral("Off") : QStringLiteral("关闭");
    }
}

void TcpWavePanel::updatePeakFilterButtonText()
{
    if (!peak_filter_button_)
    {
        return;
    }

    const QString searchEndText = peak_search_end_index_ <= 0
        ? (is_english_ ? QStringLiteral("end") : QStringLiteral("末尾"))
        : QString::number(peak_search_end_index_);
    peak_filter_button_->setText(QStringLiteral("%1:%2-%3 / %4")
        .arg(is_english_ ? QStringLiteral("Search") : QStringLiteral("峰值搜索"))
        .arg(peak_search_start_index_)
        .arg(searchEndText)
        .arg(peakFilterModeText(peak_filter_settings_.mode)));
}

float TcpWavePanel::currentWaveformPeakValue(const QVector<float>& samples) const
{
    return waveformPeakValue(samples, peak_search_start_index_, peak_search_end_index_);
}

void TcpWavePanel::rebuildPeakHistory()
{
    peak_history_.clear();
    peak_history_.reserve(peak_raw_history_.size());

    QVector<double> finiteValues;
    finiteValues.reserve(peak_raw_history_.size());
    for (float value : peak_raw_history_)
    {
        if (std::isfinite(value))
        {
            finiteValues.push_back(static_cast<double>(value));
        }
    }

    double iqrLowerBound = -std::numeric_limits<double>::infinity();
    double iqrUpperBound = std::numeric_limits<double>::infinity();
    if (peak_filter_settings_.mode == PeakFilterMode::IqrOutlier && finiteValues.size() >= 4)
    {
        const double q1 = percentileValue(finiteValues, 0.25);
        const double q3 = percentileValue(finiteValues, 0.75);
        if (std::isfinite(q1) && std::isfinite(q3))
        {
            const double iqr = q3 - q1;
            const double padding = std::max(1e-6, iqr * 1.5);
            iqrLowerBound = q1 - padding;
            iqrUpperBound = q3 + padding;
        }
    }

    const double rangeMin = std::min(peak_filter_settings_.min_value, peak_filter_settings_.max_value);
    const double rangeMax = std::max(peak_filter_settings_.min_value, peak_filter_settings_.max_value);
    for (float rawValue : peak_raw_history_)
    {
        bool keepValue = std::isfinite(rawValue);
        if (keepValue)
        {
            const double value = static_cast<double>(rawValue);
            switch (peak_filter_settings_.mode)
            {
            case PeakFilterMode::IqrOutlier:
                keepValue = value >= iqrLowerBound && value <= iqrUpperBound;
                break;
            case PeakFilterMode::KeepRange:
                keepValue = value >= rangeMin && value <= rangeMax;
                break;
            case PeakFilterMode::ExcludeRange:
                keepValue = !(value >= rangeMin && value <= rangeMax);
                break;
            case PeakFilterMode::None:
            default:
                keepValue = true;
                break;
            }
        }

        peak_history_.push_back(keepValue
            ? rawValue
            : std::numeric_limits<float>::quiet_NaN());
    }

    live_display_dirty_ = true;
}

void TcpWavePanel::resetFrameRateDisplay()
{
    if (!frame_rate_label_)
    {
        return;
    }
    frame_rate_label_->setText(is_english_ ? "Realtime: -- Hz" : "实时频率: -- Hz");
}

void TcpWavePanel::updateFrameRateDisplay(qint64 arrivalTimeMs)
{
    if (!frame_rate_label_)
    {
        return;
    }

    frame_arrival_times_ms_.push_back(arrivalTimeMs);
    while (!frame_arrival_times_ms_.isEmpty() &&
           arrivalTimeMs - frame_arrival_times_ms_.front() > kFrameRateWindowMs)
    {
        frame_arrival_times_ms_.removeFirst();
    }

    double rateHz = 0.0;
    if (frame_arrival_times_ms_.size() >= 2)
    {
        const qint64 elapsedMs = frame_arrival_times_ms_.back() - frame_arrival_times_ms_.front();
        if (elapsedMs > 0)
        {
            rateHz = (frame_arrival_times_ms_.size() - 1) * 1000.0 / static_cast<double>(elapsedMs);
        }
    }

    frame_rate_label_->setText(QString(is_english_ ? "Realtime: %1 Hz" : "实时频率: %1 Hz")
        .arg(QString::number(rateHz, 'f', 2)));
}

void TcpWavePanel::updateLiveDisplay()
{
    if (!live_display_dirty_)
    {
        return;
    }

    live_display_dirty_ = false;
    if (wave1_plot_)
    {
        wave1_plot_->setSamples(wave1_history_);
    }
    if (wave4_plot_)
    {
        wave4_plot_->setSamples(wave4_history_);
    }
    if (peak_plot_)
    {
        peak_plot_->setPeakValues(peak_history_);
    }
    if (wave1_info_label_ && !pending_wave1_info_text_.isEmpty())
    {
        wave1_info_label_->setText(pending_wave1_info_text_);
    }
    if (wave4_info_label_ && !pending_wave4_info_text_.isEmpty())
    {
        wave4_info_label_->setText(pending_wave4_info_text_);
    }
    if (!pending_live_status_text_.isEmpty())
    {
        setStatusText(pending_live_status_text_);
    }
}

void TcpWavePanel::attachWaveformSplitControls(QLabel *label, QSpinBox *spinBox)
{
    if (!top_controls_layout_ || !label || !spinBox)
    {
        return;
    }

    label->setParent(this);
    spinBox->setParent(this);
    auto *splitRowLayout = new QHBoxLayout();
    splitRowLayout->setContentsMargins(0, 0, 0, 0);
    splitRowLayout->setSpacing(4);
    splitRowLayout->addWidget(label, 0, Qt::AlignVCenter | Qt::AlignRight);
    splitRowLayout->addWidget(spinBox, 0, Qt::AlignVCenter | Qt::AlignLeft);
    top_controls_layout_->insertSpacing(std::max(0, top_controls_layout_->count() - 2), 28);
    top_controls_layout_->insertLayout(std::max(0, top_controls_layout_->count() - 2), splitRowLayout, 0);
}

QString TcpWavePanel::host() const
{
    return host_edit_ ? host_edit_->text() : QStringLiteral("127.0.0.1");
}

int TcpWavePanel::port() const
{
    return port_spin_ ? port_spin_->value() : 8888;
}

bool TcpWavePanel::isConnected() const
{
    if (remote_sky_mode_)
    {
        return remote_wave_tcp_connected_;
    }
    return socket_ && socket_->state() == QAbstractSocket::ConnectedState;
}

void TcpWavePanel::setRemoteSkyMode(bool enabled)
{
    remote_sky_mode_ = enabled;
    if (host_edit_) host_edit_->setEnabled(!enabled);
    if (port_spin_) port_spin_->setEnabled(!enabled);
    if (enabled && socket_ && socket_->state() != QAbstractSocket::UnconnectedState)
    {
        requestGracefulDisconnect();
    }
    if (connect_button_)
    {
        connect_button_->setText(enabled
            ? (remote_wave_tcp_connected_ ? (is_english_ ? "Disconnect Sky Wave" : "断开天空波形")
                                          : (is_english_ ? "Connect Sky Wave" : "连接天空波形"))
                             : (isConnected() ? (is_english_ ? "Disconnect" : "断开")
                             : (is_english_ ? "Connect" : "连接")));
    }
    if (enabled && !remote_wave_tcp_connected_)
    {
        clearRemoteWaveformDisplay(is_english_ ? QStringLiteral("Sky Wave TCP is not connected")
                                               : QStringLiteral("天空端波形 TCP 未连接"));
    }
}

void TcpWavePanel::setRemoteWaveTcpState(VaporView::DeviceState state)
{
    const bool wasConnected = remote_wave_tcp_connected_;
    remote_wave_tcp_connected_ = state == VaporView::DeviceState::Connected;
    if (remote_sky_mode_ && connect_button_)
    {
        connect_button_->setText(remote_wave_tcp_connected_
            ? (is_english_ ? "Disconnect Sky Wave" : "断开天空波形")
            : (is_english_ ? "Connect Sky Wave" : "连接天空波形"));
    }
    if (remote_sky_mode_)
    {
        setStatusText(QString(is_english_ ? "Remote Sky wave TCP: %1" : "天空端波形 TCP：%1")
                          .arg(VaporView::deviceStateName(state)));
        if (!remote_wave_tcp_connected_ && wasConnected)
        {
            last_remote_feature_time_us_ = 0;
            clearRemoteWaveformDisplay(is_english_ ? QStringLiteral("Sky Wave TCP disconnected")
                                                   : QStringLiteral("天空端波形 TCP 已断开"));
        }
    }
}

void TcpWavePanel::setRemoteFeatureRateHz(double rateHz)
{
    remote_expected_feature_interval_us_ = rateHz > 0.0 && std::isfinite(rateHz)
        ? static_cast<quint64>(std::max(1.0, 1'000'000.0 / rateHz))
        : 0ULL;
}

void TcpWavePanel::injectRemoteRawSignalFrame(quint64 timestampUs, const QVector<float>& samples)
{
    Q_UNUSED(timestampUs);
    if (remote_sky_mode_ && !remote_wave_tcp_connected_)
    {
        return;
    }
    if (samples.isEmpty())
    {
        return;
    }
    wave1_history_ = samples;
    pending_wave1_info_text_ = QString(is_english_ ? "remote raw signal: %1 samples" : "远程原始信号：%1 点")
        .arg(samples.size());
    pending_live_status_text_ = is_english_ ? "Remote raw waveform received" : "已接收远程原始信号";
    live_display_dirty_ = true;
}

void TcpWavePanel::injectRemoteSecondHarmonicFrame(quint64 timestampUs, const QVector<float>& samples)
{
    if (remote_sky_mode_ && !remote_wave_tcp_connected_)
    {
        return;
    }
    if (samples.isEmpty())
    {
        return;
    }
    wave4_history_ = samples;
    ++frame_count_;
    updateFrameRateDisplay(QDateTime::currentMSecsSinceEpoch());
    pending_wave1_info_text_ = is_english_ ? "Remote Sky source" : "天空端远程源";
    pending_wave4_info_text_ = QString(is_english_ ? "%1 samples" : "%1 点").arg(samples.size());
    pending_live_status_text_ = is_english_ ? "Remote waveform received" : "已接收远程波形";
    live_display_dirty_ = true;
    emit normalizedSecondHarmonicFrameReady(timestampUs, wave4_history_);
}

void TcpWavePanel::injectRemoteWaveformFeature(const VaporView::WaveformFeature& feature)
{
    if (remote_sky_mode_ && !remote_wave_tcp_connected_)
    {
        return;
    }
    const bool validFeature = feature.quality_flags == 0 && feature.host_time_us > 0 && std::isfinite(feature.peak);
    if (!validFeature)
    {
        peak_raw_history_.push_back(std::numeric_limits<float>::quiet_NaN());
        last_remote_feature_time_us_ = feature.host_time_us;
        if (peak_raw_history_.size() > kPeakTrendFrameWindow)
        {
            peak_raw_history_.remove(0, peak_raw_history_.size() - kPeakTrendFrameWindow);
        }
        rebuildPeakHistory();
        pending_live_status_text_ = is_english_ ? "Remote feature missing or invalid; leaving a gap"
                                                : "远程特征值缺失或无效，峰值趋势留空";
        live_display_dirty_ = true;
        return;
    }
    peak_raw_history_.push_back(feature.peak);
    last_remote_feature_time_us_ = feature.host_time_us;
    if (peak_raw_history_.size() > kPeakTrendFrameWindow)
    {
        peak_raw_history_.remove(0, peak_raw_history_.size() - kPeakTrendFrameWindow);
    }
    rebuildPeakHistory();
    pending_live_status_text_ = QString(is_english_ ? "Remote feature: peak %1 rms %2" : "远程特征值：峰值 %1 RMS %2")
        .arg(feature.peak, 0, 'g', 4)
        .arg(feature.rms, 0, 'g', 4);
    if (feature.search_start_index > 0 || feature.search_end_index > 0)
    {
        pending_live_status_text_ += QString(is_english_ ? " | search [%1, %2), index %3" : " | 搜索区间 [%1, %2)，峰值下标 %3")
            .arg(feature.search_start_index)
            .arg(feature.search_end_index == 0 ? QStringLiteral("end") : QString::number(feature.search_end_index))
            .arg(feature.peak_index, 0, 'f', 0);
    }
    live_display_dirty_ = true;
}

void TcpWavePanel::applyRemotePeakSearchRange(quint32 startIndex, quint32 endIndex)
{
    peak_search_start_index_ = static_cast<int>(startIndex);
    peak_search_end_index_ = static_cast<int>(endIndex);
    peak_raw_history_.clear();
    peak_history_.clear();
    last_remote_feature_time_us_ = 0;
    if (peak_plot_)
    {
        peak_plot_->setPeakValues({});
    }
    saveRememberedInputState();
    updatePeakFilterButtonText();
    setStatusText(is_english_
        ? QStringLiteral("Peak search range accepted by sky. Waiting for the next feature frame.")
        : QStringLiteral("峰值搜索区间已下发到天空端，等待下一帧特征值。"));
}

void TcpWavePanel::rejectRemotePeakSearchRange(const QString& reason)
{
    setStatusText(is_english_
        ? QStringLiteral("Sky rejected peak search range: %1").arg(reason)
        : QStringLiteral("天空端拒绝峰值搜索区间：%1").arg(reason));
}

void TcpWavePanel::setupSocket()
{
    socket_ = new QTcpSocket(this);
    connect(socket_, &QTcpSocket::connected, this, &TcpWavePanel::onSocketConnected);
    connect(socket_, &QTcpSocket::disconnected, this, &TcpWavePanel::onSocketDisconnected);
    connect(socket_, &QTcpSocket::readyRead, this, &TcpWavePanel::onSocketReadyRead);
    connect(socket_, &QTcpSocket::stateChanged, this, [this](QAbstractSocket::SocketState) {
        onSocketStateChanged();
    });
    connect(socket_, &QAbstractSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        onSocketError();
    });
}

void TcpWavePanel::recreateSocket()
{
    if (socket_)
    {
        requestGracefulDisconnect();
        socket_->deleteLater();
        socket_ = nullptr;
    }
    setupSocket();
}

void TcpWavePanel::requestGracefulDisconnect()
{
    if (!socket_)
    {
        return;
    }

    if (socket_->state() == QAbstractSocket::ConnectedState)
    {
        socket_->disconnectFromHost();
        if (socket_->state() != QAbstractSocket::UnconnectedState)
        {
            socket_->waitForDisconnected(1000);
        }
    }
    else if (socket_->state() == QAbstractSocket::ConnectingState ||
             socket_->state() == QAbstractSocket::HostLookupState)
    {
        socket_->abort();
    }
}

void TcpWavePanel::onToggleConnectionClicked()
{
    if (remote_sky_mode_)
    {
        emit remoteWaveTcpConnectionRequested(!remote_wave_tcp_connected_);
        return;
    }

    if (socket_ && socket_->state() != QAbstractSocket::UnconnectedState)
    {
        requestGracefulDisconnect();
        return;
    }

    recreateSocket();
    buffer_.clear();
    wave1_history_.clear();
    wave4_history_.clear();
    peak_raw_history_.clear();
    peak_history_.clear();
    last_remote_feature_time_us_ = 0;
    pending_wave1_payload_.clear();
    pending_wave1_.clear();
    if (peak_plot_)
    {
        peak_plot_->setPeakValues({});
    }
    resetParserState();
    parse_mode_ = ParseMode::AutoDetect;
    header_byte_order_ = HeaderByteOrder::Unknown;
    float_encoding_ = FloatEncoding::Unknown;
    frame_count_ = 0;
    frame_arrival_times_ms_.clear();
    resetFrameRateDisplay();
    setStatusText(QString(is_english_ ? "Connecting to %1:%2..." : "正在连接 %1:%2...")
        .arg(host_edit_->text()).arg(port_spin_->value()));
    socket_->connectToHost(host_edit_->text(), static_cast<quint16>(port_spin_->value()));
    onSocketStateChanged();
}

void TcpWavePanel::onTogglePeakPlotModeClicked()
{
    peak_plot_scatter_mode_ = !peak_plot_scatter_mode_;
    updatePeakPlotModeButtonText();
    if (peak_plot_)
    {
        peak_plot_->setPlotMode(peak_plot_scatter_mode_ ? PeakTrendPlotWidget::PlotMode::Scatter : PeakTrendPlotWidget::PlotMode::Polyline);
    }
}

void TcpWavePanel::onClearPeakPlotClicked()
{
    peak_raw_history_.clear();
    peak_history_.clear();
    last_remote_feature_time_us_ = 0;
    if (peak_plot_)
    {
        peak_plot_->setPeakValues({});
    }
}

void TcpWavePanel::onConfigurePeakFilterClicked()
{
    QDialog dialog(this);
    dialog.setWindowTitle(is_english_ ? QStringLiteral("Peak Search Range") : QStringLiteral("峰值搜索区间"));

    auto *layout = new QVBoxLayout(&dialog);
    auto *formLayout = new QFormLayout();
    formLayout->setContentsMargins(0, 0, 0, 0);

    auto *searchStartSpin = new QSpinBox(&dialog);
    searchStartSpin->setRange(0, 10000000);
    searchStartSpin->setSingleStep(1000);
    searchStartSpin->setValue(peak_search_start_index_);
    formLayout->addRow(is_english_ ? QStringLiteral("Search Start") : QStringLiteral("搜索起点"), searchStartSpin);

    auto *searchEndSpin = new QSpinBox(&dialog);
    searchEndSpin->setRange(0, 10000000);
    searchEndSpin->setSingleStep(1000);
    searchEndSpin->setSpecialValueText(is_english_ ? QStringLiteral("Full Frame") : QStringLiteral("整帧"));
    searchEndSpin->setValue(std::max(0, peak_search_end_index_));
    formLayout->addRow(is_english_ ? QStringLiteral("Search End") : QStringLiteral("搜索终点"), searchEndSpin);

    auto *modeCombo = new QComboBox(&dialog);
    modeCombo->addItem(is_english_ ? QStringLiteral("Off") : QStringLiteral("关闭"), static_cast<int>(PeakFilterMode::None));
    modeCombo->addItem(is_english_ ? QStringLiteral("IQR Outlier Filter") : QStringLiteral("IQR 异常值过滤"), static_cast<int>(PeakFilterMode::IqrOutlier));
    modeCombo->addItem(is_english_ ? QStringLiteral("Keep Range") : QStringLiteral("保留区间"), static_cast<int>(PeakFilterMode::KeepRange));
    modeCombo->addItem(is_english_ ? QStringLiteral("Exclude Range") : QStringLiteral("排除区间"), static_cast<int>(PeakFilterMode::ExcludeRange));
    modeCombo->setCurrentIndex(std::max(0, modeCombo->findData(static_cast<int>(peak_filter_settings_.mode))));
    formLayout->addRow(is_english_ ? QStringLiteral("Trend Filter") : QStringLiteral("趋势过滤"), modeCombo);

    auto *minEdit = new QLineEdit(QString::number(peak_filter_settings_.min_value, 'f', 6), &dialog);
    auto *maxEdit = new QLineEdit(QString::number(peak_filter_settings_.max_value, 'f', 6), &dialog);
    formLayout->addRow(is_english_ ? QStringLiteral("Range Min") : QStringLiteral("区间最小值"), minEdit);
    formLayout->addRow(is_english_ ? QStringLiteral("Range Max") : QStringLiteral("区间最大值"), maxEdit);
    layout->addLayout(formLayout);

    auto *hintLabel = new QLabel(
        is_english_
            ? QStringLiteral("Peak search uses sample indexes [start, end). Search End = Full Frame uses all remaining samples. IQR removes statistical outliers. Keep Range keeps only values inside [min, max]. Exclude Range removes values inside [min, max]. If you change the search window, the existing live trend is cleared and new frames use the updated range.")
            : QStringLiteral("峰值搜索使用采样点下标 [起点, 终点)。搜索终点为“整帧”时表示一直搜索到本帧末尾。IQR 会过滤统计异常值。保留区间只保留 [最小值, 最大值] 内的峰值。排除区间会过滤 [最小值, 最大值] 内的峰值。修改搜索窗口后，已有实时趋势会清空，后续新帧按新区间计算。"),
        &dialog);
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    bool minOk = false;
    bool maxOk = false;
    const double minValue = minEdit->text().trimmed().toDouble(&minOk);
    const double maxValue = maxEdit->text().trimmed().toDouble(&maxOk);
    const int searchStart = searchStartSpin->value();
    const int searchEnd = searchEndSpin->value();
    const PeakFilterMode mode = static_cast<PeakFilterMode>(modeCombo->currentData().toInt());
    if (searchEnd > 0 && searchEnd <= searchStart)
    {
        QMessageBox::warning(
            this,
            is_english_ ? QStringLiteral("Peak Search Range") : QStringLiteral("峰值搜索区间"),
            is_english_ ? QStringLiteral("Search End must be greater than Search Start, or set to Full Frame.") : QStringLiteral("搜索终点必须大于搜索起点，或者设置为整帧。"));
        return;
    }
    if ((mode == PeakFilterMode::KeepRange || mode == PeakFilterMode::ExcludeRange) && (!minOk || !maxOk))
    {
        QMessageBox::warning(
            this,
            is_english_ ? QStringLiteral("Trend Filter") : QStringLiteral("趋势过滤"),
            is_english_ ? QStringLiteral("Please enter valid numeric range values.") : QStringLiteral("请输入有效的数值区间。"));
        return;
    }

    const bool peakSearchChanged =
        peak_search_start_index_ != searchStart ||
        peak_search_end_index_ != searchEnd;
    peak_filter_settings_.mode = mode;
    if (minOk)
    {
        peak_filter_settings_.min_value = minValue;
    }
    if (maxOk)
    {
        peak_filter_settings_.max_value = maxValue;
    }

    if (peakSearchChanged)
    {
        if (remote_sky_mode_)
        {
            saveRememberedInputState();
            updatePeakFilterButtonText();
            emit remotePeakSearchRangeRequested(static_cast<quint32>(searchStart), static_cast<quint32>(searchEnd));
            setStatusText(is_english_
                ? QStringLiteral("Remote Sky: peak search range sent to sky; waiting for ACK.")
                : QStringLiteral("Remote Sky 模式：峰值搜索区间已发送到天空端，等待 ACK。"));
        }
        else
        {
            peak_search_start_index_ = searchStart;
            peak_search_end_index_ = searchEnd;
            peak_raw_history_.clear();
            peak_history_.clear();
            last_remote_feature_time_us_ = 0;
            if (peak_plot_)
            {
                peak_plot_->setPeakValues({});
            }
            saveRememberedInputState();
            updatePeakFilterButtonText();
            setStatusText(is_english_
                ? QStringLiteral("Peak search range updated. Existing live peak trend was cleared; new frames will use the new range.")
                : QStringLiteral("峰值搜索区间已更新，现有实时峰值趋势已清空；后续新帧将按新区间计算。"));
        }
    }
    else
    {
        saveRememberedInputState();
        updatePeakFilterButtonText();
        rebuildPeakHistory();
    }
}

void TcpWavePanel::onSocketConnected()
{
    setConnectedUiState(true);
    emit connectionStateChanged(true);
    setStatusText(QString(is_english_
        ? "Connected to %1:%2, waiting for the first frame..."
        : "已连接到 %1:%2，正在等待首帧数据...")
        .arg(host_edit_->text()).arg(port_spin_->value()));
}

void TcpWavePanel::onSocketDisconnected()
{
    setConnectedUiState(false);
    emit connectionStateChanged(false);
    if (frame_count_ > 0)
    {
        setStatusText(QString(is_english_
            ? "Disconnected after receiving %1 frames"
            : "已断开，本次共接收 %1 帧").arg(frame_count_));
    }
    else
    {
        setStatusText(is_english_ ? "Disconnected without receiving any frame" : "已断开，本次未收到任何数据帧");
    }
    frame_arrival_times_ms_.clear();
    resetFrameRateDisplay();
}

void TcpWavePanel::onSocketReadyRead()
{
    buffer_.append(socket_->readAll());
    processBuffer();
}

void TcpWavePanel::onSocketStateChanged()
{
    if (!socket_)
    {
        setConnectedUiState(false);
        return;
    }

    switch (socket_->state())
    {
    case QAbstractSocket::HostLookupState:
    case QAbstractSocket::ConnectingState:
        host_edit_->setEnabled(false);
        port_spin_->setEnabled(false);
        connect_button_->setEnabled(false);
        connect_button_->setText(is_english_ ? "Connecting..." : "连接中...");
        setStatusText(QString(is_english_ ? "Connecting to %1:%2..." : "正在连接 %1:%2...")
            .arg(host_edit_->text()).arg(port_spin_->value()));
        break;
    case QAbstractSocket::ConnectedState:
        setConnectedUiState(true);
        break;
    case QAbstractSocket::ClosingState:
        setConnectedUiState(true);
        setStatusText(is_english_ ? "Disconnecting..." : "正在断开...");
        break;
    case QAbstractSocket::UnconnectedState:
    default:
        setConnectedUiState(false);
        break;
    }
}

void TcpWavePanel::onSocketError()
{
    if (!socket_)
    {
        return;
    }
    setStatusText(socket_->errorString());
    if (socket_->state() == QAbstractSocket::UnconnectedState)
    {
        setConnectedUiState(false);
    }
}

void TcpWavePanel::setConnectedUiState(bool connected)
{
    const bool active = connected && socket_ && socket_->state() != QAbstractSocket::UnconnectedState;
    host_edit_->setEnabled(!active);
    port_spin_->setEnabled(!active);
    if (socket_ && socket_->state() == QAbstractSocket::ClosingState)
    {
        connect_button_->setText(is_english_ ? "Disconnecting..." : "正在断开...");
        connect_button_->setEnabled(false);
        return;
    }

    connect_button_->setEnabled(true);
    connect_button_->setText(active ? (is_english_ ? "Disconnect" : "断开") : (is_english_ ? "Connect" : "连接"));
}

void TcpWavePanel::setStatusText(const QString& text)
{
    status_label_->setText(text);
}

void TcpWavePanel::clearRemoteWaveformDisplay(const QString& statusText)
{
    wave1_history_.clear();
    wave4_history_.clear();
    peak_raw_history_.clear();
    peak_history_.clear();
    last_remote_feature_time_us_ = 0;
    frame_arrival_times_ms_.clear();
    frame_count_ = 0;
    pending_wave1_payload_.clear();
    pending_wave1_.clear();
    pending_wave1_info_text_.clear();
    pending_wave4_info_text_.clear();
    pending_live_status_text_.clear();
    if (wave1_plot_) wave1_plot_->setSamples({});
    if (wave4_plot_) wave4_plot_->setSamples({});
    if (peak_plot_) peak_plot_->setPeakValues({});
    if (wave1_info_label_) wave1_info_label_->setText(QStringLiteral("--"));
    if (wave4_info_label_) wave4_info_label_->setText(QStringLiteral("--"));
    resetFrameRateDisplay();
    if (!statusText.isEmpty())
    {
        setStatusText(statusText);
    }
    live_display_dirty_ = false;
}

void TcpWavePanel::resetParserState()
{
    read_state_ = ReadState::Wave1Header;
    expected_payload_size_ = 0;
}

void TcpWavePanel::processBuffer()
{
    while (true)
    {
        switch (read_state_)
        {
        case ReadState::Wave1Header:
            if (!tryConsumeHeader())
            {
                return;
            }
            read_state_ = ReadState::Wave1Payload;
            break;
        case ReadState::Wave1Payload:
            if (!tryConsumePayload(pending_wave1_, &pending_wave1_payload_))
            {
                return;
            }
            read_state_ = ReadState::Wave4Header;
            break;
        case ReadState::Wave4Header:
            if (!tryConsumeHeader())
            {
                return;
            }
            read_state_ = ReadState::Wave4Payload;
            break;
        case ReadState::Wave4Payload:
        {
            QVector<float> wave4;
            QByteArray wave4Payload;
            if (!tryConsumePayload(wave4, &wave4Payload))
            {
                return;
            }

            double wave1MaxMagnitude = 0.0;
            double wave4MaxMagnitude = 0.0;
            if (!isReasonableWavePayload(pending_wave1_, kMaxReasonableWaveMagnitude, &wave1MaxMagnitude) ||
                !isReasonableWavePayload(wave4, kMaxReasonableWaveMagnitude, &wave4MaxMagnitude))
            {
                setStatusText(QString(is_english_
                    ? "Dropped abnormal TCP frame, raw max=%1, harmonic max=%2. Waiting for next frame..."
                    : "已丢弃异常TCP帧，原始信号最大值=%1，二次谐波最大值=%2，等待下一帧...")
                    .arg(formatWaveValue(wave1MaxMagnitude, 3))
                    .arg(formatWaveValue(wave4MaxMagnitude, 3)));
                pending_wave1_payload_.clear();
                pending_wave1_.clear();
                float_encoding_ = FloatEncoding::Unknown;
                resetParserState();
                break;
            }

            wave1_history_ = std::move(pending_wave1_);
            wave4_history_ = std::move(wave4);
            const float rawPeakValue = currentWaveformPeakValue(wave4_history_);
            peak_raw_history_.push_back(rawPeakValue);
            if (peak_raw_history_.size() > kPeakTrendFrameWindow)
            {
                peak_raw_history_.remove(0, peak_raw_history_.size() - kPeakTrendFrameWindow);
            }
            rebuildPeakHistory();
            const float displayedPeakValue = peak_history_.isEmpty()
                ? rawPeakValue
                : peak_history_.back();
            ++frame_count_;
            updateFrameRateDisplay(QDateTime::currentMSecsSinceEpoch());
            const quint64 frameTimestampUs = static_cast<quint64>(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) * 1000ULL;
            emit rawWaveFrameReady(frameTimestampUs, pending_wave1_payload_, wave4Payload, float_encoding_);
            emit normalizedSecondHarmonicFrameReady(
                frameTimestampUs,
                wave4_history_);

            const auto describeRange = [](const QVector<float>& values) {
                if (values.isEmpty())
                {
                    return QStringLiteral("min=0.000 max=0.000");
                }
                const auto [minIt, maxIt] = std::minmax_element(values.cbegin(), values.cend());
                return QString("min=%1 max=%2")
                    .arg(formatWaveValue(*minIt, 6))
                    .arg(formatWaveValue(*maxIt, 6));
            };

            pending_wave1_info_text_ = QString(is_english_
                ? "raw signal: %1 samples, %2"
                : "原始信号: %1 个采样点，%2")
                .arg(wave1_history_.size())
                .arg(describeRange(wave1_history_));
            pending_wave4_info_text_ = QString(is_english_
                ? "normalized second harmonic: %1 samples, %2, %3"
                : "归一化二次谐波: %1 个采样点，%2，%3")
                .arg(wave4_history_.size())
                .arg(describeRange(wave4_history_))
                .arg([this, rawPeakValue, displayedPeakValue]() {
                    const QString rawPeakText = std::isfinite(rawPeakValue)
                        ? formatWaveValue(rawPeakValue, 6)
                        : QStringLiteral("--");
                    const QString displayedPeakText = std::isfinite(displayedPeakValue)
                        ? formatWaveValue(displayedPeakValue, 6)
                        : (is_english_ ? QStringLiteral("filtered") : QStringLiteral("已过滤"));
                    if (peak_filter_settings_.mode == PeakFilterMode::None ||
                        (!std::isfinite(rawPeakValue) && !std::isfinite(displayedPeakValue)) ||
                        (std::isfinite(rawPeakValue) && std::isfinite(displayedPeakValue) &&
                         std::fabs(static_cast<double>(rawPeakValue) - static_cast<double>(displayedPeakValue)) < 1e-9))
                    {
                        return QString(is_english_ ? "peak=%1" : "峰值=%1").arg(rawPeakText);
                    }
                    return QString(is_english_ ? "peak(raw/show)=%1/%2" : "峰值(原始/显示)=%1/%2")
                        .arg(rawPeakText, displayedPeakText);
                }());

            pending_live_status_text_ = QString(is_english_
                ? "Receiving frame %3 from %1:%2, float format: %4"
                : "正在接收来自 %1:%2 的数据帧，第 %3 帧，浮点格式: %4")
                .arg(host_edit_->text())
                .arg(port_spin_->value())
                .arg(frame_count_)
                .arg(VaporView::tcpFloatEncodingLabel(is_english_, float_encoding_));
            live_display_dirty_ = true;

            pending_wave1_payload_.clear();
            pending_wave1_.clear();
            resetParserState();
            break;
        }
        }
    }
}

bool TcpWavePanel::trySynchronizeLengthPrefixedStream()
{
    if (buffer_.size() < kHeaderSize)
    {
        return false;
    }

    if (header_byte_order_ != HeaderByteOrder::Unknown)
    {
        const qint32 candidate = decodeHeaderValue(buffer_.constData(), header_byte_order_);
        if (isValidPayloadSize(candidate))
        {
            return true;
        }
    }

    const HeaderByteOrder orders[] = {
        HeaderByteOrder::LittleEndian,
        HeaderByteOrder::BigEndian,
    };
    for (int offset = 0; offset <= buffer_.size() - kHeaderSize; ++offset)
    {
        for (HeaderByteOrder order : orders)
        {
            const qint32 firstPayloadSize = decodeHeaderValue(buffer_.constData() + offset, order);
            if (!isValidPayloadSize(firstPayloadSize))
            {
                continue;
            }

            const bool preferredSize = firstPayloadSize == kPreferredPayloadBytes;
            const int secondHeaderOffset = offset + kHeaderSize + firstPayloadSize;
            const bool canValidateSecondHeader = secondHeaderOffset + kHeaderSize <= buffer_.size();
            const bool secondHeaderValid = canValidateSecondHeader
                && isValidPayloadSize(decodeHeaderValue(buffer_.constData() + secondHeaderOffset, order));
            if (!preferredSize && !secondHeaderValid)
            {
                continue;
            }

            if (offset > 0)
            {
                setStatusText(QString(is_english_
                    ? "Recovered TCP frame boundary after skipping %1 bytes, header order: %2"
                    : "已跳过 %1 字节并重新找到TCP帧边界，帧头字节序: %2")
                    .arg(offset)
                    .arg(headerOrderLabel(is_english_, order)));
                buffer_.remove(0, offset);
            }
            else if (parse_mode_ == ParseMode::AutoDetect)
            {
                setStatusText(QString(is_english_
                    ? "Detected length-prefixed TCP payloads, header order: %1"
                    : "已识别为长度前缀TCP负载格式，帧头字节序: %1")
                    .arg(headerOrderLabel(is_english_, order)));
            }

            header_byte_order_ = order;
            parse_mode_ = ParseMode::LengthPrefixed;
            return true;
        }
    }

    const qint32 little = decodeHeaderValue(buffer_.constData(), HeaderByteOrder::LittleEndian);
    const qint32 big = decodeHeaderValue(buffer_.constData(), HeaderByteOrder::BigEndian);
    setStatusText(QString(is_english_
        ? "Waiting for a valid TCP frame boundary, bytes: %1, little=%2, big=%3"
        : "正在等待有效的TCP帧边界，字节预览：%1，小端=%2，大端=%3")
        .arg(hexPreview(buffer_))
        .arg(little)
        .arg(big));
    return false;
}

bool TcpWavePanel::isValidPayloadSize(qint32 candidate) const
{
    return candidate > 0 && candidate <= kMaxPayloadBytes && (candidate % kFloatSize) == 0;
}

qint32 TcpWavePanel::decodeHeaderValue(const char *raw, HeaderByteOrder order) const
{
    const uchar *bytes = reinterpret_cast<const uchar*>(raw);
    switch (order)
    {
    case HeaderByteOrder::LittleEndian:
        return qFromLittleEndian<qint32>(bytes);
    case HeaderByteOrder::BigEndian:
        return qFromBigEndian<qint32>(bytes);
    case HeaderByteOrder::Unknown:
    default:
        return qFromLittleEndian<qint32>(bytes);
    }
}

bool TcpWavePanel::tryConsumeHeader()
{
    if (!trySynchronizeLengthPrefixedStream())
    {
        return false;
    }

    const qint32 candidate = decodeHeaderValue(buffer_.constData(), header_byte_order_);
    if (!isValidPayloadSize(candidate))
    {
        setStatusText(QString(is_english_
            ? "Unexpected TCP frame header after resync (%1), bytes: %2"
            : "重同步后TCP帧头仍异常（%1），字节预览：%2")
            .arg(candidate)
            .arg(hexPreview(buffer_)));
        return false;
    }

    expected_payload_size_ = candidate;
    buffer_.remove(0, kHeaderSize);
    return true;
}

bool TcpWavePanel::tryConsumePayload(QVector<float>& output, QByteArray *rawPayload)
{
    if (buffer_.size() < expected_payload_size_)
    {
        return false;
    }

    const QByteArray payload = buffer_.left(expected_payload_size_);
    buffer_.remove(0, expected_payload_size_);
    if (rawPayload)
    {
        *rawPayload = payload;
    }
    if (float_encoding_ == FloatEncoding::Unknown)
    {
        float_encoding_ = VaporView::autoDetectTcpFloatEncoding(payload);
        setStatusText(QString(is_english_
            ? "Detected float payload format: %1"
            : "已识别浮点负载格式: %1")
            .arg(VaporView::tcpFloatEncodingLabel(is_english_, float_encoding_)));
    }
    output = decodeFloatPayload(payload);
    expected_payload_size_ = 0;
    return true;
}

QVector<float> TcpWavePanel::decodeFloatPayload(const QByteArray& payload) const
{
    const FloatEncoding encoding = float_encoding_ == FloatEncoding::Unknown
        ? FloatEncoding::LittleEndian
        : float_encoding_;
    return VaporView::decodeTcpFloatPayload(payload, encoding);
}
