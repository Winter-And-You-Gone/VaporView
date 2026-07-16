#include "ground/session/SessionViewerWidgets.h"

#include "ground/session/SessionCsv.h"
#include "shared/theme/AppTheme.h"

#include <QFontDatabase>
#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QSizePolicy>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

using VaporView::AppThemeColor;
using VaporView::appThemeColor;

namespace VaporView::Ground::SessionUi
{

using VaporView::Ground::SessionCsv::csvValueAt;

constexpr int kSessionViewerPlotHeight = 120;
constexpr int kSessionViewerPlotLeftMargin = 64;
constexpr int kSessionViewerTrendPlotLeftPadding = 8;
constexpr int kSessionViewerPlotRightMargin = 10;
constexpr int kSessionViewerPlotTopMargin = 12;
constexpr int kSessionViewerPlotBottomMargin = 28;
constexpr int kSessionViewerWaveBottomMargin = 30;
constexpr int kMaxTrendPointsPerPixel = 2;

QFont numericFontFrom(const QFont& base)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (base.pointSizeF() > 0.0)
    {
        font.setPointSizeF(base.pointSizeF());
    }
    font.setWeight(static_cast<QFont::Weight>(base.weight()));
    font.setBold(base.bold());
    return font;
}

struct SessionPlotTheme
{
    QColor background;
    QColor grid;
    QColor border;
    QColor text;
    QColor mutedText;
};

SessionPlotTheme sessionPlotThemeFor(const QWidget *widget)
{
    Q_UNUSED(widget);
    const bool dark = VaporView::isDarkThemeEnabled();
    return {
        appThemeColor(AppThemeColor::Surface, dark),
        QColor(dark ? QStringLiteral("#303030") : QStringLiteral("#E4E7EB")),
        QColor(dark ? QStringLiteral("#4A4A4A") : QStringLiteral("#CBD2D9")),
        appThemeColor(AppThemeColor::PlotText, dark),
        appThemeColor(AppThemeColor::PlotMutedText, dark)
    };
}

SessionTableTheme sessionTableThemeFor(const QWidget *widget)
{
    Q_UNUSED(widget);
    const bool dark = VaporView::isDarkThemeEnabled();
    if (dark)
    {
        const QColor primaryText = appThemeColor(AppThemeColor::TableText, false);
        return {
            appThemeColor(AppThemeColor::Surface, true),
            appThemeColor(AppThemeColor::Text, true),
            QColor(QStringLiteral("#3A3A3A")),
            appThemeColor(AppThemeColor::SurfaceRaised, true),
            appThemeColor(AppThemeColor::TextTitle, true),
            appThemeColor(AppThemeColor::Primary, true),
            primaryText,
            appThemeColor(AppThemeColor::Primary, true),
            primaryText,
            appThemeColor(AppThemeColor::TableSecondaryHighlightedRow, true),
            appThemeColor(AppThemeColor::Text, true)
        };
    }

    return {
        QColor(QStringLiteral("#FFFFFF")),
        appThemeColor(AppThemeColor::TableText, false),
        QColor(QStringLiteral("#E5E7EB")),
        QColor(QStringLiteral("#F8FAFC")),
        appThemeColor(AppThemeColor::TableText, false),
        appThemeColor(AppThemeColor::Primary, false),
        appThemeColor(AppThemeColor::TextInverse, false),
        appThemeColor(AppThemeColor::Primary, false),
        appThemeColor(AppThemeColor::TextInverse, false),
        appThemeColor(AppThemeColor::PrimarySubtle, false),
        appThemeColor(AppThemeColor::TableText, false)
    };
}

QString formatGuideValue(double value, int decimals, const QString& unit = QString())
{
    if (!std::isfinite(value))
    {
        return QStringLiteral("---");
    }
    const QString number = QString::number(value, 'f', decimals);
    return unit.isEmpty() ? number : QStringLiteral("%1 %2").arg(number, unit);
}

int dataPlotLeftMargin(const QFontMetrics& metrics,
                       const QString& maxLabel = QString(),
                       const QString& midLabel = QString(),
                       const QString& minLabel = QString())
{
    int labelWidth = metrics.horizontalAdvance(formatGuideValue(9999.999, 3));
    for (const QString& label : {maxLabel, midLabel, minLabel})
    {
        if (!label.isEmpty())
        {
            labelWidth = std::max(labelWidth, metrics.horizontalAdvance(label));
        }
    }
    return std::max(
        kSessionViewerPlotLeftMargin,
        labelWidth + kSessionViewerTrendPlotLeftPadding);
}

int trendRenderPointCount(int visibleCount, const QRectF& plotRect)
{
    const int pixelBudget = std::max(2, static_cast<int>(std::ceil(plotRect.width())) * kMaxTrendPointsPerPixel);
    return std::clamp(visibleCount, 0, pixelBudget);
}

int trendRelativeIndexForDrawPoint(int drawIndex, int drawCount, int visibleCount)
{
    if (visibleCount <= 1 || drawCount <= 1)
    {
        return 0;
    }
    return std::clamp(static_cast<int>(std::llround(
        static_cast<double>(drawIndex) * static_cast<double>(visibleCount - 1) / static_cast<double>(drawCount - 1))),
        0,
        visibleCount - 1);
}

QString formatAxisTickValue(int value)
{
    return QString::number(value);
}

void drawXAxisTicks(QPainter& painter,
                    const QRectF& plotRect,
                    int startValue,
                    int endValue,
                    int segmentCount,
                    const QColor& textColor)
{
    const int segments = std::max(1, segmentCount);
    const int span = std::max(0, endValue - startValue);
    const QFontMetrics fm = painter.fontMetrics();
    painter.save();
    painter.setPen(textColor);
    for (int i = 0; i <= segments; ++i)
    {
        const qreal ratio = static_cast<qreal>(i) / static_cast<qreal>(segments);
        const qreal x = plotRect.left() + plotRect.width() * ratio;
        const int value = startValue + static_cast<int>(std::llround(static_cast<double>(span) * ratio));
        const QString label = formatAxisTickValue(value);
        const int labelWidth = fm.horizontalAdvance(label) + 8;
        const qreal labelLeft = std::clamp(x - labelWidth * 0.5,
            plotRect.left(),
            std::max(plotRect.left(), plotRect.right() - static_cast<qreal>(labelWidth)));
        painter.drawLine(QPointF(x, plotRect.bottom()), QPointF(x, plotRect.bottom() + 4));
        painter.drawText(QRectF(labelLeft, plotRect.bottom() + 6, labelWidth, fm.height()),
            Qt::AlignHCenter | Qt::AlignVCenter,
            label);
    }
    painter.restore();
}

void drawGuideTag(QPainter& painter, const QRectF& rect, const QString& text, Qt::Alignment alignment)
{
    painter.save();
    painter.setPen(QPen(appThemeColor(AppThemeColor::PlotCurrentGuideLabelBorder, false), 1));
    painter.setBrush(appThemeColor(AppThemeColor::PlotCurrentGuideLabelFill, false));
    painter.drawRoundedRect(rect, 4.0, 4.0);
    painter.setPen(appThemeColor(AppThemeColor::PlotCurrentGuideLabelText, false));
    painter.drawText(rect.adjusted(4, 0, -4, 0), alignment | Qt::AlignVCenter, text);
    painter.restore();
}

void drawCurrentPointGuides(QPainter& painter,
                            const QRectF& plotRect,
                            const QPointF& currentPoint,
                            const QString& xLabel,
                            const QString& yLabel)
{
    painter.save();
    painter.setPen(QPen(appThemeColor(AppThemeColor::PlotCurrentGuideLine, false), 1, Qt::DashLine));
    painter.drawLine(QPointF(currentPoint.x(), plotRect.top()), QPointF(currentPoint.x(), plotRect.bottom()));
    painter.drawLine(QPointF(plotRect.left(), currentPoint.y()), QPointF(plotRect.right(), currentPoint.y()));

    const QFontMetrics fm = painter.fontMetrics();
    const int xTagWidth = std::max(40, fm.horizontalAdvance(xLabel) + 12);
    const qreal xTagLeft = std::clamp(currentPoint.x() - xTagWidth * 0.5,
        plotRect.left(), plotRect.right() - static_cast<qreal>(xTagWidth));
    drawGuideTag(painter,
        QRectF(xTagLeft, plotRect.bottom() + 2, xTagWidth, fm.height() + 2),
        xLabel,
        Qt::AlignCenter);

    const qreal yTagTop = std::clamp(currentPoint.y() - (fm.height() + 2) * 0.5,
        plotRect.top(), plotRect.bottom() - static_cast<qreal>(fm.height() + 2));
    drawGuideTag(painter,
        QRectF(2, yTagTop, plotRect.left() - 6, fm.height() + 2),
        yLabel,
        Qt::AlignRight);
    painter.restore();
}

class SessionCsvTableModelImpl final : public SessionCsvTableModel
{
public:
    explicit SessionCsvTableModelImpl(QObject *parent = nullptr)
        : SessionCsvTableModel(parent)
        , theme_(sessionTableThemeFor(nullptr))
        , primary_highlighted_row_(-1)
    {
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : rows_.size();
    }

    int columnCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : headers_.size();
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size() ||
            index.column() < 0 || index.column() >= headers_.size())
        {
            return {};
        }

        switch (role)
        {
        case Qt::DisplayRole:
            if (index.column() == 0)
            {
                return QString::number(index.row() + 1);
            }
            if (index.column() == 1)
            {
                return delta_text_by_row_.value(index.row());
            }
            return csvValueAt(rows_.at(index.row()), index.column() - 2);
        case Qt::BackgroundRole:
            return rowBackground(index.row());
        case Qt::ForegroundRole:
            return rowForeground(index.row());
        default:
            return {};
        }
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override
    {
        if (role != Qt::DisplayRole || orientation != Qt::Horizontal ||
            section < 0 || section >= headers_.size())
        {
            return {};
        }
        return headers_.at(section);
    }

    void setRows(const QStringList& headers, QVector<QStringList>&& rows)
    {
        beginResetModel();
        headers_ = headers;
        rows_ = std::move(rows);
        highlighted_rows_.clear();
        delta_text_by_row_.clear();
        primary_highlighted_row_ = -1;
        endResetModel();
    }

    void setHeaders(const QStringList& headers)
    {
        if (headers_ == headers)
        {
            return;
        }
        headers_ = headers;
        if (!headers_.isEmpty())
        {
            emit headerDataChanged(Qt::Horizontal, 0, static_cast<int>(headers_.size()) - 1);
        }
    }

    void clear()
    {
        beginResetModel();
        headers_.clear();
        rows_.clear();
        highlighted_rows_.clear();
        delta_text_by_row_.clear();
        primary_highlighted_row_ = -1;
        endResetModel();
    }

    void setTheme(const SessionTableTheme& theme)
    {
        theme_ = theme;
        if (rowCount() > 0 && columnCount() > 0)
        {
            emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1),
                             {Qt::BackgroundRole, Qt::ForegroundRole});
        }
    }

    void setHighlightedRows(const QVector<int>& rows, int primaryRow, const QHash<int, QString>& deltas)
    {
        QVector<int> changedRows = highlighted_rows_;
        changedRows += rows;
        std::sort(changedRows.begin(), changedRows.end());
        changedRows.erase(std::unique(changedRows.begin(), changedRows.end()), changedRows.end());

        highlighted_rows_ = rows;
        primary_highlighted_row_ = primaryRow;
        delta_text_by_row_ = deltas;

        if (columnCount() <= 0)
        {
            return;
        }
        for (int row : changedRows)
        {
            if (row < 0 || row >= rowCount())
            {
                continue;
            }
            emit dataChanged(index(row, 0), index(row, columnCount() - 1),
                             {Qt::DisplayRole, Qt::BackgroundRole, Qt::ForegroundRole});
        }
    }

private:
    QColor rowBackground(int row) const
    {
        if (!highlighted_rows_.contains(row))
        {
            return theme_.background;
        }
        const bool primary = row == primary_highlighted_row_ ||
            (primary_highlighted_row_ < 0 && !highlighted_rows_.isEmpty() && row == highlighted_rows_.first());
        return primary ? theme_.highlightedBackground : theme_.secondaryHighlightedBackground;
    }

    QColor rowForeground(int row) const
    {
        if (!highlighted_rows_.contains(row))
        {
            return theme_.text;
        }
        const bool primary = row == primary_highlighted_row_ ||
            (primary_highlighted_row_ < 0 && !highlighted_rows_.isEmpty() && row == highlighted_rows_.first());
        return primary ? theme_.highlightedText : theme_.secondaryHighlightedText;
    }

    QStringList headers_;
    QVector<QStringList> rows_;
    SessionTableTheme theme_;
    QVector<int> highlighted_rows_;
    QHash<int, QString> delta_text_by_row_;
    int primary_highlighted_row_;
};

class SessionWavePlotWidgetImpl final : public SessionWavePlotWidget
{
public:
    explicit SessionWavePlotWidgetImpl(QWidget *parent = nullptr)
        : SessionWavePlotWidget(parent)
        , first_sample_index_(0)
    {
        setFixedHeight(kSessionViewerPlotHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setSamples(const QVector<float>& samples, int firstSampleIndex = 0)
    {
        samples_ = samples;
        first_sample_index_ = std::max(0, firstSampleIndex);
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const SessionPlotTheme theme = sessionPlotThemeFor(this);
        const bool dark = theme.background.lightness() < 128;
        painter.fillRect(rect(), theme.background);

        const QFontMetrics fm = painter.fontMetrics();
        const int leftMargin = dataPlotLeftMargin(fm);
        const QRectF plotRect = rect().adjusted(
            leftMargin,
            kSessionViewerPlotTopMargin,
            -kSessionViewerPlotRightMargin,
            -kSessionViewerWaveBottomMargin);
        painter.setPen(QPen(theme.grid, 1));
        for (int i = 0; i <= 5; ++i)
        {
            const qreal x = plotRect.left() + plotRect.width() * i / 5.0;
            painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
        }
        for (int i = 0; i <= 8; ++i)
        {
            const qreal y = plotRect.top() + plotRect.height() * i / 8.0;
            painter.drawLine(QPointF(plotRect.left(), y), QPointF(plotRect.right(), y));
        }

        painter.setPen(QPen(theme.border, 1));
        painter.drawRect(plotRect);

        if (samples_.isEmpty())
        {
            painter.setPen(dark ? theme.mutedText : appThemeColor(AppThemeColor::TextDisabled, false));
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

        painter.setPen(QPen(appThemeColor(AppThemeColor::PlotSeriesWaveBlue, dark), 1.4));
        painter.drawPolyline(polyline);

        painter.setPen(dark ? theme.text : appThemeColor(AppThemeColor::PlotAxisStrong, false));
        painter.drawText(QRectF(4, plotRect.top() - 2, leftMargin - 8, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(maxValue, 'f', 4));
        painter.drawText(QRectF(4, plotRect.center().y() - 8, leftMargin - 8, 16), Qt::AlignRight | Qt::AlignVCenter,
                         QString::number((maxValue + minValue) * 0.5, 'f', 4));
        painter.drawText(QRectF(4, plotRect.bottom() - 8, leftMargin - 8, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(minValue, 'f', 4));
        drawXAxisTicks(painter, plotRect, first_sample_index_, first_sample_index_ + samples_.size(), 5,
                       dark ? theme.text : appThemeColor(AppThemeColor::PlotAxisStrong, false));
    }

private:
    QVector<float> samples_;
    int first_sample_index_;
};

class SessionPeakPlotWidgetImpl final : public SessionPeakPlotWidget
{
public:
    explicit SessionPeakPlotWidgetImpl(QWidget *parent = nullptr)
        : SessionPeakPlotWidget(parent)
        , current_frame_index_(-1)
        , plot_mode_(PlotMode::Scatter)
        , view_start_index_(0)
        , view_count_(0)
        , is_english_(false)
        , plot_cache_valid_(false)
    {
        setFixedHeight(kSessionViewerPlotHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setEnglish(bool english)
    {
        is_english_ = english;
        invalidatePlotCache();
        update();
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
        invalidatePlotCache();
        update();
    }

    void setCurrentFrame(int frameIndex)
    {
        if (current_frame_index_ == frameIndex)
        {
            return;
        }

        current_frame_index_ = frameIndex;
        bool viewChanged = false;
        if (current_frame_index_ >= 0 &&
            current_frame_index_ < static_cast<int>(peak_values_.size()) &&
            (current_frame_index_ < visibleStartIndex() ||
             current_frame_index_ >= (visibleStartIndex() + visibleCount())))
        {
            const int count = visibleCount();
            view_start_index_ = std::clamp(current_frame_index_ - count / 2, 0, std::max(0, static_cast<int>(peak_values_.size()) - count));
            normalizeView(false);
            notifyViewChanged();
            viewChanged = true;
        }
        if (viewChanged)
        {
            invalidatePlotCache();
        }
        update();
    }

    void setPlotMode(PlotMode mode)
    {
        plot_mode_ = mode;
        invalidatePlotCache();
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

        const int normalizedStart = visibleStartIndex();
        const int normalizedCount = visibleCount();
        if (cached_plot_.start_index == normalizedStart &&
            cached_plot_.count == normalizedCount &&
            plot_cache_valid_)
        {
            return;
        }

        notifyViewChanged();
        invalidatePlotCache();
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
        if (ensurePlotCache())
        {
            painter.drawPixmap(0, 0, plot_cache_);
            drawCurrentFrameMarker(painter, cached_plot_);
        }
    }

private:
    struct CachedPlot
    {
        QRectF plot_rect;
        QVector<QPointF> points;
        int start_index = 0;
        int count = 0;
        float min_value = 0.0f;
        float max_value = 0.0f;
        bool has_values = false;
    };

    void invalidatePlotCache()
    {
        plot_cache_valid_ = false;
    }

    bool ensurePlotCache()
    {
        const QColor background = sessionPlotThemeFor(this).background;
        if (plot_cache_valid_ && plot_cache_.size() == size() && cache_background_ == background)
        {
            return true;
        }
        if (size().isEmpty())
        {
            plot_cache_ = QPixmap();
            cached_plot_ = CachedPlot{};
            plot_cache_valid_ = false;
            return false;
        }

        plot_cache_ = QPixmap(size());
        plot_cache_.fill(background);
        cache_background_ = background;
        QPainter cachePainter(&plot_cache_);
        cachePainter.setRenderHint(QPainter::Antialiasing, true);
        renderPlotBase(cachePainter, cached_plot_);
        plot_cache_valid_ = true;
        return true;
    }

    void renderPlotBase(QPainter& painter, CachedPlot& cache)
    {
        const SessionPlotTheme theme = sessionPlotThemeFor(this);
        painter.fillRect(rect(), theme.background);
        cache = CachedPlot{};

        const QFontMetrics fm = painter.fontMetrics();
        const int leftMargin = dataPlotLeftMargin(fm);
        const QRectF plotRect = rect().adjusted(
            leftMargin,
            kSessionViewerPlotTopMargin,
            -kSessionViewerPlotRightMargin,
            -kSessionViewerPlotBottomMargin);
        cache.plot_rect = plotRect;

        painter.setPen(QPen(theme.grid, 1));
        for (int i = 0; i <= 5; ++i)
        {
            const qreal x = plotRect.left() + plotRect.width() * i / 5.0;
            painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
        }
        for (int i = 0; i <= 6; ++i)
        {
            const qreal y = plotRect.top() + plotRect.height() * i / 6.0;
            painter.drawLine(QPointF(plotRect.left(), y), QPointF(plotRect.right(), y));
        }

        painter.setPen(QPen(theme.border, 1));
        painter.drawRect(plotRect);

        if (peak_values_.isEmpty())
        {
            painter.setPen(theme.mutedText);
            painter.drawText(plotRect, Qt::AlignCenter, is_english_ ? QStringLiteral("No peak overview") : QStringLiteral("没有峰值概览"));
            return;
        }

        const int startIndex = visibleStartIndex();
        const int count = visibleCount();
        cache.start_index = startIndex;
        cache.count = count;
        float minValue = std::numeric_limits<float>::max();
        float maxValue = std::numeric_limits<float>::lowest();
        bool hasFiniteValues = false;
        for (int i = 0; i < count; ++i)
        {
            const float value = peak_values_.at(startIndex + i);
            if (!std::isfinite(value))
            {
                continue;
            }
            hasFiniteValues = true;
            minValue = std::min(minValue, value);
            maxValue = std::max(maxValue, value);
        }
        if (!hasFiniteValues)
        {
            painter.setPen(theme.mutedText);
            painter.drawText(plotRect, Qt::AlignCenter,
                is_english_ ? QStringLiteral("No valid peak values") : QStringLiteral("无有效峰值"));
            return;
        }

        if (std::fabs(maxValue - minValue) < 1e-6f)
        {
            const float pad = std::max(1e-6f, std::fabs(maxValue) * 0.05f + 1e-6f);
            minValue -= pad;
            maxValue += pad;
        }
        cache.min_value = minValue;
        cache.max_value = maxValue;
        cache.has_values = true;

        const int drawCount = trendRenderPointCount(count, plotRect);
        cache.points.reserve(drawCount);
        for (int drawIndex = 0; drawIndex < drawCount; ++drawIndex)
        {
            const int relativeIndex = trendRelativeIndexForDrawPoint(drawIndex, drawCount, count);
            const double ratio = count == 1 ? 0.5 : static_cast<double>(relativeIndex) / static_cast<double>(count - 1);
            const float value = peak_values_.at(startIndex + relativeIndex);
            if (!std::isfinite(value))
            {
                cache.points.push_back(QPointF(std::numeric_limits<qreal>::quiet_NaN(), std::numeric_limits<qreal>::quiet_NaN()));
                continue;
            }
            const double normalized = (value - minValue) / std::max(1e-6f, maxValue - minValue);
            cache.points.push_back(QPointF(plotRect.left() + ratio * plotRect.width(),
                plotRect.bottom() - normalized * plotRect.height()));
        }

        const QColor seriesColor = appThemeColor(AppThemeColor::PlotSeriesSky, false);
        if (plot_mode_ == PlotMode::Polyline && cache.points.size() >= 2)
        {
            painter.setPen(QPen(seriesColor, 1.5));
            QPolygonF segment;
            for (const QPointF& point : cache.points)
            {
                if (!std::isfinite(point.x()) || !std::isfinite(point.y()))
                {
                    if (segment.size() >= 2)
                    {
                        painter.drawPolyline(segment);
                    }
                    segment.clear();
                    continue;
                }
                segment.push_back(point);
            }
            if (segment.size() >= 2)
            {
                painter.drawPolyline(segment);
            }
        }
        else
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(seriesColor);
            for (const QPointF& point : cache.points)
            {
                if (!std::isfinite(point.x()) || !std::isfinite(point.y()))
                {
                    continue;
                }
                painter.drawEllipse(point, 2.5, 2.5);
            }
        }

        painter.setPen(theme.text);
        painter.drawText(QRectF(4, plotRect.top() - 2, leftMargin - 8, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(maxValue, 'f', 4));
        painter.drawText(QRectF(4, plotRect.center().y() - 8, leftMargin - 8, 16), Qt::AlignRight | Qt::AlignVCenter,
                         QString::number((maxValue + minValue) * 0.5, 'f', 4));
        painter.drawText(QRectF(4, plotRect.bottom() - 8, leftMargin - 8, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(minValue, 'f', 4));
        drawXAxisTicks(painter, plotRect, startIndex, startIndex + count, 5, theme.text);
    }

    void drawCurrentFrameMarker(QPainter& painter, const CachedPlot& cache)
    {
        if (!cache.has_values ||
            current_frame_index_ < cache.start_index ||
            current_frame_index_ >= (cache.start_index + cache.count))
        {
            return;
        }

        const int relativeIndex = current_frame_index_ - cache.start_index;
        if (relativeIndex < 0 || relativeIndex >= cache.count)
        {
            return;
        }

        const float value = peak_values_.at(current_frame_index_);
        if (!std::isfinite(value))
        {
            return;
        }

        const qreal x = cache.plot_rect.left() + (cache.count == 1
            ? 0.0
            : (static_cast<qreal>(relativeIndex) / static_cast<qreal>(cache.count - 1)) * cache.plot_rect.width());
        const qreal normalized = (value - cache.min_value) / std::max(1e-6f, cache.max_value - cache.min_value);
        const QPointF currentPoint(x, cache.plot_rect.bottom() - normalized * cache.plot_rect.height());
        drawCurrentPointGuides(
            painter,
            cache.plot_rect,
            currentPoint,
            QString::number(current_frame_index_ + 1),
            formatGuideValue(value, 4));
        painter.setPen(Qt::NoPen);
        painter.setBrush(appThemeColor(AppThemeColor::PlotCurrentGuideLine, false));
        painter.drawEllipse(currentPoint, 4.0, 4.0);
    }

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
    bool is_english_;
    bool plot_cache_valid_;
    QPixmap plot_cache_;
    QColor cache_background_;
    CachedPlot cached_plot_;
    std::function<void(int, int, int)> on_view_changed_;
};

class SingleSeriesTrendPlotWidgetImpl final : public SingleSeriesTrendPlotWidget
{
public:
    explicit SingleSeriesTrendPlotWidgetImpl(const QColor& color, const QString& emptyText, const QString& unit = QString(), QWidget *parent = nullptr)
        : SingleSeriesTrendPlotWidget(parent)
        , line_color_(color)
        , empty_text_(emptyText)
        , unit_(unit)
        , current_index_(-1)
        , plot_mode_(PlotMode::Polyline)
        , view_start_index_(0)
        , view_count_(0)
        , plot_cache_valid_(false)
    {
        setFont(numericFontFrom(font()));
        setFixedHeight(kSessionViewerPlotHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setValues(const QVector<double>& values)
    {
        values_ = values;
        if (current_index_ >= values_.size())
        {
            current_index_ = -1;
        }
        normalizeView();
        invalidatePlotCache();
        update();
    }

    void setCurrentIndex(int index)
    {
        if (current_index_ == index)
        {
            return;
        }

        current_index_ = index;
        update();
    }

    void setPlotMode(PlotMode mode)
    {
        plot_mode_ = mode;
        invalidatePlotCache();
        update();
    }

    void setViewRange(int startIndex, int count)
    {
        if (values_.isEmpty())
        {
            view_start_index_ = 0;
            view_count_ = 0;
            invalidatePlotCache();
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
        if (cached_plot_.start_index == visibleStartIndex() &&
            cached_plot_.count == visibleCount() &&
            plot_cache_valid_)
        {
            return;
        }
        invalidatePlotCache();
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (ensurePlotCache())
        {
            painter.drawPixmap(0, 0, plot_cache_);
            drawCurrentIndexMarker(painter, cached_plot_);
        }
    }

private:
    struct CachedPlot
    {
        QRectF plot_rect;
        int start_index = 0;
        int count = 0;
        double min_value = 0.0;
        double max_value = 0.0;
        bool has_values = false;
    };

    void invalidatePlotCache()
    {
        plot_cache_valid_ = false;
    }

    bool ensurePlotCache()
    {
        const QColor background = sessionPlotThemeFor(this).background;
        if (plot_cache_valid_ && plot_cache_.size() == size() && cache_background_ == background)
        {
            return true;
        }
        if (size().isEmpty())
        {
            plot_cache_ = QPixmap();
            cached_plot_ = CachedPlot{};
            plot_cache_valid_ = false;
            return false;
        }

        plot_cache_ = QPixmap(size());
        plot_cache_.fill(background);
        cache_background_ = background;
        QPainter cachePainter(&plot_cache_);
        cachePainter.setRenderHint(QPainter::Antialiasing, true);
        renderPlotBase(cachePainter, cached_plot_);
        plot_cache_valid_ = true;
        return true;
    }

    void renderPlotBase(QPainter& painter, CachedPlot& cache)
    {
        const SessionPlotTheme theme = sessionPlotThemeFor(this);
        painter.fillRect(rect(), theme.background);
        cache = CachedPlot{};

        if (values_.isEmpty())
        {
            const QFontMetrics fm = painter.fontMetrics();
            const int leftMargin = dataPlotLeftMargin(fm);
            const QRectF emptyPlotRect = rect().adjusted(
                leftMargin,
                kSessionViewerPlotTopMargin,
                -kSessionViewerPlotRightMargin,
                -kSessionViewerPlotBottomMargin);
            painter.setPen(QPen(theme.border, 1));
            painter.drawRect(emptyPlotRect);
            painter.setPen(theme.mutedText);
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
        const QString maxLabel = hasFiniteValues ? formatGuideValue(maxValue, 3) : QStringLiteral("---");
        const QString midLabel = hasFiniteValues ? formatGuideValue((maxValue + minValue) * 0.5, 3) : QStringLiteral("---");
        const QString minLabel = hasFiniteValues ? formatGuideValue(minValue, 3) : QStringLiteral("---");
        const QFontMetrics fm = painter.fontMetrics();
        const int leftMargin = dataPlotLeftMargin(fm, maxLabel, midLabel, minLabel);
        const QRectF plotRect = rect().adjusted(
            leftMargin,
            kSessionViewerPlotTopMargin,
            -kSessionViewerPlotRightMargin,
            -kSessionViewerPlotBottomMargin);
        cache.plot_rect = plotRect;
        cache.start_index = startIndex;
        cache.count = count;

        painter.setPen(QPen(theme.grid, 1));
        for (int i = 0; i <= 5; ++i)
        {
            const qreal x = plotRect.left() + plotRect.width() * i / 5.0;
            painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
        }
        for (int i = 0; i <= 6; ++i)
        {
            const qreal y = plotRect.top() + plotRect.height() * i / 6.0;
            painter.drawLine(QPointF(plotRect.left(), y), QPointF(plotRect.right(), y));
        }

        painter.setPen(QPen(theme.border, 1));
        painter.drawRect(plotRect);

        if (!hasFiniteValues)
        {
            painter.setPen(theme.mutedText);
            painter.drawText(plotRect, Qt::AlignCenter, empty_text_);
            return;
        }

        cache.min_value = minValue;
        cache.max_value = maxValue;
        cache.has_values = true;
        drawSeries(painter, plotRect, startIndex, count, minValue, maxValue);

        painter.setPen(theme.text);
        painter.drawText(QRectF(4, plotRect.top() - 2, leftMargin - 8, fm.height()), Qt::AlignRight | Qt::AlignVCenter, maxLabel);
        painter.drawText(QRectF(4, plotRect.center().y() - fm.height() * 0.5, leftMargin - 8, fm.height()), Qt::AlignRight | Qt::AlignVCenter, midLabel);
        painter.drawText(QRectF(4, plotRect.bottom() - fm.height() + 2, leftMargin - 8, fm.height()), Qt::AlignRight | Qt::AlignVCenter, minLabel);
        drawXAxisTicks(painter, plotRect, startIndex, startIndex + count, 5, theme.text);
    }

    void drawCurrentIndexMarker(QPainter& painter, const CachedPlot& cache)
    {
        if (!cache.has_values ||
            current_index_ < cache.start_index ||
            current_index_ >= (cache.start_index + cache.count) ||
            !std::isfinite(values_.at(current_index_)))
        {
            return;
        }

        const int relativeIndex = current_index_ - cache.start_index;
        const qreal x = cache.plot_rect.left() + (cache.count == 1 ? 0.0 : (static_cast<qreal>(relativeIndex) / static_cast<qreal>(cache.count - 1)) * cache.plot_rect.width());
        const qreal normalized = (values_.at(current_index_) - cache.min_value) / std::max(1e-9, cache.max_value - cache.min_value);
        const qreal y = cache.plot_rect.bottom() - normalized * cache.plot_rect.height();
        drawCurrentPointGuides(
            painter,
            cache.plot_rect,
            QPointF(x, y),
            QString::number(current_index_ + 1),
            formatGuideValue(values_.at(current_index_), 3));
        painter.setPen(Qt::NoPen);
        painter.setBrush(line_color_);
        painter.drawEllipse(QPointF(x, y), 3.0, 3.0);
    }

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
        if (plot_mode_ == PlotMode::Scatter)
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(line_color_);
        }
        const int drawCount = trendRenderPointCount(count, plotRect);
        for (int drawIndex = 0; drawIndex < drawCount; ++drawIndex)
        {
            const int relativeIndex = trendRelativeIndexForDrawPoint(drawIndex, drawCount, count);
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
            if (plot_mode_ == PlotMode::Scatter)
            {
                painter.drawEllipse(QPointF(x, y), 2.2, 2.2);
            }
            else
            {
                segment.append(QPointF(x, y));
            }
        }
        if (plot_mode_ == PlotMode::Polyline && segment.size() >= 2)
        {
            painter.drawPolyline(segment);
        }
        else if (plot_mode_ == PlotMode::Polyline && segment.size() == 1)
        {
            painter.drawPoint(segment.first());
        }
    }

    QColor line_color_;
    QString empty_text_;
    QString unit_;
    QVector<double> values_;
    int current_index_;
    PlotMode plot_mode_;
    int view_start_index_;
    int view_count_;
    bool plot_cache_valid_;
    QPixmap plot_cache_;
    QColor cache_background_;
    CachedPlot cached_plot_;
};

SessionCsvTableModel *createSessionCsvTableModel(QObject *parent)
{
    return new SessionCsvTableModelImpl(parent);
}

SessionWavePlotWidget *createSessionWavePlotWidget(QWidget *parent)
{
    return new SessionWavePlotWidgetImpl(parent);
}

SessionPeakPlotWidget *createSessionPeakPlotWidget(QWidget *parent)
{
    return new SessionPeakPlotWidgetImpl(parent);
}

SingleSeriesTrendPlotWidget *createSingleSeriesTrendPlotWidget(
    const QColor& color,
    const QString& emptyText,
    const QString& unit,
    QWidget *parent)
{
    return new SingleSeriesTrendPlotWidgetImpl(color, emptyText, unit, parent);
}

} // namespace VaporView::Ground::SessionUi
