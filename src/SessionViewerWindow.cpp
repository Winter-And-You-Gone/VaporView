#include "SessionViewerWindow.h"
#include "RangeSelectionAxisWidget.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <QWidget>
#include <QStringConverter>
#include <QTimeZone>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>

namespace
{
constexpr quint64 kWaveformTimestampBytes = sizeof(quint64);
constexpr quint64 kFloatBytes = sizeof(float);
const QColor kHighlightedCsvRowColor("#c7e3ff");
const QColor kSecondaryHighlightedCsvRowColor("#e8f3ff");
const QColor kDefaultCsvRowColor("#ffffff");

QString csvValueAt(const QStringList& fields, int index)
{
    if (index < 0 || index >= fields.size())
    {
        return QString();
    }
    return fields.at(index);
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
        .arg(QDateTime::fromMSecsSinceEpoch(millis, QTimeZone::UTC).toLocalTime().toString("yyyy-MM-dd HH:mm:ss"))
        .arg(micros, 6, 10, QChar('0'));
}

QString formatSignedDeltaMs(qint64 deltaUs)
{
    const double deltaMs = static_cast<double>(deltaUs) / 1000.0;
    return QString("%1%2 ms")
        .arg(deltaMs >= 0.0 ? "+" : "")
        .arg(QString::number(deltaMs, 'f', 3));
}

QStringList parseCsvLine(const QString& line)
{
    QStringList fields;
    QString current;
    bool inQuotes = false;

    for (int i = 0; i < line.size(); ++i)
    {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char('"'))
        {
            if (inQuotes && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"'))
            {
                current += QLatin1Char('"');
                ++i;
            }
            else
            {
                inQuotes = !inQuotes;
            }
            continue;
        }

        if (ch == QLatin1Char(',') && !inQuotes)
        {
            fields.push_back(current);
            current.clear();
            continue;
        }

        current += ch;
    }

    fields.push_back(current);
    return fields;
}
}

class SessionWavePlotWidget : public QWidget
{
public:
    explicit SessionWavePlotWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(220);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
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
        painter.fillRect(rect(), QColor("#ffffff"));

        const QRectF plotRect = rect().adjusted(48, 12, -10, -30);
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
            painter.drawText(plotRect, Qt::AlignCenter, tr("No waveform frame"));
            return;
        }

        const auto minMax = std::minmax_element(samples_.cbegin(), samples_.cend());
        float minValue = *minMax.first;
        float maxValue = *minMax.second;
        if (std::fabs(maxValue - minValue) < 1e-6f)
        {
            const float pad = std::max(1e-6f, std::fabs(maxValue) * 0.05f + 1e-6f);
            minValue -= pad;
            maxValue += pad;
        }

        const int columns = std::max(2, static_cast<int>(std::floor(plotRect.width())));
        QPolygonF polyline;
        polyline.reserve(columns);
        for (int x = 0; x < columns; ++x)
        {
            const double ratio = columns == 1 ? 0.0 : static_cast<double>(x) / static_cast<double>(columns - 1);
            const int sampleCount = static_cast<int>(samples_.size());
            const int sampleIndex = std::clamp(static_cast<int>(std::llround(ratio * (sampleCount - 1))), 0, sampleCount - 1);
            const float value = samples_.at(sampleIndex);
            const double normalized = (value - minValue) / std::max(1e-6f, maxValue - minValue);
            polyline.append(QPointF(plotRect.left() + ratio * plotRect.width(),
                                    plotRect.bottom() - normalized * plotRect.height()));
        }

        painter.setPen(QPen(QColor("#f0d000"), 1.4));
        painter.drawPolyline(polyline);

        painter.setPen(QColor("#d7d7d7"));
        painter.drawText(QRectF(4, plotRect.top() - 2, 40, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(maxValue, 'f', 4));
        painter.drawText(QRectF(4, plotRect.center().y() - 8, 40, 16), Qt::AlignRight | Qt::AlignVCenter,
                         QString::number((maxValue + minValue) * 0.5, 'f', 4));
        painter.drawText(QRectF(4, plotRect.bottom() - 8, 40, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(minValue, 'f', 4));
        painter.drawText(QRectF(plotRect.left(), plotRect.bottom() + 6, plotRect.width(), 16), Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("%1 samples").arg(samples_.size()));
    }

private:
    QVector<float> samples_;
};

class SessionPeakPlotWidget : public QWidget
{
public:
    enum class PlotMode
    {
        Scatter,
        Polyline
    };

    explicit SessionPeakPlotWidget(QWidget *parent = nullptr)
        : QWidget(parent)
        , current_frame_index_(-1)
        , plot_mode_(PlotMode::Scatter)
        , view_start_index_(0)
        , view_count_(0)
        , dragging_(false)
        , drag_start_x_(0)
        , drag_origin_start_(0)
    {
        setMinimumHeight(170);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    void setPeakValues(const QVector<float>& values)
    {
        const bool keepTail = peak_values_.isEmpty() ||
            view_count_ <= 0 ||
            (view_start_index_ + visibleCount()) >= peak_values_.size();
        peak_values_ = values;
        if (current_frame_index_ >= peak_values_.size())
        {
            current_frame_index_ = -1;
        }
        normalizeView(keepTail);
        notifyViewChanged();
        update();
    }

    void setCurrentFrame(int frameIndex)
    {
        current_frame_index_ = frameIndex;
        if (current_frame_index_ >= 0 &&
            current_frame_index_ < static_cast<int>(peak_values_.size()) &&
            (current_frame_index_ < visibleStartIndex() ||
             current_frame_index_ >= (visibleStartIndex() + visibleCount())))
        {
            const int count = visibleCount();
            view_start_index_ = std::clamp(current_frame_index_ - count / 2, 0, std::max(0, static_cast<int>(peak_values_.size()) - count));
            normalizeView(false);
            notifyViewChanged();
        }
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

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), QColor("#ffffff"));

        const QRectF plotRect = rect().adjusted(48, 12, -10, -28);
        painter.setPen(QPen(QColor("#c7d7ea"), 1));
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

        painter.setPen(QPen(QColor("#9bb3cc"), 1));
        painter.drawRect(plotRect);

        if (peak_values_.isEmpty())
        {
            painter.setPen(QColor("#5e7698"));
            painter.drawText(plotRect, Qt::AlignCenter, QObject::tr("No peak overview"));
            return;
        }

        const int startIndex = visibleStartIndex();
        const int count = visibleCount();
        const auto visibleBegin = peak_values_.cbegin() + startIndex;
        const auto visibleEnd = visibleBegin + count;
        const auto minMax = std::minmax_element(visibleBegin, visibleEnd);
        float minValue = *minMax.first;
        float maxValue = *minMax.second;
        if (std::fabs(maxValue - minValue) < 1e-6f)
        {
            const float pad = std::max(1e-6f, std::fabs(maxValue) * 0.05f + 1e-6f);
            minValue -= pad;
            maxValue += pad;
        }

        QVector<QPointF> points;
        points.reserve(count);
        for (int i = 0; i < count; ++i)
        {
            const double ratio = count == 1 ? 0.5 : static_cast<double>(i) / static_cast<double>(count - 1);
            const float value = peak_values_.at(startIndex + i);
            const double normalized = (value - minValue) / std::max(1e-6f, maxValue - minValue);
            points.push_back(QPointF(plotRect.left() + ratio * plotRect.width(),
                                     plotRect.bottom() - normalized * plotRect.height()));
        }

        const QColor seriesColor("#66d0ff");
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

        if (current_frame_index_ >= startIndex && current_frame_index_ < (startIndex + count))
        {
            const QPointF currentPoint = points.at(current_frame_index_ - startIndex);
            painter.setPen(QPen(QColor("#ffb347"), 1, Qt::DashLine));
            painter.drawLine(QPointF(currentPoint.x(), plotRect.top()), QPointF(currentPoint.x(), plotRect.bottom()));
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor("#ffb347"));
            painter.drawEllipse(currentPoint, 4.0, 4.0);
        }

        painter.setPen(QColor("#4f647a"));
        painter.drawText(QRectF(4, plotRect.top() - 2, 40, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(maxValue, 'f', 4));
        painter.drawText(QRectF(4, plotRect.center().y() - 8, 40, 16), Qt::AlignRight | Qt::AlignVCenter,
                         QString::number((maxValue + minValue) * 0.5, 'f', 4));
        painter.drawText(QRectF(4, plotRect.bottom() - 8, 40, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(minValue, 'f', 4));
        painter.drawText(QRectF(plotRect.left(), plotRect.bottom() + 6, plotRect.width() * 0.55, 16),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QString("%1-%2 / %3")
                             .arg(startIndex + 1)
                             .arg(startIndex + count)
                             .arg(peak_values_.size()));
        painter.drawText(QRectF(plotRect.left(), plotRect.bottom() + 6, plotRect.width(), 16), Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("%1 frames").arg(count));
    }

    void wheelEvent(QWheelEvent *event) override
    {
        if (peak_values_.size() <= 1)
        {
            return;
        }

        const int totalCount = peak_values_.size();
        const int oldCount = visibleCount();
        int newCount = oldCount;
        if (event->angleDelta().y() > 0)
        {
            newCount = std::max(20, static_cast<int>(std::floor(oldCount * 0.8)));
        }
        else if (event->angleDelta().y() < 0)
        {
            newCount = std::min(totalCount, static_cast<int>(std::ceil(oldCount * 1.25)));
        }

        if (newCount == oldCount)
        {
            event->accept();
            return;
        }

        if (newCount >= totalCount)
        {
            view_start_index_ = 0;
            view_count_ = 0;
            notifyViewChanged();
            update();
            event->accept();
            return;
        }

        const qreal ratio = width() <= 1 ? 0.5 : std::clamp(event->position().x() / static_cast<qreal>(width()), 0.0, 1.0);
        const double anchorIndex = visibleStartIndex() + ratio * std::max(0, oldCount - 1);
        view_count_ = newCount;
        view_start_index_ = static_cast<int>(std::llround(anchorIndex - ratio * std::max(0, newCount - 1)));
        normalizeView(false);
        notifyViewChanged();
        update();
        event->accept();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && peak_values_.size() > visibleCount())
        {
            dragging_ = true;
            drag_start_x_ = event->position().x();
            drag_origin_start_ = visibleStartIndex();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (dragging_ && peak_values_.size() > visibleCount())
        {
            const qreal widthPixels = std::max(1.0, static_cast<qreal>(width()));
            const qreal deltaRatio = (event->position().x() - drag_start_x_) / widthPixels;
            const int deltaFrames = static_cast<int>(std::llround(deltaRatio * visibleCount()));
            view_start_index_ = drag_origin_start_ - deltaFrames;
            normalizeView(false);
            notifyViewChanged();
            update();
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && dragging_)
        {
            dragging_ = false;
            unsetCursor();
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            view_start_index_ = 0;
            view_count_ = 0;
            dragging_ = false;
            unsetCursor();
            notifyViewChanged();
            update();
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
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
    int current_frame_index_;
    PlotMode plot_mode_;
    int view_start_index_;
    int view_count_;
    bool dragging_;
    qreal drag_start_x_;
    int drag_origin_start_;
    std::function<void(int, int, int)> on_view_changed_;
};

SessionViewerWindow::SessionViewerWindow(QWidget *parent)
    : QMainWindow(parent)
    , central_widget_(nullptr)
    , session_path_edit_(nullptr)
    , choose_session_btn_(nullptr)
    , reload_btn_(nullptr)
    , clear_view_btn_(nullptr)
    , status_label_(nullptr)
    , summary_group_(nullptr)
    , session_name_title_(nullptr)
    , session_name_value_(nullptr)
    , start_time_title_(nullptr)
    , start_time_value_(nullptr)
    , end_time_title_(nullptr)
    , end_time_value_(nullptr)
    , sensor_rows_title_(nullptr)
    , sensor_rows_value_(nullptr)
    , waveform_files_title_(nullptr)
    , waveform_files_value_(nullptr)
    , waveform_frames_title_(nullptr)
    , waveform_frames_value_(nullptr)
    , waveform_group_(nullptr)
    , frame_title_(nullptr)
    , frame_slider_(nullptr)
    , frame_spin_(nullptr)
    , frame_total_label_(nullptr)
    , frame_info_label_(nullptr)
    , waveform_plot_title_(nullptr)
    , waveform_plot_(nullptr)
    , waveform_peak_plot_title_(nullptr)
    , waveform_peak_mode_btn_(nullptr)
    , waveform_peak_plot_(nullptr)
    , csv_group_(nullptr)
    , csv_info_label_(nullptr)
    , csv_table_(nullptr)
    , session_directory_()
    , metadata_filename_()
    , sensors_csv_filename_()
    , waveform_directory_()
    , session_name_()
    , start_time_utc_()
    , end_time_utc_()
    , csv_headers_()
    , csv_timestamps_us_()
    , waveform_segments_()
    , waveform_peak_values_()
    , is_english_(false)
    , updating_frame_controls_(false)
    , waveform_peak_scatter_mode_(true)
    , highlighted_csv_rows_()
    , points_per_frame_(50000)
    , waveform_export_rate_hz_(10)
    , total_sensor_rows_(0)
    , total_waveform_frames_(0)
{
    setupUi();
    resize(1320, 860);
    setEnglish(false);

    QSettings settings("VaporView", "SessionViewer");
    const QString lastSession = settings.value("last_session_directory").toString();
    if (!lastSession.isEmpty())
    {
        openSessionPath(lastSession);
    }
}

void SessionViewerWindow::setupUi()
{
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setCentralWidget(scrollArea);

    central_widget_ = new QWidget(this);
    scrollArea->setWidget(central_widget_);

    auto *mainLayout = new QVBoxLayout(central_widget_);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    auto *controlLayout = new QGridLayout();
    controlLayout->setHorizontalSpacing(8);
    controlLayout->setVerticalSpacing(4);

    auto *pathTitle = new QLabel(this);
    pathTitle->setObjectName("fieldLabel");
    pathTitle->setText(tr("Session:"));
    controlLayout->addWidget(pathTitle, 0, 0);

    session_path_edit_ = new QLineEdit(this);
    session_path_edit_->setReadOnly(true);
    controlLayout->addWidget(session_path_edit_, 0, 1);

    choose_session_btn_ = new QPushButton(this);
    connect(choose_session_btn_, &QPushButton::clicked, this, &SessionViewerWindow::onChooseSessionClicked);
    controlLayout->addWidget(choose_session_btn_, 0, 2);

    reload_btn_ = new QPushButton(this);
    connect(reload_btn_, &QPushButton::clicked, this, &SessionViewerWindow::onReloadClicked);
    controlLayout->addWidget(reload_btn_, 0, 3);

    clear_view_btn_ = new QPushButton(this);
    connect(clear_view_btn_, &QPushButton::clicked, this, &SessionViewerWindow::onClearViewClicked);
    controlLayout->addWidget(clear_view_btn_, 0, 4);

    status_label_ = new QLabel(this);
    status_label_->setWordWrap(true);
    controlLayout->addWidget(status_label_, 1, 0, 1, 5);

    mainLayout->addLayout(controlLayout);

    auto *summaryWaveSplitter = new QSplitter(Qt::Vertical, this);

    auto *upperWidget = new QWidget(this);
    auto *upperLayout = new QVBoxLayout(upperWidget);
    upperLayout->setContentsMargins(0, 0, 0, 0);
    upperLayout->setSpacing(8);

    summary_group_ = new QGroupBox(this);
    summary_group_->setObjectName("sensorGroupBox");
    auto *summaryLayout = new QGridLayout(summary_group_);
    summaryLayout->setContentsMargins(10, 30, 10, 10);
    summaryLayout->setHorizontalSpacing(10);
    summaryLayout->setVerticalSpacing(6);

    auto createSummaryRow = [this, summaryLayout](int row, int col, QLabel*& title, QLabel*& value) {
        title = new QLabel(this);
        title->setObjectName("fieldLabel");
        value = new QLabel("---", this);
        value->setObjectName("valueLabel");
        summaryLayout->addWidget(title, row, col * 2);
        summaryLayout->addWidget(value, row, col * 2 + 1);
    };

    createSummaryRow(0, 0, session_name_title_, session_name_value_);
    createSummaryRow(0, 1, start_time_title_, start_time_value_);
    createSummaryRow(0, 2, end_time_title_, end_time_value_);
    createSummaryRow(1, 0, sensor_rows_title_, sensor_rows_value_);
    createSummaryRow(1, 1, waveform_files_title_, waveform_files_value_);
    createSummaryRow(1, 2, waveform_frames_title_, waveform_frames_value_);
    upperLayout->addWidget(summary_group_);

    waveform_group_ = new QGroupBox(this);
    waveform_group_->setObjectName("sensorGroupBox");
    auto *waveformLayout = new QVBoxLayout(waveform_group_);
    waveformLayout->setContentsMargins(10, 30, 10, 10);
    waveformLayout->setSpacing(6);

    auto *frameLayout = new QGridLayout();
    frameLayout->setHorizontalSpacing(8);
    frameLayout->setVerticalSpacing(4);

    frame_title_ = new QLabel(this);
    frame_title_->setObjectName("fieldLabel");
    frameLayout->addWidget(frame_title_, 0, 0);

    frame_slider_ = new QSlider(Qt::Horizontal, this);
    frame_slider_->setEnabled(false);
    connect(frame_slider_, &QSlider::valueChanged, this, &SessionViewerWindow::onFrameSliderChanged);
    frameLayout->addWidget(frame_slider_, 0, 1);

    frame_spin_ = new QSpinBox(this);
    frame_spin_->setRange(0, 0);
    frame_spin_->setEnabled(false);
    connect(frame_spin_, &QSpinBox::valueChanged, this, &SessionViewerWindow::onFrameSpinChanged);
    frameLayout->addWidget(frame_spin_, 0, 2);

    frame_total_label_ = new QLabel("---", this);
    frameLayout->addWidget(frame_total_label_, 0, 3);

    frame_info_label_ = new QLabel(this);
    frame_info_label_->setWordWrap(true);
    frameLayout->addWidget(frame_info_label_, 1, 0, 1, 4);

    waveformLayout->addLayout(frameLayout);

    waveform_plot_title_ = new QLabel(this);
    waveform_plot_title_->setObjectName("fieldLabel");
    waveformLayout->addWidget(waveform_plot_title_);

    waveform_plot_ = new SessionWavePlotWidget(this);
    waveformLayout->addWidget(waveform_plot_, 1);

    auto *peakHeaderLayout = new QHBoxLayout();
    peakHeaderLayout->setContentsMargins(0, 0, 0, 0);
    peakHeaderLayout->setSpacing(8);
    waveform_peak_plot_title_ = new QLabel(this);
    waveform_peak_plot_title_->setObjectName("fieldLabel");
    peakHeaderLayout->addWidget(waveform_peak_plot_title_, 1, Qt::AlignVCenter | Qt::AlignLeft);
    waveform_peak_mode_btn_ = new QPushButton(this);
    peakHeaderLayout->addWidget(waveform_peak_mode_btn_, 0, Qt::AlignVCenter | Qt::AlignRight);
    waveformLayout->addLayout(peakHeaderLayout);

    waveform_peak_plot_ = new SessionPeakPlotWidget(this);
    static_cast<SessionPeakPlotWidget*>(waveform_peak_plot_)->setPlotMode(
        waveform_peak_scatter_mode_ ? SessionPeakPlotWidget::PlotMode::Scatter : SessionPeakPlotWidget::PlotMode::Polyline);
    auto *waveformPeakRangeAxis = new RangeSelectionAxisWidget(this);
    static_cast<SessionPeakPlotWidget*>(waveform_peak_plot_)->setViewChangedCallback(
        [waveformPeakRangeAxis](int totalCount, int startIndex, int visibleCount) {
            if (waveformPeakRangeAxis)
            {
                waveformPeakRangeAxis->setRange(totalCount, startIndex, visibleCount);
            }
        });
    waveformPeakRangeAxis->setRangeChangedCallback([this](int startIndex, int visibleCount) {
        static_cast<SessionPeakPlotWidget*>(waveform_peak_plot_)->setViewRange(startIndex, visibleCount);
    });
    connect(waveform_peak_mode_btn_, &QPushButton::clicked, this, &SessionViewerWindow::onTogglePeakPlotModeClicked);
    waveformLayout->addWidget(waveform_peak_plot_, 1);
    waveformLayout->addWidget(waveformPeakRangeAxis);
    upperLayout->addWidget(waveform_group_, 1);

    summaryWaveSplitter->addWidget(upperWidget);

    csv_group_ = new QGroupBox(this);
    csv_group_->setObjectName("sensorGroupBox");
    auto *csvLayout = new QVBoxLayout(csv_group_);
    csvLayout->setContentsMargins(10, 30, 10, 10);
    csvLayout->setSpacing(6);

    csv_info_label_ = new QLabel(this);
    csv_info_label_->setWordWrap(true);
    csvLayout->addWidget(csv_info_label_);

    csv_table_ = new QTableWidget(this);
    csv_table_->setAlternatingRowColors(false);
    csv_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    csv_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    csv_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    csv_table_->setWordWrap(false);
    csv_table_->setStyleSheet(
        "QTableWidget { background-color: #ffffff; alternate-background-color: #ffffff; gridline-color: #e5e7eb; }"
        "QTableWidget::item { color: #1f2933; }"
        "QTableWidget::item:selected { background-color: #c7e3ff; color: #1f2933; }"
        "QTableWidget::item:selected:active { background-color: #c7e3ff; color: #1f2933; }"
        "QTableWidget::item:selected:!active { background-color: #c7e3ff; color: #1f2933; }");
    csv_table_->horizontalHeader()->setSectionsMovable(true);
    csv_table_->horizontalHeader()->setDefaultSectionSize(140);
    csv_table_->verticalHeader()->setVisible(false);
    csvLayout->addWidget(csv_table_, 1);

    summaryWaveSplitter->addWidget(csv_group_);
    summaryWaveSplitter->setStretchFactor(0, 2);
    summaryWaveSplitter->setStretchFactor(1, 3);

    mainLayout->addWidget(summaryWaveSplitter, 1);
}

void SessionViewerWindow::setEnglish(bool english)
{
    is_english_ = english;
    updateTexts();
}

void SessionViewerWindow::updateTexts()
{
    setWindowTitle(is_english_ ? "Data Viewer" : "数据查看器");
    choose_session_btn_->setText(is_english_ ? "Open Data..." : "打开数据...");
    reload_btn_->setText(is_english_ ? "Reload" : "重新加载");
    clear_view_btn_->setText(is_english_ ? "Clear Page" : "清空页面");
    summary_group_->setTitle(is_english_ ? "Session Summary" : "会话概览");
    waveform_group_->setTitle(is_english_ ? "Normalized Second Harmonic" : "归一化二次谐波");
    waveform_plot_title_->setText(is_english_ ? "Current Frame Waveform" : "当前帧波形");
    waveform_peak_plot_title_->setText(is_english_ ? "Peak Value of Each Frame" : "每帧峰值");
    updatePeakPlotModeButtonText();
    csv_group_->setTitle(is_english_ ? "Sensors CSV" : "传感器 CSV");
    session_name_title_->setText(is_english_ ? "Session:" : "会话:");
    start_time_title_->setText(is_english_ ? "Start:" : "开始时间:");
    end_time_title_->setText(is_english_ ? "End:" : "结束时间:");
    sensor_rows_title_->setText(is_english_ ? "Sensor Rows:" : "传感器行数:");
    waveform_files_title_->setText(is_english_ ? "Wave Files:" : "波形文件数:");
    waveform_frames_title_->setText(is_english_ ? "Wave Frames:" : "波形帧数:");
    frame_title_->setText(is_english_ ? "Frame:" : "帧:");
    if (csv_table_ && csv_table_->columnCount() > 0)
    {
        csv_table_->setHorizontalHeaderItem(0, new QTableWidgetItem(is_english_ ? "No." : "序号"));
    }

    if (session_directory_.isEmpty())
    {
        setStatusText(is_english_ ? "Choose a session directory to inspect recorded CSV and waveform files."
                                  : "请选择一个 session 目录来查看录制的 CSV 和波形文件。");
        csv_info_label_->setText(is_english_ ? "No CSV loaded" : "尚未加载 CSV");
        frame_info_label_->setText(is_english_ ? "No waveform frame loaded" : "尚未加载波形帧");
    }
    else
    {
        updateSummaryLabels();
        updateWaveformControls();
    }
}

void SessionViewerWindow::updatePeakPlotModeButtonText()
{
    if (!waveform_peak_mode_btn_)
    {
        return;
    }

    waveform_peak_mode_btn_->setText(waveform_peak_scatter_mode_
        ? (is_english_ ? "Show Polyline" : "切换到折线图")
        : (is_english_ ? "Show Scatter" : "切换到散点图"));
}

void SessionViewerWindow::setStatusText(const QString& text)
{
    if (status_label_)
    {
        status_label_->setText(text);
    }
}

void SessionViewerWindow::clearLoadedData(bool clearPathEdit)
{
    session_directory_.clear();
    metadata_filename_.clear();
    sensors_csv_filename_.clear();
    waveform_directory_.clear();
    session_name_.clear();
    start_time_utc_.clear();
    end_time_utc_.clear();
    csv_headers_.clear();
    csv_timestamps_us_.clear();
    waveform_segments_.clear();
    waveform_peak_values_.clear();
    total_sensor_rows_ = 0;
    total_waveform_frames_ = 0;
    points_per_frame_ = 50000;
    waveform_export_rate_hz_ = 10;

    csv_table_->clearContents();
    csv_table_->setRowCount(0);
    csv_table_->setColumnCount(0);
    highlighted_csv_rows_.clear();
    static_cast<SessionWavePlotWidget*>(waveform_plot_)->setSamples({});
    static_cast<SessionPeakPlotWidget*>(waveform_peak_plot_)->setPeakValues({});
    static_cast<SessionPeakPlotWidget*>(waveform_peak_plot_)->setCurrentFrame(-1);
    frame_info_label_->setText(is_english_ ? "No waveform frame loaded" : "尚未加载波形帧");
    csv_info_label_->setText(is_english_ ? "No CSV loaded" : "尚未加载 CSV");
    updateSummaryLabels();
    updateWaveformControls();
    if (clearPathEdit && session_path_edit_)
    {
        session_path_edit_->clear();
    }
    setStatusText(is_english_ ? "The current page has been cleared." : "当前页面内容已清空。");
}

QString SessionViewerWindow::resolveSessionDirectory(const QString& path) const
{
    if (path.isEmpty())
    {
        return QString();
    }

    QFileInfo info(path);
    if (info.isDir())
    {
        return QDir::fromNativeSeparators(info.absoluteFilePath());
    }
    if (info.isFile() && info.fileName().compare(QStringLiteral("session.json"), Qt::CaseInsensitive) == 0)
    {
        return QDir::fromNativeSeparators(info.absolutePath());
    }
    return QString();
}

bool SessionViewerWindow::openSessionPath(const QString& path)
{
    const QString sessionDirectory = resolveSessionDirectory(path);
    if (sessionDirectory.isEmpty())
    {
        setStatusText(is_english_ ? "The selected path is not a session directory or session.json file."
                                  : "选择的路径不是有效的 session 目录或 session.json 文件。");
        return false;
    }

    if (!loadSessionDirectory(sessionDirectory))
    {
        return false;
    }

    QSettings settings("VaporView", "SessionViewer");
    settings.setValue("last_session_directory", sessionDirectory);
    return true;
}

void SessionViewerWindow::onChooseSessionClicked()
{
    QSettings settings("VaporView", "SessionViewer");
    const QString initialDir = settings.value("last_session_directory", QDir::currentPath()).toString();
    const QString sessionDirectory = QFileDialog::getExistingDirectory(
        this,
        is_english_ ? "Choose Data Directory" : "选择数据目录",
        initialDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!sessionDirectory.isEmpty())
    {
        openSessionPath(sessionDirectory);
    }
}

void SessionViewerWindow::onReloadClicked()
{
    if (session_directory_.isEmpty())
    {
        setStatusText(is_english_ ? "No session is currently loaded." : "当前没有已加载的会话。");
        return;
    }

    loadSessionDirectory(session_directory_);
}

bool SessionViewerWindow::loadSessionDirectory(const QString& sessionDirectory)
{
    clearLoadedData(false);

    const QString normalized = QDir::fromNativeSeparators(sessionDirectory);
    if (!loadSessionMetadata(normalized))
    {
        return false;
    }
    if (!loadSensorsCsv())
    {
        return false;
    }
    if (!loadWaveformSegments())
    {
        return false;
    }
    if (!loadWaveformPeakSeries())
    {
        return false;
    }

    session_directory_ = normalized;
    session_path_edit_->setText(session_directory_);
    updateSummaryLabels();
    updateWaveformControls();

    if (total_waveform_frames_ > 0)
    {
        onFrameSpinChanged(1);
    }
    else
    {
        static_cast<SessionWavePlotWidget*>(waveform_plot_)->setSamples({});
        frame_info_label_->setText(is_english_ ? "No waveform frame file was found in this session."
                                               : "这个会话里没有找到波形帧文件。");
    }

    setStatusText(QString(is_english_ ? "Loaded session: %1" : "已加载会话: %1").arg(session_directory_));
    return true;
}

void SessionViewerWindow::onClearViewClicked()
{
    clearLoadedData(true);
}

bool SessionViewerWindow::loadSessionMetadata(const QString& sessionDirectory)
{
    const QString metadataPath = QDir(sessionDirectory).filePath(QStringLiteral("session.json"));
    QFile file(metadataPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(this,
                             is_english_ ? "Open Data" : "打开数据",
                             QString(is_english_ ? "Failed to open %1" : "无法打开 %1").arg(metadataPath));
        setStatusText(QString(is_english_ ? "Failed to open session.json: %1" : "打开 session.json 失败: %1").arg(metadataPath));
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
    {
        QMessageBox::warning(this,
                             is_english_ ? "Open Data" : "打开数据",
                             QString(is_english_ ? "Invalid session.json: %1" : "session.json 无效: %1").arg(metadataPath));
        setStatusText(QString(is_english_ ? "Invalid session metadata: %1" : "session 元数据无效: %1").arg(metadataPath));
        return false;
    }

    const QJsonObject root = document.object();
    const QJsonObject paths = root.value(QStringLiteral("paths")).toObject();

    metadata_filename_ = metadataPath;
    session_name_ = root.value(QStringLiteral("session_name")).toString(QFileInfo(sessionDirectory).fileName());
    start_time_utc_ = root.value(QStringLiteral("start_time_utc")).toString();
    end_time_utc_ = root.value(QStringLiteral("end_time_utc")).toString();
    total_sensor_rows_ = root.value(QStringLiteral("sensor_rows")).toVariant().toULongLong();
    total_waveform_frames_ = root.value(QStringLiteral("waveform_frames")).toVariant().toULongLong();
    points_per_frame_ = root.value(QStringLiteral("waveform_points_per_frame")).toInt(50000);
    waveform_export_rate_hz_ = root.value(QStringLiteral("waveform_export_rate_hz")).toInt(10);

    const QString csvRelativePath = paths.value(QStringLiteral("devices_csv")).toString(QStringLiteral("sensors/devices.csv"));
    const QString waveformRelativePath = paths.value(QStringLiteral("waveform_directory")).toString(QStringLiteral("waveform"));
    sensors_csv_filename_ = QDir(sessionDirectory).filePath(csvRelativePath);
    waveform_directory_ = QDir(sessionDirectory).filePath(waveformRelativePath);
    return true;
}

bool SessionViewerWindow::loadSensorsCsv()
{
    csv_table_->clearContents();
    csv_table_->setRowCount(0);
    csv_table_->setColumnCount(0);
    highlighted_csv_rows_.clear();
    csv_headers_.clear();
    csv_timestamps_us_.clear();

    QFile file(sensors_csv_filename_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        setStatusText(QString(is_english_ ? "Failed to open sensors CSV: %1" : "打开传感器 CSV 失败: %1").arg(sensors_csv_filename_));
        csv_info_label_->setText(is_english_ ? "The session metadata is valid, but sensors/devices.csv could not be opened."
                                             : "session 元数据是有效的，但 sensors/devices.csv 无法打开。");
        return true;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    if (stream.atEnd())
    {
        csv_info_label_->setText(is_english_ ? "devices.csv is empty." : "devices.csv 为空。");
        return true;
    }

    csv_headers_ = parseCsvLine(stream.readLine());
    QStringList displayHeaders;
    displayHeaders.reserve(csv_headers_.size() + 1);
    displayHeaders << (is_english_ ? "No." : "序号");
    displayHeaders << csv_headers_;
    csv_table_->setColumnCount(displayHeaders.size());
    csv_table_->setHorizontalHeaderLabels(displayHeaders);

    QVector<QStringList> rows;
    rows.reserve(static_cast<int>(std::min<quint64>(total_sensor_rows_ > 0 ? total_sensor_rows_ : 256ULL, 50000ULL)));
    while (!stream.atEnd())
    {
        const QString line = stream.readLine();
        if (line.isEmpty())
        {
            continue;
        }

        QStringList fields = parseCsvLine(line);
        while (fields.size() < csv_headers_.size())
        {
            fields.push_back(QString());
        }
        rows.push_back(fields);

        bool ok = false;
        csv_timestamps_us_.push_back(csvValueAt(fields, 0).toULongLong(&ok));
        if (!ok)
        {
            csv_timestamps_us_.last() = 0;
        }
    }

    csv_table_->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row)
    {
        auto *indexItem = new QTableWidgetItem(QString::number(row + 1));
        indexItem->setBackground(kDefaultCsvRowColor);
        csv_table_->setItem(row, 0, indexItem);

        const QStringList& fields = rows.at(row);
        for (int col = 0; col < csv_headers_.size(); ++col)
        {
            auto *item = new QTableWidgetItem(csvValueAt(fields, col));
            item->setBackground(kDefaultCsvRowColor);
            csv_table_->setItem(row, col + 1, item);
        }
    }

    total_sensor_rows_ = static_cast<quint64>(rows.size());
    csv_info_label_->setText(QString(is_english_
        ? "Loaded %1 CSV rows from %2"
        : "已从 %2 加载 %1 行 CSV")
        .arg(total_sensor_rows_)
        .arg(QDir::toNativeSeparators(sensors_csv_filename_)));
    return true;
}

bool SessionViewerWindow::loadWaveformSegments()
{
    waveform_segments_.clear();
    total_waveform_frames_ = 0;

    QDir dir(waveform_directory_);
    if (!dir.exists())
    {
        setStatusText(QString(is_english_ ? "Waveform directory does not exist: %1" : "波形目录不存在: %1").arg(waveform_directory_));
        return true;
    }

    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.dat"), QDir::Files, QDir::Name);
    const quint64 frameBytes = kWaveformTimestampBytes + static_cast<quint64>(points_per_frame_) * kFloatBytes;

    for (const QString& filename : files)
    {
        const QString absolutePath = dir.filePath(filename);
        const QFileInfo info(absolutePath);
        if (frameBytes == 0 || info.size() < static_cast<qint64>(frameBytes))
        {
            continue;
        }

        const quint64 frameCount = static_cast<quint64>(info.size()) / frameBytes;
        if (frameCount == 0)
        {
            continue;
        }

        WaveformSegment segment;
        segment.filename = absolutePath;
        segment.start_frame = total_waveform_frames_;
        segment.frame_count = frameCount;
        waveform_segments_.push_back(segment);
        total_waveform_frames_ += frameCount;
    }

    return true;
}

bool SessionViewerWindow::loadWaveformPeakSeries()
{
    waveform_peak_values_.clear();
    static_cast<SessionPeakPlotWidget*>(waveform_peak_plot_)->setPeakValues({});
    static_cast<SessionPeakPlotWidget*>(waveform_peak_plot_)->setCurrentFrame(-1);

    if (waveform_segments_.isEmpty() || points_per_frame_ <= 0)
    {
        return true;
    }

    const quint64 frameBytes = kWaveformTimestampBytes + static_cast<quint64>(points_per_frame_) * kFloatBytes;
    QVector<float> frameSamples(points_per_frame_);
    waveform_peak_values_.reserve(static_cast<int>(std::min<quint64>(total_waveform_frames_, static_cast<quint64>(std::numeric_limits<int>::max()))));

    for (const WaveformSegment& segment : waveform_segments_)
    {
        QFile file(segment.filename);
        if (!file.open(QIODevice::ReadOnly))
        {
            setStatusText(QString(is_english_ ? "Failed to scan waveform file: %1" : "扫描波形文件失败: %1").arg(segment.filename));
            return false;
        }

        for (quint64 frame = 0; frame < segment.frame_count; ++frame)
        {
            const QByteArray block = file.read(static_cast<qint64>(frameBytes));
            if (block.size() != static_cast<int>(frameBytes))
            {
                setStatusText(QString(is_english_ ? "Incomplete waveform frame in %1" : "%1 中的波形帧不完整").arg(segment.filename));
                return false;
            }

            std::memcpy(frameSamples.data(), block.constData() + sizeof(quint64), static_cast<size_t>(points_per_frame_) * sizeof(float));
            const auto peakIt = std::max_element(frameSamples.cbegin(), frameSamples.cend());
            waveform_peak_values_.push_back(peakIt == frameSamples.cend() ? 0.0f : *peakIt);
        }
    }

    static_cast<SessionPeakPlotWidget*>(waveform_peak_plot_)->setPeakValues(waveform_peak_values_);
    return true;
}

void SessionViewerWindow::updateSummaryLabels()
{
    session_name_value_->setText(session_name_.isEmpty() ? QStringLiteral("---") : session_name_);
    start_time_value_->setText(start_time_utc_.isEmpty() ? QStringLiteral("---") : start_time_utc_);
    end_time_value_->setText(end_time_utc_.isEmpty() ? QStringLiteral("---") : end_time_utc_);
    sensor_rows_value_->setText(QString::number(total_sensor_rows_));
    waveform_files_value_->setText(QString::number(waveform_segments_.size()));
    waveform_frames_value_->setText(QString::number(total_waveform_frames_));
}

void SessionViewerWindow::updateWaveformControls()
{
    const bool hasFrames = total_waveform_frames_ > 0 && !waveform_segments_.isEmpty();
    const QSignalBlocker sliderBlocker(frame_slider_);
    const QSignalBlocker spinBlocker(frame_spin_);
    frame_slider_->setEnabled(hasFrames);
    frame_spin_->setEnabled(hasFrames);
    if (hasFrames)
    {
        const int maxFrame = static_cast<int>(std::min<quint64>(total_waveform_frames_, static_cast<quint64>(std::numeric_limits<int>::max())));
        frame_slider_->setRange(1, maxFrame);
        frame_spin_->setRange(1, maxFrame);
        if (frame_spin_->value() < 1 || frame_spin_->value() > maxFrame)
        {
            frame_spin_->setValue(1);
            frame_slider_->setValue(1);
        }
    }
    else
    {
        frame_slider_->setRange(0, 0);
        frame_spin_->setRange(0, 0);
        frame_slider_->setValue(0);
        frame_spin_->setValue(0);
    }

    frame_total_label_->setText(hasFrames
        ? QStringLiteral("/ %1").arg(total_waveform_frames_)
        : QStringLiteral("/ 0"));
}

void SessionViewerWindow::onFrameSliderChanged(int value)
{
    if (updating_frame_controls_)
    {
        return;
    }

    updating_frame_controls_ = true;
    frame_spin_->setValue(value);
    updating_frame_controls_ = false;
    if (value > 0)
    {
        loadWaveformFrame(static_cast<quint64>(value - 1));
    }
}

void SessionViewerWindow::onFrameSpinChanged(int value)
{
    if (updating_frame_controls_)
    {
        return;
    }

    updating_frame_controls_ = true;
    frame_slider_->setValue(value);
    updating_frame_controls_ = false;
    if (value > 0)
    {
        loadWaveformFrame(static_cast<quint64>(value - 1));
    }
}

void SessionViewerWindow::onTogglePeakPlotModeClicked()
{
    waveform_peak_scatter_mode_ = !waveform_peak_scatter_mode_;
    updatePeakPlotModeButtonText();
    static_cast<SessionPeakPlotWidget*>(waveform_peak_plot_)->setPlotMode(
        waveform_peak_scatter_mode_ ? SessionPeakPlotWidget::PlotMode::Scatter : SessionPeakPlotWidget::PlotMode::Polyline);
}

bool SessionViewerWindow::loadWaveformFrame(quint64 frameIndex)
{
    if (waveform_segments_.isEmpty() || frameIndex >= total_waveform_frames_)
    {
        return false;
    }

    const auto it = std::find_if(waveform_segments_.cbegin(), waveform_segments_.cend(), [frameIndex](const WaveformSegment& segment) {
        return frameIndex >= segment.start_frame && frameIndex < segment.start_frame + segment.frame_count;
    });
    if (it == waveform_segments_.cend())
    {
        return false;
    }

    const quint64 localFrame = frameIndex - it->start_frame;
    const quint64 frameBytes = kWaveformTimestampBytes + static_cast<quint64>(points_per_frame_) * kFloatBytes;
    const quint64 offset = localFrame * frameBytes;

    QFile file(it->filename);
    if (!file.open(QIODevice::ReadOnly) || !file.seek(static_cast<qint64>(offset)))
    {
        setStatusText(QString(is_english_ ? "Failed to read waveform file: %1" : "读取波形文件失败: %1").arg(it->filename));
        return false;
    }

    const QByteArray block = file.read(static_cast<qint64>(frameBytes));
    if (block.size() != static_cast<int>(frameBytes))
    {
        setStatusText(QString(is_english_ ? "Incomplete waveform frame in %1" : "%1 中的波形帧不完整").arg(it->filename));
        return false;
    }

    quint64 timestampUs = 0;
    std::memcpy(&timestampUs, block.constData(), sizeof(quint64));

    QVector<float> samples(points_per_frame_);
    std::memcpy(samples.data(), block.constData() + sizeof(quint64), static_cast<size_t>(points_per_frame_) * sizeof(float));
    static_cast<SessionWavePlotWidget*>(waveform_plot_)->setSamples(samples);
    static_cast<SessionPeakPlotWidget*>(waveform_peak_plot_)->setCurrentFrame(static_cast<int>(frameIndex));

    const auto minMax = std::minmax_element(samples.cbegin(), samples.cend());
    const float peakValue = frameIndex < static_cast<quint64>(waveform_peak_values_.size())
        ? waveform_peak_values_.at(static_cast<int>(frameIndex))
        : *minMax.second;
    const QString frameTime = formatTimestampUs(timestampUs);
    const QString csvMatchText = highlightClosestSensorRow(timestampUs);
    frame_info_label_->setText(QString(is_english_
        ? "Frame %1 / %2 | %3 | %4 Hz export | min=%5 max=%6 peak=%7 | %8"
        : "第 %1 / %2 帧 | %3 | %4 Hz 导出 | min=%5 max=%6 峰值=%7 | %8")
        .arg(frameIndex + 1)
        .arg(total_waveform_frames_)
        .arg(frameTime)
        .arg(waveform_export_rate_hz_)
        .arg(QString::number(*minMax.first, 'f', 6))
        .arg(QString::number(*minMax.second, 'f', 6))
        .arg(QString::number(peakValue, 'f', 6))
        .arg(QFileInfo(it->filename).fileName())
        + (csvMatchText.isEmpty() ? QString() : QStringLiteral(" | ") + csvMatchText));
    return true;
}

QString SessionViewerWindow::highlightClosestSensorRow(quint64 timestampUs)
{
    if (csv_timestamps_us_.isEmpty() || csv_table_->rowCount() == 0)
    {
        return QString();
    }

    const auto it = std::lower_bound(csv_timestamps_us_.cbegin(), csv_timestamps_us_.cend(), timestampUs);
    QVector<int> rowsToHighlight;
    rowsToHighlight.reserve(2);
    if (it == csv_timestamps_us_.cbegin())
    {
        rowsToHighlight.push_back(0);
        if (csv_timestamps_us_.size() > 1)
        {
            rowsToHighlight.push_back(1);
        }
    }
    else if (it == csv_timestamps_us_.cend())
    {
        if (csv_timestamps_us_.size() > 1)
        {
            rowsToHighlight.push_back(csv_timestamps_us_.size() - 2);
        }
        rowsToHighlight.push_back(csv_timestamps_us_.size() - 1);
    }
    else
    {
        const int lowerIndex = static_cast<int>(it - csv_timestamps_us_.cbegin());
        rowsToHighlight.push_back(lowerIndex - 1);
        rowsToHighlight.push_back(lowerIndex);
    }

    std::sort(rowsToHighlight.begin(), rowsToHighlight.end());
    rowsToHighlight.erase(std::unique(rowsToHighlight.begin(), rowsToHighlight.end()), rowsToHighlight.end());

    for (int previousRow : highlighted_csv_rows_)
    {
        if (previousRow < 0 || previousRow >= csv_table_->rowCount() || rowsToHighlight.contains(previousRow))
        {
            continue;
        }
        for (int col = 0; col < csv_table_->columnCount(); ++col)
        {
            if (QTableWidgetItem *item = csv_table_->item(previousRow, col))
            {
                item->setBackground(kDefaultCsvRowColor);
            }
        }
    }

    int primaryRow = rowsToHighlight.isEmpty() ? -1 : rowsToHighlight.first();
    qint64 primaryAbsDelta = std::numeric_limits<qint64>::max();
    for (int row : rowsToHighlight)
    {
        if (row < 0 || row >= csv_timestamps_us_.size())
        {
            continue;
        }
        const qint64 deltaUs = static_cast<qint64>(csv_timestamps_us_.at(row)) - static_cast<qint64>(timestampUs);
        const qint64 absDeltaUs = std::llabs(deltaUs);
        if (absDeltaUs < primaryAbsDelta)
        {
            primaryAbsDelta = absDeltaUs;
            primaryRow = row;
        }
    }

    QStringList matchParts;
    for (int row : rowsToHighlight)
    {
        const QColor rowColor = (row == primaryRow) ? kHighlightedCsvRowColor : kSecondaryHighlightedCsvRowColor;
        for (int col = 0; col < csv_table_->columnCount(); ++col)
        {
            if (QTableWidgetItem *item = csv_table_->item(row, col))
            {
                item->setBackground(rowColor);
            }
        }

        const qint64 deltaUs = static_cast<qint64>(csv_timestamps_us_.at(row)) - static_cast<qint64>(timestampUs);
        matchParts.append(is_english_
            ? QString("CSV row %1 (%2)").arg(row + 1).arg(formatSignedDeltaMs(deltaUs))
            : QString("CSV 第%1行（%2）").arg(row + 1).arg(formatSignedDeltaMs(deltaUs)));
    }

    highlighted_csv_rows_ = rowsToHighlight;
    if (primaryRow >= 0)
    {
        csv_table_->selectRow(primaryRow);
        csv_table_->setCurrentCell(primaryRow, 0, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        const int topVisibleRow = rowsToHighlight.isEmpty() ? primaryRow : rowsToHighlight.first();
        if (QTableWidgetItem *item = csv_table_->item(topVisibleRow, 0))
        {
            csv_table_->scrollToItem(item, QAbstractItemView::PositionAtTop);
        }
    }
    csv_table_->viewport()->update();
    return matchParts.join(is_english_ ? " | " : " | ");
}
