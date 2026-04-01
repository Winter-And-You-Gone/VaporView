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
#include <limits>

namespace
{
constexpr int kHeaderSize = 4;
constexpr int kFloatSize = 4;
constexpr int kMaxPayloadBytes = 16 * 1024 * 1024;
constexpr int kPreferredPayloadBytes = 200000;

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
    parse_mode_ = ParseMode::AutoDetect;
    header_byte_order_ = HeaderByteOrder::Unknown;
    float_encoding_ = FloatEncoding::Unknown;
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
            ++frame_count_;

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
                ? "wave1: %1 samples, %2"
                : "波形图1: %1 个采样点，%2")
                .arg(pending_wave1_.size())
                .arg(describeRange(pending_wave1_)));
            wave4_info_label_->setText(QString(is_english_
                ? "wave4: %1 samples, %2"
                : "波形图4: %1 个采样点，%2")
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
