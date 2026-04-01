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
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
constexpr int kHeaderSize = 4;
constexpr int kFloatSize = 4;
constexpr int kMaxPayloadBytes = 16 * 1024 * 1024;

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
        painter.fillRect(rect(), QColor("#050805"));

        const QRectF plotRect = rect().adjusted(42, 12, -10, -24);
        painter.setPen(QPen(QColor("#1b6416"), 1));
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

        painter.setPen(QPen(QColor("#9ca39d"), 1));
        painter.drawRect(plotRect);

        if (samples_.isEmpty())
        {
            painter.setPen(QColor("#b8c4b8"));
            painter.drawText(plotRect, Qt::AlignCenter, tr("No data"));
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

        painter.setPen(QColor("#d7d7d7"));
        painter.drawText(QRectF(4, plotRect.top() - 2, 36, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(maxValue, 'f', 3));
        painter.drawText(QRectF(4, plotRect.center().y() - 8, 36, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number((maxValue + minValue) * 0.5, 'f', 3));
        painter.drawText(QRectF(4, plotRect.bottom() - 8, 36, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(minValue, 'f', 3));
        painter.drawText(QRectF(plotRect.left(), plotRect.bottom() + 4, plotRect.width(), 16), Qt::AlignRight | Qt::AlignVCenter,
                         QString("%1 samples").arg(sampleCount));
    }

private:
    QColor line_color_;
    QVector<float> samples_;
};

TcpWavePanel::TcpWavePanel(QWidget *parent)
    : QWidget(parent)
    , host_edit_(nullptr)
    , port_spin_(nullptr)
    , connect_button_(nullptr)
    , host_label_(nullptr)
    , port_label_(nullptr)
    , status_label_(nullptr)
    , hint_label_(nullptr)
    , wave1_info_label_(nullptr)
    , wave4_info_label_(nullptr)
    , wave1_group_(nullptr)
    , wave4_group_(nullptr)
    , wave1_plot_(nullptr)
    , wave4_plot_(nullptr)
    , socket_(nullptr)
    , parse_mode_(ParseMode::AutoDetect)
    , read_state_(ReadState::Wave1Header)
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
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(6);

    auto *controlLayout = new QGridLayout();
    controlLayout->setHorizontalSpacing(8);
    controlLayout->setVerticalSpacing(4);

    host_label_ = new QLabel(this);
    host_label_->setObjectName("fieldLabel");
    controlLayout->addWidget(host_label_, 0, 0);

    host_edit_ = new QLineEdit(this);
    host_edit_->setText("127.0.0.1");
    controlLayout->addWidget(host_edit_, 0, 1);

    port_label_ = new QLabel(this);
    port_label_->setObjectName("fieldLabel");
    controlLayout->addWidget(port_label_, 0, 2);

    port_spin_ = new QSpinBox(this);
    port_spin_->setRange(1, 65535);
    port_spin_->setValue(8888);
    controlLayout->addWidget(port_spin_, 0, 3);

    connect_button_ = new QPushButton(this);
    connect(connect_button_, &QPushButton::clicked, this, &TcpWavePanel::onToggleConnectionClicked);
    controlLayout->addWidget(connect_button_, 0, 4);

    status_label_ = new QLabel(this);
    status_label_->setWordWrap(true);
    controlLayout->addWidget(status_label_, 1, 0, 1, 5);

    hint_label_ = new QLabel(this);
    hint_label_->setWordWrap(true);
    controlLayout->addWidget(hint_label_, 2, 0, 1, 5);

    mainLayout->addLayout(controlLayout);

    auto *plotsLayout = new QHBoxLayout();
    plotsLayout->setSpacing(6);

    wave1_group_ = new QGroupBox(this);
    wave1_group_->setObjectName("sensorGroupBox");
    auto *wave1Layout = new QVBoxLayout(wave1_group_);
    wave1Layout->setContentsMargins(4, 4, 4, 4);
    wave1_info_label_ = new QLabel(this);
    wave1Layout->addWidget(wave1_info_label_);
    wave1_plot_ = new WavePlotWidget(QColor("#f2f2f2"), this);
    wave1Layout->addWidget(wave1_plot_, 1);
    plotsLayout->addWidget(wave1_group_, 1);

    wave4_group_ = new QGroupBox(this);
    wave4_group_->setObjectName("sensorGroupBox");
    auto *wave4Layout = new QVBoxLayout(wave4_group_);
    wave4Layout->setContentsMargins(4, 4, 4, 4);
    wave4_info_label_ = new QLabel(this);
    wave4Layout->addWidget(wave4_info_label_);
    wave4_plot_ = new WavePlotWidget(QColor("#ffe100"), this);
    wave4Layout->addWidget(wave4_plot_, 1);
    plotsLayout->addWidget(wave4_group_, 1);

    mainLayout->addLayout(plotsLayout, 1);
}

void TcpWavePanel::setEnglish(bool english)
{
    is_english_ = english;
    host_label_->setText(english ? "TCP Host:" : "TCP主机:");
    port_label_->setText(english ? "Port:" : "端口:");
    connect_button_->setText(socket_ && socket_->state() == QAbstractSocket::ConnectedState
        ? (english ? "Stop" : "停止")
        : (english ? "Start" : "启动"));
    wave1_group_->setTitle(english ? "Wave 1" : "波形图1");
    wave4_group_->setTitle(english ? "Wave 4" : "波形图4");
    hint_label_->setText(english
        ? "This TCP sender is likely single-client. Do not open the LabVIEW VI receiver and VaporView on port 8888 at the same time."
        : "这个TCP发送端大概率只支持单客户端。不要同时打开 LabVIEW VI 接收端和 VaporView 去抢同一个 8888 连接。");

    wave1_info_label_->setText(english ? "waiting for wave1 frame" : "等待波形图1数据帧");
    wave4_info_label_->setText(english ? "waiting for wave4 frame" : "等待波形图4数据帧");

    if (!socket_ || socket_->state() != QAbstractSocket::ConnectedState)
    {
        setStatusText(english ? "Idle" : "空闲");
    }
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
    pending_wave1_.clear();
    resetParserState();
    frame_count_ = 0;
    setStatusText(QString(is_english_ ? "Connecting to %1:%2..." : "正在连接 %1:%2...")
        .arg(host_edit_->text()).arg(port_spin_->value()));
    socket_->connectToHost(host_edit_->text(), static_cast<quint16>(port_spin_->value()));
    onSocketStateChanged();
}

void TcpWavePanel::onSocketConnected()
{
    setConnectedUiState(true);
    setStatusText(QString(is_english_
        ? "Connected to %1:%2, waiting for the first frame..."
        : "已连接到 %1:%2，正在等待首帧数据...")
        .arg(host_edit_->text()).arg(port_spin_->value()));
}

void TcpWavePanel::onSocketDisconnected()
{
    setConnectedUiState(false);
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
    parse_mode_ = ParseMode::AutoDetect;
    read_state_ = ReadState::Wave1Header;
    expected_payload_size_ = 0;
}

void TcpWavePanel::processBuffer()
{
    if (parse_mode_ == ParseMode::AutoDetect && buffer_.size() >= 12)
    {
        const qint32 candidate = qFromLittleEndian<qint32>(reinterpret_cast<const uchar*>(buffer_.constData()));
        const bool validLengthHeader = candidate > 0 && candidate <= kMaxPayloadBytes && (candidate % kFloatSize) == 0;
        if (validLengthHeader)
        {
            parse_mode_ = ParseMode::LengthPrefixed;
            setStatusText(is_english_ ? "Detected length-prefixed TCP payloads" : "已识别为长度前缀TCP负载格式");
        }
        else if (looksLikeRawScalarTriplet(buffer_))
        {
            parse_mode_ = ParseMode::RawScalarTriplets;
            setStatusText(is_english_
                ? "Detected LabVIEW raw scalar stream, switching parser mode"
                : "已识别为LabVIEW原始标量流，正在切换解析模式");
        }
    }

    if (parse_mode_ == ParseMode::RawScalarTriplets)
    {
        bool updated = false;
        while (buffer_.size() >= 12)
        {
            const float skipped = decodeRawScalarSample(buffer_.constData());
            const float wave1 = decodeRawScalarSample(buffer_.constData() + 4);
            const float wave4 = decodeRawScalarSample(buffer_.constData() + 8);
            buffer_.remove(0, 12);

            if (!std::isfinite(skipped) || !std::isfinite(wave1) || !std::isfinite(wave4))
            {
                continue;
            }

            appendHistorySample(wave1_history_, wave1, 50000);
            appendHistorySample(wave4_history_, wave4, 50000);
            ++frame_count_;
            updated = true;
        }

        if (updated)
        {
            wave1_plot_->setSamples(wave1_history_);
            wave4_plot_->setSamples(wave4_history_);
            wave1_info_label_->setText(QString(is_english_
                ? "wave1: %1 samples, latest=%2"
                : "波形图1: %1 个采样点，最新值=%2")
                .arg(wave1_history_.size())
                .arg(wave1_history_.isEmpty() ? 0.0 : wave1_history_.last(), 0, 'f', 6));
            wave4_info_label_->setText(QString(is_english_
                ? "wave4: %1 samples, latest=%2"
                : "波形图4: %1 个采样点，最新值=%2")
                .arg(wave4_history_.size())
                .arg(wave4_history_.isEmpty() ? 0.0 : wave4_history_.last(), 0, 'f', 6));
            setStatusText(QString(is_english_
                ? "Receiving raw scalar frame %1"
                : "正在接收原始标量流，第 %1 帧")
                .arg(frame_count_));
        }
        return;
    }

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
            ++frame_count_;

            wave1_info_label_->setText(QString(is_english_
                ? "wave1: %1 samples"
                : "波形图1: %1 个采样点").arg(pending_wave1_.size()));
            wave4_info_label_->setText(QString(is_english_
                ? "wave4: %1 samples"
                : "波形图4: %1 个采样点").arg(wave4.size()));

            setStatusText(QString(is_english_
                ? "Receiving frame %3 from %1:%2"
                : "正在接收来自 %1:%2 的数据帧，第 %3 帧")
                .arg(host_edit_->text()).arg(port_spin_->value()).arg(frame_count_));

            pending_wave1_.clear();
            resetParserState();
            break;
        }
        }
    }
}

bool TcpWavePanel::tryConsumeHeader()
{
    while (buffer_.size() >= kHeaderSize)
    {
        const qint32 candidate = qFromLittleEndian<qint32>(reinterpret_cast<const uchar*>(buffer_.constData()));
        if (candidate >= 0 && candidate <= kMaxPayloadBytes && (candidate % kFloatSize) == 0)
        {
            expected_payload_size_ = candidate;
            buffer_.remove(0, kHeaderSize);
            return true;
        }

        setStatusText(QString(is_english_
            ? "Unexpected TCP frame header (%1), bytes: %2, trying to resync..."
            : "TCP帧头异常（%1），字节预览：%2，正在尝试重新同步...")
            .arg(candidate)
            .arg(hexPreview(buffer_)));
        buffer_.remove(0, 1);
    }

    return false;
}

void TcpWavePanel::appendHistorySample(QVector<float>& history, float value, int maxSamples)
{
    history.append(value);
    const int overflow = history.size() - maxSamples;
    if (overflow > 0)
    {
        history.remove(0, overflow);
    }
}

bool TcpWavePanel::looksLikeRawScalarTriplet(const QByteArray& data) const
{
    if (data.size() < 12)
    {
        return false;
    }

    const float a = decodeRawScalarSample(data.constData());
    const float b = decodeRawScalarSample(data.constData() + 4);
    const float c = decodeRawScalarSample(data.constData() + 8);
    auto plausible = [](float value) {
        return std::isfinite(value) && std::fabs(value) < 1.0e6f;
    };
    return plausible(a) && plausible(b) && plausible(c);
}

float TcpWavePanel::decodeRawScalarSample(const char *raw) const
{
    const uchar ordered[4] = {
        static_cast<uchar>(raw[0]),
        static_cast<uchar>(raw[1]),
        static_cast<uchar>(raw[3]),
        static_cast<uchar>(raw[2]),
    };
    const quint32 bits = qFromLittleEndian<quint32>(ordered);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(float));
    return value;
}

bool TcpWavePanel::tryConsumePayload(QVector<float>& output)
{
    if (buffer_.size() < expected_payload_size_)
    {
        return false;
    }

    const QByteArray payload = buffer_.left(expected_payload_size_);
    buffer_.remove(0, expected_payload_size_);
    output = decodeFloatPayload(payload);
    expected_payload_size_ = 0;
    return true;
}

QVector<float> TcpWavePanel::decodeFloatPayload(const QByteArray& payload) const
{
    QVector<float> values;
    const int count = payload.size() / kFloatSize;
    values.resize(count);
    for (int i = 0; i < count; ++i)
    {
        const uchar *raw = reinterpret_cast<const uchar*>(payload.constData() + i * kFloatSize);
        const quint32 bits = qFromLittleEndian<quint32>(raw);
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(float));
        values[i] = value;
    }
    return values;
}
