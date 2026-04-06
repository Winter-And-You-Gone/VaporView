#include "TcpWavePanel.h"
#include <QAbstractSocket>
#include <QByteArray>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPolygonF>
#include <QPushButton>
#include <QSpinBox>
#include <QTcpSocket>
#include <QVBoxLayout>
#include <QDateTime>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
constexpr int kHeaderSize = 4;
constexpr int kFloatSize = 4;
constexpr int kMaxPayloadBytes = 16 * 1024 * 1024;
constexpr int kPreferredPayloadBytes = 200000;
constexpr int kTcpControlHeight = 30;
constexpr int kTcpButtonHeight = 38;

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

QString floatEncodingLabel(bool english, TcpWavePanel::FloatEncoding encoding)
{
    switch (encoding)
    {
    case TcpWavePanel::FloatEncoding::LittleEndian:
        return english ? "little-endian float32" : "小端 float32";
    case TcpWavePanel::FloatEncoding::BigEndian:
        return english ? "big-endian float32" : "大端 float32";
    case TcpWavePanel::FloatEncoding::WordSwappedLittleEndian:
        return english ? "word-swapped float32" : "16位字交换 float32";
    case TcpWavePanel::FloatEncoding::Unknown:
    default:
        return english ? "unknown float32" : "未知 float32";
    }
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

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF cardRect = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(QPen(QColor("#dddddd"), 1));
        painter.setBrush(QColor("#fbfbfb"));
        painter.drawRoundedRect(cardRect, 14, 14);

        if (samples_.isEmpty())
        {
            const QRectF emptyPlotRect = cardRect.adjusted(18, 8, -2, -18);
            painter.setPen(QPen(QColor("#d7dfe8"), 1));
            painter.drawRect(emptyPlotRect);
            painter.setPen(QColor("#6a6a6a"));
            painter.drawText(emptyPlotRect, Qt::AlignCenter, tr("No data"));
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

        const QString maxLabel = QString::number(maxValue, 'f', 3);
        const QString midLabel = QString::number((maxValue + minValue) * 0.5, 'f', 3);
        const QString minLabel = QString::number(minValue, 'f', 3);
        const QFontMetrics fm = painter.fontMetrics();
        const int labelWidth = std::max({fm.horizontalAdvance(maxLabel), fm.horizontalAdvance(midLabel), fm.horizontalAdvance(minLabel)});
        const int leftMargin = std::max(18, labelWidth + 4);
        const int bottomMargin = fm.height() + 2;
        const QRectF plotRect = cardRect.adjusted(leftMargin, 10, -6, -bottomMargin - 4);

        painter.setPen(QPen(QColor("#e6ebf2"), 1));
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

        painter.setPen(QPen(QColor("#d4dce6"), 1));
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

        painter.setPen(QPen(line_color_, 1.6));
        painter.drawPolyline(polyline);

        painter.setPen(QColor("#5e5e5e"));
        painter.drawText(QRectF(2, plotRect.top() - 2, leftMargin - 4, fm.height()), Qt::AlignRight | Qt::AlignVCenter, maxLabel);
        painter.drawText(QRectF(2, plotRect.center().y() - fm.height() * 0.5, leftMargin - 4, fm.height()), Qt::AlignRight | Qt::AlignVCenter, midLabel);
        painter.drawText(QRectF(2, plotRect.bottom() - fm.height() + 2, leftMargin - 4, fm.height()), Qt::AlignRight | Qt::AlignVCenter, minLabel);
        painter.drawText(QRectF(plotRect.left(), plotRect.bottom() + 2, plotRect.width(), fm.height()), Qt::AlignRight | Qt::AlignVCenter,
                         QString("%1 samples").arg(sampleCount));
    }

private:
    QColor line_color_;
    QVector<float> samples_;
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
    {
        setMinimumHeight(150);
        setMaximumHeight(190);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    void setPeakValues(const QVector<float>& values)
    {
        peak_values_ = values;
        update();
    }

    void setPlotMode(PlotMode mode)
    {
        plot_mode_ = mode;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF cardRect = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(QPen(QColor("#dddddd"), 1));
        painter.setBrush(QColor("#fbfbfb"));
        painter.drawRoundedRect(cardRect, 14, 14);

        if (peak_values_.isEmpty())
        {
            const QRectF emptyPlotRect = cardRect.adjusted(18, 8, -2, -18);
            painter.setPen(QPen(QColor("#d7dfe8"), 1));
            painter.drawRect(emptyPlotRect);
            painter.setPen(QColor("#6a6a6a"));
            painter.drawText(emptyPlotRect, Qt::AlignCenter, QObject::tr("No peak data"));
            return;
        }

        auto [minIt, maxIt] = std::minmax_element(peak_values_.cbegin(), peak_values_.cend());
        float minValue = *minIt;
        float maxValue = *maxIt;
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
        const QRectF plotRect = cardRect.adjusted(leftMargin, 10, -6, -bottomMargin - 4);

        painter.setPen(QPen(QColor("#e6ebf2"), 1));
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

        painter.setPen(QPen(QColor("#d4dce6"), 1));
        painter.drawRect(plotRect);

        QVector<QPointF> points;
        points.reserve(peak_values_.size());
        for (int i = 0; i < peak_values_.size(); ++i)
        {
            const double ratio = peak_values_.size() == 1 ? 0.5 : static_cast<double>(i) / static_cast<double>(peak_values_.size() - 1);
            const float value = peak_values_.at(i);
            const double normalized = (value - minValue) / std::max(1e-6f, maxValue - minValue);
            points.push_back(QPointF(plotRect.left() + ratio * plotRect.width(),
                                     plotRect.bottom() - normalized * plotRect.height()));
        }

        const QColor seriesColor("#f7630c");
        if (plot_mode_ == PlotMode::Polyline && points.size() >= 2)
        {
            painter.setPen(QPen(seriesColor, 1.5));
            painter.drawPolyline(QPolygonF(points));
        }
        else
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(seriesColor);
            for (const QPointF& point : points)
            {
                painter.drawEllipse(point, 2.5, 2.5);
            }
        }

        painter.setPen(QColor("#5e5e5e"));
        painter.drawText(QRectF(2, plotRect.top() - 2, leftMargin - 4, fm.height()), Qt::AlignRight | Qt::AlignVCenter, maxLabel);
        painter.drawText(QRectF(2, plotRect.center().y() - fm.height() * 0.5, leftMargin - 4, fm.height()), Qt::AlignRight | Qt::AlignVCenter, midLabel);
        painter.drawText(QRectF(2, plotRect.bottom() - fm.height() + 2, leftMargin - 4, fm.height()), Qt::AlignRight | Qt::AlignVCenter, minLabel);
        painter.drawText(QRectF(plotRect.left(), plotRect.bottom() + 2, plotRect.width(), fm.height()), Qt::AlignRight | Qt::AlignVCenter,
                         QString("%1 frames").arg(peak_values_.size()));
    }

private:
    QVector<float> peak_values_;
    PlotMode plot_mode_;
};

TcpWavePanel::TcpWavePanel(QWidget *parent)
    : QWidget(parent)
    , host_edit_(nullptr)
    , port_spin_(nullptr)
    , connect_button_(nullptr)
    , host_label_(nullptr)
    , port_label_(nullptr)
    , panel_title_label_(nullptr)
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
    , peak_mode_button_(nullptr)
    , peak_clear_button_(nullptr)
    , control_layout_(nullptr)
    , socket_(nullptr)
    , peak_plot_scatter_mode_(true)
    , parse_mode_(ParseMode::AutoDetect)
    , read_state_(ReadState::Wave1Header)
    , header_byte_order_(HeaderByteOrder::Unknown)
    , float_encoding_(FloatEncoding::Unknown)
    , expected_payload_size_(0)
    , frame_count_(0)
    , is_english_(false)
{
    setupUi();
    setupSocket();
    setEnglish(false);
}

TcpWavePanel::~TcpWavePanel()
{
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
    mainLayout->setContentsMargins(2, 2, 2, 2);
    mainLayout->setSpacing(10);

    control_layout_ = new QGridLayout();
    control_layout_->setHorizontalSpacing(1);
    control_layout_->setVerticalSpacing(4);

    panel_title_label_ = new QLabel(this);
    panel_title_label_->setObjectName("sectionTitleLabel");
    control_layout_->addWidget(panel_title_label_, 0, 0, Qt::AlignVCenter | Qt::AlignLeft);

    auto *hostRowLayout = new QHBoxLayout();
    hostRowLayout->setContentsMargins(0, 0, 0, 0);
    hostRowLayout->setSpacing(1);
    host_label_ = new QLabel(this);
    host_label_->setObjectName("fieldLabel");
    hostRowLayout->addWidget(host_label_, 0, Qt::AlignVCenter | Qt::AlignRight);

    host_edit_ = new QLineEdit(this);
    host_edit_->setText("127.0.0.1");
    host_edit_->setFixedHeight(kTcpControlHeight);
    host_edit_->setMinimumWidth(90);
    host_edit_->setMaximumWidth(110);
    hostRowLayout->addWidget(host_edit_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    control_layout_->addLayout(hostRowLayout, 0, 1, Qt::AlignVCenter | Qt::AlignLeft);

    auto *portRowLayout = new QHBoxLayout();
    portRowLayout->setContentsMargins(0, 0, 0, 0);
    portRowLayout->setSpacing(1);
    port_label_ = new QLabel(this);
    port_label_->setObjectName("fieldLabel");
    portRowLayout->addWidget(port_label_, 0, Qt::AlignVCenter | Qt::AlignRight);

    port_spin_ = new QSpinBox(this);
    port_spin_->setRange(1, 65535);
    port_spin_->setValue(8888);
    port_spin_->setFixedHeight(kTcpControlHeight);
    port_spin_->setFixedWidth(108);
    portRowLayout->addWidget(port_spin_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    control_layout_->addLayout(portRowLayout, 0, 2, Qt::AlignVCenter | Qt::AlignLeft);

    connect_button_ = new QPushButton(this);
    connect_button_->setObjectName("compactTcpStartButton");
    connect_button_->setFixedHeight(kTcpButtonHeight);
    connect(connect_button_, &QPushButton::clicked, this, &TcpWavePanel::onToggleConnectionClicked);
    control_layout_->addWidget(connect_button_, 0, 4, Qt::AlignVCenter | Qt::AlignLeft);

    status_label_ = new QLabel(this);
    status_label_->setObjectName("fieldLabel");
    status_label_->setWordWrap(true);
    control_layout_->addWidget(status_label_, 1, 0, 1, 5);

    hint_label_ = new QLabel(this);
    hint_label_->setObjectName("fieldLabel");
    hint_label_->setWordWrap(true);
    control_layout_->addWidget(hint_label_, 2, 0, 1, 5);

    mainLayout->addLayout(control_layout_);

    auto *plotsLayout = new QHBoxLayout();
    plotsLayout->setSpacing(1);

    wave1_group_ = new QGroupBox(this);
    wave1_group_->setObjectName("cardSurface");
    auto *wave1Layout = new QVBoxLayout(wave1_group_);
    wave1Layout->setContentsMargins(12, 10, 12, 12);
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
    wave1_plot_ = new WavePlotWidget(QColor("#0078d4"), this);
    wave1Layout->addWidget(wave1_plot_, 1);
    plotsLayout->addWidget(wave1_group_, 1);

    wave4_group_ = new QGroupBox(this);
    wave4_group_->setObjectName("cardSurface");
    auto *wave4Layout = new QVBoxLayout(wave4_group_);
    wave4Layout->setContentsMargins(12, 10, 12, 12);
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
    wave4_plot_ = new WavePlotWidget(QColor("#f7630c"), this);
    wave4Layout->addWidget(wave4_plot_, 1);
    plotsLayout->addWidget(wave4_group_, 1);

    mainLayout->addLayout(plotsLayout, 1);

    peak_group_ = new QGroupBox(this);
    peak_group_->setObjectName("cardSurface");
    peak_group_->setMinimumHeight(198);
    peak_group_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
    auto *peakLayout = new QVBoxLayout(peak_group_);
    peakLayout->setContentsMargins(12, 10, 12, 12);
    auto *peakHeaderLayout = new QHBoxLayout();
    peakHeaderLayout->setContentsMargins(0, 0, 0, 0);
    peakHeaderLayout->setSpacing(6);
    peak_title_label_ = new QLabel(this);
    peak_title_label_->setObjectName("sectionTitleLabel");
    peakHeaderLayout->addWidget(peak_title_label_, 0, Qt::AlignVCenter | Qt::AlignLeft);
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
    mainLayout->addWidget(peak_group_, 0);
}

void TcpWavePanel::setEnglish(bool english)
{
    is_english_ = english;
    if (panel_title_label_)
    {
        panel_title_label_->setText(english ? "TCP Wave Monitor" : "TCP波形监视");
    }
    host_label_->setText(english ? "TCP Host:" : "TCP主机:");
    port_label_->setText(english ? "Port:" : "端口:");
    connect_button_->setText(socket_ && socket_->state() == QAbstractSocket::ConnectedState
        ? (english ? "Stop" : "停止")
        : (english ? "Start" : "启动"));
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
        peak_title_label_->setText(english ? "Normalized Second Harmonic Peak Trend" : "归一化二次谐波峰值趋势");
    }
    if (peak_clear_button_)
    {
        peak_clear_button_->setText(english ? "Clear" : "清空");
    }
    hint_label_->setText(english
        ? "This TCP sender is likely single-client. Do not open the LabVIEW VI receiver and VaporView on port 8888 at the same time."
        : "这个TCP发送端大概率只支持单客户端。不要同时打开 LabVIEW VI 接收端和 VaporView 去抢同一个 8888 连接。");

    wave1_info_label_->setText(english ? "waiting for raw-signal frame" : "等待原始信号数据帧");
    wave4_info_label_->setText(english ? "waiting for normalized second harmonic frame" : "等待归一化二次谐波数据帧");
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
        ? (is_english_ ? "Show Polyline" : "切换到折线图")
        : (is_english_ ? "Show Scatter" : "切换到散点图"));
}

void TcpWavePanel::attachWaveformSplitControls(QLabel *label, QSpinBox *spinBox)
{
    if (!control_layout_ || !label || !spinBox)
    {
        return;
    }

    label->setParent(this);
    spinBox->setParent(this);
    auto *splitRowLayout = new QHBoxLayout();
    splitRowLayout->setContentsMargins(0, 0, 0, 0);
    splitRowLayout->setSpacing(1);
    splitRowLayout->addWidget(label, 0, Qt::AlignVCenter | Qt::AlignRight);
    splitRowLayout->addWidget(spinBox, 0, Qt::AlignVCenter | Qt::AlignLeft);
    control_layout_->addLayout(splitRowLayout, 0, 3, Qt::AlignVCenter | Qt::AlignLeft);
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
    return socket_ && socket_->state() == QAbstractSocket::ConnectedState;
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
    if (socket_ && socket_->state() != QAbstractSocket::UnconnectedState)
    {
        requestGracefulDisconnect();
        return;
    }

    recreateSocket();
    buffer_.clear();
    wave1_history_.clear();
    wave4_history_.clear();
    peak_history_.clear();
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
    peak_history_.clear();
    if (peak_plot_)
    {
        peak_plot_->setPeakValues({});
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
    connect_button_->setText(active ? (is_english_ ? "Stop" : "停止") : (is_english_ ? "Start" : "启动"));
}

void TcpWavePanel::setStatusText(const QString& text)
{
    status_label_->setText(text);
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
            if (!tryConsumePayload(pending_wave1_))
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
            if (!tryConsumePayload(wave4))
            {
                return;
            }
            wave1_history_ = pending_wave1_;
            wave4_history_ = wave4;
            wave1_plot_->setSamples(wave1_history_);
            wave4_plot_->setSamples(wave4_history_);
            const auto peakIt = std::max_element(wave4_history_.cbegin(), wave4_history_.cend());
            peak_history_.push_back(peakIt == wave4_history_.cend() ? 0.0f : *peakIt);
            if (peak_plot_)
            {
                peak_plot_->setPeakValues(peak_history_);
            }
            ++frame_count_;
            emit normalizedSecondHarmonicFrameReady(
                static_cast<quint64>(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) * 1000ULL,
                wave4_history_);

            const auto describeRange = [](const QVector<float>& values) {
                if (values.isEmpty())
                {
                    return QStringLiteral("min=0.000 max=0.000");
                }
                const auto [minIt, maxIt] = std::minmax_element(values.cbegin(), values.cend());
                return QString("min=%1 max=%2")
                    .arg(*minIt, 0, 'f', 6)
                    .arg(*maxIt, 0, 'f', 6);
            };

            wave1_info_label_->setText(QString(is_english_
                ? "raw signal: %1 samples, %2"
                : "原始信号: %1 个采样点，%2")
                .arg(pending_wave1_.size())
                .arg(describeRange(pending_wave1_)));
            wave4_info_label_->setText(QString(is_english_
                ? "normalized second harmonic: %1 samples, %2"
                : "归一化二次谐波: %1 个采样点，%2")
                .arg(wave4.size())
                .arg(describeRange(wave4)));

            setStatusText(QString(is_english_
                ? "Receiving frame %3 from %1:%2, float format: %4"
                : "正在接收来自 %1:%2 的数据帧，第 %3 帧，浮点格式: %4")
                .arg(host_edit_->text())
                .arg(port_spin_->value())
                .arg(frame_count_)
                .arg(floatEncodingLabel(is_english_, float_encoding_)));

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

bool TcpWavePanel::tryConsumePayload(QVector<float>& output)
{
    if (buffer_.size() < expected_payload_size_)
    {
        return false;
    }

    const QByteArray payload = buffer_.left(expected_payload_size_);
    buffer_.remove(0, expected_payload_size_);
    if (float_encoding_ == FloatEncoding::Unknown)
    {
        float_encoding_ = autoDetectFloatEncoding(payload);
        setStatusText(QString(is_english_
            ? "Detected float payload format: %1"
            : "已识别浮点负载格式: %1")
            .arg(floatEncodingLabel(is_english_, float_encoding_)));
    }
    output = decodeFloatPayload(payload);
    expected_payload_size_ = 0;
    return true;
}

float TcpWavePanel::decodeFloatSample(const char *raw, FloatEncoding encoding) const
{
    quint32 bits = 0;
    switch (encoding)
    {
    case FloatEncoding::LittleEndian:
        bits = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(raw));
        break;
    case FloatEncoding::BigEndian:
        bits = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(raw));
        break;
    case FloatEncoding::WordSwappedLittleEndian:
    {
        const uchar ordered[4] = {
            static_cast<uchar>(raw[0]),
            static_cast<uchar>(raw[1]),
            static_cast<uchar>(raw[3]),
            static_cast<uchar>(raw[2]),
        };
        bits = qFromLittleEndian<quint32>(ordered);
        break;
    }
    case FloatEncoding::Unknown:
    default:
        bits = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(raw));
        break;
    }

    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(float));
    return value;
}

TcpWavePanel::FloatEncoding TcpWavePanel::autoDetectFloatEncoding(const QByteArray& payload) const
{
    const FloatEncoding candidates[] = {
        FloatEncoding::LittleEndian,
        FloatEncoding::BigEndian,
        FloatEncoding::WordSwappedLittleEndian,
    };
    const int sampleCount = std::min(static_cast<int>(payload.size() / kFloatSize), 1024);
    double bestScore = -std::numeric_limits<double>::infinity();
    FloatEncoding bestEncoding = FloatEncoding::LittleEndian;

    for (FloatEncoding encoding : candidates)
    {
        double score = 0.0;
        float previous = 0.0f;
        bool hasPrevious = false;
        for (int i = 0; i < sampleCount; ++i)
        {
            const float value = decodeFloatSample(payload.constData() + i * kFloatSize, encoding);
            if (!std::isfinite(value))
            {
                score -= 1000.0;
                continue;
            }

            const double magnitude = std::fabs(static_cast<double>(value));
            score += 100.0;
            if (magnitude < 10.0)
            {
                score += 20.0;
            }
            else if (magnitude < 1000.0)
            {
                score += 5.0;
            }
            else if (magnitude > 1.0e6)
            {
                score -= 200.0;
            }

            if (hasPrevious)
            {
                const double delta = std::fabs(static_cast<double>(value) - static_cast<double>(previous));
                if (delta < 0.1)
                {
                    score += 5.0;
                }
                else if (delta < 1.0)
                {
                    score += 3.0;
                }
                else if (delta < 10.0)
                {
                    score += 1.0;
                }
                else if (delta > 1.0e4)
                {
                    score -= 25.0;
                }
            }

            previous = value;
            hasPrevious = true;
        }

        if (score > bestScore)
        {
            bestScore = score;
            bestEncoding = encoding;
        }
    }

    return bestEncoding;
}

QVector<float> TcpWavePanel::decodeFloatPayload(const QByteArray& payload) const
{
    QVector<float> values;
    const int count = payload.size() / kFloatSize;
    values.resize(count);
    const FloatEncoding encoding = float_encoding_ == FloatEncoding::Unknown
        ? FloatEncoding::LittleEndian
        : float_encoding_;
    for (int i = 0; i < count; ++i)
    {
        values[i] = decodeFloatSample(payload.constData() + i * kFloatSize, encoding);
    }
    return values;
}
