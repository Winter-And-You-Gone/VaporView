#include "AppTheme.h"
#include "SessionViewerWindow.h"
#include "CustomTitleBar.h"
#include "RangeSelectionAxisWidget.h"
#include "RawDataParserWindow.h"
#include "SessionTimeFormat.h"
#include "TrajectoryViewerDialog.h"
#include "WindowSizing.h"

#include <QByteArray>
#include <QAbstractTableModel>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QProgressBar>
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
#include <QSaveFile>
#include <QTableView>
#include <QTextStream>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <QWidget>
#include <QStringConverter>
#include <QThread>
#include <QTimeZone>
#include <QtEndian>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <future>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

using VaporView::AppThemeColor;
using VaporView::appThemeColor;

struct WaveformPeakSeriesResult
{
    bool loaded = false;
    QString error;
    QVector<quint64> timestamps_us;
    QVector<float> peak_values;
};

namespace
{
constexpr quint64 kWaveformTimestampBytes = sizeof(quint64);
constexpr quint64 kFloatBytes = sizeof(float);
constexpr int kSessionViewerPlotHeight = 120;
constexpr int kSessionViewerPlotLeftMargin = 64;
constexpr int kSessionViewerTrendPlotLeftPadding = 8;
constexpr int kSessionViewerPlotRightMargin = 10;
constexpr int kSessionViewerPlotTopMargin = 12;
constexpr int kSessionViewerPlotBottomMargin = 28;
constexpr int kSessionViewerWaveBottomMargin = 30;
constexpr int kDefaultPeakSearchStartIndex = 0;
constexpr int kDefaultPeakSearchEndIndex = 0;
constexpr int kSessionViewerDefaultWidth = 1280;
constexpr int kSessionViewerDefaultHeight = 800;
constexpr int kMaxTrendPointsPerPixel = 2;
constexpr qsizetype kPeakPayloadChunkBytes = 32 * 1024 * 1024;
constexpr char kUnifiedRawMagic[8] = {'V', 'V', 'R', 'A', 'W', 'D', 'A', 'T'};
constexpr quint32 kUnifiedRawRecordMarker = 0x44525756u;
constexpr quint16 kRawSourceTcpWave = 5u;
constexpr quint32 kRawTcpWaveCombinedPayloadFlag = 0x00000001u;

qint64 monotonicMilliseconds()
{
    static QElapsedTimer timer = []() {
        QElapsedTimer initialized;
        initialized.start();
        return initialized;
    }();
    return timer.elapsed();
}

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

int rangedProgressPercent(quint64 done, quint64 total, int startPercent, int endPercent)
{
    if (total == 0)
    {
        return std::clamp(startPercent, 0, 100);
    }
    const double ratio = std::clamp(static_cast<double>(done) / static_cast<double>(total), 0.0, 1.0);
    const int value = startPercent + static_cast<int>(std::lround(ratio * (endPercent - startPercent)));
    return std::clamp(value, 0, 100);
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
    QColor highlightedText;
    QColor secondaryHighlightedBackground;
    QColor secondaryHighlightedText;
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

struct WaveformPeakPayload
{
    quint64 timestamp_us = 0;
    QByteArray payload;
    VaporView::TcpFloatEncoding encoding = VaporView::TcpFloatEncoding::LittleEndian;
};

struct WaveformPeakResult
{
    quint64 timestamp_us = 0;
    float peak_value = std::numeric_limits<float>::quiet_NaN();
};

struct BackgroundRawTcpWaveFrame
{
    QString filename;
    quint64 harmonic_payload_offset = 0;
    quint32 harmonic_payload_size = 0;
    quint64 timestamp_us = 0;
    VaporView::TcpFloatEncoding float_encoding = VaporView::TcpFloatEncoding::Unknown;
};

struct BackgroundIndexedWaveformFrame
{
    QString filename;
    quint64 timestamp_us = 0;
    quint32 point_count = 0;
};

struct BackgroundWaveformSegment
{
    QString filename;
    quint64 frame_count = 0;
};

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

int dataPlotLeftMargin(const QFontMetrics& fm,
                       const QString& maxLabel = QString(),
                       const QString& midLabel = QString(),
                       const QString& minLabel = QString())
{
    int labelWidth = fm.horizontalAdvance(formatGuideValue(9999.999, 3));
    if (!maxLabel.isEmpty())
    {
        labelWidth = std::max(labelWidth, fm.horizontalAdvance(maxLabel));
    }
    if (!midLabel.isEmpty())
    {
        labelWidth = std::max(labelWidth, fm.horizontalAdvance(midLabel));
    }
    if (!minLabel.isEmpty())
    {
        labelWidth = std::max(labelWidth, fm.horizontalAdvance(minLabel));
    }
    return std::max(kSessionViewerPlotLeftMargin, labelWidth + kSessionViewerTrendPlotLeftPadding);
}

float waveformPeakValue(const float* samples, int sampleCount, int searchStartIndex, int searchEndIndex)
{
    if (!samples || sampleCount <= 0)
    {
        return std::numeric_limits<float>::quiet_NaN();
    }

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

bool isFullFramePeakSearch(int searchStartIndex, int searchEndIndex)
{
    return searchStartIndex == 0 && searchEndIndex <= 0;
}

std::pair<int, int> waveformPeakSearchRange(int sampleCount, int searchStartIndex, int searchEndIndex)
{
    if (sampleCount <= 0)
    {
        return {0, 0};
    }

    const int startIndex = std::clamp(searchStartIndex, 0, sampleCount);
    const int endIndex = searchEndIndex <= 0
        ? sampleCount
        : std::clamp(searchEndIndex, 0, sampleCount);
    return {startIndex, std::max(startIndex, endIndex)};
}

bool readWaveformPeakPayload(QFile& file,
                             quint64 samplePayloadOffset,
                             int sampleCount,
                             int searchStartIndex,
                             int searchEndIndex,
                             QByteArray& payload)
{
    payload.clear();
    const auto [startIndex, endIndex] = waveformPeakSearchRange(sampleCount, searchStartIndex, searchEndIndex);
    if (startIndex >= endIndex)
    {
        return true;
    }

    const quint64 byteOffset = samplePayloadOffset + static_cast<quint64>(startIndex) * kFloatBytes;
    const quint64 byteCount = static_cast<quint64>(endIndex - startIndex) * kFloatBytes;
    if (byteOffset > static_cast<quint64>(std::numeric_limits<qint64>::max()) ||
        byteCount > static_cast<quint64>(std::numeric_limits<qint64>::max()))
    {
        return false;
    }
    if (!file.seek(static_cast<qint64>(byteOffset)))
    {
        return false;
    }

    payload = file.read(static_cast<qint64>(byteCount));
    return payload.size() == static_cast<qsizetype>(byteCount);
}

float waveformPeakValueFromPayload(const QByteArray& payload, VaporView::TcpFloatEncoding encoding)
{
    const int sampleCount = static_cast<int>(payload.size() / static_cast<int>(kFloatBytes));
    if (sampleCount <= 0 || payload.size() % static_cast<int>(kFloatBytes) != 0)
    {
        return std::numeric_limits<float>::quiet_NaN();
    }

    const VaporView::TcpFloatEncoding effectiveEncoding = encoding == VaporView::TcpFloatEncoding::Unknown
        ? VaporView::autoDetectTcpFloatEncoding(payload)
        : encoding;
    bool hasPeak = false;
    float peakValue = std::numeric_limits<float>::lowest();
    const char *samples = payload.constData();
    for (int index = 0; index < sampleCount; ++index)
    {
        const float value = VaporView::decodeTcpFloatSample(samples + index * static_cast<int>(kFloatBytes), effectiveEncoding);
        if (!std::isfinite(value))
        {
            continue;
        }
        hasPeak = true;
        peakValue = std::max(peakValue, value);
    }
    return hasPeak ? peakValue : std::numeric_limits<float>::quiet_NaN();
}

bool isWaveformPeakCalculationCancelled(const std::shared_ptr<std::atomic_bool>& cancelFlag)
{
    return cancelFlag && cancelFlag->load(std::memory_order_relaxed);
}

QVector<WaveformPeakResult> computeWaveformPeakChunk(const QVector<WaveformPeakPayload>& payloads,
                                                     const std::shared_ptr<std::atomic_bool>& cancelFlag = {})
{
    QVector<WaveformPeakResult> results(payloads.size());
    if (payloads.isEmpty() || isWaveformPeakCalculationCancelled(cancelFlag))
    {
        return results;
    }

    const int desiredThreads = std::max(1, QThread::idealThreadCount());
    const int workerCount = std::clamp(desiredThreads, 1, static_cast<int>(payloads.size()));
    const int blockSize = (static_cast<int>(payloads.size()) + workerCount - 1) / workerCount;

    std::vector<std::future<void>> futures;
    futures.reserve(static_cast<size_t>(workerCount));
    for (int worker = 0; worker < workerCount; ++worker)
    {
        const int begin = worker * blockSize;
        const int end = std::min(static_cast<int>(payloads.size()), begin + blockSize);
        if (begin >= end)
        {
            continue;
        }

        futures.emplace_back(std::async(std::launch::async, [&payloads, &results, begin, end, cancelFlag]() {
            for (int index = begin; index < end; ++index)
            {
                if (isWaveformPeakCalculationCancelled(cancelFlag))
                {
                    return;
                }
                const WaveformPeakPayload& payload = payloads.at(index);
                WaveformPeakResult result;
                result.timestamp_us = payload.timestamp_us;
                result.peak_value = waveformPeakValueFromPayload(payload.payload, payload.encoding);
                results[index] = result;
            }
        }));
    }

    for (std::future<void>& future : futures)
    {
        future.get();
    }
    return results;
}

void appendWaveformPeakResults(const QVector<WaveformPeakPayload>& payloads,
                               QVector<quint64>& timestampsUs,
                               QVector<float>& peakValues,
                               const std::shared_ptr<std::atomic_bool>& cancelFlag = {})
{
    if (isWaveformPeakCalculationCancelled(cancelFlag))
    {
        return;
    }
    const QVector<WaveformPeakResult> results = computeWaveformPeakChunk(payloads, cancelFlag);
    if (isWaveformPeakCalculationCancelled(cancelFlag))
    {
        return;
    }
    timestampsUs.reserve(timestampsUs.size() + results.size());
    peakValues.reserve(peakValues.size() + results.size());
    for (const WaveformPeakResult& result : results)
    {
        timestampsUs.push_back(result.timestamp_us);
        peakValues.push_back(result.peak_value);
    }
}

WaveformPeakSeriesResult calculateRawTcpWavePeakSeries(QVector<BackgroundRawTcpWaveFrame> frames,
                                                       int searchStartIndex,
                                                       int searchEndIndex,
                                                       std::shared_ptr<std::atomic_bool> cancelFlag = {})
{
    WaveformPeakSeriesResult result;
    result.loaded = true;
    if (frames.isEmpty() || isWaveformPeakCalculationCancelled(cancelFlag))
    {
        return result;
    }

    QFile file;
    QString openFilename;
    QVector<WaveformPeakPayload> payloads;
    qsizetype chunkBytes = 0;

    auto flushChunk = [&]() {
        if (payloads.isEmpty())
        {
            return;
        }
        appendWaveformPeakResults(payloads, result.timestamps_us, result.peak_values, cancelFlag);
        payloads.clear();
        chunkBytes = 0;
    };

    for (const BackgroundRawTcpWaveFrame& frame : frames)
    {
        if (isWaveformPeakCalculationCancelled(cancelFlag))
        {
            result.loaded = false;
            return result;
        }
        if (openFilename != frame.filename)
        {
            file.close();
            file.setFileName(frame.filename);
            if (!file.open(QIODevice::ReadOnly))
            {
                result.loaded = false;
                result.error = frame.filename;
                return result;
            }
            openFilename = frame.filename;
        }

        const quint64 sampleCount64 = frame.harmonic_payload_size / kFloatBytes;
        const int sampleCount = static_cast<int>(std::min<quint64>(
            sampleCount64,
            static_cast<quint64>(std::numeric_limits<int>::max())));
        QByteArray payload;
        if (!readWaveformPeakPayload(file,
                                     frame.harmonic_payload_offset,
                                     sampleCount,
                                     searchStartIndex,
                                     searchEndIndex,
                                     payload))
        {
            result.loaded = false;
            result.error = frame.filename;
            return result;
        }

        WaveformPeakPayload peakPayload;
        peakPayload.timestamp_us = frame.timestamp_us;
        peakPayload.payload = std::move(payload);
        peakPayload.encoding = frame.float_encoding;
        chunkBytes += peakPayload.payload.size();
        payloads.push_back(std::move(peakPayload));
        if (chunkBytes >= kPeakPayloadChunkBytes)
        {
            flushChunk();
        }
    }

    flushChunk();
    return result;
}

WaveformPeakSeriesResult calculateIndexedWaveformPeakSeries(QVector<BackgroundIndexedWaveformFrame> frames,
                                                            int searchStartIndex,
                                                            int searchEndIndex,
                                                            std::shared_ptr<std::atomic_bool> cancelFlag = {})
{
    WaveformPeakSeriesResult result;
    result.loaded = true;
    if (frames.isEmpty() || isWaveformPeakCalculationCancelled(cancelFlag))
    {
        return result;
    }

    QVector<WaveformPeakPayload> payloads;
    qsizetype chunkBytes = 0;

    auto flushChunk = [&]() {
        if (payloads.isEmpty())
        {
            return;
        }
        appendWaveformPeakResults(payloads, result.timestamps_us, result.peak_values, cancelFlag);
        payloads.clear();
        chunkBytes = 0;
    };

    for (const BackgroundIndexedWaveformFrame& frame : frames)
    {
        if (isWaveformPeakCalculationCancelled(cancelFlag))
        {
            result.loaded = false;
            return result;
        }
        QFile file(frame.filename);
        if (!file.open(QIODevice::ReadOnly))
        {
            result.loaded = false;
            result.error = frame.filename;
            return result;
        }

        QByteArray payload;
        if (!readWaveformPeakPayload(file,
                                     0,
                                     static_cast<int>(std::min<quint64>(frame.point_count, static_cast<quint64>(std::numeric_limits<int>::max()))),
                                     searchStartIndex,
                                     searchEndIndex,
                                     payload))
        {
            result.loaded = false;
            result.error = frame.filename;
            return result;
        }

        WaveformPeakPayload peakPayload;
        peakPayload.timestamp_us = frame.timestamp_us;
        peakPayload.payload = std::move(payload);
        peakPayload.encoding = VaporView::TcpFloatEncoding::LittleEndian;
        chunkBytes += peakPayload.payload.size();
        payloads.push_back(std::move(peakPayload));
        if (chunkBytes >= kPeakPayloadChunkBytes)
        {
            flushChunk();
        }
    }

    flushChunk();
    return result;
}

WaveformPeakSeriesResult calculateLegacyWaveformPeakSeries(QVector<BackgroundWaveformSegment> segments,
                                                           int pointsPerFrame,
                                                           int searchStartIndex,
                                                           int searchEndIndex,
                                                           std::shared_ptr<std::atomic_bool> cancelFlag = {})
{
    WaveformPeakSeriesResult result;
    result.loaded = true;
    if (segments.isEmpty() || pointsPerFrame <= 0 || isWaveformPeakCalculationCancelled(cancelFlag))
    {
        return result;
    }

    QVector<WaveformPeakPayload> payloads;
    qsizetype chunkBytes = 0;
    const quint64 frameBytes = kWaveformTimestampBytes + static_cast<quint64>(pointsPerFrame) * kFloatBytes;

    auto flushChunk = [&]() {
        if (payloads.isEmpty())
        {
            return;
        }
        appendWaveformPeakResults(payloads, result.timestamps_us, result.peak_values, cancelFlag);
        payloads.clear();
        chunkBytes = 0;
    };

    for (const BackgroundWaveformSegment& segment : segments)
    {
        if (isWaveformPeakCalculationCancelled(cancelFlag))
        {
            result.loaded = false;
            return result;
        }
        QFile file(segment.filename);
        if (!file.open(QIODevice::ReadOnly))
        {
            result.loaded = false;
            result.error = segment.filename;
            return result;
        }

        for (quint64 localFrame = 0; localFrame < segment.frame_count; ++localFrame)
        {
            if (isWaveformPeakCalculationCancelled(cancelFlag))
            {
                result.loaded = false;
                return result;
            }
            const quint64 frameOffset = localFrame * frameBytes;
            if (frameOffset > static_cast<quint64>(std::numeric_limits<qint64>::max()) ||
                !file.seek(static_cast<qint64>(frameOffset)))
            {
                result.loaded = false;
                result.error = segment.filename;
                return result;
            }

            quint64 timestampLe = 0;
            if (file.read(reinterpret_cast<char*>(&timestampLe), sizeof(timestampLe)) != static_cast<qint64>(sizeof(timestampLe)))
            {
                result.loaded = false;
                result.error = segment.filename;
                return result;
            }

            QByteArray payload;
            if (!readWaveformPeakPayload(file,
                                         frameOffset + kWaveformTimestampBytes,
                                         pointsPerFrame,
                                         searchStartIndex,
                                         searchEndIndex,
                                         payload))
            {
                result.loaded = false;
                result.error = segment.filename;
                return result;
            }

            WaveformPeakPayload peakPayload;
            peakPayload.timestamp_us = qFromLittleEndian(timestampLe);
            peakPayload.payload = std::move(payload);
            peakPayload.encoding = VaporView::TcpFloatEncoding::LittleEndian;
            chunkBytes += peakPayload.payload.size();
            payloads.push_back(std::move(peakPayload));
            if (chunkBytes >= kPeakPayloadChunkBytes)
            {
                flushChunk();
            }
        }
    }

    flushChunk();
    return result;
}

quint64 midpointTimestamp(quint64 first, quint64 second)
{
    return first <= second
        ? first + (second - first) / 2ULL
        : second + (first - second) / 2ULL;
}

quint64 addClampedUs(quint64 timestampUs, quint64 deltaUs)
{
    const quint64 maxValue = std::numeric_limits<quint64>::max();
    return deltaUs > maxValue - timestampUs ? maxValue : timestampUs + deltaUs;
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
}

class SessionCsvTableModel : public QAbstractTableModel
{
public:
    explicit SessionCsvTableModel(QObject *parent = nullptr)
        : QAbstractTableModel(parent)
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
    , loading_dialog_label_(nullptr)
    , loading_dialog_progress_bar_(nullptr)
    , loading_dialog_progress_percent_(0)
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
    , csv_model_(nullptr)
    , session_directory_()
    , metadata_filename_()
    , sensors_csv_filename_()
    , waveform_directory_()
    , waveform_index_filename_()
    , waveform_peak_index_filename_()
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
    , rtk_track_stats_()
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
    , peak_series_request_id_(0)
    , peak_series_watcher_(nullptr)
    , peak_series_cancel_flag_(nullptr)
    , highlighted_csv_rows_()
    , primary_highlighted_csv_row_(-1)
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
    resize(kSessionViewerDefaultWidth, kSessionViewerDefaultHeight);
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
    peak_search_end_index_ = std::max(0, settings.value("peak_search/end_index", kDefaultPeakSearchEndIndex).toInt());
    if (peak_search_end_index_ > 0 && peak_search_end_index_ <= peak_search_start_index_)
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
    cancelBackgroundWaveformPeakSeries(false);
    if (trajectory_viewer_dialog_)
    {
        trajectory_viewer_dialog_->close();
        trajectory_viewer_dialog_->disconnect(this);
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

    central_widget_ = new QWidget(scrollArea);
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

    auto *pathTitle = new QLabel(central_widget_);
    pathTitle->setObjectName("fieldLabel");
    pathTitle->setText(tr("Session:"));
    controlLayout->addWidget(pathTitle, 0, 0);

    session_path_edit_ = new QLineEdit(central_widget_);
    session_path_edit_->setReadOnly(true);
    controlLayout->addWidget(session_path_edit_, 0, 1);

    choose_session_btn_ = new QPushButton(central_widget_);
    connect(choose_session_btn_, &QPushButton::clicked, this, &SessionViewerWindow::onChooseSessionClicked);
    controlLayout->addWidget(choose_session_btn_, 0, 2);

    reload_btn_ = new QPushButton(central_widget_);
    connect(reload_btn_, &QPushButton::clicked, this, &SessionViewerWindow::onReloadClicked);
    controlLayout->addWidget(reload_btn_, 0, 3);

    trajectory_view_btn_ = new QPushButton(central_widget_);
    trajectory_view_btn_->setEnabled(false);
    connect(trajectory_view_btn_, &QPushButton::clicked, this, &SessionViewerWindow::onViewTrajectoryClicked);
    controlLayout->addWidget(trajectory_view_btn_, 0, 4);

    raw_data_parser_btn_ = new QPushButton(central_widget_);
    connect(raw_data_parser_btn_, &QPushButton::clicked, this, &SessionViewerWindow::onRawDataParserClicked);
    controlLayout->addWidget(raw_data_parser_btn_, 0, 5);

    clear_view_btn_ = new QPushButton(central_widget_);
    connect(clear_view_btn_, &QPushButton::clicked, this, &SessionViewerWindow::onClearViewClicked);
    controlLayout->addWidget(clear_view_btn_, 0, 6);

    status_label_ = new QLabel(central_widget_);
    status_label_->setWordWrap(true);
    status_label_->setFocusPolicy(Qt::StrongFocus);
    controlLayout->addWidget(status_label_, 1, 0, 1, 7);

    mainLayout->addLayout(controlLayout);

    auto *summaryWaveSplitter = new QSplitter(Qt::Vertical, central_widget_);
    summaryWaveSplitter->setObjectName("sessionViewerContentSplitter");
    summaryWaveSplitter->setAttribute(Qt::WA_StyledBackground, true);
    summaryWaveSplitter->setAutoFillBackground(true);

    auto *upperWidget = new QWidget(summaryWaveSplitter);
    upperWidget->setObjectName("sessionViewerContentPane");
    upperWidget->setAttribute(Qt::WA_StyledBackground, true);
    upperWidget->setAutoFillBackground(true);
    auto *upperLayout = new QVBoxLayout(upperWidget);
    upperLayout->setContentsMargins(0, 0, 0, 0);
    upperLayout->setSpacing(8);

    summary_group_ = new QGroupBox(upperWidget);
    summary_group_->setObjectName("sensorGroupBox");
    summary_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    summary_layout_ = new QGridLayout(summary_group_);
    summary_layout_->setContentsMargins(8, 28, 8, 8);
    summary_layout_->setHorizontalSpacing(8);
    summary_layout_->setVerticalSpacing(4);

    auto createSummaryRow = [this](QLabel*& title, QLabel*& value) {
        title = new QLabel(summary_group_);
        title->setObjectName("fieldLabel");
        title->setMinimumWidth(64);
        title->setMaximumWidth(156);
        title->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        value = new QLabel("---", summary_group_);
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

    waveform_group_ = new QGroupBox(upperWidget);
    waveform_group_->setObjectName("sensorGroupBox");
    auto *waveformLayout = new QVBoxLayout(waveform_group_);
    waveformLayout->setContentsMargins(10, 30, 10, 10);
    waveformLayout->setSpacing(6);

    auto *frameLayout = new QGridLayout();
    frameLayout->setHorizontalSpacing(8);
    frameLayout->setVerticalSpacing(4);

    frame_title_ = new QLabel(waveform_group_);
    frame_title_->setObjectName("fieldLabel");
    frameLayout->addWidget(frame_title_, 0, 0);

    frame_slider_ = new QSlider(Qt::Horizontal, waveform_group_);
    frame_slider_->setEnabled(false);
    frame_slider_->setTracking(false);
    connect(frame_slider_, &QSlider::sliderMoved, this, &SessionViewerWindow::onFrameSliderMoved);
    connect(frame_slider_, &QSlider::valueChanged, this, &SessionViewerWindow::onFrameSliderChanged);
    frameLayout->addWidget(frame_slider_, 0, 1);

    frame_spin_ = new QSpinBox(waveform_group_);
    frame_spin_->setRange(0, 0);
    frame_spin_->setEnabled(false);
    connect(frame_spin_, &QSpinBox::valueChanged, this, &SessionViewerWindow::onFrameSpinChanged);
    frameLayout->addWidget(frame_spin_, 0, 2);

    frame_total_label_ = new QLabel("---", waveform_group_);
    frame_total_label_->setFont(numericFontFrom(frame_total_label_->font()));
    frame_total_label_->setFixedWidth(QFontMetrics(frame_total_label_->font()).horizontalAdvance(QStringLiteral("/ 999999999")) + 8);
    frameLayout->addWidget(frame_total_label_, 0, 3);

    frame_info_label_ = new QLabel(waveform_group_);
    frame_info_label_->setFont(numericFontFrom(frame_info_label_->font()));
    frame_info_label_->setWordWrap(true);
    frameLayout->addWidget(frame_info_label_, 1, 0, 1, 4);

    waveformLayout->addLayout(frameLayout);

    waveform_plot_title_ = new QLabel(waveform_group_);
    waveform_plot_title_->setObjectName("fieldLabel");
    waveformLayout->addWidget(waveform_plot_title_);

    waveform_plot_ = new SessionWavePlotWidget(waveform_group_);
    waveform_plot_->setObjectName(QStringLiteral("sessionViewerWaveformPlot"));
    waveformLayout->addWidget(waveform_plot_, 1);

    auto *peakHeaderLayout = new QHBoxLayout();
    peakHeaderLayout->setContentsMargins(0, 0, 0, 0);
    peakHeaderLayout->setSpacing(8);
    waveform_peak_plot_title_ = new QLabel(waveform_group_);
    waveform_peak_plot_title_->setObjectName("fieldLabel");
    peakHeaderLayout->addWidget(waveform_peak_plot_title_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    auto *waveformPeakRangeAxis = new RangeSelectionAxisWidget(waveform_group_);
    waveformPeakRangeAxis->setCompactMode(true);
    waveformPeakRangeAxis->setMinimumWidth(240);
    peakHeaderLayout->addWidget(waveformPeakRangeAxis, 1, Qt::AlignVCenter);
    waveform_frame_filter_btn_ = new QPushButton(waveform_group_);
    peakHeaderLayout->addWidget(waveform_frame_filter_btn_, 0, Qt::AlignVCenter | Qt::AlignRight);
    waveform_peak_filter_btn_ = new QPushButton(waveform_group_);
    peakHeaderLayout->addWidget(waveform_peak_filter_btn_, 0, Qt::AlignVCenter | Qt::AlignRight);
    waveform_peak_mode_btn_ = new QPushButton(waveform_group_);
    peakHeaderLayout->addWidget(waveform_peak_mode_btn_, 0, Qt::AlignVCenter | Qt::AlignRight);
    waveformLayout->addLayout(peakHeaderLayout);

    waveform_peak_plot_ = new SessionPeakPlotWidget(waveform_group_);
    waveform_peak_plot_->setObjectName(QStringLiteral("sessionViewerPeakPlot"));
    static_cast<SessionPeakPlotWidget*>(waveform_peak_plot_)->setPlotMode(
        waveform_peak_scatter_mode_ ? SessionPeakPlotWidget::PlotMode::Scatter : SessionPeakPlotWidget::PlotMode::Polyline);
    connect(waveform_frame_filter_btn_, &QPushButton::clicked, this, &SessionViewerWindow::onToggleWaveformFrameFilterClicked);
    connect(waveform_peak_filter_btn_, &QPushButton::clicked, this, &SessionViewerWindow::onConfigurePeakFilterClicked);
    connect(waveform_peak_mode_btn_, &QPushButton::clicked, this, &SessionViewerWindow::onTogglePeakPlotModeClicked);
    waveformLayout->addWidget(waveform_peak_plot_, 1);

    temperature_plot_title_ = new QLabel(waveform_group_);
    temperature_plot_title_->setObjectName("fieldLabel");
    waveformLayout->addWidget(temperature_plot_title_);
    temperature_plot_ = new SingleSeriesTrendPlotWidget(appThemeColor(AppThemeColor::PlotSeriesTemperature, false),
        is_english_ ? "No temperature series" : "没有温度趋势数据",
        QStringLiteral("°C"),
        waveform_group_);
    temperature_plot_->setObjectName(QStringLiteral("sessionViewerTemperaturePlot"));
    static_cast<SingleSeriesTrendPlotWidget*>(temperature_plot_)->setPlotMode(
        waveform_peak_scatter_mode_ ? SingleSeriesTrendPlotWidget::PlotMode::Scatter : SingleSeriesTrendPlotWidget::PlotMode::Polyline);
    waveformLayout->addWidget(temperature_plot_);

    humidity_plot_title_ = new QLabel(waveform_group_);
    humidity_plot_title_->setObjectName("fieldLabel");
    waveformLayout->addWidget(humidity_plot_title_);
    humidity_plot_ = new SingleSeriesTrendPlotWidget(appThemeColor(AppThemeColor::PlotSeriesHumidity, false),
        is_english_ ? "No humidity series" : "没有湿度趋势数据",
        QStringLiteral("%RH"),
        waveform_group_);
    humidity_plot_->setObjectName(QStringLiteral("sessionViewerHumidityPlot"));
    static_cast<SingleSeriesTrendPlotWidget*>(humidity_plot_)->setPlotMode(
        waveform_peak_scatter_mode_ ? SingleSeriesTrendPlotWidget::PlotMode::Scatter : SingleSeriesTrendPlotWidget::PlotMode::Polyline);
    waveformLayout->addWidget(humidity_plot_);

    pressure_plot_title_ = new QLabel(waveform_group_);
    pressure_plot_title_->setObjectName("fieldLabel");
    waveformLayout->addWidget(pressure_plot_title_);
    pressure_plot_ = new SingleSeriesTrendPlotWidget(appThemeColor(AppThemeColor::PlotSeriesPressure, false),
        is_english_ ? "No pressure series" : "没有气压趋势数据",
        QStringLiteral("hPa"),
        waveform_group_);
    pressure_plot_->setObjectName(QStringLiteral("sessionViewerPressurePlot"));
    static_cast<SingleSeriesTrendPlotWidget*>(pressure_plot_)->setPlotMode(
        waveform_peak_scatter_mode_ ? SingleSeriesTrendPlotWidget::PlotMode::Scatter : SingleSeriesTrendPlotWidget::PlotMode::Polyline);
    waveformLayout->addWidget(pressure_plot_);

    environment_info_label_ = new QLabel(waveform_group_);
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

    csv_group_ = new QGroupBox(summaryWaveSplitter);
    csv_group_->setObjectName("sensorGroupBox");
    auto *csvLayout = new QVBoxLayout(csv_group_);
    csvLayout->setContentsMargins(10, 30, 10, 10);
    csvLayout->setSpacing(6);

    csv_info_label_ = new QLabel(csv_group_);
    csv_info_label_->setWordWrap(true);
    csvLayout->addWidget(csv_info_label_);

    csv_model_ = new SessionCsvTableModel(this);
    csv_table_ = new QTableView(csv_group_);
    csv_table_->setObjectName(QStringLiteral("sessionViewerCsvTable"));
    csv_table_->viewport()->setObjectName(QStringLiteral("sessionViewerCsvViewport"));
    csv_table_->setModel(csv_model_);
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
    tablePalette.setColor(QPalette::Window, theme.background);
    tablePalette.setColor(QPalette::Highlight, theme.selectedBackground);
    tablePalette.setColor(QPalette::HighlightedText, theme.selectedText);
    csv_table_->setPalette(tablePalette);
    csv_table_->viewport()->setPalette(tablePalette);
    csv_table_->viewport()->setBackgroundRole(QPalette::Base);
    csv_table_->viewport()->setAutoFillBackground(true);
    csv_table_->horizontalHeader()->setPalette(tablePalette);

    csv_table_->setStyleSheet(QStringLiteral(
        "QTableView {"
        " background-color: %1;"
        " alternate-background-color: %1;"
        " border: 1px solid %3;"
        " color: %2;"
        " gridline-color: %3;"
        " selection-background-color: %6;"
        " selection-color: %7;"
        "}"
        "QWidget#sessionViewerCsvViewport { background-color: %1; }"
        "QTableView::item { color: %2; }"
        "QTableView::item:selected { background-color: %6; color: %7; }"
        "QTableView::item:selected:active { background-color: %6; color: %7; }"
        "QTableView::item:selected:!active { background-color: %6; color: %7; }"
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
    if (!csv_table_ || !csv_model_)
    {
        return;
    }

    csv_model_->setTheme(sessionTableThemeFor(this));
    csv_table_->viewport()->update();
}

void SessionViewerWindow::updateCsvDisplayHeaders()
{
    if (!csv_model_)
    {
        return;
    }

    QStringList displayHeaders;
    displayHeaders.reserve(csv_headers_.size() + 2);
    displayHeaders << (is_english_ ? QStringLiteral("No.") : QStringLiteral("序号"));
    displayHeaders << (is_english_ ? QStringLiteral("Delta") : QStringLiteral("时间误差"));
    displayHeaders << csv_headers_;
    csv_model_->setHeaders(displayHeaders);
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
        updateSessionLoadingDialogTheme();
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
    temperature_plot_title_->setText(is_english_ ? "Temperature  °C" : "温度  ℃");
    humidity_plot_title_->setText(is_english_ ? "Humidity  %RH" : "湿度  %RH");
    pressure_plot_title_->setText(is_english_ ? "Pressure  hPa" : "气压  hPa");
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
    updateCsvDisplayHeaders();

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

QString SessionViewerWindow::peakSearchRangeText() const
{
    const QString searchEndText = peak_search_end_index_ <= 0
        ? (is_english_ ? QStringLiteral("end") : QStringLiteral("末尾"))
        : QString::number(peak_search_end_index_);
    return QStringLiteral("%1-%2").arg(peak_search_start_index_).arg(searchEndText);
}

void SessionViewerWindow::updatePeakFilterButtonText()
{
    if (!waveform_peak_filter_btn_)
    {
        return;
    }

    waveform_peak_filter_btn_->setText(QStringLiteral("%1:%2 / %3")
        .arg(is_english_ ? QStringLiteral("Peak") : QStringLiteral("峰值"))
        .arg(peakSearchRangeText())
        .arg(peakFilterModeText(peak_filter_settings_.mode)));
}

void SessionViewerWindow::syncPeakSettingsToTrajectoryViewer()
{
    if (!trajectory_viewer_dialog_)
    {
        return;
    }

    trajectory_viewer_dialog_->setPeakSettings(
        peak_search_start_index_,
        peak_search_end_index_,
        static_cast<int>(peak_filter_settings_.mode),
        peak_filter_settings_.min_value,
        peak_filter_settings_.max_value);
}

bool SessionViewerWindow::applyPeakSettings(int searchStartIndex,
                                            int searchEndIndex,
                                            PeakFilterMode mode,
                                            double minValue,
                                            double maxValue,
                                            bool hasMinValue,
                                            bool hasMaxValue,
                                            const QString& recalculatingText,
                                            const QString& filteringText)
{
    if (searchStartIndex < 0 || (searchEndIndex > 0 && searchEndIndex <= searchStartIndex))
    {
        return false;
    }

    const bool peakSearchChanged =
        peak_search_start_index_ != searchStartIndex ||
        peak_search_end_index_ != searchEndIndex;
    peak_search_start_index_ = searchStartIndex;
    peak_search_end_index_ = searchEndIndex;
    peak_filter_settings_.mode = mode;
    if (hasMinValue)
    {
        peak_filter_settings_.min_value = minValue;
    }
    if (hasMaxValue)
    {
        peak_filter_settings_.max_value = maxValue;
    }

    QSettings settings("VaporView", "SessionViewer");
    settings.setValue("peak_filter/mode",
        mode == PeakFilterMode::IqrOutlier
            ? QStringLiteral("iqr")
            : mode == PeakFilterMode::KeepRange
                ? QStringLiteral("keep_range")
                : mode == PeakFilterMode::ExcludeRange
                    ? QStringLiteral("exclude_range")
                    : QStringLiteral("none"));
    settings.setValue("peak_filter/min_value", peak_filter_settings_.min_value);
    settings.setValue("peak_filter/max_value", peak_filter_settings_.max_value);
    settings.setValue("peak_search/start_index", peak_search_start_index_);
    settings.setValue("peak_search/end_index", peak_search_end_index_);

    updatePeakFilterButtonText();
    syncPeakSettingsToTrajectoryViewer();
    beginSessionLoading(peakSearchChanged ? recalculatingText : filteringText);
    if (peakSearchChanged &&
        (!waveform_segments_.isEmpty() || !raw_tcp_wave_frames_.isEmpty() || !indexed_waveform_frames_.isEmpty()))
    {
        const bool loaded = loadWaveformPeakSeries();
        finishSessionLoading();
        syncPeakSettingsToTrajectoryViewer();
        return loaded;
    }

    applyPeakFilter();
    finishSessionLoading();
    syncPeakSettingsToTrajectoryViewer();
    return true;
}

void SessionViewerWindow::applyPeakSettingsFromTrajectory(int searchStartIndex,
                                                          int searchEndIndex,
                                                          int filterMode,
                                                          double minValue,
                                                          double maxValue)
{
    const PeakFilterMode mode = static_cast<PeakFilterMode>(filterMode);
    if (mode != PeakFilterMode::None &&
        mode != PeakFilterMode::IqrOutlier &&
        mode != PeakFilterMode::KeepRange &&
        mode != PeakFilterMode::ExcludeRange)
    {
        return;
    }
    if (!applyPeakSettings(searchStartIndex,
            searchEndIndex,
            mode,
            minValue,
            maxValue,
            true,
            true,
            is_english_ ? QStringLiteral("Recalculating waveform peak series...") : QStringLiteral("正在重新计算波形峰值序列..."),
            is_english_ ? QStringLiteral("Applying peak filter...") : QStringLiteral("正在应用峰值过滤...")))
    {
        syncPeakSettingsToTrajectoryViewer();
    }
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

void SessionViewerWindow::updateSessionLoadingDialogTheme()
{
    if (!loading_dialog_)
    {
        return;
    }

    const QPalette sourcePalette = palette();
    QColor windowColor = sourcePalette.color(QPalette::Window);
    if (!windowColor.isValid() || windowColor.alpha() == 0)
    {
        windowColor = sourcePalette.color(QPalette::Base);
    }
    const bool dark = windowColor.lightness() < 128;
    const QColor panelColor = appThemeColor(dark ? AppThemeColor::Window : AppThemeColor::Surface, dark);
    const QColor fieldColor = appThemeColor(AppThemeColor::FieldBackground, dark);
    const QColor borderColor = appThemeColor(AppThemeColor::FieldBorder, dark);
    const QColor textColor = appThemeColor(AppThemeColor::TextStrong, dark);
    const QColor chunkColor = appThemeColor(AppThemeColor::ProgressChunk, dark);

    QPalette loadingPalette = loading_dialog_->palette();
    loadingPalette.setColor(QPalette::Window, panelColor);
    loadingPalette.setColor(QPalette::Base, panelColor);
    loadingPalette.setColor(QPalette::Text, textColor);
    loadingPalette.setColor(QPalette::WindowText, textColor);
    loading_dialog_->setPalette(loadingPalette);

    if (QWidget *content = loading_dialog_->findChild<QWidget *>(QStringLiteral("customTitleBarContent")))
    {
        content->setAutoFillBackground(true);
        content->setPalette(loadingPalette);
        if (auto *layout = qobject_cast<QVBoxLayout *>(content->layout()))
        {
            layout->setContentsMargins(22, 18, 22, 18);
            layout->setSpacing(14);
        }
    }

    loading_dialog_->setStyleSheet(QStringLiteral(
        "QProgressDialog, QWidget#customTitleBarContent { background-color: %1; color: %2; }"
        "QWidget#customTitleBarContent QLabel { background-color: transparent; color: %2; font-size: 14px; }"
        "QWidget#customTitleBarContent QProgressBar { background-color: %3; border: 1px solid %4; border-radius: 4px; min-height: 10px; text-align: center; color: %2; }"
        "QWidget#customTitleBarContent QProgressBar::chunk { background-color: %5; border-radius: 3px; }")
        .arg(panelColor.name(), textColor.name(), fieldColor.name(), borderColor.name(), chunkColor.name()));
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
        loading_dialog_->setRange(0, 100);
        loading_dialog_->setMinimumWidth(360);
        loading_dialog_->setAttribute(Qt::WA_StyledBackground, true);
        loading_dialog_->setAutoFillBackground(true);
        VaporView::installCustomTitleBar(loading_dialog_, false);
        if (QWidget *content = loading_dialog_->findChild<QWidget *>(QStringLiteral("customTitleBarContent")))
        {
            auto *layout = qobject_cast<QVBoxLayout *>(content->layout());
            if (!layout)
            {
                layout = new QVBoxLayout(content);
            }
            loading_dialog_label_ = new QLabel(content);
            loading_dialog_label_->setAlignment(Qt::AlignCenter);
            loading_dialog_label_->setWordWrap(true);
            loading_dialog_progress_bar_ = new QProgressBar(content);
            loading_dialog_progress_bar_->setRange(0, 100);
            loading_dialog_progress_bar_->setValue(0);
            loading_dialog_progress_bar_->setFormat(QStringLiteral("%p%"));
            loading_dialog_progress_bar_->setTextVisible(true);
            loading_dialog_progress_bar_->setMinimumHeight(14);
            layout->addWidget(loading_dialog_label_);
            layout->addWidget(loading_dialog_progress_bar_);
        }
    }

    loading_dialog_->setWindowTitle(is_english_ ? "Loading Data" : "正在加载数据");
    loading_dialog_progress_percent_ = 0;
    updateSessionLoadingDialogTheme();
    updateSessionLoadingProgress(text, 0);
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
    loading_dialog_->setValue(loading_dialog_progress_percent_);
    if (loading_dialog_label_)
    {
        loading_dialog_label_->setText(text);
    }
    if (loading_dialog_progress_bar_)
    {
        loading_dialog_progress_bar_->setVisible(true);
        loading_dialog_progress_bar_->setRange(0, 100);
        loading_dialog_progress_bar_->setValue(loading_dialog_progress_percent_);
    }
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void SessionViewerWindow::updateSessionLoadingProgress(const QString& text, int percent)
{
    loading_dialog_progress_percent_ = std::clamp(percent, 0, 100);
    updateSessionLoadingText(text);
}

void SessionViewerWindow::finishSessionLoading()
{
    if (loading_dialog_)
    {
        updateSessionLoadingProgress(status_label_ ? status_label_->text() : QString(), 100);
    }
    session_loading_ = false;
    if (loading_dialog_)
    {
        loading_dialog_->hide();
    }
    setSessionLoadingControlsEnabled(true);
}

void SessionViewerWindow::clearLoadedData(bool clearPathEdit)
{
    cancelBackgroundWaveformPeakSeries(false);
    session_directory_.clear();
    metadata_filename_.clear();
    sensors_csv_filename_.clear();
    waveform_directory_.clear();
    waveform_index_filename_.clear();
    waveform_peak_index_filename_.clear();
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
    rtk_track_stats_ = RtkTrackStats();
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

    if (csv_model_)
    {
        csv_model_->clear();
    }
    highlighted_csv_rows_.clear();
    primary_highlighted_csv_row_ = -1;
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
        trajectory_viewer_dialog_->setTrackStats(rtk_track_stats_);
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

    if (!ensureTrajectoryPeakValuesReady())
    {
        QMessageBox::warning(this,
            is_english_ ? "RTK Trajectory" : "RTK轨迹",
            is_english_ ? "Failed to prepare waveform peak values for the trajectory viewer."
                        : "无法为轨迹查看器准备波形峰值。");
        return;
    }

    if (!trajectory_viewer_dialog_)
    {
        trajectory_viewer_dialog_ = new TrajectoryViewerDialog(this);
        trajectory_viewer_dialog_->setAttribute(Qt::WA_QuitOnClose, false);
        trajectory_viewer_dialog_->setAttribute(Qt::WA_DeleteOnClose, false);
        connect(trajectory_viewer_dialog_, &QObject::destroyed, this, [this]() {
            trajectory_viewer_dialog_ = nullptr;
        });
        connect(trajectory_viewer_dialog_, &TrajectoryViewerDialog::peakSettingsChangeRequested,
                this, &SessionViewerWindow::applyPeakSettingsFromTrajectory);
    }

    trajectory_viewer_dialog_->setEnglish(is_english_);
    trajectory_viewer_dialog_->setTrackLabel(QStringLiteral("RTK trajectory"), QStringLiteral("RTK轨迹"));
    syncPeakSettingsToTrajectoryViewer();
    trajectory_viewer_dialog_->setTrackStats(rtk_track_stats_);
    trajectory_viewer_dialog_->setTrackPoints(rtk_track_points_);
    connect(trajectory_viewer_dialog_, &TrajectoryViewerDialog::trackPointActivated,
            this, &SessionViewerWindow::focusTrajectoryPoint,
            Qt::UniqueConnection);
    VaporView::centerWindowOnScreen(trajectory_viewer_dialog_, this);
    trajectory_viewer_dialog_->show();
    trajectory_viewer_dialog_->raise();
    trajectory_viewer_dialog_->activateWindow();
}

bool SessionViewerWindow::ensureTrajectoryPeakValuesReady()
{
    const bool hasWaveformFrames =
        !waveform_segments_.isEmpty() ||
        !raw_tcp_wave_frames_.isEmpty() ||
        !indexed_waveform_frames_.isEmpty();
    if (!hasWaveformFrames)
    {
        return true;
    }

    const bool peakSeriesReady =
        !waveform_peak_values_.isEmpty() &&
        waveform_peak_values_.size() == waveform_timestamps_us_.size() &&
        (!total_waveform_frames_ || static_cast<quint64>(waveform_peak_values_.size()) == total_waveform_frames_);
    if (peakSeriesReady)
    {
        return true;
    }

    beginSessionLoading(is_english_
        ? QStringLiteral("Preparing trajectory peak values...")
        : QStringLiteral("正在准备轨迹峰值数据..."));
    cancelBackgroundWaveformPeakSeries(false);
    const bool loaded = loadWaveformPeakSeries(false);
    finishSessionLoading();
    syncPeakSettingsToTrajectoryViewer();
    return loaded;
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
        raw_data_parser_window_->setAttribute(Qt::WA_DeleteOnClose, false);
        connect(raw_data_parser_window_, &QObject::destroyed, this, [this]() {
            raw_data_parser_window_ = nullptr;
        });
    }

    raw_data_parser_window_->setEnglish(is_english_);
    VaporView::centerWindowOnScreen(raw_data_parser_window_, this);
    raw_data_parser_window_->show();
    raw_data_parser_window_->raise();
    raw_data_parser_window_->activateWindow();
    raw_data_parser_window_->openSessionPath(session_directory_);
}

bool SessionViewerWindow::loadSessionDirectory(QString sessionDirectory)
{
    beginSessionLoading(is_english_ ? "Preparing to load session data..." : "正在准备加载会话数据...");
    const qint64 loadStartedMs = monotonicMilliseconds();
    qint64 lastStageMs = loadStartedMs;
    QStringList loadTimings;
    auto recordStageTiming = [&](const QString& stageName) {
        const qint64 now = monotonicMilliseconds();
        const qint64 stageMs = std::max<qint64>(0, now - lastStageMs);
        lastStageMs = now;
        loadTimings.push_back(QStringLiteral("%1 %2 ms").arg(stageName).arg(stageMs));
    };
    auto timingSummary = [&]() {
        if (loadTimings.isEmpty())
        {
            return QString();
        }
        const qint64 totalMs = std::max<qint64>(0, monotonicMilliseconds() - loadStartedMs);
        return QStringLiteral("%1 | %2 %3 ms")
            .arg(loadTimings.join(QStringLiteral(" | ")))
            .arg(is_english_ ? QStringLiteral("Total") : QStringLiteral("总计"))
            .arg(totalMs);
    };
    clearLoadedData(false);

    const QString normalized = QDir::fromNativeSeparators(sessionDirectory);
    session_directory_ = normalized;
    updateSessionLoadingProgress(is_english_ ? "Reading session metadata..." : "正在读取会话元数据...", 3);
    if (!loadSessionMetadata(normalized))
    {
        finishSessionLoading();
        return false;
    }
    recordStageTiming(is_english_ ? QStringLiteral("Metadata") : QStringLiteral("元数据"));
    updateSessionLoadingProgress(is_english_ ? "Reading sensors CSV..." : "正在读取传感器 CSV...", 8);
    if (!loadSensorsCsv())
    {
        finishSessionLoading();
        return false;
    }
    recordStageTiming(is_english_ ? QStringLiteral("Sensors CSV") : QStringLiteral("传感器 CSV"));
    updateSessionLoadingProgress(is_english_ ? "Indexing waveform files..." : "正在索引波形文件...", 36);
    if (!loadWaveformSegments())
    {
        finishSessionLoading();
        return false;
    }
    recordStageTiming(is_english_ ? QStringLiteral("TCP/waveform index") : QStringLiteral("TCP/波形索引"));
    updateSessionLoadingProgress(is_english_ ? "Calculating waveform peak series..." : "正在计算波形峰值序列...", 45);
    if (!loadWaveformPeakSeries(true))
    {
        finishSessionLoading();
        return false;
    }
    recordStageTiming(is_english_ ? QStringLiteral("Peak series") : QStringLiteral("峰值序列"));

    updateSessionLoadingProgress(is_english_ ? "Updating viewer..." : "正在更新显示...", 98);
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

    recordStageTiming(is_english_ ? QStringLiteral("Viewer refresh") : QStringLiteral("界面刷新"));
    const QString summary = timingSummary();
    setProperty("_vvSessionLoadTimingSummary", summary);
    if (status_label_)
    {
        status_label_->setToolTip(summary);
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
    const QString waveformPeakIndexRelativePath = paths.value(QStringLiteral("waveform_peak_index")).toString(QStringLiteral("raw/tcp_wave_peaks.csv"));
    const QJsonObject rawFiles = root.value(QStringLiteral("raw_files")).toObject();
    const QJsonObject tcpWaveRaw = rawFiles.value(QStringLiteral("tcp_wave")).toObject();
    const QString rawTcpWaveRelativePath = tcpWaveRaw.value(QStringLiteral("path")).toString(QStringLiteral("raw/tcp_wave.dat"));
    sensors_csv_filename_ = QDir(sessionDirectory).filePath(csvRelativePath);
    waveform_directory_ = QDir(sessionDirectory).filePath(waveformRelativePath);
    waveform_index_filename_ = QDir(sessionDirectory).filePath(waveformIndexRelativePath);
    waveform_peak_index_filename_ = QDir(sessionDirectory).filePath(waveformPeakIndexRelativePath);
    raw_tcp_wave_filename_ = QDir(sessionDirectory).filePath(rawTcpWaveRelativePath);
    return true;
}

bool SessionViewerWindow::loadSensorsCsv()
{
    if (csv_model_)
    {
        csv_model_->clear();
    }
    highlighted_csv_rows_.clear();
    primary_highlighted_csv_row_ = -1;
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
    const bool hasTrackColumns = navLatIndex >= 0 && navLonIndex >= 0;
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

    QVector<QStringList> rows;
    rows.reserve(static_cast<int>(std::min<quint64>(total_sensor_rows_ > 0 ? total_sensor_rows_ : 256ULL, 50000ULL)));
    temperature_values_.clear();
    humidity_values_.clear();
    pressure_values_.clear();
    rtk_track_points_.clear();
    rtk_track_stats_ = RtkTrackStats();
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

        if (hasTrackColumns)
        {
            ++rtk_track_stats_.scanned_rows;
            const bool navValid = epsilonValidIndex < 0 || parseBooleanCsvField(csvValueAt(fields, epsilonValidIndex), true);
            const double lat = parseOptionalDouble(csvValueAt(fields, navLatIndex));
            const double lon = parseOptionalDouble(csvValueAt(fields, navLonIndex));
            const QString gnssFix = csvValueAt(fields, gnssFixIndex).trimmed().toUpper();
            const bool missingOrInvalidNav = !navValid || !std::isfinite(lat) || !std::isfinite(lon);
            const bool zeroCoordinate = !missingOrInvalidNav && std::abs(lat) < 1e-8 && std::abs(lon) < 1e-8;
            const bool outOfRange = !missingOrInvalidNav && !zeroCoordinate &&
                (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0);
            const bool badFix = gnssFix == QStringLiteral("NONE") ||
                gnssFix == QStringLiteral("NO_FIX") ||
                gnssFix == QStringLiteral("INVALID") ||
                gnssFix == QStringLiteral("NO_GPS");
            if (missingOrInvalidNav)
            {
                ++rtk_track_stats_.rejected_invalid_nav;
            }
            else if (zeroCoordinate)
            {
                ++rtk_track_stats_.rejected_zero_coordinate;
            }
            else if (outOfRange)
            {
                ++rtk_track_stats_.rejected_out_of_range;
            }
            else if (badFix)
            {
                ++rtk_track_stats_.rejected_bad_fix;
            }
            else
            {
                bool timestampOk = false;
                const quint64 trackTimestampUs = trackTimestampIndex >= 0
                    ? csvValueAt(fields, trackTimestampIndex).toULongLong(&timestampOk)
                    : csv_timestamps_us_.last();
                RtkTrackPoint point;
                point.latitude = lat;
                point.longitude = lon;
                point.csv_row = rows.size() - 1;
                point.gnss_fix = gnssFix;
                if (std::isfinite(temperatureValue))
                {
                    point.temperature_c = temperatureValue;
                    point.has_temperature = true;
                }
                const double humidityValue = humidity_values_.isEmpty() ? std::numeric_limits<double>::quiet_NaN() : humidity_values_.last();
                if (std::isfinite(humidityValue))
                {
                    point.humidity_rh = humidityValue;
                    point.has_humidity = true;
                }
                if (std::isfinite(pressureValue))
                {
                    point.pressure_hpa = pressureValue;
                    point.has_pressure = true;
                }
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
                    if (jumpMeters > rtk_track_stats_.jump_threshold_m)
                    {
                        ++rtk_track_stats_.rejected_jump;
                        continue;
                    }
                    point.segment_distance_m = jumpMeters;
                    point.cumulative_distance_m = previous.cumulative_distance_m + jumpMeters;
                    if (previous.timestamp_us > 0 && point.timestamp_us > previous.timestamp_us)
                    {
                        const double elapsedSeconds = static_cast<double>(point.timestamp_us - previous.timestamp_us) / 1000000.0;
                        if (elapsedSeconds > 1e-6)
                        {
                            point.speed_mps = jumpMeters / elapsedSeconds;
                            point.has_speed = true;
                        }
                    }
                }
                rtk_track_points_.push_back(point);
                ++rtk_track_stats_.accepted_points;
            }
        }

        if (session_loading_ && rows.size() % 5000 == 0)
        {
            updateSessionLoadingProgress(QString(is_english_
                ? "Reading sensors CSV... %1 rows"
                : "正在读取传感器 CSV... %1 行")
                .arg(rows.size()),
                rangedProgressPercent(static_cast<quint64>(rows.size()), total_sensor_rows_, 8, 24));
        }
    }

    if (session_loading_)
    {
        updateSessionLoadingProgress(is_english_
            ? QStringLiteral("Preparing virtual CSV table...")
            : QStringLiteral("正在准备虚拟 CSV 表格..."),
            36);
    }
    csv_model_->setRows(displayHeaders, std::move(rows));
    csv_model_->setTheme(sessionTableThemeFor(this));
    csv_table_->setColumnWidth(0, 48);
    csv_table_->setColumnWidth(1, 96);

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
            updateSessionLoadingProgress(QString(is_english_
                ? "Indexing waveform files... %1/%2 files"
                : "正在索引波形文件... %1/%2 个文件")
                .arg(fileIndex + 1)
                .arg(files.size()),
                rangedProgressPercent(static_cast<quint64>(fileIndex + 1), static_cast<quint64>(files.size()), 36, 45));
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
                    updateSessionLoadingProgress(QString(is_english_
                        ? "Indexing raw TCP waveform frames... %1 frames"
                        : "正在索引 raw TCP 波形帧... %1 帧")
                        .arg(raw_tcp_wave_frames_.size()),
                        rangedProgressPercent(static_cast<quint64>(std::max<qint64>(0, nextRecord)),
                                              static_cast<quint64>(std::max<qint64>(1, file.size())),
                                              36,
                                              45));
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

void SessionViewerWindow::applyPeakFilter(int startPercent, int endPercent)
{
    const int clampedStart = std::clamp(startPercent, 0, 100);
    const int clampedEnd = std::clamp(endPercent, clampedStart, 100);
    const int prepareEnd = clampedStart + (clampedEnd - clampedStart) / 3;
    if (session_loading_)
    {
        updateSessionLoadingProgress(is_english_ ? "Applying peak filter..." : "正在应用峰值过滤...", clampedStart);
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
            updateSessionLoadingProgress(QString(is_english_
                ? "Preparing peak filter... %1/%2 values"
                : "正在准备峰值过滤... %1/%2 个值")
                .arg(index + 1)
                .arg(waveform_peak_raw_values_.size()),
                rangedProgressPercent(static_cast<quint64>(index + 1),
                                      static_cast<quint64>(waveform_peak_raw_values_.size()),
                                      clampedStart,
                                      prepareEnd));
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
            updateSessionLoadingProgress(QString(is_english_
                ? "Applying peak filter... %1/%2 values"
                : "正在应用峰值过滤... %1/%2 个值")
                .arg(index + 1)
                .arg(waveform_peak_raw_values_.size()),
                rangedProgressPercent(static_cast<quint64>(index + 1),
                                      static_cast<quint64>(waveform_peak_raw_values_.size()),
                                      prepareEnd,
                                      std::max(prepareEnd, clampedEnd - 1)));
        }
    }

    if (session_loading_)
    {
        updateSessionLoadingProgress(is_english_ ? "Refreshing filtered plots..." : "正在刷新过滤后的图表...", clampedEnd);
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

bool SessionViewerWindow::loadWaveformPeakIndexSeries()
{
    if (!isFullFramePeakSearch(peak_search_start_index_, peak_search_end_index_) ||
        waveform_peak_index_filename_.isEmpty() ||
        !QFileInfo::exists(waveform_peak_index_filename_))
    {
        return false;
    }

    QFile file(waveform_peak_index_filename_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    if (stream.atEnd())
    {
        return false;
    }

    const QStringList headers = parseCsvLine(stream.readLine());
    const int hostTimeIndex = findHeaderIndex(headers, {QStringLiteral("host_time_us"), QStringLiteral("timestamp_us")});
    const int peakValueIndex = findHeaderIndex(headers, {QStringLiteral("peak_value"), QStringLiteral("peak")});
    const int searchStartIndex = findHeaderIndex(headers, {QStringLiteral("search_start_index")});
    const int searchEndIndex = findHeaderIndex(headers, {QStringLiteral("search_end_index")});
    if (hostTimeIndex < 0 || peakValueIndex < 0)
    {
        return false;
    }

    QVector<quint64> timestampsUs;
    QVector<float> peakValues;
    const qsizetype reserveCount = static_cast<qsizetype>(std::min<quint64>(
        total_waveform_frames_,
        static_cast<quint64>(std::numeric_limits<qsizetype>::max())));
    timestampsUs.reserve(reserveCount);
    peakValues.reserve(reserveCount);

    while (!stream.atEnd())
    {
        const QString line = stream.readLine();
        if (line.trimmed().isEmpty())
        {
            continue;
        }

        const QStringList fields = parseCsvLine(line);
        if (searchStartIndex >= 0)
        {
            bool ok = false;
            const int rowStart = csvValueAt(fields, searchStartIndex).toInt(&ok);
            if (ok && rowStart != 0)
            {
                return false;
            }
        }
        if (searchEndIndex >= 0)
        {
            bool ok = false;
            const int rowEnd = csvValueAt(fields, searchEndIndex).toInt(&ok);
            if (ok && rowEnd > 0)
            {
                return false;
            }
        }

        bool timestampOk = false;
        const quint64 timestampUs = csvValueAt(fields, hostTimeIndex).toULongLong(&timestampOk);
        if (!timestampOk)
        {
            continue;
        }

        const QString peakText = csvValueAt(fields, peakValueIndex).trimmed();
        bool peakOk = false;
        const float peakValue = peakText.isEmpty()
            ? std::numeric_limits<float>::quiet_NaN()
            : peakText.toFloat(&peakOk);
        timestampsUs.push_back(timestampUs);
        peakValues.push_back(peakText.isEmpty() || !peakOk
            ? std::numeric_limits<float>::quiet_NaN()
            : peakValue);
    }

    if (peakValues.isEmpty())
    {
        return false;
    }
    if (total_waveform_frames_ > 0 && static_cast<quint64>(peakValues.size()) != total_waveform_frames_)
    {
        return false;
    }

    waveform_timestamps_us_ = std::move(timestampsUs);
    waveform_peak_raw_values_ = std::move(peakValues);
    return true;
}

bool SessionViewerWindow::writeWaveformPeakIndexSeries() const
{
    if (waveform_peak_index_filename_.isEmpty() ||
        waveform_timestamps_us_.isEmpty() ||
        waveform_peak_raw_values_.isEmpty() ||
        waveform_timestamps_us_.size() != waveform_peak_raw_values_.size())
    {
        return false;
    }

    const QFileInfo info(waveform_peak_index_filename_);
    if (!QDir().mkpath(info.absolutePath()))
    {
        return false;
    }

    QSaveFile file(waveform_peak_index_filename_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << "host_time_us,peak_value,peak_index,point_count,search_start_index,search_end_index\n";
    for (int index = 0; index < waveform_timestamps_us_.size(); ++index)
    {
        const float peakValue = waveform_peak_raw_values_.at(index);
        stream << waveform_timestamps_us_.at(index) << ',';
        if (std::isfinite(peakValue))
        {
            stream << QString::number(static_cast<double>(peakValue), 'g', 9);
        }
        stream << ",-1," << points_per_frame_ << ",0,0\n";
    }
    return file.commit();
}

bool SessionViewerWindow::loadRawTcpWavePeakSeries()
{
    if (raw_tcp_wave_frames_.isEmpty())
    {
        return true;
    }

    QFile file;
    QString openFilename;
    QVector<WaveformPeakPayload> payloads;
    qsizetype chunkBytes = 0;
    const quint64 progressInterval = std::max<quint64>(1, static_cast<quint64>(raw_tcp_wave_frames_.size()) / 100);

    auto flushChunk = [&]() {
        if (payloads.isEmpty())
        {
            return;
        }
        appendWaveformPeakResults(payloads, waveform_timestamps_us_, waveform_peak_raw_values_);
        payloads.clear();
        chunkBytes = 0;
    };

    for (int frameIndex = 0; frameIndex < raw_tcp_wave_frames_.size(); ++frameIndex)
    {
        const RawTcpWaveFrame& frame = raw_tcp_wave_frames_.at(frameIndex);
        if (openFilename != frame.filename)
        {
            file.close();
            file.setFileName(frame.filename);
            if (!file.open(QIODevice::ReadOnly))
            {
                setStatusText(QString(is_english_ ? "Failed to read raw TCP wave file: %1" : "读取 raw TCP 波形文件失败: %1").arg(frame.filename));
                return false;
            }
            openFilename = frame.filename;
        }

        const quint64 sampleCount64 = frame.harmonic_payload_size / kFloatBytes;
        const int sampleCount = static_cast<int>(std::min<quint64>(
            sampleCount64,
            static_cast<quint64>(std::numeric_limits<int>::max())));
        QByteArray payload;
        if (!readWaveformPeakPayload(file,
                                     frame.harmonic_payload_offset,
                                     sampleCount,
                                     peak_search_start_index_,
                                     peak_search_end_index_,
                                     payload))
        {
            setStatusText(QString(is_english_ ? "Incomplete raw TCP wave frame in %1" : "%1 中的 raw TCP 波形帧不完整").arg(frame.filename));
            return false;
        }

        WaveformPeakPayload peakPayload;
        peakPayload.timestamp_us = frame.timestamp_us;
        peakPayload.payload = std::move(payload);
        peakPayload.encoding = frame.float_encoding;
        chunkBytes += peakPayload.payload.size();
        payloads.push_back(std::move(peakPayload));

        if (chunkBytes >= kPeakPayloadChunkBytes)
        {
            flushChunk();
        }
        if (session_loading_ && (static_cast<quint64>(frameIndex + 1) == total_waveform_frames_ ||
                                 static_cast<quint64>(frameIndex + 1) % progressInterval == 0))
        {
            updateSessionLoadingProgress(QString(is_english_
                ? "Calculating waveform peak series... %1/%2 frames"
                : "正在计算波形峰值序列... %1/%2 帧")
                .arg(frameIndex + 1)
                .arg(total_waveform_frames_),
                rangedProgressPercent(static_cast<quint64>(frameIndex + 1), total_waveform_frames_, 45, 90));
        }
    }

    flushChunk();
    return true;
}

bool SessionViewerWindow::loadIndexedWaveformPeakSeries()
{
    if (indexed_waveform_frames_.isEmpty())
    {
        return true;
    }

    QVector<WaveformPeakPayload> payloads;
    qsizetype chunkBytes = 0;
    const quint64 progressInterval = std::max<quint64>(1, static_cast<quint64>(indexed_waveform_frames_.size()) / 100);

    auto flushChunk = [&]() {
        if (payloads.isEmpty())
        {
            return;
        }
        appendWaveformPeakResults(payloads, waveform_timestamps_us_, waveform_peak_raw_values_);
        payloads.clear();
        chunkBytes = 0;
    };

    for (int frameIndex = 0; frameIndex < indexed_waveform_frames_.size(); ++frameIndex)
    {
        const IndexedWaveformFrame& frame = indexed_waveform_frames_.at(frameIndex);
        QFile file(frame.filename);
        if (!file.open(QIODevice::ReadOnly))
        {
            setStatusText(QString(is_english_ ? "Failed to read indexed waveform file: %1" : "读取索引波形文件失败: %1").arg(frame.filename));
            return false;
        }

        QByteArray payload;
        if (!readWaveformPeakPayload(file,
                                     0,
                                     static_cast<int>(std::min<quint64>(frame.point_count, static_cast<quint64>(std::numeric_limits<int>::max()))),
                                     peak_search_start_index_,
                                     peak_search_end_index_,
                                     payload))
        {
            setStatusText(QString(is_english_ ? "Incomplete indexed waveform file: %1" : "索引波形文件不完整: %1").arg(frame.filename));
            return false;
        }

        WaveformPeakPayload peakPayload;
        peakPayload.timestamp_us = frame.timestamp_us;
        peakPayload.payload = std::move(payload);
        peakPayload.encoding = VaporView::TcpFloatEncoding::LittleEndian;
        chunkBytes += peakPayload.payload.size();
        payloads.push_back(std::move(peakPayload));

        if (chunkBytes >= kPeakPayloadChunkBytes)
        {
            flushChunk();
        }
        if (session_loading_ && (static_cast<quint64>(frameIndex + 1) == total_waveform_frames_ ||
                                 static_cast<quint64>(frameIndex + 1) % progressInterval == 0))
        {
            updateSessionLoadingProgress(QString(is_english_
                ? "Calculating waveform peak series... %1/%2 frames"
                : "正在计算波形峰值序列... %1/%2 帧")
                .arg(frameIndex + 1)
                .arg(total_waveform_frames_),
                rangedProgressPercent(static_cast<quint64>(frameIndex + 1), total_waveform_frames_, 45, 90));
        }
    }

    flushChunk();
    return true;
}

bool SessionViewerWindow::loadLegacyWaveformPeakSeries()
{
    if (waveform_segments_.isEmpty())
    {
        return true;
    }
    if (points_per_frame_ <= 0)
    {
        return true;
    }

    QVector<WaveformPeakPayload> payloads;
    qsizetype chunkBytes = 0;
    quint64 processedFrames = 0;
    const quint64 progressInterval = std::max<quint64>(1, total_waveform_frames_ / 100);
    const quint64 frameBytes = kWaveformTimestampBytes + static_cast<quint64>(points_per_frame_) * kFloatBytes;

    auto flushChunk = [&]() {
        if (payloads.isEmpty())
        {
            return;
        }
        appendWaveformPeakResults(payloads, waveform_timestamps_us_, waveform_peak_raw_values_);
        payloads.clear();
        chunkBytes = 0;
    };

    for (const WaveformSegment& segment : waveform_segments_)
    {
        QFile file(segment.filename);
        if (!file.open(QIODevice::ReadOnly))
        {
            setStatusText(QString(is_english_ ? "Failed to read waveform file: %1" : "读取波形文件失败: %1").arg(segment.filename));
            return false;
        }

        for (quint64 localFrame = 0; localFrame < segment.frame_count; ++localFrame)
        {
            const quint64 frameOffset = localFrame * frameBytes;
            if (frameOffset > static_cast<quint64>(std::numeric_limits<qint64>::max()) ||
                !file.seek(static_cast<qint64>(frameOffset)))
            {
                setStatusText(QString(is_english_ ? "Failed to read waveform file: %1" : "读取波形文件失败: %1").arg(segment.filename));
                return false;
            }

            quint64 timestampLe = 0;
            if (file.read(reinterpret_cast<char*>(&timestampLe), sizeof(timestampLe)) != static_cast<qint64>(sizeof(timestampLe)))
            {
                setStatusText(QString(is_english_ ? "Incomplete waveform frame in %1" : "%1 中的波形帧不完整").arg(segment.filename));
                return false;
            }

            QByteArray payload;
            if (!readWaveformPeakPayload(file,
                                         frameOffset + kWaveformTimestampBytes,
                                         points_per_frame_,
                                         peak_search_start_index_,
                                         peak_search_end_index_,
                                         payload))
            {
                setStatusText(QString(is_english_ ? "Incomplete waveform frame in %1" : "%1 中的波形帧不完整").arg(segment.filename));
                return false;
            }

            WaveformPeakPayload peakPayload;
            peakPayload.timestamp_us = qFromLittleEndian(timestampLe);
            peakPayload.payload = std::move(payload);
            peakPayload.encoding = VaporView::TcpFloatEncoding::LittleEndian;
            chunkBytes += peakPayload.payload.size();
            payloads.push_back(std::move(peakPayload));

            ++processedFrames;
            if (chunkBytes >= kPeakPayloadChunkBytes)
            {
                flushChunk();
            }
            if (session_loading_ && (processedFrames == total_waveform_frames_ || processedFrames % progressInterval == 0))
            {
                updateSessionLoadingProgress(QString(is_english_
                    ? "Calculating waveform peak series... %1/%2 frames"
                    : "正在计算波形峰值序列... %1/%2 帧")
                    .arg(processedFrames)
                    .arg(total_waveform_frames_),
                    rangedProgressPercent(processedFrames, total_waveform_frames_, 45, 90));
            }
        }
    }

    flushChunk();
    return true;
}

bool SessionViewerWindow::loadWaveformPeakSeries(bool allowBackground)
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

    if (isFullFramePeakSearch(peak_search_start_index_, peak_search_end_index_) && loadWaveformPeakIndexSeries())
    {
        if (session_loading_)
        {
            updateSessionLoadingProgress(is_english_
                ? "Loaded cached waveform peak index."
                : "已加载缓存的波形峰值索引。",
                90);
        }
        applyPeakFilter(90, 97);
        return true;
    }

    if (allowBackground)
    {
        startBackgroundWaveformPeakSeries();
        return true;
    }

    bool loaded = false;
    if (!raw_tcp_wave_frames_.isEmpty())
    {
        loaded = loadRawTcpWavePeakSeries();
    }
    else if (!indexed_waveform_frames_.isEmpty())
    {
        loaded = loadIndexedWaveformPeakSeries();
    }
    else
    {
        loaded = loadLegacyWaveformPeakSeries();
    }
    if (!loaded)
    {
        return false;
    }

    if (isFullFramePeakSearch(peak_search_start_index_, peak_search_end_index_))
    {
        writeWaveformPeakIndexSeries();
    }
    applyPeakFilter(90, 97);
    return true;
}

void SessionViewerWindow::startBackgroundWaveformPeakSeries()
{
    cancelBackgroundWaveformPeakSeries(false);
    const quint64 requestId = ++peak_series_request_id_;
    const QString sessionDirectory = session_directory_;
    const int searchStartIndex = peak_search_start_index_;
    const int searchEndIndex = peak_search_end_index_;
    const int pointsPerFrame = points_per_frame_;
    const bool fullFrameSearch = isFullFramePeakSearch(searchStartIndex, searchEndIndex);
    auto cancelFlag = std::make_shared<std::atomic_bool>(false);
    peak_series_cancel_flag_ = cancelFlag;

    QVector<BackgroundRawTcpWaveFrame> rawFrames;
    rawFrames.reserve(raw_tcp_wave_frames_.size());
    for (const RawTcpWaveFrame& frame : raw_tcp_wave_frames_)
    {
        BackgroundRawTcpWaveFrame copy;
        copy.filename = frame.filename;
        copy.harmonic_payload_offset = frame.harmonic_payload_offset;
        copy.harmonic_payload_size = frame.harmonic_payload_size;
        copy.timestamp_us = frame.timestamp_us;
        copy.float_encoding = frame.float_encoding;
        rawFrames.push_back(std::move(copy));
    }

    QVector<BackgroundIndexedWaveformFrame> indexedFrames;
    indexedFrames.reserve(indexed_waveform_frames_.size());
    for (const IndexedWaveformFrame& frame : indexed_waveform_frames_)
    {
        BackgroundIndexedWaveformFrame copy;
        copy.filename = frame.filename;
        copy.timestamp_us = frame.timestamp_us;
        copy.point_count = frame.point_count;
        indexedFrames.push_back(std::move(copy));
    }

    QVector<BackgroundWaveformSegment> segments;
    segments.reserve(waveform_segments_.size());
    for (const WaveformSegment& segment : waveform_segments_)
    {
        BackgroundWaveformSegment copy;
        copy.filename = segment.filename;
        copy.frame_count = segment.frame_count;
        segments.push_back(std::move(copy));
    }

    peak_series_watcher_ = new QFutureWatcher<WaveformPeakSeriesResult>(this);
    connect(peak_series_watcher_, &QFutureWatcher<WaveformPeakSeriesResult>::finished, this, [this, requestId, sessionDirectory, fullFrameSearch]() {
        QFutureWatcher<WaveformPeakSeriesResult> *watcher = peak_series_watcher_;
        if (!watcher)
        {
            return;
        }
        const WaveformPeakSeriesResult result = watcher->result();
        peak_series_watcher_ = nullptr;
        watcher->deleteLater();
        if (requestId != peak_series_request_id_ || sessionDirectory != session_directory_)
        {
            return;
        }
        if (!result.loaded)
        {
            setStatusText(QString(is_english_
                ? "Failed to calculate waveform peak series: %1"
                : "计算波形峰值序列失败: %1").arg(result.error));
            return;
        }

        waveform_timestamps_us_ = result.timestamps_us;
        waveform_peak_raw_values_ = result.peak_values;
        if (fullFrameSearch)
        {
            writeWaveformPeakIndexSeries();
        }
        applyPeakFilter();
        updateSummaryLabels();
        syncPeakSettingsToTrajectoryViewer();
        setStatusText(QString(is_english_
            ? "Loaded session: %1 (waveform peaks ready)"
            : "已加载会话: %1（波形峰值已就绪）").arg(session_directory_));
    });

    peak_series_watcher_->setFuture(QtConcurrent::run([rawFrames = std::move(rawFrames),
                                                       indexedFrames = std::move(indexedFrames),
                                                       segments = std::move(segments),
                                                       pointsPerFrame,
                                                       searchStartIndex,
                                                       searchEndIndex,
                                                       cancelFlag]() mutable {
        if (!rawFrames.isEmpty())
        {
            return calculateRawTcpWavePeakSeries(std::move(rawFrames), searchStartIndex, searchEndIndex, cancelFlag);
        }
        if (!indexedFrames.isEmpty())
        {
            return calculateIndexedWaveformPeakSeries(std::move(indexedFrames), searchStartIndex, searchEndIndex, cancelFlag);
        }
        return calculateLegacyWaveformPeakSeries(std::move(segments), pointsPerFrame, searchStartIndex, searchEndIndex, cancelFlag);
    }));
}

void SessionViewerWindow::cancelBackgroundWaveformPeakSeries(bool waitForFinished)
{
    Q_UNUSED(waitForFinished);
    ++peak_series_request_id_;
    if (peak_series_cancel_flag_)
    {
        peak_series_cancel_flag_->store(true, std::memory_order_relaxed);
        peak_series_cancel_flag_.reset();
    }
    if (!peak_series_watcher_)
    {
        return;
    }

    QFutureWatcher<WaveformPeakSeriesResult> *watcher = peak_series_watcher_;
    peak_series_watcher_ = nullptr;
    disconnect(watcher, nullptr, this, nullptr);
    delete watcher;
}

void SessionViewerWindow::updateSummaryLabels()
{
    const bool hasSession = !session_name_.isEmpty() || !metadata_filename_.isEmpty();
    session_name_value_->setText(session_name_.isEmpty() ? QStringLiteral("---") : session_name_);
    start_time_value_->setText(VaporView::formatSessionMetadataTimeBeijing(start_time_utc_));
    end_time_value_->setText(VaporView::formatSessionMetadataTimeBeijing(end_time_utc_));
    duration_value_->setText(hasSession ? VaporView::formatSessionDurationText(start_time_utc_, end_time_utc_, is_english_) : QStringLiteral("---"));
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
    updateSessionLoadingProgress(is_english_ ? "Refreshing plots..." : "正在刷新图表...", 80);
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
    searchEndSpin->setRange(0, 10000000);
    searchEndSpin->setSingleStep(1000);
    searchEndSpin->setSpecialValueText(is_english_ ? QStringLiteral("Full Frame") : QStringLiteral("整帧"));
    searchEndSpin->setValue(std::max(0, peak_search_end_index_));
    searchEndSpin->setMinimumWidth(inputColumnWidth);
    addFormRow(1, is_english_ ? QStringLiteral("Search End") : QStringLiteral("搜索终点"), searchEndSpin);

    auto *modeCombo = new QComboBox(formWidget);
    modeCombo->addItem(is_english_ ? QStringLiteral("Off") : QStringLiteral("关闭"), static_cast<int>(PeakFilterMode::None));
    modeCombo->addItem(is_english_ ? QStringLiteral("IQR Outlier Filter") : QStringLiteral("IQR 异常值过滤"), static_cast<int>(PeakFilterMode::IqrOutlier));
    modeCombo->addItem(is_english_ ? QStringLiteral("Keep Range") : QStringLiteral("保留区间"), static_cast<int>(PeakFilterMode::KeepRange));
    modeCombo->addItem(is_english_ ? QStringLiteral("Exclude Range") : QStringLiteral("排除区间"), static_cast<int>(PeakFilterMode::ExcludeRange));
    modeCombo->setCurrentIndex(std::max(0, modeCombo->findData(static_cast<int>(peak_filter_settings_.mode))));
    modeCombo->setMinimumWidth(inputColumnWidth);
    VaporView::configureComboBoxPopup(modeCombo, VaporView::isDarkThemeEnabled());
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
            ? QStringLiteral("Peak search uses sample indexes [start, end). Search End = Full Frame uses all remaining samples. IQR removes statistical outliers. Keep Range keeps only values inside [min, max]. Exclude Range removes values inside [min, max]. If no peak remains after filtering, the plot shows no valid values.")
            : QStringLiteral("峰值搜索使用采样点下标 [起点, 终点)。搜索终点为“整帧”时表示一直搜索到本帧末尾。IQR 会过滤统计异常值。保留区间只保留 [最小值, 最大值] 内的峰值。排除区间会过滤 [最小值, 最大值] 内的峰值。过滤后没有峰值时，趋势图显示无有效值。"),
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
    if (searchEnd > 0 && searchEnd <= searchStart)
    {
        QMessageBox::warning(
            this,
            is_english_ ? QStringLiteral("Peak Settings") : QStringLiteral("峰值设置"),
            is_english_ ? QStringLiteral("Search End must be greater than Search Start, or set to Full Frame.") : QStringLiteral("搜索终点必须大于搜索起点，或者设置为整帧。"));
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

    applyPeakSettings(searchStart,
        searchEnd,
        mode,
        minValue,
        maxValue,
        minOk,
        maxOk,
        is_english_ ? QStringLiteral("Recalculating waveform peak series...") : QStringLiteral("正在重新计算波形峰值序列..."),
        is_english_ ? QStringLiteral("Applying peak filter...") : QStringLiteral("正在应用峰值过滤..."));
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
    QVector<quint64> trackTimestampsUs;
    trackTimestampsUs.reserve(rtk_track_points_.size());
    for (const RtkTrackPoint& point : std::as_const(rtk_track_points_))
    {
        trackTimestampsUs.push_back(point.timestamp_us);
    }

    const double waveformRateHz = calculateMeasuredRateHz(waveform_timestamps_us_);
    const double trackRateHz = calculateMeasuredRateHz(trackTimestampsUs);
    const bool averageHighRatePeaks =
        waveformRateHz > 0.0 &&
        trackRateHz > 0.0 &&
        waveformRateHz > trackRateHz * 1.05;

    QVector<int> previousTrackTimestampIndex(rtk_track_points_.size(), -1);
    QVector<int> nextTrackTimestampIndex(rtk_track_points_.size(), -1);
    if (averageHighRatePeaks)
    {
        int previousIndex = -1;
        for (int index = 0; index < rtk_track_points_.size(); ++index)
        {
            previousTrackTimestampIndex[index] = previousIndex;
            if (rtk_track_points_.at(index).timestamp_us > 0)
            {
                previousIndex = index;
            }
        }

        int nextIndex = -1;
        for (int index = rtk_track_points_.size() - 1; index >= 0; --index)
        {
            nextTrackTimestampIndex[index] = nextIndex;
            if (rtk_track_points_.at(index).timestamp_us > 0)
            {
                nextIndex = index;
            }
        }
    }

    for (int trackIndex = 0; trackIndex < rtk_track_points_.size(); ++trackIndex)
    {
        RtkTrackPoint& point = rtk_track_points_[trackIndex];
        point.peak_value = 0.0f;
        point.has_peak_value = false;
        point.waveform_frame_index = -1;
        point.waveform_timestamp_us = 0;
        point.waveform_delta_us = 0;
        point.has_waveform_match = false;

        if (point.timestamp_us == 0)
        {
            continue;
        }

        const int peakIndex = findClosestTimestampIndex(waveform_timestamps_us_, point.timestamp_us);
        if (peakIndex < 0 || peakIndex >= waveform_peak_values_.size())
        {
            continue;
        }

        point.waveform_frame_index = peakIndex;
        if (peakIndex < waveform_timestamps_us_.size())
        {
            point.waveform_timestamp_us = waveform_timestamps_us_.at(peakIndex);
            point.waveform_delta_us = point.waveform_timestamp_us >= point.timestamp_us
                ? point.waveform_timestamp_us - point.timestamp_us
                : point.timestamp_us - point.waveform_timestamp_us;
            point.has_waveform_match = true;
        }

        float peakValue = waveform_peak_values_.at(peakIndex);
        if (averageHighRatePeaks)
        {
            const int previousIndex = previousTrackTimestampIndex.at(trackIndex);
            const int nextIndex = nextTrackTimestampIndex.at(trackIndex);
            quint64 lowerBoundUs = 0;
            quint64 upperBoundUs = 0;
            bool hasLowerBound = false;
            bool hasUpperBound = false;

            if (previousIndex >= 0)
            {
                const quint64 previousUs = rtk_track_points_.at(previousIndex).timestamp_us;
                if (previousUs > 0 && previousUs < point.timestamp_us)
                {
                    lowerBoundUs = midpointTimestamp(previousUs, point.timestamp_us);
                    hasLowerBound = true;
                }
            }
            if (!hasLowerBound && nextIndex >= 0)
            {
                const quint64 nextUs = rtk_track_points_.at(nextIndex).timestamp_us;
                if (nextUs > point.timestamp_us)
                {
                    const quint64 halfInterval = (nextUs - point.timestamp_us) / 2ULL;
                    lowerBoundUs = point.timestamp_us > halfInterval ? point.timestamp_us - halfInterval : 0;
                    hasLowerBound = true;
                }
            }

            if (nextIndex >= 0)
            {
                const quint64 nextUs = rtk_track_points_.at(nextIndex).timestamp_us;
                if (nextUs > point.timestamp_us)
                {
                    upperBoundUs = midpointTimestamp(point.timestamp_us, nextUs);
                    hasUpperBound = true;
                }
            }
            if (!hasUpperBound && previousIndex >= 0)
            {
                const quint64 previousUs = rtk_track_points_.at(previousIndex).timestamp_us;
                if (previousUs > 0 && previousUs < point.timestamp_us)
                {
                    const quint64 halfInterval = (point.timestamp_us - previousUs) / 2ULL;
                    upperBoundUs = addClampedUs(point.timestamp_us, halfInterval);
                    hasUpperBound = true;
                }
            }

            if (hasLowerBound && hasUpperBound && lowerBoundUs < upperBoundUs)
            {
                const auto firstIt = std::lower_bound(waveform_timestamps_us_.cbegin(), waveform_timestamps_us_.cend(), lowerBoundUs);
                const auto lastIt = std::lower_bound(waveform_timestamps_us_.cbegin(), waveform_timestamps_us_.cend(), upperBoundUs);
                const int firstPeakIndex = static_cast<int>(std::distance(waveform_timestamps_us_.cbegin(), firstIt));
                const int lastPeakIndex = std::min(
                    static_cast<int>(std::distance(waveform_timestamps_us_.cbegin(), lastIt)),
                    static_cast<int>(waveform_peak_values_.size()));
                double sum = 0.0;
                int count = 0;
                for (int valueIndex = firstPeakIndex; valueIndex < lastPeakIndex; ++valueIndex)
                {
                    const float candidate = waveform_peak_values_.at(valueIndex);
                    if (std::isfinite(candidate))
                    {
                        sum += static_cast<double>(candidate);
                        ++count;
                    }
                }
                if (count > 0)
                {
                    peakValue = static_cast<float>(sum / static_cast<double>(count));
                }
            }
        }
        if (!std::isfinite(peakValue))
        {
            continue;
        }

        point.peak_value = peakValue;
        point.has_peak_value = true;
    }

    if (trajectory_viewer_dialog_)
    {
        trajectory_viewer_dialog_->setTrackStats(rtk_track_stats_);
        trajectory_viewer_dialog_->setTrackPoints(rtk_track_points_);
    }
}

void SessionViewerWindow::focusTrajectoryPoint(int trackPointIndex)
{
    if (trackPointIndex < 0 || trackPointIndex >= rtk_track_points_.size())
    {
        return;
    }

    const RtkTrackPoint& point = rtk_track_points_.at(trackPointIndex);
    if (point.has_waveform_match && point.waveform_frame_index >= 0)
    {
        const int frameValue = point.waveform_frame_index + 1;
        const bool frameInRange = frame_spin_ &&
            frameValue >= frame_spin_->minimum() &&
            frameValue <= frame_spin_->maximum();
        if (frameInRange)
        {
            const QSignalBlocker spinBlocker(frame_spin_);
            const QSignalBlocker sliderBlocker(frame_slider_);
            frame_spin_->setValue(frameValue);
            frame_slider_->setValue(frameValue);
        }
        loadWaveformFrame(static_cast<quint64>(point.waveform_frame_index));
    }
    else if (point.timestamp_us > 0)
    {
        highlightClosestSensorRow(point.timestamp_us, true);
    }

    setStatusText(QString(is_english_
        ? "Focused trajectory point #%1 at CSV row %2."
        : "已定位轨迹点 #%1，对应 CSV 第 %2 行。")
        .arg(trackPointIndex + 1)
        .arg(point.csv_row >= 0 ? point.csv_row + 1 : 0));
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
    if (csv_timestamps_us_.isEmpty() || !csv_model_ || csv_model_->rowCount() == 0)
    {
        primary_highlighted_csv_row_ = -1;
        if (csv_model_)
        {
            csv_model_->setHighlightedRows({}, -1, {});
        }
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
    primary_highlighted_csv_row_ = primaryRow;

    QStringList matchParts;
    QHash<int, QString> deltaTextByRow;
    for (int row : rowsToHighlight)
    {
        const qint64 deltaUs = static_cast<qint64>(csv_timestamps_us_.at(row)) - static_cast<qint64>(timestampUs);
        deltaTextByRow.insert(row, formatSignedDeltaMs(deltaUs));
        const QString rowText = fixedIntegerField(row + 1, 8);
        const QString deltaText = fixedTextField(formatSignedDeltaMs(deltaUs), 12);
        matchParts.append(is_english_
            ? QString("CSV row %1 (%2)").arg(rowText, deltaText)
            : QString("CSV 第%1行（%2）").arg(rowText, deltaText));
    }

    highlighted_csv_rows_ = rowsToHighlight;
    csv_model_->setHighlightedRows(highlighted_csv_rows_, primary_highlighted_csv_row_, deltaTextByRow);
    if (primaryRow >= 0)
    {
        static_cast<SingleSeriesTrendPlotWidget*>(temperature_plot_)->setCurrentIndex(primaryRow);
        static_cast<SingleSeriesTrendPlotWidget*>(humidity_plot_)->setCurrentIndex(primaryRow);
        static_cast<SingleSeriesTrendPlotWidget*>(pressure_plot_)->setCurrentIndex(primaryRow);
        if (scrollToCsvRow)
        {
            const int topVisibleRow = rowsToHighlight.isEmpty() ? primaryRow : rowsToHighlight.first();
            if (topVisibleRow >= 0 && topVisibleRow < csv_model_->rowCount())
            {
                csv_table_->scrollTo(csv_model_->index(topVisibleRow, 0), QAbstractItemView::PositionAtTop);
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
