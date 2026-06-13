#include "SessionViewerWindow.h"
#include "CustomTitleBar.h"
#include "RangeSelectionAxisWidget.h"
#include "RawDataParserWindow.h"
#include "TrajectoryViewerDialog.h"
#include "WindowSizing.h"

#include <QByteArray>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFontDatabase>
#include <QFontMetrics>
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
#include <QPalette>
#include <QPixmap>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
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
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>

namespace
{
constexpr quint64 kWaveformTimestampBytes = sizeof(quint64);
constexpr quint64 kFloatBytes = sizeof(float);
constexpr int kSessionViewerPlotHeight = 120;
constexpr int kSessionViewerPlotLeftMargin = 64;
constexpr int kSessionViewerTrendPlotLeftMargin = 112;
constexpr int kSessionViewerPlotRightMargin = 10;
constexpr int kSessionViewerPlotTopMargin = 12;
constexpr int kSessionViewerPlotBottomMargin = 28;
constexpr int kSessionViewerWaveBottomMargin = 30;
constexpr int kDefaultPeakSearchStartIndex = 10000;
constexpr int kDefaultPeakSearchEndIndex = 50000;
constexpr int kSessionViewerPreferredWidth = 1320;
constexpr int kSessionViewerPreferredHeight = 860;
constexpr int kSessionViewerMinimumWidth = 800;
constexpr int kSessionViewerMinimumHeight = 520;
constexpr int kMaxTrendPointsPerPixel = 2;
constexpr char kUnifiedRawMagic[8] = {'V', 'V', 'R', 'A', 'W', 'D', 'A', 'T'};
constexpr quint32 kUnifiedRawRecordMarker = 0x44525756u;
constexpr quint16 kRawSourceTcpWave = 5u;
constexpr quint32 kRawTcpWaveCombinedPayloadFlag = 0x00000001u;
const QColor kHighlightedCsvRowColor("#c7e3ff");
const QColor kSecondaryHighlightedCsvRowColor("#e8f3ff");
const QColor kDefaultCsvRowColor("#ffffff");
const QColor kCurrentGuideLineColor("#ffb347");
const QColor kCurrentGuideLabelFillColor("#8b4a00");
const QColor kCurrentGuideLabelBorderColor("#5f3000");
const QColor kCurrentGuideLabelTextColor("#fff7ea");

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
    if (!std::isfinite(value))
    {
        return fixedTextField(QStringLiteral("---"), width);
    }
    return fixedTextField(QString::number(value, 'f', decimals), width);
}

QString fixedSignedTextField(const QString& text, int width)
{
    QString displayText = text;
    if (!displayText.isEmpty() &&
        displayText.at(0) != QLatin1Char('-') &&
        displayText.at(0) != QLatin1Char('+'))
    {
        displayText.prepend(QLatin1Char(' '));
    }
    const int targetWidth = std::max(width, static_cast<int>(displayText.size()));
    return displayText.leftJustified(targetWidth, QLatin1Char(' '));
}

QString fixedSignedDecimalField(double value, int decimals, int width)
{
    if (!std::isfinite(value))
    {
        return fixedSignedTextField(QStringLiteral("---"), width);
    }
    return fixedSignedTextField(QString::number(value, 'f', decimals), width);
}

struct SessionPlotTheme
{
    QColor background;
    QColor grid;
    QColor border;
    QColor text;
    QColor mutedText;
};

struct SessionTableTheme
{
    QColor background;
    QColor text;
    QColor grid;
    QColor headerBackground;
    QColor headerText;
    QColor selectedBackground;
    QColor selectedText;
    QColor highlightedBackground;
    QColor secondaryHighlightedBackground;
};

SessionPlotTheme sessionPlotThemeFor(const QWidget *widget)
{
    const QPalette palette = widget->palette();
    QColor background = palette.color(QPalette::Base);
    if (!background.isValid() || background.alpha() == 0)
    {
        background = palette.color(QPalette::Window);
    }
    const bool dark = background.lightness() < 128;
    return {
        background,
        dark ? QColor("#202020") : QColor("#e3e8ef"),
        dark ? QColor("#202020") : QColor("#cfd7e3"),
        dark ? QColor("#a7b4c2") : QColor("#5e6b78"),
        dark ? QColor("#8fa1b3") : QColor("#7a8899")
    };
}

SessionTableTheme sessionTableThemeFor(const QWidget *widget)
{
    const QPalette palette = widget->palette();
    QColor background = palette.color(QPalette::Base);
    if (!background.isValid() || background.alpha() == 0)
    {
        background = palette.color(QPalette::Window);
    }
    const bool dark = background.lightness() < 128;
    if (dark)
    {
        return {
            QColor("#121212"),
            QColor("#e5e7eb"),
            QColor("#2a2a2a"),
            QColor("#181818"),
            QColor("#d8dee9"),
            QColor("#245b8f"),
            QColor("#ffffff"),
            QColor("#1d4f78"),
            QColor("#17384f")
        };
    }

    return {
        kDefaultCsvRowColor,
        QColor("#1f2933"),
        QColor("#e5e7eb"),
        QColor("#ffffff"),
        QColor("#1f2933"),
        kHighlightedCsvRowColor,
        QColor("#1f2933"),
        kHighlightedCsvRowColor,
        kSecondaryHighlightedCsvRowColor
    };
}

#pragma pack(push, 1)
struct UnifiedRawFileHeader
{
    char magic[8];
    quint32 version;
    quint32 header_size;
    quint16 source_id;
    quint16 reserved;
};

struct UnifiedRawRecordHeader
{
    quint32 marker;
    quint32 header_size;
    quint64 host_timestamp_us;
    quint32 payload_size;
    quint16 source_id;
    quint16 record_type;
    quint32 flags;
    quint64 sequence;
};
#pragma pack(pop)

QString csvValueAt(const QStringList& fields, int index)
{
    if (index < 0 || index >= fields.size())
    {
        return QString();
    }
    return fields.at(index);
}

int findClosestTimestampIndex(const QVector<quint64>& timestampsUs, quint64 timestampUs)
{
    if (timestampsUs.isEmpty())
    {
        return -1;
    }

    const auto lower = std::lower_bound(timestampsUs.cbegin(), timestampsUs.cend(), timestampUs);
    if (lower == timestampsUs.cbegin())
    {
        return 0;
    }
    if (lower == timestampsUs.cend())
    {
        return timestampsUs.size() - 1;
    }

    const int upperIndex = static_cast<int>(std::distance(timestampsUs.cbegin(), lower));
    const int lowerIndex = upperIndex - 1;
    const quint64 lowerDelta = timestampUs - timestampsUs.at(lowerIndex);
    const quint64 upperDelta = timestampsUs.at(upperIndex) - timestampUs;
    return lowerDelta <= upperDelta ? lowerIndex : upperIndex;
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

double calculateMeasuredRateHz(const QVector<quint64>& timestampsUs)
{
    if (timestampsUs.size() < 2)
    {
        return 0.0;
    }

    quint64 firstUs = 0;
    quint64 lastUs = 0;
    bool foundFirst = false;
    int validCount = 0;
    for (quint64 timestampUs : timestampsUs)
    {
        if (timestampUs == 0)
        {
            continue;
        }

        if (!foundFirst)
        {
            firstUs = timestampUs;
            foundFirst = true;
        }
        lastUs = timestampUs;
        ++validCount;
    }

    if (!foundFirst || validCount < 2 || lastUs <= firstUs)
    {
        return 0.0;
    }

    const double durationSeconds = static_cast<double>(lastUs - firstUs) / 1000000.0;
    if (durationSeconds <= 0.0)
    {
        return 0.0;
    }

    return static_cast<double>(validCount - 1) / durationSeconds;
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

QString formatOptionalSeriesValueFixed(double value, int decimals, int width, const QString& unit = QString())
{
    const QString number = fixedDecimalField(value, decimals, width);
    return unit.isEmpty() ? number : QStringLiteral("%1 %2").arg(number, unit);
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

float waveformPeakValue(const float* samples, int sampleCount, int searchStartIndex, int searchEndIndex)
{
    if (!samples || sampleCount <= 0)
    {
        return std::numeric_limits<float>::quiet_NaN();
    }

    const int startIndex = std::clamp(searchStartIndex, 0, sampleCount);
    const int endIndex = std::clamp(searchEndIndex, 0, sampleCount);
    if (startIndex >= endIndex)
    {
        return std::numeric_limits<float>::quiet_NaN();
    }

    bool hasPeak = false;
    float peakValue = std::numeric_limits<float>::lowest();
    for (int index = startIndex; index < endIndex; ++index)
    {
        const float value = samples[index];
        if (!std::isfinite(value))
        {
            continue;
        }
        hasPeak = true;
        peakValue = std::max(peakValue, value);
    }

    return hasPeak ? peakValue : std::numeric_limits<float>::quiet_NaN();
}

float waveformPeakValue(const QVector<float>& samples, int searchStartIndex, int searchEndIndex)
{
    return waveformPeakValue(samples.constData(), samples.size(), searchStartIndex, searchEndIndex);
}

double haversineDistanceMeters(double lat1Deg, double lon1Deg, double lat2Deg, double lon2Deg)
{
    constexpr double kEarthRadiusMeters = 6371000.0;
    const double lat1 = qDegreesToRadians(lat1Deg);
    const double lon1 = qDegreesToRadians(lon1Deg);
    const double lat2 = qDegreesToRadians(lat2Deg);
    const double lon2 = qDegreesToRadians(lon2Deg);
    const double dLat = lat2 - lat1;
    const double dLon = lon2 - lon1;
    const double a = std::sin(dLat * 0.5) * std::sin(dLat * 0.5) +
        std::cos(lat1) * std::cos(lat2) * std::sin(dLon * 0.5) * std::sin(dLon * 0.5);
    const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(std::max(0.0, 1.0 - a)));
    return kEarthRadiusMeters * c;
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
    painter.setPen(QPen(kCurrentGuideLabelBorderColor, 1));
    painter.setBrush(kCurrentGuideLabelFillColor);
    painter.drawRoundedRect(rect, 4.0, 4.0);
    painter.setPen(kCurrentGuideLabelTextColor);
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
    painter.setPen(QPen(kCurrentGuideLineColor, 1, Qt::DashLine));
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
}

class SessionWavePlotWidget : public QWidget
{
public:
    explicit SessionWavePlotWidget(QWidget *parent = nullptr)
        : QWidget(parent)
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

        const QRectF plotRect = rect().adjusted(
            kSessionViewerPlotLeftMargin,
            kSessionViewerPlotTopMargin,
            -kSessionViewerPlotRightMargin,
            -kSessionViewerWaveBottomMargin);
        painter.setPen(QPen(dark ? theme.grid : QColor("#f0d000"), 1));
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

        painter.setPen(QPen(dark ? theme.border : QColor("#c9b53a"), 1));
        painter.drawRect(plotRect);

        if (samples_.isEmpty())
        {
            painter.setPen(dark ? theme.mutedText : QColor("#64748b"));
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

        painter.setPen(QPen(dark ? QColor("#56d364") : QColor("#1b6416"), 1.4));
        painter.drawPolyline(polyline);

        painter.setPen(dark ? theme.text : QColor("#334155"));
        painter.drawText(QRectF(4, plotRect.top() - 2, kSessionViewerPlotLeftMargin - 8, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(maxValue, 'f', 4));
        painter.drawText(QRectF(4, plotRect.center().y() - 8, kSessionViewerPlotLeftMargin - 8, 16), Qt::AlignRight | Qt::AlignVCenter,
                         QString::number((maxValue + minValue) * 0.5, 'f', 4));
        painter.drawText(QRectF(4, plotRect.bottom() - 8, kSessionViewerPlotLeftMargin - 8, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(minValue, 'f', 4));
        drawXAxisTicks(painter, plotRect, first_sample_index_, first_sample_index_ + samples_.size(), 5, dark ? theme.text : QColor("#334155"));
    }

private:
    QVector<float> samples_;
    int first_sample_index_;
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

        const QRectF plotRect = rect().adjusted(
            kSessionViewerPlotLeftMargin,
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

        const QColor seriesColor("#66d0ff");
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
        painter.drawText(QRectF(4, plotRect.top() - 2, kSessionViewerPlotLeftMargin - 8, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(maxValue, 'f', 4));
        painter.drawText(QRectF(4, plotRect.center().y() - 8, kSessionViewerPlotLeftMargin - 8, 16), Qt::AlignRight | Qt::AlignVCenter,
                         QString::number((maxValue + minValue) * 0.5, 'f', 4));
        painter.drawText(QRectF(4, plotRect.bottom() - 8, kSessionViewerPlotLeftMargin - 8, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(minValue, 'f', 4));
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
        painter.setBrush(kCurrentGuideLineColor);
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

class SingleSeriesTrendPlotWidget : public QWidget
{
public:
    enum class PlotMode
    {
        Scatter,
        Polyline
    };

    explicit SingleSeriesTrendPlotWidget(const QColor& color, const QString& emptyText, const QString& unit = QString(), QWidget *parent = nullptr)
        : QWidget(parent)
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
            const QRectF emptyPlotRect = rect().adjusted(
                kSessionViewerPlotLeftMargin,
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
        const QString maxLabel = hasFiniteValues ? formatGuideValue(maxValue, 3, unit_) : QStringLiteral("---");
        const QString midLabel = hasFiniteValues ? formatGuideValue((maxValue + minValue) * 0.5, 3, unit_) : QStringLiteral("---");
        const QString minLabel = hasFiniteValues ? formatGuideValue(minValue, 3, unit_) : QStringLiteral("---");
        const QFontMetrics fm = painter.fontMetrics();
        const int leftMargin = kSessionViewerTrendPlotLeftMargin;
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
            formatGuideValue(values_.at(current_index_), 3, unit_));
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

SessionViewerWindow::SessionViewerWindow(QWidget *parent)
    : QMainWindow(parent)
    , central_widget_(nullptr)
    , session_path_edit_(nullptr)
    , choose_session_btn_(nullptr)
    , reload_btn_(nullptr)
    , trajectory_view_btn_(nullptr)
    , raw_data_parser_btn_(nullptr)
    , clear_view_btn_(nullptr)
    , status_label_(nullptr)
    , loading_dialog_(nullptr)
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
    , waveform_frame_filter_btn_(nullptr)
    , waveform_peak_filter_btn_(nullptr)
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
    , waveform_index_filename_()
    , raw_tcp_wave_filename_()
    , default_data_directory_()
    , session_name_()
    , start_time_utc_()
    , end_time_utc_()
    , csv_headers_()
    , csv_timestamps_us_()
    , temperature_values_()
    , humidity_values_()
    , pressure_values_()
    , rtk_track_points_()
    , waveform_timestamps_us_()
    , waveform_segments_()
    , raw_tcp_wave_frames_()
    , indexed_waveform_frames_()
    , current_waveform_frame_samples_()
    , waveform_peak_raw_values_()
    , waveform_peak_values_()
    , peak_filter_settings_()
    , peak_search_start_index_(kDefaultPeakSearchStartIndex)
    , peak_search_end_index_(kDefaultPeakSearchEndIndex)
    , is_english_(false)
    , updating_frame_controls_(false)
    , waveform_peak_scatter_mode_(true)
    , waveform_show_filtered_frame_(false)
    , session_loading_(false)
    , highlighted_csv_rows_()
    , trajectory_viewer_dialog_(nullptr)
    , raw_data_parser_window_(nullptr)
    , points_per_frame_(50000)
    , sensor_export_rate_hz_(10)
    , waveform_export_rate_hz_(10)
    , waveform_export_mode_(QStringLiteral("fixed_rate"))
    , total_sensor_rows_(0)
    , total_waveform_frames_(0)
{
    setWindowFlag(Qt::Window, true);
    setupUi();
    VaporView::installCustomTitleBar(this);
    resize(VaporView::defaultWindowSizeWithinScreenFraction(
        this,
        QSize(kSessionViewerPreferredWidth, kSessionViewerPreferredHeight),
        0.5,
        QSize(kSessionViewerMinimumWidth, kSessionViewerMinimumHeight)));
    setEnglish(false);
    VaporView::centerWindowOnScreen(this, parent);

    QSettings settings("VaporView", "SessionViewer");
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
    peak_filter_settings_.min_value = settings.value("peak_filter/min_value", 0.0).toDouble();
    peak_filter_settings_.max_value = settings.value("peak_filter/max_value", 0.0).toDouble();
    peak_search_start_index_ = std::max(0, settings.value("peak_search/start_index", kDefaultPeakSearchStartIndex).toInt());
    peak_search_end_index_ = settings.value("peak_search/end_index", kDefaultPeakSearchEndIndex).toInt();
    if (peak_search_end_index_ <= peak_search_start_index_)
    {
        peak_search_end_index_ = peak_search_start_index_ + 1;
    }
    default_data_directory_ = QDir::fromNativeSeparators(settings.value("default_data_directory").toString());
    updatePeakFilterButtonText();
    const QString lastSession = settings.value("last_session_directory").toString();
    if (!lastSession.isEmpty())
    {
        restoreLastSessionPath(lastSession);
    }
}

SessionViewerWindow::~SessionViewerWindow()
{
    if (trajectory_viewer_dialog_)
    {
        delete trajectory_viewer_dialog_;
        trajectory_viewer_dialog_ = nullptr;
    }
    if (raw_data_parser_window_)
    {
        delete raw_data_parser_window_;
        raw_data_parser_window_ = nullptr;
    }
}

void SessionViewerWindow::setupUi()
{
    setObjectName("sessionViewerWindow");
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setObjectName("sessionViewerScrollArea");
    scrollArea->setAttribute(Qt::WA_StyledBackground, true);
    scrollArea->setAutoFillBackground(true);
    scrollArea->viewport()->setObjectName("sessionViewerViewport");
    scrollArea->viewport()->setAttribute(Qt::WA_StyledBackground, true);
    scrollArea->viewport()->setAutoFillBackground(true);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setCentralWidget(scrollArea);

    central_widget_ = new QWidget(this);
    central_widget_->setObjectName("sessionViewerCentralWidget");
    central_widget_->setAttribute(Qt::WA_StyledBackground, true);
    central_widget_->setAutoFillBackground(true);
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

    trajectory_view_btn_ = new QPushButton(this);
    trajectory_view_btn_->setEnabled(false);
    connect(trajectory_view_btn_, &QPushButton::clicked, this, &SessionViewerWindow::onViewTrajectoryClicked);
    controlLayout->addWidget(trajectory_view_btn_, 0, 4);

    raw_data_parser_btn_ = new QPushButton(this);
    connect(raw_data_parser_btn_, &QPushButton::clicked, this, &SessionViewerWindow::onRawDataParserClicked);
    controlLayout->addWidget(raw_data_parser_btn_, 0, 5);

    clear_view_btn_ = new QPushButton(this);
    connect(clear_view_btn_, &QPushButton::clicked, this, &SessionViewerWindow::onClearViewClicked);
    controlLayout->addWidget(clear_view_btn_, 0, 6);

    status_label_ = new QLabel(this);
    status_label_->setWordWrap(true);
    status_label_->setFocusPolicy(Qt::StrongFocus);
    controlLayout->addWidget(status_label_, 1, 0, 1, 7);

    mainLayout->addLayout(controlLayout);

    auto *summaryWaveSplitter = new QSplitter(Qt::Vertical, this);
    summaryWaveSplitter->setObjectName("sessionViewerContentSplitter");
    summaryWaveSplitter->setAttribute(Qt::WA_StyledBackground, true);
    summaryWaveSplitter->setAutoFillBackground(true);

    auto *upperWidget = new QWidget(this);
    upperWidget->setObjectName("sessionViewerContentPane");
    upperWidget->setAttribute(Qt::WA_StyledBackground, true);
    upperWidget->setAutoFillBackground(true);
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
    frame_slider_->setTracking(false);
    connect(frame_slider_, &QSlider::sliderMoved, this, &SessionViewerWindow::onFrameSliderMoved);
    connect(frame_slider_, &QSlider::valueChanged, this, &SessionViewerWindow::onFrameSliderChanged);
    frameLayout->addWidget(frame_slider_, 0, 1);

    frame_spin_ = new QSpinBox(this);
    frame_spin_->setRange(0, 0);
    frame_spin_->setEnabled(false);
    connect(frame_spin_, &QSpinBox::valueChanged, this, &SessionViewerWindow::onFrameSpinChanged);
    frameLayout->addWidget(frame_spin_, 0, 2);

    frame_total_label_ = new QLabel("---", this);
    frame_total_label_->setFont(numericFontFrom(frame_total_label_->font()));
    frame_total_label_->setFixedWidth(QFontMetrics(frame_total_label_->font()).horizontalAdvance(QStringLiteral("/ 999999999")) + 8);
    frameLayout->addWidget(frame_total_label_, 0, 3);

    frame_info_label_ = new QLabel(this);
    frame_info_label_->setFont(numericFontFrom(frame_info_label_->font()));
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
    peakHeaderLayout->addWidget(waveform_peak_plot_title_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    auto *waveformPeakRangeAxis = new RangeSelectionAxisWidget(this);
    waveformPeakRangeAxis->setCompactMode(true);
    waveformPeakRangeAxis->setMinimumWidth(240);
    peakHeaderLayout->addWidget(waveformPeakRangeAxis, 1, Qt::AlignVCenter);
    waveform_frame_filter_btn_ = new QPushButton(this);
    peakHeaderLayout->addWidget(waveform_frame_filter_btn_, 0, Qt::AlignVCenter | Qt::AlignRight);
    waveform_peak_filter_btn_ = new QPushButton(this);
    peakHeaderLayout->addWidget(waveform_peak_filter_btn_, 0, Qt::AlignVCenter | Qt::AlignRight);
    waveform_peak_mode_btn_ = new QPushButton(this);
    peakHeaderLayout->addWidget(waveform_peak_mode_btn_, 0, Qt::AlignVCenter | Qt::AlignRight);
    waveformLayout->addLayout(peakHeaderLayout);

    waveform_peak_plot_ = new SessionPeakPlotWidget(this);
    static_cast<SessionPeakPlotWidget*>(waveform_peak_plot_)->setPlotMode(
        waveform_peak_scatter_mode_ ? SessionPeakPlotWidget::PlotMode::Scatter : SessionPeakPlotWidget::PlotMode::Polyline);
    connect(waveform_frame_filter_btn_, &QPushButton::clicked, this, &SessionViewerWindow::onToggleWaveformFrameFilterClicked);
    connect(waveform_peak_filter_btn_, &QPushButton::clicked, this, &SessionViewerWindow::onConfigurePeakFilterClicked);
    connect(waveform_peak_mode_btn_, &QPushButton::clicked, this, &SessionViewerWindow::onTogglePeakPlotModeClicked);
    waveformLayout->addWidget(waveform_peak_plot_, 1);

    temperature_plot_title_ = new QLabel(this);
    temperature_plot_title_->setObjectName("fieldLabel");
    waveformLayout->addWidget(temperature_plot_title_);
    temperature_plot_ = new SingleSeriesTrendPlotWidget(QColor("#d14343"),
        is_english_ ? "No temperature series" : "没有温度趋势数据",
        QStringLiteral("°C"),
        this);
    static_cast<SingleSeriesTrendPlotWidget*>(temperature_plot_)->setPlotMode(
        waveform_peak_scatter_mode_ ? SingleSeriesTrendPlotWidget::PlotMode::Scatter : SingleSeriesTrendPlotWidget::PlotMode::Polyline);
    waveformLayout->addWidget(temperature_plot_);

    humidity_plot_title_ = new QLabel(this);
    humidity_plot_title_->setObjectName("fieldLabel");
    waveformLayout->addWidget(humidity_plot_title_);
    humidity_plot_ = new SingleSeriesTrendPlotWidget(QColor("#2f7fd3"),
        is_english_ ? "No humidity series" : "没有湿度趋势数据",
        QStringLiteral("%RH"),
        this);
    static_cast<SingleSeriesTrendPlotWidget*>(humidity_plot_)->setPlotMode(
        waveform_peak_scatter_mode_ ? SingleSeriesTrendPlotWidget::PlotMode::Scatter : SingleSeriesTrendPlotWidget::PlotMode::Polyline);
    waveformLayout->addWidget(humidity_plot_);

    pressure_plot_title_ = new QLabel(this);
    pressure_plot_title_->setObjectName("fieldLabel");
    waveformLayout->addWidget(pressure_plot_title_);
    pressure_plot_ = new SingleSeriesTrendPlotWidget(QColor("#2f9d57"),
        is_english_ ? "No pressure series" : "没有气压趋势数据",
        QStringLiteral("hPa"),
        this);
    static_cast<SingleSeriesTrendPlotWidget*>(pressure_plot_)->setPlotMode(
        waveform_peak_scatter_mode_ ? SingleSeriesTrendPlotWidget::PlotMode::Scatter : SingleSeriesTrendPlotWidget::PlotMode::Polyline);
    waveformLayout->addWidget(pressure_plot_);

    environment_info_label_ = new QLabel(this);
    environment_info_label_->setFont(numericFontFrom(environment_info_label_->font()));
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
    csv_table_->horizontalHeader()->setSectionsMovable(true);
    csv_table_->horizontalHeader()->setDefaultSectionSize(140);
    csv_table_->verticalHeader()->setVisible(false);
    applyCsvTableTheme();
    csvLayout->addWidget(csv_table_, 1);

    summaryWaveSplitter->addWidget(csv_group_);
    summaryWaveSplitter->setStretchFactor(0, 2);
    summaryWaveSplitter->setStretchFactor(1, 3);

    mainLayout->addWidget(summaryWaveSplitter, 1);
}

void SessionViewerWindow::applyCsvTableTheme()
{
    if (!csv_table_)
    {
        return;
    }

    const SessionTableTheme theme = sessionTableThemeFor(this);

    QPalette tablePalette = csv_table_->palette();
    tablePalette.setColor(QPalette::Base, theme.background);
    tablePalette.setColor(QPalette::AlternateBase, theme.background);
    tablePalette.setColor(QPalette::Text, theme.text);
    tablePalette.setColor(QPalette::WindowText, theme.text);
    tablePalette.setColor(QPalette::Highlight, theme.selectedBackground);
    tablePalette.setColor(QPalette::HighlightedText, theme.selectedText);
    csv_table_->setPalette(tablePalette);
    csv_table_->viewport()->setPalette(tablePalette);
    csv_table_->horizontalHeader()->setPalette(tablePalette);

    csv_table_->setStyleSheet(QStringLiteral(
        "QTableWidget {"
        " background-color: %1;"
        " alternate-background-color: %1;"
        " color: %2;"
        " gridline-color: %3;"
        " selection-background-color: %6;"
        " selection-color: %7;"
        "}"
        "QTableWidget::item { color: %2; }"
        "QTableWidget::item:selected { background-color: %6; color: %7; }"
        "QTableWidget::item:selected:active { background-color: %6; color: %7; }"
        "QTableWidget::item:selected:!active { background-color: %6; color: %7; }"
        "QHeaderView::section {"
        " background-color: %4;"
        " color: %5;"
        " border: 0px;"
        " border-right: 1px solid %3;"
        " border-bottom: 1px solid %3;"
        " padding: 4px 8px;"
        "}"
        "QTableCornerButton::section {"
        " background-color: %4;"
        " border: 0px;"
        " border-right: 1px solid %3;"
        " border-bottom: 1px solid %3;"
        "}")
        .arg(theme.background.name(),
             theme.text.name(),
             theme.grid.name(),
             theme.headerBackground.name(),
             theme.headerText.name(),
             theme.selectedBackground.name(),
             theme.selectedText.name()));
}

void SessionViewerWindow::refreshCsvItemTheme()
{
    if (!csv_table_)
    {
        return;
    }

    const SessionTableTheme theme = sessionTableThemeFor(this);
    const int currentRow = csv_table_->currentRow();
    for (int row = 0; row < csv_table_->rowCount(); ++row)
    {
        QColor background = theme.background;
        if (highlighted_csv_rows_.contains(row))
        {
            background = (row == currentRow) ? theme.highlightedBackground : theme.secondaryHighlightedBackground;
        }

        for (int col = 0; col < csv_table_->columnCount(); ++col)
        {
            if (QTableWidgetItem *item = csv_table_->item(row, col))
            {
                item->setBackground(background);
                item->setForeground(theme.text);
            }
        }
    }
    csv_table_->viewport()->update();
}

void SessionViewerWindow::setEnglish(bool english)
{
    is_english_ = english;
    updateTexts();
}

void SessionViewerWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event && (event->type() == QEvent::PaletteChange ||
                  event->type() == QEvent::ApplicationPaletteChange ||
                  event->type() == QEvent::StyleChange))
    {
        applyCsvTableTheme();
        refreshCsvItemTheme();
    }
}

void SessionViewerWindow::setDefaultDataDirectory(const QString& directory)
{
    const QString normalized = QDir::fromNativeSeparators(directory.trimmed());
    default_data_directory_ = normalized;
    if (!normalized.isEmpty())
    {
        QSettings settings("VaporView", "SessionViewer");
        settings.setValue("default_data_directory", normalized);
    }
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
    trajectory_view_btn_->setText(is_english_ ? "View Trajectory" : "轨迹查看");
    raw_data_parser_btn_->setText(is_english_ ? "Raw Data Parser..." : "原始数据解析...");
    clear_view_btn_->setText(is_english_ ? "Clear Page" : "清空页面");
    summary_group_->setTitle(is_english_ ? "Data Summary" : "数据概览");
    waveform_group_->setTitle(is_english_ ? "Normalized Second Harmonic" : "归一化二次谐波");
    waveform_plot_title_->setText(is_english_ ? "Current Frame Waveform" : "当前帧波形");
    waveform_peak_plot_title_->setText(is_english_ ? "Peak Value of Each Frame" : "每帧峰值");
    if (waveform_peak_plot_)
    {
        static_cast<SessionPeakPlotWidget*>(waveform_peak_plot_)->setEnglish(is_english_);
    }
    temperature_plot_title_->setText(is_english_ ? "Temperature" : "温度");
    humidity_plot_title_->setText(is_english_ ? "Humidity" : "湿度");
    pressure_plot_title_->setText(is_english_ ? "Pressure" : "气压");
    updateWaveformFrameFilterButtonText();
    updatePeakPlotModeButtonText();
    updatePeakFilterButtonText();
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

    if (trajectory_viewer_dialog_)
    {
        trajectory_viewer_dialog_->setEnglish(is_english_);
    }
    if (raw_data_parser_window_)
    {
        raw_data_parser_window_->setEnglish(is_english_);
    }
}

void SessionViewerWindow::updateWaveformFrameFilterButtonText()
{
    if (!waveform_frame_filter_btn_)
    {
        return;
    }

    waveform_frame_filter_btn_->setText(waveform_show_filtered_frame_
        ? (is_english_ ? "Show Full Frame" : "显示完整波形")
        : (is_english_ ? "Show Filtered Frame" : "显示过滤波形"));
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

QString SessionViewerWindow::peakFilterModeText(PeakFilterMode mode) const
{
    switch (mode)
    {
    case PeakFilterMode::IqrOutlier:
        return is_english_ ? QStringLiteral("IQR") : QStringLiteral("IQR");
    case PeakFilterMode::KeepRange:
        return is_english_ ? QStringLiteral("Keep Range") : QStringLiteral("保留区间");
    case PeakFilterMode::ExcludeRange:
        return is_english_ ? QStringLiteral("Exclude Range") : QStringLiteral("排除区间");
    case PeakFilterMode::None:
    default:
        return is_english_ ? QStringLiteral("Off") : QStringLiteral("关闭");
    }
}

void SessionViewerWindow::updatePeakFilterButtonText()
{
    if (!waveform_peak_filter_btn_)
    {
        return;
    }

    waveform_peak_filter_btn_->setText(QStringLiteral("%1:%2-%3 / %4")
        .arg(is_english_ ? QStringLiteral("Peak") : QStringLiteral("峰值"))
        .arg(peak_search_start_index_)
        .arg(peak_search_end_index_)
        .arg(peakFilterModeText(peak_filter_settings_.mode)));
}

void SessionViewerWindow::relayoutSummaryFields()
{
    if (!summary_layout_ || !summary_group_)
    {
        return;
    }

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

void SessionViewerWindow::setSessionLoadingControlsEnabled(bool enabled)
{
    if (choose_session_btn_) choose_session_btn_->setEnabled(enabled);
    if (reload_btn_) reload_btn_->setEnabled(enabled);
    if (raw_data_parser_btn_) raw_data_parser_btn_->setEnabled(enabled);
    if (clear_view_btn_) clear_view_btn_->setEnabled(enabled);
    if (waveform_frame_filter_btn_) waveform_frame_filter_btn_->setEnabled(enabled);
    if (waveform_peak_filter_btn_) waveform_peak_filter_btn_->setEnabled(enabled);
    if (waveform_peak_mode_btn_) waveform_peak_mode_btn_->setEnabled(enabled);
    if (trajectory_view_btn_) trajectory_view_btn_->setEnabled(enabled && !rtk_track_points_.isEmpty());
    if (enabled)
    {
        updateWaveformControls();
    }
}

void SessionViewerWindow::beginSessionLoading(const QString& text)
{
    session_loading_ = true;
    if (status_label_)
    {
        // Keep QScrollArea from following focus down to the CSV table when buttons are disabled.
        status_label_->setFocus(Qt::OtherFocusReason);
    }
    setSessionLoadingControlsEnabled(false);

    if (!loading_dialog_)
    {
        loading_dialog_ = new QProgressDialog(this);
        loading_dialog_->setWindowModality(Qt::NonModal);
        loading_dialog_->setModal(false);
        loading_dialog_->setCancelButton(nullptr);
        loading_dialog_->setMinimumDuration(0);
        loading_dialog_->setAutoClose(false);
        loading_dialog_->setAutoReset(false);
        loading_dialog_->setRange(0, 0);
        loading_dialog_->setMinimumWidth(360);
        loading_dialog_->setAttribute(Qt::WA_StyledBackground, true);
        loading_dialog_->setAutoFillBackground(true);
        QPalette loadingPalette = loading_dialog_->palette();
        loadingPalette.setColor(QPalette::Window, QColor("#fdfdfc"));
        loadingPalette.setColor(QPalette::Base, QColor("#ffffff"));
        loadingPalette.setColor(QPalette::Text, QColor("#111827"));
        loadingPalette.setColor(QPalette::WindowText, QColor("#111827"));
        loading_dialog_->setPalette(loadingPalette);
        loading_dialog_->setStyleSheet(QStringLiteral(
            "QProgressDialog { background-color: #fdfdfc; color: #111827; }"
            "QProgressDialog QLabel { background-color: transparent; color: #111827; }"
            "QProgressBar { background-color: #eef0f3; border: 1px solid #d8dde5; border-radius: 4px; min-height: 10px; }"
            "QProgressBar::chunk { background-color: #4b5563; border-radius: 3px; }"));
    }

    loading_dialog_->setWindowTitle(is_english_ ? "Loading Data" : "正在加载数据");
    updateSessionLoadingText(text);
    loading_dialog_->show();
    loading_dialog_->raise();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void SessionViewerWindow::updateSessionLoadingText(const QString& text)
{
    setStatusText(text);
    if (!session_loading_ || !loading_dialog_)
    {
        return;
    }

    loading_dialog_->setLabelText(text);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void SessionViewerWindow::finishSessionLoading()
{
    session_loading_ = false;
    if (loading_dialog_)
    {
        loading_dialog_->hide();
    }
    setSessionLoadingControlsEnabled(true);
}

void SessionViewerWindow::clearLoadedData(bool clearPathEdit)
{
    session_directory_.clear();
    metadata_filename_.clear();
    sensors_csv_filename_.clear();
    waveform_directory_.clear();
    waveform_index_filename_.clear();
    raw_tcp_wave_filename_.clear();
    session_name_.clear();
    start_time_utc_.clear();
    end_time_utc_.clear();
    csv_headers_.clear();
    csv_timestamps_us_.clear();
    temperature_values_.clear();
    humidity_values_.clear();
    pressure_values_.clear();
    rtk_track_points_.clear();
    waveform_timestamps_us_.clear();
    waveform_segments_.clear();
    raw_tcp_wave_frames_.clear();
    indexed_waveform_frames_.clear();
    current_waveform_frame_samples_.clear();
    waveform_peak_raw_values_.clear();
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
    trajectory_view_btn_->setEnabled(false);
    if (trajectory_viewer_dialog_)
    {
        trajectory_viewer_dialog_->setTrackPoints({});
    }
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

void SessionViewerWindow::restoreLastSessionPath(const QString& path)
{
    const QString sessionDirectory = resolveSessionDirectory(path);
    if (sessionDirectory.isEmpty())
    {
        return;
    }

    clearLoadedData(false);
    session_directory_ = sessionDirectory;
    if (session_path_edit_)
    {
        session_path_edit_->setText(session_directory_);
    }
    setStatusText(is_english_
        ? "Restored the last session path only. Click Reload to load its CSV and waveform files."
        : "已恢复上次会话路径，尚未读取大文件；点击“重新加载”后再加载 CSV 和波形数据。");
}

QString SessionViewerWindow::formatMeasuredRateText(const QVector<quint64>& timestampsUs, int metadataRateHz, const QString& metadataMode) const
{
    const double measuredRateHz = calculateMeasuredRateHz(timestampsUs);
    if (measuredRateHz > 0.0)
    {
        return QStringLiteral("%1 Hz").arg(QString::number(measuredRateHz, 'f', measuredRateHz >= 10.0 ? 2 : 3));
    }

    int validCount = 0;
    for (quint64 timestampUs : timestampsUs)
    {
        if (timestampUs != 0)
        {
            ++validCount;
        }
    }

    if (validCount == 1)
    {
        return is_english_ ? QStringLiteral("Single frame") : QStringLiteral("仅 1 条数据");
    }

    if (metadataMode == QStringLiteral("per_frame"))
    {
        return is_english_ ? QStringLiteral("Per-frame") : QStringLiteral("逐帧导出");
    }

    if (metadataRateHz > 0)
    {
        return QStringLiteral("%1 Hz").arg(metadataRateHz);
    }

    return QStringLiteral("---");
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
    QString initialDir = default_data_directory_;
    if (initialDir.isEmpty())
    {
        initialDir = settings.value("default_data_directory").toString();
    }
    if (initialDir.isEmpty())
    {
        const QString lastSession = settings.value("last_session_directory").toString();
        const QString lastSessionDirectory = resolveSessionDirectory(lastSession);
        if (!lastSessionDirectory.isEmpty())
        {
            initialDir = QFileInfo(lastSessionDirectory).absolutePath();
        }
    }
    if (initialDir.isEmpty() || !QFileInfo(initialDir).isDir())
    {
        initialDir = QDir::currentPath();
    }
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
        QSettings settings("VaporView", "SessionViewer");
        const QString lastSessionDirectory = resolveSessionDirectory(settings.value("last_session_directory").toString());
        if (lastSessionDirectory.isEmpty())
        {
            setStatusText(is_english_ ? "No session is currently loaded." : "当前没有已加载的会话。");
            return;
        }
        session_directory_ = lastSessionDirectory;
        if (session_path_edit_)
        {
            session_path_edit_->setText(session_directory_);
        }
    }

    loadSessionDirectory(session_directory_);
}

void SessionViewerWindow::onClearViewClicked()
{
    const QString previousSessionDirectory = session_directory_;
    clearLoadedData(previousSessionDirectory.isEmpty());
    if (!previousSessionDirectory.isEmpty())
    {
        session_directory_ = previousSessionDirectory;
        if (session_path_edit_)
        {
            session_path_edit_->setText(session_directory_);
        }
    }
}

void SessionViewerWindow::onViewTrajectoryClicked()
{
    if (rtk_track_points_.isEmpty())
    {
        QMessageBox::information(this,
            is_english_ ? "RTK Trajectory" : "RTK轨迹",
            is_english_ ? "No valid RTK latitude/longitude samples were found in the current session."
                        : "当前会话中没有找到有效的 RTK 经纬度轨迹点。");
        return;
    }

    if (!trajectory_viewer_dialog_)
    {
        trajectory_viewer_dialog_ = new TrajectoryViewerDialog();
        trajectory_viewer_dialog_->setAttribute(Qt::WA_QuitOnClose, false);
        trajectory_viewer_dialog_->setAttribute(Qt::WA_DeleteOnClose, true);
        connect(trajectory_viewer_dialog_, &QObject::destroyed, this, [this]() {
            trajectory_viewer_dialog_ = nullptr;
        });
    }

    trajectory_viewer_dialog_->setEnglish(is_english_);
    trajectory_viewer_dialog_->setTrackLabel(QStringLiteral("RTK trajectory"), QStringLiteral("RTK轨迹"));
    trajectory_viewer_dialog_->setTrackPoints(rtk_track_points_);
    VaporView::centerWindowOnScreen(trajectory_viewer_dialog_, this);
    trajectory_viewer_dialog_->show();
    trajectory_viewer_dialog_->raise();
    trajectory_viewer_dialog_->activateWindow();
}

void SessionViewerWindow::onRawDataParserClicked()
{
    if (session_directory_.isEmpty())
    {
        QMessageBox::information(this,
            is_english_ ? "Raw Data Parser" : "原始数据解析器",
            is_english_ ? "Choose or restore a session directory first."
                        : "请先选择或恢复一个 session 目录。");
        return;
    }

    if (!raw_data_parser_window_)
    {
        raw_data_parser_window_ = new RawDataParserWindow();
        raw_data_parser_window_->setAttribute(Qt::WA_QuitOnClose, false);
        raw_data_parser_window_->setAttribute(Qt::WA_DeleteOnClose, true);
        connect(raw_data_parser_window_, &QObject::destroyed, this, [this]() {
            raw_data_parser_window_ = nullptr;
        });
    }

    raw_data_parser_window_->setEnglish(is_english_);
    raw_data_parser_window_->openSessionPath(session_directory_);
    VaporView::centerWindowOnScreen(raw_data_parser_window_, this);
    raw_data_parser_window_->show();
    raw_data_parser_window_->raise();
    raw_data_parser_window_->activateWindow();
}

bool SessionViewerWindow::loadSessionDirectory(QString sessionDirectory)
{
    beginSessionLoading(is_english_ ? "Preparing to load session data..." : "正在准备加载会话数据...");
    clearLoadedData(false);

    const QString normalized = QDir::fromNativeSeparators(sessionDirectory);
    session_directory_ = normalized;
    updateSessionLoadingText(is_english_ ? "Reading session metadata..." : "正在读取会话元数据...");
    if (!loadSessionMetadata(normalized))
    {
        finishSessionLoading();
        return false;
    }
    updateSessionLoadingText(is_english_ ? "Reading sensors CSV..." : "正在读取传感器 CSV...");
    if (!loadSensorsCsv())
    {
        finishSessionLoading();
        return false;
    }
    updateSessionLoadingText(is_english_ ? "Indexing waveform files..." : "正在索引波形文件...");
    if (!loadWaveformSegments())
    {
        finishSessionLoading();
        return false;
    }
    updateSessionLoadingText(is_english_ ? "Calculating waveform peak series..." : "正在计算波形峰值序列...");
    if (!loadWaveformPeakSeries())
    {
        finishSessionLoading();
        return false;
    }

    updateSessionLoadingText(is_english_ ? "Updating viewer..." : "正在更新显示...");
    session_path_edit_->setText(session_directory_);
    updateSummaryLabels();
    updateWaveformControls();

    if (total_waveform_frames_ > 0)
    {
        {
            const QSignalBlocker sliderBlocker(frame_slider_);
            const QSignalBlocker spinBlocker(frame_spin_);
            frame_slider_->setValue(1);
            frame_spin_->setValue(1);
        }
        loadWaveformFrame(0, false);
    }
    else
    {
        static_cast<SessionWavePlotWidget*>(waveform_plot_)->setSamples({});
        frame_info_label_->setText(is_english_ ? "No waveform frame file was found in this session."
                                               : "这个会话里没有找到波形帧文件。");
    }

    setStatusText(QString(is_english_ ? "Loaded session: %1" : "已加载会话: %1").arg(session_directory_));
    finishSessionLoading();
    return true;
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
    const QString waveformIndexRelativePath = paths.value(QStringLiteral("waveform_index")).toString(QStringLiteral("waveform_index.csv"));
    const QJsonObject rawFiles = root.value(QStringLiteral("raw_files")).toObject();
    const QJsonObject tcpWaveRaw = rawFiles.value(QStringLiteral("tcp_wave")).toObject();
    const QString rawTcpWaveRelativePath = tcpWaveRaw.value(QStringLiteral("path")).toString(QStringLiteral("raw/tcp_wave.dat"));
    sensors_csv_filename_ = QDir(sessionDirectory).filePath(csvRelativePath);
    waveform_directory_ = QDir(sessionDirectory).filePath(waveformRelativePath);
    waveform_index_filename_ = QDir(sessionDirectory).filePath(waveformIndexRelativePath);
    raw_tcp_wave_filename_ = QDir(sessionDirectory).filePath(rawTcpWaveRelativePath);
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
    const int recordTimestampIndex = findHeaderIndex(csv_headers_, {QStringLiteral("record_timestamp_us")});
    const int epsilonHostTimestampIndex = findHeaderIndex(csv_headers_, {QStringLiteral("epsilon_host_timestamp_us")});
    const int navLatIndex = findHeaderIndex(csv_headers_, {QStringLiteral("nav_lat_deg"), QStringLiteral("rtk_lat")});
    const int navLonIndex = findHeaderIndex(csv_headers_, {QStringLiteral("nav_lon_deg"), QStringLiteral("rtk_lon")});
    const int navHeightIndex = findHeaderIndex(csv_headers_, {QStringLiteral("nav_height_m"), QStringLiteral("rtk_height"), QStringLiteral("height_m"), QStringLiteral("altitude_m")});
    const int trackTimestampIndex = findHeaderIndex(csv_headers_, {
        QStringLiteral("epsilon_host_timestamp_us"),
        QStringLiteral("record_timestamp_us"),
        QStringLiteral("rtk_timestamp_us")
    });
    const int epsilonValidIndex = findHeaderIndex(csv_headers_, {QStringLiteral("epsilon_valid"), QStringLiteral("rtk_valid")});
    const int gnssFixIndex = findHeaderIndex(csv_headers_, {QStringLiteral("gnss_fix"), QStringLiteral("rtk_fix")});
    const int thValidIndex = findHeaderIndex(csv_headers_, {QStringLiteral("th_valid")});
    const int baroValidIndex = findHeaderIndex(csv_headers_, {QStringLiteral("baro_valid")});
    const bool hasLegacyThermometerColumns =
        findHeaderIndex(csv_headers_, {QStringLiteral("th_timestamp_us"), QStringLiteral("th_valid")}) >= 0;
    const bool hasLegacyBarometerColumns =
        findHeaderIndex(csv_headers_, {QStringLiteral("baro_timestamp_us"), QStringLiteral("baro_valid")}) >= 0;
    const int hmpTemperatureIndex = findHeaderIndex(csv_headers_,
        hasLegacyThermometerColumns
            ? QStringList{QStringLiteral("hmp_temperature_c"), QStringLiteral("temp_c")}
            : QStringList{QStringLiteral("hmp_temperature_c")});
    const int hmpHumidityIndex = findHeaderIndex(csv_headers_, {QStringLiteral("hmp_humidity_rh"), QStringLiteral("humidity_rh")});
    const int ptbPressureIndex = findHeaderIndex(csv_headers_,
        hasLegacyBarometerColumns
            ? QStringList{QStringLiteral("ptb_pressure_hpa"), QStringLiteral("baro_hpa"), QStringLiteral("baro_pa")}
            : QStringList{QStringLiteral("ptb_pressure_hpa")});
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
    rtk_track_points_.clear();
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
        quint64 timestampUs = 0;
        if (recordTimestampIndex >= 0)
        {
            timestampUs = csvValueAt(fields, recordTimestampIndex).toULongLong(&ok);
        }
        else if (epsilonHostTimestampIndex >= 0)
        {
            timestampUs = csvValueAt(fields, epsilonHostTimestampIndex).toULongLong(&ok);
        }
        else if (trackTimestampIndex >= 0)
        {
            timestampUs = csvValueAt(fields, trackTimestampIndex).toULongLong(&ok);
        }
        else
        {
            timestampUs = csvValueAt(fields, 0).toULongLong(&ok);
        }
        csv_timestamps_us_.push_back(timestampUs);
        if (!ok)
        {
            csv_timestamps_us_.last() = 0;
        }

        const bool thValid = thValidIndex < 0 || parseBooleanCsvField(csvValueAt(fields, thValidIndex), true);
        const bool baroValid = baroValidIndex < 0 || parseBooleanCsvField(csvValueAt(fields, baroValidIndex), true);
        double temperatureValue = std::numeric_limits<double>::quiet_NaN();
        if (hmpTemperatureIndex >= 0 && thValid)
        {
            temperatureValue = parseOptionalDouble(csvValueAt(fields, hmpTemperatureIndex));
        }
        temperature_values_.push_back(temperatureValue);

        humidity_values_.push_back((hmpHumidityIndex >= 0 && thValid)
                                       ? parseOptionalDouble(csvValueAt(fields, hmpHumidityIndex))
                                       : std::numeric_limits<double>::quiet_NaN());

        double pressureValue = std::numeric_limits<double>::quiet_NaN();
        if (ptbPressureIndex >= 0 && baroValid)
        {
            pressureValue = parseOptionalDouble(csvValueAt(fields, ptbPressureIndex));
            const QString pressureHeader = csv_headers_.value(ptbPressureIndex).trimmed().toLower();
            if (std::isfinite(pressureValue) && pressureHeader.endsWith(QStringLiteral("_pa")))
            {
                pressureValue /= 100.0;
            }
        }
        pressure_values_.push_back(pressureValue);

        const bool navValid = epsilonValidIndex < 0 || parseBooleanCsvField(csvValueAt(fields, epsilonValidIndex), true);
        const double lat = parseOptionalDouble(csvValueAt(fields, navLatIndex));
        const double lon = parseOptionalDouble(csvValueAt(fields, navLonIndex));
        const QString gnssFix = csvValueAt(fields, gnssFixIndex).trimmed().toUpper();
        if (navValid &&
            std::isfinite(lat) &&
            std::isfinite(lon) &&
            !(std::abs(lat) < 1e-8 && std::abs(lon) < 1e-8) &&
            lat >= -90.0 && lat <= 90.0 &&
            lon >= -180.0 && lon <= 180.0 &&
            gnssFix != QStringLiteral("NONE") &&
            gnssFix != QStringLiteral("NO_FIX") &&
            gnssFix != QStringLiteral("INVALID") &&
            gnssFix != QStringLiteral("NO_GPS"))
        {
            bool timestampOk = false;
            const quint64 trackTimestampUs = trackTimestampIndex >= 0
                ? csvValueAt(fields, trackTimestampIndex).toULongLong(&timestampOk)
                : csv_timestamps_us_.last();
            RtkTrackPoint point;
            point.latitude = lat;
            point.longitude = lon;
            const double height = parseOptionalDouble(csvValueAt(fields, navHeightIndex));
            if (std::isfinite(height))
            {
                point.height_m = height;
                point.has_height = true;
            }
            point.timestamp_us = timestampOk ? trackTimestampUs : csv_timestamps_us_.last();
            if (!rtk_track_points_.isEmpty())
            {
                const RtkTrackPoint& previous = rtk_track_points_.last();
                const double jumpMeters = haversineDistanceMeters(previous.latitude, previous.longitude, point.latitude, point.longitude);
                if (jumpMeters > 20.0)
                {
                    continue;
                }
            }
            rtk_track_points_.push_back(point);
        }

        if (session_loading_ && rows.size() % 5000 == 0)
        {
            updateSessionLoadingText(QString(is_english_
                ? "Reading sensors CSV... %1 rows"
                : "正在读取传感器 CSV... %1 行")
                .arg(rows.size()));
        }
    }

    csv_table_->setRowCount(rows.size());
    const SessionTableTheme tableTheme = sessionTableThemeFor(this);
    for (int row = 0; row < rows.size(); ++row)
    {
        auto *indexItem = new QTableWidgetItem(QString::number(row + 1));
        indexItem->setBackground(tableTheme.background);
        indexItem->setForeground(tableTheme.text);
        csv_table_->setItem(row, 0, indexItem);

        auto *deltaItem = new QTableWidgetItem(QString());
        deltaItem->setBackground(tableTheme.background);
        deltaItem->setForeground(tableTheme.text);
        csv_table_->setItem(row, 1, deltaItem);

        const QStringList& fields = rows.at(row);
        for (int col = 0; col < csv_headers_.size(); ++col)
        {
            auto *item = new QTableWidgetItem(csvValueAt(fields, col));
            item->setBackground(tableTheme.background);
            item->setForeground(tableTheme.text);
            csv_table_->setItem(row, col + 2, item);
        }

        if (session_loading_ && (row + 1) % 5000 == 0)
        {
            updateSessionLoadingText(QString(is_english_
                ? "Rendering CSV table... %1/%2 rows"
                : "正在渲染 CSV 表格... %1/%2 行")
                .arg(row + 1)
                .arg(rows.size()));
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
    updateRtkTrackPeakValues();
    trajectory_view_btn_->setEnabled(!rtk_track_points_.isEmpty());
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
    raw_tcp_wave_frames_.clear();
    indexed_waveform_frames_.clear();
    total_waveform_frames_ = 0;

    if (loadUnifiedRawTcpWaveFrames() && !raw_tcp_wave_frames_.isEmpty())
    {
        total_waveform_frames_ = static_cast<quint64>(raw_tcp_wave_frames_.size());
        return true;
    }

    if (loadIndexedWaveformFrames() && !indexed_waveform_frames_.isEmpty())
    {
        total_waveform_frames_ = static_cast<quint64>(indexed_waveform_frames_.size());
        return true;
    }

    QDir dir(waveform_directory_);
    if (!dir.exists())
    {
        if (!QFileInfo::exists(raw_tcp_wave_filename_))
        {
            setStatusText(QString(is_english_
                ? "No raw TCP wave file or legacy waveform directory was found."
                : "没有找到 raw TCP 波形文件，也没有找到旧版 waveform 目录。"));
        }
        return true;
    }

    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.dat"), QDir::Files, QDir::Name);
    const quint64 frameBytes = kWaveformTimestampBytes + static_cast<quint64>(points_per_frame_) * kFloatBytes;

    for (int fileIndex = 0; fileIndex < files.size(); ++fileIndex)
    {
        const QString& filename = files.at(fileIndex);
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

        if (session_loading_ && (fileIndex + 1) % 20 == 0)
        {
            updateSessionLoadingText(QString(is_english_
                ? "Indexing waveform files... %1/%2 files"
                : "正在索引波形文件... %1/%2 个文件")
                .arg(fileIndex + 1)
                .arg(files.size()));
        }
    }

    return true;
}

bool SessionViewerWindow::loadIndexedWaveformFrames()
{
    if (waveform_index_filename_.isEmpty() || !QFileInfo::exists(waveform_index_filename_))
    {
        return true;
    }

    QFile file(waveform_index_filename_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        setStatusText(QString(is_english_ ? "Failed to open waveform index: %1" : "打开波形索引失败: %1").arg(waveform_index_filename_));
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    if (stream.atEnd())
    {
        return true;
    }

    const QStringList headers = parseCsvLine(stream.readLine());
    const int hostTimeIndex = findHeaderIndex(headers, {QStringLiteral("host_time_us")});
    const int epsilonTimeIndex = findHeaderIndex(headers, {QStringLiteral("epsilon_time_us")});
    const int pointCountIndex = findHeaderIndex(headers, {QStringLiteral("point_count")});
    const int filenameIndex = findHeaderIndex(headers, {QStringLiteral("filename")});
    if ((hostTimeIndex < 0 && epsilonTimeIndex < 0) || pointCountIndex < 0 || filenameIndex < 0)
    {
        setStatusText(QString(is_english_ ? "Invalid waveform index header: %1" : "波形索引表头无效: %1").arg(waveform_index_filename_));
        return false;
    }

    const QDir sessionDir(session_directory_);
    while (!stream.atEnd())
    {
        const QString line = stream.readLine();
        if (line.trimmed().isEmpty())
        {
            continue;
        }

        const QStringList fields = parseCsvLine(line);
        bool timeOk = false;
        bool countOk = false;
        const quint64 timestampUs = csvValueAt(fields, hostTimeIndex >= 0 ? hostTimeIndex : epsilonTimeIndex).toULongLong(&timeOk);
        const quint32 pointCount = csvValueAt(fields, pointCountIndex).toUInt(&countOk);
        const QString relativeFilename = csvValueAt(fields, filenameIndex).trimmed();
        if (!timeOk || !countOk || pointCount == 0 || relativeFilename.isEmpty())
        {
            continue;
        }

        const QString absolutePath = sessionDir.filePath(relativeFilename);
        const QFileInfo info(absolutePath);
        const quint64 expectedBytes = static_cast<quint64>(pointCount) * kFloatBytes;
        if (!info.exists() || info.size() < 0 || static_cast<quint64>(info.size()) < expectedBytes)
        {
            continue;
        }

        IndexedWaveformFrame frame;
        frame.filename = absolutePath;
        frame.timestamp_us = timestampUs;
        frame.point_count = pointCount;
        indexed_waveform_frames_.push_back(frame);
    }

    if (!indexed_waveform_frames_.isEmpty() && points_per_frame_ <= 0)
    {
        points_per_frame_ = static_cast<int>(indexed_waveform_frames_.first().point_count);
    }
    return true;
}

bool SessionViewerWindow::loadUnifiedRawTcpWaveFrames()
{
    if (raw_tcp_wave_filename_.isEmpty() || !QFileInfo::exists(raw_tcp_wave_filename_))
    {
        return true;
    }

    QFile file(raw_tcp_wave_filename_);
    if (!file.open(QIODevice::ReadOnly))
    {
        setStatusText(QString(is_english_ ? "Failed to open raw TCP wave file: %1" : "打开 raw TCP 波形文件失败: %1").arg(raw_tcp_wave_filename_));
        return false;
    }

    UnifiedRawFileHeader fileHeader{};
    if (file.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader)) != static_cast<qint64>(sizeof(fileHeader)) ||
        std::memcmp(fileHeader.magic, kUnifiedRawMagic, sizeof(fileHeader.magic)) != 0)
    {
        setStatusText(QString(is_english_ ? "Invalid raw TCP wave DAT header: %1" : "raw TCP 波形 DAT 文件头无效: %1").arg(raw_tcp_wave_filename_));
        return false;
    }

    const quint32 fileHeaderSize = qFromLittleEndian(fileHeader.header_size);
    const quint16 sourceId = qFromLittleEndian(fileHeader.source_id);
    if (fileHeaderSize < sizeof(UnifiedRawFileHeader) || sourceId != kRawSourceTcpWave)
    {
        setStatusText(QString(is_english_ ? "Unexpected raw TCP wave DAT source: %1" : "raw TCP 波形 DAT 数据源不匹配: %1").arg(raw_tcp_wave_filename_));
        return false;
    }
    if (fileHeaderSize > sizeof(UnifiedRawFileHeader) && !file.seek(fileHeaderSize))
    {
        return false;
    }

    while (!file.atEnd())
    {
        const qint64 recordStart = file.pos();
        UnifiedRawRecordHeader recordHeader{};
        const qint64 headerBytes = file.read(reinterpret_cast<char*>(&recordHeader), sizeof(recordHeader));
        if (headerBytes == 0)
        {
            break;
        }
        if (headerBytes != static_cast<qint64>(sizeof(recordHeader)))
        {
            setStatusText(QString(is_english_ ? "Incomplete raw TCP wave record header in %1" : "%1 中存在不完整 raw TCP 波形记录头").arg(raw_tcp_wave_filename_));
            return false;
        }

        const quint32 marker = qFromLittleEndian(recordHeader.marker);
        const quint32 recordHeaderSize = qFromLittleEndian(recordHeader.header_size);
        const quint64 timestampUs = qFromLittleEndian(recordHeader.host_timestamp_us);
        const quint32 payloadSize = qFromLittleEndian(recordHeader.payload_size);
        const quint16 recordSourceId = qFromLittleEndian(recordHeader.source_id);
        const quint32 flags = qFromLittleEndian(recordHeader.flags);
        if (marker != kUnifiedRawRecordMarker || recordHeaderSize < sizeof(UnifiedRawRecordHeader))
        {
            setStatusText(QString(is_english_ ? "Invalid raw TCP wave record marker in %1" : "%1 中存在无效 raw TCP 波形记录标记").arg(raw_tcp_wave_filename_));
            return false;
        }

        const qint64 payloadOffset = recordStart + static_cast<qint64>(recordHeaderSize);
        const qint64 nextRecord = payloadOffset + static_cast<qint64>(payloadSize);
        if (!file.seek(payloadOffset))
        {
            return false;
        }

        if (recordSourceId == kRawSourceTcpWave && (flags & kRawTcpWaveCombinedPayloadFlag) != 0 && payloadSize >= sizeof(quint32) * 2)
        {
            quint32 rawSignalSizeLe = 0;
            quint32 harmonicSizeLe = 0;
            if (file.read(reinterpret_cast<char*>(&rawSignalSizeLe), sizeof(rawSignalSizeLe)) != static_cast<qint64>(sizeof(rawSignalSizeLe)) ||
                file.read(reinterpret_cast<char*>(&harmonicSizeLe), sizeof(harmonicSizeLe)) != static_cast<qint64>(sizeof(harmonicSizeLe)))
            {
                return false;
            }

            const quint32 rawSignalSize = qFromLittleEndian(rawSignalSizeLe);
            const quint32 harmonicSize = qFromLittleEndian(harmonicSizeLe);
            const quint64 requiredPayloadSize = static_cast<quint64>(sizeof(quint32) * 2) + rawSignalSize + harmonicSize;
            if (requiredPayloadSize <= payloadSize && harmonicSize > 0 && harmonicSize % kFloatBytes == 0)
            {
                RawTcpWaveFrame frame;
                frame.filename = raw_tcp_wave_filename_;
                frame.harmonic_payload_offset = static_cast<quint64>(payloadOffset) + sizeof(quint32) * 2ULL + rawSignalSize;
                frame.harmonic_payload_size = harmonicSize;
                frame.timestamp_us = timestampUs;
                frame.float_encoding = VaporView::tcpFloatEncodingFromRawDatFlags(flags);
                raw_tcp_wave_frames_.push_back(frame);
                if (points_per_frame_ <= 0)
                {
                    points_per_frame_ = static_cast<int>(harmonicSize / kFloatBytes);
                }
                if (session_loading_ && raw_tcp_wave_frames_.size() % 2000 == 0)
                {
                    updateSessionLoadingText(QString(is_english_
                        ? "Indexing raw TCP waveform frames... %1 frames"
                        : "正在索引 raw TCP 波形帧... %1 帧")
                        .arg(raw_tcp_wave_frames_.size()));
                }
            }
        }

        if (!file.seek(nextRecord))
        {
            break;
        }
    }

    return true;
}

void SessionViewerWindow::applyPeakFilter()
{
    if (session_loading_)
    {
        updateSessionLoadingText(is_english_ ? "Applying peak filter..." : "正在应用峰值过滤...");
    }
    waveform_peak_values_.clear();
    waveform_peak_values_.reserve(waveform_peak_raw_values_.size());

    QVector<double> finiteValues;
    finiteValues.reserve(waveform_peak_raw_values_.size());
    for (int index = 0; index < waveform_peak_raw_values_.size(); ++index)
    {
        const float value = waveform_peak_raw_values_.at(index);
        if (std::isfinite(value))
        {
            finiteValues.push_back(static_cast<double>(value));
        }
        if (session_loading_ && (index + 1) % 100000 == 0)
        {
            updateSessionLoadingText(QString(is_english_
                ? "Preparing peak filter... %1/%2 values"
                : "正在准备峰值过滤... %1/%2 个值")
                .arg(index + 1)
                .arg(waveform_peak_raw_values_.size()));
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
    for (int index = 0; index < waveform_peak_raw_values_.size(); ++index)
    {
        const float rawValue = waveform_peak_raw_values_.at(index);
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

        waveform_peak_values_.push_back(keepValue
            ? rawValue
            : std::numeric_limits<float>::quiet_NaN());
        if (session_loading_ && ((index + 1) == waveform_peak_raw_values_.size() || (index + 1) % 100000 == 0))
        {
            updateSessionLoadingText(QString(is_english_
                ? "Applying peak filter... %1/%2 values"
                : "正在应用峰值过滤... %1/%2 个值")
                .arg(index + 1)
                .arg(waveform_peak_raw_values_.size()));
        }
    }

    if (session_loading_)
    {
        updateSessionLoadingText(is_english_ ? "Refreshing filtered plots..." : "正在刷新过滤后的图表...");
    }
    if (waveform_peak_plot_)
    {
        static_cast<SessionPeakPlotWidget*>(waveform_peak_plot_)->setPeakValues(waveform_peak_values_);
    }
    updateRtkTrackPeakValues();
    if (frame_spin_ && frame_spin_->value() > 0)
    {
        loadWaveformFrame(static_cast<quint64>(frame_spin_->value() - 1));
    }
}

bool SessionViewerWindow::loadWaveformPeakSeries()
{
    waveform_peak_raw_values_.clear();
    waveform_peak_values_.clear();
    waveform_timestamps_us_.clear();
    static_cast<SessionPeakPlotWidget*>(waveform_peak_plot_)->setPeakValues({});
    static_cast<SessionPeakPlotWidget*>(waveform_peak_plot_)->setCurrentFrame(-1);

    if (waveform_segments_.isEmpty() && raw_tcp_wave_frames_.isEmpty() && indexed_waveform_frames_.isEmpty())
    {
        return true;
    }
    if (!indexed_waveform_frames_.isEmpty() && points_per_frame_ <= 0)
    {
        points_per_frame_ = static_cast<int>(indexed_waveform_frames_.first().point_count);
    }
    if (waveform_segments_.isEmpty() && raw_tcp_wave_frames_.isEmpty() && points_per_frame_ <= 0)
    {
        return true;
    }

    waveform_peak_raw_values_.reserve(static_cast<int>(std::min<quint64>(total_waveform_frames_, static_cast<quint64>(std::numeric_limits<int>::max()))));
    waveform_timestamps_us_.reserve(static_cast<int>(std::min<quint64>(total_waveform_frames_, static_cast<quint64>(std::numeric_limits<int>::max()))));

    const quint64 progressInterval = std::max<quint64>(1, total_waveform_frames_ / 100);
    for (quint64 frameIndex = 0; frameIndex < total_waveform_frames_; ++frameIndex)
    {
        quint64 timestampUs = 0;
        QVector<float> frameSamples;
        if (!readWaveformFrameSamples(frameIndex, timestampUs, frameSamples))
        {
            return false;
        }

        waveform_timestamps_us_.push_back(timestampUs);
        waveform_peak_raw_values_.push_back(waveformPeakValue(frameSamples, peak_search_start_index_, peak_search_end_index_));
        if (session_loading_ && ((frameIndex + 1) == total_waveform_frames_ || (frameIndex + 1) % progressInterval == 0))
        {
            updateSessionLoadingText(QString(is_english_
                ? "Calculating waveform peak series... %1/%2 frames"
                : "正在计算波形峰值序列... %1/%2 帧")
                .arg(frameIndex + 1)
                .arg(total_waveform_frames_));
        }
    }

    applyPeakFilter();
    return true;
}

void SessionViewerWindow::updateSummaryLabels()
{
    const bool hasSession = !session_name_.isEmpty() || !metadata_filename_.isEmpty();
    session_name_value_->setText(session_name_.isEmpty() ? QStringLiteral("---") : session_name_);
    start_time_value_->setText(start_time_utc_.isEmpty() ? QStringLiteral("---") : start_time_utc_);
    end_time_value_->setText(end_time_utc_.isEmpty() ? QStringLiteral("---") : end_time_utc_);
    duration_value_->setText(hasSession ? formatDurationText(start_time_utc_, end_time_utc_, is_english_) : QStringLiteral("---"));
    sensor_export_rate_value_->setText(hasSession
        ? formatMeasuredRateText(csv_timestamps_us_, sensor_export_rate_hz_, QStringLiteral("fixed_rate"))
        : QStringLiteral("---"));
    sensor_rows_value_->setText(hasSession ? QString::number(total_sensor_rows_) : QStringLiteral("---"));
    waveform_export_rate_value_->setText(hasSession
        ? formatMeasuredRateText(waveform_timestamps_us_, waveform_export_rate_hz_, waveform_export_mode_)
        : QStringLiteral("---"));
    const int waveformFileCount = !raw_tcp_wave_frames_.isEmpty()
        ? 1
        : !indexed_waveform_frames_.isEmpty()
            ? indexed_waveform_frames_.size()
            : waveform_segments_.size();
    waveform_files_value_->setText(hasSession ? QString::number(waveformFileCount) : QStringLiteral("---"));
    waveform_frames_value_->setText(hasSession ? QString::number(total_waveform_frames_) : QStringLiteral("---"));
}

void SessionViewerWindow::updateWaveformControls()
{
    const bool hasFrames = total_waveform_frames_ > 0 &&
        (!waveform_segments_.isEmpty() || !raw_tcp_wave_frames_.isEmpty() || !indexed_waveform_frames_.isEmpty());
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

    const int totalDigits = std::max(1, static_cast<int>(QString::number(std::max<quint64>(total_waveform_frames_, 1ULL)).size()));
    frame_total_label_->setText(hasFrames
        ? QStringLiteral("/ %1").arg(fixedIntegerField(total_waveform_frames_, totalDigits))
        : QStringLiteral("/ %1").arg(fixedIntegerField(0, totalDigits)));
}

void SessionViewerWindow::onFrameSliderMoved(int value)
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
        previewWaveformFrame(static_cast<quint64>(value - 1));
    }
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

QVector<float> SessionViewerWindow::visibleWaveformSamples(const QVector<float>& samples, int& firstSampleIndex) const
{
    firstSampleIndex = 0;
    if (!waveform_show_filtered_frame_ || samples.isEmpty())
    {
        return samples;
    }

    const int sampleCount = static_cast<int>(samples.size());
    const int startIndex = std::clamp(peak_search_start_index_, 0, sampleCount);
    const int endIndex = std::clamp(peak_search_end_index_, 0, sampleCount);
    if (startIndex >= endIndex)
    {
        return samples;
    }

    firstSampleIndex = startIndex;
    return samples.mid(startIndex, endIndex - startIndex);
}

void SessionViewerWindow::onToggleWaveformFrameFilterClicked()
{
    waveform_show_filtered_frame_ = !waveform_show_filtered_frame_;
    updateWaveformFrameFilterButtonText();
    int firstSampleIndex = 0;
    static_cast<SessionWavePlotWidget*>(waveform_plot_)->setSamples(
        visibleWaveformSamples(current_waveform_frame_samples_, firstSampleIndex),
        firstSampleIndex);
}

void SessionViewerWindow::onTogglePeakPlotModeClicked()
{
    beginSessionLoading(waveform_peak_scatter_mode_
        ? (is_english_ ? "Switching to polyline plots..." : "正在切换到折线图...")
        : (is_english_ ? "Switching to scatter plots..." : "正在切换到散点图..."));
    waveform_peak_scatter_mode_ = !waveform_peak_scatter_mode_;
    updatePeakPlotModeButtonText();
    static_cast<SessionPeakPlotWidget*>(waveform_peak_plot_)->setPlotMode(
        waveform_peak_scatter_mode_ ? SessionPeakPlotWidget::PlotMode::Scatter : SessionPeakPlotWidget::PlotMode::Polyline);
    static_cast<SingleSeriesTrendPlotWidget*>(temperature_plot_)->setPlotMode(
        waveform_peak_scatter_mode_ ? SingleSeriesTrendPlotWidget::PlotMode::Scatter : SingleSeriesTrendPlotWidget::PlotMode::Polyline);
    static_cast<SingleSeriesTrendPlotWidget*>(humidity_plot_)->setPlotMode(
        waveform_peak_scatter_mode_ ? SingleSeriesTrendPlotWidget::PlotMode::Scatter : SingleSeriesTrendPlotWidget::PlotMode::Polyline);
    static_cast<SingleSeriesTrendPlotWidget*>(pressure_plot_)->setPlotMode(
        waveform_peak_scatter_mode_ ? SingleSeriesTrendPlotWidget::PlotMode::Scatter : SingleSeriesTrendPlotWidget::PlotMode::Polyline);
    updateSessionLoadingText(is_english_ ? "Refreshing plots..." : "正在刷新图表...");
    waveform_peak_plot_->repaint();
    temperature_plot_->repaint();
    humidity_plot_->repaint();
    pressure_plot_->repaint();
    finishSessionLoading();
}

void SessionViewerWindow::onConfigurePeakFilterClicked()
{
    QDialog dialog(this);
    dialog.setWindowTitle(is_english_ ? QStringLiteral("Peak Settings") : QStringLiteral("峰值设置"));
    VaporView::installCustomTitleBar(&dialog, false);

    QWidget *content = dialog.findChild<QWidget *>(QStringLiteral("customTitleBarContent"));
    if (!content)
    {
        content = &dialog;
    }
    auto *layout = qobject_cast<QVBoxLayout *>(content->layout());
    if (!layout)
    {
        layout = new QVBoxLayout(content);
    }
    layout->setContentsMargins(22, 18, 22, 18);
    layout->setSpacing(14);

    auto *formWidget = new QWidget(content);
    auto *formLayout = new QGridLayout(formWidget);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setHorizontalSpacing(14);
    formLayout->setVerticalSpacing(10);
    const int labelColumnWidth = is_english_ ? 104 : 86;
    const int inputColumnWidth = 240;
    auto addFormRow = [formWidget, formLayout, labelColumnWidth](int row, const QString& labelText, QWidget *editor) {
        auto *label = new QLabel(labelText, formWidget);
        label->setMinimumWidth(labelColumnWidth);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        editor->setMinimumHeight(34);
        formLayout->addWidget(label, row, 0, Qt::AlignRight | Qt::AlignVCenter);
        formLayout->addWidget(editor, row, 1);
    };

    auto *searchStartSpin = new QSpinBox(formWidget);
    searchStartSpin->setRange(0, 10000000);
    searchStartSpin->setSingleStep(1000);
    searchStartSpin->setValue(peak_search_start_index_);
    searchStartSpin->setMinimumWidth(inputColumnWidth);
    addFormRow(0, is_english_ ? QStringLiteral("Search Start") : QStringLiteral("搜索起点"), searchStartSpin);

    auto *searchEndSpin = new QSpinBox(formWidget);
    searchEndSpin->setRange(1, 10000000);
    searchEndSpin->setSingleStep(1000);
    searchEndSpin->setValue(peak_search_end_index_);
    searchEndSpin->setMinimumWidth(inputColumnWidth);
    addFormRow(1, is_english_ ? QStringLiteral("Search End") : QStringLiteral("搜索终点"), searchEndSpin);

    auto *modeCombo = new QComboBox(formWidget);
    modeCombo->addItem(is_english_ ? QStringLiteral("Off") : QStringLiteral("关闭"), static_cast<int>(PeakFilterMode::None));
    modeCombo->addItem(is_english_ ? QStringLiteral("IQR Outlier Filter") : QStringLiteral("IQR 异常值过滤"), static_cast<int>(PeakFilterMode::IqrOutlier));
    modeCombo->addItem(is_english_ ? QStringLiteral("Keep Range") : QStringLiteral("保留区间"), static_cast<int>(PeakFilterMode::KeepRange));
    modeCombo->addItem(is_english_ ? QStringLiteral("Exclude Range") : QStringLiteral("排除区间"), static_cast<int>(PeakFilterMode::ExcludeRange));
    modeCombo->setCurrentIndex(std::max(0, modeCombo->findData(static_cast<int>(peak_filter_settings_.mode))));
    modeCombo->setMinimumWidth(inputColumnWidth);
    addFormRow(2, is_english_ ? QStringLiteral("Method") : QStringLiteral("方式"), modeCombo);

    auto *minEdit = new QLineEdit(QString::number(peak_filter_settings_.min_value, 'f', 6), formWidget);
    auto *maxEdit = new QLineEdit(QString::number(peak_filter_settings_.max_value, 'f', 6), formWidget);
    minEdit->setMinimumWidth(inputColumnWidth);
    maxEdit->setMinimumWidth(inputColumnWidth);
    addFormRow(3, is_english_ ? QStringLiteral("Range Min") : QStringLiteral("区间最小值"), minEdit);
    addFormRow(4, is_english_ ? QStringLiteral("Range Max") : QStringLiteral("区间最大值"), maxEdit);
    formLayout->setColumnMinimumWidth(0, labelColumnWidth);
    formLayout->setColumnMinimumWidth(1, inputColumnWidth);
    formLayout->setColumnStretch(1, 1);
    layout->addWidget(formWidget);

    auto *hintLabel = new QLabel(
        is_english_
            ? QStringLiteral("Peak search uses sample indexes [start, end). IQR removes statistical outliers. Keep Range keeps only values inside [min, max]. Exclude Range removes values inside [min, max]. If no peak remains after filtering, the plot shows no valid values.")
            : QStringLiteral("峰值搜索使用采样点下标 [起点, 终点)。IQR 会过滤统计异常值。保留区间只保留 [最小值, 最大值] 内的峰值。排除区间会过滤 [最小值, 最大值] 内的峰值。过滤后没有峰值时，趋势图显示无有效值。"),
        content);
    hintLabel->setWordWrap(true);
    hintLabel->setMinimumWidth(labelColumnWidth + inputColumnWidth + formLayout->horizontalSpacing());
    hintLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    layout->addWidget(hintLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, content);
    if (QPushButton *okButton = buttons->button(QDialogButtonBox::Ok))
    {
        okButton->setText(is_english_ ? QStringLiteral("OK") : QStringLiteral("确定"));
    }
    if (QPushButton *cancelButton = buttons->button(QDialogButtonBox::Cancel))
    {
        cancelButton->setText(is_english_ ? QStringLiteral("Cancel") : QStringLiteral("取消"));
    }
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    dialog.setMinimumSize(is_english_ ? QSize(520, 430) : QSize(500, 420));
    dialog.resize(dialog.minimumSize());
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
    if (searchEnd <= searchStart)
    {
        QMessageBox::warning(
            this,
            is_english_ ? QStringLiteral("Peak Settings") : QStringLiteral("峰值设置"),
            is_english_ ? QStringLiteral("Search End must be greater than Search Start.") : QStringLiteral("搜索终点必须大于搜索起点。"));
        return;
    }
    if ((mode == PeakFilterMode::KeepRange || mode == PeakFilterMode::ExcludeRange) && (!minOk || !maxOk))
    {
        QMessageBox::warning(
            this,
            is_english_ ? QStringLiteral("Peak Settings") : QStringLiteral("峰值设置"),
            is_english_ ? QStringLiteral("Please enter valid numeric range values.") : QStringLiteral("请输入有效的数值区间。"));
        return;
    }

    const bool peakSearchChanged =
        peak_search_start_index_ != searchStart ||
        peak_search_end_index_ != searchEnd;
    peak_search_start_index_ = searchStart;
    peak_search_end_index_ = searchEnd;
    peak_filter_settings_.mode = mode;
    if (minOk)
    {
        peak_filter_settings_.min_value = minValue;
    }
    if (maxOk)
    {
        peak_filter_settings_.max_value = maxValue;
    }

    QSettings settings("VaporView", "SessionViewer");
    QString modeKey = QStringLiteral("none");
    if (mode == PeakFilterMode::IqrOutlier)
    {
        modeKey = QStringLiteral("iqr");
    }
    else if (mode == PeakFilterMode::KeepRange)
    {
        modeKey = QStringLiteral("keep_range");
    }
    else if (mode == PeakFilterMode::ExcludeRange)
    {
        modeKey = QStringLiteral("exclude_range");
    }
    settings.setValue("peak_filter/mode", modeKey);
    settings.setValue("peak_filter/min_value", peak_filter_settings_.min_value);
    settings.setValue("peak_filter/max_value", peak_filter_settings_.max_value);
    settings.setValue("peak_search/start_index", peak_search_start_index_);
    settings.setValue("peak_search/end_index", peak_search_end_index_);

    updatePeakFilterButtonText();
    beginSessionLoading(peakSearchChanged
        ? (is_english_ ? "Recalculating waveform peak series..." : "正在重新计算波形峰值序列...")
        : (is_english_ ? "Applying peak filter..." : "正在应用峰值过滤..."));
    if (peakSearchChanged &&
        (!waveform_segments_.isEmpty() || !raw_tcp_wave_frames_.isEmpty() || !indexed_waveform_frames_.isEmpty()))
    {
        const bool loaded = loadWaveformPeakSeries();
        finishSessionLoading();
        if (!loaded)
        {
            return;
        }
    }
    else
    {
        applyPeakFilter();
        finishSessionLoading();
    }
}

bool SessionViewerWindow::readWaveformFrameSamples(quint64 frameIndex, quint64& timestampUs, QVector<float>& samples)
{
    timestampUs = 0;
    samples.clear();

    if (!raw_tcp_wave_frames_.isEmpty())
    {
        if (frameIndex >= static_cast<quint64>(raw_tcp_wave_frames_.size()))
        {
            return false;
        }

        const RawTcpWaveFrame& frame = raw_tcp_wave_frames_.at(static_cast<int>(frameIndex));
        QFile file(frame.filename);
        if (!file.open(QIODevice::ReadOnly) || !file.seek(static_cast<qint64>(frame.harmonic_payload_offset)))
        {
            setStatusText(QString(is_english_ ? "Failed to read raw TCP wave file: %1" : "读取 raw TCP 波形文件失败: %1").arg(frame.filename));
            return false;
        }

        const QByteArray payload = file.read(static_cast<qint64>(frame.harmonic_payload_size));
        if (payload.size() != static_cast<int>(frame.harmonic_payload_size) || payload.size() % static_cast<int>(kFloatBytes) != 0)
        {
            setStatusText(QString(is_english_ ? "Incomplete raw TCP wave frame in %1" : "%1 中的 raw TCP 波形帧不完整").arg(frame.filename));
            return false;
        }

        const VaporView::TcpFloatEncoding encoding = frame.float_encoding == VaporView::TcpFloatEncoding::Unknown
            ? VaporView::autoDetectTcpFloatEncoding(payload)
            : frame.float_encoding;
        samples = VaporView::decodeTcpFloatPayload(payload, encoding);
        timestampUs = frame.timestamp_us;
        return true;
    }

    if (!indexed_waveform_frames_.isEmpty())
    {
        if (frameIndex >= static_cast<quint64>(indexed_waveform_frames_.size()))
        {
            return false;
        }

        const IndexedWaveformFrame& frame = indexed_waveform_frames_.at(static_cast<int>(frameIndex));
        QFile file(frame.filename);
        if (!file.open(QIODevice::ReadOnly))
        {
            setStatusText(QString(is_english_ ? "Failed to read indexed waveform file: %1" : "读取索引波形文件失败: %1").arg(frame.filename));
            return false;
        }

        const quint64 sampleBytes = static_cast<quint64>(frame.point_count) * kFloatBytes;
        const QByteArray block = file.read(static_cast<qint64>(sampleBytes));
        if (block.size() != static_cast<int>(sampleBytes))
        {
            setStatusText(QString(is_english_ ? "Incomplete indexed waveform file: %1" : "索引波形文件不完整: %1").arg(frame.filename));
            return false;
        }

        samples.resize(static_cast<int>(frame.point_count));
        std::memcpy(samples.data(), block.constData(), static_cast<size_t>(sampleBytes));
        timestampUs = frame.timestamp_us;
        return true;
    }

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

    std::memcpy(&timestampUs, block.constData(), sizeof(quint64));
    samples.resize(points_per_frame_);
    std::memcpy(samples.data(), block.constData() + sizeof(quint64), static_cast<size_t>(points_per_frame_) * sizeof(float));
    return true;
}

bool SessionViewerWindow::previewWaveformFrame(quint64 frameIndex)
{
    quint64 timestampUs = 0;
    QVector<float> samples;
    if (!readWaveformFrameSamples(frameIndex, timestampUs, samples))
    {
        return false;
    }

    current_waveform_frame_samples_ = samples;
    int firstSampleIndex = 0;
    static_cast<SessionWavePlotWidget*>(waveform_plot_)->setSamples(visibleWaveformSamples(samples, firstSampleIndex), firstSampleIndex);
    static_cast<SessionPeakPlotWidget*>(waveform_peak_plot_)->setCurrentFrame(static_cast<int>(frameIndex));
    const int previewCsvRow = timestampUs == 0 ? -1 : findClosestCsvRow(timestampUs);
    static_cast<SingleSeriesTrendPlotWidget*>(temperature_plot_)->setCurrentIndex(previewCsvRow);
    static_cast<SingleSeriesTrendPlotWidget*>(humidity_plot_)->setCurrentIndex(previewCsvRow);
    static_cast<SingleSeriesTrendPlotWidget*>(pressure_plot_)->setCurrentIndex(previewCsvRow);
    previewClosestSensorRow(timestampUs);
    const int frameDigits = std::max(1, static_cast<int>(QString::number(total_waveform_frames_).size()));
    frame_info_label_->setText(QString(is_english_
        ? "Previewing frame %1 / %2. Release the slider to sync CSV and details."
        : "正在预览第 %1 / %2 帧。松开滑块后同步 CSV 和详细信息。")
        .arg(fixedIntegerField(frameIndex + 1, frameDigits))
        .arg(fixedIntegerField(total_waveform_frames_, frameDigits)));
    return true;
}

bool SessionViewerWindow::loadWaveformFrame(quint64 frameIndex, bool scrollToCsvRow)
{
    quint64 timestampUs = 0;
    QVector<float> samples;
    if (!readWaveformFrameSamples(frameIndex, timestampUs, samples))
    {
        return false;
    }

    current_waveform_frame_samples_ = samples;
    int firstSampleIndex = 0;
    static_cast<SessionWavePlotWidget*>(waveform_plot_)->setSamples(visibleWaveformSamples(samples, firstSampleIndex), firstSampleIndex);
    static_cast<SessionPeakPlotWidget*>(waveform_peak_plot_)->setCurrentFrame(static_cast<int>(frameIndex));

    const auto minMax = std::minmax_element(samples.cbegin(), samples.cend());
    const float rawPeakValue = frameIndex < static_cast<quint64>(waveform_peak_raw_values_.size())
        ? waveform_peak_raw_values_.at(static_cast<int>(frameIndex))
        : waveformPeakValue(samples, peak_search_start_index_, peak_search_end_index_);
    const float filteredPeakValue = frameIndex < static_cast<quint64>(waveform_peak_values_.size())
        ? waveform_peak_values_.at(static_cast<int>(frameIndex))
        : rawPeakValue;
    const QString frameTime = formatTimestampUs(timestampUs);
    const QString csvMatchText = highlightClosestSensorRow(timestampUs, scrollToCsvRow);
    QString sourceFilename = raw_tcp_wave_frames_.isEmpty() ? QString() : raw_tcp_wave_filename_;
    if (sourceFilename.isEmpty() && frameIndex < static_cast<quint64>(indexed_waveform_frames_.size()))
    {
        sourceFilename = indexed_waveform_frames_.at(static_cast<int>(frameIndex)).filename;
    }
    if (sourceFilename.isEmpty())
    {
        const auto sourceIt = std::find_if(waveform_segments_.cbegin(), waveform_segments_.cend(), [frameIndex](const WaveformSegment& segment) {
            return frameIndex >= segment.start_frame && frameIndex < segment.start_frame + segment.frame_count;
        });
        if (sourceIt != waveform_segments_.cend())
        {
            sourceFilename = sourceIt->filename;
        }
    }
    const QString waveformExportText = (waveform_export_mode_ == QStringLiteral("per_frame") || waveform_export_rate_hz_ <= 0)
        ? (is_english_ ? QStringLiteral("per-frame export") : QStringLiteral("逐帧导出"))
        : QString(is_english_ ? "%1 Hz export" : "%1 Hz 导出").arg(fixedDecimalField(waveform_export_rate_hz_, 2, 8));
    const QString peakText = std::isfinite(filteredPeakValue)
        ? fixedSignedDecimalField(filteredPeakValue, 6, 14)
        : fixedTextField(is_english_ ? QStringLiteral("No valid value") : QStringLiteral("无有效值"), 14, Qt::AlignLeft);
    const int frameDigits = std::max(1, static_cast<int>(QString::number(total_waveform_frames_).size()));
    frame_info_label_->setText(QString(is_english_
        ? "Frame %1 / %2 | %3 | %4 | min=%5 max=%6 peak=%7 | %8"
        : "第 %1 / %2 帧 | %3 | %4 | min=%5 max=%6 峰值=%7 | %8")
        .arg(fixedIntegerField(frameIndex + 1, frameDigits))
        .arg(fixedIntegerField(total_waveform_frames_, frameDigits))
        .arg(frameTime)
        .arg(waveformExportText)
        .arg(fixedSignedDecimalField(*minMax.first, 6, 14))
        .arg(fixedSignedDecimalField(*minMax.second, 6, 14))
        .arg(peakText)
        .arg(QFileInfo(sourceFilename).fileName())
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

void SessionViewerWindow::updateRtkTrackPeakValues()
{
    for (RtkTrackPoint& point : rtk_track_points_)
    {
        point.peak_value = 0.0f;
        point.has_peak_value = false;

        if (point.timestamp_us == 0)
        {
            continue;
        }

        const int peakIndex = findClosestTimestampIndex(waveform_timestamps_us_, point.timestamp_us);
        if (peakIndex < 0 || peakIndex >= waveform_peak_values_.size())
        {
            continue;
        }

        const float peakValue = waveform_peak_values_.at(peakIndex);
        if (!std::isfinite(peakValue))
        {
            continue;
        }

        point.peak_value = peakValue;
        point.has_peak_value = true;
    }

    if (trajectory_viewer_dialog_)
    {
        trajectory_viewer_dialog_->setTrackPoints(rtk_track_points_);
    }
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

void SessionViewerWindow::previewClosestSensorRow(quint64 timestampUs)
{
    highlightClosestSensorRow(timestampUs, false);
}

QString SessionViewerWindow::highlightClosestSensorRow(quint64 timestampUs, bool scrollToCsvRow)
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

    const SessionTableTheme tableTheme = sessionTableThemeFor(this);
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
                item->setBackground(tableTheme.background);
                item->setForeground(tableTheme.text);
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
        const QColor rowColor = (row == primaryRow) ? tableTheme.highlightedBackground : tableTheme.secondaryHighlightedBackground;
        for (int col = 0; col < csv_table_->columnCount(); ++col)
        {
            if (QTableWidgetItem *item = csv_table_->item(row, col))
            {
                item->setBackground(rowColor);
                item->setForeground(tableTheme.text);
            }
        }

        const qint64 deltaUs = static_cast<qint64>(csv_timestamps_us_.at(row)) - static_cast<qint64>(timestampUs);
        if (QTableWidgetItem *deltaItem = csv_table_->item(row, 1))
        {
            deltaItem->setText(formatSignedDeltaMs(deltaUs));
        }
        const QString rowText = fixedIntegerField(row + 1, 8);
        const QString deltaText = fixedTextField(formatSignedDeltaMs(deltaUs), 12);
        matchParts.append(is_english_
            ? QString("CSV row %1 (%2)").arg(rowText, deltaText)
            : QString("CSV 第%1行（%2）").arg(rowText, deltaText));
    }

    highlighted_csv_rows_ = rowsToHighlight;
    if (primaryRow >= 0)
    {
        static_cast<SingleSeriesTrendPlotWidget*>(temperature_plot_)->setCurrentIndex(primaryRow);
        static_cast<SingleSeriesTrendPlotWidget*>(humidity_plot_)->setCurrentIndex(primaryRow);
        static_cast<SingleSeriesTrendPlotWidget*>(pressure_plot_)->setCurrentIndex(primaryRow);
        if (scrollToCsvRow)
        {
            const int topVisibleRow = rowsToHighlight.isEmpty() ? primaryRow : rowsToHighlight.first();
            if (QTableWidgetItem *item = csv_table_->item(topVisibleRow, 0))
            {
                csv_table_->scrollToItem(item, QAbstractItemView::PositionAtTop);
            }
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
            .arg(formatOptionalSeriesValueFixed(primaryRow < temperature_values_.size() ? temperature_values_.at(primaryRow) : std::numeric_limits<double>::quiet_NaN(), 2, 8, QStringLiteral("°C")))
            .arg(formatOptionalSeriesValueFixed(primaryRow < humidity_values_.size() ? humidity_values_.at(primaryRow) : std::numeric_limits<double>::quiet_NaN(), 2, 8, QStringLiteral("%RH")))
            .arg(formatOptionalSeriesValueFixed(primaryRow < pressure_values_.size() ? pressure_values_.at(primaryRow) : std::numeric_limits<double>::quiet_NaN(), 2, 9, QStringLiteral("hPa")))
            .arg(fixedIntegerField(primaryRow + 1, 8)));
    }

    return matchParts.join(is_english_ ? " | " : " | ");
}
