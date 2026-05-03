#include "VaporViewBackends.h"

#include "serial_probe_utils.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSerialPortInfo>
#include <QSet>
#include <QStorageInfo>
#include <QStringConverter>
#include <QTextStream>
#include <QThread>
#include <QUrl>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace
{
constexpr int kPtbMinSampleRateHz = 1;
constexpr int kPtbMaxSampleRateHz = 70;
constexpr int kDefaultEpsilonSampleRateHz = 100;
constexpr int kDefaultPtbSampleRateHz = 20;
constexpr int kDefaultHmpSampleRateHz = 20;
constexpr int kDefaultLidarSampleRateHz = 100;
constexpr int kEpsilonPacketConfigApplyVersion = 2;
constexpr char kUnifiedRawMagic[8] = {'V', 'V', 'R', 'A', 'W', 'D', 'A', 'T'};
constexpr quint32 kUnifiedRawFormatVersion = 2u;
constexpr quint32 kUnifiedRawRecordMarker = 0x44525756u;
constexpr quint16 kRawSourceEpsilon = 1u;
constexpr quint16 kRawSourcePtb = 2u;
constexpr quint16 kRawSourceHmp = 3u;
constexpr quint16 kRawSourceLidar = 4u;
constexpr quint16 kRawSourceTcpWave = 5u;
constexpr quint16 kRawRecordTypeGeneric = 1u;
constexpr quint32 kRawTcpWaveCombinedPayloadFlag = 0x00000001u;
constexpr int kFloatSize = 4;
constexpr int kMaxTcpPayloadSize = 4 * 1024 * 1024;
constexpr int kLiveDisplayRefreshMs = 11;
constexpr int kPeakTrendFrameWindow = 1000;
constexpr int kDefaultPeakSearchStartIndex = 0;
constexpr int kDefaultPeakSearchEndIndex = 0;
constexpr int kCsvDragPreviewRows = 16;
constexpr int kCsvFullPreviewRows = 80;

enum PeakFilterMode
{
    PeakFilterNone = 0,
    PeakFilterIqrOutlier = 1,
    PeakFilterKeepRange = 2,
    PeakFilterExcludeRange = 3,
};

QString translatedTcpSocketMessage(const QString& message)
{
    if (message.contains(QStringLiteral("connection refused"), Qt::CaseInsensitive) ||
        message.contains(QStringLiteral("refused"), Qt::CaseInsensitive))
    {
        return message + QStringLiteral("；中文：连接被拒绝，请确认波形源或 mock_tcp_waveform_sender.py 已启动，主机和端口配置正确。");
    }
    return message;
}

QString chineseTcpSocketMessage(const QString& message)
{
    if (message.contains(QStringLiteral("connection refused"), Qt::CaseInsensitive) ||
        message.contains(QStringLiteral("refused"), Qt::CaseInsensitive))
    {
        return QStringLiteral("TCP 波形源连接被拒绝，请确认 mock_tcp_waveform_sender.py 已启动，主机和端口配置正确。");
    }
    if (message.contains(QStringLiteral("timed out"), Qt::CaseInsensitive) ||
        message.contains(QStringLiteral("timeout"), Qt::CaseInsensitive))
    {
        return QStringLiteral("TCP 波形源发送或连接超时，请检查波形源进程和网络状态。");
    }
    if (message.contains(QStringLiteral("host"), Qt::CaseInsensitive) &&
        message.contains(QStringLiteral("found"), Qt::CaseInsensitive))
    {
        return QStringLiteral("TCP 波形源主机不可达或无法解析，请检查 IP 地址。");
    }
    return QStringLiteral("TCP 波形源错误：%1").arg(message);
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

QVariantList decimateToVariantList(const QVector<float>& values, int maxSamples)
{
    QVariantList out;
    if (values.isEmpty() || maxSamples <= 0)
    {
        return out;
    }
    const int stride = std::max(1, static_cast<int>(std::ceil(static_cast<double>(values.size()) / maxSamples)));
    out.reserve(std::min(maxSamples + 1, static_cast<int>(values.size())));
    for (int i = 0; i < values.size(); i += stride)
    {
        out.append(values.at(i));
    }
    if ((values.size() - 1) % stride != 0)
    {
        out.append(values.constLast());
    }
    return out;
}

QVariantList decimateToVariantList(const QVector<double>& values, int maxSamples)
{
    QVariantList out;
    if (values.isEmpty() || maxSamples <= 0)
    {
        return out;
    }
    const int stride = std::max(1, static_cast<int>(std::ceil(static_cast<double>(values.size()) / maxSamples)));
    out.reserve(std::min(maxSamples + 1, static_cast<int>(values.size())));
    for (int i = 0; i < values.size(); i += stride)
    {
        out.append(values.at(i));
    }
    if ((values.size() - 1) % stride != 0)
    {
        out.append(values.constLast());
    }
    return out;
}

QString formatRateHz(double value)
{
    if (!std::isfinite(value) || value <= 0.0)
    {
        return QStringLiteral("---");
    }
    return value >= 10.0
        ? QStringLiteral("%1Hz").arg(QString::number(value, 'f', 0))
        : QStringLiteral("%1Hz").arg(QString::number(value, 'f', 1));
}

double secondsBetweenUs(quint64 startUs, quint64 endUs)
{
    if (startUs == 0 || endUs <= startUs)
    {
        return 0.0;
    }
    return static_cast<double>(endUs - startUs) / 1000000.0;
}

double summaryDurationSeconds(const QVariantMap& summary)
{
    const QDateTime start = QDateTime::fromString(summary.value(QStringLiteral("startTime")).toString(), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    const QDateTime end = QDateTime::fromString(summary.value(QStringLiteral("endTime")).toString(), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    if (start.isValid() && end.isValid() && end > start)
    {
        return static_cast<double>(start.secsTo(end));
    }

    const QStringList parts = summary.value(QStringLiteral("duration")).toString().split(QLatin1Char(':'));
    if (parts.size() == 3)
    {
        bool okH = false;
        bool okM = false;
        bool okS = false;
        const int hours = parts.at(0).toInt(&okH);
        const int minutes = parts.at(1).toInt(&okM);
        const int seconds = parts.at(2).toInt(&okS);
        if (okH && okM && okS)
        {
            return static_cast<double>(hours * 3600 + minutes * 60 + seconds);
        }
    }
    return 0.0;
}

void updateSessionSummaryCountText(QVariantMap& summary)
{
    const qlonglong waveformFrames = summary.value(QStringLiteral("waveformFrames")).toLongLong();
    const qlonglong sensorRows = summary.value(QStringLiteral("sensorRows")).toLongLong();
    const double durationSeconds = summaryDurationSeconds(summary);
    const QString waveformRate = durationSeconds > 0.0 && waveformFrames > 1
        ? formatRateHz(static_cast<double>(waveformFrames - 1) / durationSeconds)
        : summary.value(QStringLiteral("waveformRateHz"), QStringLiteral("---")).toString();
    const QString sensorRate = durationSeconds > 0.0 && sensorRows > 1
        ? formatRateHz(static_cast<double>(sensorRows - 1) / durationSeconds)
        : summary.value(QStringLiteral("sensorRateHz"), QStringLiteral("---")).toString();
    summary[QStringLiteral("waveformRateHz")] = waveformRate;
    summary[QStringLiteral("sensorRateHz")] = sensorRate;
    summary[QStringLiteral("framesText")] = QStringLiteral("%1 | %2").arg(waveformFrames).arg(sensorRows);
    summary[QStringLiteral("ratesText")] = QStringLiteral("%1 | %2").arg(waveformRate, sensorRate);
}

int normalizedPeakFilterMode(int mode)
{
    return mode >= PeakFilterNone && mode <= PeakFilterExcludeRange ? mode : PeakFilterNone;
}

QString peakFilterModeKey(int mode)
{
    switch (normalizedPeakFilterMode(mode))
    {
    case PeakFilterIqrOutlier: return QStringLiteral("iqr");
    case PeakFilterKeepRange: return QStringLiteral("keep_range");
    case PeakFilterExcludeRange: return QStringLiteral("exclude_range");
    case PeakFilterNone:
    default: return QStringLiteral("none");
    }
}

int peakFilterModeFromKey(QString mode)
{
    mode = mode.trimmed().toLower();
    if (mode == QStringLiteral("iqr")) return PeakFilterIqrOutlier;
    if (mode == QStringLiteral("keep") || mode == QStringLiteral("keep_range")) return PeakFilterKeepRange;
    if (mode == QStringLiteral("exclude") || mode == QStringLiteral("exclude_range")) return PeakFilterExcludeRange;
    return PeakFilterNone;
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

struct EpsilonPacketConfigOption
{
    quint8 packet_id = 0;
    const char *message_name = nullptr;
    const char *title_zh = nullptr;
    const char *title_en = nullptr;
    std::vector<int> supported_rates_hz;
};

const std::vector<EpsilonPacketConfigOption>& epsilonPacketConfigOptions()
{
    static const std::vector<EpsilonPacketConfigOption> kOptions = {
        {0x40, "MSG_IMU", "IMU原始数据", "IMU Raw Data", {0, 1, 2, 5, 10, 20, 50, 100, 200, 250, 500, 1000}},
        {0x41, "MSG_AHRS", "AHRS姿态解", "AHRS Attitude", {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x42, "MSG_INSGPS", "INS/GPS融合解", "INS/GPS Navigation", {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x50, "MSG_SYS_STATE", "系统状态", "System State", {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x59, "MSG_RAW_GNSS", "原始GNSS", "Raw GNSS", {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x5A, "MSG_SATELLITE", "卫星汇总", "Satellite Summary", {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x5C, "MSG_GEODETIC_POS", "大地坐标", "Geodetic Position", {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
        {0x5D, "MSG_ECEF_POS", "ECEF坐标", "ECEF Position", {0, 1, 2, 5, 10, 20, 50, 100, 250, 500}},
    };
    return kOptions;
}

QString epsilonPacketRateSettingsKey(quint8 packetId)
{
    return QStringLiteral("epsilon_custom_packet_rate_%1")
        .arg(packetId, 2, 16, QLatin1Char('0'))
        .toUpper();
}

int nearestSupportedEpsilonPacketRate(const EpsilonPacketConfigOption& option, int desiredRateHz)
{
    int fallbackRateHz = 0;
    for (int rateHz : option.supported_rates_hz)
    {
        if (rateHz == desiredRateHz)
        {
            return rateHz;
        }
        if (rateHz <= desiredRateHz)
        {
            fallbackRateHz = rateHz;
        }
    }
    return fallbackRateHz;
}

std::map<uint8_t, int> groupedEpsilonPacketRates(int baseRateHz)
{
    const int lowRateHz = std::min(baseRateHz, 20);
    std::map<uint8_t, int> rates;
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        const int desiredRateHz =
            (option.packet_id == 0x59 || option.packet_id == 0x5A ||
             option.packet_id == 0x5C || option.packet_id == 0x5D)
                ? lowRateHz
                : baseRateHz;
        rates[option.packet_id] = nearestSupportedEpsilonPacketRate(option, desiredRateHz);
    }
    return rates;
}

std::map<uint8_t, int> defaultEpsilonPacketRates()
{
    return {
        {0x40, 250}, {0x41, 50}, {0x42, 100}, {0x50, 100},
        {0x59, 10}, {0x5A, 1}, {0x5C, 10}, {0x5D, 10},
    };
}

bool epsilonPacketRateSupported(const EpsilonPacketConfigOption& option, int rateHz)
{
    return std::find(option.supported_rates_hz.cbegin(), option.supported_rates_hz.cend(), rateHz) != option.supported_rates_hz.cend();
}

std::map<uint8_t, int> loadCustomEpsilonPacketRates(QSettings& settings, int fallbackBaseRateHz)
{
    std::map<uint8_t, int> packetRates = groupedEpsilonPacketRates(fallbackBaseRateHz);
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        const int fallbackRate = packetRates[option.packet_id];
        const int storedRate = settings.value(epsilonPacketRateSettingsKey(option.packet_id), fallbackRate).toInt();
        packetRates[option.packet_id] = epsilonPacketRateSupported(option, storedRate) ? storedRate : fallbackRate;
    }
    return packetRates;
}

QString epsilonPacketRatesSummary(const std::map<uint8_t, int>& packetRates)
{
    QStringList parts;
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        const auto it = packetRates.find(option.packet_id);
        if (it != packetRates.end())
        {
            parts << QStringLiteral("%1=%2Hz")
                         .arg(option.packet_id, 2, 16, QLatin1Char('0'))
                         .toUpper()
                         .arg(it->second);
        }
    }
    return parts.join(QStringLiteral(", "));
}

int epsilonPacketCallbackRate(const std::map<uint8_t, int>& packetRates, int fallbackRateHz)
{
    int maxRateHz = 0;
    for (const auto& entry : packetRates)
    {
        maxRateHz = std::max(maxRateHz, entry.second);
    }
    return maxRateHz > 0 ? maxRateHz : fallbackRateHz;
}

int clampPtbSampleRate(int hz)
{
    return std::clamp(hz, kPtbMinSampleRateHz, kPtbMaxSampleRateHz);
}

QString timestampUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString sessionDirectoryTimestamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
}

QString csvEscape(const QString &value)
{
    QString escaped = value;
    escaped.replace('"', QStringLiteral("\"\""));
    if (escaped.contains(',') || escaped.contains('"') || escaped.contains('\n') || escaped.contains('\r'))
    {
        escaped = QStringLiteral("\"%1\"").arg(escaped);
    }
    return escaped;
}

QString csvBool(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

bool shouldMirrorToErrorLog(const QString& message)
{
    static const QStringList keywords = {
        QStringLiteral("error"), QStringLiteral("failed"), QStringLiteral("timeout"),
        QStringLiteral("exception"), QStringLiteral("disconnect"), QStringLiteral("异常"),
        QStringLiteral("失败"), QStringLiteral("错误"), QStringLiteral("超时"),
        QStringLiteral("掉线"), QStringLiteral("断开"),
    };
    const QString lower = message.toLower();
    for (const QString& keyword : keywords)
    {
        if (lower.contains(keyword.toLower()))
        {
            return true;
        }
    }
    return false;
}

QString formatBytes(qint64 bytes)
{
    if (bytes < 1024)
    {
        return QStringLiteral("%1 B").arg(bytes);
    }
    const double kb = static_cast<double>(bytes) / 1024.0;
    if (kb < 1024.0)
    {
        return QStringLiteral("%1 KB").arg(QString::number(kb, 'f', 1));
    }
    const double mb = kb / 1024.0;
    if (mb < 1024.0)
    {
        return QStringLiteral("%1 MB").arg(QString::number(mb, 'f', 1));
    }
    return QStringLiteral("%1 GB").arg(QString::number(mb / 1024.0, 'f', 2));
}

QStorageInfo storageInfoForPath(const QString& path)
{
    QString probe = path.trimmed();
    if (probe.isEmpty())
    {
        probe = QCoreApplication::applicationDirPath();
    }

    QFileInfo info(probe);
    QDir dir(info.isDir() ? info.absoluteFilePath() : info.absolutePath());
    while (!dir.exists() && dir.cdUp())
    {
    }

    QStorageInfo storage(dir.exists() ? dir.absolutePath() : QCoreApplication::applicationDirPath());
    storage.refresh();
    return storage;
}

QString formatDuration(qint64 seconds)
{
    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    const qint64 secs = seconds % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(secs, 2, 10, QLatin1Char('0'));
}

quint64 currentTimestampUs()
{
    return static_cast<quint64>(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) * 1000ULL;
}

QString jsonString(const QJsonObject& object, const QString& key, const QString& fallback = QString())
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : fallback;
}

quint64 jsonUInt64(const QJsonObject& object, const QString& key, quint64 fallback = 0)
{
    const QJsonValue value = object.value(key);
    if (value.isDouble())
    {
        return static_cast<quint64>(std::max(0.0, value.toDouble()));
    }
    if (value.isString())
    {
        bool ok = false;
        const quint64 parsed = value.toString().toULongLong(&ok);
        return ok ? parsed : fallback;
    }
    return fallback;
}

int jsonInt(const QJsonObject& object, const QString& key, int fallback = 0)
{
    return static_cast<int>(std::min<quint64>(jsonUInt64(object, key, static_cast<quint64>(std::max(0, fallback))),
                                             static_cast<quint64>(std::numeric_limits<int>::max())));
}

QVariantMap makeMetric(const QString& label, const QString& value, const QString& unit = QString())
{
    return QVariantMap{{QStringLiteral("label"), label}, {QStringLiteral("value"), value}, {QStringLiteral("unit"), unit}};
}

QString normalizeIconLibrary(const QString& library)
{
    QString normalized = library.trimmed().toLower();
    if (normalized == QStringLiteral("tabler icons"))
    {
        normalized = QStringLiteral("tabler");
    }
    else if (normalized == QStringLiteral("phosphor icons"))
    {
        normalized = QStringLiteral("phosphor");
    }
    if (normalized != QStringLiteral("tabler") && normalized != QStringLiteral("phosphor"))
    {
        normalized = QStringLiteral("lucide");
    }
    return normalized;
}
}  // namespace

AppBackend::AppBackend(QObject *parent)
    : QObject(parent)
    , language_(QStringLiteral("zh"))
    , dark_(false)
    , font_scale_(100)
{
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
    language_ = settings.value(QStringLiteral("ui_language"), QStringLiteral("zh")).toString();
    dark_ = settings.value(QStringLiteral("qml_dark"), false).toBool();
    font_scale_ = settings.value(QStringLiteral("font_scale_percent"), 100).toInt();
}

QString AppBackend::language() const { return language_; }
bool AppBackend::english() const { return language_ == QStringLiteral("en"); }
bool AppBackend::dark() const { return dark_; }
int AppBackend::fontScale() const { return font_scale_; }
QString AppBackend::version() const { return QStringLiteral("1.0.0"); }

void AppBackend::setLanguage(const QString& language)
{
    const QString normalized = language == QStringLiteral("en") ? QStringLiteral("en") : QStringLiteral("zh");
    if (language_ == normalized)
    {
        return;
    }
    language_ = normalized;
    QSettings(QStringLiteral("VaporView"), QStringLiteral("MainWindow")).setValue(QStringLiteral("ui_language"), language_);
    emit languageChanged();
}

void AppBackend::setEnglish(bool english)
{
    setLanguage(english ? QStringLiteral("en") : QStringLiteral("zh"));
}

void AppBackend::setDark(bool dark)
{
    if (dark_ == dark)
    {
        return;
    }
    dark_ = dark;
    QSettings(QStringLiteral("VaporView"), QStringLiteral("MainWindow")).setValue(QStringLiteral("qml_dark"), dark_);
    emit darkChanged();
}

void AppBackend::setFontScale(int percent)
{
    const int clamped = std::clamp(percent, 70, 150);
    if (font_scale_ == clamped)
    {
        return;
    }
    font_scale_ = clamped;
    QSettings(QStringLiteral("VaporView"), QStringLiteral("MainWindow")).setValue(QStringLiteral("font_scale_percent"), font_scale_);
    emit fontScaleChanged();
}

QString AppBackend::loadIconLibrary() const
{
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
    return normalizeIconLibrary(settings.value(QStringLiteral("icon_library"), QStringLiteral("lucide")).toString());
}

void AppBackend::saveIconLibrary(const QString& library)
{
    QSettings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"))
        .setValue(QStringLiteral("icon_library"), normalizeIconLibrary(library));
}

void AppBackend::toggleLanguage()
{
    setEnglish(!english());
}

void AppBackend::toggleTheme()
{
    setDark(!dark_);
}

void AppBackend::notify(const QString& level, const QString& message)
{
    emit notificationRequested(level, message);
}

QString AppBackend::t(const QString& key) const
{
    static const QHash<QString, QString> zh = {
        {"app.title", "VaporView"}, {"app.subtitle", "机载水汽检测系统"},
        {"topbar.session", "当前会话"}, {"topbar.systemOnline", "系统在线"}, {"topbar.partialOffline", "部分离线"},
        {"topbar.recording", "记录中"}, {"topbar.paused", "暂停"}, {"topbar.stopped", "未记录"},
        {"topbar.recordUsage", "记录占用"}, {"topbar.diskRemaining", "磁盘剩余"}, {"topbar.totalDisk", "磁盘总量"},
        {"topbar.connect", "连接"}, {"topbar.disconnect", "断开"}, {"topbar.cancel", "取消"},
        {"topbar.start", "开始记录"}, {"topbar.pause", "暂停"}, {"topbar.resume", "继续"}, {"topbar.stop", "结束记录"},
        {"nav.home", "首页"}, {"nav.devices", "设备"}, {"nav.detailedData", "详细数据"}, {"nav.waveform", "波形"},
        {"nav.sessions", "记录查看"}, {"nav.rtk", "RTK"}, {"nav.rawParser", "原始解析"}, {"nav.settings", "设置"},
        {"home.coordinateTitle", "坐标 / 姿态摘要"}, {"home.latitude", "纬度"}, {"home.longitude", "经度"},
        {"home.altitude", "高度"}, {"home.velocity", "速度"}, {"home.heading", "航向角"}, {"home.pitch", "俯仰角"},
        {"home.rtkStatus", "RTK 状态"}, {"home.satellites", "卫星数"}, {"home.gnssTime", "GNSS 时间"},
        {"home.localTime", "本地时间"},
        {"home.envTitle", "环境摘要"}, {"home.temperature", "温度"}, {"home.humidity", "湿度"},
        {"home.pressure", "气压"}, {"home.laserRange", "激光测距"}, {"home.recTitle", "记录 / 系统摘要"},
        {"home.recStatus", "记录状态"}, {"home.recDuration", "记录时长"}, {"home.recSize", "文件大小"},
        {"home.recFrames", "帧数"}, {"home.sysUptime", "系统运行"}, {"home.deviceStatus", "设备状态"},
        {"home.autoDetect", "自动识别"}, {"home.frameRate", "帧率"}, {"home.signalRange", "信号范围"},
        {"home.currTimestamp", "当前时间戳"}, {"home.filterStatus", "滤波状态"}, {"home.latestPeak", "最新峰值"},
        {"home.systemLog", "系统日志"}, {"home.clearLog", "清空"}, {"home.noLog", "暂无日志"},
        {"detailed.gnssGroup", "EPSILON / 导航数据"}, {"detailed.envGroup", "环境传感器"}, {"detailed.sysGroup", "系统状态"},
        {"devices.autoDetect", "自动识别"}, {"devices.refreshPorts", "刷新端口"}, {"devices.connectAll", "全部连接"},
        {"devices.disconnectAll", "全部断开"}, {"devices.epsilon", "EPSILON 组合导航"}, {"devices.ptb210", "PTB210 气压计"},
        {"devices.hmp", "HMP 温湿度"}, {"devices.tfa1500", "TFA1500-L 激光测距"}, {"devices.waveformSource", "波形源"},
        {"devices.connected", "已连接"}, {"devices.notConnected", "未连接"}, {"devices.ipAddress", "IP 地址"},
        {"devices.port", "端口"}, {"devices.baudRate", "波特率"}, {"devices.sampleRate", "采样频率"}, {"devices.receiveRate", "接收频率"},
        {"devices.connect", "连接"}, {"devices.test", "测试"}, {"devices.details", "详情"},
        {"devices.testNotAvailable", "该设备的独立测试入口尚未接入后端，真实连接能力请使用连接按钮。"},
        {"waveform.rawData", "原始数据"}, {"waveform.secondHarmonic", "归一化二次谐波"}, {"waveform.peakTrend", "峰值趋势"},
        {"waveform.controlPanel", "控制面板"}, {"waveform.filterSwitch", "滤波开关"}, {"waveform.cutoffFreq", "峰值范围"},
        {"waveform.recordFreq", "记录频率"}, {"waveform.previewControl", "预览控制"}, {"waveform.export", "导出"},
        {"waveform.peakValue", "峰值"}, {"sessions.sessionTable", "记录列表"}, {"sessions.detailPanel", "详情面板"},
        {"sessions.sessionInfo", "记录信息"}, {"sessions.csvPreview", "CSV 预览"}, {"sessions.waveformPreview", "波形预览"},
        {"sessions.exportTab", "导出"}, {"sessions.name", "名称"}, {"sessions.date", "日期"},
        {"sessions.duration", "时长"}, {"sessions.size", "大小"}, {"sessions.frames", "帧数"}, {"sessions.status", "状态"},
        {"rtk.ntripConfig", "NTRIP 配置"}, {"rtk.diagnostics", "诊断信息"}, {"rtk.casterAddress", "Caster 地址"},
        {"rtk.port", "端口"}, {"rtk.mountPoint", "Mount Point"}, {"rtk.username", "用户名"}, {"rtk.password", "密码"},
        {"rtk.ggaSource", "GGA 来源"}, {"rtk.testConnection", "测试连接"}, {"rtk.saveConfig", "保存配置"},
        {"rtk.rtcmThroughput", "RTCM 吞吐量"}, {"rtk.ggaUpdateTime", "GGA 更新时间"}, {"rtk.diffStatus", "差分状态"},
        {"rtk.latency", "延迟"}, {"rtk.diagLog", "诊断日志"}, {"rtk.connected", "已连接"},
        {"rawParser.dropZone", "选择原始文件或会话目录"}, {"rawParser.parseRecords", "解析记录"},
        {"rawParser.formatInfo", "格式说明"}, {"rawParser.fieldName", "字段名"}, {"rawParser.fieldType", "类型"},
        {"rawParser.export", "导出"}, {"rawParser.clearAll", "清空全部"}, {"rawParser.records", "条记录"},
        {"settings.languageTheme", "语言与主题"}, {"settings.language", "界面语言"}, {"settings.theme", "主题"},
        {"settings.light", "亮色"}, {"settings.dark", "暗色"}, {"settings.recordDir", "记录目录"},
        {"settings.browse", "浏览"}, {"settings.defaultSampleRate", "默认采样率"}, {"settings.displayDensity", "显示密度"},
        {"settings.fontScale", "字体比例"}, {"settings.advancedDiag", "高级诊断"}, {"settings.about", "关于"},
        {"settings.iconLibrary", "图标库"},
        {"settings.aboutText", "VaporView 机载水汽检测系统"}, {"settings.version", "版本"}, {"settings.save", "保存"},
        {"settings.reset", "重置"}, {"unit.m", "m"}, {"unit.ms", "m/s"}, {"unit.deg", "°"}, {"unit.celsius", "°C"},
        {"unit.percent", "%"}, {"unit.kpa", "kPa"}, {"unit.hz", "Hz"}, {"unit.v", "V"}, {"unit.gb", "GB"},
        {"unit.mb", "MB"}, {"unit.kbps", "kbps"}, {"unit.s", "s"},
    };
    static const QHash<QString, QString> en = {
        {"app.title", "VaporView"}, {"app.subtitle", "Airborne Water Vapor Detection"},
        {"topbar.session", "Current Session"}, {"topbar.systemOnline", "System Online"}, {"topbar.partialOffline", "Partial Offline"},
        {"topbar.recording", "Recording"}, {"topbar.paused", "Paused"}, {"topbar.stopped", "Not Recording"},
        {"topbar.recordUsage", "Rec Usage"}, {"topbar.diskRemaining", "Disk Free"}, {"topbar.totalDisk", "Total Disk"},
        {"topbar.connect", "Connect"}, {"topbar.disconnect", "Disconnect"}, {"topbar.cancel", "Cancel"},
        {"topbar.start", "Start Recording"}, {"topbar.pause", "Pause"}, {"topbar.resume", "Resume"}, {"topbar.stop", "Stop Recording"},
        {"nav.home", "Home"}, {"nav.devices", "Devices"}, {"nav.detailedData", "Detailed Data"}, {"nav.waveform", "Waveform"},
        {"nav.sessions", "Record View"}, {"nav.rtk", "RTK"}, {"nav.rawParser", "Raw Parser"}, {"nav.settings", "Settings"},
        {"home.coordinateTitle", "Position & Attitude"}, {"home.latitude", "Latitude"}, {"home.longitude", "Longitude"},
        {"home.altitude", "Altitude"}, {"home.velocity", "Velocity"}, {"home.heading", "Heading"}, {"home.pitch", "Pitch"},
        {"home.rtkStatus", "RTK Status"}, {"home.satellites", "Satellites"}, {"home.gnssTime", "GNSS Time"},
        {"home.localTime", "Local Time"},
        {"home.envTitle", "Environment"}, {"home.temperature", "Temperature"}, {"home.humidity", "Humidity"},
        {"home.pressure", "Pressure"}, {"home.laserRange", "Laser Range"}, {"home.recTitle", "Recording & System"},
        {"home.recStatus", "Rec Status"}, {"home.recDuration", "Duration"}, {"home.recSize", "File Size"},
        {"home.recFrames", "Frames"}, {"home.sysUptime", "Uptime"}, {"home.deviceStatus", "Device Status"},
        {"home.autoDetect", "Auto Detect"}, {"home.frameRate", "Frame Rate"}, {"home.signalRange", "Signal Range"},
        {"home.currTimestamp", "Timestamp"}, {"home.filterStatus", "Filter"}, {"home.latestPeak", "Latest Peak"},
        {"home.systemLog", "System Log"}, {"home.clearLog", "Clear"}, {"home.noLog", "No logs"},
        {"detailed.gnssGroup", "EPSILON / Navigation"}, {"detailed.envGroup", "Environment Sensors"}, {"detailed.sysGroup", "System Status"},
        {"devices.autoDetect", "Auto Detect"}, {"devices.refreshPorts", "Refresh Ports"}, {"devices.connectAll", "Connect All"},
        {"devices.disconnectAll", "Disconnect All"}, {"devices.epsilon", "EPSILON Navigation"}, {"devices.ptb210", "PTB210 Barometer"},
        {"devices.hmp", "HMP Temp/Humid"}, {"devices.tfa1500", "TFA1500-L Rangefinder"}, {"devices.waveformSource", "Waveform Source"},
        {"devices.connected", "Connected"}, {"devices.notConnected", "Not Connected"}, {"devices.ipAddress", "IP Address"},
        {"devices.port", "Port"}, {"devices.baudRate", "Baud Rate"}, {"devices.sampleRate", "Sample Rate"}, {"devices.receiveRate", "Receive Rate"},
        {"devices.connect", "Connect"}, {"devices.test", "Test"}, {"devices.details", "Details"},
        {"devices.testNotAvailable", "Per-device test is not wired to the backend yet. Use Connect for the real device workflow."},
        {"waveform.rawData", "Raw Data"}, {"waveform.secondHarmonic", "Normalized Second Harmonic"}, {"waveform.peakTrend", "Peak Trend"},
        {"waveform.controlPanel", "Control Panel"}, {"waveform.filterSwitch", "Filter"}, {"waveform.cutoffFreq", "Peak Range"},
        {"waveform.recordFreq", "Record Freq"}, {"waveform.previewControl", "Preview"}, {"waveform.export", "Export"},
        {"waveform.peakValue", "Peak"}, {"sessions.sessionTable", "Record List"}, {"sessions.detailPanel", "Details"},
        {"sessions.sessionInfo", "Record Info"}, {"sessions.csvPreview", "CSV Preview"}, {"sessions.waveformPreview", "Waveform Preview"},
        {"sessions.exportTab", "Export"}, {"sessions.name", "Name"}, {"sessions.date", "Date"},
        {"sessions.duration", "Duration"}, {"sessions.size", "Size"}, {"sessions.frames", "Frames"}, {"sessions.status", "Status"},
        {"rtk.ntripConfig", "NTRIP Config"}, {"rtk.diagnostics", "Diagnostics"}, {"rtk.casterAddress", "Caster Address"},
        {"rtk.port", "Port"}, {"rtk.mountPoint", "Mount Point"}, {"rtk.username", "Username"}, {"rtk.password", "Password"},
        {"rtk.ggaSource", "GGA Source"}, {"rtk.testConnection", "Test Connection"}, {"rtk.saveConfig", "Save Config"},
        {"rtk.rtcmThroughput", "RTCM Throughput"}, {"rtk.ggaUpdateTime", "GGA Update Time"}, {"rtk.diffStatus", "Diff Status"},
        {"rtk.latency", "Latency"}, {"rtk.diagLog", "Diag Log"}, {"rtk.connected", "Connected"},
        {"rawParser.dropZone", "Choose raw file or session directory"}, {"rawParser.parseRecords", "Parsed Records"},
        {"rawParser.formatInfo", "Format Info"}, {"rawParser.fieldName", "Field Name"}, {"rawParser.fieldType", "Type"},
        {"rawParser.export", "Export"}, {"rawParser.clearAll", "Clear All"}, {"rawParser.records", "records"},
        {"settings.languageTheme", "Language & Theme"}, {"settings.language", "Language"}, {"settings.theme", "Theme"},
        {"settings.light", "Light"}, {"settings.dark", "Dark"}, {"settings.recordDir", "Record Directory"},
        {"settings.browse", "Browse"}, {"settings.defaultSampleRate", "Default Sample Rate"}, {"settings.displayDensity", "Display Density"},
        {"settings.fontScale", "Font Scale"}, {"settings.advancedDiag", "Advanced Diag"}, {"settings.about", "About"},
        {"settings.iconLibrary", "Icon Library"},
        {"settings.aboutText", "VaporView Airborne System"}, {"settings.version", "Version"}, {"settings.save", "Save"},
        {"settings.reset", "Reset"}, {"unit.m", "m"}, {"unit.ms", "m/s"}, {"unit.deg", "°"}, {"unit.celsius", "°C"},
        {"unit.percent", "%"}, {"unit.kpa", "kPa"}, {"unit.hz", "Hz"}, {"unit.v", "V"}, {"unit.gb", "GB"},
        {"unit.mb", "MB"}, {"unit.kbps", "kbps"}, {"unit.s", "s"},
    };
    const auto& dict = english() ? en : zh;
    return dict.value(key, key);
}

DeviceModel::DeviceModel(QObject *parent)
    : QAbstractListModel(parent)
{
    devices_ = {
        {QStringLiteral("epsilon"), QStringLiteral("devices.epsilon"), QStringLiteral("EPSILON"), QStringLiteral("COM3"), 921600, kDefaultEpsilonSampleRateHz, false, false, 0.0, QStringLiteral("Not connected"), QStringLiteral("serial")},
        {QStringLiteral("ptb"), QStringLiteral("devices.ptb210"), QStringLiteral("PTB210"), QStringLiteral("COM5"), 9600, kDefaultPtbSampleRateHz, false, false, 0.0, QStringLiteral("Not connected"), QStringLiteral("serial")},
        {QStringLiteral("hmp"), QStringLiteral("devices.hmp"), QStringLiteral("HMP3"), QStringLiteral("COM6"), 19200, kDefaultHmpSampleRateHz, false, false, 0.0, QStringLiteral("Not connected"), QStringLiteral("serial")},
        {QStringLiteral("lidar"), QStringLiteral("devices.tfa1500"), QStringLiteral("TFA1500-L"), QStringLiteral("COM7"), 500000, kDefaultLidarSampleRateHz, false, false, 0.0, QStringLiteral("Not connected"), QStringLiteral("serial")},
        {QStringLiteral("waveform"), QStringLiteral("devices.waveformSource"), QStringLiteral("TCP Wave"), QStringLiteral("127.0.0.1:8888"), 0, 20, false, false, 0.0, QStringLiteral("Not connected"), QStringLiteral("tcp")},
    };
}

int DeviceModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : devices_.size();
}

QVariant DeviceModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= devices_.size())
    {
        return {};
    }
    const Device& d = devices_.at(index.row());
    switch (role)
    {
    case IdRole: return d.id;
    case NameKeyRole: return d.name_key;
    case DisplayNameRole: return d.display_name;
    case PortRole: return d.port;
    case BaudRateRole: return d.baud_rate;
    case SampleRateRole: return d.sample_rate;
    case ConnectedRole: return d.connected;
    case OnlineRole: return d.online;
    case ActualRateRole: return d.actual_rate;
    case StatusTextRole: return d.status_text;
    case KindRole: return d.kind;
    default: return {};
    }
}

QHash<int, QByteArray> DeviceModel::roleNames() const
{
    return {
        {IdRole, "id"}, {NameKeyRole, "nameKey"}, {DisplayNameRole, "displayName"},
        {PortRole, "port"}, {BaudRateRole, "baudRate"}, {SampleRateRole, "sampleRate"},
        {ConnectedRole, "connected"}, {OnlineRole, "online"}, {ActualRateRole, "actualRate"},
        {StatusTextRole, "statusText"}, {KindRole, "kind"},
    };
}

QVariantMap DeviceModel::get(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= devices_.size())
    {
        return map;
    }
    const QModelIndex idx = index(row);
    for (auto it = roleNames().cbegin(); it != roleNames().cend(); ++it)
    {
        map[QString::fromLatin1(it.value())] = data(idx, it.key());
    }
    return map;
}

int DeviceModel::indexOf(const QString& id) const
{
    for (int i = 0; i < devices_.size(); ++i)
    {
        if (devices_.at(i).id == id)
        {
            return i;
        }
    }
    return -1;
}

DeviceModel::Device DeviceModel::deviceAt(const QString& id) const
{
    const int row = indexOf(id);
    return row >= 0 ? devices_.at(row) : Device{};
}

QList<DeviceModel::Device> DeviceModel::devices() const
{
    return QList<Device>(devices_.cbegin(), devices_.cend());
}

void DeviceModel::setDevice(const QString& id, const Device& device)
{
    const int row = indexOf(id);
    if (row < 0)
    {
        return;
    }
    devices_[row] = device;
    emit dataChanged(index(row), index(row), roleNames().keys());
}

void DeviceModel::setDeviceValue(const QString& id, int role, const QVariant& value)
{
    const int row = indexOf(id);
    if (row < 0)
    {
        return;
    }
    Device& d = devices_[row];
    switch (role)
    {
    case PortRole: d.port = value.toString(); break;
    case BaudRateRole: d.baud_rate = value.toInt(); break;
    case SampleRateRole: d.sample_rate = value.toInt(); break;
    case ConnectedRole: d.connected = value.toBool(); break;
    case OnlineRole: d.online = value.toBool(); break;
    case ActualRateRole: d.actual_rate = value.toDouble(); break;
    case StatusTextRole: d.status_text = value.toString(); break;
    default: return;
    }
    emit dataChanged(index(row), index(row), {role});
}

DeviceBackend::DeviceBackend(QObject *parent)
    : QObject(parent)
    , is_english_(false)
    , connected_(false)
    , connection_in_progress_(false)
    , auto_detect_in_progress_(false)
    , epsilon_reconfigure_in_progress_(false)
    , status_text_(QStringLiteral("Ready"))
    , progress_value_(0)
    , progress_maximum_(1)
    , cancel_requested_(false)
{
    loadDeviceSettings();
    refreshPorts();
    connect(&refresh_timer_, &QTimer::timeout, this, [this]() {
        updateDeviceRates();
        emit dataChanged();
    });
    refresh_timer_.start(250);
}

DeviceBackend::~DeviceBackend()
{
    cancel_requested_.store(true);
    if (detect_thread_.joinable()) detect_thread_.join();
    if (connection_thread_.joinable()) connection_thread_.join();
    if (reconfigure_thread_.joinable()) reconfigure_thread_.join();
    stopAllCollectors();
    saveDeviceSettings();
}

DeviceModel *DeviceBackend::devices() { return &devices_; }
QStringList DeviceBackend::ports() const { return ports_; }
QStringList DeviceBackend::logLines() const { return log_lines_; }
bool DeviceBackend::connected() const { return connected_; }
bool DeviceBackend::busy() const { return connection_in_progress_ || auto_detect_in_progress_ || epsilon_reconfigure_in_progress_; }
bool DeviceBackend::connectionInProgress() const { return connection_in_progress_; }
bool DeviceBackend::autoDetectInProgress() const { return auto_detect_in_progress_; }
QString DeviceBackend::statusText() const { return status_text_; }
int DeviceBackend::progressValue() const { return progress_value_; }
int DeviceBackend::progressMaximum() const { return progress_maximum_; }

QVariantMap DeviceBackend::coordinateData() const
{
    QMutexLocker lock(&data_mutex_);
    const auto e = current_epsilon_;
    const bool utcValid = e.utc_unix_s > 0;
    const qint64 utcMs = utcValid
        ? static_cast<qint64>(e.utc_unix_s) * 1000 + static_cast<qint64>(e.utc_microseconds / 1000)
        : 0;
    const QDateTime utcDateTime = utcValid
        ? QDateTime::fromMSecsSinceEpoch(utcMs, Qt::UTC)
        : QDateTime();
    return {
        {QStringLiteral("latitude"), e.latitude_deg},
        {QStringLiteral("longitude"), e.longitude_deg},
        {QStringLiteral("altitude"), e.height_m},
        {QStringLiteral("velocity"), std::hypot(e.vel_n_mps, e.vel_e_mps)},
        {QStringLiteral("heading"), e.yaw_deg},
        {QStringLiteral("pitch"), e.pitch_deg},
        {QStringLiteral("roll"), e.roll_deg},
        {QStringLiteral("rtkStatus"), QString::fromStdString(e.gnss_fix_text)},
        {QStringLiteral("satellites"), e.gnss_satellites},
        {QStringLiteral("ppsLocked"), utcValid},
        {QStringLiteral("timestamp"), utcValid
            ? utcDateTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
            : QStringLiteral("---")},
        {QStringLiteral("localTime"), QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"))},
        {QStringLiteral("valid"), e.valid},
    };
}

QVariantMap DeviceBackend::environmentData() const
{
    QMutexLocker lock(&data_mutex_);
    return {
        {QStringLiteral("temperature"), current_hmp_.temperature},
        {QStringLiteral("humidity"), current_hmp_.humidity},
        {QStringLiteral("pressure"), current_ptb_.pressure_hpa},
        {QStringLiteral("laserRange"), current_lidar_.distance_m},
        {QStringLiteral("laserStrength"), current_lidar_.signal_strength},
        {QStringLiteral("valid"), current_hmp_.valid || current_ptb_.valid || current_lidar_.valid},
    };
}

QVariantMap DeviceBackend::detailedData() const
{
    QMutexLocker lock(&data_mutex_);
    const auto e = current_epsilon_;
    QVariantList nav = {
        makeMetric(QStringLiteral("Lat"), QString::number(e.latitude_deg, 'f', 9), QStringLiteral("deg")),
        makeMetric(QStringLiteral("Lon"), QString::number(e.longitude_deg, 'f', 9), QStringLiteral("deg")),
        makeMetric(QStringLiteral("Height"), QString::number(e.height_m, 'f', 3), QStringLiteral("m")),
        makeMetric(QStringLiteral("Vel N"), QString::number(e.vel_n_mps, 'f', 3), QStringLiteral("m/s")),
        makeMetric(QStringLiteral("Vel E"), QString::number(e.vel_e_mps, 'f', 3), QStringLiteral("m/s")),
        makeMetric(QStringLiteral("Vel D"), QString::number(e.vel_d_mps, 'f', 3), QStringLiteral("m/s")),
        makeMetric(QStringLiteral("Roll"), QString::number(e.roll_deg, 'f', 3), QStringLiteral("deg")),
        makeMetric(QStringLiteral("Pitch"), QString::number(e.pitch_deg, 'f', 3), QStringLiteral("deg")),
        makeMetric(QStringLiteral("Yaw"), QString::number(e.yaw_deg, 'f', 3), QStringLiteral("deg")),
        makeMetric(QStringLiteral("Quat W"), QString::number(e.quat_w, 'f', 6)),
        makeMetric(QStringLiteral("Quat X"), QString::number(e.quat_x, 'f', 6)),
        makeMetric(QStringLiteral("Quat Y"), QString::number(e.quat_y, 'f', 6)),
        makeMetric(QStringLiteral("Quat Z"), QString::number(e.quat_z, 'f', 6)),
        makeMetric(QStringLiteral("GNSS Fix"), QString::fromStdString(e.gnss_fix_text)),
        makeMetric(QStringLiteral("Satellites"), QString::number(e.gnss_satellites)),
        makeMetric(QStringLiteral("HDOP"), QString::number(e.hdop, 'f', 2)),
        makeMetric(QStringLiteral("VDOP"), QString::number(e.vdop, 'f', 2)),
        makeMetric(QStringLiteral("Diff Age"), QString::number(e.diff_age_s, 'f', 2), QStringLiteral("s")),
    };
    QVariantList env = {
        makeMetric(QStringLiteral("Temperature"), QString::number(current_hmp_.temperature, 'f', 2), QStringLiteral("C")),
        makeMetric(QStringLiteral("Humidity"), QString::number(current_hmp_.humidity, 'f', 2), QStringLiteral("%RH")),
        makeMetric(QStringLiteral("Pressure"), QString::number(current_ptb_.pressure_hpa, 'f', 2), QStringLiteral("hPa")),
        makeMetric(QStringLiteral("Laser Range"), QString::number(current_lidar_.distance_m, 'f', 3), QStringLiteral("m")),
        makeMetric(QStringLiteral("Laser Strength"), QString::number(current_lidar_.signal_strength)),
        makeMetric(QStringLiteral("EPSILON Valid"), csvBool(e.valid)),
    };
    return {{QStringLiteral("nav"), nav}, {QStringLiteral("env"), env}};
}

QVariantMap DeviceBackend::systemData() const
{
    return {
        {QStringLiteral("connected"), connected_},
        {QStringLiteral("busy"), busy()},
        {QStringLiteral("status"), status_text_},
        {QStringLiteral("ports"), ports_.size()},
    };
}

VaporView::EpsilonData DeviceBackend::epsilonData() const
{
    QMutexLocker lock(&data_mutex_);
    return current_epsilon_;
}

VaporView::PtbData DeviceBackend::ptbData() const
{
    QMutexLocker lock(&data_mutex_);
    return current_ptb_;
}

VaporView::HmpData DeviceBackend::hmpData() const
{
    QMutexLocker lock(&data_mutex_);
    return current_hmp_;
}

VaporView::LidarData DeviceBackend::lidarData() const
{
    QMutexLocker lock(&data_mutex_);
    return current_lidar_;
}

double DeviceBackend::collectorActualRate(const QString& id) const
{
    const CollectorSnapshot snapshot = snapshotCollectors();
    if (id == QStringLiteral("epsilon") && snapshot.epsilon) return snapshot.epsilon->getActualRate();
    if (id == QStringLiteral("ptb") && snapshot.ptb) return snapshot.ptb->getActualRate();
    if (id == QStringLiteral("hmp") && snapshot.hmp) return snapshot.hmp->getActualRate();
    if (id == QStringLiteral("lidar") && snapshot.lidar) return snapshot.lidar->getActualRate();
    return 0.0;
}

bool DeviceBackend::anySerialCollectorRunning() const
{
    const CollectorSnapshot snapshot = snapshotCollectors();
    return (snapshot.epsilon && snapshot.epsilon->isRunning()) ||
           (snapshot.ptb && snapshot.ptb->isRunning()) ||
           (snapshot.hmp && snapshot.hmp->isRunning()) ||
           (snapshot.lidar && snapshot.lidar->isRunning());
}

QString DeviceBackend::selectedPort(const QString& id) const { return devices_.deviceAt(id).port.trimmed(); }
int DeviceBackend::selectedBaud(const QString& id) const { return devices_.deviceAt(id).baud_rate; }
int DeviceBackend::selectedSampleRate(const QString& id) const { return devices_.deviceAt(id).sample_rate; }
QVariantMap DeviceBackend::device(int row) const { return devices_.get(row); }

QString DeviceBackend::selectPlaceholder() const
{
    return is_english_ ? QStringLiteral("-- Select --") : QStringLiteral("-- 选择 --");
}

void DeviceBackend::refreshPorts()
{
    QStringList ports;
    const auto infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo& info : infos)
    {
        ports.append(info.portName());
    }
    ports.removeDuplicates();
    ports.sort();
    ports_ = ports;
    emit portsChanged();
    log(QString(is_english_ ? "Ports refreshed: %1 serial ports" : "端口已刷新: %1 个串口").arg(ports_.size()));
}

void DeviceBackend::clearLog()
{
    log_lines_.clear();
    emit logLinesChanged();
}

void DeviceBackend::appendLogLine(const QString& message, const QString& level)
{
    Q_UNUSED(level);
    const QString line = QStringLiteral("[%1] %2").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), message);
    log_lines_.append(line);
    while (log_lines_.size() > 600)
    {
        log_lines_.removeFirst();
    }
    emit logLinesChanged();
}

void DeviceBackend::updateDevicePort(const QString& id, const QString& port)
{
    devices_.setDeviceValue(id, DeviceModel::PortRole, port);
    saveDeviceSettings();
}

void DeviceBackend::updateDeviceBaud(const QString& id, int baud)
{
    devices_.setDeviceValue(id, DeviceModel::BaudRateRole, baud);
    saveDeviceSettings();
}

void DeviceBackend::updateDeviceSampleRate(const QString& id, int hz)
{
    devices_.setDeviceValue(id, DeviceModel::SampleRateRole, parseRateForDevice(id, hz));
    saveDeviceSettings();
}

void DeviceBackend::setWaveformDeviceState(bool connected, const QString& endpoint, double hz)
{
    devices_.setDeviceValue(QStringLiteral("waveform"), DeviceModel::ConnectedRole, connected);
    devices_.setDeviceValue(QStringLiteral("waveform"), DeviceModel::OnlineRole, connected);
    devices_.setDeviceValue(QStringLiteral("waveform"), DeviceModel::PortRole, endpoint);
    devices_.setDeviceValue(QStringLiteral("waveform"), DeviceModel::ActualRateRole, hz);
    devices_.setDeviceValue(QStringLiteral("waveform"), DeviceModel::StatusTextRole, connected ? QStringLiteral("Connected") : QStringLiteral("Not connected"));
}

void DeviceBackend::setEnglish(bool english)
{
    is_english_ = english;
}

void DeviceBackend::log(const QString& message, const QString& level)
{
    appendLogLine(message, level);
    emit notificationRequested(level, message);
}

void DeviceBackend::setBusyState(bool connectionBusy, bool detectBusy, bool reconfigureBusy)
{
    const bool oldBusy = busy();
    connection_in_progress_ = connectionBusy;
    auto_detect_in_progress_ = detectBusy;
    epsilon_reconfigure_in_progress_ = reconfigureBusy;
    if (oldBusy != busy())
    {
        emit busyChanged();
    }
    else
    {
        emit busyChanged();
    }
}

void DeviceBackend::setStatusText(const QString& text)
{
    if (status_text_ == text)
    {
        return;
    }
    status_text_ = text;
    emit statusTextChanged();
}

void DeviceBackend::setProgress(int value, int maximum)
{
    progress_value_ = value;
    progress_maximum_ = std::max(1, maximum);
    emit progressChanged();
}

bool DeviceBackend::shouldAbort() const
{
    return cancel_requested_.load();
}

int DeviceBackend::parseRateForDevice(const QString& id, int hz) const
{
    if (id == QStringLiteral("epsilon")) return std::clamp(hz, 20, 200);
    if (id == QStringLiteral("ptb")) return clampPtbSampleRate(hz);
    if (id == QStringLiteral("lidar")) return std::clamp(hz, 1, 100);
    return std::clamp(hz, 1, 1000);
}

QStringList DeviceBackend::supportedBaudRates() const
{
    return {QStringLiteral("9600"), QStringLiteral("19200"), QStringLiteral("38400"), QStringLiteral("57600"),
            QStringLiteral("115200"), QStringLiteral("230400"), QStringLiteral("460800"), QStringLiteral("500000"),
            QStringLiteral("921600")};
}

QStringList DeviceBackend::supportedRates(const QString& id) const
{
    if (id == QStringLiteral("ptb")) return {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("5"), QStringLiteral("10"), QStringLiteral("20"), QStringLiteral("50"), QStringLiteral("70")};
    if (id == QStringLiteral("epsilon")) return {QStringLiteral("20"), QStringLiteral("50"), QStringLiteral("100"), QStringLiteral("200")};
    if (id == QStringLiteral("lidar")) return {QStringLiteral("1"), QStringLiteral("5"), QStringLiteral("10"), QStringLiteral("20"), QStringLiteral("50"), QStringLiteral("100")};
    return {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("5"), QStringLiteral("10"), QStringLiteral("20"), QStringLiteral("50"), QStringLiteral("100"), QStringLiteral("200"), QStringLiteral("500"), QStringLiteral("1000")};
}

DeviceBackend::CollectorSnapshot DeviceBackend::snapshotCollectors() const
{
    std::lock_guard<std::mutex> lock(collector_mutex_);
    return collectors_;
}

void DeviceBackend::setCollectors(CollectorSnapshot collectors)
{
    std::lock_guard<std::mutex> lock(collector_mutex_);
    collectors_ = std::move(collectors);
}

void DeviceBackend::stopAllCollectors()
{
    CollectorSnapshot collectors;
    {
        std::lock_guard<std::mutex> lock(collector_mutex_);
        collectors = std::move(collectors_);
        collectors_ = {};
    }
    if (collectors.epsilon) collectors.epsilon->stop();
    if (collectors.ptb) collectors.ptb->stop();
    if (collectors.hmp) collectors.hmp->stop();
    if (collectors.lidar) collectors.lidar->stop();
    for (const QString& id : {QStringLiteral("epsilon"), QStringLiteral("ptb"), QStringLiteral("hmp"), QStringLiteral("lidar")})
    {
        devices_.setDeviceValue(id, DeviceModel::ConnectedRole, false);
        devices_.setDeviceValue(id, DeviceModel::OnlineRole, false);
        devices_.setDeviceValue(id, DeviceModel::StatusTextRole, QStringLiteral("Not connected"));
        devices_.setDeviceValue(id, DeviceModel::ActualRateRole, 0.0);
    }
    connected_ = false;
    emit connectionStateChanged();
}

bool DeviceBackend::stopCollector(const QString& id)
{
    std::shared_ptr<VaporView::EpsilonCollector> epsilon;
    std::shared_ptr<VaporView::PtbCollector> ptb;
    std::shared_ptr<VaporView::HmpCollector> hmp;
    std::shared_ptr<VaporView::LidarCollector> lidar;
    {
        std::lock_guard<std::mutex> lock(collector_mutex_);
        if (id == QStringLiteral("epsilon"))
        {
            epsilon = std::move(collectors_.epsilon);
        }
        else if (id == QStringLiteral("ptb"))
        {
            ptb = std::move(collectors_.ptb);
        }
        else if (id == QStringLiteral("hmp"))
        {
            hmp = std::move(collectors_.hmp);
        }
        else if (id == QStringLiteral("lidar"))
        {
            lidar = std::move(collectors_.lidar);
        }
    }
    const bool hadCollector = epsilon || ptb || hmp || lidar;
    if (epsilon) epsilon->stop();
    if (ptb) ptb->stop();
    if (hmp) hmp->stop();
    if (lidar) lidar->stop();
    devices_.setDeviceValue(id, DeviceModel::ConnectedRole, false);
    devices_.setDeviceValue(id, DeviceModel::OnlineRole, false);
    devices_.setDeviceValue(id, DeviceModel::StatusTextRole, QStringLiteral("Not connected"));
    devices_.setDeviceValue(id, DeviceModel::ActualRateRole, 0.0);
    connected_ = anySerialCollectorRunning();
    emit connectionStateChanged();
    return hadCollector;
}

void DeviceBackend::updateDeviceRates()
{
    for (const QString& id : {QStringLiteral("epsilon"), QStringLiteral("ptb"), QStringLiteral("hmp"), QStringLiteral("lidar")})
    {
        devices_.setDeviceValue(id, DeviceModel::ActualRateRole, collectorActualRate(id));
    }
}

void DeviceBackend::saveDeviceSettings() const
{
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
    for (const auto& device : devices_.devices())
    {
        if (device.kind != QStringLiteral("serial"))
        {
            continue;
        }
        settings.setValue(QStringLiteral("%1_port").arg(device.id), device.port);
        settings.setValue(QStringLiteral("%1_baud").arg(device.id), device.baud_rate);
        settings.setValue(QStringLiteral("%1_rate").arg(device.id), device.sample_rate);
    }
}

void DeviceBackend::loadDeviceSettings()
{
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
    for (const auto& device : devices_.devices())
    {
        if (device.kind != QStringLiteral("serial"))
        {
            continue;
        }
        DeviceModel::Device updated = device;
        updated.port = settings.value(QStringLiteral("%1_port").arg(device.id), updated.port).toString();
        updated.baud_rate = settings.value(QStringLiteral("%1_baud").arg(device.id), updated.baud_rate).toInt();
        updated.sample_rate = settings.value(QStringLiteral("%1_rate").arg(device.id), updated.sample_rate).toInt();
        devices_.setDevice(device.id, updated);
    }
}

std::map<uint8_t, int> DeviceBackend::effectiveEpsilonPacketRates(int baseRateHz, bool *usingCustom) const
{
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
    bool useCustomProfile = settings.value(QStringLiteral("epsilon_custom_packet_rates_enabled"), false).toBool();
    if (useCustomProfile &&
        !settings.value(QStringLiteral("epsilon_custom_packet_rates_user_saved"), false).toBool() &&
        loadCustomEpsilonPacketRates(settings, baseRateHz) == defaultEpsilonPacketRates())
    {
        useCustomProfile = false;
    }
    if (usingCustom)
    {
        *usingCustom = useCustomProfile;
    }
    return useCustomProfile ? loadCustomEpsilonPacketRates(settings, baseRateHz) : groupedEpsilonPacketRates(baseRateHz);
}

void DeviceBackend::autoDetectPortsOrCancel()
{
    if (auto_detect_in_progress_)
    {
        cancel_requested_.store(true);
        log(is_english_ ? QStringLiteral("Cancel requested, stopping automatic serial-port detection...")
                        : QStringLiteral("已请求取消，正在停止自动识别串口..."));
        return;
    }
    if (busy())
    {
        return;
    }
    if (detect_thread_.joinable())
    {
        detect_thread_.join();
    }

    refreshPorts();
    cancel_requested_.store(false);
    setBusyState(false, true, false);
    setStatusText(is_english_ ? QStringLiteral("Detecting Ports...") : QStringLiteral("正在识别串口..."));
    setProgress(0, std::max(1, static_cast<int>(ports_.size()) * 4));
    log(is_english_ ? QStringLiteral("Starting automatic serial-port detection...")
                    : QStringLiteral("开始自动识别串口..."));

    const QStringList portNames = ports_;
    const bool english = is_english_;
    detect_thread_ = std::thread([this, portNames, english]() {
        struct ProbeSpec
        {
            QString key;
            QString label;
            int baud = 0;
            std::function<bool(const QString&)> probe;
        };

        auto post = [this](auto fn) {
            QMetaObject::invokeMethod(this, std::move(fn), Qt::QueuedConnection);
        };
        auto postLog = [post](const QString& msg) mutable {
            post([thisPtr = static_cast<DeviceBackend*>(nullptr), msg]() {});
        };
        Q_UNUSED(postLog);

        auto cancel = [this]() { return cancel_requested_.load(); };
        auto probeCollector = [cancel](const QString& portName, auto&& collector, const VaporView::SerialConfig& config) {
            collector->setCancelCallback(cancel);
            if (!collector->start(portName.toStdString(), config))
            {
                return false;
            }
            const bool responded = collector->checkDeviceResponse();
            collector->stop();
            return responded;
        };

        QVector<ProbeSpec> specs;
        specs.push_back({QStringLiteral("epsilon"), QStringLiteral("EPSILON"), 921600, [probeCollector](const QString& portName) {
                             auto collector = std::make_unique<VaporView::EpsilonCollector>();
                             return probeCollector(portName, std::move(collector), VaporView::SerialConfig::N81(921600));
                         }});
        specs.push_back({QStringLiteral("ptb"), QStringLiteral("PTB210"), 9600, [probeCollector](const QString& portName) {
                             auto collector = std::make_unique<VaporView::PtbCollector>();
                             return probeCollector(portName, std::move(collector), VaporView::SerialConfig::E71(9600));
                         }});
        specs.push_back({QStringLiteral("hmp"), QStringLiteral("HMP3"), 19200, [probeCollector](const QString& portName) {
                             auto collector = std::make_unique<VaporView::HmpCollector>();
                             return probeCollector(portName, std::move(collector), VaporView::SerialConfig::N82(19200));
                         }});
        specs.push_back({QStringLiteral("lidar"), QStringLiteral("TFA1500-L"), 500000, [probeCollector](const QString& portName) {
                             auto collector = std::make_unique<VaporView::LidarCollector>();
                             return probeCollector(portName, std::move(collector), VaporView::SerialConfig::N81(500000));
                         }});

        QHash<QString, QString> detectedPorts;
        QHash<QString, int> detectedBauds;
        QSet<QString> usedPorts;
        int step = 0;
        const int maxSteps = std::max(1, static_cast<int>(portNames.size()) * static_cast<int>(specs.size()));
        for (const ProbeSpec& spec : specs)
        {
            if (cancel())
            {
                break;
            }
            for (const QString& portName : portNames)
            {
                if (cancel())
                {
                    break;
                }
                if (usedPorts.contains(portName))
                {
                    continue;
                }
                ++step;
                post([this, spec, portName, step, maxSteps, english]() {
                    setProgress(step, maxSteps);
                    log(QString(english ? "[Auto Detect] Probing %1 on %2 @ %3..."
                                        : "[自动识别] 正在探测 %1: %2 @ %3 ...")
                            .arg(spec.label, portName)
                            .arg(spec.baud));
                });
                if (spec.probe(portName))
                {
                    detectedPorts[spec.key] = portName;
                    detectedBauds[spec.key] = spec.baud;
                    usedPorts.insert(portName);
                    post([this, spec, portName, english]() {
                        log(QString(english ? "[Auto Detect] Identified %1 on %2"
                                            : "[自动识别] 已识别 %1: %2")
                                .arg(spec.label, portName));
                    });
                    break;
                }
            }
        }

        post([this, detectedPorts, detectedBauds, english]() {
            for (auto it = detectedPorts.cbegin(); it != detectedPorts.cend(); ++it)
            {
                devices_.setDeviceValue(it.key(), DeviceModel::PortRole, it.value());
                devices_.setDeviceValue(it.key(), DeviceModel::BaudRateRole, detectedBauds.value(it.key()));
            }
            saveDeviceSettings();
            cancel_requested_.store(false);
            setBusyState(false, false, false);
            setProgress(0, 1);
            setStatusText(english ? QStringLiteral("Ready") : QStringLiteral("就绪"));
            log(QString(english ? "Auto detect finished: identified %1 device(s)."
                                : "自动识别完成：共识别出 %1 个设备。")
                    .arg(detectedPorts.size()));
        });
    });
}

void DeviceBackend::connectDevices()
{
    if (busy())
    {
        return;
    }
    if (connection_thread_.joinable())
    {
        connection_thread_.join();
    }

    cancel_requested_.store(false);
    setBusyState(true, false, false);
    setStatusText(is_english_ ? QStringLiteral("Connecting...") : QStringLiteral("正在连接..."));
    setProgress(0, 12);
    log(is_english_ ? QStringLiteral("========== Starting Connection ==========")
                    : QStringLiteral("========== 开始连接 =========="));

    const auto epsilonDevice = devices_.deviceAt(QStringLiteral("epsilon"));
    const auto ptbDevice = devices_.deviceAt(QStringLiteral("ptb"));
    const auto hmpDevice = devices_.deviceAt(QStringLiteral("hmp"));
    const auto lidarDevice = devices_.deviceAt(QStringLiteral("lidar"));
    const bool english = is_english_;

    connection_thread_ = std::thread([this, epsilonDevice, ptbDevice, hmpDevice, lidarDevice, english]() {
        auto post = [this](auto fn) {
            QMetaObject::invokeMethod(this, std::move(fn), Qt::QueuedConnection);
        };
        auto postLog = [post](const QString& msg) mutable { post([thisBackend = static_cast<DeviceBackend*>(nullptr), msg]() {}); };
        Q_UNUSED(postLog);

        CollectorSnapshot collectors;
        collectors.epsilon = std::make_shared<VaporView::EpsilonCollector>();
        collectors.ptb = std::make_shared<VaporView::PtbCollector>();
        collectors.hmp = std::make_shared<VaporView::HmpCollector>();
        collectors.lidar = std::make_shared<VaporView::LidarCollector>();
        setCollectors(collectors);

        auto logCallback = [this](const std::string& msg) {
            const QString qmsg = QString::fromStdString(msg);
            QMetaObject::invokeMethod(this, [this, qmsg]() { log(qmsg); }, Qt::QueuedConnection);
        };
        auto cancelCallback = [this]() { return cancel_requested_.load(); };
        collectors.epsilon->setLogCallback(logCallback);
        collectors.ptb->setLogCallback(logCallback);
        collectors.hmp->setLogCallback(logCallback);
        collectors.lidar->setLogCallback(logCallback);
        collectors.epsilon->setCancelCallback(cancelCallback);
        collectors.ptb->setCancelCallback(cancelCallback);
        collectors.hmp->setCancelCallback(cancelCallback);
        collectors.lidar->setCancelCallback(cancelCallback);

        collectors.epsilon->setRawFrameCallback([this](uint64_t hostTimestampUs, uint8_t packetId, uint8_t serialNumber, const uint8_t* data, size_t size) {
            const QByteArray payload(reinterpret_cast<const char*>(data), static_cast<int>(size));
            QMetaObject::invokeMethod(this, [this, hostTimestampUs, packetId, serialNumber, payload]() {
                emit epsilonRawFrame(hostTimestampUs, packetId, serialNumber, payload);
            }, Qt::QueuedConnection);
        });
        collectors.ptb->setRawResponseCallback([this](uint64_t hostTimestampUs, const uint8_t* data, size_t size) {
            const QByteArray payload(reinterpret_cast<const char*>(data), static_cast<int>(size));
            QMetaObject::invokeMethod(this, [this, hostTimestampUs, payload]() {
                emit ptbRawFrame(hostTimestampUs, payload);
            }, Qt::QueuedConnection);
        });
        collectors.hmp->setRawResponseCallback([this](uint64_t hostTimestampUs, const uint8_t* data, size_t size) {
            const QByteArray payload(reinterpret_cast<const char*>(data), static_cast<int>(size));
            QMetaObject::invokeMethod(this, [this, hostTimestampUs, payload]() {
                emit hmpRawFrame(hostTimestampUs, payload);
            }, Qt::QueuedConnection);
        });
        collectors.lidar->setRawFrameCallback([this](uint64_t hostTimestampUs, VaporView::LidarProtocol protocol, const uint8_t* data, size_t size) {
            const QByteArray payload(reinterpret_cast<const char*>(data), static_cast<int>(size));
            QMetaObject::invokeMethod(this, [this, hostTimestampUs, protocol, payload]() {
                emit lidarRawFrame(hostTimestampUs, static_cast<int>(protocol), payload);
            }, Qt::QueuedConnection);
        });

        int progress = 0;
        int total = 0;
        int connectedCount = 0;
        const QString placeholder = selectPlaceholder();
        auto validPort = [&placeholder](const QString& port) {
            return !port.trimmed().isEmpty() && !port.trimmed().startsWith(QStringLiteral("--")) && port.trimmed() != placeholder;
        };
        for (const auto& d : {epsilonDevice, ptbDevice, hmpDevice, lidarDevice})
        {
            if (validPort(d.port)) ++total;
        }
        const int maxProgress = std::max(1, total * 3);

        auto abortIfRequested = [&]() {
            if (!cancel_requested_.load())
            {
                return false;
            }
            stopAllCollectors();
            post([this, english]() {
                setBusyState(false, false, false);
                setStatusText(english ? QStringLiteral("Connection canceled") : QStringLiteral("连接已取消"));
                log(english ? QStringLiteral("Connection canceled") : QStringLiteral("连接已取消"));
            });
            return true;
        };

        auto connectCollector = [&](const DeviceModel::Device& device, auto* collector, const VaporView::SerialConfig& config, auto&& onReady) -> int {
            if (!validPort(device.port))
            {
                post([this, device, english]() {
                    log(QString(english ? "[%1] Skipped (not selected)" : "[%1] 跳过 (未选择)").arg(device.display_name));
                });
                return 0;
            }
            if (abortIfRequested()) return -1;
            post([this, device, progress = ++progress, maxProgress, english]() {
                setProgress(progress, maxProgress);
                log(QString(english ? "[%1] Opening %2 @ %3..." : "[%1] 正在打开 %2 @ %3 ...")
                        .arg(device.display_name, device.port)
                        .arg(device.baud_rate));
            });
            if (!collector->start(device.port.toStdString(), config))
            {
                post([this, device, collector, english]() {
                    log(QString(english ? "[%1] Failed to open port: %2" : "[%1] 打开端口失败: %2")
                            .arg(device.display_name, QString::fromStdString(collector->getLastError())), QStringLiteral("error"));
                });
                return 0;
            }
            if (abortIfRequested()) return -1;
            post([this, device, progress = ++progress, maxProgress, english]() {
                setProgress(progress, maxProgress);
                log(QString(english ? "[%1] Checking device response..." : "[%1] 正在检测设备响应...").arg(device.display_name));
            });
            if (!collector->checkDeviceResponse())
            {
                collector->stop();
                post([this, device, english]() {
                    log(QString(english ? "[%1] Device not responding." : "[%1] 设备无响应。").arg(device.display_name), QStringLiteral("warning"));
                });
                return 0;
            }
            if (abortIfRequested()) return -1;
            if (!onReady())
            {
                collector->stop();
                return 0;
            }
            post([this, device, progress = ++progress, maxProgress, english]() {
                setProgress(progress, maxProgress);
                devices_.setDeviceValue(device.id, DeviceModel::ConnectedRole, true);
                devices_.setDeviceValue(device.id, DeviceModel::OnlineRole, true);
                devices_.setDeviceValue(device.id, DeviceModel::StatusTextRole, QStringLiteral("Connected"));
                log(QString(english ? "[%1] Connected on %2 @ %3" : "[%1] 已连接: %2 @ %3")
                        .arg(device.display_name, device.port)
                        .arg(device.baud_rate));
            });
            return 1;
        };

        bool usingCustomEpsilonProfile = false;
        const std::map<uint8_t, int> epsilonRates = effectiveEpsilonPacketRates(epsilonDevice.sample_rate, &usingCustomEpsilonProfile);
        collectors.epsilon->setSampleRate(epsilonPacketCallbackRate(epsilonRates, epsilonDevice.sample_rate));
        collectors.ptb->setSampleRate(ptbDevice.sample_rate);
        collectors.hmp->setSampleRate(hmpDevice.sample_rate);
        collectors.lidar->setSampleRate(lidarDevice.sample_rate);

        int result = connectCollector(epsilonDevice, collectors.epsilon.get(), VaporView::SerialConfig::N81(epsilonDevice.baud_rate), [&]() {
            collectors.epsilon->setDataCallback([this]() {
                QMetaObject::invokeMethod(this, [this]() {
                    const auto snapshot = snapshotCollectors();
                    if (!snapshot.epsilon) return;
                    {
                        QMutexLocker lock(&data_mutex_);
                        current_epsilon_ = snapshot.epsilon->getLatestData();
                    }
                    emit dataChanged();
                }, Qt::QueuedConnection);
            });
            if (!collectors.epsilon->setOutputPacketRates(epsilonRates))
            {
                QMetaObject::invokeMethod(this, [this, english]() {
                    log(english ? QStringLiteral("[EPSILON] Failed to configure packet-rate profile.")
                                : QStringLiteral("[EPSILON] 包频率配置失败。"), QStringLiteral("warning"));
                }, Qt::QueuedConnection);
                return false;
            }
            return collectors.epsilon->startStreaming();
        });
        if (result < 0) return;
        connectedCount += result;

        result = connectCollector(ptbDevice, collectors.ptb.get(), VaporView::SerialConfig::E71(ptbDevice.baud_rate), [&]() {
            collectors.ptb->setDataCallback([this]() {
                QMetaObject::invokeMethod(this, [this]() {
                    const auto snapshot = snapshotCollectors();
                    if (!snapshot.ptb) return;
                    {
                        QMutexLocker lock(&data_mutex_);
                        current_ptb_ = snapshot.ptb->getLatestData();
                    }
                    emit dataChanged();
                }, Qt::QueuedConnection);
            });
            collectors.ptb->setDeviceSampleRate(ptbDevice.sample_rate);
            return collectors.ptb->startStreaming();
        });
        if (result < 0) return;
        connectedCount += result;

        result = connectCollector(hmpDevice, collectors.hmp.get(), VaporView::SerialConfig::N82(hmpDevice.baud_rate), [&]() {
            collectors.hmp->setDataCallback([this]() {
                QMetaObject::invokeMethod(this, [this]() {
                    const auto snapshot = snapshotCollectors();
                    if (!snapshot.hmp) return;
                    {
                        QMutexLocker lock(&data_mutex_);
                        current_hmp_ = snapshot.hmp->getLatestData();
                    }
                    emit dataChanged();
                }, Qt::QueuedConnection);
            });
            return collectors.hmp->startStreaming();
        });
        if (result < 0) return;
        connectedCount += result;

        result = connectCollector(lidarDevice, collectors.lidar.get(), VaporView::SerialConfig::N81(lidarDevice.baud_rate), [&]() {
            collectors.lidar->setDataCallback([this]() {
                QMetaObject::invokeMethod(this, [this]() {
                    const auto snapshot = snapshotCollectors();
                    if (!snapshot.lidar) return;
                    {
                        QMutexLocker lock(&data_mutex_);
                        current_lidar_ = snapshot.lidar->getLatestData();
                    }
                    emit dataChanged();
                }, Qt::QueuedConnection);
            });
            collectors.lidar->setDeviceSampleRate(lidarDevice.sample_rate);
            return collectors.lidar->startStreaming();
        });
        if (result < 0) return;
        connectedCount += result;

        post([this, connectedCount, total, english]() {
            connected_ = connectedCount > 0;
            setBusyState(false, false, false);
            setProgress(0, 1);
            setStatusText(connected_ ? (english ? QStringLiteral("Connected") : QStringLiteral("已连接"))
                                     : (english ? QStringLiteral("Disconnected") : QStringLiteral("未连接")));
            log(QString(english ? "========== Serial Connection Summary: %1/%2 serial devices connected =========="
                                : "========== 串口连接摘要: %1/%2 个串口设备已连接 ==========")
                    .arg(connectedCount)
                    .arg(total));
            emit connectionStateChanged();
        });
    });
}

void DeviceBackend::connectDevice(const QString& id)
{
    const DeviceModel::Device device = devices_.deviceAt(id);
    if (device.id.isEmpty() || device.kind != QStringLiteral("serial"))
    {
        log(is_english_ ? QStringLiteral("This device is not a serial collector.")
                        : QStringLiteral("该设备不是串口采集设备。"), QStringLiteral("warning"));
        return;
    }
    if (busy())
    {
        return;
    }
    if (connection_thread_.joinable())
    {
        connection_thread_.join();
    }

    cancel_requested_.store(false);
    setBusyState(true, false, false);
    setStatusText(is_english_ ? QStringLiteral("Connecting...") : QStringLiteral("正在连接..."));
    setProgress(0, 3);
    const bool english = is_english_;
    log(QString(english ? "========== Starting %1 Connection =========="
                        : "========== 开始连接 %1 ==========")
            .arg(device.display_name));

    connection_thread_ = std::thread([this, device, english]() {
        auto post = [this](auto fn) {
            QMetaObject::invokeMethod(this, std::move(fn), Qt::QueuedConnection);
        };
        const QString placeholder = selectPlaceholder();
        auto validPort = [&placeholder](const QString& port) {
            return !port.trimmed().isEmpty() && !port.trimmed().startsWith(QStringLiteral("--")) && port.trimmed() != placeholder;
        };
        auto finish = [this, post, english](bool ok) mutable {
            post([this, english, ok]() {
                connected_ = anySerialCollectorRunning();
                setBusyState(false, false, false);
                setProgress(0, 1);
                setStatusText(connected_ ? (english ? QStringLiteral("Connected") : QStringLiteral("已连接"))
                                         : (english ? QStringLiteral("Disconnected") : QStringLiteral("未连接")));
                if (!ok)
                {
                    emit connectionStateChanged();
                }
            });
        };
        if (!validPort(device.port))
        {
            post([this, device, english]() {
                log(QString(english ? "[%1] Skipped (not selected)" : "[%1] 跳过 (未选择)").arg(device.display_name), QStringLiteral("warning"));
            });
            finish(false);
            return;
        }

        auto logCallback = [this](const std::string& msg) {
            const QString qmsg = QString::fromStdString(msg);
            QMetaObject::invokeMethod(this, [this, qmsg]() { log(qmsg); }, Qt::QueuedConnection);
        };
        auto cancelCallback = [this]() { return cancel_requested_.load(); };

        auto fail = [this, post, device, english](const QString& text) mutable {
            post([this, device, text, english]() {
                Q_UNUSED(english);
                log(QStringLiteral("[%1] %2").arg(device.display_name, text), QStringLiteral("error"));
            });
        };

        post([this, device, english]() {
            setProgress(1, 3);
            log(QString(english ? "[%1] Opening %2 @ %3..." : "[%1] 正在打开 %2 @ %3 ...")
                    .arg(device.display_name, device.port)
                    .arg(device.baud_rate));
        });

        bool ok = false;
        CollectorSnapshot snapshot = snapshotCollectors();

        if (device.id == QStringLiteral("epsilon"))
        {
            auto collector = std::make_shared<VaporView::EpsilonCollector>();
            collector->setLogCallback(logCallback);
            collector->setCancelCallback(cancelCallback);
            bool usingCustom = false;
            const auto rates = effectiveEpsilonPacketRates(device.sample_rate, &usingCustom);
            collector->setSampleRate(epsilonPacketCallbackRate(rates, device.sample_rate));
            collector->setRawFrameCallback([this](uint64_t hostTimestampUs, uint8_t packetId, uint8_t serialNumber, const uint8_t* data, size_t size) {
                const QByteArray payload(reinterpret_cast<const char*>(data), static_cast<int>(size));
                QMetaObject::invokeMethod(this, [this, hostTimestampUs, packetId, serialNumber, payload]() {
                    emit epsilonRawFrame(hostTimestampUs, packetId, serialNumber, payload);
                }, Qt::QueuedConnection);
            });
            if (!collector->start(device.port.toStdString(), VaporView::SerialConfig::N81(device.baud_rate)))
            {
                fail(QString(english ? "Failed to open port: %1" : "打开端口失败: %1").arg(QString::fromStdString(collector->getLastError())));
                finish(false);
                return;
            }
            post([this]() { setProgress(2, 3); });
            if (!collector->checkDeviceResponse() || !collector->setOutputPacketRates(rates))
            {
                collector->stop();
                fail(english ? QStringLiteral("Device not responding or packet-rate setup failed.")
                             : QStringLiteral("设备无响应或包频率配置失败。"));
                finish(false);
                return;
            }
            collector->setDataCallback([this]() {
                QMetaObject::invokeMethod(this, [this]() {
                    const auto snapshot = snapshotCollectors();
                    if (!snapshot.epsilon) return;
                    {
                        QMutexLocker lock(&data_mutex_);
                        current_epsilon_ = snapshot.epsilon->getLatestData();
                    }
                    emit dataChanged();
                }, Qt::QueuedConnection);
            });
            ok = collector->startStreaming();
            if (ok) snapshot.epsilon = collector;
        }
        else if (device.id == QStringLiteral("ptb"))
        {
            auto collector = std::make_shared<VaporView::PtbCollector>();
            collector->setLogCallback(logCallback);
            collector->setCancelCallback(cancelCallback);
            collector->setSampleRate(device.sample_rate);
            collector->setRawResponseCallback([this](uint64_t hostTimestampUs, const uint8_t* data, size_t size) {
                const QByteArray payload(reinterpret_cast<const char*>(data), static_cast<int>(size));
                QMetaObject::invokeMethod(this, [this, hostTimestampUs, payload]() { emit ptbRawFrame(hostTimestampUs, payload); }, Qt::QueuedConnection);
            });
            if (!collector->start(device.port.toStdString(), VaporView::SerialConfig::E71(device.baud_rate)))
            {
                fail(QString(english ? "Failed to open port: %1" : "打开端口失败: %1").arg(QString::fromStdString(collector->getLastError())));
                finish(false);
                return;
            }
            post([this]() { setProgress(2, 3); });
            if (!collector->checkDeviceResponse())
            {
                collector->stop();
                fail(english ? QStringLiteral("Device not responding.") : QStringLiteral("设备无响应。"));
                finish(false);
                return;
            }
            collector->setDataCallback([this]() {
                QMetaObject::invokeMethod(this, [this]() {
                    const auto snapshot = snapshotCollectors();
                    if (!snapshot.ptb) return;
                    {
                        QMutexLocker lock(&data_mutex_);
                        current_ptb_ = snapshot.ptb->getLatestData();
                    }
                    emit dataChanged();
                }, Qt::QueuedConnection);
            });
            collector->setDeviceSampleRate(device.sample_rate);
            ok = collector->startStreaming();
            if (ok) snapshot.ptb = collector;
        }
        else if (device.id == QStringLiteral("hmp"))
        {
            auto collector = std::make_shared<VaporView::HmpCollector>();
            collector->setLogCallback(logCallback);
            collector->setCancelCallback(cancelCallback);
            collector->setSampleRate(device.sample_rate);
            collector->setRawResponseCallback([this](uint64_t hostTimestampUs, const uint8_t* data, size_t size) {
                const QByteArray payload(reinterpret_cast<const char*>(data), static_cast<int>(size));
                QMetaObject::invokeMethod(this, [this, hostTimestampUs, payload]() { emit hmpRawFrame(hostTimestampUs, payload); }, Qt::QueuedConnection);
            });
            if (!collector->start(device.port.toStdString(), VaporView::SerialConfig::N82(device.baud_rate)))
            {
                fail(QString(english ? "Failed to open port: %1" : "打开端口失败: %1").arg(QString::fromStdString(collector->getLastError())));
                finish(false);
                return;
            }
            post([this]() { setProgress(2, 3); });
            if (!collector->checkDeviceResponse())
            {
                collector->stop();
                fail(english ? QStringLiteral("Device not responding.") : QStringLiteral("设备无响应。"));
                finish(false);
                return;
            }
            collector->setDataCallback([this]() {
                QMetaObject::invokeMethod(this, [this]() {
                    const auto snapshot = snapshotCollectors();
                    if (!snapshot.hmp) return;
                    {
                        QMutexLocker lock(&data_mutex_);
                        current_hmp_ = snapshot.hmp->getLatestData();
                    }
                    emit dataChanged();
                }, Qt::QueuedConnection);
            });
            ok = collector->startStreaming();
            if (ok) snapshot.hmp = collector;
        }
        else if (device.id == QStringLiteral("lidar"))
        {
            auto collector = std::make_shared<VaporView::LidarCollector>();
            collector->setLogCallback(logCallback);
            collector->setCancelCallback(cancelCallback);
            collector->setSampleRate(device.sample_rate);
            collector->setRawFrameCallback([this](uint64_t hostTimestampUs, VaporView::LidarProtocol protocol, const uint8_t* data, size_t size) {
                const QByteArray payload(reinterpret_cast<const char*>(data), static_cast<int>(size));
                QMetaObject::invokeMethod(this, [this, hostTimestampUs, protocol, payload]() {
                    emit lidarRawFrame(hostTimestampUs, static_cast<int>(protocol), payload);
                }, Qt::QueuedConnection);
            });
            if (!collector->start(device.port.toStdString(), VaporView::SerialConfig::N81(device.baud_rate)))
            {
                fail(QString(english ? "Failed to open port: %1" : "打开端口失败: %1").arg(QString::fromStdString(collector->getLastError())));
                finish(false);
                return;
            }
            post([this]() { setProgress(2, 3); });
            if (!collector->checkDeviceResponse())
            {
                collector->stop();
                fail(english ? QStringLiteral("Device not responding.") : QStringLiteral("设备无响应。"));
                finish(false);
                return;
            }
            collector->setDataCallback([this]() {
                QMetaObject::invokeMethod(this, [this]() {
                    const auto snapshot = snapshotCollectors();
                    if (!snapshot.lidar) return;
                    {
                        QMutexLocker lock(&data_mutex_);
                        current_lidar_ = snapshot.lidar->getLatestData();
                    }
                    emit dataChanged();
                }, Qt::QueuedConnection);
            });
            collector->setDeviceSampleRate(device.sample_rate);
            ok = collector->startStreaming();
            if (ok) snapshot.lidar = collector;
        }

        if (!ok)
        {
            fail(english ? QStringLiteral("Failed to start streaming.") : QStringLiteral("启动连续采集失败。"));
            finish(false);
            return;
        }

        setCollectors(snapshot);
        post([this, device, english]() {
            setProgress(3, 3);
            devices_.setDeviceValue(device.id, DeviceModel::ConnectedRole, true);
            devices_.setDeviceValue(device.id, DeviceModel::OnlineRole, true);
            devices_.setDeviceValue(device.id, DeviceModel::StatusTextRole, QStringLiteral("Connected"));
            connected_ = true;
            log(QString(english ? "[%1] Connected on %2 @ %3" : "[%1] 已连接: %2 @ %3")
                    .arg(device.display_name, device.port)
                    .arg(device.baud_rate));
            emit connectionStateChanged();
        });
        finish(true);
    });
}

void DeviceBackend::disconnectDevices()
{
    log(is_english_ ? QStringLiteral("Disconnecting...") : QStringLiteral("正在断开..."));
    cancel_requested_.store(true);
    if (connection_thread_.joinable())
    {
        connection_thread_.join();
    }
    stopAllCollectors();
    cancel_requested_.store(false);
    setBusyState(false, false, false);
    setStatusText(is_english_ ? QStringLiteral("Disconnected") : QStringLiteral("未连接"));
    log(is_english_ ? QStringLiteral("Disconnected") : QStringLiteral("已断开"));
}

void DeviceBackend::disconnectDevice(const QString& id)
{
    const DeviceModel::Device device = devices_.deviceAt(id);
    if (device.id.isEmpty() || device.kind != QStringLiteral("serial"))
    {
        return;
    }
    if (connection_in_progress_)
    {
        cancelConnect();
        return;
    }
    const bool stopped = stopCollector(id);
    setStatusText(connected_ ? (is_english_ ? QStringLiteral("Connected") : QStringLiteral("已连接"))
                             : (is_english_ ? QStringLiteral("Disconnected") : QStringLiteral("未连接")));
    log(stopped ? QString(is_english_ ? "[%1] Disconnected" : "[%1] 已断开").arg(device.display_name)
                : QString(is_english_ ? "[%1] Was not connected" : "[%1] 未连接").arg(device.display_name));
}

void DeviceBackend::cancelConnect()
{
    if (!connection_in_progress_)
    {
        return;
    }
    cancel_requested_.store(true);
    log(is_english_ ? QStringLiteral("Cancel requested, stopping connection attempt...")
                    : QStringLiteral("已请求取消，正在停止连接流程..."));
}

void DeviceBackend::saveEpsilonPacketRates(const QVariantMap& packetRates, bool customEnabled)
{
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
    const auto groupedRates = groupedEpsilonPacketRates(selectedSampleRate(QStringLiteral("epsilon")));
    bool hasCustomOverrides = false;
    for (const EpsilonPacketConfigOption& option : epsilonPacketConfigOptions())
    {
        const QString key = QStringLiteral("%1").arg(option.packet_id, 2, 16, QLatin1Char('0')).toUpper();
        const int rate = packetRates.value(key, groupedRates.at(option.packet_id)).toInt();
        settings.setValue(epsilonPacketRateSettingsKey(option.packet_id), epsilonPacketRateSupported(option, rate) ? rate : groupedRates.at(option.packet_id));
        if (rate != groupedRates.at(option.packet_id))
        {
            hasCustomOverrides = true;
        }
    }
    const bool enabled = customEnabled || hasCustomOverrides;
    settings.setValue(QStringLiteral("epsilon_custom_packet_rates_enabled"), enabled);
    settings.setValue(QStringLiteral("epsilon_custom_packet_rates_user_saved"), enabled);
    settings.remove(QStringLiteral("epsilon_last_config_signature"));
    settings.remove(QStringLiteral("epsilon_last_config_apply_version"));
    log(enabled ? QStringLiteral("[EPSILON] Custom packet-rate profile saved.")
                : QStringLiteral("[EPSILON] Grouped packet-rate profile saved."));
}

void DeviceBackend::reconfigureEpsilonOutput()
{
    if (busy())
    {
        return;
    }
    if (reconfigure_thread_.joinable())
    {
        reconfigure_thread_.join();
    }
    const auto device = devices_.deviceAt(QStringLiteral("epsilon"));
    if (device.port.trimmed().isEmpty() || device.port.startsWith(QStringLiteral("--")))
    {
        log(is_english_ ? QStringLiteral("Select an EPSILON serial port first.")
                        : QStringLiteral("请先选择 EPSILON 串口。"), QStringLiteral("warning"));
        return;
    }

    const bool english = is_english_;
    setBusyState(false, false, true);
    setStatusText(english ? QStringLiteral("Reconfiguring EPSILON...") : QStringLiteral("正在重配 EPSILON..."));
    log(QString(english ? "[EPSILON] Starting manual output reconfiguration: %1 @ %2"
                        : "[EPSILON] 开始手动重配输出: %1 @ %2")
            .arg(device.port)
            .arg(device.baud_rate));

    reconfigure_thread_ = std::thread([this, device, english]() {
        auto collector = std::make_shared<VaporView::EpsilonCollector>();
        collector->setLogCallback([this](const std::string& msg) {
            const QString qmsg = QString::fromStdString(msg);
            QMetaObject::invokeMethod(this, [this, qmsg]() { log(qmsg); }, Qt::QueuedConnection);
        });
        bool usingCustom = false;
        const auto packetRates = effectiveEpsilonPacketRates(device.sample_rate, &usingCustom);
        bool ok = false;
        if (collector->start(device.port.toStdString(), VaporView::SerialConfig::N81(device.baud_rate)))
        {
            ok = collector->setOutputPacketRates(packetRates, true);
            collector->stop();
        }
        QMetaObject::invokeMethod(this, [this, ok, packetRates, english]() {
            setBusyState(false, false, false);
            setStatusText(connected_ ? (english ? QStringLiteral("Connected") : QStringLiteral("已连接")) : QStringLiteral("Ready"));
            log(ok ? QString(english ? "[EPSILON] Output profile applied: %1" : "[EPSILON] 输出配置已应用: %1").arg(epsilonPacketRatesSummary(packetRates))
                   : QString(english ? "[EPSILON] Failed to apply output profile." : "[EPSILON] 输出配置应用失败。"),
                ok ? QStringLiteral("info") : QStringLiteral("error"));
        }, Qt::QueuedConnection);
    });
}

void DeviceBackend::configureEpsilonRtcmPort(const QString& forwardPort, int forwardBaud)
{
    if (busy())
    {
        return;
    }
    const auto device = devices_.deviceAt(QStringLiteral("epsilon"));
    if (device.port.trimmed().isEmpty() || device.port.startsWith(QStringLiteral("--")) || forwardPort.trimmed().isEmpty())
    {
        log(is_english_ ? QStringLiteral("Select EPSILON main port and RTCM forwarding port first.")
                        : QStringLiteral("请先选择 EPSILON 主串口和 RTCM 转发串口。"), QStringLiteral("warning"));
        return;
    }
    if (reconfigure_thread_.joinable())
    {
        reconfigure_thread_.join();
    }
    const bool english = is_english_;
    setBusyState(false, false, true);
    setStatusText(english ? QStringLiteral("Configuring EPSILON RTCM Port...") : QStringLiteral("正在配置 EPSILON RTCM 串口..."));
    reconfigure_thread_ = std::thread([this, device, forwardPort, forwardBaud, english]() {
        auto collector = std::make_shared<VaporView::EpsilonCollector>();
        collector->setLogCallback([this](const std::string& msg) {
            const QString qmsg = QString::fromStdString(msg);
            QMetaObject::invokeMethod(this, [this, qmsg]() { log(qmsg); }, Qt::QueuedConnection);
        });
        bool ok = false;
        if (collector->start(device.port.toStdString(), VaporView::SerialConfig::N81(device.baud_rate)))
        {
            ok = collector->configureRtcmPort(2, forwardBaud);
            collector->stop();
        }
        if (ok)
        {
            QSettings mainSettings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
            mainSettings.setValue(QStringLiteral("epsilon_rtcm_forward_port"), forwardPort);
            mainSettings.setValue(QStringLiteral("epsilon_rtcm_forward_baud"), forwardBaud);
            QSettings rtkSettings(QStringLiteral("VaporView"), QStringLiteral("RtkConfig"));
            rtkSettings.setValue(QStringLiteral("output_port"), forwardPort);
            rtkSettings.setValue(QStringLiteral("baudrate"), QString::number(forwardBaud));
        }
        QMetaObject::invokeMethod(this, [this, ok, forwardPort, forwardBaud, english]() {
            setBusyState(false, false, false);
            setStatusText(connected_ ? (english ? QStringLiteral("Connected") : QStringLiteral("已连接")) : QStringLiteral("Ready"));
            log(ok ? QString(english ? "[EPSILON] RTCM port is ready. Forwarding: %1 @ %2"
                                      : "[EPSILON] RTCM 串口已就绪，转发配置: %1 @ %2")
                         .arg(forwardPort)
                         .arg(forwardBaud)
                   : QString(english ? "[EPSILON] Failed to configure RTCM port." : "[EPSILON] RTCM 串口配置失败。"),
                ok ? QStringLiteral("info") : QStringLiteral("error"));
        }, Qt::QueuedConnection);
    });
}

bool DeviceBackend::applyEpsilonMainAntennaLeverArm(double xM, double yM, double zM)
{
    if (busy())
    {
        log(is_english_ ? QStringLiteral("EPSILON is busy. Try again later.")
                        : QStringLiteral("EPSILON 当前正忙，请稍后再试。"), QStringLiteral("warning"));
        return false;
    }
    const auto device = devices_.deviceAt(QStringLiteral("epsilon"));
    if (device.port.trimmed().isEmpty() || device.port.startsWith(QStringLiteral("--")))
    {
        log(is_english_ ? QStringLiteral("Select the EPSILON main serial port first.")
                        : QStringLiteral("请先选择 EPSILON 主串口。"), QStringLiteral("warning"));
        return false;
    }
    auto collector = std::make_unique<VaporView::EpsilonCollector>();
    if (!collector->start(device.port.toStdString(), VaporView::SerialConfig::N81(device.baud_rate)))
    {
        log(is_english_ ? QStringLiteral("Failed to open EPSILON port for lever-arm configuration.")
                        : QStringLiteral("打开 EPSILON 串口配置杆臂失败。"), QStringLiteral("error"));
        return false;
    }
    const bool ok = collector->configureMainAntennaLeverArm(xM, yM, zM);
    collector->stop();
    log(ok ? QStringLiteral("[EPSILON] Main antenna lever arm applied.")
           : QStringLiteral("[EPSILON] Failed to apply main antenna lever arm."),
        ok ? QStringLiteral("info") : QStringLiteral("error"));
    return ok;
}

TcpWaveReceiverWorker::TcpWaveReceiverWorker(QObject *parent)
    : QObject(parent)
    , socket_(nullptr)
    , read_state_(ReadState::RawHeader)
    , header_byte_order_(HeaderByteOrder::Unknown)
    , float_encoding_(VaporView::TcpFloatEncoding::Unknown)
    , expected_payload_size_(0)
    , last_display_emit_ms_(0)
    , raw_frame_forwarding_enabled_(false)
    , peak_search_start_index_(kDefaultPeakSearchStartIndex)
    , peak_search_end_index_(kDefaultPeakSearchEndIndex)
{
}

TcpWaveReceiverWorker::~TcpWaveReceiverWorker()
{
    stop();
}

void TcpWaveReceiverWorker::connectToHost(const QString& host, int port)
{
    if (socket_ && socket_->state() != QAbstractSocket::UnconnectedState)
    {
        return;
    }
    if (!socket_)
    {
        socket_ = new QTcpSocket(this);
        connect(socket_, &QTcpSocket::readyRead, this, &TcpWaveReceiverWorker::onReadyRead);
        connect(socket_, &QTcpSocket::connected, this, &TcpWaveReceiverWorker::onSocketConnected);
        connect(socket_, &QTcpSocket::disconnected, this, &TcpWaveReceiverWorker::onSocketDisconnected);
        connect(socket_, &QAbstractSocket::errorOccurred, this, &TcpWaveReceiverWorker::onSocketError);
    }
    resetStreamState();
    socket_->connectToHost(host, static_cast<quint16>(std::clamp(port, 1, 65535)));
}

void TcpWaveReceiverWorker::disconnectFromHost()
{
    if (socket_ && socket_->state() != QAbstractSocket::UnconnectedState)
    {
        socket_->disconnectFromHost();
    }
}

void TcpWaveReceiverWorker::setRawFrameForwardingEnabled(bool enabled)
{
    raw_frame_forwarding_enabled_ = enabled;
}

void TcpWaveReceiverWorker::setPeakSearchWindow(int startIndex, int endIndex)
{
    peak_search_start_index_ = std::max(0, startIndex);
    peak_search_end_index_ = std::max(0, endIndex);
}

void TcpWaveReceiverWorker::stop()
{
    if (!socket_)
    {
        return;
    }
    socket_->disconnect(this);
    socket_->close();
    delete socket_;
    socket_ = nullptr;
    resetStreamState();
}

void TcpWaveReceiverWorker::resetStreamState()
{
    buffer_.clear();
    pending_raw_payload_.clear();
    pending_raw_samples_.clear();
    read_state_ = ReadState::RawHeader;
    header_byte_order_ = HeaderByteOrder::Unknown;
    float_encoding_ = VaporView::TcpFloatEncoding::Unknown;
    expected_payload_size_ = 0;
    frame_arrivals_ms_.clear();
    last_display_emit_ms_ = 0;
}

void TcpWaveReceiverWorker::onSocketConnected()
{
    emit connected();
}

void TcpWaveReceiverWorker::onSocketDisconnected()
{
    emit disconnected();
}

void TcpWaveReceiverWorker::onSocketError()
{
    emit socketError(socket_ ? socket_->errorString() : QStringLiteral("TCP socket error."));
}

void TcpWaveReceiverWorker::onReadyRead()
{
    if (!socket_)
    {
        return;
    }
    buffer_.append(socket_->readAll());
    processBuffer();
}

bool TcpWaveReceiverWorker::isValidPayloadSize(qint32 candidate) const
{
    return candidate > 0 && candidate <= kMaxTcpPayloadSize && candidate % kFloatSize == 0;
}

qint32 TcpWaveReceiverWorker::decodeHeaderValue(const char *raw, HeaderByteOrder order) const
{
    return order == HeaderByteOrder::BigEndian
        ? qFromBigEndian<qint32>(reinterpret_cast<const uchar*>(raw))
        : qFromLittleEndian<qint32>(reinterpret_cast<const uchar*>(raw));
}

bool TcpWaveReceiverWorker::tryConsumeHeader()
{
    if (buffer_.size() < 4)
    {
        return false;
    }

    HeaderByteOrder order = header_byte_order_;
    qint32 size = 0;
    if (order == HeaderByteOrder::Unknown)
    {
        const qint32 little = decodeHeaderValue(buffer_.constData(), HeaderByteOrder::LittleEndian);
        const qint32 big = decodeHeaderValue(buffer_.constData(), HeaderByteOrder::BigEndian);
        if (isValidPayloadSize(little))
        {
            order = HeaderByteOrder::LittleEndian;
            size = little;
        }
        else if (isValidPayloadSize(big))
        {
            order = HeaderByteOrder::BigEndian;
            size = big;
        }
        else
        {
            buffer_.remove(0, 1);
            return true;
        }
    }
    else
    {
        size = decodeHeaderValue(buffer_.constData(), order);
        if (!isValidPayloadSize(size))
        {
            header_byte_order_ = HeaderByteOrder::Unknown;
            buffer_.remove(0, 1);
            return true;
        }
    }

    header_byte_order_ = order;
    expected_payload_size_ = size;
    buffer_.remove(0, 4);
    return true;
}

bool TcpWaveReceiverWorker::tryConsumePayload(QVector<float>& output, QByteArray *rawPayload)
{
    if (expected_payload_size_ <= 0 || buffer_.size() < expected_payload_size_)
    {
        return false;
    }
    const QByteArray payload = buffer_.left(expected_payload_size_);
    buffer_.remove(0, expected_payload_size_);
    if (float_encoding_ == VaporView::TcpFloatEncoding::Unknown)
    {
        float_encoding_ = VaporView::autoDetectTcpFloatEncoding(payload);
    }
    output = VaporView::decodeTcpFloatPayload(payload, float_encoding_);
    if (rawPayload)
    {
        *rawPayload = payload;
    }
    expected_payload_size_ = 0;
    return true;
}

double TcpWaveReceiverWorker::updateFrameRate(qint64 nowMs)
{
    frame_arrivals_ms_.append(nowMs);
    while (frame_arrivals_ms_.size() > 60)
    {
        frame_arrivals_ms_.removeFirst();
    }
    if (frame_arrivals_ms_.size() < 2)
    {
        return 0.0;
    }
    const qint64 span = frame_arrivals_ms_.last() - frame_arrivals_ms_.first();
    return span > 0 ? static_cast<double>(frame_arrivals_ms_.size() - 1) * 1000.0 / static_cast<double>(span) : 0.0;
}

void TcpWaveReceiverWorker::processBuffer()
{
    bool progressed = true;
    while (progressed)
    {
        progressed = false;
        if (read_state_ == ReadState::RawHeader || read_state_ == ReadState::HarmonicHeader)
        {
            progressed = tryConsumeHeader();
            if (progressed && expected_payload_size_ > 0)
            {
                read_state_ = (read_state_ == ReadState::RawHeader) ? ReadState::RawPayload : ReadState::HarmonicPayload;
            }
        }
        if (read_state_ == ReadState::RawPayload)
        {
            QVector<float> rawSamples;
            QByteArray payload;
            if (!tryConsumePayload(rawSamples, &payload))
            {
                continue;
            }
            pending_raw_payload_ = payload;
            pending_raw_samples_ = std::move(rawSamples);
            read_state_ = ReadState::HarmonicHeader;
            progressed = true;
        }
        if (read_state_ == ReadState::HarmonicPayload)
        {
            QVector<float> harmonicSamples;
            QByteArray harmonicPayload;
            if (!tryConsumePayload(harmonicSamples, &harmonicPayload))
            {
                continue;
            }
            const float peakValue = waveformPeakValue(harmonicSamples, peak_search_start_index_, peak_search_end_index_);
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            const quint64 timestampUs = currentTimestampUs();
            const double frameRate = updateFrameRate(nowMs);
            emit frameMetricsDecoded(timestampUs, peakValue, frameRate);
            if (raw_frame_forwarding_enabled_)
            {
                emit rawFrameDecoded(timestampUs, pending_raw_payload_, harmonicPayload, float_encoding_);
            }
            if (last_display_emit_ms_ == 0 || nowMs - last_display_emit_ms_ >= kLiveDisplayRefreshMs)
            {
                last_display_emit_ms_ = nowMs;
                emit displayFrameDecoded(std::move(pending_raw_samples_), std::move(harmonicSamples));
            }
            read_state_ = ReadState::RawHeader;
            progressed = true;
        }
    }
}

WaveformBackend::WaveformBackend(QObject *parent)
    : QObject(parent)
    , receiver_worker_(new TcpWaveReceiverWorker)
    , host_(QStringLiteral("127.0.0.1"))
    , port_(8888)
    , status_text_(QStringLiteral("Disconnected"))
    , read_state_(ReadState::RawHeader)
    , header_byte_order_(HeaderByteOrder::Unknown)
    , float_encoding_(VaporView::TcpFloatEncoding::Unknown)
    , expected_payload_size_(0)
    , frame_count_(0)
    , peak_total_count_(0)
    , frame_rate_(0.0)
    , connected_(false)
    , filter_enabled_(false)
    , peak_filter_mode_(PeakFilterNone)
    , peak_search_start_index_(kDefaultPeakSearchStartIndex)
    , peak_search_end_index_(kDefaultPeakSearchEndIndex)
    , harmonic_filtered_view_(false)
    , scatter_mode_(false)
    , live_display_dirty_(false)
    , samples_dirty_(false)
    , peak_dirty_(false)
    , frame_rate_dirty_(false)
    , raw_frame_forwarding_enabled_(false)
    , filter_min_(0.0)
    , filter_max_(0.0)
{
    loadSettings();
    qRegisterMetaType<QVector<float>>("QVector<float>");
    receiver_worker_->moveToThread(&receiver_thread_);
    connect(&receiver_thread_, &QThread::finished, receiver_worker_, &QObject::deleteLater);
    connect(receiver_worker_, &TcpWaveReceiverWorker::connected, this, &WaveformBackend::onSocketConnected);
    connect(receiver_worker_, &TcpWaveReceiverWorker::disconnected, this, &WaveformBackend::onSocketDisconnected);
    connect(receiver_worker_, &TcpWaveReceiverWorker::socketError, this, [this](const QString& message) {
        connected_ = false;
        setStatusText(translatedTcpSocketMessage(message));
        emit connectedChanged();
        emit notificationRequested(QStringLiteral("error"), message);
        emit notificationRequested(QStringLiteral("error"), chineseTcpSocketMessage(message));
    });
    connect(receiver_worker_, &TcpWaveReceiverWorker::displayFrameDecoded, this, &WaveformBackend::onWorkerDisplayFrameDecoded);
    connect(receiver_worker_, &TcpWaveReceiverWorker::frameMetricsDecoded, this, &WaveformBackend::onWorkerFrameMetricsDecoded);
    connect(receiver_worker_, &TcpWaveReceiverWorker::rawFrameDecoded, this, &WaveformBackend::onWorkerRawFrameDecoded);
    receiver_thread_.start();
    connect(&live_display_timer_, &QTimer::timeout, this, &WaveformBackend::updateLiveDisplay);
    live_display_timer_.start(kLiveDisplayRefreshMs);
}

WaveformBackend::~WaveformBackend()
{
    saveSettings();
    if (receiver_thread_.isRunning())
    {
        QMetaObject::invokeMethod(receiver_worker_, "stop", Qt::BlockingQueuedConnection);
        receiver_thread_.quit();
        receiver_thread_.wait();
    }
}

QString WaveformBackend::host() const { return host_; }
int WaveformBackend::port() const { return port_; }
bool WaveformBackend::connected() const { return connected_; }
QString WaveformBackend::statusText() const { return status_text_; }
double WaveformBackend::frameRate() const { return frame_rate_; }
QVariantList WaveformBackend::rawSamples() const { return raw_samples_cache_; }
QVariantList WaveformBackend::harmonicSamples() const
{
    return harmonic_filtered_view_ ? harmonic_filtered_samples_cache_ : harmonic_samples_cache_;
}
QVariantList WaveformBackend::peakSamples() const { return peak_samples_cache_; }
int WaveformBackend::rawSampleCount() const { return raw_history_.size(); }
int WaveformBackend::harmonicSampleCount() const { return harmonic_history_.size(); }
int WaveformBackend::peakTotalCount() const
{
    return peak_total_count_ > std::numeric_limits<int>::max() ? std::numeric_limits<int>::max()
                                                               : static_cast<int>(peak_total_count_);
}
bool WaveformBackend::filterEnabled() const { return filter_enabled_; }
double WaveformBackend::filterMin() const { return filter_min_; }
double WaveformBackend::filterMax() const { return filter_max_; }
int WaveformBackend::peakFilterMode() const { return peak_filter_mode_; }
int WaveformBackend::peakSearchStartIndex() const { return peak_search_start_index_; }
int WaveformBackend::peakSearchEndIndex() const { return peak_search_end_index_; }
bool WaveformBackend::harmonicFilteredView() const { return harmonic_filtered_view_; }
bool WaveformBackend::scatterMode() const { return scatter_mode_; }
double WaveformBackend::latestPeak() const
{
    for (auto it = peak_history_.crbegin(); it != peak_history_.crend(); ++it)
    {
        if (std::isfinite(*it))
        {
            return *it;
        }
    }
    return 0.0;
}

void WaveformBackend::setHost(const QString& host)
{
    const QString trimmed = host.trimmed();
    if (trimmed.isEmpty() || host_ == trimmed)
    {
        return;
    }
    host_ = trimmed;
    saveSettings();
    emit endpointChanged();
}

void WaveformBackend::setPort(int port)
{
    const int clamped = std::clamp(port, 1, 65535);
    if (port_ == clamped)
    {
        return;
    }
    port_ = clamped;
    saveSettings();
    emit endpointChanged();
}

void WaveformBackend::setFilterEnabled(bool enabled)
{
    const int nextMode = enabled
        ? (peak_filter_mode_ == PeakFilterNone ? PeakFilterKeepRange : peak_filter_mode_)
        : PeakFilterNone;
    if (filter_enabled_ == enabled && peak_filter_mode_ == nextMode)
    {
        return;
    }
    filter_enabled_ = enabled;
    peak_filter_mode_ = nextMode;
    saveSettings();
    rebuildFilteredPeakHistory();
    harmonic_filtered_samples_cache_ = vectorToFilteredVariantList(harmonic_history_);
    emit samplesChanged();
    emit peakSamplesChanged();
    emit filterChanged();
}

void WaveformBackend::setHarmonicFilteredView(bool enabled)
{
    if (harmonic_filtered_view_ == enabled)
    {
        return;
    }
    harmonic_filtered_view_ = enabled;
    emit samplesChanged();
    emit filterChanged();
}

void WaveformBackend::setScatterMode(bool scatter)
{
    if (scatter_mode_ == scatter)
    {
        return;
    }
    scatter_mode_ = scatter;
    emit filterChanged();
}

void WaveformBackend::connectToHost()
{
    if (connected() || status_text_ == QStringLiteral("Connecting..."))
    {
        return;
    }
    buffer_.clear();
    pending_raw_payload_.clear();
    read_state_ = ReadState::RawHeader;
    header_byte_order_ = HeaderByteOrder::Unknown;
    expected_payload_size_ = 0;
    setStatusText(QStringLiteral("Connecting..."));
    QMetaObject::invokeMethod(
        receiver_worker_,
        "connectToHost",
        Qt::QueuedConnection,
        Q_ARG(QString, host_),
        Q_ARG(int, port_));
}

void WaveformBackend::disconnectFromHost()
{
    if (!connected() && status_text_ != QStringLiteral("Connecting..."))
    {
        return;
    }
    setStatusText(QStringLiteral("Disconnecting..."));
    QMetaObject::invokeMethod(receiver_worker_, "disconnectFromHost", Qt::QueuedConnection);
}

void WaveformBackend::toggleConnection()
{
    connected() ? disconnectFromHost() : connectToHost();
}

void WaveformBackend::setRawFrameForwardingEnabled(bool enabled)
{
    if (raw_frame_forwarding_enabled_ == enabled)
    {
        return;
    }
    raw_frame_forwarding_enabled_ = enabled;
    QMetaObject::invokeMethod(
        receiver_worker_,
        "setRawFrameForwardingEnabled",
        Qt::QueuedConnection,
        Q_ARG(bool, enabled));
}

void WaveformBackend::configurePeakSettings(int startIndex, int endIndex, int mode, double minValue, double maxValue)
{
    const int nextStart = std::max(0, startIndex);
    const int nextEnd = std::max(0, endIndex);
    const int nextMode = normalizedPeakFilterMode(mode);
    if (nextEnd > 0 && nextEnd <= nextStart)
    {
        emit notificationRequested(QStringLiteral("warning"), QStringLiteral("搜索终点必须大于搜索起点，或者设置为整帧。"));
        return;
    }
    if ((nextMode == PeakFilterKeepRange || nextMode == PeakFilterExcludeRange) &&
        (!std::isfinite(minValue) || !std::isfinite(maxValue)))
    {
        emit notificationRequested(QStringLiteral("warning"), QStringLiteral("请输入有效的数值区间。"));
        return;
    }
    const bool searchChanged = peak_search_start_index_ != nextStart || peak_search_end_index_ != nextEnd;
    peak_search_start_index_ = nextStart;
    peak_search_end_index_ = nextEnd;
    peak_filter_mode_ = nextMode;
    filter_enabled_ = peak_filter_mode_ != PeakFilterNone;
    if (std::isfinite(minValue))
    {
        filter_min_ = minValue;
    }
    if (std::isfinite(maxValue))
    {
        filter_max_ = maxValue;
    }
    saveSettings();
    QMetaObject::invokeMethod(
        receiver_worker_,
        "setPeakSearchWindow",
        Qt::QueuedConnection,
        Q_ARG(int, peak_search_start_index_),
        Q_ARG(int, peak_search_end_index_));
    if (searchChanged)
    {
        peak_raw_history_.clear();
        peak_history_.clear();
        peak_samples_cache_.clear();
        peak_total_count_ = 0;
        emit peakSamplesChanged();
    }
    else
    {
        rebuildFilteredPeakHistory();
        emit peakSamplesChanged();
    }
    harmonic_filtered_samples_cache_ = vectorToFilteredVariantList(harmonic_history_);
    emit samplesChanged();
    emit filterChanged();
}

void WaveformBackend::clearPeakHistory()
{
    peak_raw_history_.clear();
    peak_history_.clear();
    peak_samples_cache_.clear();
    peak_total_count_ = 0;
    peak_dirty_ = false;
    live_display_dirty_ = samples_dirty_ || frame_rate_dirty_;
    emit peakSamplesChanged();
}

void WaveformBackend::configurePeakFilter(double minValue, double maxValue, bool enabled)
{
    configurePeakSettings(peak_search_start_index_,
                          peak_search_end_index_,
                          enabled ? PeakFilterKeepRange : PeakFilterNone,
                          minValue,
                          maxValue);
}

void WaveformBackend::loadSettings()
{
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("TcpWavePanel"));
    host_ = settings.value(QStringLiteral("connection/host"), host_).toString();
    port_ = settings.value(QStringLiteral("connection/port"), port_).toInt();
    peak_filter_mode_ = settings.contains(QStringLiteral("peak_filter/mode"))
        ? peakFilterModeFromKey(settings.value(QStringLiteral("peak_filter/mode"), QStringLiteral("none")).toString())
        : (settings.value(QStringLiteral("peak_filter/enabled"), false).toBool() ? PeakFilterKeepRange : PeakFilterNone);
    filter_enabled_ = peak_filter_mode_ != PeakFilterNone;
    filter_min_ = settings.value(QStringLiteral("peak_filter/min_value"), 0.0).toDouble();
    filter_max_ = settings.value(QStringLiteral("peak_filter/max_value"), 0.0).toDouble();
    peak_search_start_index_ = std::max(0, settings.value(QStringLiteral("peak_search/start_index"), kDefaultPeakSearchStartIndex).toInt());
    peak_search_end_index_ = std::max(0, settings.value(QStringLiteral("peak_search/end_index"), kDefaultPeakSearchEndIndex).toInt());
    QMetaObject::invokeMethod(
        receiver_worker_,
        "setPeakSearchWindow",
        Qt::QueuedConnection,
        Q_ARG(int, peak_search_start_index_),
        Q_ARG(int, peak_search_end_index_));
}

void WaveformBackend::saveSettings() const
{
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("TcpWavePanel"));
    settings.setValue(QStringLiteral("connection/host"), host_);
    settings.setValue(QStringLiteral("connection/port"), port_);
    settings.setValue(QStringLiteral("peak_filter/enabled"), filter_enabled_);
    settings.setValue(QStringLiteral("peak_filter/mode"), peakFilterModeKey(peak_filter_mode_));
    settings.setValue(QStringLiteral("peak_filter/min_value"), filter_min_);
    settings.setValue(QStringLiteral("peak_filter/max_value"), filter_max_);
    settings.setValue(QStringLiteral("peak_search/start_index"), peak_search_start_index_);
    settings.setValue(QStringLiteral("peak_search/end_index"), peak_search_end_index_);
}

void WaveformBackend::setStatusText(const QString& text)
{
    if (status_text_ == text)
    {
        return;
    }
    status_text_ = text;
    emit statusTextChanged();
}

void WaveformBackend::onSocketConnected()
{
    connected_ = true;
    setStatusText(QStringLiteral("Connected"));
    emit connectedChanged();
    emit notificationRequested(QStringLiteral("info"), QStringLiteral("TCP wave source connected."));
    emit notificationRequested(QStringLiteral("info"), QStringLiteral("TCP 波形源已连接。"));
}

void WaveformBackend::onSocketDisconnected()
{
    connected_ = false;
    setStatusText(QStringLiteral("Disconnected"));
    emit connectedChanged();
    emit notificationRequested(QStringLiteral("info"), QStringLiteral("TCP wave source disconnected."));
    emit notificationRequested(QStringLiteral("info"), QStringLiteral("TCP 波形源已断开。"));
}

void WaveformBackend::onSocketError()
{
    const QString message = socket_.errorString();
    connected_ = false;
    setStatusText(translatedTcpSocketMessage(message));
    emit connectedChanged();
    emit notificationRequested(QStringLiteral("error"), message);
    emit notificationRequested(QStringLiteral("error"), chineseTcpSocketMessage(message));
}

void WaveformBackend::onWorkerDisplayFrameDecoded(
    QVector<float> rawSamples,
    QVector<float> harmonicSamples)
{
    raw_history_ = std::move(rawSamples);
    harmonic_history_ = std::move(harmonicSamples);
    markLiveDisplayDirty(true, false, false);
}

void WaveformBackend::onWorkerFrameMetricsDecoded(
    quint64 timestampUs,
    float peakValue,
    double frameRate)
{
    Q_UNUSED(timestampUs);

    if (std::isfinite(peakValue))
    {
        ++peak_total_count_;
        peak_raw_history_.append(peakValue);
        while (peak_raw_history_.size() > kPeakTrendFrameWindow)
        {
            peak_raw_history_.removeFirst();
        }
        rebuildFilteredPeakHistory();
        markLiveDisplayDirty(false, true, false);
    }

    ++frame_count_;
    if (frameRate > 0.0)
    {
        frame_rate_ = frameRate;
        markLiveDisplayDirty(false, false, true);
    }
}

void WaveformBackend::onWorkerRawFrameDecoded(
    quint64 timestampUs,
    QByteArray rawSignalPayload,
    QByteArray harmonicPayload,
    VaporView::TcpFloatEncoding floatEncoding)
{
    float_encoding_ = floatEncoding;
    emit rawWaveFrameReady(timestampUs, std::move(rawSignalPayload), std::move(harmonicPayload), floatEncoding);
}

void WaveformBackend::onReadyRead()
{
    buffer_.append(socket_.readAll());
    processBuffer();
}

bool WaveformBackend::isValidPayloadSize(qint32 candidate) const
{
    return candidate > 0 && candidate <= kMaxTcpPayloadSize && candidate % kFloatSize == 0;
}

qint32 WaveformBackend::decodeHeaderValue(const char *raw, HeaderByteOrder order) const
{
    if (order == HeaderByteOrder::BigEndian)
    {
        return qFromBigEndian<qint32>(reinterpret_cast<const uchar*>(raw));
    }
    return qFromLittleEndian<qint32>(reinterpret_cast<const uchar*>(raw));
}

bool WaveformBackend::tryConsumeHeader()
{
    if (buffer_.size() < 4)
    {
        return false;
    }
    HeaderByteOrder order = header_byte_order_;
    qint32 size = 0;
    if (order == HeaderByteOrder::Unknown)
    {
        const qint32 little = decodeHeaderValue(buffer_.constData(), HeaderByteOrder::LittleEndian);
        const qint32 big = decodeHeaderValue(buffer_.constData(), HeaderByteOrder::BigEndian);
        if (isValidPayloadSize(little))
        {
            order = HeaderByteOrder::LittleEndian;
            size = little;
        }
        else if (isValidPayloadSize(big))
        {
            order = HeaderByteOrder::BigEndian;
            size = big;
        }
        else
        {
            buffer_.remove(0, 1);
            return true;
        }
    }
    else
    {
        size = decodeHeaderValue(buffer_.constData(), order);
        if (!isValidPayloadSize(size))
        {
            header_byte_order_ = HeaderByteOrder::Unknown;
            buffer_.remove(0, 1);
            return true;
        }
    }
    header_byte_order_ = order;
    expected_payload_size_ = size;
    buffer_.remove(0, 4);
    return true;
}

bool WaveformBackend::tryConsumePayload(QVector<float>& output, QByteArray *rawPayload)
{
    if (expected_payload_size_ <= 0 || buffer_.size() < expected_payload_size_)
    {
        return false;
    }
    const QByteArray payload = buffer_.left(expected_payload_size_);
    buffer_.remove(0, expected_payload_size_);
    if (float_encoding_ == VaporView::TcpFloatEncoding::Unknown)
    {
        float_encoding_ = VaporView::autoDetectTcpFloatEncoding(payload);
    }
    output = VaporView::decodeTcpFloatPayload(payload, float_encoding_);
    if (rawPayload)
    {
        *rawPayload = payload;
    }
    expected_payload_size_ = 0;
    return true;
}

QVariantList WaveformBackend::vectorToVariantList(const QVector<float>& values, int maxCount) const
{
    QVariantList list;
    if (values.isEmpty())
    {
        return list;
    }
    const int valueCount = static_cast<int>(values.size());
    const int safeMaxCount = std::max(1, maxCount);
    const int stride = std::max(1, (valueCount + safeMaxCount - 1) / safeMaxCount);
    for (int i = 0; i < valueCount; i += stride)
    {
        list.append(values.at(i));
    }
    return list;
}

QVariantList WaveformBackend::vectorToFilteredVariantList(const QVector<float>& values, int maxCount) const
{
    QVariantList list;
    if (values.isEmpty())
    {
        return list;
    }
    const int valueCount = static_cast<int>(values.size());
    const int safeMaxCount = std::max(1, maxCount);
    const int searchStart = std::clamp(peak_search_start_index_, 0, valueCount);
    const int searchEnd = peak_search_end_index_ <= 0
        ? valueCount
        : std::clamp(peak_search_end_index_, 0, valueCount);
    if (searchStart >= searchEnd)
    {
        return list;
    }
    const int windowCount = searchEnd - searchStart;
    const int windowStride = std::max(1, (windowCount + safeMaxCount - 1) / safeMaxCount);
    const double rangeMin = std::min(filter_min_, filter_max_);
    const double rangeMax = std::max(filter_min_, filter_max_);
    const double quietNaN = std::numeric_limits<double>::quiet_NaN();
    double iqrLowerBound = -std::numeric_limits<double>::infinity();
    double iqrUpperBound = std::numeric_limits<double>::infinity();
    if (peak_filter_mode_ == PeakFilterIqrOutlier)
    {
        QVector<double> finiteValues;
        finiteValues.reserve(valueCount);
        for (int index = searchStart; index < searchEnd; ++index)
        {
            const float value = values.at(index);
            if (std::isfinite(value))
            {
                finiteValues.push_back(static_cast<double>(value));
            }
        }
        if (finiteValues.size() >= 4)
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
    }
    for (int i = searchStart; i < searchEnd; i += windowStride)
    {
        const double value = values.at(i);
        bool keepValue = std::isfinite(value);
        if (keepValue)
        {
            switch (peak_filter_mode_)
            {
            case PeakFilterIqrOutlier:
                keepValue = value >= iqrLowerBound && value <= iqrUpperBound;
                break;
            case PeakFilterKeepRange:
                keepValue = value >= rangeMin && value <= rangeMax;
                break;
            case PeakFilterExcludeRange:
                keepValue = !(value >= rangeMin && value <= rangeMax);
                break;
            case PeakFilterNone:
            default:
                keepValue = true;
                break;
            }
        }
        list.append(keepValue ? QVariant(value) : QVariant(quietNaN));
    }
    const int lastIndex = searchEnd - 1;
    if ((lastIndex - searchStart) % windowStride != 0)
    {
        const double value = values.at(lastIndex);
        bool keepValue = std::isfinite(value);
        if (keepValue)
        {
            switch (peak_filter_mode_)
            {
            case PeakFilterIqrOutlier:
                keepValue = value >= iqrLowerBound && value <= iqrUpperBound;
                break;
            case PeakFilterKeepRange:
                keepValue = value >= rangeMin && value <= rangeMax;
                break;
            case PeakFilterExcludeRange:
                keepValue = !(value >= rangeMin && value <= rangeMax);
                break;
            case PeakFilterNone:
            default:
                keepValue = true;
                break;
            }
        }
        list.append(keepValue ? QVariant(value) : QVariant(quietNaN));
    }
    return list;
}

void WaveformBackend::updateFrameRate(qint64 nowMs)
{
    frame_arrivals_ms_.append(nowMs);
    while (frame_arrivals_ms_.size() > 60)
    {
        frame_arrivals_ms_.removeFirst();
    }
    if (frame_arrivals_ms_.size() >= 2)
    {
        const qint64 span = frame_arrivals_ms_.last() - frame_arrivals_ms_.first();
        if (span > 0)
        {
            frame_rate_ = static_cast<double>(frame_arrivals_ms_.size() - 1) * 1000.0 / static_cast<double>(span);
            markLiveDisplayDirty(false, false, true);
        }
    }
}

void WaveformBackend::markLiveDisplayDirty(bool samples, bool peak, bool frameRate)
{
    live_display_dirty_ = true;
    samples_dirty_ = samples_dirty_ || samples;
    peak_dirty_ = peak_dirty_ || peak;
    frame_rate_dirty_ = frame_rate_dirty_ || frameRate;
}

void WaveformBackend::updateLiveDisplay()
{
    if (!live_display_dirty_)
    {
        return;
    }

    const bool emitSamples = samples_dirty_;
    const bool emitPeak = peak_dirty_;
    const bool emitFrameRate = frame_rate_dirty_;

    live_display_dirty_ = false;
    samples_dirty_ = false;
    peak_dirty_ = false;
    frame_rate_dirty_ = false;

    if (emitSamples)
    {
        raw_samples_cache_ = vectorToVariantList(raw_history_);
        harmonic_samples_cache_ = vectorToVariantList(harmonic_history_);
        harmonic_filtered_samples_cache_ = vectorToFilteredVariantList(harmonic_history_);
    }
    if (emitPeak)
    {
        peak_samples_cache_ = vectorToVariantList(peak_history_, kPeakTrendFrameWindow);
    }

    if (emitFrameRate)
    {
        emit frameRateChanged();
    }
    if (emitSamples)
    {
        emit samplesChanged();
    }
    if (emitPeak)
    {
        emit peakSamplesChanged();
    }
}

void WaveformBackend::rebuildFilteredPeakHistory()
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
    if (peak_filter_mode_ == PeakFilterIqrOutlier && finiteValues.size() >= 4)
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

    const double rangeMin = std::min(filter_min_, filter_max_);
    const double rangeMax = std::max(filter_min_, filter_max_);
    const float quietNaN = std::numeric_limits<float>::quiet_NaN();
    for (float rawValue : peak_raw_history_)
    {
        bool keepValue = std::isfinite(rawValue);
        if (keepValue)
        {
            const double value = static_cast<double>(rawValue);
            switch (peak_filter_mode_)
            {
            case PeakFilterIqrOutlier:
                keepValue = value >= iqrLowerBound && value <= iqrUpperBound;
                break;
            case PeakFilterKeepRange:
                keepValue = value >= rangeMin && value <= rangeMax;
                break;
            case PeakFilterExcludeRange:
                keepValue = !(value >= rangeMin && value <= rangeMax);
                break;
            case PeakFilterNone:
            default:
                keepValue = true;
                break;
            }
        }
        peak_history_.append(keepValue ? rawValue : quietNaN);
    }

    while (peak_history_.size() > kPeakTrendFrameWindow)
    {
        peak_history_.removeFirst();
    }
    peak_samples_cache_ = vectorToVariantList(peak_history_, kPeakTrendFrameWindow);
}

void WaveformBackend::processBuffer()
{
    bool progressed = true;
    while (progressed)
    {
        progressed = false;
        if (read_state_ == ReadState::RawHeader || read_state_ == ReadState::HarmonicHeader)
        {
            progressed = tryConsumeHeader();
            if (progressed && expected_payload_size_ > 0)
            {
                read_state_ = (read_state_ == ReadState::RawHeader) ? ReadState::RawPayload : ReadState::HarmonicPayload;
            }
        }
        if (read_state_ == ReadState::RawPayload)
        {
            QVector<float> samples;
            QByteArray payload;
            if (!tryConsumePayload(samples, &payload))
            {
                continue;
            }
            raw_history_ = samples;
            pending_raw_payload_ = payload;
            read_state_ = ReadState::HarmonicHeader;
            progressed = true;
            markLiveDisplayDirty(true, false, false);
        }
        if (read_state_ == ReadState::HarmonicPayload)
        {
            QVector<float> samples;
            QByteArray harmonicPayload;
            if (!tryConsumePayload(samples, &harmonicPayload))
            {
                continue;
            }
            harmonic_history_ = samples;
            const float peakValue = waveformPeakValue(harmonic_history_, peak_search_start_index_, peak_search_end_index_);
            if (std::isfinite(peakValue))
            {
                ++peak_total_count_;
                peak_raw_history_.append(peakValue);
                while (peak_raw_history_.size() > kPeakTrendFrameWindow)
                {
                    peak_raw_history_.removeFirst();
                }
                rebuildFilteredPeakHistory();
                markLiveDisplayDirty(false, true, false);
            }
            ++frame_count_;
            const quint64 timestampUs = currentTimestampUs();
            updateFrameRate(QDateTime::currentMSecsSinceEpoch());
            markLiveDisplayDirty(true, false, false);
            emit rawWaveFrameReady(timestampUs, pending_raw_payload_, harmonicPayload, float_encoding_);
            read_state_ = ReadState::RawHeader;
            progressed = true;
        }
    }
}

RecordingBackend::RecordingBackend(DeviceBackend *deviceBackend, WaveformBackend *waveformBackend, QObject *parent)
    : QObject(parent)
    , device_backend_(deviceBackend)
    , waveform_backend_(waveformBackend)
    , session_start_time_us_(0)
    , recording_thread_running_(false)
    , recording_paused_(false)
    , export_rate_hz_(20)
    , waveform_export_rate_hz_(0)
    , recording_entry_count_(0)
    , waveform_frame_count_(0)
    , waveform_file_count_(0)
    , raw_epsilon_record_count_(0)
    , raw_ptb_record_count_(0)
    , raw_hmp_record_count_(0)
    , raw_lidar_record_count_(0)
    , raw_tcp_wave_record_count_(0)
    , steady_clock_anchor_(std::chrono::steady_clock::now())
    , system_clock_anchor_(std::chrono::system_clock::now())
{
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
    recording_directory_ = settings.value(QStringLiteral("recording_directory"), defaultRecordingDirectory()).toString();
    export_rate_hz_ = std::clamp(settings.value(QStringLiteral("recording_export_rate_hz"), 20).toInt(), 1, 200);
    waveform_export_rate_hz_ = std::clamp(settings.value(QStringLiteral("waveform_recording_rate_hz"), 0).toInt(), 0, 200);

    connect(device_backend_, &DeviceBackend::epsilonRawFrame, this, &RecordingBackend::onEpsilonRawFrame);
    connect(device_backend_, &DeviceBackend::ptbRawFrame, this, &RecordingBackend::onPtbRawFrame);
    connect(device_backend_, &DeviceBackend::hmpRawFrame, this, &RecordingBackend::onHmpRawFrame);
    connect(device_backend_, &DeviceBackend::lidarRawFrame, this, &RecordingBackend::onLidarRawFrame);
    connect(waveform_backend_, &WaveformBackend::rawWaveFrameReady, this, &RecordingBackend::onTcpRawWaveFrame);
    connect(&stats_timer_, &QTimer::timeout, this, &RecordingBackend::updateStats);
    stats_timer_.start(1000);
}

RecordingBackend::~RecordingBackend()
{
    stopRecording();
}

QString RecordingBackend::recordingDirectory() const { return recording_directory_; }
QString RecordingBackend::sessionDirectory() const { return session_directory_; }
QString RecordingBackend::status() const
{
    if (recording()) return QStringLiteral("recording");
    if (paused()) return QStringLiteral("paused");
    return QStringLiteral("stopped");
}
bool RecordingBackend::recording() const { return sensors_file_ && sensors_file_->isOpen() && !recording_paused_ && recording_thread_running_.load(); }
bool RecordingBackend::paused() const { return sensors_file_ && sensors_file_->isOpen() && recording_paused_; }
qint64 RecordingBackend::sensorRows() const { return recording_entry_count_.load(); }
qint64 RecordingBackend::waveformFrames() const { return waveform_frame_count_.load(); }
int RecordingBackend::exportRateHz() const { return export_rate_hz_; }
int RecordingBackend::waveformExportRateHz() const { return waveform_export_rate_hz_; }

QString RecordingBackend::fileSizeText() const
{
    qint64 total = 0;
    for (const QString& path : {sensors_filename_, raw_epsilon_filename_, raw_ptb_filename_, raw_hmp_filename_, raw_lidar_filename_, raw_tcp_wave_filename_})
    {
        if (!path.isEmpty())
        {
            total += QFileInfo(path).size();
        }
    }
    return formatBytes(total);
}

QString RecordingBackend::recordUsageText() const
{
    return fileSizeText();
}

QString RecordingBackend::diskRemainingText() const
{
    const QStorageInfo storage = storageInfoForPath(recording_directory_);
    if (!storage.isValid() || !storage.isReady())
    {
        return QStringLiteral("--");
    }
    return formatBytes(storage.bytesAvailable());
}

QString RecordingBackend::diskTotalText() const
{
    const QStorageInfo storage = storageInfoForPath(recording_directory_);
    if (!storage.isValid() || !storage.isReady())
    {
        return QStringLiteral("--");
    }
    return formatBytes(storage.bytesTotal());
}

QString RecordingBackend::durationText() const
{
    if (session_start_time_us_ == 0)
    {
        return QStringLiteral("00:00:00");
    }
    const qint64 elapsedSec = static_cast<qint64>((currentTimestampUs() - session_start_time_us_) / 1000000ULL);
    return formatDuration(elapsedSec);
}

QString RecordingBackend::systemUptimeText() const
{
    const auto elapsed = std::chrono::steady_clock::now() - steady_clock_anchor_;
    const qint64 elapsedSec = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    return formatDuration(elapsedSec);
}

void RecordingBackend::setRecordingDirectory(const QString& directory)
{
    const QString normalized = QDir::fromNativeSeparators(directory.trimmed());
    if (normalized.isEmpty() || recording_directory_ == normalized)
    {
        return;
    }
    recording_directory_ = normalized;
    QSettings(QStringLiteral("VaporView"), QStringLiteral("MainWindow")).setValue(QStringLiteral("recording_directory"), recording_directory_);
    emit recordingDirectoryChanged();
    emit recordingStatsChanged();
}

void RecordingBackend::setExportRateHz(int hz)
{
    const int clamped = std::clamp(hz, 1, 200);
    if (export_rate_hz_ == clamped)
    {
        return;
    }
    export_rate_hz_ = clamped;
    QSettings(QStringLiteral("VaporView"), QStringLiteral("MainWindow")).setValue(QStringLiteral("recording_export_rate_hz"), export_rate_hz_);
    emit exportRateHzChanged();
}

void RecordingBackend::setWaveformExportRateHz(int hz)
{
    const int clamped = std::clamp(hz, 0, 200);
    if (waveform_export_rate_hz_ == clamped)
    {
        return;
    }
    waveform_export_rate_hz_ = clamped;
    QSettings(QStringLiteral("VaporView"), QStringLiteral("MainWindow")).setValue(QStringLiteral("waveform_recording_rate_hz"), waveform_export_rate_hz_);
    emit exportRateHzChanged();
}

QString RecordingBackend::defaultRecordingDirectory() const
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i)
    {
        if (QFileInfo::exists(dir.filePath(QStringLiteral("CMakeLists.txt"))) && QFileInfo::exists(dir.filePath(QStringLiteral("README.md"))))
        {
            return dir.filePath(QStringLiteral("data"));
        }
        if (!dir.cdUp())
        {
            break;
        }
    }
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data"));
}

QString RecordingBackend::locateRepositoryRoot() const
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i)
    {
        if (QFileInfo::exists(dir.filePath(QStringLiteral("CMakeLists.txt"))) && QFileInfo::exists(dir.filePath(QStringLiteral("README.md"))))
        {
            return dir.path();
        }
        if (!dir.cdUp())
        {
            break;
        }
    }
    return QString();
}

QString RecordingBackend::sessionNameTimestamp() const { return sessionDirectoryTimestamp(); }
QString RecordingBackend::timestampUtc() const { return ::timestampUtc(); }
quint64 RecordingBackend::currentTimestampUs() const { return ::currentTimestampUs(); }

quint64 RecordingBackend::steadyToEpochUs(const std::chrono::steady_clock::time_point& timePoint) const
{
    const auto delta = timePoint - steady_clock_anchor_;
    const auto systemTime = system_clock_anchor_ + delta;
    return static_cast<quint64>(std::chrono::duration_cast<std::chrono::microseconds>(systemTime.time_since_epoch()).count());
}

bool RecordingBackend::prepareRecordingSessionLayout(const QString& recordsPath, const QString& sessionName)
{
    QDir recordsDir(recordsPath);
    if (!recordsDir.exists() && !recordsDir.mkpath(QStringLiteral(".")))
    {
        return false;
    }
    QString finalSessionName = sessionName;
    QString finalSessionDirectory = recordsDir.filePath(finalSessionName);
    int suffix = 1;
    while (QFileInfo::exists(finalSessionDirectory))
    {
        finalSessionName = QStringLiteral("%1_%2").arg(sessionName).arg(suffix++);
        finalSessionDirectory = recordsDir.filePath(finalSessionName);
    }
    QDir sessionDir(finalSessionDirectory);
    if (!recordsDir.mkpath(finalSessionName) ||
        !sessionDir.mkpath(QStringLiteral("sensors")) ||
        !sessionDir.mkpath(QStringLiteral("raw")) ||
        !sessionDir.mkpath(QStringLiteral("logs")) ||
        !sessionDir.mkpath(QStringLiteral("config")))
    {
        return false;
    }
    session_name_ = finalSessionName;
    session_directory_ = QDir::fromNativeSeparators(finalSessionDirectory);
    sensors_filename_ = QDir::fromNativeSeparators(sessionDir.filePath(QStringLiteral("sensors/devices.csv")));
    raw_epsilon_filename_ = QDir::fromNativeSeparators(sessionDir.filePath(QStringLiteral("raw/epsilon.dat")));
    raw_ptb_filename_ = QDir::fromNativeSeparators(sessionDir.filePath(QStringLiteral("raw/ptb.dat")));
    raw_hmp_filename_ = QDir::fromNativeSeparators(sessionDir.filePath(QStringLiteral("raw/hmp.dat")));
    raw_lidar_filename_ = QDir::fromNativeSeparators(sessionDir.filePath(QStringLiteral("raw/lidar.dat")));
    raw_tcp_wave_filename_ = QDir::fromNativeSeparators(sessionDir.filePath(QStringLiteral("raw/tcp_wave.dat")));
    raw_dat_doc_filename_ = QDir::fromNativeSeparators(sessionDir.filePath(QStringLiteral("raw_dat_format.md")));
    session_metadata_filename_ = QDir::fromNativeSeparators(sessionDir.filePath(QStringLiteral("session.json")));
    event_log_filename_ = QDir::fromNativeSeparators(sessionDir.filePath(QStringLiteral("logs/event_log.csv")));
    error_log_filename_ = QDir::fromNativeSeparators(sessionDir.filePath(QStringLiteral("logs/error_log.txt")));
    device_config_filename_ = QDir::fromNativeSeparators(sessionDir.filePath(QStringLiteral("config/device_config.json")));
    return true;
}

bool RecordingBackend::copyRawDatFormatDocumentToSession()
{
    const QString repositoryRoot = locateRepositoryRoot();
    if (repositoryRoot.isEmpty() || raw_dat_doc_filename_.isEmpty())
    {
        return false;
    }
    const QString sourcePath = QDir(repositoryRoot).filePath(QStringLiteral("docs/raw_dat_format.md"));
    if (!QFileInfo::exists(sourcePath))
    {
        return false;
    }
    QFile::remove(raw_dat_doc_filename_);
    return QFile::copy(sourcePath, raw_dat_doc_filename_);
}

bool RecordingBackend::openUnifiedRawDatFile(std::unique_ptr<QFile>& file, const QString& filename, quint16 sourceId)
{
    file = std::make_unique<QFile>(filename);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        file.reset();
        return false;
    }
    UnifiedRawFileHeader header{};
    std::memcpy(header.magic, kUnifiedRawMagic, sizeof(header.magic));
    header.version = qToLittleEndian(kUnifiedRawFormatVersion);
    header.header_size = qToLittleEndian(static_cast<quint32>(sizeof(UnifiedRawFileHeader)));
    header.source_id = qToLittleEndian(sourceId);
    header.reserved = 0;
    if (file->write(reinterpret_cast<const char*>(&header), sizeof(header)) != static_cast<qint64>(sizeof(header)))
    {
        file.reset();
        return false;
    }
    file->flush();
    return true;
}

bool RecordingBackend::writeUnifiedRawRecord(QFile *file,
                                             std::atomic<quint64>& recordCount,
                                             quint16 sourceId,
                                             quint16 recordType,
                                             quint32 flags,
                                             quint64 hostTimestampUs,
                                             const void *payload,
                                             size_t payloadSize)
{
    if (!file || !file->isOpen() || (payloadSize > 0 && !payload) ||
        payloadSize > static_cast<size_t>(std::numeric_limits<quint32>::max()))
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(recording_files_mutex_);
    if (!file->isOpen())
    {
        return false;
    }
    const quint64 sequence = recordCount.load(std::memory_order_relaxed);
    UnifiedRawRecordHeader header{};
    header.marker = qToLittleEndian(kUnifiedRawRecordMarker);
    header.header_size = qToLittleEndian(static_cast<quint32>(sizeof(UnifiedRawRecordHeader)));
    header.host_timestamp_us = qToLittleEndian(hostTimestampUs);
    header.payload_size = qToLittleEndian(static_cast<quint32>(payloadSize));
    header.source_id = qToLittleEndian(sourceId);
    header.record_type = qToLittleEndian(recordType);
    header.flags = qToLittleEndian(flags);
    header.sequence = qToLittleEndian(sequence);
    if (file->write(reinterpret_cast<const char*>(&header), sizeof(header)) != static_cast<qint64>(sizeof(header)))
    {
        return false;
    }
    if (payloadSize > 0 &&
        file->write(reinterpret_cast<const char*>(payload), static_cast<qint64>(payloadSize)) != static_cast<qint64>(payloadSize))
    {
        return false;
    }
    recordCount.store(sequence + 1, std::memory_order_relaxed);
    return true;
}

void RecordingBackend::writeSensorsHeader()
{
    if (!sensors_file_ || !sensors_file_->isOpen())
    {
        return;
    }
    QTextStream out(sensors_file_.get());
    out.setEncoding(QStringConverter::Utf8);
    out.setGenerateByteOrderMark(true);
    out
        << "record_timestamp_us,"
        << "epsilon_host_timestamp_us,epsilon_device_timestamp_us,epsilon_utc_unix_s,epsilon_utc_microseconds,"
        << "nav_lat_deg,nav_lon_deg,nav_height_m,"
        << "ecef_x_m,ecef_y_m,ecef_z_m,"
        << "ned_n_m,ned_e_m,ned_d_m,"
        << "vel_n_mps,vel_e_mps,vel_d_mps,"
        << "body_vel_x_mps,body_vel_y_mps,body_vel_z_mps,"
        << "body_acc_x_mps2,body_acc_y_mps2,body_acc_z_mps2,"
        << "roll_deg,pitch_deg,yaw_deg,"
        << "quat_w,quat_x,quat_y,quat_z,"
        << "ang_vel_x_radps,ang_vel_y_radps,ang_vel_z_radps,"
        << "imu_acc_x_mps2,imu_acc_y_mps2,imu_acc_z_mps2,"
        << "imu_gyr_x_radps,imu_gyr_y_radps,imu_gyr_z_radps,"
        << "mag_x_mg,mag_y_mg,mag_z_mg,"
        << "gnss_fix,gnss_satellites,hdop,vdop,hacc_m,vacc_m,"
        << "lat_std_m,lon_std_m,height_std_m,diff_age_s,"
        << "heading_valid,system_status_bits,filter_status_bits,update_status_bits,"
        << "epsilon_valid,epsilon_error_message,"
        << "hmp_temperature_c,hmp_humidity_rh,ptb_pressure_hpa,lidar_distance_m,lidar_signal_strength,lidar_valid\n";
    out.flush();
}

void RecordingBackend::writeSessionMetadata(const QString& endTimeUtc)
{
    if (session_metadata_filename_.isEmpty())
    {
        return;
    }
    QJsonObject root;
    root[QStringLiteral("app")] = QStringLiteral("VaporView");
    root[QStringLiteral("version")] = QStringLiteral("1.0.0");
    root[QStringLiteral("session_name")] = session_name_;
    root[QStringLiteral("session_directory")] = session_directory_;
    root[QStringLiteral("start_time_utc")] = session_start_time_utc_;
    root[QStringLiteral("end_time_utc")] = endTimeUtc;
    root[QStringLiteral("sensor_export_rate_hz")] = export_rate_hz_;
    root[QStringLiteral("waveform_export_rate_hz")] = waveform_export_rate_hz_;
    root[QStringLiteral("waveform_export_mode")] = waveform_export_rate_hz_ > 0 ? QStringLiteral("fixed_rate") : QStringLiteral("per_frame");
    root[QStringLiteral("sensor_rows")] = static_cast<double>(recording_entry_count_.load());
    root[QStringLiteral("waveform_frames")] = static_cast<double>(waveform_frame_count_.load());
    root[QStringLiteral("waveform_files")] = static_cast<double>(waveform_file_count_.load());
    root[QStringLiteral("sensors_csv")] = QStringLiteral("sensors/devices.csv");
    root[QStringLiteral("raw_tcp_wave")] = QStringLiteral("raw/tcp_wave.dat");
    QSaveFile file(session_metadata_filename_);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.commit();
    }
}

void RecordingBackend::writeDeviceConfigSnapshot()
{
    QJsonObject root;
    root[QStringLiteral("created_utc")] = timestampUtc();
    QJsonArray devices;
    for (int row = 0; row < device_backend_->devices()->rowCount(); ++row)
    {
        const QVariantMap d = device_backend_->devices()->get(row);
        QJsonObject obj;
        for (auto it = d.cbegin(); it != d.cend(); ++it)
        {
            obj[it.key()] = QJsonValue::fromVariant(it.value());
        }
        devices.append(obj);
    }
    root[QStringLiteral("devices")] = devices;
    QSaveFile file(device_config_filename_);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.commit();
    }
}

void RecordingBackend::appendEventLogLine(const QString& level, const QString& message)
{
    std::lock_guard<std::mutex> lock(recording_files_mutex_);
    if (!event_log_file_ || !event_log_file_->isOpen())
    {
        return;
    }
    QTextStream out(event_log_file_.get());
    out.setEncoding(QStringConverter::Utf8);
    out << csvEscape(timestampUtc()) << ',' << currentTimestampUs() << ',' << csvEscape(level) << ',' << csvEscape(message) << '\n';
    out.flush();
}

void RecordingBackend::appendErrorLogLine(const QString& message)
{
    std::lock_guard<std::mutex> lock(recording_files_mutex_);
    if (!error_log_file_ || !error_log_file_->isOpen())
    {
        return;
    }
    QTextStream out(error_log_file_.get());
    out.setEncoding(QStringConverter::Utf8);
    out << '[' << timestampUtc() << "] " << message << '\n';
    out.flush();
}

void RecordingBackend::log(const QString& message, const QString& level)
{
    appendEventLogLine(level, message);
    if (shouldMirrorToErrorLog(message))
    {
        appendErrorLogLine(message);
    }
    emit notificationRequested(level, message);
}

bool RecordingBackend::startRecording()
{
    if (!device_backend_->anySerialCollectorRunning() && !waveform_backend_->connected())
    {
        log(QStringLiteral("At least one serial device or the TCP wave link must be connected before recording."), QStringLiteral("warning"));
        return false;
    }
    if (sensors_file_ && sensors_file_->isOpen())
    {
        if (!recording_paused_)
        {
            return true;
        }
        startRecordingWorkers();
        recording_paused_ = false;
        writeSessionMetadata();
        emit recordingStateChanged();
        return true;
    }
    QString recordsPath = recording_directory_.trimmed();
    if (recordsPath.isEmpty())
    {
        recordsPath = defaultRecordingDirectory();
        recording_directory_ = recordsPath;
    }
    const QString sessionName = QStringLiteral("session_%1").arg(sessionNameTimestamp());
    if (!prepareRecordingSessionLayout(recordsPath, sessionName))
    {
        log(QStringLiteral("Failed to create session directories."), QStringLiteral("error"));
        return false;
    }
    sensors_file_ = std::make_unique<QFile>(sensors_filename_);
    event_log_file_ = std::make_unique<QFile>(event_log_filename_);
    error_log_file_ = std::make_unique<QFile>(error_log_filename_);
    if (!sensors_file_->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
        !event_log_file_->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
        !error_log_file_->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
        !openUnifiedRawDatFile(raw_epsilon_file_, raw_epsilon_filename_, kRawSourceEpsilon) ||
        !openUnifiedRawDatFile(raw_ptb_file_, raw_ptb_filename_, kRawSourcePtb) ||
        !openUnifiedRawDatFile(raw_hmp_file_, raw_hmp_filename_, kRawSourceHmp) ||
        !openUnifiedRawDatFile(raw_lidar_file_, raw_lidar_filename_, kRawSourceLidar) ||
        !openUnifiedRawDatFile(raw_tcp_wave_file_, raw_tcp_wave_filename_, kRawSourceTcpWave))
    {
        sensors_file_.reset();
        resetUnifiedRawDatFiles();
        event_log_file_.reset();
        error_log_file_.reset();
        log(QStringLiteral("Failed to open session files for writing."), QStringLiteral("error"));
        return false;
    }
    session_start_time_utc_ = timestampUtc();
    session_start_time_us_ = currentTimestampUs();
    recording_entry_count_.store(0);
    waveform_frame_count_.store(0);
    waveform_file_count_.store(0);
    raw_epsilon_record_count_.store(0);
    raw_ptb_record_count_.store(0);
    raw_hmp_record_count_.store(0);
    raw_lidar_record_count_.store(0);
    raw_tcp_wave_record_count_.store(0);
    {
        QTextStream eventOut(event_log_file_.get());
        eventOut.setEncoding(QStringConverter::Utf8);
        eventOut << "timestamp_utc,timestamp_us,level,message\n";
        eventOut.flush();
    }
    writeSensorsHeader();
    copyRawDatFormatDocumentToSession();
    writeSessionMetadata();
    writeDeviceConfigSnapshot();
    startRecordingWorkers();
    log(QStringLiteral("Started recording session: %1").arg(session_directory_));
    emit recordingStateChanged();
    emit recordingDirectoryChanged();
    return true;
}

void RecordingBackend::pauseRecording()
{
    if (!sensors_file_ || !sensors_file_->isOpen() || recording_paused_)
    {
        return;
    }
    stopRecordingWorkers();
    recording_paused_ = true;
    writeSessionMetadata();
    log(QStringLiteral("Paused recording session: %1").arg(session_directory_));
    emit recordingStateChanged();
}

void RecordingBackend::stopRecording()
{
    const bool hadOpenSession = sensors_file_ && sensors_file_->isOpen();
    stopRecordingWorkers();
    if (!hadOpenSession)
    {
        recording_paused_ = false;
        emit recordingStateChanged();
        return;
    }
    const QString completedSession = session_directory_;
    writeSessionMetadata(timestampUtc());
    log(QStringLiteral("Stopped recording (%1 sensor rows, %2 waveform frames): %3")
            .arg(recording_entry_count_.load())
            .arg(waveform_frame_count_.load())
            .arg(session_directory_));
    closeUnifiedRawDatFiles();
    {
        std::lock_guard<std::mutex> lock(recording_files_mutex_);
        if (sensors_file_ && sensors_file_->isOpen()) sensors_file_->close();
        if (event_log_file_ && event_log_file_->isOpen()) event_log_file_->close();
        if (error_log_file_ && error_log_file_->isOpen()) error_log_file_->close();
    }
    sensors_file_.reset();
    resetUnifiedRawDatFiles();
    event_log_file_.reset();
    error_log_file_.reset();
    recording_paused_ = false;
    emit sessionCompleted(completedSession);
    emit recordingStateChanged();
    emit recordingStatsChanged();
}

void RecordingBackend::openLatestSessionInViewer()
{
    if (!session_directory_.isEmpty())
    {
        emit sessionCompleted(session_directory_);
    }
}

void RecordingBackend::closeUnifiedRawDatFiles()
{
    std::lock_guard<std::mutex> lock(recording_files_mutex_);
    for (QFile *file : {raw_epsilon_file_.get(), raw_ptb_file_.get(), raw_hmp_file_.get(), raw_lidar_file_.get(), raw_tcp_wave_file_.get()})
    {
        if (file && file->isOpen())
        {
            file->flush();
            file->close();
        }
    }
}

void RecordingBackend::resetUnifiedRawDatFiles()
{
    raw_epsilon_file_.reset();
    raw_ptb_file_.reset();
    raw_hmp_file_.reset();
    raw_lidar_file_.reset();
    raw_tcp_wave_file_.reset();
}

void RecordingBackend::startRecordingWorkers()
{
    if (recording_thread_running_.load())
    {
        return;
    }
    recording_paused_ = false;
    waveform_backend_->setRawFrameForwardingEnabled(true);
    QFile *filePtr = sensors_file_.get();
    recording_thread_running_.store(true);
    recording_thread_ = std::thread([this, filePtr]() {
        const int rateHz = std::max(1, export_rate_hz_);
        const auto period = std::chrono::microseconds(1000000 / rateHz);
        auto nextTick = std::chrono::steady_clock::now();
        while (recording_thread_running_.load())
        {
            const quint64 recordTimestampUs = currentTimestampUs();
            const VaporView::EpsilonData e = device_backend_->epsilonData();
            const VaporView::PtbData ptb = device_backend_->ptbData();
            const VaporView::HmpData hmp = device_backend_->hmpData();
            const VaporView::LidarData lidar = device_backend_->lidarData();

            QStringList row;
            row.reserve(72);
            row << QString::number(recordTimestampUs);
            auto appendEmptyColumns = [&row](int count) {
                for (int i = 0; i < count; ++i) row << QString();
            };
            auto appendBool = [&row](bool value) { row << csvBool(value); };
            if (e.valid)
            {
                row
                    << QString::number(steadyToEpochUs(e.timestamp))
                    << QString::number(e.device_timestamp_us)
                    << QString::number(e.utc_unix_s)
                    << QString::number(e.utc_microseconds)
                    << QString::number(e.latitude_deg, 'f', 9)
                    << QString::number(e.longitude_deg, 'f', 9)
                    << QString::number(e.height_m, 'f', 6)
                    << QString::number(e.ecef_x_m, 'f', 6)
                    << QString::number(e.ecef_y_m, 'f', 6)
                    << QString::number(e.ecef_z_m, 'f', 6)
                    << QString::number(e.ned_n_m, 'f', 6)
                    << QString::number(e.ned_e_m, 'f', 6)
                    << QString::number(e.ned_d_m, 'f', 6)
                    << QString::number(e.vel_n_mps, 'f', 6)
                    << QString::number(e.vel_e_mps, 'f', 6)
                    << QString::number(e.vel_d_mps, 'f', 6)
                    << QString::number(e.body_vel_x_mps, 'f', 6)
                    << QString::number(e.body_vel_y_mps, 'f', 6)
                    << QString::number(e.body_vel_z_mps, 'f', 6)
                    << QString::number(e.body_acc_x_mps2, 'f', 6)
                    << QString::number(e.body_acc_y_mps2, 'f', 6)
                    << QString::number(e.body_acc_z_mps2, 'f', 6)
                    << QString::number(e.roll_deg, 'f', 6)
                    << QString::number(e.pitch_deg, 'f', 6)
                    << QString::number(e.yaw_deg, 'f', 6)
                    << QString::number(e.quat_w, 'f', 8)
                    << QString::number(e.quat_x, 'f', 8)
                    << QString::number(e.quat_y, 'f', 8)
                    << QString::number(e.quat_z, 'f', 8)
                    << QString::number(e.ang_vel_x_radps, 'f', 8)
                    << QString::number(e.ang_vel_y_radps, 'f', 8)
                    << QString::number(e.ang_vel_z_radps, 'f', 8)
                    << QString::number(e.imu_acc_x_mps2, 'f', 6)
                    << QString::number(e.imu_acc_y_mps2, 'f', 6)
                    << QString::number(e.imu_acc_z_mps2, 'f', 6)
                    << QString::number(e.imu_gyr_x_radps, 'f', 8)
                    << QString::number(e.imu_gyr_y_radps, 'f', 8)
                    << QString::number(e.imu_gyr_z_radps, 'f', 8)
                    << QString::number(e.mag_x_mg, 'f', 6)
                    << QString::number(e.mag_y_mg, 'f', 6)
                    << QString::number(e.mag_z_mg, 'f', 6)
                    << QString::fromStdString(e.gnss_fix_text)
                    << QString::number(e.gnss_satellites)
                    << QString::number(e.hdop, 'f', 4)
                    << QString::number(e.vdop, 'f', 4)
                    << QString::number(e.hacc_m, 'f', 4)
                    << QString::number(e.vacc_m, 'f', 4)
                    << QString::number(e.lat_std_m, 'f', 4)
                    << QString::number(e.lon_std_m, 'f', 4)
                    << QString::number(e.height_std_m, 'f', 4)
                    << (std::isfinite(e.diff_age_s) ? QString::number(e.diff_age_s, 'f', 4) : QString())
                    << csvBool(e.heading_valid)
                    << QString::number(e.system_status_bits)
                    << QString::number(e.filter_status_bits)
                    << QString::number(e.update_status_bits);
                appendBool(e.valid);
                row << QString::fromStdString(e.error_message);
            }
            else
            {
                appendEmptyColumns(57);
            }
            if (hmp.valid) row << QString::number(hmp.temperature, 'f', 6) << QString::number(hmp.humidity, 'f', 6);
            else appendEmptyColumns(2);
            if (ptb.valid) row << QString::number(ptb.pressure_hpa, 'f', 6);
            else appendEmptyColumns(1);
            if (lidar.valid)
            {
                row << QString::number(lidar.distance_m, 'f', 6) << QString::number(lidar.signal_strength);
                appendBool(lidar.valid);
            }
            else
            {
                appendEmptyColumns(3);
            }
            {
                std::lock_guard<std::mutex> lock(recording_files_mutex_);
                if (filePtr && filePtr->isOpen())
                {
                    QTextStream out(filePtr);
                    out.setEncoding(QStringConverter::Utf8);
                    for (int i = 0; i < row.size(); ++i)
                    {
                        if (i > 0) out << ',';
                        out << csvEscape(row.at(i));
                    }
                    out << '\n';
                    out.flush();
                }
            }
            recording_entry_count_.fetch_add(1);
            QMetaObject::invokeMethod(this, [this]() { emit recordingStatsChanged(); }, Qt::QueuedConnection);
            nextTick += period;
            std::this_thread::sleep_until(nextTick);
        }
    });
    emit recordingStateChanged();
}

void RecordingBackend::stopRecordingWorkers()
{
    recording_thread_running_.store(false);
    if (recording_thread_.joinable())
    {
        recording_thread_.join();
    }
    waveform_backend_->setRawFrameForwardingEnabled(false);
}

void RecordingBackend::onEpsilonRawFrame(quint64 hostTimestampUs, int packetId, int serialNumber, QByteArray payload)
{
    if (!recording_thread_running_.load() || recording_paused_) return;
    writeUnifiedRawRecord(raw_epsilon_file_.get(), raw_epsilon_record_count_, kRawSourceEpsilon,
                          static_cast<quint16>(packetId), static_cast<quint32>(serialNumber),
                          hostTimestampUs, payload.constData(), static_cast<size_t>(payload.size()));
}

void RecordingBackend::onPtbRawFrame(quint64 hostTimestampUs, QByteArray payload)
{
    if (!recording_thread_running_.load() || recording_paused_) return;
    writeUnifiedRawRecord(raw_ptb_file_.get(), raw_ptb_record_count_, kRawSourcePtb, kRawRecordTypeGeneric, 0u,
                          hostTimestampUs, payload.constData(), static_cast<size_t>(payload.size()));
}

void RecordingBackend::onHmpRawFrame(quint64 hostTimestampUs, QByteArray payload)
{
    if (!recording_thread_running_.load() || recording_paused_) return;
    writeUnifiedRawRecord(raw_hmp_file_.get(), raw_hmp_record_count_, kRawSourceHmp, 0x03u, 0u,
                          hostTimestampUs, payload.constData(), static_cast<size_t>(payload.size()));
}

void RecordingBackend::onLidarRawFrame(quint64 hostTimestampUs, int protocol, QByteArray payload)
{
    if (!recording_thread_running_.load() || recording_paused_) return;
    writeUnifiedRawRecord(raw_lidar_file_.get(), raw_lidar_record_count_, kRawSourceLidar, static_cast<quint16>(protocol), 0u,
                          hostTimestampUs, payload.constData(), static_cast<size_t>(payload.size()));
}

void RecordingBackend::onTcpRawWaveFrame(quint64 timestampUs, QByteArray rawSignalPayload, QByteArray harmonicPayload, VaporView::TcpFloatEncoding floatEncoding)
{
    if (!recording_thread_running_.load() || recording_paused_) return;
    QByteArray payload;
    payload.resize(static_cast<int>(sizeof(quint32) * 2 + rawSignalPayload.size() + harmonicPayload.size()));
    char *cursor = payload.data();
    const quint32 rawSize = qToLittleEndian(static_cast<quint32>(rawSignalPayload.size()));
    const quint32 harmonicSize = qToLittleEndian(static_cast<quint32>(harmonicPayload.size()));
    std::memcpy(cursor, &rawSize, sizeof(rawSize));
    cursor += sizeof(rawSize);
    std::memcpy(cursor, &harmonicSize, sizeof(harmonicSize));
    cursor += sizeof(harmonicSize);
    if (!rawSignalPayload.isEmpty())
    {
        std::memcpy(cursor, rawSignalPayload.constData(), rawSignalPayload.size());
        cursor += rawSignalPayload.size();
    }
    if (!harmonicPayload.isEmpty())
    {
        std::memcpy(cursor, harmonicPayload.constData(), harmonicPayload.size());
    }
    if (writeUnifiedRawRecord(raw_tcp_wave_file_.get(), raw_tcp_wave_record_count_, kRawSourceTcpWave,
                              kRawRecordTypeGeneric,
                              kRawTcpWaveCombinedPayloadFlag | VaporView::tcpFloatEncodingToRawDatFlags(floatEncoding),
                              timestampUs, payload.constData(), static_cast<size_t>(payload.size())))
    {
        waveform_frame_count_.fetch_add(1);
        waveform_file_count_.store(1);
        emit recordingStatsChanged();
    }
}

void RecordingBackend::updateStats()
{
    if (sensors_file_ && sensors_file_->isOpen())
    {
        writeSessionMetadata();
    }
    emit recordingStatsChanged();
}

RtkBackend::RtkBackend(DeviceBackend *deviceBackend, QObject *parent)
    : QObject(parent)
    , device_backend_(deviceBackend)
    , port_(QStringLiteral("2101"))
    , output_baud_(115200)
{
    loadConfig();
    connect(&stats_timer_, &QTimer::timeout, this, &RtkBackend::pollStats);
    stats_timer_.start(1000);
}

RtkBackend::~RtkBackend()
{
    service_.stop();
}

QString RtkBackend::server() const { return server_; }
QString RtkBackend::port() const { return port_; }
QString RtkBackend::username() const { return username_; }
QString RtkBackend::password() const { return password_; }
QString RtkBackend::mountpoint() const { return mountpoint_; }
QString RtkBackend::outputPort() const { return output_port_; }
int RtkBackend::outputBaud() const { return output_baud_; }
bool RtkBackend::running() const { return service_.isRunning(); }
QStringList RtkBackend::diagnostics() const { return diagnostics_; }
QVariantMap RtkBackend::stats() const { return stats_; }

void RtkBackend::setServer(const QString& value) { if (server_ != value) { server_ = value; emit configChanged(); } }
void RtkBackend::setPort(const QString& value) { if (port_ != value) { port_ = value; emit configChanged(); } }
void RtkBackend::setUsername(const QString& value) { if (username_ != value) { username_ = value; emit configChanged(); } }
void RtkBackend::setPassword(const QString& value) { if (password_ != value) { password_ = value; emit configChanged(); } }
void RtkBackend::setMountpoint(const QString& value) { if (mountpoint_ != value) { mountpoint_ = value; emit configChanged(); } }
void RtkBackend::setOutputPort(const QString& value) { if (output_port_ != value) { output_port_ = value; emit configChanged(); } }
void RtkBackend::setOutputBaud(int value) { if (output_baud_ != value) { output_baud_ = value; emit configChanged(); } }

RtkStreamConfig RtkBackend::buildConfig() const
{
    RtkStreamConfig config;
    config.server = server_.trimmed();
    config.port = port_.trimmed();
    config.username = username_;
    config.password = password_;
    config.mountpoint = mountpoint_.trimmed();
    config.outputPort = output_port_.trimmed();
    config.baudrate = output_baud_;
    const QVariantMap coordinate = device_backend_->coordinateData();
    config.sendNmeaGga = true;
    config.nmeaLatitudeDeg = coordinate.value(QStringLiteral("latitude")).toDouble();
    config.nmeaLongitudeDeg = coordinate.value(QStringLiteral("longitude")).toDouble();
    config.nmeaHeightM = coordinate.value(QStringLiteral("altitude")).toDouble();
    return config;
}

void RtkBackend::appendDiagnostic(const QString& line, const QString& level)
{
    diagnostics_.append(QStringLiteral("[%1] %2").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), line));
    while (diagnostics_.size() > 500)
    {
        diagnostics_.removeFirst();
    }
    emit diagnosticsChanged();
    emit notificationRequested(level, line);
}

void RtkBackend::start()
{
    if (service_.isRunning())
    {
        return;
    }
    QString error;
    const RtkStreamConfig config = buildConfig();
    if (config.server.isEmpty() || config.port.isEmpty() || config.mountpoint.isEmpty() || config.outputPort.isEmpty())
    {
        appendDiagnostic(QStringLiteral("Please fill server, port, mountpoint and output port."), QStringLiteral("warning"));
        return;
    }
    if (!service_.start(config, &error))
    {
        appendDiagnostic(QStringLiteral("Failed to start RTK: %1").arg(error), QStringLiteral("error"));
        emit runningChanged();
        return;
    }
    appendDiagnostic(QStringLiteral("RTK stream started."));
    emit runningChanged();
}

void RtkBackend::stop()
{
    if (!service_.isRunning())
    {
        return;
    }
    service_.stop();
    appendDiagnostic(QStringLiteral("RTK stream stopped."));
    emit runningChanged();
}

void RtkBackend::testConnection()
{
    const RtkStreamConfig config = buildConfig();
    if (config.server.isEmpty() || config.port.isEmpty())
    {
        appendDiagnostic(QStringLiteral("Please enter server address and port first."), QStringLiteral("warning"));
        return;
    }
    appendDiagnostic(QStringLiteral("RTK configuration looks complete enough to start a stream test."));
}

void RtkBackend::saveConfig()
{
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("RtkConfig"));
    settings.setValue(QStringLiteral("server"), server_);
    settings.setValue(QStringLiteral("port"), port_);
    settings.setValue(QStringLiteral("username"), username_);
    settings.setValue(QStringLiteral("password"), password_);
    settings.setValue(QStringLiteral("mountpoint"), mountpoint_);
    settings.setValue(QStringLiteral("output_port"), output_port_);
    settings.setValue(QStringLiteral("baudrate"), output_baud_);
    appendDiagnostic(QStringLiteral("RTK configuration saved."));
}

void RtkBackend::loadConfig()
{
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("RtkConfig"));
    server_ = settings.value(QStringLiteral("server"), server_).toString();
    port_ = settings.value(QStringLiteral("port"), port_).toString();
    username_ = settings.value(QStringLiteral("username"), username_).toString();
    password_ = settings.value(QStringLiteral("password"), password_).toString();
    mountpoint_ = settings.value(QStringLiteral("mountpoint"), mountpoint_).toString();
    output_port_ = settings.value(QStringLiteral("output_port"), settings.value(QStringLiteral("epsilon_rtcm_forward_port")).toString()).toString();
    output_baud_ = settings.value(QStringLiteral("baudrate"), 115200).toInt();
    emit configChanged();
}

void RtkBackend::clearDiagnostics()
{
    diagnostics_.clear();
    emit diagnosticsChanged();
}

void RtkBackend::applyMainAntennaLeverArm(double xM, double yM, double zM)
{
    const bool ok = device_backend_->applyEpsilonMainAntennaLeverArm(xM, yM, zM);
    appendDiagnostic(ok ? QStringLiteral("Main antenna lever arm applied.")
                        : QStringLiteral("Failed to apply main antenna lever arm."),
                     ok ? QStringLiteral("info") : QStringLiteral("error"));
}

void RtkBackend::pollStats()
{
    const RtkStreamStats s = service_.stats();
    stats_ = {
        {QStringLiteral("running"), s.running},
        {QStringLiteral("inputBps"), s.inputBps},
        {QStringLiteral("outputBps"), s.outputBps},
        {QStringLiteral("inputBytes"), s.inputBytes},
        {QStringLiteral("outputBytes"), s.outputBytes},
        {QStringLiteral("rtcm3FrameCount"), s.rtcm3FrameCount},
        {QStringLiteral("rtcm3CrcOkCount"), s.rtcm3CrcOkCount},
        {QStringLiteral("rtcm3CrcFailCount"), s.rtcm3CrcFailCount},
        {QStringLiteral("messageTypes"), s.rtcmMessageTypes},
        {QStringLiteral("message"), s.message},
    };
    emit statsChanged();
    emit runningChanged();
}

SessionBackend::SessionBackend(QObject *parent)
    : QObject(parent)
{
    QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
    recording_directory_ = settings.value(QStringLiteral("recording_directory"), QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data"))).toString();
    refreshSessions();

    waveform_prefetch_timer_.setSingleShot(true);
    waveform_prefetch_timer_.setInterval(25);
    connect(&waveform_prefetch_timer_, &QTimer::timeout, this, [this]() {
        if (pending_prefetch_frame_index_ >= 0)
        {
            prefetchWaveformFrames(pending_prefetch_frame_index_);
        }
    });
}

QString SessionBackend::recordingDirectory() const { return recording_directory_; }
QVariantList SessionBackend::sessions() const { return sessions_; }
QVariantMap SessionBackend::selectedSession() const { return selected_session_; }
QStringList SessionBackend::csvPreviewColumns() const { return csv_preview_columns_; }
QVariantList SessionBackend::csvPreviewRows() const { return csv_preview_rows_; }
QVariantList SessionBackend::waveformPreview() const { return waveform_preview_; }
QVariantList SessionBackend::waveformRawPreview() const { return waveform_raw_preview_; }
QVariantList SessionBackend::waveformHarmonicPreview() const { return waveform_harmonic_preview_; }
QVariantList SessionBackend::peakTrendPreview() const { return peak_trend_preview_; }
QVariantList SessionBackend::temperaturePreview() const { return temperature_preview_; }
QVariantList SessionBackend::humidityPreview() const { return humidity_preview_; }
QVariantList SessionBackend::pressurePreview() const { return pressure_preview_; }
QVariantList SessionBackend::peakTrendRangePreview() const { return peak_trend_range_preview_; }
QVariantList SessionBackend::temperatureRangePreview() const { return temperature_range_preview_; }
QVariantList SessionBackend::humidityRangePreview() const { return humidity_range_preview_; }
QVariantList SessionBackend::pressureRangePreview() const { return pressure_range_preview_; }
int SessionBackend::trendViewStart() const { return trend_view_start_; }
int SessionBackend::trendViewEnd() const { return trend_view_end_; }
int SessionBackend::csvRangeStartRow() const { return csv_range_start_row_; }
int SessionBackend::csvRangeEndRow() const { return csv_range_end_row_; }
int SessionBackend::currentCsvRangeCursorIndex() const
{
    if (current_csv_row_ < csv_range_start_row_ || current_csv_row_ > csv_range_end_row_)
        return -1;
    return current_csv_row_ - csv_range_start_row_;
}
bool SessionBackend::waveformIndexReady() const { return !waveform_frame_index_.isEmpty(); }
bool SessionBackend::loading() const { return loading_; }
int SessionBackend::loadingProgress() const { return loading_progress_; }
QString SessionBackend::loadingText() const { return loading_text_; }
int SessionBackend::currentFrameIndex() const { return current_frame_index_; }
int SessionBackend::currentCsvRow() const { return current_csv_row_; }
int SessionBackend::secondaryCsvRow() const { return secondary_csv_row_; }
int SessionBackend::csvPreviewGeneration() const { return csv_preview_generation_; }
int SessionBackend::waveformRawPointCount() const { return waveform_raw_point_count_; }
int SessionBackend::waveformHarmonicPointCount() const { return waveform_harmonic_point_count_; }

void SessionBackend::setRecordingDirectory(const QString& directory)
{
    const QString normalized = QDir::fromNativeSeparators(directory.trimmed());
    if (normalized.isEmpty() || normalized == recording_directory_)
    {
        return;
    }
    recording_directory_ = normalized;
    QSettings(QStringLiteral("VaporView"), QStringLiteral("MainWindow")).setValue(QStringLiteral("recording_directory"), recording_directory_);
    emit recordingDirectoryChanged();
    refreshSessions();
}

QVariantMap SessionBackend::sessionSummaryForDirectory(const QString& path, bool detailed) const
{
    Q_UNUSED(detailed);
    QVariantMap map;
    const QFileInfo info(path);
    map[QStringLiteral("path")] = QDir::fromNativeSeparators(info.absoluteFilePath());
    map[QStringLiteral("name")] = info.fileName();
    map[QStringLiteral("date")] = info.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    map[QStringLiteral("status")] = QStringLiteral("completed");
    map[QStringLiteral("duration")] = QStringLiteral("--:--:--");
    map[QStringLiteral("size")] = QStringLiteral("0 B");
    map[QStringLiteral("frames")] = 0;
    map[QStringLiteral("sensorRows")] = 0;
    map[QStringLiteral("waveformFrames")] = 0;
    map[QStringLiteral("startTime")] = QStringLiteral("---");
    map[QStringLiteral("endTime")] = QStringLiteral("---");
    map[QStringLiteral("sensorRateHz")] = QStringLiteral("---");
    map[QStringLiteral("waveformRateHz")] = QStringLiteral("---");
    map[QStringLiteral("framesText")] = QStringLiteral("0 | 0");
    map[QStringLiteral("ratesText")] = QStringLiteral("--- | ---");

    const QDir sessionDir(path);
    qint64 size = 0;
    QStringList countedFiles;
    auto addFileSize = [&](const QString& relativeOrAbsolutePath) {
        if (relativeOrAbsolutePath.trimmed().isEmpty())
        {
            return;
        }
        QFileInfo fileInfo(relativeOrAbsolutePath);
        if (fileInfo.isRelative())
        {
            fileInfo.setFile(sessionDir.filePath(relativeOrAbsolutePath));
        }
        const QString absolutePath = QDir::fromNativeSeparators(fileInfo.absoluteFilePath());
        if (!fileInfo.exists() || !fileInfo.isFile() || countedFiles.contains(absolutePath))
        {
            return;
        }
        countedFiles.append(absolutePath);
        size += fileInfo.size();
    };
    addFileSize(QStringLiteral("session.json"));

    QFile metadata(sessionDir.filePath(QStringLiteral("session.json")));
    QDateTime startDt;
    QDateTime endDt;
    if (metadata.open(QIODevice::ReadOnly))
    {
        const QJsonObject root = QJsonDocument::fromJson(metadata.readAll()).object();
        map[QStringLiteral("name")] = jsonString(root, QStringLiteral("session_name"), info.fileName());
        map[QStringLiteral("date")] = jsonString(root, QStringLiteral("start_time_utc"), map.value(QStringLiteral("date")).toString());
        const quint64 waveformFrames = jsonUInt64(root, QStringLiteral("waveform_frames"));
        const quint64 sensorRows = jsonUInt64(root, QStringLiteral("sensor_rows"));
        map[QStringLiteral("frames")] = QVariant::fromValue<qlonglong>(static_cast<qlonglong>(std::min<quint64>(waveformFrames, std::numeric_limits<qlonglong>::max())));
        map[QStringLiteral("sensorRows")] = QVariant::fromValue<qlonglong>(static_cast<qlonglong>(std::min<quint64>(sensorRows, std::numeric_limits<qlonglong>::max())));
        map[QStringLiteral("waveformFrames")] = QVariant::fromValue<qlonglong>(static_cast<qlonglong>(std::min<quint64>(waveformFrames, std::numeric_limits<qlonglong>::max())));
        map[QStringLiteral("sensorRateHz")] = root.contains(QStringLiteral("sensor_export_rate_hz"))
            ? QStringLiteral("%1Hz").arg(jsonInt(root, QStringLiteral("sensor_export_rate_hz")))
            : QStringLiteral("---");
        map[QStringLiteral("waveformRateHz")] = root.contains(QStringLiteral("waveform_export_rate_hz"))
            ? QStringLiteral("%1Hz").arg(jsonInt(root, QStringLiteral("waveform_export_rate_hz")))
            : QStringLiteral("---");
        const QString start = jsonString(root, QStringLiteral("start_time_utc"));
        const QString end = jsonString(root, QStringLiteral("end_time_utc"));
        startDt = QDateTime::fromString(start, Qt::ISODateWithMs);
        endDt = QDateTime::fromString(end, Qt::ISODateWithMs);
        map[QStringLiteral("startTime")] = startDt.isValid()
            ? startDt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
            : (start.isEmpty() ? QStringLiteral("---") : start);
        map[QStringLiteral("endTime")] = endDt.isValid()
            ? endDt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
            : (end.isEmpty() ? QStringLiteral("---") : end);
        if (startDt.isValid() && endDt.isValid())
        {
            map[QStringLiteral("duration")] = formatDuration(startDt.secsTo(endDt));
        }
        if (end.isEmpty())
        {
            map[QStringLiteral("status")] = QStringLiteral("partial");
        }

        const QJsonObject paths = root.value(QStringLiteral("paths")).toObject();
        addFileSize(paths.value(QStringLiteral("devices_csv")).toString(QStringLiteral("sensors/devices.csv")));
        addFileSize(paths.value(QStringLiteral("event_log")).toString());
        addFileSize(paths.value(QStringLiteral("error_log")).toString());
        addFileSize(paths.value(QStringLiteral("device_config")).toString());
        addFileSize(paths.value(QStringLiteral("raw_format_doc")).toString());

        const QJsonObject rawFiles = root.value(QStringLiteral("raw_files")).toObject();
        for (auto it = rawFiles.constBegin(); it != rawFiles.constEnd(); ++it)
        {
            addFileSize(it.value().toObject().value(QStringLiteral("path")).toString());
        }
    }
    else
    {
        addFileSize(QStringLiteral("sensors/devices.csv"));
        addFileSize(QStringLiteral("raw/epsilon.dat"));
        addFileSize(QStringLiteral("raw/ptb.dat"));
        addFileSize(QStringLiteral("raw/hmp.dat"));
        addFileSize(QStringLiteral("raw/lidar.dat"));
        addFileSize(QStringLiteral("raw/tcp_wave.dat"));
    }
    map[QStringLiteral("size")] = formatBytes(size);

    updateSessionSummaryCountText(map);
    return map;
}

void SessionBackend::refreshSessions()
{
    const QString previousPath = selected_session_.value(QStringLiteral("path")).toString();
    sessions_.clear();
    QDir dir(recording_directory_);
    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (const QFileInfo& entry : entries)
    {
        if (QFileInfo::exists(QDir(entry.absoluteFilePath()).filePath(QStringLiteral("session.json"))) ||
            QFileInfo::exists(QDir(entry.absoluteFilePath()).filePath(QStringLiteral("sensors/devices.csv"))))
        {
            sessions_.append(sessionSummaryForDirectory(entry.absoluteFilePath()));
        }
    }
    all_sessions_ = sessions_;
    emit sessionsChanged();

    if (sessions_.isEmpty())
    {
        selected_session_.clear();
        clearPreviewData();
        emit selectedSessionChanged();
        return;
    }

    int selectedIndex = -1;
    for (int i = 0; i < sessions_.size(); ++i)
    {
        if (sessions_.at(i).toMap().value(QStringLiteral("path")).toString() == previousPath)
        {
            selectedIndex = i;
            break;
        }
    }
    if (selectedIndex < 0)
    {
        selectedIndex = 0;
    }
    selected_session_ = sessions_.at(selectedIndex).toMap();
    clearPreviewData();
    emit selectedSessionChanged();
}

void SessionBackend::applySessionSortFilter()
{
    QVariantList filtered = all_sessions_;
    if (!session_filter_text_.isEmpty())
    {
        QVariantList tmp;
        for (const QVariant& s : filtered)
        {
            if (s.toMap().value(QStringLiteral("name")).toString().contains(session_filter_text_, Qt::CaseInsensitive))
            {
                tmp.append(s);
            }
        }
        filtered = tmp;
    }

    switch (sort_sessions_mode_)
    {
    case 1: // date ascending
        std::sort(filtered.begin(), filtered.end(), [](const QVariant& a, const QVariant& b) {
            return a.toMap().value(QStringLiteral("date")).toString() < b.toMap().value(QStringLiteral("date")).toString();
        });
        break;
    case 2: // size descending
        std::sort(filtered.begin(), filtered.end(), [](const QVariant& a, const QVariant& b) {
            return a.toMap().value(QStringLiteral("size")).toString() > b.toMap().value(QStringLiteral("size")).toString();
        });
        break;
    case 3: // name ascending
        std::sort(filtered.begin(), filtered.end(), [](const QVariant& a, const QVariant& b) {
            return a.toMap().value(QStringLiteral("name")).toString().toLower() < b.toMap().value(QStringLiteral("name")).toString().toLower();
        });
        break;
    default: // 0 = date descending (default, directory listing order)
        break;
    }

    sessions_ = filtered;
    emit sessionsChanged();
}

void SessionBackend::sortSessions(int mode)
{
    sort_sessions_mode_ = mode;
    applySessionSortFilter();
}

void SessionBackend::setSessionFilter(const QString& text)
{
    session_filter_text_ = text.trimmed();
    applySessionSortFilter();
}

void SessionBackend::clearSessionFilter()
{
    session_filter_text_.clear();
    applySessionSortFilter();
}

void SessionBackend::openSessionPath(const QString& path)
{
    const QString normalized = QDir::fromNativeSeparators(path.trimmed());
    if (normalized.isEmpty())
    {
        return;
    }
    startLoadingSession(normalized);
}

void SessionBackend::selectSession(int index)
{
    if (index < 0 || index >= sessions_.size())
    {
        return;
    }
    selected_session_ = sessions_.at(index).toMap();
    clearPreviewData();
    emit selectedSessionChanged();
}

void SessionBackend::reloadSelectedSession()
{
    const QString path = selected_session_.value(QStringLiteral("path")).toString();
    if (path.isEmpty())
    {
        if (!sessions_.isEmpty())
        {
            startLoadingSession(sessions_.constFirst().toMap().value(QStringLiteral("path")).toString());
        }
        return;
    }
    startLoadingSession(path);
}

void SessionBackend::clear()
{
    ++load_generation_;
    selected_session_.clear();
    clearPreviewData();
    emit selectedSessionChanged();
}

void SessionBackend::clearPreviewData()
{
    csv_preview_columns_.clear();
    csv_preview_rows_.clear();
    waveform_preview_.clear();
    waveform_raw_preview_.clear();
    waveform_harmonic_preview_.clear();
    peak_trend_preview_.clear();
    temperature_preview_.clear();
    humidity_preview_.clear();
    pressure_preview_.clear();
    peak_trend_full_.clear();
    temperature_full_.clear();
    humidity_full_.clear();
    pressure_full_.clear();
    peak_trend_range_preview_.clear();
    temperature_range_preview_.clear();
    humidity_range_preview_.clear();
    pressure_range_preview_.clear();
    trend_view_start_ = 0;
    trend_view_end_ = 0;
    csv_range_start_row_ = 0;
    csv_range_end_row_ = 0;
    csv_rows_all_.clear();
    csv_timestamps_us_.clear();
    waveform_timestamps_us_.clear();
    waveform_index_path_.clear();
    waveform_frame_index_.clear();
    current_frame_index_ = -1;
    current_csv_row_ = -1;
    secondary_csv_row_ = -1;
    clearWaveformCache();
    waveform_raw_point_count_ = 0;
    waveform_harmonic_point_count_ = 0;
    emit frameSelectionChanged();
}

void SessionBackend::loadSelectedSession(const QString& path)
{
    startLoadingSession(path);
}

void SessionBackend::setLoadingState(bool loading, int progress, const QString& text)
{
    const bool changed = loading_ != loading || loading_progress_ != progress || loading_text_ != text;
    loading_ = loading;
    loading_progress_ = std::clamp(progress, 0, 100);
    loading_text_ = text;
    if (changed)
    {
        emit loadingChanged();
    }
}

void SessionBackend::postLoadProgress(int generation, int progress, const QString& text)
{
    QMetaObject::invokeMethod(this, [this, generation, progress, text]() {
        if (generation != load_generation_)
        {
            return;
        }
        setLoadingState(true, progress, text);
    }, Qt::QueuedConnection);
}

void SessionBackend::startLoadingSession(const QString& path)
{
    const QString normalized = QDir::fromNativeSeparators(path.trimmed());
    if (normalized.isEmpty())
    {
        return;
    }

    const int generation = ++load_generation_;
    setLoadingState(true, 1, QStringLiteral("正在准备加载记录数据..."));
    clearPreviewData();
    emit selectedSessionChanged();

    if (session_load_thread_)
    {
        session_load_thread_->requestInterruption();
    }

    auto *thread = QThread::create([this, normalized, generation]() {
        auto result = std::make_shared<SessionLoadResult>(buildSessionLoadResult(normalized, generation));
        QMetaObject::invokeMethod(this, [this, result, generation]() {
            applySessionLoadResult(result, generation);
        }, Qt::QueuedConnection);
    });
    session_load_thread_ = thread;
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (session_load_thread_ == thread)
        {
            session_load_thread_ = nullptr;
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void SessionBackend::applySessionLoadResult(const std::shared_ptr<SessionLoadResult>& result, int generation)
{
    if (generation != load_generation_)
    {
        return;
    }
    if (!result || !result->ok)
    {
        setLoadingState(false, 0, result ? result->error : QStringLiteral("加载记录失败。"));
        emit notificationRequested(QStringLiteral("error"), result ? result->error : QStringLiteral("加载记录失败。"));
        return;
    }

    selected_session_ = result->selectedSession;
    csv_preview_columns_ = result->csvColumns;
    csv_rows_all_ = result->csvRowsAll;
    csv_timestamps_us_ = result->csvTimestampsUs;
    waveform_timestamps_us_ = result->waveformTimestampsUs;
    waveform_index_path_ = result->waveformIndexPath;
    waveform_frame_index_ = result->waveformFrameIndex;
    waveform_raw_preview_ = result->rawPreview;
    waveform_harmonic_preview_ = result->harmonicPreview;
    waveform_preview_ = waveform_harmonic_preview_;
    peak_trend_preview_ = result->peakValues;
    temperature_preview_ = result->temperatureValues;
    humidity_preview_ = result->humidityValues;
    pressure_preview_ = result->pressureValues;
    peak_trend_full_ = result->peakValues;
    temperature_full_ = result->temperatureValues;
    humidity_full_ = result->humidityValues;
    pressure_full_ = result->pressureValues;
    waveform_raw_point_count_ = result->rawPointCount;
    waveform_harmonic_point_count_ = result->harmonicPointCount;
    current_frame_index_ = result->currentFrameIndex;

    // Initialize trend view range to full data
    trend_view_start_ = 0;
    trend_view_end_ = std::max(0, static_cast<int>(peak_trend_full_.size()) - 1);
    rebuildTrendRangePreviews();

    if (!waveform_frame_index_.isEmpty())
    {
        selected_session_[QStringLiteral("frames")] = waveform_frame_index_.size();
        selected_session_[QStringLiteral("waveformFrames")] = waveform_frame_index_.size();
        updateSessionSummaryCountText(selected_session_);
    }

    const quint64 timestampUs = current_frame_index_ >= 0 && current_frame_index_ < waveform_timestamps_us_.size()
        ? waveform_timestamps_us_.at(current_frame_index_)
        : 0ULL;
    updateCsvPreviewForTimestamp(timestampUs);

    setLoadingState(false, 100, QStringLiteral("记录数据加载完成。"));
    emit selectedSessionChanged();
    emit frameSelectionChanged();
}

SessionBackend::SessionLoadResult SessionBackend::buildSessionLoadResult(const QString& path, int generation)
{
    SessionLoadResult result;
    result.path = path;
    result.selectedSession = sessionSummaryForDirectory(path, false);
    postLoadProgress(generation, 8, QStringLiteral("正在读取传感器 CSV..."));

    QFile csvFile(QDir(path).filePath(QStringLiteral("sensors/devices.csv")));
    if (csvFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QTextStream in(&csvFile);
        in.setEncoding(QStringConverter::Utf8);
        const QStringList headers = parseCsvLine(in.readLine());
        const int timestampIndex = findHeaderIndex(headers, {
            QStringLiteral("record_timestamp_us"),
            QStringLiteral("epsilon_host_timestamp_us"),
            QStringLiteral("rtk_timestamp_us")
        });
        const int temperatureIndex = findHeaderIndex(headers, {QStringLiteral("hmp_temperature_c"), QStringLiteral("temp_c")});
        const int humidityIndex = findHeaderIndex(headers, {QStringLiteral("hmp_humidity_rh"), QStringLiteral("humidity_rh")});
        const int pressureIndex = findHeaderIndex(headers, {QStringLiteral("ptb_pressure_hpa"), QStringLiteral("baro_hpa"), QStringLiteral("baro_pa")});
        const int thValidIndex = findHeaderIndex(headers, {QStringLiteral("th_valid")});
        const int baroValidIndex = findHeaderIndex(headers, {QStringLiteral("baro_valid")});
        const bool pressureIsPa = pressureIndex >= 0 && headers.value(pressureIndex).trimmed().toLower().endsWith(QStringLiteral("_pa"));
        quint64 firstTimestampUs = 0;
        int rowNumber = 0;
        const QStringList preferred = {
            QStringLiteral("#"), QStringLiteral("时间偏差"), QStringLiteral("record_timestamp_us"),
            QStringLiteral("record_timestamp_utc"), QStringLiteral("epsilon_host_timestamp_us"),
            QStringLiteral("nav_lat_deg"), QStringLiteral("nav_lon_deg"), QStringLiteral("nav_height_m"),
            QStringLiteral("gnss_fix"), QStringLiteral("gnss_satellites")
        };
        result.csvColumns = preferred;

        while (!in.atEnd())
        {
            if (QThread::currentThread()->isInterruptionRequested())
            {
                result.error = QStringLiteral("记录加载已取消。");
                return result;
            }
            const QString line = in.readLine();
            if (line.trimmed().isEmpty())
            {
                continue;
            }
            const QStringList fields = parseCsvLine(line);
            if (fields.isEmpty())
            {
                continue;
            }
            QVariantMap row;
            row[QStringLiteral("#")] = rowNumber + 1;
            row[QStringLiteral("_sourceRow")] = rowNumber;
            row[QStringLiteral("_matchRank")] = -1;
            row[QStringLiteral("时间偏差")] = QString();
            quint64 timestampUs = 0;
            if (timestampIndex >= 0 && timestampIndex < fields.size())
            {
                bool ok = false;
                timestampUs = fields.at(timestampIndex).toULongLong(&ok);
                if (!ok)
                {
                    timestampUs = 0;
                }
            }
            if (timestampUs > 0)
            {
                if (firstTimestampUs == 0)
                {
                    firstTimestampUs = timestampUs;
                }
                row[QStringLiteral("record_timestamp_utc")] =
                    QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(timestampUs / 1000), Qt::UTC).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
            }
            result.csvTimestampsUs.push_back(timestampUs);
            for (int i = 0; i < std::min(headers.size(), fields.size()); ++i)
            {
                row[headers.at(i)] = fields.at(i);
                if (!result.csvColumns.contains(headers.at(i)) && result.csvColumns.size() < 14)
                {
                    result.csvColumns.append(headers.at(i));
                }
            }
            const bool thValid = thValidIndex < 0 || parseBooleanCsvField(fields.value(thValidIndex), true);
            const bool baroValid = baroValidIndex < 0 || parseBooleanCsvField(fields.value(baroValidIndex), true);
            double pressure = (pressureIndex >= 0 && baroValid) ? parseOptionalDouble(fields.value(pressureIndex)) : std::numeric_limits<double>::quiet_NaN();
            if (std::isfinite(pressure) && pressureIsPa)
            {
                pressure /= 100.0;
            }
            result.temperatureValues.append((temperatureIndex >= 0 && thValid) ? parseOptionalDouble(fields.value(temperatureIndex)) : std::numeric_limits<double>::quiet_NaN());
            result.humidityValues.append((humidityIndex >= 0 && thValid) ? parseOptionalDouble(fields.value(humidityIndex)) : std::numeric_limits<double>::quiet_NaN());
            result.pressureValues.append(pressure);
            result.csvRowsAll.append(row);
            ++rowNumber;
            if (rowNumber % 5000 == 0)
            {
                postLoadProgress(generation, 8 + std::min(18, rowNumber / 5000), QStringLiteral("正在读取传感器 CSV... %1 行").arg(rowNumber));
            }
        }

        if (!result.csvRowsAll.isEmpty())
        {
            result.selectedSession[QStringLiteral("sensorRows")] = result.csvRowsAll.size();
            updateSessionSummaryCountText(result.selectedSession);
        }
    }

    postLoadProgress(generation, 28, QStringLiteral("正在索引 TCP 波形帧..."));
    QFile file(QDir(path).filePath(QStringLiteral("raw/tcp_wave.dat")));
    if (file.open(QIODevice::ReadOnly) && file.size() > static_cast<qint64>(sizeof(UnifiedRawFileHeader)))
    {
        UnifiedRawFileHeader fileHeader{};
        if (file.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader)) == static_cast<qint64>(sizeof(fileHeader)) &&
            std::memcmp(fileHeader.magic, kUnifiedRawMagic, sizeof(fileHeader.magic)) == 0)
        {
            const quint32 fileHeaderSize = qFromLittleEndian(fileHeader.header_size);
            const quint16 sourceId = qFromLittleEndian(fileHeader.source_id);
            if (fileHeaderSize >= sizeof(UnifiedRawFileHeader) && sourceId == kRawSourceTcpWave &&
                (fileHeaderSize <= sizeof(UnifiedRawFileHeader) || file.seek(fileHeaderSize)))
            {
                int frameCount = 0;
                while (!file.atEnd())
                {
                    if (QThread::currentThread()->isInterruptionRequested())
                    {
                        result.error = QStringLiteral("记录加载已取消。");
                        return result;
                    }
                    const qint64 recordStart = file.pos();
                    UnifiedRawRecordHeader header{};
                    const qint64 headerBytes = file.read(reinterpret_cast<char*>(&header), sizeof(header));
                    if (headerBytes == 0)
                    {
                        break;
                    }
                    if (headerBytes != static_cast<qint64>(sizeof(header)))
                    {
                        break;
                    }
                    const quint32 marker = qFromLittleEndian(header.marker);
                    const quint32 recordHeaderSize = qFromLittleEndian(header.header_size);
                    const quint64 timestampUs = qFromLittleEndian(header.host_timestamp_us);
                    const quint32 payloadSize = qFromLittleEndian(header.payload_size);
                    const quint16 recordSourceId = qFromLittleEndian(header.source_id);
                    const quint32 flags = qFromLittleEndian(header.flags);
                    if (marker != kUnifiedRawRecordMarker || recordHeaderSize < sizeof(UnifiedRawRecordHeader))
                    {
                        break;
                    }
                    const qint64 payloadOffset = recordStart + static_cast<qint64>(recordHeaderSize);
                    const qint64 nextRecord = payloadOffset + static_cast<qint64>(payloadSize);
                    if (!file.seek(payloadOffset))
                    {
                        break;
                    }
                    if (recordSourceId == kRawSourceTcpWave && (flags & kRawTcpWaveCombinedPayloadFlag) != 0 && payloadSize >= sizeof(quint32) * 2)
                    {
                        quint32 rawSizeLe = 0;
                        quint32 harmonicSizeLe = 0;
                        if (file.read(reinterpret_cast<char*>(&rawSizeLe), sizeof(rawSizeLe)) != static_cast<qint64>(sizeof(rawSizeLe)) ||
                            file.read(reinterpret_cast<char*>(&harmonicSizeLe), sizeof(harmonicSizeLe)) != static_cast<qint64>(sizeof(harmonicSizeLe)))
                        {
                            break;
                        }
                        const quint32 rawSize = qFromLittleEndian(rawSizeLe);
                        const quint32 harmonicSize = qFromLittleEndian(harmonicSizeLe);
                        const quint64 requiredPayloadSize = static_cast<quint64>(sizeof(quint32) * 2) + rawSize + harmonicSize;
                        if (requiredPayloadSize <= payloadSize && harmonicSize > 0 && harmonicSize % kFloatSize == 0)
                        {
                            WaveformFrameIndex frame;
                            frame.rawPayloadOffset = static_cast<quint64>(payloadOffset) + sizeof(quint32) * 2ULL;
                            frame.rawPayloadSize = rawSize;
                            frame.harmonicPayloadOffset = frame.rawPayloadOffset + rawSize;
                            frame.harmonicPayloadSize = harmonicSize;
                            frame.flags = flags;
                            frame.timestampUs = timestampUs;
                            result.waveformFrameIndex.push_back(frame);
                            result.waveformTimestampsUs.push_back(timestampUs);
                            ++frameCount;
                            if (frameCount % 2000 == 0)
                            {
                                postLoadProgress(generation, 28, QStringLiteral("正在索引 TCP 波形帧... %1 帧").arg(frameCount));
                            }
                        }
                    }
                    if (!file.seek(nextRecord))
                    {
                        break;
                    }
                }
                if (!result.waveformFrameIndex.isEmpty())
                {
                    result.waveformIndexPath = QDir::fromNativeSeparators(QFileInfo(file.fileName()).absoluteFilePath());
                }
            }
        }
    }

    const int frameTotal = result.waveformFrameIndex.size();
    if (frameTotal > 0)
    {
        result.selectedSession[QStringLiteral("frames")] = frameTotal;
        result.selectedSession[QStringLiteral("waveformFrames")] = frameTotal;
        updateSessionSummaryCountText(result.selectedSession);
    }

    postLoadProgress(generation, 38, QStringLiteral("正在计算峰值趋势..."));
    QFile waveFile(QDir(path).filePath(QStringLiteral("raw/tcp_wave.dat")));
    if (waveFile.open(QIODevice::ReadOnly))
    {
        const int progressInterval = std::max(1, frameTotal / 100);
        for (int i = 0; i < frameTotal; ++i)
        {
            if (QThread::currentThread()->isInterruptionRequested())
            {
                result.error = QStringLiteral("记录加载已取消。");
                return result;
            }
            const WaveformFrameIndex& frame = result.waveformFrameIndex.at(i);
            const VaporView::TcpFloatEncoding encoding = VaporView::tcpFloatEncodingFromRawDatFlags(frame.flags);
            if (i == 0 && frame.rawPayloadSize > 0 && waveFile.seek(static_cast<qint64>(frame.rawPayloadOffset)))
            {
                const QByteArray raw = waveFile.read(static_cast<qint64>(frame.rawPayloadSize));
                if (raw.size() == static_cast<int>(frame.rawPayloadSize))
                {
                    const QVector<float> rawValues = VaporView::decodeTcpFloatPayload(raw, encoding);
                    result.rawPointCount = rawValues.size();
                    result.rawPreview = decimateToVariantList(rawValues, 1200);
                }
            }
            if (frame.harmonicPayloadSize > 0 && waveFile.seek(static_cast<qint64>(frame.harmonicPayloadOffset)))
            {
                const QByteArray harmonic = waveFile.read(static_cast<qint64>(frame.harmonicPayloadSize));
                if (harmonic.size() == static_cast<int>(frame.harmonicPayloadSize))
                {
                    const QVector<float> harmonicValues = VaporView::decodeTcpFloatPayload(harmonic, encoding);
                    if (i == 0)
                    {
                        result.harmonicPointCount = harmonicValues.size();
                        result.harmonicPreview = decimateToVariantList(harmonicValues, 1200);
                    }
                    result.peakValues.append(waveformPeakValue(harmonicValues, 0, 0));
                }
            }
            if ((i + 1) % progressInterval == 0 || (i + 1) == frameTotal)
            {
                const int progress = 38 + static_cast<int>(std::round(54.0 * static_cast<double>(i + 1) / static_cast<double>(std::max(1, frameTotal))));
                postLoadProgress(generation, progress, QStringLiteral("正在计算峰值趋势... %1/%2 帧").arg(i + 1).arg(frameTotal));
            }
        }
    }

    result.currentFrameIndex = frameTotal > 0 ? 0 : -1;
    result.ok = true;
    postLoadProgress(generation, 96, QStringLiteral("正在更新记录查看页面..."));
    return result;
}

void SessionBackend::rebuildTrendRangePreviews()
{
    auto decimateSlice = [](const QVariantList& full, int start, int end) -> QVariantList {
        QVariantList result;
        const int count = end - start + 1;
        if (count <= 0)
            return result;
        static constexpr int kMaxPreview = 1200;
        if (count <= kMaxPreview) {
            for (int i = start; i <= end; ++i)
                result.append(full.at(i));
            return result;
        }
        const int stride = count / kMaxPreview;
        for (int i = start; i <= end; i += stride)
            result.append(full.at(i));
        if (result.size() < 2)
            result.append(full.at(end));
        return result;
    };

    // Slice peak values: frame index maps directly
    if (!peak_trend_full_.isEmpty())
    {
        const int maxFrame = peak_trend_full_.size() - 1;
        trend_view_start_ = std::max(0, std::min(trend_view_start_, maxFrame));
        trend_view_end_ = std::max(0, std::min(trend_view_end_, maxFrame));
        if (trend_view_end_ < trend_view_start_)
            trend_view_end_ = trend_view_start_;
        peak_trend_range_preview_ = decimateSlice(peak_trend_full_, trend_view_start_, trend_view_end_);
    }

    // Map frame range to CSV time range for sensor trends
    if (!waveform_timestamps_us_.isEmpty() && !csv_timestamps_us_.isEmpty()
        && !temperature_full_.isEmpty())
    {
        const int tsSize = static_cast<int>(waveform_timestamps_us_.size());
        const int frameIdx = std::min(trend_view_start_, tsSize - 1);
        const int frameIdxEnd = std::min(trend_view_end_, tsSize - 1);
        const quint64 startTs = waveform_timestamps_us_.at(frameIdx);
        const quint64 endTs = waveform_timestamps_us_.at(frameIdxEnd);

        auto csvIt = std::lower_bound(csv_timestamps_us_.begin(), csv_timestamps_us_.end(), startTs);
        auto csvEndIt = std::upper_bound(csv_timestamps_us_.begin(), csv_timestamps_us_.end(), endTs);

        const int tempSize = static_cast<int>(temperature_full_.size());
        csv_range_start_row_ = static_cast<int>(std::distance(csv_timestamps_us_.begin(), csvIt));
        csv_range_end_row_ = static_cast<int>(std::distance(csv_timestamps_us_.begin(), csvEndIt)) - 1;
        csv_range_start_row_ = std::max(0, std::min(csv_range_start_row_, tempSize - 1));
        csv_range_end_row_ = std::max(csv_range_start_row_, std::min(csv_range_end_row_, tempSize - 1));

        temperature_range_preview_ = decimateSlice(temperature_full_, csv_range_start_row_, csv_range_end_row_);
        humidity_range_preview_ = decimateSlice(humidity_full_, csv_range_start_row_, csv_range_end_row_);
        pressure_range_preview_ = decimateSlice(pressure_full_, csv_range_start_row_, csv_range_end_row_);
    }
    else
    {
        csv_range_start_row_ = 0;
        csv_range_end_row_ = 0;
        temperature_range_preview_.clear();
        humidity_range_preview_.clear();
        pressure_range_preview_.clear();
    }
}

void SessionBackend::setTrendViewRange(int startFrame, int endFrame)
{
    trend_view_start_ = startFrame;
    trend_view_end_ = endFrame;
    rebuildTrendRangePreviews();
    emit trendViewRangeChanged();
}

void SessionBackend::loadSessionFrame(int frameIndex)
{
    if (loading_)
    {
        return;
    }
    const QString path = selected_session_.value(QStringLiteral("path")).toString();
    if (path.isEmpty())
    {
        return;
    }
    const int clampedIndex = std::clamp(frameIndex, 0, static_cast<int>(waveform_frame_index_.size()) - 1);
    const QVariantMap waveform = getWaveformFrame(QDir(path).filePath(QStringLiteral("raw/tcp_wave.dat")), clampedIndex);
    const QVariantList raw = waveform.value(QStringLiteral("raw")).toList();
    const QVariantList harmonic = waveform.value(QStringLiteral("harmonic")).toList();
    if (raw.isEmpty() && harmonic.isEmpty())
    {
        return;
    }
    current_frame_index_ = clampedIndex;
    waveform_raw_preview_ = raw;
    waveform_harmonic_preview_ = harmonic;
    waveform_preview_ = waveform_harmonic_preview_;
    waveform_raw_point_count_ = waveform.value(QStringLiteral("rawPointCount"), waveform_raw_preview_.size()).toInt();
    waveform_harmonic_point_count_ = waveform.value(QStringLiteral("harmonicPointCount"), waveform_harmonic_preview_.size()).toInt();

    // Release path: full 80-row CSV window
    updateCsvPreviewForTimestamp(waveform.value(QStringLiteral("timestampUs")).toULongLong(), kCsvFullPreviewRows);
    emit frameSelectionChanged();
    scheduleWaveformPrefetch(current_frame_index_);
}

void SessionBackend::clearWaveformCache()
{
    for (auto& entry : waveform_cache_)
        entry.frameIndex = -1;
    waveform_cache_pos_ = 0;
}

QVariantMap SessionBackend::getWaveformFrame(const QString& rawPath, int frameIndex)
{
    for (int i = 0; i < kWaveformCacheSize; ++i)
    {
        const auto& entry = waveform_cache_[i];
        if (entry.frameIndex == frameIndex && (entry.rawPointCount > 0 || entry.harmonicPointCount > 0))
        {
            QVariantMap result;
            result[QStringLiteral("raw")] = entry.rawPreview;
            result[QStringLiteral("harmonic")] = entry.harmonicPreview;
            result[QStringLiteral("rawPointCount")] = entry.rawPointCount;
            result[QStringLiteral("harmonicPointCount")] = entry.harmonicPointCount;
            result[QStringLiteral("timestampUs")] = QVariant::fromValue<qulonglong>(entry.timestampUs);
            return result;
        }
    }

    QVariantMap result = readWaveformFramePreview(rawPath, frameIndex);
    if (!result.isEmpty())
    {
        auto& entry = waveform_cache_[waveform_cache_pos_ % kWaveformCacheSize];
        entry.frameIndex = frameIndex;
        entry.rawPreview = result.value(QStringLiteral("raw")).toList();
        entry.harmonicPreview = result.value(QStringLiteral("harmonic")).toList();
        entry.rawPointCount = result.value(QStringLiteral("rawPointCount")).toInt();
        entry.harmonicPointCount = result.value(QStringLiteral("harmonicPointCount")).toInt();
        entry.timestampUs = result.value(QStringLiteral("timestampUs")).value<qulonglong>();
        ++waveform_cache_pos_;
    }
    return result;
}

void SessionBackend::setFrameCursor(int frameIndex)
{
    QElapsedTimer ft;
    ft.start();

    if (loading_)
    {
        return;
    }
    if (waveform_frame_index_.isEmpty())
    {
        return;
    }
    const int clampedIndex = std::clamp(frameIndex, 0, static_cast<int>(waveform_frame_index_.size()) - 1);

    // Duplicate frame: skip UI update
    if (clampedIndex == current_frame_index_ &&
        (!waveform_raw_preview_.isEmpty() || !waveform_harmonic_preview_.isEmpty()))
    {
        return;
    }

    const QString path = selected_session_.value(QStringLiteral("path")).toString();
    if (path.isEmpty())
    {
        return;
    }
    const QString rawPath = QDir(path).filePath(QStringLiteral("raw/tcp_wave.dat"));

    QElapsedTimer wft;
    wft.start();
    const QVariantMap waveform = getWaveformFrame(rawPath, clampedIndex);
    const qint64 waveformMs = wft.elapsed();

    if (!waveform.isEmpty())
    {
        waveform_raw_preview_ = waveform.value(QStringLiteral("raw")).toList();
        waveform_harmonic_preview_ = waveform.value(QStringLiteral("harmonic")).toList();
        waveform_preview_ = waveform_harmonic_preview_;
        waveform_raw_point_count_ = waveform.value(QStringLiteral("rawPointCount"), waveform_raw_preview_.size()).toInt();
        waveform_harmonic_point_count_ = waveform.value(QStringLiteral("harmonicPointCount"), waveform_harmonic_preview_.size()).toInt();
    }
    current_frame_index_ = clampedIndex;

    QElapsedTimer cft;
    cft.start();
    // Drag path: only 16 CSV rows for lightweight update
    if (current_frame_index_ >= 0 && current_frame_index_ < waveform_timestamps_us_.size())
    {
        const quint64 ts = waveform_timestamps_us_.at(current_frame_index_);
        updateCsvPreviewForTimestamp(ts, kCsvDragPreviewRows);
    }
    else
    {
        csv_preview_rows_.clear();
        current_csv_row_ = -1;
        secondary_csv_row_ = -1;
    }
    const qint64 csvMs = cft.elapsed();

    QElapsedTimer eft;
    eft.start();
    emit frameSelectionChanged();
    const qint64 emitMs = eft.elapsed();
    const qint64 totalMs = ft.elapsed();

    static constexpr bool kSessionSliderPerfLog = true;
    if constexpr (kSessionSliderPerfLog)
    {
        qDebug().noquote() << QStringLiteral("[SessionSlider] frame %1  waveform %2ms  csv %3ms  emit %4ms  total %5ms")
            .arg(clampedIndex)
            .arg(waveformMs)
            .arg(csvMs)
            .arg(emitMs)
            .arg(totalMs);
    }
}

bool SessionBackend::waveformFrameCached(int frameIndex) const
{
    for (int i = 0; i < kWaveformCacheSize; ++i)
    {
        const auto& entry = waveform_cache_[i];
        if (entry.frameIndex == frameIndex &&
            (entry.rawPointCount > 0 || entry.harmonicPointCount > 0))
        {
            return true;
        }
    }
    return false;
}

void SessionBackend::scheduleWaveformPrefetch(int centerFrameIndex)
{
    pending_prefetch_frame_index_ = centerFrameIndex;
    waveform_prefetch_timer_.start();
}

void SessionBackend::prefetchWaveformFrames(int centerFrameIndex)
{
    if (loading_ || waveform_frame_index_.isEmpty())
    {
        return;
    }

    const QString path = selected_session_.value(QStringLiteral("path")).toString();
    if (path.isEmpty())
    {
        return;
    }

    const QString rawPath = QDir(path).filePath(QStringLiteral("raw/tcp_wave.dat"));
    static const int offsets[] = {1, -1, 2, -2, 3, -3};

    for (int offset : offsets)
    {
        const int index = centerFrameIndex + offset;
        if (index < 0 || index >= waveform_frame_index_.size())
        {
            continue;
        }

        if (waveformFrameCached(index))
        {
            continue;
        }

        getWaveformFrame(rawPath, index);
    }
}

int SessionBackend::findClosestCsvRow(quint64 timestampUs) const
{
    if (timestampUs == 0 || csv_timestamps_us_.isEmpty())
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
        ? timestampUs - csv_timestamps_us_.at(lowerIndex)
        : csv_timestamps_us_.at(lowerIndex) - timestampUs;
    const quint64 upperDelta = timestampUs >= csv_timestamps_us_.at(upperIndex)
        ? timestampUs - csv_timestamps_us_.at(upperIndex)
        : csv_timestamps_us_.at(upperIndex) - timestampUs;
    return lowerDelta <= upperDelta ? lowerIndex : upperIndex;
}

QVector<int> SessionBackend::closestCsvRows(quint64 timestampUs) const
{
    QVector<int> rows;
    if (csv_timestamps_us_.isEmpty())
    {
        return rows;
    }

    const int primary = findClosestCsvRow(timestampUs);
    if (primary < 0)
    {
        return rows;
    }

    rows.push_back(primary);

    // Pick the closer adjacent row as secondary
    const int size = static_cast<int>(csv_timestamps_us_.size());
    if (primary == 0)
    {
        if (size > 1)
            rows.push_back(1);
    }
    else if (primary == size - 1)
    {
        rows.push_back(primary - 1);
    }
    else
    {
        const quint64 leftTs = csv_timestamps_us_.at(primary - 1);
        const quint64 rightTs = csv_timestamps_us_.at(primary + 1);
        const quint64 leftDelta = timestampUs > leftTs ? timestampUs - leftTs : leftTs - timestampUs;
        const quint64 rightDelta = timestampUs > rightTs ? timestampUs - rightTs : rightTs - timestampUs;
        rows.push_back(leftDelta <= rightDelta ? primary - 1 : primary + 1);
    }

    return rows;
}

void SessionBackend::updateCsvPreviewForTimestamp(quint64 timestampUs)
{
    current_csv_row_ = -1;
    secondary_csv_row_ = -1;
    csv_preview_rows_.clear();
    const QVector<int> highlightedRows = closestCsvRows(timestampUs);
    if (!highlightedRows.isEmpty())
    {
        current_csv_row_ = highlightedRows.value(0, -1);
        secondary_csv_row_ = highlightedRows.value(1, -1);
    }

    const QSet<int> highlightedSet(highlightedRows.begin(), highlightedRows.end());
    const int totalRows = static_cast<int>(csv_rows_all_.size());
    const int windowSize = 300;
    const int contextRows = 30;
    const int lowestHighlighted = highlightedRows.isEmpty() ? 0 :
        (highlightedRows.size() > 1 ? std::min(highlightedRows.first(), highlightedRows.last()) : highlightedRows.first());
    const int startRow = std::max(0, lowestHighlighted - contextRows);
    const int endRow = std::min(startRow + windowSize, totalRows);
    for (int i = startRow; i < endRow; ++i)
    {
        int rank = -1;
        if (highlightedSet.contains(i))
        {
            rank = highlightedRows.indexOf(i);
        }
        QVariantMap row = csv_rows_all_.at(i).toMap();
        row[QStringLiteral("_matchRank")] = rank;
        if (timestampUs > 0 && i < csv_timestamps_us_.size())
        {
            const qint64 deltaUs = static_cast<qint64>(csv_timestamps_us_.at(i)) - static_cast<qint64>(timestampUs);
            const double deltaMs = static_cast<double>(deltaUs) / 1000.0;
            const QString deltaText = QStringLiteral("%1%2 ms")
                .arg(deltaMs >= 0.0 ? QStringLiteral("+") : QString())
                .arg(deltaMs, 0, 'f', 3);
            row[QStringLiteral("时间偏差")] = deltaText;
            row[QStringLiteral("_deltaText")] = deltaText;
        }
        csv_preview_rows_.append(row);
    }
    ++csv_preview_generation_;
}

void SessionBackend::updateCsvPreviewForTimestamp(quint64 timestampUs, int maxRows)
{
    current_csv_row_ = -1;
    secondary_csv_row_ = -1;
    csv_preview_rows_.clear();

    if (timestampUs == 0 || csv_rows_all_.isEmpty())
    {
        ++csv_preview_generation_;
        return;
    }

    const QVector<int> highlightedRows = closestCsvRows(timestampUs);
    if (!highlightedRows.isEmpty())
    {
        current_csv_row_ = highlightedRows.value(0, -1);
        secondary_csv_row_ = highlightedRows.value(1, -1);
    }

    const int totalRows = static_cast<int>(csv_rows_all_.size());
    // Start at the smaller of the two highlighted rows so both appear within the first 2 positions
    int startRow = 0;
    if (current_csv_row_ >= 0)
    {
        startRow = current_csv_row_;
        if (secondary_csv_row_ >= 0)
            startRow = std::min(current_csv_row_, secondary_csv_row_);
    }
    const int safeMaxRows = std::max(1, maxRows);
    const int endRow = std::min(startRow + safeMaxRows, totalRows);
    const QSet<int> highlightedSet(highlightedRows.begin(), highlightedRows.end());

    for (int i = startRow; i < endRow; ++i)
    {
        int rank = -1;
        if (highlightedSet.contains(i))
            rank = highlightedRows.indexOf(i);

        QVariantMap row = csv_rows_all_.at(i).toMap();
        row[QStringLiteral("_matchRank")] = rank;

        if (i < csv_timestamps_us_.size())
        {
            const qint64 deltaUs = static_cast<qint64>(csv_timestamps_us_.at(i)) - static_cast<qint64>(timestampUs);
            const double deltaMs = static_cast<double>(deltaUs) / 1000.0;
            const QString deltaText = QStringLiteral("%1%2 ms")
                .arg(deltaMs >= 0.0 ? QStringLiteral("+") : QString())
                .arg(deltaMs, 0, 'f', 3);
            row[QStringLiteral("时间偏差")] = deltaText;
            row[QStringLiteral("_deltaText")] = deltaText;
        }

        csv_preview_rows_.append(row);
    }
    ++csv_preview_generation_;
}

QVariantList SessionBackend::readCsvPreview(const QString& csvPath, int maxRows) const
{
    QVariantList rows;
    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return rows;
    }
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    const QStringList headers = parseCsvLine(in.readLine());
    const int timestampIndex = findHeaderIndex(headers, {QStringLiteral("record_timestamp_us"), QStringLiteral("epsilon_host_timestamp_us")});
    quint64 firstTimestampUs = 0;
    int count = 0;
    while (!in.atEnd() && count < maxRows)
    {
        const QStringList fields = parseCsvLine(in.readLine());
        if (fields.isEmpty())
        {
            continue;
        }
        QVariantMap row;
        row[QStringLiteral("#")] = count + 1;
        double deltaSeconds = 0.0;
        if (timestampIndex >= 0 && timestampIndex < fields.size())
        {
            bool ok = false;
            const quint64 timestampUs = fields.at(timestampIndex).toULongLong(&ok);
            if (ok)
            {
                if (firstTimestampUs == 0)
                {
                    firstTimestampUs = timestampUs;
                }
                deltaSeconds = static_cast<double>(timestampUs - firstTimestampUs) / 1000000.0;
                row[QStringLiteral("record_timestamp_utc")] =
                    QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(timestampUs / 1000), Qt::UTC).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
            }
        }
        row[QStringLiteral("时间偏差")] = QString::number(deltaSeconds, 'f', 3);
        for (int i = 0; i < std::min(headers.size(), fields.size()); ++i)
        {
            row[headers.at(i)] = fields.at(i);
        }
        rows.append(row);
        ++count;
    }
    return rows;
}

QVariantMap SessionBackend::readWaveformPreviews(const QString& rawPath)
{
    QVariantMap result;
    QVariantList rawPreview;
    QVariantList harmonicPreview;
    QVariantList peakPreview;
    waveform_index_path_.clear();
    waveform_frame_index_.clear();

    QFile file(rawPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        return result;
    }
    if (file.size() <= static_cast<qint64>(sizeof(UnifiedRawFileHeader)))
    {
        return result;
    }
    UnifiedRawFileHeader fileHeader{};
    if (file.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader)) != static_cast<qint64>(sizeof(fileHeader)) ||
        std::memcmp(fileHeader.magic, kUnifiedRawMagic, sizeof(fileHeader.magic)) != 0)
    {
        return result;
    }
    const quint32 fileHeaderSize = qFromLittleEndian(fileHeader.header_size);
    const quint16 sourceId = qFromLittleEndian(fileHeader.source_id);
    if (fileHeaderSize < sizeof(UnifiedRawFileHeader) || sourceId != kRawSourceTcpWave)
    {
        return result;
    }
    if (fileHeaderSize > sizeof(UnifiedRawFileHeader) && !file.seek(fileHeaderSize))
    {
        return result;
    }
    while (!file.atEnd())
    {
        const qint64 recordStart = file.pos();
        UnifiedRawRecordHeader header{};
        const qint64 headerBytes = file.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (headerBytes == 0)
        {
            break;
        }
        if (headerBytes != static_cast<qint64>(sizeof(header)))
        {
            break;
        }
        const quint32 marker = qFromLittleEndian(header.marker);
        const quint32 recordHeaderSize = qFromLittleEndian(header.header_size);
        const quint64 timestampUs = qFromLittleEndian(header.host_timestamp_us);
        const quint32 payloadSize = qFromLittleEndian(header.payload_size);
        const quint16 recordSourceId = qFromLittleEndian(header.source_id);
        const quint32 flags = qFromLittleEndian(header.flags);
        if (marker != kUnifiedRawRecordMarker || recordHeaderSize < sizeof(UnifiedRawRecordHeader))
        {
            break;
        }

        const qint64 payloadOffset = recordStart + static_cast<qint64>(recordHeaderSize);
        const qint64 nextRecord = payloadOffset + static_cast<qint64>(payloadSize);
        if (!file.seek(payloadOffset))
        {
            break;
        }

        if (recordSourceId == kRawSourceTcpWave && (flags & kRawTcpWaveCombinedPayloadFlag) != 0 && payloadSize >= sizeof(quint32) * 2)
        {
            quint32 rawSizeLe = 0;
            quint32 harmonicSizeLe = 0;
            if (file.read(reinterpret_cast<char*>(&rawSizeLe), sizeof(rawSizeLe)) != static_cast<qint64>(sizeof(rawSizeLe)) ||
                file.read(reinterpret_cast<char*>(&harmonicSizeLe), sizeof(harmonicSizeLe)) != static_cast<qint64>(sizeof(harmonicSizeLe)))
            {
                break;
            }

            const quint32 rawSize = qFromLittleEndian(rawSizeLe);
            const quint32 harmonicSize = qFromLittleEndian(harmonicSizeLe);
            const quint64 requiredPayloadSize = static_cast<quint64>(sizeof(quint32) * 2) + rawSize + harmonicSize;
            if (requiredPayloadSize <= payloadSize && harmonicSize > 0 && harmonicSize % kFloatSize == 0)
            {
                WaveformFrameIndex frame;
                frame.rawPayloadOffset = static_cast<quint64>(payloadOffset) + sizeof(quint32) * 2ULL;
                frame.rawPayloadSize = rawSize;
                frame.harmonicPayloadOffset = frame.rawPayloadOffset + rawSize;
                frame.harmonicPayloadSize = harmonicSize;
                frame.flags = flags;
                frame.timestampUs = timestampUs;
                waveform_frame_index_.push_back(frame);

                const VaporView::TcpFloatEncoding encoding = VaporView::tcpFloatEncodingFromRawDatFlags(flags);
                if ((rawPreview.isEmpty() || harmonicPreview.isEmpty()) && rawSize > 0)
                {
                    if (!file.seek(static_cast<qint64>(frame.rawPayloadOffset)))
                    {
                        break;
                    }
                    const QByteArray raw = file.read(static_cast<qint64>(rawSize));
                    if (raw.size() == static_cast<int>(rawSize) && rawPreview.isEmpty())
                    {
                        rawPreview = decimateToVariantList(VaporView::decodeTcpFloatPayload(raw, encoding), 800);
                    }
                }

                if (!file.seek(static_cast<qint64>(frame.harmonicPayloadOffset)))
                {
                    break;
                }
                const QByteArray harmonic = file.read(static_cast<qint64>(harmonicSize));
                if (harmonic.size() == static_cast<int>(harmonicSize))
                {
                    const auto harmonicValues = VaporView::decodeTcpFloatPayload(harmonic, encoding);
                    if (harmonicPreview.isEmpty())
                    {
                        harmonicPreview = decimateToVariantList(harmonicValues, 800);
                    }
                    if (peakPreview.size() < 600)
                    {
                        const float peak = waveformPeakValue(harmonicValues, 0, 0);
                        if (std::isfinite(peak))
                        {
                            peakPreview.append(peak);
                        }
                    }
                }
            }
        }

        if (!file.seek(nextRecord))
        {
            break;
        }
    }
    if (!waveform_frame_index_.isEmpty())
    {
        waveform_index_path_ = QDir::fromNativeSeparators(QFileInfo(rawPath).absoluteFilePath());
    }
    result[QStringLiteral("raw")] = rawPreview;
    result[QStringLiteral("harmonic")] = harmonicPreview;
    result[QStringLiteral("peak")] = peakPreview;
    result[QStringLiteral("frameCount")] = waveform_frame_index_.size();
    return result;
}

QVariantMap SessionBackend::readWaveformFramePreview(const QString& rawPath, int frameIndex) const
{
    QVariantMap result;
    const QString normalizedRawPath = QDir::fromNativeSeparators(QFileInfo(rawPath).absoluteFilePath());
    if (waveform_frame_index_.isEmpty() || waveform_index_path_ != normalizedRawPath)
    {
        return result;
    }
    const int targetIndex = std::clamp(frameIndex, 0, static_cast<int>(waveform_frame_index_.size()) - 1);
    const WaveformFrameIndex& frame = waveform_frame_index_.at(targetIndex);
    QFile file(rawPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        return result;
    }
    const VaporView::TcpFloatEncoding encoding = VaporView::tcpFloatEncodingFromRawDatFlags(frame.flags);
    if (frame.rawPayloadSize > 0 && file.seek(static_cast<qint64>(frame.rawPayloadOffset)))
    {
        const QByteArray raw = file.read(static_cast<qint64>(frame.rawPayloadSize));
        if (raw.size() == static_cast<int>(frame.rawPayloadSize))
        {
            const QVector<float> values = VaporView::decodeTcpFloatPayload(raw, encoding);
            result[QStringLiteral("rawPointCount")] = values.size();
            result[QStringLiteral("raw")] = decimateToVariantList(values, 1200);
        }
    }
    if (frame.harmonicPayloadSize > 0 && file.seek(static_cast<qint64>(frame.harmonicPayloadOffset)))
    {
        const QByteArray harmonic = file.read(static_cast<qint64>(frame.harmonicPayloadSize));
        if (harmonic.size() == static_cast<int>(frame.harmonicPayloadSize))
        {
            const QVector<float> values = VaporView::decodeTcpFloatPayload(harmonic, encoding);
            result[QStringLiteral("harmonicPointCount")] = values.size();
            result[QStringLiteral("harmonic")] = decimateToVariantList(values, 1200);
        }
    }
    result[QStringLiteral("timestampUs")] = QVariant::fromValue<qulonglong>(frame.timestampUs);
    return result;
}

QVariantMap SessionBackend::countSensorRowsAndRange(const QString& csvPath) const
{
    QVariantMap result;
    result[QStringLiteral("rows")] = 0;
    result[QStringLiteral("firstTimestampUs")] = 0;
    result[QStringLiteral("lastTimestampUs")] = 0;

    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return result;
    }
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    if (in.atEnd())
    {
        return result;
    }

    const QStringList headers = parseCsvLine(in.readLine());
    const int timestampIndex = findHeaderIndex(headers, {
        QStringLiteral("record_timestamp_us"),
        QStringLiteral("epsilon_host_timestamp_us"),
        QStringLiteral("rtk_timestamp_us")
    });

    int rows = 0;
    quint64 firstTimestampUs = 0;
    quint64 lastTimestampUs = 0;
    while (!in.atEnd())
    {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty())
        {
            continue;
        }
        const QStringList fields = parseCsvLine(line);
        ++rows;
        if (timestampIndex >= 0 && timestampIndex < fields.size())
        {
            bool ok = false;
            const quint64 timestampUs = fields.at(timestampIndex).toULongLong(&ok);
            if (ok && timestampUs > 0)
            {
                if (firstTimestampUs == 0)
                {
                    firstTimestampUs = timestampUs;
                }
                lastTimestampUs = timestampUs;
            }
        }
    }

    result[QStringLiteral("rows")] = rows;
    result[QStringLiteral("firstTimestampUs")] = firstTimestampUs;
    result[QStringLiteral("lastTimestampUs")] = lastTimestampUs;
    return result;
}

int SessionBackend::countWaveformFrames(const QString& rawPath) const
{
    QFile file(rawPath);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= static_cast<qint64>(sizeof(UnifiedRawFileHeader)))
    {
        return 0;
    }
    UnifiedRawFileHeader fileHeader{};
    if (file.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader)) != static_cast<qint64>(sizeof(fileHeader)) ||
        std::memcmp(fileHeader.magic, kUnifiedRawMagic, sizeof(fileHeader.magic)) != 0)
    {
        return 0;
    }
    const quint32 fileHeaderSize = qFromLittleEndian(fileHeader.header_size);
    if (fileHeaderSize > sizeof(UnifiedRawFileHeader) && !file.seek(fileHeaderSize))
    {
        return 0;
    }

    int frames = 0;
    while (!file.atEnd())
    {
        UnifiedRawRecordHeader header{};
        if (file.read(reinterpret_cast<char*>(&header), sizeof(header)) != static_cast<qint64>(sizeof(header)))
        {
            break;
        }
        const quint32 marker = qFromLittleEndian(header.marker);
        const quint32 recordHeaderSize = qFromLittleEndian(header.header_size);
        if (marker != kUnifiedRawRecordMarker || recordHeaderSize < sizeof(UnifiedRawRecordHeader))
        {
            break;
        }
        if (recordHeaderSize > sizeof(UnifiedRawRecordHeader) && !file.seek(file.pos() + recordHeaderSize - sizeof(UnifiedRawRecordHeader)))
        {
            break;
        }
        const quint32 payloadSize = qFromLittleEndian(header.payload_size);
        const quint16 sourceId = qFromLittleEndian(header.source_id);
        const quint32 flags = qFromLittleEndian(header.flags);
        if (sourceId == kRawSourceTcpWave && (flags & kRawTcpWaveCombinedPayloadFlag) != 0)
        {
            ++frames;
        }
        if (!file.seek(file.pos() + payloadSize))
        {
            break;
        }
    }
    return frames;
}

QVariantMap SessionBackend::readSensorTrendPreviews(const QString& csvPath, int maxRows) const
{
    QVariantMap result;
    QVector<double> temperatures;
    QVector<double> humidities;
    QVector<double> pressures;
    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return result;
    }
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    if (in.atEnd())
    {
        return result;
    }

    const QStringList headers = parseCsvLine(in.readLine());
    const int temperatureIndex = findHeaderIndex(headers, {QStringLiteral("hmp_temperature_c"), QStringLiteral("temp_c")});
    const int humidityIndex = findHeaderIndex(headers, {QStringLiteral("hmp_humidity_rh"), QStringLiteral("humidity_rh")});
    const int pressureIndex = findHeaderIndex(headers, {QStringLiteral("ptb_pressure_hpa"), QStringLiteral("baro_hpa"), QStringLiteral("baro_pa")});
    const int thValidIndex = findHeaderIndex(headers, {QStringLiteral("th_valid")});
    const int baroValidIndex = findHeaderIndex(headers, {QStringLiteral("baro_valid")});
    const bool pressureIsPa = pressureIndex >= 0 && headers.value(pressureIndex).trimmed().toLower().endsWith(QStringLiteral("_pa"));

    while (!in.atEnd() && temperatures.size() < maxRows)
    {
        const QStringList fields = parseCsvLine(in.readLine());
        const bool thValid = thValidIndex < 0 || parseBooleanCsvField(fields.value(thValidIndex), true);
        const bool baroValid = baroValidIndex < 0 || parseBooleanCsvField(fields.value(baroValidIndex), true);

        temperatures.push_back((temperatureIndex >= 0 && thValid) ? parseOptionalDouble(fields.value(temperatureIndex)) : std::numeric_limits<double>::quiet_NaN());
        humidities.push_back((humidityIndex >= 0 && thValid) ? parseOptionalDouble(fields.value(humidityIndex)) : std::numeric_limits<double>::quiet_NaN());

        double pressure = (pressureIndex >= 0 && baroValid) ? parseOptionalDouble(fields.value(pressureIndex)) : std::numeric_limits<double>::quiet_NaN();
        if (std::isfinite(pressure) && pressureIsPa)
        {
            pressure /= 100.0;
        }
        pressures.push_back(pressure);
    }

    result[QStringLiteral("temperature")] = decimateToVariantList(temperatures, 260);
    result[QStringLiteral("humidity")] = decimateToVariantList(humidities, 260);
    result[QStringLiteral("pressure")] = decimateToVariantList(pressures, 260);
    return result;
}

RawParserBackend::RawParserBackend(QObject *parent)
    : QObject(parent)
    , status_text_(QStringLiteral("No raw file loaded."))
{
}

QString RawParserBackend::rawFilePath() const { return raw_file_path_; }
QVariantList RawParserBackend::records() const { return records_; }
QVariantMap RawParserBackend::selectedRecord() const { return selected_record_; }
QString RawParserBackend::statusText() const { return status_text_; }

void RawParserBackend::openRawFile(const QString& path)
{
    QString normalized = QDir::fromNativeSeparators(path.trimmed());
    if (normalized.startsWith(QStringLiteral("file:///")))
    {
        normalized = QUrl(normalized).toLocalFile();
    }
    loadRawFile(normalized);
}

void RawParserBackend::openSessionPath(const QString& path)
{
    const QDir dir(QDir::fromNativeSeparators(path));
    const QStringList candidates = {
        dir.filePath(QStringLiteral("raw/epsilon.dat")),
        dir.filePath(QStringLiteral("raw/ptb.dat")),
        dir.filePath(QStringLiteral("raw/hmp.dat")),
        dir.filePath(QStringLiteral("raw/lidar.dat")),
        dir.filePath(QStringLiteral("raw/tcp_wave.dat")),
    };
    for (const QString& candidate : candidates)
    {
        if (QFileInfo::exists(candidate))
        {
            loadRawFile(candidate);
            return;
        }
    }
    emit notificationRequested(QStringLiteral("warning"), QStringLiteral("No raw DAT file found in session."));
}

void RawParserBackend::selectRecord(int index)
{
    if (index < 0 || index >= records_.size())
    {
        return;
    }
    selected_record_ = records_.at(index).toMap();
    emit selectedRecordChanged();
}

void RawParserBackend::clear()
{
    raw_file_path_.clear();
    records_.clear();
    selected_record_.clear();
    status_text_ = QStringLiteral("No raw file loaded.");
    emit recordsChanged();
    emit selectedRecordChanged();
}

QString RawParserBackend::sourceName(int sourceId) const
{
    switch (sourceId)
    {
    case kRawSourceEpsilon: return QStringLiteral("EPSILON");
    case kRawSourcePtb: return QStringLiteral("PTB210");
    case kRawSourceHmp: return QStringLiteral("HMP3");
    case kRawSourceLidar: return QStringLiteral("TFA1500-L");
    case kRawSourceTcpWave: return QStringLiteral("TCP Wave");
    default: return QStringLiteral("Unknown");
    }
}

QString RawParserBackend::recordTypeName(int sourceId, int recordType) const
{
    if (sourceId == kRawSourceTcpWave) return QStringLiteral("Combined waveform");
    if (sourceId == kRawSourceHmp) return QStringLiteral("Modbus 0x%1").arg(recordType, 2, 16, QLatin1Char('0'));
    if (sourceId == kRawSourceEpsilon) return QStringLiteral("Packet 0x%1").arg(recordType, 2, 16, QLatin1Char('0')).toUpper();
    return QStringLiteral("0x%1").arg(recordType, 2, 16, QLatin1Char('0')).toUpper();
}

bool RawParserBackend::loadRawFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        emit notificationRequested(QStringLiteral("error"), QStringLiteral("Failed to open raw DAT file."));
        return false;
    }
    UnifiedRawFileHeader fileHeader{};
    if (file.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader)) != static_cast<qint64>(sizeof(fileHeader)) ||
        std::memcmp(fileHeader.magic, kUnifiedRawMagic, sizeof(fileHeader.magic)) != 0)
    {
        emit notificationRequested(QStringLiteral("error"), QStringLiteral("Invalid VaporView raw DAT file."));
        return false;
    }
    const int fileSource = qFromLittleEndian(fileHeader.source_id);
    records_.clear();
    int row = 0;
    while (!file.atEnd() && row < 5000)
    {
        const qint64 recordOffset = file.pos();
        UnifiedRawRecordHeader header{};
        if (file.read(reinterpret_cast<char*>(&header), sizeof(header)) != static_cast<qint64>(sizeof(header)))
        {
            break;
        }
        const quint32 marker = qFromLittleEndian(header.marker);
        if (marker != kUnifiedRawRecordMarker)
        {
            break;
        }
        const quint32 payloadSize = qFromLittleEndian(header.payload_size);
        const int sourceId = qFromLittleEndian(header.source_id);
        const int recordType = qFromLittleEndian(header.record_type);
        const quint32 flags = qFromLittleEndian(header.flags);
        const quint64 hostTimestampUs = qFromLittleEndian(header.host_timestamp_us);
        const quint64 sequence = qFromLittleEndian(header.sequence);
        const QByteArray payload = file.read(payloadSize);
        QVariantMap record;
        record[QStringLiteral("index")] = row + 1;
        record[QStringLiteral("offset")] = recordOffset;
        record[QStringLiteral("source")] = sourceName(sourceId);
        record[QStringLiteral("sourceId")] = sourceId;
        record[QStringLiteral("recordType")] = QStringLiteral("0x%1").arg(recordType, 2, 16, QLatin1Char('0')).toUpper();
        record[QStringLiteral("recordTypeName")] = recordTypeName(sourceId, recordType);
        record[QStringLiteral("flags")] = QStringLiteral("0x%1").arg(flags, 8, 16, QLatin1Char('0')).toUpper();
        record[QStringLiteral("timestampUs")] = QString::number(hostTimestampUs);
        record[QStringLiteral("time")] = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(hostTimestampUs / 1000), Qt::UTC).toString(Qt::ISODateWithMs);
        record[QStringLiteral("payloadSize")] = payload.size();
        record[QStringLiteral("sequence")] = QString::number(sequence);
        record[QStringLiteral("payloadHex")] = QString::fromLatin1(payload.left(64).toHex(' '));
        records_.append(record);
        ++row;
    }
    raw_file_path_ = QDir::fromNativeSeparators(path);
    status_text_ = QStringLiteral("Loaded %1 records from %2 (%3)").arg(records_.size()).arg(QFileInfo(path).fileName()).arg(sourceName(fileSource));
    selected_record_ = records_.isEmpty() ? QVariantMap{} : records_.first().toMap();
    emit recordsChanged();
    emit selectedRecordChanged();
    return true;
}

bool RawParserBackend::exportListCsv(const QString& path) const
{
    QFile file(QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return false;
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << "index,time,timestamp_us,source,record_type,flags,payload_size,sequence\n";
    for (const QVariant& item : records_)
    {
        const QVariantMap r = item.toMap();
        out << r.value("index").toInt() << ','
            << csvEscape(r.value("time").toString()) << ','
            << csvEscape(r.value("timestampUs").toString()) << ','
            << csvEscape(r.value("source").toString()) << ','
            << csvEscape(r.value("recordType").toString()) << ','
            << csvEscape(r.value("flags").toString()) << ','
            << r.value("payloadSize").toInt() << ','
            << csvEscape(r.value("sequence").toString()) << '\n';
    }
    return true;
}

bool RawParserBackend::exportSelectedPayload(const QString& path) const
{
    if (selected_record_.isEmpty()) return false;
    QFile in(raw_file_path_);
    QFile out(QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path);
    if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const qint64 offset = selected_record_.value(QStringLiteral("offset")).toLongLong();
    in.seek(offset);
    UnifiedRawRecordHeader header{};
    if (in.read(reinterpret_cast<char*>(&header), sizeof(header)) != static_cast<qint64>(sizeof(header))) return false;
    const quint32 payloadSize = qFromLittleEndian(header.payload_size);
    out.write(in.read(payloadSize));
    return true;
}

bool RawParserBackend::exportDecodedJson(const QString& path) const
{
    QJsonArray array;
    for (const QVariant& item : records_)
    {
        array.append(QJsonObject::fromVariantMap(item.toMap()));
    }
    QFile file(QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    return true;
}

SettingsBackend::SettingsBackend(AppBackend *appBackend, RecordingBackend *recordingBackend, QObject *parent)
    : QObject(parent)
    , app_backend_(appBackend)
    , recording_backend_(recordingBackend)
{
    connect(recording_backend_, &RecordingBackend::recordingDirectoryChanged, this, &SettingsBackend::recordDirectoryChanged);
}

QString SettingsBackend::recordDirectory() const
{
    return recording_backend_->recordingDirectory();
}

QString SettingsBackend::aboutText() const
{
    return QStringLiteral("VaporView 1.0.0 - Airborne Water Vapor Detection");
}

void SettingsBackend::setRecordDirectory(const QString& directory)
{
    recording_backend_->setRecordingDirectory(directory);
}

void SettingsBackend::save()
{
    emit notificationRequested(QStringLiteral("info"), QStringLiteral("Settings saved."));
}

void SettingsBackend::reset()
{
    app_backend_->setEnglish(false);
    app_backend_->setDark(false);
    app_backend_->setFontScale(100);
    app_backend_->saveIconLibrary(QStringLiteral("lucide"));
    emit notificationRequested(QStringLiteral("info"), QStringLiteral("Settings reset."));
}
