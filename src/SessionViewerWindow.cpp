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
#include <QResizeEvent>
#include <QSettings>
#include <QShowEvent>
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

QString formatDurationText(const QString& startUtc, const QString& endUtc, bool english)
{
    const QDateTime start = QDateTime::fromString(startUtc, Qt::ISODate);
    const QDateTime end = QDateTime::fromString(endUtc, Qt::ISODate);
    if (!start.isValid() || !end.isValid() || end < start)
    {
        return QStringLiteral("---");
    }

    qint64 seconds = start.secsTo(end);
    const qint64 hours = seconds / 3600;
    seconds %= 3600;
    const qint64 minutes = seconds / 60;
    seconds %= 60;

    QStringList parts;
    if (hours > 0)
    {
        parts << (english ? QString("%1h").arg(hours) : QString("%1小时").arg(hours));
    }
    if (minutes > 0 || hours > 0)
    {
        parts << (english ? QString("%1m").arg(minutes) : QString("%1分").arg(minutes));
    }
    parts << (english ? QString("%1s").arg(seconds) : QString("%1秒").arg(seconds));
    return parts.join(' ');
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

int findHeaderIndex(const QStringList& headers, const QStringList& candidates)
{
    for (const QString& candidate : candidates)
    {
        const int index = headers.indexOf(candidate);
        if (index >= 0)
        {
            return index;
        }
    }
    return -1;
}

bool parseBooleanCsvField(const QString& value, bool defaultValue = false)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized.isEmpty())
    {
        return defaultValue;
    }
    if (normalized == QStringLiteral("true") || normalized == QStringLiteral("1") || normalized == QStringLiteral("yes"))
    {
        return true;
    }
    if (normalized == QStringLiteral("false") || normalized == QStringLiteral("0") || normalized == QStringLiteral("no"))
    {
        return false;
    }
    return defaultValue;
}

double parseOptionalDouble(const QString& value)
{
    bool ok = false;
    const double parsed = value.toDouble(&ok);
    return ok ? parsed : std::numeric_limits<double>::quiet_NaN();
}

QString formatOptionalSeriesValue(double value, int decimals, const QString& unit = QString())
{
    if (!std::isfinite(value))
    {
        return QStringLiteral("---");
    }
    return unit.isEmpty()
        ? QString::number(value, 'f', decimals)
        : QStringLiteral("%1 %2").arg(QString::number(value, 'f', decimals), unit);
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
        painter.setPen(QPen(QColor("#f0d000"), 1));
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

        painter.setPen(QPen(QColor("#c9b53a"), 1));
        painter.drawRect(plotRect);

        if (samples_.isEmpty())
        {
            painter.setPen(QColor("#64748b"));
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

        painter.setPen(QPen(QColor("#1b6416"), 1.4));
        painter.drawPolyline(polyline);

        painter.setPen(QColor("#334155"));
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

class SingleSeriesTrendPlotWidget : public QWidget
{
public:
    explicit SingleSeriesTrendPlotWidget(const QColor& color, const QString& emptyText, QWidget *parent = nullptr)
        : QWidget(parent)
        , line_color_(color)
        , empty_text_(emptyText)
        , current_index_(-1)
        , view_start_index_(0)
        , view_count_(0)
    {
        setMinimumHeight(130);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    void setValues(const QVector<double>& values)
    {
        values_ = values;
        if (current_index_ >= values_.size())
        {
            current_index_ = -1;
        }
        normalizeView();
        update();
    }

    void setCurrentIndex(int index)
    {
        current_index_ = index;
        update();
    }

    void setViewRange(int startIndex, int count)
    {
        if (values_.isEmpty())
        {
            view_start_index_ = 0;
            view_count_ = 0;
            update();
            return;
        }

        if (count <= 0 || count >= values_.size())
        {
            view_start_index_ = 0;
            view_count_ = 0;
        }
        else
        {
            view_start_index_ = startIndex;
            view_count_ = count;
            normalizeView();
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), QColor("#ffffff"));

        if (values_.isEmpty())
        {
            const QRectF emptyPlotRect = rect().adjusted(16, 12, -10, -28);
            painter.setPen(QPen(QColor("#cfd7e3"), 1));
            painter.drawRect(emptyPlotRect);
            painter.setPen(QColor("#7a8899"));
            painter.drawText(emptyPlotRect, Qt::AlignCenter, empty_text_);
            return;
        }

        const int startIndex = visibleStartIndex();
        const int count = visibleCount();
        const auto visibleBegin = values_.cbegin() + startIndex;
        const auto visibleEnd = visibleBegin + count;
        double minValue = std::numeric_limits<double>::infinity();
        double maxValue = -std::numeric_limits<double>::infinity();
        for (auto it = visibleBegin; it != visibleEnd; ++it)
        {
            if (!std::isfinite(*it))
            {
                continue;
            }
            minValue = std::min(minValue, *it);
            maxValue = std::max(maxValue, *it);
        }

        const bool hasFiniteValues = std::isfinite(minValue) && std::isfinite(maxValue);
        const QString maxLabel = hasFiniteValues ? QString::number(maxValue, 'f', 3) : QStringLiteral("---");
        const QString midLabel = hasFiniteValues ? QString::number((maxValue + minValue) * 0.5, 'f', 3) : QStringLiteral("---");
        const QString minLabel = hasFiniteValues ? QString::number(minValue, 'f', 3) : QStringLiteral("---");
        const QFontMetrics fm = painter.fontMetrics();
        const int labelWidth = std::max({fm.horizontalAdvance(maxLabel), fm.horizontalAdvance(midLabel), fm.horizontalAdvance(minLabel)});
        const int leftMargin = std::max(36, labelWidth + 6);
        const QRectF plotRect = rect().adjusted(leftMargin, 12, -10, -28);
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

        if (!hasFiniteValues)
        {
            painter.setPen(QColor("#7a8899"));
            painter.drawText(plotRect, Qt::AlignCenter, empty_text_);
            return;
        }

        if (current_index_ >= startIndex && current_index_ < (startIndex + count))
        {
            const int relativeIndex = current_index_ - startIndex;
            const qreal ratio = count == 1 ? 0.0 : static_cast<qreal>(relativeIndex) / static_cast<qreal>(count - 1);
            const qreal x = plotRect.left() + ratio * plotRect.width();
            painter.setPen(QPen(QColor("#94a3b8"), 1, Qt::DashLine));
            painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
        }

        drawSeries(painter, plotRect, startIndex, count, minValue, maxValue);

        painter.setPen(QColor("#5e6b78"));
        painter.drawText(QRectF(4, plotRect.top() - 2, leftMargin - 6, fm.height()), Qt::AlignRight | Qt::AlignVCenter, maxLabel);
        painter.drawText(QRectF(4, plotRect.center().y() - fm.height() * 0.5, leftMargin - 6, fm.height()), Qt::AlignRight | Qt::AlignVCenter, midLabel);
        painter.drawText(QRectF(4, plotRect.bottom() - fm.height() + 2, leftMargin - 6, fm.height()), Qt::AlignRight | Qt::AlignVCenter, minLabel);
        painter.drawText(QRectF(plotRect.left(), plotRect.bottom() + 4, plotRect.width() * 0.55, 16),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QString("%1-%2 / %3").arg(startIndex + 1).arg(startIndex + count).arg(values_.size()));
        painter.drawText(QRectF(plotRect.left(), plotRect.bottom() + 4, plotRect.width(), 16),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("%1 samples").arg(count));
    }

private:
    int visibleStartIndex() const
    {
        if (values_.isEmpty())
        {
            return 0;
        }
        return std::clamp(view_start_index_, 0, std::max(0, static_cast<int>(values_.size()) - visibleCount()));
    }

    int visibleCount() const
    {
        const int totalCount = static_cast<int>(values_.size());
        if (totalCount <= 0)
        {
            return 0;
        }
        if (view_count_ <= 0 || view_count_ >= totalCount)
        {
            return totalCount;
        }
        return std::clamp(view_count_, 1, totalCount);
    }

    void normalizeView()
    {
        const int totalCount = static_cast<int>(values_.size());
        if (totalCount <= 0 || view_count_ <= 0 || view_count_ >= totalCount)
        {
            view_start_index_ = 0;
            view_count_ = 0;
            return;
        }
        view_count_ = std::clamp(view_count_, 1, totalCount);
        view_start_index_ = std::clamp(view_start_index_, 0, std::max(0, totalCount - view_count_));
    }

    void drawSeries(QPainter& painter,
                    const QRectF& plotRect,
                    int startIndex,
                    int count,
                    double minValue,
                    double maxValue)
    {
        QPolygonF segment;
        painter.setPen(QPen(line_color_, 1.5));
        for (int relativeIndex = 0; relativeIndex < count; ++relativeIndex)
        {
            const int i = startIndex + relativeIndex;
            const double value = values_.at(i);
            if (!std::isfinite(value))
            {
                if (segment.size() >= 2)
                {
                    painter.drawPolyline(segment);
                }
                segment.clear();
                continue;
            }

            const qreal x = plotRect.left() + (count == 1 ? 0.0 : (static_cast<qreal>(relativeIndex) / static_cast<qreal>(count - 1)) * plotRect.width());
            const qreal normalized = (value - minValue) / std::max(1e-9, maxValue - minValue);
            const qreal y = plotRect.bottom() - normalized * plotRect.height();
            segment.append(QPointF(x, y));
        }
        if (segment.size() >= 2)
        {
            painter.drawPolyline(segment);
        }
        else if (segment.size() == 1)
        {
            painter.drawPoint(segment.first());
        }

        if (current_index_ >= startIndex && current_index_ < (startIndex + count) && std::isfinite(values_.at(current_index_)))
        {
            const int relativeIndex = current_index_ - startIndex;
            const qreal x = plotRect.left() + (count == 1 ? 0.0 : (static_cast<qreal>(relativeIndex) / static_cast<qreal>(count - 1)) * plotRect.width());
            const qreal normalized = (values_.at(current_index_) - minValue) / std::max(1e-9, maxValue - minValue);
            const qreal y = plotRect.bottom() - normalized * plotRect.height();
            painter.setBrush(line_color_);
            painter.drawEllipse(QPointF(x, y), 3.0, 3.0);
        }
    }

    QColor line_color_;
    QString empty_text_;
    QVector<double> values_;
    int current_index_;
    int view_start_index_;
    int view_count_;
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
    , summary_layout_(nullptr)
    , session_name_title_(nullptr)
    , session_name_value_(nullptr)
    , start_time_title_(nullptr)
    , start_time_value_(nullptr)
    , end_time_title_(nullptr)
    , end_time_value_(nullptr)
    , duration_title_(nullptr)
    , duration_value_(nullptr)
    , sensor_export_rate_title_(nullptr)
    , sensor_export_rate_value_(nullptr)
    , sensor_rows_title_(nullptr)
    , sensor_rows_value_(nullptr)
    , waveform_export_rate_title_(nullptr)
    , waveform_export_rate_value_(nullptr)
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
    , temperature_plot_title_(nullptr)
    , temperature_plot_(nullptr)
    , humidity_plot_title_(nullptr)
    , humidity_plot_(nullptr)
    , pressure_plot_title_(nullptr)
    , pressure_plot_(nullptr)
    , environment_info_label_(nullptr)
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
    , temperature_values_()
    , humidity_values_()
    , pressure_values_()
    , waveform_timestamps_us_()
    , waveform_segments_()
    , waveform_peak_values_()
    , is_english_(false)
    , updating_frame_controls_(false)
    , waveform_peak_scatter_mode_(true)
    , highlighted_csv_rows_()
    , points_per_frame_(50000)
    , sensor_export_rate_hz_(10)
    , waveform_export_rate_hz_(10)
    , waveform_export_mode_(QStringLiteral("fixed_rate"))
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
    summary_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    summary_layout_ = new QGridLayout(summary_group_);
    summary_layout_->setContentsMargins(8, 28, 8, 8);
    summary_layout_->setHorizontalSpacing(8);
    summary_layout_->setVerticalSpacing(4);

    auto createSummaryRow = [this](QLabel*& title, QLabel*& value) {
        title = new QLabel(this);
        title->setObjectName("fieldLabel");
        title->setMinimumWidth(64);
        title->setMaximumWidth(156);
        title->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        value = new QLabel("---", this);
        value->setObjectName("valueLabel");
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
    connect(waveform_peak_mode_btn_, &QPushButton::clicked, this, &SessionViewerWindow::onTogglePeakPlotModeClicked);
    waveformLayout->addWidget(waveform_peak_plot_, 1);
    waveformLayout->addWidget(waveformPeakRangeAxis);

    temperature_plot_title_ = new QLabel(this);
    temperature_plot_title_->setObjectName("fieldLabel");
    waveformLayout->addWidget(temperature_plot_title_);
    temperature_plot_ = new SingleSeriesTrendPlotWidget(QColor("#d14343"),
        is_english_ ? "No temperature series" : "没有温度趋势数据", this);
    waveformLayout->addWidget(temperature_plot_);

    humidity_plot_title_ = new QLabel(this);
    humidity_plot_title_->setObjectName("fieldLabel");
    waveformLayout->addWidget(humidity_plot_title_);
    humidity_plot_ = new SingleSeriesTrendPlotWidget(QColor("#2f7fd3"),
        is_english_ ? "No humidity series" : "没有湿度趋势数据", this);
    waveformLayout->addWidget(humidity_plot_);

    pressure_plot_title_ = new QLabel(this);
    pressure_plot_title_->setObjectName("fieldLabel");
    waveformLayout->addWidget(pressure_plot_title_);
    pressure_plot_ = new SingleSeriesTrendPlotWidget(QColor("#2f9d57"),
        is_english_ ? "No pressure series" : "没有气压趋势数据", this);
    waveformLayout->addWidget(pressure_plot_);

    environment_info_label_ = new QLabel(this);
    environment_info_label_->setWordWrap(true);
    environment_info_label_->setObjectName("fieldLabel");
    waveformLayout->addWidget(environment_info_label_);

    static_cast<SessionPeakPlotWidget*>(waveform_peak_plot_)->setViewChangedCallback(
        [this, waveformPeakRangeAxis](int totalCount, int startIndex, int visibleCount) {
            if (waveformPeakRangeAxis)
            {
                waveformPeakRangeAxis->setRange(totalCount, startIndex, visibleCount);
            }
            syncEnvironmentRangeToWaveformRange(startIndex, visibleCount);
        });
    waveformPeakRangeAxis->setRangeChangedCallback([this](int startIndex, int visibleCount) {
        static_cast<SessionPeakPlotWidget*>(waveform_peak_plot_)->setViewRange(startIndex, visibleCount);
        syncEnvironmentRangeToWaveformRange(startIndex, visibleCount);
    });
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

void SessionViewerWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    relayoutSummaryFields();
}

void SessionViewerWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    relayoutSummaryFields();
}

void SessionViewerWindow::updateTexts()
{
    setWindowTitle(is_english_ ? "Data Viewer" : "数据查看器");
    choose_session_btn_->setText(is_english_ ? "Open Data..." : "打开数据...");
    reload_btn_->setText(is_english_ ? "Reload" : "重新加载");
    clear_view_btn_->setText(is_english_ ? "Clear Page" : "清空页面");
    summary_group_->setTitle(is_english_ ? "Data Summary" : "数据概览");
    waveform_group_->setTitle(is_english_ ? "Normalized Second Harmonic" : "归一化二次谐波");
    waveform_plot_title_->setText(is_english_ ? "Current Frame Waveform" : "当前帧波形");
    waveform_peak_plot_title_->setText(is_english_ ? "Peak Value of Each Frame" : "每帧峰值");
    temperature_plot_title_->setText(is_english_ ? "Temperature" : "温度");
    humidity_plot_title_->setText(is_english_ ? "Humidity" : "湿度");
    pressure_plot_title_->setText(is_english_ ? "Pressure" : "气压");
    updatePeakPlotModeButtonText();
    csv_group_->setTitle(is_english_ ? "Sensors CSV" : "传感器 CSV");
    session_name_title_->setText(is_english_ ? "Session:" : "会话:");
    start_time_title_->setText(is_english_ ? "Start:" : "开始时间:");
    end_time_title_->setText(is_english_ ? "End:" : "结束时间:");
    duration_title_->setText(is_english_ ? "Duration:" : "记录时间:");
    sensor_export_rate_title_->setText(is_english_ ? "CSV Rate:" : "设备CSV文件记录频率:");
    sensor_rows_title_->setText(is_english_ ? "Sensor Rows:" : "传感器行数:");
    waveform_export_rate_title_->setText(is_english_ ? "Wave Rate:" : "波形记录频率:");
    waveform_files_title_->setText(is_english_ ? "Wave Files:" : "波形文件数:");
    waveform_frames_title_->setText(is_english_ ? "Wave Frames:" : "波形帧数:");
    frame_title_->setText(is_english_ ? "Frame:" : "帧:");
    if (csv_table_ && csv_table_->columnCount() > 0)
    {
        csv_table_->setHorizontalHeaderItem(0, new QTableWidgetItem(is_english_ ? "No." : "序号"));
        if (csv_table_->columnCount() > 1)
        {
            csv_table_->setHorizontalHeaderItem(1, new QTableWidgetItem(is_english_ ? "Delta" : "时间误差"));
        }
    }

    if (session_directory_.isEmpty())
    {
        setStatusText(is_english_ ? "Choose a session directory to inspect recorded CSV and waveform files."
                                  : "请选择一个 session 目录来查看录制的 CSV 和波形文件。");
        csv_info_label_->setText(is_english_ ? "No CSV loaded" : "尚未加载 CSV");
        frame_info_label_->setText(is_english_ ? "No waveform frame loaded" : "尚未加载波形帧");
        environment_info_label_->setText(is_english_ ? "No environmental series loaded" : "尚未加载环境趋势数据");
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

void SessionViewerWindow::relayoutSummaryFields()
{
    if (!summary_layout_ || !summary_group_)
    {
        return;
    }

    while (summary_layout_->count() > 0)
    {
        summary_layout_->takeAt(0);
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
    const int maxPairColumns = 6;
    for (int column = 0; column < maxPairColumns * 2; ++column)
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
        for (int index = 0; index < longFields.size(); ++index)
        {
            addFieldPair(longFields.at(index), 0, index);
        }
        for (int index = 0; index < shortFields.size(); ++index)
        {
            addFieldPair(shortFields.at(index), 1, index);
        }
        return;
    }

    if (availableWidth >= 1280)
    {
        for (int index = 0; index < longFields.size(); ++index)
        {
            addFieldPair(longFields.at(index), 0, index);
        }
        for (int index = 0; index < shortFields.size(); ++index)
        {
            addFieldPair(shortFields.at(index), 1 + index / 3, index % 3);
        }
        return;
    }

    if (availableWidth >= 980)
    {
        for (int index = 0; index < longFields.size(); ++index)
        {
            addFieldPair(longFields.at(index), 0, index);
        }
        for (int index = 0; index < shortFields.size(); ++index)
        {
            addFieldPair(shortFields.at(index), 1 + index / 2, index % 2);
        }
        return;
    }

    const QVector<QPair<QLabel*, QLabel*>> allFields = longFields + shortFields;
    const int pairColumns = availableWidth >= 640 ? 2 : 1;
    for (int index = 0; index < allFields.size(); ++index)
    {
        addFieldPair(allFields.at(index), index / pairColumns, index % pairColumns);
    }
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
    temperature_values_.clear();
    humidity_values_.clear();
    pressure_values_.clear();
    waveform_timestamps_us_.clear();
    waveform_segments_.clear();
    waveform_peak_values_.clear();
    total_sensor_rows_ = 0;
    total_waveform_frames_ = 0;
    points_per_frame_ = 50000;
    sensor_export_rate_hz_ = 10;
    waveform_export_rate_hz_ = 10;
    waveform_export_mode_ = QStringLiteral("fixed_rate");

    csv_table_->clearContents();
    csv_table_->setRowCount(0);
    csv_table_->setColumnCount(0);
    highlighted_csv_rows_.clear();
    static_cast<SessionWavePlotWidget*>(waveform_plot_)->setSamples({});
    static_cast<SessionPeakPlotWidget*>(waveform_peak_plot_)->setPeakValues({});
    static_cast<SessionPeakPlotWidget*>(waveform_peak_plot_)->setCurrentFrame(-1);
    static_cast<SingleSeriesTrendPlotWidget*>(temperature_plot_)->setValues({});
    static_cast<SingleSeriesTrendPlotWidget*>(temperature_plot_)->setCurrentIndex(-1);
    static_cast<SingleSeriesTrendPlotWidget*>(humidity_plot_)->setValues({});
    static_cast<SingleSeriesTrendPlotWidget*>(humidity_plot_)->setCurrentIndex(-1);
    static_cast<SingleSeriesTrendPlotWidget*>(pressure_plot_)->setValues({});
    static_cast<SingleSeriesTrendPlotWidget*>(pressure_plot_)->setCurrentIndex(-1);
    frame_info_label_->setText(is_english_ ? "No waveform frame loaded" : "尚未加载波形帧");
    csv_info_label_->setText(is_english_ ? "No CSV loaded" : "尚未加载 CSV");
    environment_info_label_->setText(is_english_ ? "No environmental series loaded" : "尚未加载环境趋势数据");
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
    sensor_export_rate_hz_ = root.value(QStringLiteral("sensor_export_rate_hz")).toInt(10);
    waveform_export_rate_hz_ = root.value(QStringLiteral("waveform_export_rate_hz")).toInt(10);
    waveform_export_mode_ = root.value(QStringLiteral("waveform_export_mode")).toString(
        waveform_export_rate_hz_ > 0 ? QStringLiteral("fixed_rate") : QStringLiteral("per_frame"));

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
    const int tempIndex = findHeaderIndex(csv_headers_, {QStringLiteral("temp_c")});
    const int humidityIndex = findHeaderIndex(csv_headers_, {QStringLiteral("humidity_rh")});
    const int pressureIndex = findHeaderIndex(csv_headers_, {QStringLiteral("baro_hpa"), QStringLiteral("baro_pa")});
    const int thValidIndex = findHeaderIndex(csv_headers_, {QStringLiteral("th_valid")});
    const int baroValidIndex = findHeaderIndex(csv_headers_, {QStringLiteral("baro_valid")});
    QStringList displayHeaders;
    displayHeaders.reserve(csv_headers_.size() + 2);
    displayHeaders << (is_english_ ? "No." : "序号");
    displayHeaders << (is_english_ ? "Delta" : "时间误差");
    displayHeaders << csv_headers_;
    csv_table_->setColumnCount(displayHeaders.size());
    csv_table_->setHorizontalHeaderLabels(displayHeaders);
    csv_table_->setColumnWidth(0, 48);
    csv_table_->setColumnWidth(1, 96);

    QVector<QStringList> rows;
    rows.reserve(static_cast<int>(std::min<quint64>(total_sensor_rows_ > 0 ? total_sensor_rows_ : 256ULL, 50000ULL)));
    temperature_values_.clear();
    humidity_values_.clear();
    pressure_values_.clear();
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

        const bool thValid = thValidIndex < 0 || parseBooleanCsvField(csvValueAt(fields, thValidIndex), true);
        const bool baroValid = baroValidIndex < 0 || parseBooleanCsvField(csvValueAt(fields, baroValidIndex), true);
        temperature_values_.push_back((tempIndex >= 0 && thValid) ? parseOptionalDouble(csvValueAt(fields, tempIndex)) : std::numeric_limits<double>::quiet_NaN());
        humidity_values_.push_back((humidityIndex >= 0 && thValid) ? parseOptionalDouble(csvValueAt(fields, humidityIndex)) : std::numeric_limits<double>::quiet_NaN());
        pressure_values_.push_back((pressureIndex >= 0 && baroValid) ? parseOptionalDouble(csvValueAt(fields, pressureIndex)) : std::numeric_limits<double>::quiet_NaN());
    }

    csv_table_->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row)
    {
        auto *indexItem = new QTableWidgetItem(QString::number(row + 1));
        indexItem->setBackground(kDefaultCsvRowColor);
        csv_table_->setItem(row, 0, indexItem);

        auto *deltaItem = new QTableWidgetItem(QString());
        deltaItem->setBackground(kDefaultCsvRowColor);
        csv_table_->setItem(row, 1, deltaItem);

        const QStringList& fields = rows.at(row);
        for (int col = 0; col < csv_headers_.size(); ++col)
        {
            auto *item = new QTableWidgetItem(csvValueAt(fields, col));
            item->setBackground(kDefaultCsvRowColor);
            csv_table_->setItem(row, col + 2, item);
        }
    }

    total_sensor_rows_ = static_cast<quint64>(rows.size());
    csv_info_label_->setText(QString(is_english_
        ? "Loaded %1 CSV rows from %2"
        : "已从 %2 加载 %1 行 CSV")
        .arg(total_sensor_rows_)
        .arg(QDir::toNativeSeparators(sensors_csv_filename_)));

    static_cast<SingleSeriesTrendPlotWidget*>(temperature_plot_)->setValues(temperature_values_);
    static_cast<SingleSeriesTrendPlotWidget*>(temperature_plot_)->setCurrentIndex(-1);
    static_cast<SingleSeriesTrendPlotWidget*>(humidity_plot_)->setValues(humidity_values_);
    static_cast<SingleSeriesTrendPlotWidget*>(humidity_plot_)->setCurrentIndex(-1);
    static_cast<SingleSeriesTrendPlotWidget*>(pressure_plot_)->setValues(pressure_values_);
    static_cast<SingleSeriesTrendPlotWidget*>(pressure_plot_)->setCurrentIndex(-1);
    const bool hasEnvironmentSeries =
        std::any_of(temperature_values_.cbegin(), temperature_values_.cend(), [](double value) { return std::isfinite(value); }) ||
        std::any_of(humidity_values_.cbegin(), humidity_values_.cend(), [](double value) { return std::isfinite(value); }) ||
        std::any_of(pressure_values_.cbegin(), pressure_values_.cend(), [](double value) { return std::isfinite(value); });
    environment_info_label_->setText(hasEnvironmentSeries
        ? (is_english_
            ? "Loaded temperature, humidity, and pressure trend series."
            : "已加载温度、湿度和气压趋势。")
        : (is_english_
            ? "No temperature, humidity, or pressure columns were found in this CSV."
            : "这个 CSV 中没有找到温度、湿度或气压列。"));
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
    waveform_timestamps_us_.clear();
    static_cast<SessionPeakPlotWidget*>(waveform_peak_plot_)->setPeakValues({});
    static_cast<SessionPeakPlotWidget*>(waveform_peak_plot_)->setCurrentFrame(-1);

    if (waveform_segments_.isEmpty() || points_per_frame_ <= 0)
    {
        return true;
    }

    const quint64 frameBytes = kWaveformTimestampBytes + static_cast<quint64>(points_per_frame_) * kFloatBytes;
    QVector<float> frameSamples(points_per_frame_);
    waveform_peak_values_.reserve(static_cast<int>(std::min<quint64>(total_waveform_frames_, static_cast<quint64>(std::numeric_limits<int>::max()))));
    waveform_timestamps_us_.reserve(static_cast<int>(std::min<quint64>(total_waveform_frames_, static_cast<quint64>(std::numeric_limits<int>::max()))));

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

            quint64 timestampUs = 0;
            std::memcpy(&timestampUs, block.constData(), sizeof(quint64));
            waveform_timestamps_us_.push_back(timestampUs);
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
    const bool hasSession = !session_name_.isEmpty() || !metadata_filename_.isEmpty();
    session_name_value_->setText(session_name_.isEmpty() ? QStringLiteral("---") : session_name_);
    start_time_value_->setText(start_time_utc_.isEmpty() ? QStringLiteral("---") : start_time_utc_);
    end_time_value_->setText(end_time_utc_.isEmpty() ? QStringLiteral("---") : end_time_utc_);
    duration_value_->setText(hasSession ? formatDurationText(start_time_utc_, end_time_utc_, is_english_) : QStringLiteral("---"));
    sensor_export_rate_value_->setText(hasSession && sensor_export_rate_hz_ > 0
        ? QStringLiteral("%1 Hz").arg(sensor_export_rate_hz_)
        : QStringLiteral("---"));
    sensor_rows_value_->setText(hasSession ? QString::number(total_sensor_rows_) : QStringLiteral("---"));
    waveform_export_rate_value_->setText(!hasSession ? QStringLiteral("---")
        : ((waveform_export_mode_ == QStringLiteral("per_frame") || waveform_export_rate_hz_ <= 0)
            ? (is_english_ ? QStringLiteral("Per-frame") : QStringLiteral("逐帧导出"))
            : QStringLiteral("%1 Hz").arg(waveform_export_rate_hz_)));
    waveform_files_value_->setText(hasSession ? QString::number(waveform_segments_.size()) : QStringLiteral("---"));
    waveform_frames_value_->setText(hasSession ? QString::number(total_waveform_frames_) : QStringLiteral("---"));
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
    const QString waveformExportText = (waveform_export_mode_ == QStringLiteral("per_frame") || waveform_export_rate_hz_ <= 0)
        ? (is_english_ ? QStringLiteral("per-frame export") : QStringLiteral("逐帧导出"))
        : QString(is_english_ ? "%1 Hz export" : "%1 Hz 导出").arg(waveform_export_rate_hz_);
    frame_info_label_->setText(QString(is_english_
        ? "Frame %1 / %2 | %3 | %4 | min=%5 max=%6 peak=%7 | %8"
        : "第 %1 / %2 帧 | %3 | %4 | min=%5 max=%6 峰值=%7 | %8")
        .arg(frameIndex + 1)
        .arg(total_waveform_frames_)
        .arg(frameTime)
        .arg(waveformExportText)
        .arg(QString::number(*minMax.first, 'f', 6))
        .arg(QString::number(*minMax.second, 'f', 6))
        .arg(QString::number(peakValue, 'f', 6))
        .arg(QFileInfo(it->filename).fileName())
        + (csvMatchText.isEmpty() ? QString() : QStringLiteral(" | ") + csvMatchText));
    return true;
}

int SessionViewerWindow::findClosestCsvRow(quint64 timestampUs) const
{
    if (csv_timestamps_us_.isEmpty())
    {
        return -1;
    }

    const auto it = std::lower_bound(csv_timestamps_us_.cbegin(), csv_timestamps_us_.cend(), timestampUs);
    if (it == csv_timestamps_us_.cbegin())
    {
        return 0;
    }
    if (it == csv_timestamps_us_.cend())
    {
        return static_cast<int>(csv_timestamps_us_.size()) - 1;
    }

    const int upperIndex = static_cast<int>(it - csv_timestamps_us_.cbegin());
    const int lowerIndex = upperIndex - 1;
    const quint64 lowerDelta = timestampUs >= csv_timestamps_us_.at(lowerIndex)
        ? (timestampUs - csv_timestamps_us_.at(lowerIndex))
        : (csv_timestamps_us_.at(lowerIndex) - timestampUs);
    const quint64 upperDelta = timestampUs >= csv_timestamps_us_.at(upperIndex)
        ? (timestampUs - csv_timestamps_us_.at(upperIndex))
        : (csv_timestamps_us_.at(upperIndex) - timestampUs);
    return lowerDelta <= upperDelta ? lowerIndex : upperIndex;
}

void SessionViewerWindow::syncEnvironmentRangeToWaveformRange(int startFrameIndex, int visibleFrameCount)
{
    if (!temperature_plot_ || !humidity_plot_ || !pressure_plot_)
    {
        return;
    }

    if (csv_timestamps_us_.isEmpty())
    {
        static_cast<SingleSeriesTrendPlotWidget*>(temperature_plot_)->setViewRange(0, 0);
        static_cast<SingleSeriesTrendPlotWidget*>(humidity_plot_)->setViewRange(0, 0);
        static_cast<SingleSeriesTrendPlotWidget*>(pressure_plot_)->setViewRange(0, 0);
        return;
    }

    if (waveform_timestamps_us_.isEmpty() || visibleFrameCount <= 0)
    {
        static_cast<SingleSeriesTrendPlotWidget*>(temperature_plot_)->setViewRange(0, 0);
        static_cast<SingleSeriesTrendPlotWidget*>(humidity_plot_)->setViewRange(0, 0);
        static_cast<SingleSeriesTrendPlotWidget*>(pressure_plot_)->setViewRange(0, 0);
        return;
    }

    const int totalFrames = static_cast<int>(waveform_timestamps_us_.size());
    const int clampedStart = std::clamp(startFrameIndex, 0, std::max(0, totalFrames - 1));
    const int clampedEnd = std::clamp(clampedStart + visibleFrameCount - 1, clampedStart, totalFrames - 1);
    const int startCsvRow = findClosestCsvRow(waveform_timestamps_us_.at(clampedStart));
    const int endCsvRow = findClosestCsvRow(waveform_timestamps_us_.at(clampedEnd));
    if (startCsvRow < 0 || endCsvRow < 0)
    {
        return;
    }

    const int csvStart = std::min(startCsvRow, endCsvRow);
    const int csvCount = std::max(1, std::abs(endCsvRow - startCsvRow) + 1);
    static_cast<SingleSeriesTrendPlotWidget*>(temperature_plot_)->setViewRange(csvStart, csvCount);
    static_cast<SingleSeriesTrendPlotWidget*>(humidity_plot_)->setViewRange(csvStart, csvCount);
    static_cast<SingleSeriesTrendPlotWidget*>(pressure_plot_)->setViewRange(csvStart, csvCount);
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
        if (QTableWidgetItem *deltaItem = csv_table_->item(previousRow, 1))
        {
            deltaItem->setText(QString());
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
        if (QTableWidgetItem *deltaItem = csv_table_->item(row, 1))
        {
            deltaItem->setText(formatSignedDeltaMs(deltaUs));
        }
        matchParts.append(is_english_
            ? QString("CSV row %1 (%2)").arg(row + 1).arg(formatSignedDeltaMs(deltaUs))
            : QString("CSV 第%1行（%2）").arg(row + 1).arg(formatSignedDeltaMs(deltaUs)));
    }

    highlighted_csv_rows_ = rowsToHighlight;
    if (primaryRow >= 0)
    {
        static_cast<SingleSeriesTrendPlotWidget*>(temperature_plot_)->setCurrentIndex(primaryRow);
        static_cast<SingleSeriesTrendPlotWidget*>(humidity_plot_)->setCurrentIndex(primaryRow);
        static_cast<SingleSeriesTrendPlotWidget*>(pressure_plot_)->setCurrentIndex(primaryRow);
        csv_table_->selectRow(primaryRow);
        csv_table_->setCurrentCell(primaryRow, 0, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        const int topVisibleRow = rowsToHighlight.isEmpty() ? primaryRow : rowsToHighlight.first();
        if (QTableWidgetItem *item = csv_table_->item(topVisibleRow, 0))
        {
            csv_table_->scrollToItem(item, QAbstractItemView::PositionAtTop);
        }
    }
    else
    {
        static_cast<SingleSeriesTrendPlotWidget*>(temperature_plot_)->setCurrentIndex(-1);
        static_cast<SingleSeriesTrendPlotWidget*>(humidity_plot_)->setCurrentIndex(-1);
        static_cast<SingleSeriesTrendPlotWidget*>(pressure_plot_)->setCurrentIndex(-1);
    }
    csv_table_->viewport()->update();

    if (primaryRow >= 0)
    {
        environment_info_label_->setText(QString(is_english_
            ? "Red: temperature %1, blue: humidity %2, green: pressure %3 (CSV row %4)."
            : "红色: 温度 %1，蓝色: 湿度 %2，绿色: 气压 %3（CSV 第%4行）。")
            .arg(formatOptionalSeriesValue(primaryRow < temperature_values_.size() ? temperature_values_.at(primaryRow) : std::numeric_limits<double>::quiet_NaN(), 2, QStringLiteral("°C")))
            .arg(formatOptionalSeriesValue(primaryRow < humidity_values_.size() ? humidity_values_.at(primaryRow) : std::numeric_limits<double>::quiet_NaN(), 2, QStringLiteral("%RH")))
            .arg(formatOptionalSeriesValue(primaryRow < pressure_values_.size() ? pressure_values_.at(primaryRow) : std::numeric_limits<double>::quiet_NaN(), 2, QStringLiteral("hPa")))
            .arg(primaryRow + 1));
    }

    return matchParts.join(is_english_ ? " | " : " | ");
}
